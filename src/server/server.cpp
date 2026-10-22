#include "server.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "json.hpp"
#include "kodik.hpp"

void Server::getAnime(std::int64_t anime_id, AnimeCallback callback) {
  core_.getAnime(anime_id, std::move(callback), true);
}

bool Server::animeNeedsRefresh(std::int64_t anime_id) const {
  return core_.animeNeedsRefresh(anime_id);
}

bool Server::animeIsLoaded(std::int64_t anime_id) const {
  return core_.animeIsLoaded(anime_id);
}

bool Server::getSearch(yani::SearchInput params, SearchCallback callback) {
  return core_.getSearch(std::move(params), std::move(callback));
}

bool Server::searchPageNeedsRefresh(const yani::SearchInput &params) const {
  return core_.searchPageNeedsRefresh(params);
}

bool Server::getFeed(FeedCallback callback) {
  return core_.getFeed(std::move(callback));
}

bool Server::feedNeedsRefresh() const { return core_.feedNeedsRefresh(); }

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
