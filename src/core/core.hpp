#pragma once

#include <api.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "anime_cache.hpp"
#include "anime_collection.hpp"
#include "history.hpp"
#include "save_manager.hpp"

class TaskPool {
  public:
  TaskPool(std::size_t workers, std::size_t queue_limit);
  ~TaskPool();
  TaskPool(const TaskPool &) = delete;
  TaskPool &operator=(const TaskPool &) = delete;

  bool submit(std::function<void()> task);
  void shutdown(bool discard_pending = false);

  private:
  void worker_loop();
  const std::size_t queue_limit_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<std::function<void()>> queue_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};

// Owns process-local API snapshots. The IPC layer receives value copies and
// never observes storage addresses or cache implementation details.
class Core {
  public:
  struct HistoryEntry {
    std::int64_t anime_id = 0;
  };
  using AnimeSaveData = aniparser::AnimeSaveData;

  using AnimeCallback = AnimeCache::Callback;
  using AnimeSubscriptionId = AnimeCache::SubscriptionId;
  using SearchCallback = std::function<void(
      yani::SearchInput, std::vector<yani::SearchResult>, bool updated)>;
  using ErrorCallback = std::function<void(yani::ApiError)>;
  using FeedCallback = std::function<void(yani::FeedResponse, bool updated)>;
  using HistorySubscriptionId = std::uint64_t;
  static constexpr std::size_t kHistoryLimit = 1000;
  static constexpr std::size_t kSaveDataLimit = 10000;
  static constexpr std::size_t kHistoryPageLimit = 50;
  struct HistoryItem {
    std::int64_t anime_id = 0;
    std::optional<yani::AnimeResult> anime;
    std::optional<AnimeSaveData> save_data;
  };
  struct HistorySnapshot {
    std::vector<HistoryItem> items;
    std::uint64_t revision = 0;
    std::uint64_t sequence = 0;
    bool refreshing = false;
    std::string update_reason;
    std::optional<yani::ApiError> error;
  };
  using HistoryCallback = std::function<void(HistorySnapshot, bool updated)>;

  struct ApiBackend {
    std::function<yani::Result<yani::AnimeResult>(std::int64_t, bool)> anime =
        [](std::int64_t id, bool videos) {
          return yani::getAnime(std::to_string(id), videos);
        };
    std::function<yani::Result<std::vector<yani::SearchResult>>(
        const yani::SearchInput &)>
        search = yani::searchTitles;
    std::function<yani::Result<yani::FeedResponse>()> feed = yani::getFeed;
  };
  Core();
  explicit Core(ApiBackend backend);
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  AnimeSubscriptionId subscribeAnime(std::int64_t anime_id,
                                     AnimeCallback callback,
                                     bool need_videos = true,
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
  yani::Result<bool> updateAnimeSaveData(AnimeSaveData data);
  yani::Result<bool> removeAnimeSaveData(std::int64_t anime_id);
  std::optional<AnimeSaveData> getAnimeSaveData(std::int64_t anime_id) const;
  std::vector<HistoryEntry> getHistory() const;
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
  HistorySnapshot getFavoritesSnapshot(std::size_t offset = 0,
                                       std::size_t limit = 20) const;
  std::uint64_t getFavoritesRevision() const;
  HistorySubscriptionId subscribeFavorites(HistoryCallback callback,
                                           std::size_t offset = 0,
                                           std::size_t limit = 20);
  void cancelFavoritesSubscription(HistorySubscriptionId subscription_id);

  yani::Result<bool> addPlanned(std::int64_t anime_id);
  yani::Result<bool> removePlanned(std::int64_t anime_id);
  bool isPlanned(std::int64_t anime_id) const;
  HistorySnapshot getPlannedSnapshot(std::size_t offset = 0,
                                     std::size_t limit = 20) const;
  std::uint64_t getPlannedRevision() const;
  HistorySubscriptionId subscribePlanned(HistoryCallback callback,
                                         std::size_t offset = 0,
                                         std::size_t limit = 20);
  void cancelPlannedSubscription(HistorySubscriptionId subscription_id);

  yani::Result<bool> addWatched(std::int64_t anime_id);
  yani::Result<bool> removeWatched(std::int64_t anime_id);
  bool isWatched(std::int64_t anime_id) const;
  HistorySnapshot getWatchedSnapshot(std::size_t offset = 0,
                                     std::size_t limit = 20) const;
  std::uint64_t getWatchedRevision() const;
  HistorySubscriptionId subscribeWatched(HistoryCallback callback,
                                         std::size_t offset = 0,
                                         std::size_t limit = 20);
  void cancelWatchedSubscription(HistorySubscriptionId subscription_id);

  yani::Result<bool> addCompleted(std::int64_t anime_id);
  yani::Result<bool> removeCompleted(std::int64_t anime_id);
  bool isCompleted(std::int64_t anime_id) const;
  HistorySnapshot getCompletedSnapshot(std::size_t offset = 0,
                                       std::size_t limit = 20) const;
  std::uint64_t getCompletedRevision() const;
  HistorySubscriptionId subscribeCompleted(HistoryCallback callback,
                                           std::size_t offset = 0,
                                           std::size_t limit = 20);
  void cancelCompletedSubscription(HistorySubscriptionId subscription_id);

  yani::Result<bool> addDropped(std::int64_t anime_id);
  yani::Result<bool> removeDropped(std::int64_t anime_id);
  bool isDropped(std::int64_t anime_id) const;
  int getStatus(std::int64_t anime_id) const;
  HistorySnapshot getDroppedSnapshot(std::size_t offset = 0,
                                     std::size_t limit = 20) const;
  std::uint64_t getDroppedRevision() const;
  HistorySubscriptionId subscribeDropped(HistoryCallback callback,
                                         std::size_t offset = 0,
                                         std::size_t limit = 20);
  void cancelDroppedSubscription(HistorySubscriptionId subscription_id);

  private:
  using Clock = AnimeCache::Clock;
  enum class AnimeStatus : std::size_t { Planned, Watched, Completed, Dropped };
  struct HistorySubscription {
    std::size_t offset = 0;
    std::size_t limit = 20;
    std::uint64_t sequence = 0;
    std::vector<std::int64_t> anime_ids;
    HistoryCallback callback;
  };
  struct SearchEntry {
    // Search pages own ordering and membership only. Anime data lives once in
    // the shared Anime cache and is projected when a snapshot is requested.
    std::vector<std::int64_t> anime_ids;
    Clock::time_point refreshed_at{};
  };

  static std::string search_key(const yani::SearchInput &params);
  HistorySnapshot history_page_locked(std::size_t offset,
                                      std::size_t limit,
                                      std::uint64_t sequence = 0,
                                      std::string reason = {}) const;
  HistorySnapshot favorites_page_locked(std::size_t offset,
                                        std::size_t limit,
                                        std::uint64_t sequence = 0,
                                        std::string reason = {}) const;
  std::vector<std::int64_t> stale_history_ids_locked(std::size_t offset,
                                                     std::size_t limit) const;
  std::vector<std::int64_t> stale_favorites_ids_locked(std::size_t offset,
                                                       std::size_t limit) const;
  void
  notify_history_subscribers(std::string reason,
                             const std::vector<std::int64_t> &anime_ids = {},
                             std::optional<yani::ApiError> error = {});
  void refresh_history_metadata(std::vector<std::int64_t> anime_ids);
  void
  notify_favorites_subscribers(std::string reason,
                               const std::vector<std::int64_t> &anime_ids = {},
                               std::optional<yani::ApiError> error = {});
  void refresh_favorites_metadata(std::vector<std::int64_t> anime_ids);
  yani::Result<bool> add_anime_status(AnimeStatus status,
                                      std::int64_t anime_id);
  yani::Result<bool> remove_anime_status(AnimeStatus status,
                                         std::int64_t anime_id);
  bool is_anime_status(AnimeStatus status, std::int64_t anime_id) const;
  HistorySnapshot anime_status_page_locked(AnimeStatus status,
                                           std::size_t offset,
                                           std::size_t limit,
                                           std::uint64_t sequence = 0,
                                           std::string reason = {}) const;
  std::vector<std::int64_t> stale_anime_status_ids_locked(
      AnimeStatus status, std::size_t offset, std::size_t limit) const;
  HistorySubscriptionId subscribe_anime_status(AnimeStatus status,
                                               HistoryCallback callback,
                                               std::size_t offset,
                                               std::size_t limit);
  void notify_anime_status_subscribers(
      AnimeStatus status,
      std::string reason,
      const std::vector<std::int64_t> &anime_ids = {},
      std::optional<yani::ApiError> error = {});
  void refresh_anime_status_metadata(AnimeStatus status,
                                     std::vector<std::int64_t> anime_ids);
  void cancel_anime_status_subscription(AnimeStatus status,
                                        HistorySubscriptionId id);
  yani::Result<bool>
  change_state(std::function<bool(aniparser::PersistentState &)> change);
  void drain_notifications();
  aniparser::PersistentState persistent_state_locked() const;
  void cache_feed_results_locked(const yani::FeedResponse &feed);
  ApiBackend backend_;
  TaskPool tasks_{4, 128};
  mutable std::mutex mutex_;
  AnimeCache anime_cache_;
  std::unordered_map<std::string, SearchEntry> searches_;
  struct SearchObserver {
    SearchCallback value;
    ErrorCallback error;
  };
  struct FeedObserver {
    FeedCallback value;
    ErrorCallback error;
  };
  std::unordered_map<std::string, std::vector<SearchObserver>>
      search_callbacks_;
  std::unordered_set<std::string> search_updating_;
  std::optional<yani::FeedResponse> feed_;
  Clock::time_point feed_refreshed_at_{};
  bool feed_updating_ = false;
  std::vector<FeedObserver> feed_callbacks_;
  SaveManager save_manager_;
  std::mutex collection_save_mutex_;
  std::deque<std::function<void()>> notifications_;
  bool dispatching_notifications_ = false;
  HistoryCollection history_;
  FavoritesCollection favorites_;
  std::array<AnimeStatusCollection, 4> anime_states_;
  std::unordered_map<std::int64_t, AnimeSaveData> anime_save_data_;
  HistorySubscriptionId next_history_subscription_id_ = 0;
  std::uint64_t history_revision_ = 0;
  std::unordered_map<HistorySubscriptionId, HistorySubscription>
      history_callbacks_;
  HistorySubscriptionId next_favorites_subscription_id_ = 0;
  std::uint64_t favorites_revision_ = 0;
  std::unordered_map<HistorySubscriptionId, HistorySubscription>
      favorites_callbacks_;
  std::array<HistorySubscriptionId, 4> next_anime_status_subscription_id_{};
  std::array<std::uint64_t, 4> anime_status_revisions_{};
  std::array<std::unordered_map<HistorySubscriptionId, HistorySubscription>, 4>
      anime_status_callbacks_;
};
