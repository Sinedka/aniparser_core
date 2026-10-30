#include "server.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "json.hpp"
#include "kodik.hpp"

Server::AnimeSubscriptionId Server::subscribeAnime(std::int64_t anime_id,
                                                   AnimeCallback callback) {
  return core_.subscribeAnime(anime_id, std::move(callback), true);
}

bool Server::unsubscribeAnime(AnimeSubscriptionId subscription_id) {
  return core_.unsubscribeAnime(subscription_id);
}

bool Server::animeNeedsRefresh(std::int64_t anime_id) const {
  return core_.animeNeedsRefresh(anime_id);
}

bool Server::animeIsLoaded(std::int64_t anime_id) const {
  return core_.animeIsLoaded(anime_id);
}

bool Server::subscribeSearch(yani::SearchInput params,
                             SearchCallback callback) {
  return core_.subscribeSearch(std::move(params), std::move(callback));
}

bool Server::searchPageNeedsRefresh(const yani::SearchInput &params) const {
  return core_.searchPageNeedsRefresh(params);
}

bool Server::subscribeFeed(FeedCallback callback) {
  return core_.subscribeFeed(std::move(callback));
}

bool Server::feedNeedsRefresh() const { return core_.feedNeedsRefresh(); }

bool Server::addHistory(std::int64_t anime_id) {
  return core_.addHistory(anime_id);
}

bool Server::removeHistory(std::int64_t anime_id) {
  return core_.removeHistory(anime_id);
}

bool Server::updateAnimeSaveData(Core::AnimeSaveData data) {
  return core_.updateAnimeSaveData(std::move(data));
}

std::optional<Core::AnimeSaveData>
Server::getAnimeSaveData(std::int64_t anime_id) const {
  return core_.getAnimeSaveData(anime_id);
}

std::vector<Core::HistoryEntry> Server::getHistory() const {
  return core_.getHistory();
}

Server::HistorySnapshot Server::getHistorySnapshot(std::size_t offset,
                                                   std::size_t limit) const {
  return core_.getHistorySnapshot(offset, limit);
}

std::uint64_t Server::getHistoryRevision() const {
  return core_.getHistoryRevision();
}

Server::HistorySubscriptionId Server::subscribeHistory(HistoryCallback callback,
                                                       std::size_t offset,
                                                       std::size_t limit) {
  return core_.subscribeHistory(std::move(callback), offset, limit);
}

void Server::cancelHistorySubscription(HistorySubscriptionId subscription_id) {
  core_.cancelHistorySubscription(subscription_id);
}

bool Server::addFavorite(std::int64_t anime_id) {
  return core_.addFavorite(anime_id);
}

bool Server::removeFavorite(std::int64_t anime_id) {
  return core_.removeFavorite(anime_id);
}

bool Server::isFavorite(std::int64_t anime_id) const {
  return core_.isFavorite(anime_id);
}

int Server::getStatus(std::int64_t anime_id) const {
  return core_.getStatus(anime_id);
}

Server::HistorySnapshot Server::getFavoritesSnapshot(std::size_t offset,
                                                     std::size_t limit) const {
  return core_.getFavoritesSnapshot(offset, limit);
}

std::uint64_t Server::getFavoritesRevision() const {
  return core_.getFavoritesRevision();
}

Server::HistorySubscriptionId Server::subscribeFavorites(
    HistoryCallback callback, std::size_t offset, std::size_t limit) {
  return core_.subscribeFavorites(std::move(callback), offset, limit);
}

void Server::cancelFavoritesSubscription(HistorySubscriptionId id) {
  core_.cancelFavoritesSubscription(id);
}

#define DEFINE_ANIME_STATUS(Name)                                              \
  bool Server::add##Name(std::int64_t id) { return core_.add##Name(id); }      \
  bool Server::remove##Name(std::int64_t id) {                                 \
    return core_.remove##Name(id);                                             \
  }                                                                            \
  bool Server::is##Name(std::int64_t id) const { return core_.is##Name(id); }  \
  Server::HistorySnapshot Server::get##Name##Snapshot(                         \
      std::size_t offset, std::size_t limit) const {                           \
    return core_.get##Name##Snapshot(offset, limit);                           \
  }                                                                            \
  std::uint64_t Server::get##Name##Revision() const {                          \
    return core_.get##Name##Revision();                                        \
  }                                                                            \
  Server::HistorySubscriptionId Server::subscribe##Name(                       \
      HistoryCallback callback, std::size_t offset, std::size_t limit) {       \
    return core_.subscribe##Name(std::move(callback), offset, limit);          \
  }                                                                            \
  void Server::cancel##Name##Subscription(HistorySubscriptionId id) {          \
    core_.cancel##Name##Subscription(id);                                      \
  }

DEFINE_ANIME_STATUS(Planned)
DEFINE_ANIME_STATUS(Watched)
DEFINE_ANIME_STATUS(Completed)
DEFINE_ANIME_STATUS(Dropped)
#undef DEFINE_ANIME_STATUS

nlohmann::json
Server::historyToJson(const std::vector<Core::HistoryItem> &history) {
  auto result = nlohmann::json::array();
  for (const auto &entry : history) {
    result.push_back(
        {{"anime_id", entry.anime_id},
         {"anime",
          entry.anime ? animeToJson(*entry.anime) : nlohmann::json(nullptr)},
         {"save_data", entry.save_data ? animeSaveDataToJson(*entry.save_data)
                                       : nlohmann::json(nullptr)}});
  }
  return result;
}

nlohmann::json Server::animeSaveDataToJson(const Core::AnimeSaveData &data) {
  return {{"anime_id", data.anime_id},
          {"player", data.player},
          {"dubber", data.dubber},
          {"episode", data.episode},
          {"time", data.time}};
}

nlohmann::json Server::animeToJson(const yani::AnimeResult &anime) {
  nlohmann::json result = anime;
  result.erase("videos");
  result["players"] = nlohmann::json::array();

  for (const auto &video : anime.videos) {
    if (!yani::supportsVideo(video.iframe_url) || video.data.player.empty() ||
        video.data.dubbing.empty())
      continue;

    auto &players = result["players"];
    auto player =
        std::find_if(players.begin(), players.end(), [&](const auto &item) {
          return item.value("name", std::string{}) == video.data.player;
        });
    if (player == players.end()) {
      players.push_back(
          {{"name", video.data.player}, {"dubbers", nlohmann::json::array()}});
      player = std::prev(players.end());
    }

    auto &dubbers = (*player)["dubbers"];
    auto dubber =
        std::find_if(dubbers.begin(), dubbers.end(), [&](const auto &item) {
          return item.value("name", std::string{}) == video.data.dubbing;
        });
    if (dubber == dubbers.end()) {
      dubbers.push_back({{"name", video.data.dubbing},
                         {"episodes", nlohmann::json::array()}});
      dubber = std::prev(dubbers.end());
    }
    (*dubber)["episodes"].push_back({{"video_id", video.video_id},
                                     {"iframe_url", video.iframe_url},
                                     {"number", video.number},
                                     {"date", video.date},
                                     {"index", video.index},
                                     {"skips", video.skips}});
  }

  const auto episode_number =
      [](const nlohmann::json &episode) -> std::optional<double> {
    const auto number = episode.value("number", std::string{});
    double value = 0;
    const auto parsed =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != number.data() + number.size() || !std::isfinite(value))
      return std::nullopt;
    return value;
  };
  for (auto &player : result["players"])
    for (auto &dubber : player["dubbers"]) {
      auto &episodes = dubber["episodes"];
      std::stable_sort(episodes.begin(), episodes.end(),
                       [&](const auto &left, const auto &right) {
                         const auto left_number = episode_number(left);
                         const auto right_number = episode_number(right);
                         if (left_number && right_number)
                           return *left_number < *right_number;
                         if (left_number.has_value() !=
                             right_number.has_value())
                           return left_number.has_value();
                         return left.value("number", std::string{}) <
                                right.value("number", std::string{});
                       });
    }
  return result;
}

yani::Result<std::string> Server::getVideo(std::string_view iframe,
                                           std::string_view quality) {
  return yani::getVideo(iframe, quality);
}

yani::Result<std::vector<yani::VideoQuality>>
Server::getVideoOptions(std::string_view iframe) {
  return yani::getVideoOptions(iframe);
}
