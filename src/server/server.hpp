#pragma once

#include <core.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <string_view>
#include <vector>

#include "video_extractor.hpp"

// Application facade. It exposes API models and value types to IPC clients;
// storage and refresh policy stay encapsulated in Core.
class Server {
  public:
  using AnimeCallback = Core::AnimeCallback;
  using SearchCallback = Core::SearchCallback;
  using FeedCallback = Core::FeedCallback;

  explicit Server(Core &core) : core_(core) {}

  void getAnime(std::int64_t anime_id, AnimeCallback callback);
  bool animeNeedsRefresh(std::int64_t anime_id) const;
  bool animeIsLoaded(std::int64_t anime_id) const;

  bool getSearch(yani::SearchInput params, SearchCallback callback);
  bool searchPageNeedsRefresh(const yani::SearchInput &params) const;
  bool getFeed(FeedCallback callback);
  bool feedNeedsRefresh() const;

  static nlohmann::json animeToJson(const yani::AnimeResult &anime);

  yani::Result<std::string> getVideo(std::string_view iframe,
                                     std::string_view quality = "720");
  yani::Result<std::vector<yani::VideoQuality>>
  getVideoOptions(std::string_view iframe);

  private:
  Core &core_;
};
