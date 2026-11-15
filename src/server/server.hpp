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
  using AnimeSubscriptionId = Core::AnimeSubscriptionId;
  using SearchCallback = Core::SearchCallback;
  using ErrorCallback = Core::ErrorCallback;
  using FeedCallback = Core::FeedCallback;
  using HistoryCallback = Core::HistoryCallback;
  using HistorySubscriptionId = Core::HistorySubscriptionId;
  using HistorySnapshot = Core::HistorySnapshot;

  explicit Server(Core &core) : core_(core) {}

  AnimeSubscriptionId subscribeAnime(std::int64_t anime_id,
                                     AnimeCallback callback,
                                     bool* refreshing = nullptr);
  bool unsubscribeAnime(AnimeSubscriptionId subscription_id);
  bool animeNeedsRefresh(std::int64_t anime_id) const;
  bool animeIsLoaded(std::int64_t anime_id) const;

  bool subscribeSearch(yani::SearchInput params,
                       SearchCallback callback,
                       ErrorCallback on_error = {},
                       bool* refreshing = nullptr);
  bool searchPageNeedsRefresh(const yani::SearchInput &params) const;
  bool subscribeFeed(FeedCallback callback,
                     ErrorCallback on_error = {},
                     bool* refreshing = nullptr);
  bool feedNeedsRefresh() const;

  yani::Result<bool> addHistory(std::int64_t anime_id);
  yani::Result<bool> removeHistory(std::int64_t anime_id);
  yani::Result<bool> updateAnimeSaveData(Core::AnimeSaveData data);
  yani::Result<bool> removeAnimeSaveData(std::int64_t anime_id);
  std::optional<Core::AnimeSaveData>
  getAnimeSaveData(std::int64_t anime_id) const;
  std::vector<Core::HistoryEntry> getHistory() const;
  HistorySnapshot getHistorySnapshot(std::size_t offset = 0,
                                     std::size_t limit = 20) const;
  std::uint64_t getHistoryRevision() const;
  HistorySubscriptionId subscribeHistory(HistoryCallback callback,
                                         std::size_t offset = 0,
                                         std::size_t limit = 20);
  void cancelHistorySubscription(HistorySubscriptionId subscription_id);

  yani::Result<bool> addFavorite(std::int64_t anime_id);
  yani::Result<bool> removeFavorite(std::int64_t anime_id);
  bool isFavorite(std::int64_t anime_id) const;
  int getStatus(std::int64_t anime_id) const;
  HistorySnapshot getFavoritesSnapshot(std::size_t offset = 0,
                                       std::size_t limit = 20) const;
  std::uint64_t getFavoritesRevision() const;
  HistorySubscriptionId subscribeFavorites(HistoryCallback callback,
                                           std::size_t offset = 0,
                                           std::size_t limit = 20);
  void cancelFavoritesSubscription(HistorySubscriptionId subscription_id);

#define DECLARE_ANIME_STATUS(Name)                                             \
  yani::Result<bool> add##Name(std::int64_t anime_id);                         \
  yani::Result<bool> remove##Name(std::int64_t anime_id);                      \
  bool is##Name(std::int64_t anime_id) const;                                  \
  HistorySnapshot get##Name##Snapshot(std::size_t offset = 0,                  \
                                      std::size_t limit = 20) const;           \
  std::uint64_t get##Name##Revision() const;                                   \
  HistorySubscriptionId subscribe##Name(HistoryCallback callback,              \
                                        std::size_t offset = 0,                \
                                        std::size_t limit = 20);               \
  void cancel##Name##Subscription(HistorySubscriptionId subscription_id);
  DECLARE_ANIME_STATUS(Planned)
  DECLARE_ANIME_STATUS(Watched)
  DECLARE_ANIME_STATUS(Completed)
  DECLARE_ANIME_STATUS(Dropped)
#undef DECLARE_ANIME_STATUS

  static nlohmann::json animeToJson(const yani::AnimeResult &anime);
  static nlohmann::json
  historyToJson(const std::vector<Core::HistoryItem> &history);
  static nlohmann::json animeSaveDataToJson(const Core::AnimeSaveData &data);

  yani::Result<std::string> getVideo(std::string_view iframe,
                                     std::string_view quality = "720");
  yani::Result<std::vector<yani::VideoQuality>>
  getVideoOptions(std::string_view iframe);
  yani::Result<nlohmann::json> getAnilibriaVideos(std::int64_t anime_id);

  private:
  Core &core_;
};
