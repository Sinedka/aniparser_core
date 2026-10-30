#pragma once

#include <api.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Core serializes all calls to this class with its mutex. Anime data is owned
// here; callers only receive value copies and never keep Entry pointers.
class AnimeCache {
  public:
  using Clock = std::chrono::steady_clock;
  using SubscriptionId = std::uint64_t;
  using Callback = std::function<void(std::int64_t,
                                      std::optional<yani::AnimeResult>,
                                      bool,
                                      std::optional<yani::ApiError>)>;

  static constexpr std::size_t kCapacity = 1024;
  static constexpr std::size_t kCheckedIdCapacity = 1000;
  static constexpr auto kRefreshInterval = std::chrono::minutes(5);

  struct SubscribeResult {
    SubscriptionId subscription_id = 0;
    std::optional<yani::AnimeResult> anime;
    bool should_refresh = false;
  };
  struct PendingCallback {
    SubscriptionId subscription_id = 0;
    Callback callback;
  };

  SubscribeResult subscribe(std::int64_t anime_id, Callback callback);
  bool unsubscribe(SubscriptionId subscription_id);
  bool needsFullRefresh(std::int64_t anime_id) const;
  bool isLoaded(std::int64_t anime_id) const;
  std::optional<yani::AnimeResult> get(std::int64_t anime_id) const;

  std::vector<PendingCallback>
  finishRefresh(std::int64_t anime_id, std::optional<yani::AnimeResult> anime);

  void storeSearchResults(const std::string &search_key,
                          const std::vector<yani::SearchResult> &results);
  void mergePartial(std::int64_t anime_id, const yani::AnimeResult &partial);
  std::vector<yani::SearchResult>
  searchResults(const std::vector<std::int64_t> &anime_ids) const;

  void addToSearch(const std::string &search_key, std::int64_t anime_id);
  void removeFromSearch(const std::string &search_key, std::int64_t anime_id);

  bool cardNeedsRefresh(std::int64_t anime_id) const;
  void markCardChecked(std::int64_t anime_id);

  private:
  class Entry {
public:
    Entry() = default;

private:
    friend class AnimeCache;
    void subscribe(SubscriptionId id, Callback callback);
    bool unsubscribe(SubscriptionId id);
    void addToSearch(const std::string &search_key);
    void removeFromSearch(const std::string &search_key);
    bool canErase() const;

    yani::AnimeResult value;
    Clock::time_point refreshed_at{};
    Clock::time_point card_refreshed_at{};
    bool has_full_details = false;
    bool updating = false;
    std::unordered_map<SubscriptionId, Callback> subscribers;
    std::unordered_set<std::string> search_keys;
  };

  Entry* findOrCreate(std::int64_t anime_id);
  void queueIfUnused(std::int64_t anime_id, const Entry &entry);
  void removeCandidate(std::int64_t anime_id);
  void cleanCandidates();
  static yani::SearchResult toSearchResult(const yani::AnimeResult &anime);
  static void merge(yani::AnimeResult &target,
                    const yani::AnimeResult &partial);

  std::unordered_map<std::int64_t, std::unique_ptr<Entry>> entries_;
  std::unordered_map<SubscriptionId, std::int64_t> subscriber_index_;
  std::unordered_map<std::int64_t, Clock::time_point> checked_ids_;
  std::list<std::int64_t> cleanup_candidates_;
  std::unordered_map<std::int64_t, std::list<std::int64_t>::iterator>
      cleanup_candidate_index_;
  SubscriptionId next_subscription_id_ = 0;
};
