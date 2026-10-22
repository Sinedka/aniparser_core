#pragma once

#include <api.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class TaskPool {
  public:
  TaskPool(std::size_t workers, std::size_t queue_limit);
  ~TaskPool();
  TaskPool(const TaskPool &) = delete;
  TaskPool &operator=(const TaskPool &) = delete;

  bool submit(std::function<void()> task);
  void shutdown();

  private:
  void worker_loop();
  const std::size_t queue_limit_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::condition_variable space_available_;
  std::deque<std::function<void()>> queue_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};

// Owns process-local API snapshots. The IPC layer receives value copies and
// never observes storage addresses or cache implementation details.
class Core {
  public:
  using AnimeCallback = std::function<void(std::int64_t,
                                           std::optional<yani::AnimeResult>,
                                           bool updated,
                                           std::optional<yani::ApiError>)>;
  using SearchCallback = std::function<void(
      yani::SearchInput, std::vector<yani::SearchResult>, bool updated)>;
  using FeedCallback = std::function<void(yani::FeedResponse, bool updated)>;

  Core();
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  void getAnime(std::int64_t anime_id,
                AnimeCallback callback,
                bool need_videos = true);
  bool animeNeedsRefresh(std::int64_t anime_id) const;
  bool animeIsLoaded(std::int64_t anime_id) const;

  bool getSearch(yani::SearchInput params, SearchCallback callback);
  bool searchPageNeedsRefresh(const yani::SearchInput &params) const;

  bool getFeed(FeedCallback callback);
  bool feedNeedsRefresh() const;

  private:
  using Clock = std::chrono::steady_clock;
  struct AnimeEntry {
    yani::AnimeResult value;
    Clock::time_point refreshed_at{};
    bool has_full_details = false;
    bool updating = false;
    std::vector<AnimeCallback> callbacks;
  };
  struct SearchEntry {
    std::vector<yani::SearchResult> value;
    Clock::time_point refreshed_at{};
  };

  static std::string search_key(const yani::SearchInput &params);
  AnimeEntry* find_or_create_anime_locked(std::int64_t anime_id);
  void
  cache_search_results_locked(const std::vector<yani::SearchResult> &results);
  void cache_feed_results_locked(const yani::FeedResponse &feed);
  TaskPool tasks_{4, 128};
  mutable std::mutex mutex_;
  std::unordered_map<std::int64_t, AnimeEntry> anime_;
  std::unordered_map<std::string, SearchEntry> searches_;
  std::unordered_map<std::string, std::vector<SearchCallback>>
      search_callbacks_;
  std::unordered_set<std::string> search_updating_;
  std::optional<yani::FeedResponse> feed_;
  Clock::time_point feed_refreshed_at_{};
  bool feed_updating_ = false;
  std::vector<FeedCallback> feed_callbacks_;
};
