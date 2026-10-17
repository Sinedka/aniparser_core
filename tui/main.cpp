#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <notcurses/notcurses.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CacheReader.hpp"

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

constexpr uint64_t color_ink = 0x10151d;
constexpr uint64_t color_panel = 0x1a222d;
constexpr uint64_t color_accent = 0x70d6c1;
constexpr uint64_t color_muted = 0x93a2b4;

Json handle_json(shm::HeapHandle handle) {
  if (!handle) return nullptr;
  return {{"segment_id", handle.segment_id}, {"offset", handle.offset}};
}

shm::HeapHandle parse_handle(const Json &value) {
  if (!value.is_object()) return {};
  return {value.value("segment_id", shm::invalid_segment_id),
          value.value("offset", shm::invalid_offset)};
}

std::string shared_string(const CacheReader &cache,
                          const shm::SharedString &value) {
  return cache.string(value);
}

struct AnimeRow {
  shm::HeapHandle handle{};
  int64_t id{};
  std::string title;
  std::string image;
  std::string info;
  std::string description;
  std::string section;
};

AnimeRow anime_row(const CacheReader &cache,
                   shm::HeapHandle handle,
                   std::string section = {}) {
  AnimeRow row{.handle = handle, .section = std::move(section)};
  const auto* anime = cache.anime(handle);
  if (!anime) {
    row.title = "Anime unavailable [segment " +
                std::to_string(handle.segment_id) + ", offset " +
                std::to_string(handle.offset) + "]";
    return row;
  }
  row.id = anime->anime_id;
  row.title = shared_string(cache, anime->title);
  if (row.title.empty())
    row.title = shared_string(cache, anime->anime_status.title);
  if (row.title.empty()) row.title = "Anime " + std::to_string(row.id);
  row.description = shared_string(cache, anime->description);
  for (const auto* poster : {&anime->poster.medium, &anime->poster.big,
                             &anime->poster.small, &anime->poster.fullsize}) {
    row.image = shared_string(cache, *poster);
    if (!row.image.empty()) break;
  }
  const auto type = shared_string(cache, anime->type.name);
  row.info = type;
  if (anime->year > 0) {
    if (!row.info.empty()) row.info += "  ·  ";
    row.info += std::to_string(anime->year);
  }
  if (anime->rating.average > 0) {
    if (!row.info.empty()) row.info += "  ·  ";
    row.info += "★ " + std::to_string(anime->rating.average);
  }
  if (anime->episodes.count > 0) {
    if (!row.info.empty()) row.info += "  ·  ";
    row.info += std::to_string(anime->episodes.count) + " eps";
  }
  return row;
}

class IpcClient {
  public:
  ~IpcClient() {
    if (fd_ >= 0) ::close(fd_);
  }

  bool connect_to(const std::string &path) {
    if (fd_ >= 0) return path_ == path;
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) return false;
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    path_ = path;
    return true;
  }

  const std::string &socket_path() const { return path_; }
  bool connected() const { return fd_ >= 0; }

  Json request(std::string method, Json fields = Json::object()) {
    const auto id = ++next_id_;
    Json request{{"id", id}, {"method", std::move(method)}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
      request[it.key()] = it.value();
    auto bytes = request.dump();
    bytes.push_back('\n');
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const auto n =
          ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
      if (n <= 0) throw std::runtime_error("IPC write failed");
      sent += static_cast<std::size_t>(n);
    }
    while (true) {
      auto message = read_message();
      if (message.value("id", int64_t{-1}) == id && !message.contains("event"))
        return message;
      inbox_.push_back(std::move(message));
    }
  }

  std::vector<Json> poll_messages() {
    std::vector<Json> result;
    while (!inbox_.empty()) {
      result.push_back(std::move(inbox_.front()));
      inbox_.pop_front();
    }
    while (true) {
      const auto newline = input_.find('\n');
      if (newline == std::string::npos) break;
      auto line = input_.substr(0, newline);
      input_.erase(0, newline + 1);
      result.push_back(Json::parse(line));
    }
    while (true) {
      pollfd pfd{fd_, POLLIN, 0};
      if (::poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) break;
      read_available();
      while (!parsed_.empty()) {
        result.push_back(std::move(parsed_.front()));
        parsed_.pop_front();
      }
    }
    return result;
  }

  private:
  Json read_message() {
    while (true) {
      const auto newline = input_.find('\n');
      if (newline != std::string::npos) {
        auto line = input_.substr(0, newline);
        input_.erase(0, newline + 1);
        return Json::parse(line);
      }
      char buffer[4096];
      const auto count = ::recv(fd_, buffer, sizeof(buffer), 0);
      if (count <= 0) throw std::runtime_error("IPC connection closed");
      input_.append(buffer, static_cast<std::size_t>(count));
    }
  }

  void read_available() {
    char buffer[8192];
    const auto count = ::recv(fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count <= 0) return;
    input_.append(buffer, static_cast<std::size_t>(count));
    for (;;) {
      const auto newline = input_.find('\n');
      if (newline == std::string::npos) break;
      auto line = input_.substr(0, newline);
      input_.erase(0, newline + 1);
      parsed_.push_back(Json::parse(line));
    }
  }

  int fd_ = -1;
  std::string path_;
  int64_t next_id_ = 0;
  std::string input_;
  std::deque<Json> inbox_;
  std::deque<Json> parsed_;
};

class ImageLoader {
  public:
  ImageLoader() : worker_([this] { run(); }) {}
  ~ImageLoader() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      jobs_ = {};
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  std::optional<fs::path> image(const std::string &url) {
    if (url.empty()) return std::nullopt;
    const auto normalized_url = url.starts_with("//") ? "https:" + url : url;
    const auto path = cache_path(normalized_url);
    if (fs::exists(path)) return path;
    std::lock_guard lock(mutex_);
    if (requested_.insert(normalized_url).second) {
      jobs_.push({normalized_url, path});
      cv_.notify_one();
    }
    return std::nullopt;
  }

  private:
  static fs::path cache_path(const std::string &url) {
    const auto hash = std::hash<std::string>{}(url);
    return fs::temp_directory_path() / "aniparser-tui" /
           (std::to_string(hash) + ".img");
  }

  void run() {
    while (true) {
      Job job;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_ && jobs_.empty()) return;
        job = std::move(jobs_.front());
        jobs_.pop();
      }
      try {
        auto response =
            cpr::Get(cpr::Url{job.url}, cpr::Timeout{12000},
                     cpr::VerifySsl{true}, cpr::UserAgent{"AniParser-TUI/1.0"});
        if (response.status_code != 200 || response.text.empty() ||
            response.text.size() > 12 * 1024 * 1024)
          continue;
        fs::create_directories(job.path.parent_path());
        auto temp = job.path;
        temp += ".tmp";
        std::ofstream out(temp, std::ios::binary);
        out.write(response.text.data(),
                  static_cast<std::streamsize>(response.text.size()));
        out.close();
        if (out) fs::rename(temp, job.path);
      } catch (...) {}
    }
  }

  struct Job {
    std::string url;
    fs::path path;
  };
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<Job> jobs_;
  std::unordered_set<std::string> requested_;
  bool stopping_ = false;
  std::thread worker_;
};

class Tui {
  public:
  Tui(IpcClient &ipc, CacheReader &cache, notcurses* nc)
    : ipc_(ipc)
    , cache_(cache)
    , nc_(nc)
    , std_(notcurses_stdplane(nc)) {}
  ~Tui() {
    release_search();
    release_feed();
    release_detail();
    for (const auto &item : detail_history_) {
      try {
        ipc_.request("deleteAnime", {{"handle", handle_json(item.handle)}});
      } catch (...) {}
    }
    for (auto* visual : visuals_) ncvisual_destroy(visual);
    for (auto* plane : image_planes_) ncplane_destroy(plane);
  }

  void initialize() { load_feed(); }
  int run() {
    bool running = true;
    while (running) {
      pump_events();
      draw();
      timespec timeout{0, 100'000'000};
      ncinput input{};
      const auto key = notcurses_get(nc_, &timeout, &input);
      if (key == (uint32_t)-1 || key == 0) continue;
      if (input.evtype == NCTYPE_RELEASE) continue;
      if (key == 'q' || key == 'Q') running = false;
      else if (key == NCKEY_UP)
        move_selection(screen_ == Screen::Feed ? -grid_columns_ : -1);
      else if (key == NCKEY_DOWN)
        move_selection(screen_ == Screen::Feed ? grid_columns_ : 1);
      else if (key == NCKEY_LEFT && screen_ == Screen::Feed) move_selection(-1);
      else if (key == NCKEY_RIGHT && screen_ == Screen::Feed) move_selection(1);
      else if (key == NCKEY_ENTER || key == '\n' || key == '\r') activate();
      else if (key == NCKEY_ESC || key == NCKEY_BACKSPACE || key == 127)
        go_back();
      else if (key == '/') search_prompt();
      else if (key == 'f' || key == 'F') {
        screen_ = Screen::Feed;
        load_feed();
      } else if ((key == 'n' || key == 'N') && screen_ == Screen::Search)
        next_search_page();
      else if ((key == 'v' || key == 'V') && screen_ == Screen::Details)
        open_viewing_order();
      else if (key == 'r' || key == 'R') refresh_feed_or_detail();
    }
    return 0;
  }

  private:
  enum class Screen { Feed, Search, Details, ViewingOrder };

  void load_feed() {
    status_ = "Loading feed…";
    try {
      const auto reply = ipc_.request("getFeed", {{"watches", Json::array()}});
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Feed failed"));
      release_feed();
      feed_handle_ = parse_handle(reply.value("handle", Json{}));
      feed_request_id_ = reply.value("id", int64_t{-1});
      if (!populate_feed(feed_handle_))
        throw std::runtime_error("Cannot map feed handle");
      screen_ = Screen::Feed;
      selected_ = 0;
      status_ = reply.value("refreshing", false)
                    ? "Cached feed · refreshing from API…"
                    : "Feed ready";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  bool populate_feed(shm::HeapHandle handle) {
    const auto* feed = cache_.feed(handle);
    if (!feed) return false;
    std::vector<AnimeRow> rows;
    const auto add = [this, &rows](const auto &items, std::string section) {
      const auto* handles = cache_.vector(items);
      for (uint64_t i = 0; handles && i < items.size; ++i)
        rows.push_back(anime_row(cache_, handles[i], section));
    };
    add(feed->new_anime, "New");
    add(feed->announcements, "Announcements");
    add(feed->recommendations, "Recommended");
    add(feed->carousel_items, "Featured");
    add(feed->schedule_items, "Schedule");
    if (const auto* cols = cache_.vector(feed->collections))
      for (uint64_t i = 0; i < feed->collections.size; ++i) {
        auto name = shared_string(cache_, cols[i].title);
        add(cols[i].anime, name.empty() ? "Collection" : name);
      }
    feed_visible_ = std::move(rows);
    return true;
  }

  void search_prompt() {
    query_.clear();
    status_ = "Type a title and press Enter (Esc cancels)";
    draw();
    for (;;) {
      ncinput input{};
      const auto key = notcurses_get(nc_, nullptr, &input);
      if (input.evtype == NCTYPE_RELEASE) continue;
      if (key == NCKEY_ESC) {
        status_ = "Search cancelled";
        return;
      }
      if (key == NCKEY_ENTER || key == '\n' || key == '\r') break;
      if (key == NCKEY_BACKSPACE || key == 127) {
        if (!query_.empty()) {
          auto start = query_.size() - 1;
          while (start > 0 &&
                 (static_cast<unsigned char>(query_[start]) & 0xc0) == 0x80)
            --start;
          query_.erase(start);
        }
      } else if (key >= 32 && key <= 0x10ffff) append_utf8(query_, key);
      draw_search_input();
    }
    if (query_.empty()) return;
    release_search();
    visible_.clear();
    selected_ = 0;
    search_next_offset_ = 0;
    search_has_more_ = true;
    screen_ = Screen::Search;
    request_search_page();
  }

  void draw_search_input() {
    const int available_width = std::max(1, cols() - 42);
    ncplane_set_bg_rgb(std_, color_panel);
    ncplane_erase_region(std_, 1, 2, 1, available_width);
    text(1, 2, "/ " + query_, color_muted, color_panel);
    notcurses_render(nc_);
  }

  void request_search_page() {
    if (search_loading_ || !search_has_more_) return;
    search_loading_ = true;
    const auto request_offset = search_next_offset_;
    const auto request_query = query_;
    const auto socket_path = ipc_.socket_path();
    if (!search_ipc_) search_ipc_ = std::make_shared<IpcClient>();
    pending_search_ipc_ = search_ipc_;
    const auto page_ipc = pending_search_ipc_;
    status_ = visible_.empty() ? "Searching…" : "Loading more results…";
    search_future_ =
        std::async(std::launch::async, [page_ipc, socket_path, request_query,
                                        request_offset] {
          if (!page_ipc->connected() && !page_ipc->connect_to(socket_path))
            throw std::runtime_error("Cannot connect to backend for search");
          return page_ipc->request("getSearch", {{"params",
                                                  {{"q", request_query},
                                                   {"offset", request_offset},
                                                   {"limit", 10}}}});
        });
  }

  void poll_search_page() {
    if (!search_future_.valid() || search_future_.wait_for(std::chrono::seconds(
                                       0)) != std::future_status::ready)
      return;
    search_loading_ = false;
    try {
      const auto reply = search_future_.get();
      pending_search_ipc_.reset();
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Search failed"));
      const auto handle = parse_handle(reply.value("handle", Json{}));
      const auto* page = cache_.search(handle);
      if (!handle || !page) {
        if (handle)
          ipc_.request("deleteSearch", {{"handle", handle_json(handle)}});
        throw std::runtime_error("Cannot map search page");
      }
      search_handles_.push_back(handle);
      search_request_ids_.push_back(reply.value("id", int64_t{-1}));
      search_page_counts_.push_back(0);
      const auto count = std::min<std::uint64_t>(page->anime_count,
                                                 SharedSearch::anime_capacity);
      for (std::uint64_t i = 0; i < count; ++i)
        visible_.push_back(anime_row(cache_, page->anime[i], "Search"));
      search_page_counts_.back() = count;
      search_next_offset_ += SharedSearch::anime_capacity;
      search_has_more_ = count == SharedSearch::anime_capacity;
      status_ = reply.value("refreshing", false)
                    ? "Cached results · refreshing from API…"
                : visible_.empty()
                    ? "No results"
                    : (search_has_more_
                           ? "Search results · keep scrolling to load more"
                           : "Search results · end of results");
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void next_search_page() { request_search_page(); }

  void activate() {
    if (screen_ == Screen::Details) {
      open_viewing_order();
      return;
    }
    if (screen_ == Screen::ViewingOrder) {
      if (selected_ < viewing_.size()) start_anime(viewing_[selected_].id);
      return;
    }
    const auto &rows = screen_ == Screen::Feed ? feed_visible_ : visible_;
    if (selected_ >= rows.size()) return;
    start_anime(rows[selected_].id);
  }

  void start_anime(int64_t id) {
    if (id <= 0) return;
    try {
      const bool nested = screen_ == Screen::ViewingOrder;
      const auto reply = ipc_.request("getAnime", {{"anime_id", id}});
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "getAnime failed"));
      if (nested && detail_handle_) {
        detail_history_.push_back(
            {detail_handle_, detail_id_, detail_request_id_, detail_loaded_,
             Screen::ViewingOrder, selected_, detail_return_screen_,
             detail_return_selected_});
        detail_handle_ = {};
      } else {
        previous_screen_ = screen_;
        previous_selected_ = selected_;
        release_detail();
        detail_return_screen_ = previous_screen_;
        detail_return_selected_ = previous_selected_;
      }
      detail_handle_ = parse_handle(reply.value("handle", Json{}));
      detail_request_id_ = reply.value("id", int64_t{-1});
      detail_id_ = id;
      detail_loaded_ = reply.value("loaded", false);
      viewing_.clear();
      screen_ = Screen::Details;
      if (detail_loaded_) populate_viewing_order();
      status_ = reply.value("refreshing", false)
                    ? "Anime shown from cache · refreshing from API…"
                : detail_loaded_ ? "Anime details from cache"
                                 : "Anime cache is incomplete";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void pump_events() {
    poll_search_page();
    if (!search_loading_ && search_ipc_) {
      for (const auto &message : search_ipc_->poll_messages())
        if (message.value("event", std::string{}) == "search.updated")
          apply_search_update(message);
    }
    for (const auto &message : ipc_.poll_messages()) {
      const auto event = message.value("event", std::string{});
      if (event == "feed.updated") {
        apply_feed_update(message);
        continue;
      }
      if (event != "anime.updated") continue;
      const auto request_id = message.value("id", int64_t{-2});
      auto updated = parse_handle(message.value("handle", Json{}));
      if (request_id != detail_request_id_) {
        for (auto &prior : detail_history_) {
          if (prior.request_id != request_id) continue;
          if (updated) prior.handle = updated;
          prior.loaded = prior.loaded || message.value("ok", false);
        }
        continue;
      }
      if (updated) detail_handle_ = updated;
      const bool refreshed = message.value("ok", false);
      detail_loaded_ = detail_loaded_ || refreshed;
      status_ = refreshed        ? "Anime details updated"
                : detail_loaded_ ? "Refresh failed · showing cached details"
                                 : "Anime details could not be refreshed";
      if (detail_loaded_) populate_viewing_order();
    }
  }

  void apply_feed_update(const Json &message) {
    const auto request_id = message.value("id", int64_t{-1});
    const auto handle = parse_handle(message.value("handle", Json{}));
    if (request_id != feed_request_id_) {
      if (handle) {
        try {
          ipc_.request("deleteFeed", {{"handle", handle_json(handle)}});
        } catch (...) {}
      }
      return;
    }
    if (!message.value("ok", false) || !handle) {
      status_ = message.value("error", "Feed refresh failed");
      return;
    }
    const auto previous = feed_handle_;
    if (!populate_feed(handle)) {
      try {
        ipc_.request("deleteFeed", {{"handle", handle_json(handle)}});
      } catch (...) {}
      status_ = "Cannot map refreshed feed";
      return;
    }
    feed_handle_ = handle;
    if (screen_ == Screen::Feed)
      selected_ = feed_visible_.empty()
                      ? 0
                      : std::min(selected_, feed_visible_.size() - 1);
    if (previous) {
      try {
        ipc_.request("deleteFeed", {{"handle", handle_json(previous)}});
      } catch (...) {}
    }
    if (screen_ == Screen::Feed) status_ = "Feed refreshed";
  }

  void apply_search_update(const Json &message) {
    const auto request_id = message.value("id", int64_t{-1});
    const auto request = std::find(search_request_ids_.begin(),
                                   search_request_ids_.end(), request_id);
    const auto handle = parse_handle(message.value("handle", Json{}));
    if (request == search_request_ids_.end()) {
      if (handle) {
        try {
          ipc_.request("deleteSearch", {{"handle", handle_json(handle)}});
        } catch (...) {}
      }
      return;
    }
    if (!message.value("ok", false) || !handle) {
      status_ = message.value("error", "Search refresh failed");
      return;
    }
    const auto page_index = static_cast<std::size_t>(
        std::distance(search_request_ids_.begin(), request));
    if (page_index >= search_handles_.size()) {
      try {
        ipc_.request("deleteSearch", {{"handle", handle_json(handle)}});
      } catch (...) {}
      return;
    }
    const auto* page = cache_.search(handle);
    if (!page) {
      try {
        ipc_.request("deleteSearch", {{"handle", handle_json(handle)}});
      } catch (...) {}
      status_ = "Cannot map refreshed search page";
      return;
    }

    const auto start = page_index * SharedSearch::anime_capacity;
    const auto old_count = search_page_counts_[page_index];
    const auto count = std::min<std::uint64_t>(page->anime_count,
                                               SharedSearch::anime_capacity);
    std::vector<AnimeRow> refreshed;
    refreshed.reserve(count);
    for (std::uint64_t i = 0; i < count; ++i)
      refreshed.push_back(anime_row(cache_, page->anime[i], "Search"));
    if (start + old_count <= visible_.size()) {
      visible_.erase(visible_.begin() + static_cast<std::ptrdiff_t>(start),
                     visible_.begin() +
                         static_cast<std::ptrdiff_t>(start + old_count));
      visible_.insert(visible_.begin() + static_cast<std::ptrdiff_t>(start),
                      refreshed.begin(), refreshed.end());
    }
    const auto old_handle = search_handles_[page_index];
    search_handles_[page_index] = handle;
    search_page_counts_[page_index] = count;
    search_has_more_ = count == SharedSearch::anime_capacity;
    try {
      ipc_.request("deleteSearch", {{"handle", handle_json(old_handle)}});
    } catch (...) {}
    selected_ = visible_.empty() ? 0 : std::min(selected_, visible_.size() - 1);
    status_ = "Search results refreshed";
  }

  void populate_viewing_order() {
    viewing_.clear();
    const auto* anime = cache_.anime(detail_handle_);
    if (!anime) return;
    const auto* related = cache_.vector(anime->viewing_order);
    for (uint64_t i = 0; related && i < anime->viewing_order.size; ++i)
      viewing_.push_back(anime_row(cache_, related[i], "Viewing order"));
  }

  void open_viewing_order() {
    if (!detail_loaded_) {
      status_ = "Viewing order loads with Anime details";
      return;
    }
    if (viewing_.empty()) populate_viewing_order();
    if (viewing_.empty()) {
      status_ = "No viewing order entries";
      return;
    }
    selected_ = 0;
    screen_ = Screen::ViewingOrder;
    status_ = "Viewing order";
  }

  void go_back() {
    if (screen_ == Screen::Details) {
      release_detail();
      if (!detail_history_.empty()) {
        const auto prior = detail_history_.back();
        detail_history_.pop_back();
        detail_handle_ = prior.handle;
        detail_id_ = prior.id;
        detail_request_id_ = prior.request_id;
        detail_loaded_ = prior.loaded;
        detail_return_screen_ = prior.detail_back_screen;
        detail_return_selected_ = prior.detail_back_selected;
        screen_ = prior.return_screen;
        selected_ = prior.return_selected;
        if (screen_ == Screen::ViewingOrder) populate_viewing_order();
        status_ =
            screen_ == Screen::ViewingOrder ? "Viewing order" : "Anime details";
      } else {
        screen_ = detail_return_screen_;
        selected_ = detail_return_selected_;
        status_ = "";
      }
    } else if (screen_ == Screen::ViewingOrder) {
      screen_ = Screen::Details;
      status_ = "Anime details";
    } else if (screen_ == Screen::Search) {
      release_search();
      screen_ = Screen::Feed;
      load_feed();
    }
  }

  void refresh_feed_or_detail() {
    if (screen_ == Screen::Details) start_anime(detail_id_);
    else if (screen_ == Screen::ViewingOrder) start_anime(detail_id_);
    else load_feed();
  }

  void move_selection(int delta) {
    const auto &rows = screen_ == Screen::ViewingOrder ? viewing_
                       : screen_ == Screen::Feed       ? feed_visible_
                                                       : visible_;
    if (rows.empty()) return;
    const int64_t next =
        std::clamp<int64_t>(static_cast<int64_t>(selected_) + delta, 0,
                            static_cast<int64_t>(rows.size() - 1));
    selected_ = static_cast<size_t>(next);
    if (screen_ == Screen::Search && search_has_more_ &&
        selected_ + 3 >= visible_.size())
      request_search_page();
  }

  int rows() const {
    unsigned y = 0, x = 0;
    ncplane_dim_yx(std_, &y, &x);
    return static_cast<int>(y);
  }
  int cols() const {
    unsigned y = 0, x = 0;
    ncplane_dim_yx(std_, &y, &x);
    return static_cast<int>(x);
  }

  void text(int y,
            int x,
            const std::string &value,
            uint64_t fg = 0xffffff,
            uint64_t bg = color_ink,
            uint16_t style = NCSTYLE_NONE) {
    (void)bg;
    ncplane_set_fg_rgb(std_, fg);
    ncplane_set_bg_alpha(std_, NCALPHA_TRANSPARENT);
    ncplane_set_styles(std_, style);
    ncplane_putstr_yx(std_, y, x, value.c_str());
    ncplane_set_styles(std_, NCSTYLE_NONE);
  }

  void image(const std::string &url, int y, int x, int h, int w) {
    const auto path = images_.image(url);
    if (!path) return;
    auto* visual = ncvisual_from_file(path->c_str());
    if (!visual) return;
    ncplane_options popts{};
    popts.y = y;
    popts.x = x;
    popts.rows = h;
    popts.cols = w;
    auto* plane = ncplane_create(std_, &popts);
    if (!plane) {
      ncvisual_destroy(visual);
      return;
    }
    ncvisual_options vopts{};
    vopts.n = plane;
    vopts.y = 0;
    vopts.x = 0;
    vopts.scaling = NCSCALE_SCALE;
    vopts.blitter = NCBLIT_PIXEL;
    if (!ncvisual_blit(nc_, visual, &vopts)) {
      // Use Kitty/Sixel bitmap rendering whenever the terminal supports it.
      // Fall back to the terminal's best cell blitter otherwise.
      ncplane_destroy(plane);
      popts.y = y;
      popts.x = x;
      popts.rows = h;
      popts.cols = w;
      plane = ncplane_create(std_, &popts);
      if (!plane) {
        ncvisual_destroy(visual);
        return;
      }
      vopts.n = plane;
      vopts.blitter = NCBLIT_DEFAULT;
      if (!ncvisual_blit(nc_, visual, &vopts)) {
        ncplane_destroy(plane);
        ncvisual_destroy(visual);
        return;
      }
    }
    image_planes_.push_back(plane);
    visuals_.push_back(visual);
  }

  void draw_header(const std::string &title) {
    const auto width = cols();
    ncplane_set_bg_rgb(std_, color_ink);
    ncplane_set_fg_rgb(std_, 0xffffff);
    ncplane_erase(std_);
    ncplane_set_bg_rgb(std_, color_panel);
    ncplane_erase_region(std_, 0, 0, 2, width);
    text(0, 2, "ANI / PARSER", color_accent, color_panel, NCSTYLE_BOLD);
    text(0, 17, title, 0xffffff, color_panel, NCSTYLE_BOLD);
    const auto search_label = query_.empty() ? "/ Search anime" : "/ " + query_;
    text(1, 2, search_label, color_muted, color_panel);
    text(1, std::max(2, width - 38), "↑↓ select · Enter open · q quit",
         color_muted, color_panel);
  }

  void draw_footer() {
    const auto y = rows();
    text(y - 1, 2, status_, color_muted, color_ink);
    std::string actions = "f feed  / search  Esc back";
    if (screen_ == Screen::Search) actions += "  n next page";
    if (screen_ == Screen::Details) actions += "  v viewing order";
    text(y - 1, std::max(2, cols() - static_cast<int>(actions.size()) - 2),
         actions, color_accent, color_ink);
  }

  void draw_feed() {
    const int width = cols(), height = rows();
    const int columns = std::max(1, std::min(4, width / 26));
    grid_columns_ = columns;
    const int card_w = std::max(18, (width - 4) / columns);
    const int card_h = std::max(9, (height - 5) / 2);
    const int per_page =
        std::max(1, columns * std::max(1, (height - 5) / card_h));
    const size_t start = (selected_ / per_page) * per_page;
    for (size_t i = start; i < feed_visible_.size() && i < start + per_page;
         ++i) {
      const int slot = static_cast<int>(i - start);
      const int col = slot % columns, row = slot / columns;
      const int x = 2 + col * card_w, y = 3 + row * card_h;
      const auto bg = i == selected_ ? 0x263a44 : color_panel;
      ncplane_set_bg_rgb(std_, bg);
      ncplane_erase_region(std_, y, x, card_h - 1, card_w - 2);
      const auto &item = feed_visible_[i];
      image(item.image, y + 1, x + 1, std::max(3, card_h - 4), card_w - 4);
      text(y + card_h - 3, x + 1, item.title,
           i == selected_ ? color_accent : 0xffffff, bg,
           i == selected_ ? NCSTYLE_BOLD : NCSTYLE_NONE);
      text(y + card_h - 2, x + 1,
           item.section + "  ·  #" + std::to_string(item.id), color_muted, bg);
    }
  }

  static std::size_t utf8_columns(std::string_view value) {
    std::size_t columns = 0;
    for (const unsigned char ch : value)
      if ((ch & 0xc0) != 0x80) ++columns;
    return columns;
  }

  static std::vector<std::string> wrap_description(std::string_view value,
                                                   std::size_t width,
                                                   std::size_t max_lines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t pos = 0;
    while (pos < value.size() && lines.size() < max_lines) {
      while (pos < value.size() && (value[pos] == ' ' || value[pos] == '\n' ||
                                    value[pos] == '\r' || value[pos] == '\t'))
        ++pos;
      if (pos == value.size()) break;
      auto end = pos;
      while (end < value.size() && value[end] != ' ' && value[end] != '\n' &&
             value[end] != '\r' && value[end] != '\t')
        ++end;
      const auto word = value.substr(pos, end - pos);
      const auto added_width = utf8_columns(word) + (line.empty() ? 0 : 1);
      if (!line.empty() && utf8_columns(line) + added_width > width) {
        lines.push_back(std::move(line));
        line.clear();
        if (lines.size() == max_lines) break;
      }
      if (!line.empty()) line.push_back(' ');
      line.append(word);
      pos = end;
    }
    if (!line.empty() && lines.size() < max_lines)
      lines.push_back(std::move(line));
    if (pos < value.size() && !lines.empty()) {
      auto &last = lines.back();
      if (utf8_columns(last) + 1 <= width) last += "…";
    }
    return lines;
  }

  void draw_anime_rows(const std::vector<AnimeRow> &items,
                       const std::string &heading) {
    text(2, 2, heading, color_accent, color_ink, NCSTYLE_BOLD);
    const int width = cols(), height = rows();
    constexpr int image_width = 15;
    constexpr int image_height = 9;
    constexpr int row_height = image_height + 1;
    const size_t visible_count =
        static_cast<size_t>(std::max(1, (height - 5) / row_height));
    const size_t start = (selected_ / visible_count) * visible_count;
    const int image_w = image_width;
    const int text_x = image_w + 4;
    const auto text_width =
        static_cast<std::size_t>(std::max(12, width - text_x - 3));
    for (size_t i = start; i < items.size() && i < start + visible_count; ++i) {
      const int y = 4 + static_cast<int>(i - start) * row_height;
      const auto bg = i == selected_ ? 0x263a44 : color_panel;
      ncplane_set_bg_rgb(std_, bg);
      ncplane_erase_region(std_, y, 2, row_height - 1, width - 4);
      image(items[i].image, y, 3, image_height, image_width);
      text(y, text_x, items[i].title, i == selected_ ? color_accent : 0xffffff,
           bg, NCSTYLE_BOLD);
      std::string info = items[i].info;
      if (!info.empty()) info += "  ·  ";
      info += "#" + std::to_string(items[i].id);
      text(y + 2, text_x, info, color_muted, bg);
      const auto description =
          wrap_description(items[i].description, text_width,
                           static_cast<std::size_t>(row_height - 4));
      for (std::size_t line_index = 0; line_index < description.size();
           ++line_index)
        text(y + 4 + static_cast<int>(line_index), text_x,
             description[line_index], 0xd5dde6, bg);
    }
  }

  void draw_details() {
    line.clear();
    const auto* anime = cache_.anime(detail_handle_);
    if (!anime) {
      text(4, 3, "Opening Anime #" + std::to_string(detail_id_) + "…",
           color_accent);
      text(6, 3, "Details are loading asynchronously. Esc returns to the list.",
           color_muted);
      return;
    }
    const auto data = anime_row(cache_, detail_handle_);
    const int h = std::max(6, rows() - 9), w = std::min(32, cols() / 3);
    image(data.image, 3, 3, h, w);
    text(3, w + 6, data.title, color_accent, color_ink, NCSTYLE_BOLD);
    text(5, w + 6, "#" + std::to_string(detail_id_) + "  ·  " + data.info,
         color_muted);
    const auto status = shared_string(cache_, anime->anime_status.title);
    if (!status.empty()) text(6, w + 6, "Status: " + status, color_muted);
    auto description = shared_string(cache_, anime->description);
    const int max_width = std::max(20, cols() - w - 10);
    size_t pos = 0;
    int y = 8;
    while (pos < description.size() && y < rows() - 4) {
      const auto end = description.find_first_of(" \n", pos);
      const auto word_end = end == std::string::npos ? description.size() : end;
      auto word = description.substr(pos, word_end - pos);
      if (static_cast<int>(word.size()) > max_width) word.resize(max_width);
      if (description[pos] == '\n') {
        ++y;
        ++pos;
        continue;
      }
      if (!line.empty() &&
          static_cast<int>(line.size() + word.size() + 1) > max_width) {
        text(y++, w + 6, line, 0xd5dde6);
        line.clear();
      }
      if (!line.empty()) line += ' ';
      line += word;
      pos = word_end;
      while (pos < description.size() && description[pos] == ' ') ++pos;
    }
    if (!line.empty() && y < rows() - 3) text(y, w + 6, line, 0xd5dde6);
    text(rows() - 3, 3,
         detail_loaded_ ? "v / Enter  Viewing order" : "Fetching details…",
         detail_loaded_ ? color_accent : color_muted);
  }

  void draw() {
    for (auto* plane : image_planes_) ncplane_destroy(plane);
    image_planes_.clear();
    for (auto* visual : visuals_) ncvisual_destroy(visual);
    visuals_.clear();
    switch (screen_) {
    case Screen::Feed:
      draw_header("Feed");
      draw_feed();
      break;
    case Screen::Search:
      draw_header("Search");
      draw_anime_rows(visible_, "Search results · " + query_);
      break;
    case Screen::Details:
      draw_header("Anime");
      draw_details();
      break;
    case Screen::ViewingOrder:
      draw_header("Viewing order");
      draw_anime_rows(viewing_, "Viewing order");
      break;
    }
    draw_footer();
    notcurses_render(nc_);
  }

  void release_search() {
    if (search_future_.valid()) {
      search_future_.wait();
      try {
        const auto reply = search_future_.get();
        const auto pending = parse_handle(reply.value("handle", Json{}));
        if (pending)
          ipc_.request("deleteSearch", {{"handle", handle_json(pending)}});
      } catch (...) {}
    }
    search_loading_ = false;
    pending_search_ipc_.reset();
    search_ipc_.reset();
    for (const auto handle : search_handles_) {
      try {
        ipc_.request("deleteSearch", {{"handle", handle_json(handle)}});
      } catch (...) {}
    }
    search_handles_.clear();
    search_request_ids_.clear();
    search_page_counts_.clear();
  }
  void release_feed() {
    if (feed_handle_) {
      try {
        ipc_.request("deleteFeed", {{"handle", handle_json(feed_handle_)}});
      } catch (...) {}
    }
    feed_handle_ = {};
    feed_request_id_ = -1;
  }
  void release_detail() {
    if (!detail_handle_) return;
    try {
      ipc_.request("deleteAnime", {{"handle", handle_json(detail_handle_)}});
    } catch (...) {}
    detail_handle_ = {};
    detail_request_id_ = -1;
    detail_loaded_ = false;
  }

  static void append_utf8(std::string &out, uint32_t codepoint) {
    if (codepoint <= 0x7f) out.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
      out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  IpcClient &ipc_;
  CacheReader &cache_;
  notcurses* nc_;
  ncplane* std_;
  ImageLoader images_;
  std::vector<ncvisual*> visuals_;
  std::vector<ncplane*> image_planes_;
  shm::HeapHandle feed_handle_{};
  int64_t feed_request_id_ = -1;
  shm::HeapHandle detail_handle_{};
  std::vector<AnimeRow> visible_;
  std::vector<AnimeRow> feed_visible_;
  std::vector<AnimeRow> viewing_;
  std::vector<shm::SearchHandle> search_handles_;
  std::vector<int64_t> search_request_ids_;
  std::vector<std::uint64_t> search_page_counts_;
  std::future<Json> search_future_;
  std::shared_ptr<IpcClient> search_ipc_;
  std::shared_ptr<IpcClient> pending_search_ipc_;
  struct DetailState {
    shm::HeapHandle handle;
    int64_t id;
    int64_t request_id;
    bool loaded;
    Screen return_screen;
    size_t return_selected;
    Screen detail_back_screen;
    size_t detail_back_selected;
  };
  std::vector<DetailState> detail_history_;
  Screen screen_ = Screen::Feed;
  Screen previous_screen_ = Screen::Feed;
  Screen detail_return_screen_ = Screen::Feed;
  size_t selected_ = 0;
  size_t previous_selected_ = 0;
  size_t detail_return_selected_ = 0;
  int64_t search_next_offset_ = 0;
  int64_t detail_id_ = 0;
  int64_t detail_request_id_ = -1;
  std::string query_;
  std::string status_ = "Connecting…";
  bool detail_loaded_ = false;
  bool search_loading_ = false;
  bool search_has_more_ = false;
  int grid_columns_ = 3;
  std::string line;
};
} // namespace

int main(int argc, char** argv) {
  const std::string cache_name = argc > 1 ? argv[1] : "aniparser";
  const std::string socket_path = argc > 2 ? argv[2] : "/tmp/aniparser.sock";
  IpcClient ipc;
  if (!ipc.connect_to(socket_path)) {
    std::cerr << "Cannot connect to backend socket: " << socket_path << '\n';
    return 1;
  }
  try {
    const auto hello = ipc.request("hello");
    if (!hello.value("ok", false) || hello.value("layout", 0) != 8)
      throw std::runtime_error("Backend cache layout is incompatible");
    const auto &heaps = hello.at("heaps");
    const auto anime_heap_name =
        heaps.at("anime").at("name").get<std::string>();
    if (!anime_heap_name.starts_with(cache_name + ".v8."))
      throw std::runtime_error(
          "The selected cache name does not match the backend");
    CacheReader cache;
    if (!cache.open(heaps))
      throw std::runtime_error("Cannot read backend shared memory");
    notcurses_options options{};
    options.flags = NCOPTION_SUPPRESS_BANNERS;
    auto* nc = notcurses_init(&options, stdout);
    if (!nc) throw std::runtime_error("Cannot initialize Notcurses");
    int result = 1;
    {
      Tui tui(ipc, cache, nc);
      tui.initialize();
      result = tui.run();
    }
    notcurses_stop(nc);
    return result;
  } catch (const std::exception &error) {
    std::cerr << "TUI error: " << error.what() << '\n';
    return 1;
  }
}
