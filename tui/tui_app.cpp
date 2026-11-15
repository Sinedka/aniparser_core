#include "tui_app.hpp"

#include <nlohmann/json.hpp>
#include <notcurses/notcurses.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "clipboard.hpp"
#include "ipc_client.hpp"
#include "state_utils.hpp"

namespace aniparser::tui {
namespace {
using Json = nlohmann::json;
constexpr uint64_t kBackground = 0x10151d;
constexpr uint64_t kPanel = 0x1a222d;
constexpr uint64_t kAccent = 0x70d6c1;
constexpr uint64_t kMuted = 0x93a2b4;

void log_line(std::string_view path, std::string_view message) {
  std::ofstream log(std::string(path), std::ios::app);
  if (!log) return;
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  log << timestamp << ' ' << message << '\n';
}

struct AnimeRow {
  std::int64_t id = 0;
  std::string title;
  std::string info;
  std::string description;
  Json model;
};

std::string value_string(const Json &object, std::string_view key) {
  if (!object.is_object()) return {};
  const auto value = object.find(std::string(key));
  if (value == object.end() || value->is_null()) return {};
  if (value->is_string()) return value->get<std::string>();
  return value->dump();
}

std::string joined_names(const Json &items, std::string_view field) {
  if (!items.is_array()) return {};
  std::string result;
  for (const auto &item : items) {
    const auto name = field.empty() && item.is_string()
                          ? item.get<std::string>()
                          : value_string(item, field);
    if (name.empty()) continue;
    if (!result.empty()) result += ", ";
    result += name;
  }
  return result;
}

AnimeRow anime_row(Json anime) {
  AnimeRow row;
  row.model = std::move(anime);
  row.id = row.model.value("anime_id", int64_t{0});
  row.title = row.model.value("title", std::string{});
  if (row.title.empty()) row.title = "Anime " + std::to_string(row.id);
  row.description = row.model.value("description", std::string{});
  row.info = value_string(row.model.value("type", Json::object()), "name");
  const auto year = row.model.value("year", int64_t{0});
  if (year > 0) {
    if (!row.info.empty()) row.info += " · ";
    row.info += std::to_string(year);
  }
  const auto rating =
      row.model.value("rating", Json::object()).value("average", 0.0);
  if (rating > 0) {
    if (!row.info.empty()) row.info += " · ";
    row.info += "★ " + std::to_string(rating);
  }
  return row;
}

void append_utf8(std::string &target, uint32_t codepoint) {
  if (codepoint <= 0x7f) target.push_back(static_cast<char>(codepoint));
  else if (codepoint <= 0x7ff) {
    target.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    target.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    target.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    target.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    target.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0x10ffff) {
    target.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    target.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    target.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    target.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

std::string quality_label(std::string_view quality) {
  const bool numeric = !quality.empty() &&
                       std::all_of(quality.begin(), quality.end(), [](char ch) {
                         return ch >= '0' && ch <= '9';
                       });
  return std::string(quality) + (numeric ? "p" : "");
}

struct StreamOption {
  std::string quality;
  std::string url;
};
struct Episode {
  std::string number;
  std::string iframe;
  std::vector<StreamOption> streams;
};
struct Dubber {
  std::string name;
  std::vector<Episode> episodes;
};
struct Player {
  std::string name;
  bool anilibria = false;
  std::vector<Dubber> dubbers;
};

class TuiApplication {
  public:
  TuiApplication(IpcClient &ipc, notcurses* context)
    : ipc_(ipc)
    , context_(context)
    , plane_(notcurses_stdplane(context)) {
    ipc_.on_wait = [this] {
      draw();
      timespec immediate{};
      ncinput input{};
      const auto key = notcurses_get(context_, &immediate, &input);
      if (input.evtype == NCTYPE_RELEASE) return true;
      if (key == static_cast<uint32_t>(-1) || key == 'q' || key == 'Q') {
        quit_requested_ = true;
        return false;
      }
      if (key == NCKEY_ESC) return false;
      if (key != 0 && key != static_cast<uint32_t>(-1) && key != NCKEY_RESIZE &&
          pending_input_.size() < 256)
        pending_input_.emplace_back(key, input);
      return true;
    };
  }
  ~TuiApplication() { ipc_.on_wait = {}; }

  void initialize() {
    const auto reply =
        ipc_.request("subscribeHistory", {{"offset", 0}, {"limit", 20}});
    updateHistory(reply);
    load_feed();
  }
  int run() {
    bool running = true;
    while (running && !quit_requested_) {
      pump_events();
      draw();
      timespec timeout{0, 100'000'000};
      ncinput input{};
      uint32_t key;
      if (pending_input_.empty())
        key = notcurses_get(context_, &timeout, &input);
      else {
        key = pending_input_.front().first;
        input = pending_input_.front().second;
        pending_input_.pop_front();
      }
      if (key == static_cast<uint32_t>(-1)) break;
      if (key == 0 || input.evtype == NCTYPE_RELEASE) continue;
      if (key == 'q' || key == 'Q') running = false;
      else if (key == NCKEY_UP) move(-1);
      else if (key == NCKEY_DOWN) move(1);
      else if (key == NCKEY_ENTER || key == '\n' || key == '\r') activate();
      else if (key == NCKEY_ESC || key == NCKEY_BACKSPACE || key == 127)
        go_back();
      else if (key == '/') search_prompt();
      else if ((key == 'f' || key == 'F') && screen_ != Screen::Details)
        load_feed();
      else if ((key == 'n' || key == 'N') && screen_ == Screen::Search &&
               search_has_more_)
        search(query_, next_offset_);
      else if ((key == 'v' || key == 'V') && screen_ == Screen::Details)
        show_viewing_order();
    }
    return 0;
  }

  private:
  enum class Screen {
    Feed,
    Search,
    Details,
    Viewing,
    Players,
    Dubbers,
    Episodes,
    StreamLinks
  };

  struct NavigationState {
    Screen screen;
    Screen return_screen;
    std::size_t selected, return_selected, player, dubber, episode;
    std::int64_t anime_id, request_id;
    bool loading;
    Json detail;
    std::vector<Player> players;
    std::vector<StreamOption> streams;
    std::vector<AnimeRow> viewing;
    std::vector<AnimeRow> search_rows;
    std::string query;
    std::int64_t search_request_id, next_offset;
    bool has_more;
  };
  std::optional<NavigationState> search_return_state_;
  std::vector<NavigationState> detail_back_stack_;

  NavigationState capture_navigation() const {
    return NavigationState{screen_,
                           return_screen_,
                           selected_,
                           return_selected_,
                           player_index_,
                           dubber_index_,
                           episode_selection_,
                           detail_id_,
                           detail_request_id_,
                           detail_loading_,
                           detail_,
                           players_,
                           stream_options_,
                           viewing_rows_,
                           search_rows_,
                           query_,
                           search_request_id_,
                           next_offset_,
                           search_has_more_};
  }
  void restore_navigation(NavigationState state, bool restore_search) {
    screen_ = state.screen;
    return_screen_ = state.return_screen;
    selected_ = state.selected;
    return_selected_ = state.return_selected;
    player_index_ = state.player;
    dubber_index_ = state.dubber;
    episode_selection_ = state.episode;
    detail_id_ = state.anime_id;
    detail_request_id_ = state.request_id;
    detail_loading_ = state.loading;
    detail_ = std::move(state.detail);
    players_ = std::move(state.players);
    stream_options_ = std::move(state.streams);
    viewing_rows_ = std::move(state.viewing);
    if (restore_search) {
      search_rows_ = std::move(state.search_rows);
      query_ = std::move(state.query);
      search_request_id_ = state.search_request_id;
      next_offset_ = state.next_offset;
      search_has_more_ = state.has_more;
    }
    if (detail_loading_ && screen_ != Screen::Feed &&
        screen_ != Screen::Search) {
      screen_ = Screen::Details;
      try {
        const auto reply =
            ipc_.request("subscribeAnime", {{"anime_id", detail_id_}});
        if (!reply.value("ok", false))
          throw std::runtime_error(reply.value("error", "Anime failed"));
        detail_request_id_ = reply.at("id").get<std::int64_t>();
        update_anime(reply.value("anime", Json::object()));
        detail_loading_ = reply.value("refreshing", false);
      } catch (const std::exception &error) {
        detail_loading_ = false;
        status_ = error.what();
      }
    }
    selected_ = bounded_selection(selected_, active_menu_size());
  }

  void load_feed() {
    try {
      const auto reply = ipc_.request("subscribeFeed");
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Feed failed"));
      update_feed(reply.value("feed", Json::object()));
      detail_back_stack_.clear();
      search_return_state_.reset();
      feed_request_id_ = reply.value("id", int64_t{-1});
      screen_ = Screen::Feed;
      selected_ = 0;
      status_ = reply.value("refreshing", false)
                    ? "Cached feed · refreshing API"
                    : "Feed loaded";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void add_feed_rows(const char* key) {
    const auto rows = feed_.value(key, Json::array());
    if (!rows.is_array()) return;
    for (const auto &row : rows) feed_rows_.push_back(anime_row(row));
  }

  void update_feed(const Json &snapshot) {
    feed_ = snapshot;
    feed_rows_.clear();
    add_feed_rows("new");
    if (feed_rows_.empty()) add_feed_rows("recommends");
  }

  void update_search(const Json &page, bool replace_page) {
    const auto offset = static_cast<std::size_t>(
        std::max<int64_t>(0, page.value("offset", int64_t{0})));
    const auto rows = page.value("results", Json::array());
    if (!rows.is_array()) return;
    if (offset == 0) {
      search_rows_.clear();
    } else if (replace_page && offset < search_rows_.size()) {
      search_rows_.resize(offset);
    }
    for (const auto &row : rows) search_rows_.push_back(anime_row(row));
    const auto limit = static_cast<std::size_t>(
        std::max<int64_t>(1, page.value("limit", int64_t{10})));
    search_has_more_ = rows.size() >= limit;
    next_offset_ = static_cast<int64_t>(offset + limit);
  }

  void update_anime(const Json &snapshot) {
    detail_loading_ = !snapshot.is_object();
    detail_ = snapshot.is_object() ? snapshot : Json::object();
    build_players();
    // A new authoritative model invalidates all menus derived from the old one.
    if (screen_ == Screen::Players || screen_ == Screen::Dubbers ||
        screen_ == Screen::Episodes || screen_ == Screen::StreamLinks ||
        screen_ == Screen::Viewing)
      screen_ = Screen::Details;
    player_index_ = dubber_index_ = episode_selection_ = 0;
    stream_options_.clear();
    selected_ = bounded_selection(selected_, active_menu_size());
  }

  void search_prompt() {
    const auto return_state = capture_navigation();
    std::string entered;
    status_ = "Type a title and press Enter · Esc cancels";
    log_line("/tmp/aniparser_tui-search.log", "search prompt opened");
    draw();
    for (;;) {
      ncinput input{};
      const auto key = notcurses_get(context_, nullptr, &input);
      if (key == static_cast<uint32_t>(-1)) {
        quit_requested_ = true;
        return;
      }
      if (input.evtype == NCTYPE_RELEASE) continue;
      log_line("/tmp/aniparser_tui-search.log",
               "prompt key=" + std::to_string(key));
      if (key == NCKEY_ESC) return;
      if (key == NCKEY_ENTER || key == '\n' || key == '\r') break;
      if ((key == NCKEY_BACKSPACE || key == 127) && !entered.empty()) {
        erase_last_utf8(entered);
      } else if (key >= 32 && key <= 0x10ffff) {
        append_utf8(entered, key);
      }
      status_ = "Search: " + entered + " · Enter to search · Esc to cancel";
      draw();
    }
    log_line("/tmp/aniparser_tui-search.log",
             "query entered: " + Json(entered).dump());
    if (screen_ != Screen::Search) search_return_state_ = return_state;
    search(entered, 0);
  }

  void search(const std::string &query, std::int64_t offset) {
    try {
      query_ = query;
      log_line("/tmp/aniparser_tui-search.log",
               "request scheduled: q=" + Json(query).dump() +
                   " offset=" + std::to_string(offset));
      const auto reply = ipc_.request(
          "subscribeSearch",
          {{"params", {{"q", query}, {"offset", offset}, {"limit", 10}}}});
      log_line("/tmp/aniparser_tui-search.log",
               "subscribeSearch reply: " + reply.dump());
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Search failed"));
      search_request_id_ = reply.value("id", int64_t{-1});
      update_search(reply, false);
      screen_ = Screen::Search;
      if (offset == 0) selected_ = 0;
      status_ = reply.value("refreshing", false)
                    ? "Cached results · refreshing API"
                    : "Search results";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void open_anime(std::int64_t id) {
    try {
      if (detail_back_stack_.size() >= 64)
        throw std::runtime_error("Navigation depth limit reached");
      auto previous = capture_navigation();
      const auto reply = ipc_.request("subscribeAnime", {{"anime_id", id}});
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Anime request failed"));
      detail_back_stack_.push_back(std::move(previous));
      return_screen_ = screen_;
      return_selected_ = selected_;
      detail_request_id_ = reply.value("id", int64_t{-1});
      detail_id_ = id;
      update_anime(reply.value("anime", Json::object()));
      detail_loading_ = reply.value("refreshing", false);
      screen_ = Screen::Details;
      selected_ = 0;
      status_ = detail_loading_ ? "Loading anime from API…"
                                : (reply.value("refreshing", false)
                                       ? "Cached anime · refreshing API"
                                       : "Anime details");
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void build_players() {
    players_.clear();
    for (const auto &source_player : detail_.value("players", Json::array())) {
      Player player{.name = value_string(source_player, "name")};
      for (const auto &source_dubber :
           source_player.value("dubbers", Json::array())) {
        Dubber dubber{.name = value_string(source_dubber, "name")};
        for (const auto &source_episode :
             source_dubber.value("episodes", Json::array()))
          dubber.episodes.push_back(
              {value_string(source_episode, "number"),
               value_string(source_episode, "iframe_url")});
        if (!dubber.episodes.empty())
          player.dubbers.push_back(std::move(dubber));
      }
      if (!player.dubbers.empty()) players_.push_back(std::move(player));
    }
    const auto remote_ids = detail_.value("remote_ids", Json::object());
    if (!value_string(remote_ids, "anilibria_alias").empty())
      players_.push_back(Player{.name = "AniLibria", .anilibria = true});
  }

  void activate() {
    if (screen_ == Screen::StreamLinks) {
      if (selected_ >= stream_options_.size()) return;
      const auto &option = stream_options_[selected_];
      if (copy_to_clipboard(option.url))
        status_ = "Copied " + quality_label(option.quality) +
                  " stream URL to clipboard";
      else status_ = "Clipboard unavailable · install wl-copy, xclip, or xsel";
      return;
    }
    if (screen_ == Screen::Details) {
      if (players_.empty()) {
        status_ = "No supported Kodik episodes";
        return;
      }
      screen_ = Screen::Players;
      selected_ = 0;
      status_ = "Choose player";
      return;
    }
    if (screen_ == Screen::Players) {
      if (selected_ >= players_.size()) return;
      player_index_ = selected_;
      if (players_[player_index_].anilibria) {
        load_anilibria_episodes();
        return;
      }
      screen_ = Screen::Dubbers;
      selected_ = 0;
      status_ = "Choose dubbing";
      return;
    }
    if (screen_ == Screen::Dubbers) {
      if (player_index_ >= players_.size() ||
          selected_ >= players_[player_index_].dubbers.size())
        return;
      dubber_index_ = selected_;
      screen_ = Screen::Episodes;
      selected_ = 0;
      status_ = "Choose episode";
      return;
    }
    if (screen_ == Screen::Episodes) {
      show_stream_links();
      return;
    }
    const auto &rows = active_rows();
    if (selected_ < rows.size()) open_anime(rows[selected_].id);
  }

  void load_anilibria_episodes() {
    if (player_index_ >= players_.size()) return;
    status_ = "Loading AniLibria episodes and HLS links…";
    try {
      const auto reply =
          ipc_.request("getAnilibriaVideos", {{"anime_id", detail_id_}});
      if (!reply.value("ok", false))
        throw std::runtime_error(
            reply.value("error", "AniLibria request failed"));
      Dubber dubber{.name = "AniLibria"};
      for (const auto &item : reply.value("episodes", Json::array())) {
        const auto ordinal = item.value("ordinal", int64_t{0});
        if (ordinal <= 0) continue;
        Episode episode{.number = std::to_string(ordinal)};
        for (const auto &quality : item.value("qualities", Json::array())) {
          const auto name = value_string(quality, "quality");
          const auto url = value_string(quality, "url");
          if (!name.empty() && !url.empty())
            episode.streams.push_back({name, url});
        }
        if (!episode.streams.empty())
          dubber.episodes.push_back(std::move(episode));
      }
      if (dubber.episodes.empty())
        throw std::runtime_error(
            "AniLibria returned no episodes with HLS links");
      players_[player_index_].dubbers.clear();
      players_[player_index_].dubbers.push_back(std::move(dubber));
      screen_ = Screen::Dubbers;
      selected_ = 0;
      status_ = "Choose dubbing";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  void show_stream_links() {
    if (player_index_ >= players_.size() ||
        dubber_index_ >= players_[player_index_].dubbers.size())
      return;
    const auto &episodes =
        players_[player_index_].dubbers[dubber_index_].episodes;
    if (selected_ >= episodes.size()) return;
    status_ = "Parsing episode stream links…";
    try {
      stream_options_.clear();
      const auto &episode = episodes[selected_];
      if (players_[player_index_].anilibria) {
        stream_options_ = episode.streams;
      } else {
        const auto reply =
            ipc_.request("getVideoOptions", {{"iframe", episode.iframe}});
        if (!reply.value("ok", false))
          throw std::runtime_error(reply.value("error", "Video parse failed"));
        const auto qualities = reply.value("qualities", Json::array());
        if (!qualities.is_array() || qualities.empty())
          throw std::runtime_error("No stream qualities returned");
        for (const auto &option : qualities) {
          const auto quality = value_string(option, "quality");
          const auto url = value_string(option, "url");
          if (!quality.empty() && !url.empty())
            stream_options_.push_back({quality, url});
        }
      }
      if (stream_options_.empty())
        throw std::runtime_error("No valid stream links returned");
      episode_selection_ = selected_;
      screen_ = Screen::StreamLinks;
      selected_ = 0;
      status_ = "Available stream links · Esc returns to episodes";
    } catch (const std::exception &error) { status_ = error.what(); }
  }

  std::vector<std::vector<std::string>>
  stream_link_blocks(unsigned width) const {
    const auto chunk_size =
        static_cast<std::size_t>(std::max(1u, width > 8 ? width - 8 : 1));
    std::vector<std::vector<std::string>> blocks;
    for (const auto &option : stream_options_) {
      std::vector<std::string> lines;
      lines.push_back("Quality " + quality_label(option.quality) +
                      " · Enter copies URL");
      for (std::size_t offset = 0; offset < option.url.size();
           offset += chunk_size)
        lines.push_back("  " + option.url.substr(offset, chunk_size));
      blocks.push_back(std::move(lines));
    }
    return blocks;
  }

  void show_viewing_order() {
    viewing_rows_.clear();
    const auto order = detail_.value("viewing_order", Json::array());
    for (const auto &item : order) viewing_rows_.push_back(anime_row(item));
    screen_ = Screen::Viewing;
    selected_ = 0;
    status_ = "Viewing order";
  }

  void move(int delta) {
    const auto count = active_menu_size();
    if (!count) return;
    selected_ =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(selected_) + delta,
                                 0, static_cast<std::int64_t>(count - 1));
  }

  std::size_t active_menu_size() const {
    switch (screen_) {
    case Screen::Players:
      return players_.size();
    case Screen::Dubbers:
      return player_index_ < players_.size()
                 ? players_[player_index_].dubbers.size()
                 : 0;
    case Screen::Episodes:
      return player_index_ < players_.size() &&
                     dubber_index_ < players_[player_index_].dubbers.size()
                 ? players_[player_index_]
                       .dubbers[dubber_index_]
                       .episodes.size()
                 : 0;
    case Screen::StreamLinks:
      return stream_options_.size();
    default:
      return active_rows().size();
    }
  }

  const std::vector<AnimeRow> &active_rows() const {
    if (screen_ == Screen::Feed) return feed_rows_;
    if (screen_ == Screen::Viewing) return viewing_rows_;
    return search_rows_;
  }

  void go_back() {
    if (screen_ == Screen::StreamLinks) {
      screen_ = Screen::Episodes;
      selected_ = episode_selection_;
    } else if (screen_ == Screen::Episodes) {
      screen_ = Screen::Dubbers;
      selected_ = dubber_index_;
    } else if (screen_ == Screen::Dubbers) {
      screen_ = Screen::Players;
      selected_ = player_index_;
    } else if (screen_ == Screen::Players || screen_ == Screen::Viewing) {
      screen_ = Screen::Details;
      selected_ = 0;
    } else if (screen_ == Screen::Details) {
      if (!detail_back_stack_.empty()) {
        auto previous = std::move(detail_back_stack_.back());
        detail_back_stack_.pop_back();
        restore_navigation(std::move(previous), false);
      } else {
        screen_ = Screen::Feed;
        selected_ = 0;
      }
    } else if (screen_ == Screen::Search) {
      if (search_return_state_) {
        auto state = std::move(*search_return_state_);
        search_return_state_.reset();
        restore_navigation(std::move(state), true);
      } else {
        screen_ = Screen::Feed;
        selected_ = 0;
      }
    }
    selected_ = bounded_selection(selected_, active_menu_size());
  }

  void pump_events() {
    for (const auto &event : ipc_.poll_messages()) {
      const auto type = event.value("event", std::string{});
      if (type == "feed.updated" &&
          event.value("id", int64_t{-1}) == feed_request_id_) {
        if (!event.value("ok", false)) {
          status_ = event.value("error", "Feed failed");
          continue;
        }
        update_feed(event.value("feed", Json::object()));
        status_ = "Feed refreshed";
      } else if (type == "search.updated" &&
                 event.value("id", int64_t{-1}) == search_request_id_) {
        log_line("/tmp/aniparser_tui-search.log",
                 "search.updated event: " + event.dump());
        if (!event.value("ok", false)) {
          status_ = event.value("error", "Search failed");
          continue;
        }
        update_search(event, true);
        status_ = "Search results refreshed";
      } else if (type == "anime.updated" &&
                 event.value("id", int64_t{-1}) == detail_request_id_) {
        detail_loading_ = false;
        log_line("/tmp/aniparser_tui-anime.log",
                 "anime.updated event: " + event.dump());
        if (event.value("ok", false) && !event.at("anime").is_null()) {
          update_anime(event.at("anime"));
          status_ = "Anime details updated";
        } else {
          const auto error = event.value("error", std::string{});
          const auto api_status = event.value("status", 0);
          status_ = "Anime refresh failed";
          if (api_status > 0)
            status_ += " (HTTP " + std::to_string(api_status) + ")";
          if (!error.empty()) status_ += ": " + error;
        }
      } else if (type == "history.updated") {
        updateHistory(event);
      }
      selected_ = active_menu_size() == 0
                      ? 0
                      : std::min(selected_, active_menu_size() - 1);
    }
  }

  void updateHistory(const Json &message) {
    const auto revision = message.value("revision", history_revision_);
    if (revision < history_revision_) return;
    const bool updated =
        message.value("event", std::string{}) == "history.updated";
    const auto sequence = message.value("sequence", std::uint64_t{0});
    if (sequence < history_sequence_ ||
        (updated && sequence <= history_sequence_))
      return;
    const auto items = message.value("items", Json::array());
    if (!items.is_array()) return;
    history_ = items;
    history_revision_ = revision;
    history_sequence_ = sequence;
    if (updated)
      status_ = message.value("ok", false)
                    ? "History updated"
                    : "History refresh failed: " +
                          message.value("error", std::string{});
  }

  void text(int y,
            int x,
            std::string_view value,
            uint64_t fg = 0xffffff,
            uint64_t bg = kBackground,
            uint16_t style = NCSTYLE_NONE) {
    ncplane_set_fg_rgb(plane_, fg);
    ncplane_set_bg_rgb(plane_, bg);
    ncplane_putstr_yx(plane_, y, x, std::string(value).c_str());
    ncplane_set_styles(plane_, style);
  }

  void draw() {
    unsigned height = 0;
    unsigned width = 0;
    ncplane_dim_yx(plane_, &height, &width);
    (void)width;
    ncplane_set_bg_rgb(plane_, kBackground);
    ncplane_erase(plane_);
    text(1, 2, "ANIPARSER", kAccent, kBackground, NCSTYLE_BOLD);
    const auto heading = screen_ == Screen::Feed          ? "Feed"
                         : screen_ == Screen::Search      ? "Search"
                         : screen_ == Screen::Details     ? "Anime"
                         : screen_ == Screen::Viewing     ? "Viewing order"
                         : screen_ == Screen::Players     ? "Choose player"
                         : screen_ == Screen::Dubbers     ? "Choose dubbing"
                         : screen_ == Screen::StreamLinks ? "Stream links"
                                                          : "Choose episode";
    text(2, 2, heading, kAccent, kBackground, NCSTYLE_BOLD);
    if (screen_ == Screen::Players || screen_ == Screen::Dubbers ||
        screen_ == Screen::Episodes || screen_ == Screen::StreamLinks) {
      auto context = value_string(detail_, "title");
      if (screen_ == Screen::Dubbers && player_index_ < players_.size())
        context += " · " + players_[player_index_].name;
      else if ((screen_ == Screen::Episodes ||
                screen_ == Screen::StreamLinks) &&
               player_index_ < players_.size() &&
               dubber_index_ < players_[player_index_].dubbers.size())
        context += " · " + players_[player_index_].name + " · " +
                   players_[player_index_].dubbers[dubber_index_].name;
      if (screen_ == Screen::StreamLinks && player_index_ < players_.size() &&
          dubber_index_ < players_[player_index_].dubbers.size()) {
        const auto &episodes =
            players_[player_index_].dubbers[dubber_index_].episodes;
        if (episode_selection_ < episodes.size())
          context += " · Episode " + episodes[episode_selection_].number;
      }
      text(3, 3, context, kMuted);
    }
    if (screen_ == Screen::Details) {
      if (detail_loading_) text(3, 3, "Loading anime…", kMuted);
      text(4, 3, value_string(detail_, "title"), 0xffffff, kPanel,
           NCSTYLE_BOLD);
      std::string metadata =
          value_string(detail_.value("type", Json::object()), "name");
      const auto year = detail_.value("year", int64_t{0});
      if (year > 0)
        metadata += (metadata.empty() ? "" : " · ") + std::to_string(year);
      const auto rating =
          detail_.value("rating", Json::object()).value("average", 0.0);
      if (rating > 0) {
        if (!metadata.empty()) metadata += " · ";
        metadata += "★ " + std::to_string(rating);
      }
      const auto status =
          value_string(detail_.value("anime_status", Json::object()), "title");
      if (!status.empty()) {
        if (!metadata.empty()) metadata += " · ";
        metadata += status;
      }
      text(5, 3, metadata, kMuted);

      std::string details;
      const auto episodes =
          detail_.value("episodes", Json::object()).value("count", int64_t{0});
      if (episodes > 0) details = "Episodes: " + std::to_string(episodes);
      const auto age =
          value_string(detail_.value("min_age", Json::object()), "title");
      if (!age.empty()) {
        if (!details.empty()) details += " · ";
        details += age;
      }
      const auto views = detail_.value("views", int64_t{0});
      if (views > 0) {
        if (!details.empty()) details += " · ";
        details += "Views: " + std::to_string(views);
      }
      text(6, 3, details, 0xd5dde6);

      const auto studios =
          joined_names(detail_.value("studios", Json::array()), "title");
      if (!studios.empty()) text(8, 3, "Studios: " + studios, 0xd5dde6);
      const auto genres =
          joined_names(detail_.value("genres", Json::array()), "title");
      if (!genres.empty()) text(9, 3, "Genres: " + genres, 0xd5dde6);
      const auto other_titles =
          joined_names(detail_.value("other_titles", Json::array()), "");
      if (!other_titles.empty())
        text(10, 3, "Also known as: " + other_titles, 0xd5dde6);
      text(12, 3, value_string(detail_, "description"), 0xd5dde6);
      text(14, 3, "Enter choose player · v viewing order", kMuted);
    } else if (screen_ == Screen::StreamLinks) {
      const auto blocks = stream_link_blocks(width);
      const auto visible =
          static_cast<std::size_t>(std::max(1, static_cast<int>(height) - 7));
      std::vector<std::pair<std::string, std::size_t>> lines;
      std::vector<std::size_t> block_starts;
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        block_starts.push_back(lines.size());
        for (const auto &line : blocks[i]) lines.emplace_back(line, i);
      }
      const auto first =
          lines.size() <= visible || block_starts.empty()
              ? 0
              : block_starts[std::min(selected_, block_starts.size() - 1)];
      const auto end = std::min(lines.size(), first + visible);
      for (std::size_t i = first; i < end; ++i) {
        const bool selected_link = lines[i].second == selected_;
        text(4 + static_cast<int>(i - first), 3, lines[i].first,
             selected_link ? kAccent : 0xd5dde6,
             selected_link ? kPanel : kBackground);
      }
    } else if (screen_ == Screen::Players || screen_ == Screen::Dubbers ||
               screen_ == Screen::Episodes) {
      std::vector<std::string> choices;
      if (screen_ == Screen::Players)
        for (const auto &player : players_) choices.push_back(player.name);
      else if (screen_ == Screen::Dubbers && player_index_ < players_.size())
        for (const auto &dubber : players_[player_index_].dubbers)
          choices.push_back(dubber.name);
      else if (screen_ == Screen::Episodes && player_index_ < players_.size() &&
               dubber_index_ < players_[player_index_].dubbers.size())
        for (const auto &episode :
             players_[player_index_].dubbers[dubber_index_].episodes)
          choices.push_back("Episode " + episode.number);
      draw_menu(choices, height);
    } else {
      const auto &rows = active_rows();
      const auto visible =
          static_cast<std::size_t>(std::max(1, static_cast<int>(height) - 7));
      const auto first = selected_ >= visible ? selected_ - visible + 1 : 0;
      const auto end = std::min(rows.size(), first + visible);
      for (std::size_t i = first; i < end; ++i) {
        const auto screen_row = i - first;
        const auto bg = i == selected_ ? 0x263a44 : kPanel;
        text(4 + static_cast<int>(screen_row), 3,
             rows[i].title + "  " + rows[i].info,
             i == selected_ ? kAccent : 0xffffff, bg);
      }
    }
    text(static_cast<int>(height) - 2, 2, status_, kMuted);
    const auto help =
        screen_ == Screen::Episodes
            ? "↑↓ select · Enter show stream links · Esc back · q quit"
        : screen_ == Screen::StreamLinks
            ? "↑↓ select quality · Enter copy URL · Esc back · q quit"
            : "↑↓ select · Enter open · / search · f feed · n next · Esc back "
              "· q quit";
    text(static_cast<int>(height) - 1, 2, help, kAccent);
    notcurses_render(context_);
  }

  void draw_menu(const std::vector<std::string> &choices, unsigned height) {
    const auto visible =
        static_cast<std::size_t>(std::max(1, static_cast<int>(height) - 7));
    const auto first = selected_ >= visible ? selected_ - visible + 1 : 0;
    const auto end = std::min(choices.size(), first + visible);
    for (std::size_t i = first; i < end; ++i) {
      const auto screen_row = i - first;
      text(4 + static_cast<int>(screen_row), 3,
           (i == selected_ ? "> " : "  ") + choices[i],
           i == selected_ ? kAccent : 0xffffff,
           i == selected_ ? 0x263a44 : kPanel);
    }
  }

  std::deque<std::pair<uint32_t, ncinput>> pending_input_;
  IpcClient &ipc_;
  notcurses* context_;
  ncplane* plane_;
  Screen screen_ = Screen::Feed;
  Screen return_screen_ = Screen::Feed;
  std::size_t return_selected_ = 0;
  std::size_t selected_ = 0;
  std::size_t player_index_ = 0;
  std::size_t dubber_index_ = 0;
  std::size_t episode_selection_ = 0;
  std::int64_t feed_request_id_ = -1;
  std::int64_t search_request_id_ = -1;
  std::int64_t detail_request_id_ = -1;
  std::int64_t detail_id_ = 0;
  std::int64_t next_offset_ = 10;
  bool search_has_more_ = false;
  bool detail_loading_ = false;
  bool quit_requested_ = false;
  std::string query_;
  std::string status_ = "Ready";
  Json feed_ = Json::object();
  Json detail_ = Json::object();
  Json history_ = Json::array();
  std::uint64_t history_revision_ = 0;
  std::uint64_t history_sequence_ = 0;
  std::vector<AnimeRow> feed_rows_;
  std::vector<AnimeRow> search_rows_;
  std::vector<AnimeRow> viewing_rows_;
  std::vector<Player> players_;
  std::vector<StreamOption> stream_options_;
};
} // namespace

int run(const std::string &endpoint) {
  IpcClient ipc;
  if (!ipc.connect_to(endpoint)) {
    std::cerr << "Cannot connect to IPC server: " << endpoint << '\n';
    return 1;
  }
  try {
    const auto hello = ipc.request("hello");
    if (!hello.value("ok", false) || hello.value("protocol", 0) != 3)
      throw std::runtime_error("IPC protocol is incompatible");
    const auto stop_context = [](notcurses* context) {
      notcurses_stop(context);
    };
    std::unique_ptr<notcurses, decltype(stop_context)> context(
        notcurses_core_init(nullptr, stdout), stop_context);
    if (!context) throw std::runtime_error("Cannot initialize notcurses");
    TuiApplication tui(ipc, context.get());
    tui.initialize();
    const int result = tui.run();
    return result;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
} // namespace aniparser::tui
