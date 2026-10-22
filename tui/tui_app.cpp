#include "tui_app.hpp"

#include <nlohmann/json.hpp>
#include <notcurses/notcurses.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "clipboard.hpp"
#include "ipc_client.hpp"

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

bool has_value(const Json &value) {
  if (value.is_null()) return false;
  if (value.is_string()) return !value.get_ref<const std::string &>().empty();
  if (value.is_array() || value.is_object()) return !value.empty();
  if (value.is_number()) return value.get<double>() != 0.0;
  return true;
}

void merge_snapshot(Json &target, const Json &update) {
  if (target.is_object() && update.is_object()) {
    for (auto it = update.begin(); it != update.end(); ++it) {
      if (!has_value(it.value())) continue;
      if (!target.contains(it.key())) target[it.key()] = it.value();
      else merge_snapshot(target[it.key()], it.value());
    }
  } else if (has_value(update)) {
    target = update;
  }
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

void erase_last_utf8(std::string &text) {
  if (text.empty()) return;
  text.pop_back();
  while (!text.empty() &&
         (static_cast<unsigned char>(text.back()) & 0xc0) == 0x80)
    text.pop_back();
}

struct Episode {
  std::string number;
  std::string iframe;
};
struct Dubber {
  std::string name;
  std::vector<Episode> episodes;
};
struct Player {
  std::string name;
  std::vector<Dubber> dubbers;
};
struct StreamOption {
  std::string quality;
  std::string url;
};

class TuiApplication {
  public:
  TuiApplication(IpcClient &ipc, notcurses* context)
    : ipc_(ipc)
    , context_(context)
    , plane_(notcurses_stdplane(context)) {}

  void initialize() { load_feed(); }
  int run() {
    bool running = true;
    while (running) {
      pump_events();
      draw();
      timespec timeout{0, 100'000'000};
      ncinput input{};
      const auto key = notcurses_get(context_, &timeout, &input);
      if (key == (uint32_t)-1 || key == 0 || input.evtype == NCTYPE_RELEASE)
        continue;
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

  void load_feed() {
    try {
      const auto reply = ipc_.request("getFeed");
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Feed failed"));
      update_feed(reply.value("feed", Json::object()));
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

  void update_anime(const Json &snapshot, bool merge = false) {
    if (merge && detail_.is_object()) merge_snapshot(detail_, snapshot);
    else detail_ = snapshot;
    build_players();
  }

  void search_prompt() {
    search_return_screen_ = screen_;
    search_return_selected_ = selected_;
    query_.clear();
    status_ = "Type a title and press Enter · Esc cancels";
    log_line("/tmp/aniparser_tui-search.log", "search prompt opened");
    draw();
    for (;;) {
      ncinput input{};
      const auto key = notcurses_get(context_, nullptr, &input);
      if (input.evtype == NCTYPE_RELEASE) continue;
      log_line("/tmp/aniparser_tui-search.log",
               "prompt key=" + std::to_string(key));
      if (key == NCKEY_ESC) return;
      if (key == NCKEY_ENTER || key == '\n' || key == '\r') break;
      if ((key == NCKEY_BACKSPACE || key == 127) && !query_.empty()) {
        erase_last_utf8(query_);
      } else if (key >= 32 && key <= 0x10ffff) {
        append_utf8(query_, key);
      }
      status_ = "Search: " + query_ + " · Enter to search · Esc to cancel";
      draw();
    }
    log_line("/tmp/aniparser_tui-search.log",
             "query entered: " + Json(query_).dump());
    search(query_, 0);
  }

  void search(const std::string &query, std::int64_t offset) {
    try {
      query_ = query;
      log_line("/tmp/aniparser_tui-search.log",
               "request scheduled: q=" + Json(query).dump() +
                   " offset=" + std::to_string(offset));
      const auto reply = ipc_.request(
          "getSearch",
          {{"params", {{"q", query}, {"offset", offset}, {"limit", 10}}}});
      log_line("/tmp/aniparser_tui-search.log",
               "getSearch reply: " + reply.dump());
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
      return_screen_ = screen_;
      return_selected_ = selected_;
      const auto reply = ipc_.request("getAnime", {{"anime_id", id}});
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Anime request failed"));
      detail_request_id_ = reply.value("id", int64_t{-1});
      update_anime(reply.value("anime", Json::object()));
      detail_id_ = id;
      screen_ = Screen::Details;
      selected_ = 0;
      status_ = detail_.is_null() ? "Loading anime from API…"
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
  }

  void activate() {
    if (screen_ == Screen::StreamLinks) {
      if (selected_ >= stream_options_.size()) return;
      const auto &option = stream_options_[selected_];
      if (copy_to_clipboard(option.url))
        status_ = "Copied " + option.quality + "p stream URL to clipboard";
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
      player_index_ = selected_;
      screen_ = Screen::Dubbers;
      selected_ = 0;
      status_ = "Choose dubbing";
      return;
    }
    if (screen_ == Screen::Dubbers) {
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

  void show_stream_links() {
    if (player_index_ >= players_.size() ||
        dubber_index_ >= players_[player_index_].dubbers.size())
      return;
    const auto &episodes =
        players_[player_index_].dubbers[dubber_index_].episodes;
    if (selected_ >= episodes.size()) return;
    status_ = "Parsing episode stream links…";
    try {
      const auto reply = ipc_.request("getVideoOptions",
                                      {{"iframe", episodes[selected_].iframe}});
      if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Video parse failed"));
      const auto qualities = reply.value("qualities", Json::array());
      if (!qualities.is_array() || qualities.empty())
        throw std::runtime_error("No stream qualities returned");
      stream_options_.clear();
      for (const auto &option : qualities) {
        const auto quality = value_string(option, "quality");
        const auto url = value_string(option, "url");
        if (!quality.empty() && !url.empty())
          stream_options_.push_back({quality, url});
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
      lines.push_back("Quality " + option.quality + "p · Enter copies URL");
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
      screen_ = return_screen_;
      selected_ = return_selected_;
    } else if (screen_ == Screen::Search) {
      screen_ = search_return_screen_;
      selected_ = search_return_selected_;
    }
  }

  void pump_events() {
    for (const auto &event : ipc_.poll_messages()) {
      const auto type = event.value("event", std::string{});
      if (type == "feed.updated" &&
          event.value("id", int64_t{-1}) == feed_request_id_) {
        update_feed(event.value("feed", Json::object()));
        status_ = "Feed refreshed";
      } else if (type == "search.updated" &&
                 event.value("id", int64_t{-1}) == search_request_id_) {
        log_line("/tmp/aniparser_tui-search.log",
                 "search.updated event: " + event.dump());
        update_search(event, true);
        status_ = "Search results refreshed";
      } else if (type == "anime.updated" &&
                 event.value("id", int64_t{-1}) == detail_request_id_) {
        log_line("/tmp/aniparser_tui-anime.log",
                 "anime.updated event: " + event.dump());
        if (event.value("ok", false) && !event.at("anime").is_null()) {
          update_anime(event.at("anime"), true);
          status_ = "Anime details updated";
        } else {
          const auto error = event.value("error", std::string{});
          const auto api_status = event.value("status", 0);
          status_ = "Anime refresh failed";
          if (api_status > 0)
            status_ += " (HTTP " + std::to_string(api_status) + ")";
          if (!error.empty()) status_ += ": " + error;
        }
      }
      selected_ = active_menu_size() == 0
                      ? 0
                      : std::min(selected_, active_menu_size() - 1);
    }
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

  IpcClient &ipc_;
  notcurses* context_;
  ncplane* plane_;
  Screen screen_ = Screen::Feed;
  Screen return_screen_ = Screen::Feed;
  Screen search_return_screen_ = Screen::Feed;
  std::size_t return_selected_ = 0;
  std::size_t search_return_selected_ = 0;
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
  std::string query_;
  std::string status_ = "Ready";
  Json feed_ = Json::object();
  Json detail_ = Json::object();
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
    if (!hello.value("ok", false) || hello.value("protocol", 0) != 2)
      throw std::runtime_error("IPC protocol is incompatible");
    auto* context = notcurses_init(nullptr, stdout);
    if (!context) throw std::runtime_error("Cannot initialize notcurses");
    TuiApplication tui(ipc, context);
    tui.initialize();
    const int result = tui.run();
    notcurses_stop(context);
    return result;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
} // namespace aniparser::tui
