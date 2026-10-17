#pragma once

#include <api.hpp>
#include <cache.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

class Core {
  public:
  using AnimeUpdateCallback = std::function<void(std::int64_t, Anime*)>;
  using SearchPageCallback =
      std::function<bool(const yani::SearchInput &, SharedSearch*)>;
  using FeedCallback =
      std::function<void(const std::vector<std::string> &, SharedFeed*)>;

  explicit Core(shm::IShmSegmentProvider &provider,
                std::string cache_name,
                std::size_t anime_segment_size = 8 * 1024 * 1024,
                std::size_t variable_segment_size = 32 * 1024 * 1024);
  ~Core();

  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  // Open an existing cache or create a new one.
  bool initialize();

  // Fetch from the API, convert the full result, and store it in shared memory.
  yani::Result<shm::HeapHandle> getAnime(std::string_view alias_or_id,
                                         bool need_videos = false);

  // Return the cached slot immediately, then refresh it from the API. The
  // callback receives nullptr if the refresh fails.
  Anime* getAnime(std::int64_t anime_id, AnimeUpdateCallback callback);

  // Fetch pages asynchronously. Each request uses limit=10 and increments
  // offset by 10. Return true from the callback to request the next page;
  // false stops pagination. The caller owns delivered pages and can release
  // the chain from its root handle with releaseSearch().
  bool searchTitles(yani::SearchInput params, SearchPageCallback callback);
  bool searchPageNeedsRefresh(yani::SearchInput params);
  SharedSearch* resolveSearch(shm::SearchHandle handle);
  bool releaseSearch(shm::SearchHandle handle);

  bool getFeed(std::vector<std::string> watches, FeedCallback callback);
  bool feedNeedsRefresh(const std::vector<std::string> &watches);
  SharedFeed* resolveFeed(shm::HeapHandle handle);
  bool releaseFeed(shm::HeapHandle handle);

  Anime* resolveAnime(shm::HeapHandle handle);
  bool releaseAnime(shm::HeapHandle handle);
  bool releaseAnime(Anime* anime);

  private:
  shm::IShmSegmentProvider &provider_;
  std::string cache_name_;
  std::size_t anime_segment_size_;
  std::size_t variable_segment_size_;
  bool initialized_ = false;

  struct AnimeEntry {
    shm::HeapHandle handle{};
    bool updating = false;
    bool loaded_full = false;
    std::chrono::steady_clock::time_point last_api_request{};
    std::vector<AnimeUpdateCallback> callbacks;
  };

  struct CachedSearchPage {
    std::chrono::steady_clock::time_point last_requested_at;
    std::vector<yani::SearchResult> results;
  };

  std::mutex entries_mutex_;
  bool shutting_down_ = false;
  std::unordered_map<std::int64_t, AnimeEntry> anime_by_id_;
  std::mutex search_cache_mutex_;
  std::unordered_map<std::string, CachedSearchPage> search_cache_;
  struct CachedFeed {
    std::chrono::steady_clock::time_point last_api_request;
    yani::FeedResponse response;
  };
  std::mutex feed_cache_mutex_;
  std::unordered_map<std::string, CachedFeed> feed_cache_;
  std::mutex tasks_mutex_;
  std::vector<std::future<void>> update_tasks_;
  std::mutex maintenance_mutex_;
  std::condition_variable_any maintenance_cv_;
  std::jthread maintenance_thread_;
  void maintenance_loop(std::stop_token token);
  void cleanup_idle_cache();
  bool animeNeedsRefresh(std::int64_t anime_id);
  bool animeIsLoaded(std::int64_t anime_id);

  friend class Server;
};
