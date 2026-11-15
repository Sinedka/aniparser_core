#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

#include "callback_queue.hpp"
#include "core.hpp"
#include "json.hpp"

namespace {
constexpr auto kRefreshInterval = AnimeCache::kRefreshInterval;
template <typename T, typename Call>
yani::Result<T> safe_api_call(Call &&call) {
  try {
    return call();
  } catch (const std::exception &exception) {
    return std::unexpected(
        yani::ApiError{.status = 0, .message = exception.what(), .data = {}});
  } catch (...) {
    return std::unexpected(yani::ApiError{
        .status = 0, .message = "Unknown API exception", .data = {}});
  }
}
template <typename T>
void append_key(nlohmann::json &key, const std::optional<T> &value) {
  key.push_back(value ? nlohmann::json(*value) : nlohmann::json(nullptr));
}
yani::AnimeResult copy_feed_fields(const yani::FeedAnimeItem &feed_item) {
  yani::AnimeResult partial;
  partial.anime_id = feed_item.anime_id;
  partial.anime_status = feed_item.anime_status;
  partial.anime_url = feed_item.anime_url;
  partial.poster = feed_item.poster;
  partial.rating.average = feed_item.rating.average;
  partial.rating.counters = feed_item.rating.counters;
  partial.title = feed_item.title;
  partial.type = feed_item.type;
  partial.year = feed_item.year;
  partial.description = feed_item.description;
  partial.views = feed_item.views;
  partial.season = feed_item.season;
  partial.min_age.value = feed_item.min_age.value;
  partial.min_age.title = feed_item.min_age.title;
  partial.min_age.titleLong = feed_item.min_age.title_long;
  partial.remote_ids.worldart_id = feed_item.remote_ids.worldart_id;
  partial.remote_ids.worldart_type = feed_item.remote_ids.worldart_type;
  partial.remote_ids.kp_id = feed_item.remote_ids.kp_id;
  partial.remote_ids.sr_id = feed_item.remote_ids.sr_id;
  partial.remote_ids.anilibria_alias = feed_item.remote_ids.anilibria_alias;
  partial.remote_ids.shikimori_id = feed_item.remote_ids.shikimori_id;
  partial.remote_ids.myanimelist_id = feed_item.remote_ids.myanimelist_id;
  partial.top = feed_item.top;
  partial.blocked_in = feed_item.blocked_in;
  return partial;
}
} // namespace

std::string Core::search_key(const yani::SearchInput &p) {
  nlohmann::json key = nlohmann::json::array();
  append_key(key, p.studio_ids);
  append_key(key, p.q);
  append_key(key, p.director_ids);
  append_key(key, p.worldart_ids);
  append_key(key, p.mal_ids);
  append_key(key, p.kp_ids);
  append_key(key, p.shikimori_ids);
  append_key(key, p.min_age);
  append_key(key, p.max_rating_counters);
  append_key(key, p.min_rating_counters);
  append_key(key, p.ids);
  append_key(key, p.season);
  append_key(key, p.status);
  append_key(key, p.types);
  append_key(key, p.exclude_genres);
  append_key(key, p.genres);
  append_key(key, p.max_rating);
  append_key(key, p.min_rating);
  append_key(key, p.ep_to);
  append_key(key, p.ep_from);
  append_key(key, p.to_year);
  append_key(key, p.sort_forward);
  append_key(key, p.sort);
  append_key(key, p.offset);
  append_key(key, p.limit);
  return key.dump();
}

void Core::cache_feed_results_locked(const yani::FeedResponse &feed) {
  const auto cache_partial = [this](std::int64_t anime_id,
                                    const yani::AnimeResult &partial) {
    anime_cache_.mergePartial(anime_id, partial);
  };
  const auto cache_items = [&cache_partial](const auto &items) {
    for (const auto &item : items) {
      cache_partial(item.anime_id, copy_feed_fields(item));
    }
  };
  cache_items(feed.new_);
  cache_items(feed.announcements);
  cache_items(feed.recommends);
  cache_items(feed.top_carousel.items);
  for (const auto &collection : feed.collections)
    cache_items(collection.animes);

  for (const auto &item : feed.new_videos) {
    yani::AnimeResult partial;
    partial.anime_id = item.anime_id;
    partial.anime_url = item.anime_url;
    partial.title = item.title;
    partial.description = item.description;
    partial.poster = item.poster;
    cache_partial(item.anime_id, partial);
  }
  for (const auto &item : feed.last_watches) {
    yani::AnimeResult partial;
    partial.anime_id = item.anime_id;
    partial.anime_url = item.anime_url;
    partial.title = item.title;
    partial.description = item.description;
    partial.poster = item.poster;
    cache_partial(item.anime_id, partial);
  }
  for (const auto &item : feed.schedule) {
    yani::AnimeResult partial;
    partial.anime_id = item.anime_id;
    partial.anime_url = item.anime_url;
    partial.title = item.title;
    partial.description = item.description;
    partial.poster = item.poster;
    cache_partial(item.anime_id, partial);
  }
}

bool Core::subscribeSearch(yani::SearchInput params,
                           SearchCallback callback,
                           ErrorCallback on_error,
                           bool* refreshing) {
  if (refreshing) *refreshing = false;
  if (!callback) return false;
  const auto initial_callback = callback;
  const auto delivery = std::make_shared<CallbackQueue>();
  callback = [delivery, callback](yani::SearchInput page,
                                  std::vector<yani::SearchResult> rows,
                                  bool updated) {
    delivery->post([callback, page = std::move(page), rows = std::move(rows),
                    updated]() mutable {
      callback(std::move(page), std::move(rows), updated);
    });
  };
  if (on_error)
    on_error = [delivery, on_error](yani::ApiError error) {
      delivery->post([on_error, error = std::move(error)]() mutable {
        on_error(std::move(error));
      });
    };

  const auto key = search_key(params);
  std::vector<yani::SearchResult> snapshot;
  bool start_update = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = searches_.find(key);
    bool complete_snapshot = true;
    if (it != searches_.end()) {
      snapshot = anime_cache_.searchResults(it->second.anime_ids);
      complete_snapshot = snapshot.size() == it->second.anime_ids.size();
    }
    const bool expired =
        it == searches_.end() || !complete_snapshot ||
        Clock::now() - it->second.refreshed_at > kRefreshInterval;
    if (refreshing) *refreshing = expired || search_updating_.contains(key);
    if (expired || search_updating_.contains(key)) {
      search_callbacks_[key].push_back({callback, on_error});
      if (!search_updating_.contains(key)) {
        search_updating_.insert(key);
        start_update = true;
      }
    }
  }
  delivery->initial(
      [initial_callback, params, snapshot = std::move(snapshot)]() mutable {
        initial_callback(params, std::move(snapshot), false);
      });
  if (!start_update) return true;
  if (tasks_.submit([this, params = std::move(params), key]() mutable {
        auto response = safe_api_call<std::vector<yani::SearchResult>>(
            [&] { return backend_.search(params); });
        if (!response)
          std::cerr << "[api error] getSearch: status="
                    << response.error().status
                    << " message=" << response.error().message << '\n';
        std::vector<SearchObserver> callbacks;
        std::vector<yani::SearchResult> value;
        {
          std::lock_guard lock(mutex_);
          search_updating_.erase(key);
          if (response) {
            value = std::move(*response);
            for (auto it = searches_.begin(); it != searches_.end();) {
              if (it->first != key && Clock::now() - it->second.refreshed_at >
                                          AnimeCache::kRefreshInterval * 2) {
                for (const auto anime_id : it->second.anime_ids)
                  anime_cache_.removeFromSearch(it->first, anime_id);
                it = searches_.erase(it);
              } else ++it;
            }
            if (const auto current = searches_.find(key);
                current != searches_.end()) {
              for (const auto anime_id : current->second.anime_ids)
                anime_cache_.removeFromSearch(key, anime_id);
              searches_.erase(current);
            }
            if (searches_.size() >= 256) {
              auto oldest = searches_.begin();
              for (auto it = searches_.begin(); it != searches_.end(); ++it)
                if (it->second.refreshed_at < oldest->second.refreshed_at)
                  oldest = it;
              if (oldest != searches_.end()) {
                for (const auto anime_id : oldest->second.anime_ids)
                  anime_cache_.removeFromSearch(oldest->first, anime_id);
                searches_.erase(oldest);
              }
            }
            std::vector<std::int64_t> anime_ids;
            anime_ids.reserve(value.size());
            for (const auto &result : value)
              if (result.anime_id > 0) anime_ids.push_back(result.anime_id);
            anime_cache_.storeSearchResults(key, value);
            searches_[key] = SearchEntry{std::move(anime_ids), Clock::now()};
            value = anime_cache_.searchResults(searches_.at(key).anime_ids);
          }
          if (const auto it = search_callbacks_.find(key);
              it != search_callbacks_.end()) {
            callbacks = std::move(it->second);
            search_callbacks_.erase(it);
          }
        }
        for (auto &notify : callbacks) {
          try {
            if (response) notify.value(params, value, true);
            else if (notify.error) notify.error(response.error());
          } catch (...) {}
        }
      }))
    return true;
  std::vector<SearchObserver> rejected;
  {
    std::lock_guard lock(mutex_);
    search_updating_.erase(key);
    rejected = std::move(search_callbacks_[key]);
    search_callbacks_.erase(key);
  }
  for (auto &observer : rejected) {
    try {
      if (observer.error)
        observer.error(yani::ApiError{.message = "API queue full or stopping"});
    } catch (...) {}
  }
  return false;
}

bool Core::searchPageNeedsRefresh(const yani::SearchInput &params) const {
  std::lock_guard lock(mutex_);
  const auto it = searches_.find(search_key(params));
  return it == searches_.end() ||
         Clock::now() - it->second.refreshed_at > kRefreshInterval ||
         anime_cache_.searchResults(it->second.anime_ids).size() !=
             it->second.anime_ids.size();
}

bool Core::subscribeFeed(FeedCallback callback,
                         ErrorCallback on_error,
                         bool* refreshing) {
  if (refreshing) *refreshing = false;
  if (!callback) return false;
  const auto initial_callback = callback;
  const auto delivery = std::make_shared<CallbackQueue>();
  callback = [delivery, callback](yani::FeedResponse feed, bool updated) {
    delivery->post([callback, feed = std::move(feed), updated]() mutable {
      callback(std::move(feed), updated);
    });
  };
  if (on_error)
    on_error = [delivery, on_error](yani::ApiError error) {
      delivery->post([on_error, error = std::move(error)]() mutable {
        on_error(std::move(error));
      });
    };

  std::optional<yani::FeedResponse> snapshot;
  bool start_update = false;
  {
    std::lock_guard lock(mutex_);
    snapshot = feed_;
    const bool expired =
        !feed_ || Clock::now() - feed_refreshed_at_ > kRefreshInterval;
    if (refreshing) *refreshing = expired || feed_updating_;
    if (feed_updating_) feed_callbacks_.push_back({callback, on_error});
    else if (expired) {
      feed_updating_ = true;
      feed_callbacks_.push_back({callback, on_error});
      start_update = true;
    }
  }
  delivery->initial(
      [initial_callback, snapshot = std::move(snapshot)]() mutable {
        initial_callback(snapshot.value_or(yani::FeedResponse{}), false);
      });
  if (!start_update) return true;
  if (tasks_.submit([this] {
        auto response = safe_api_call<yani::FeedResponse>(
            [this] { return backend_.feed(); });
        if (!response)
          std::cerr << "[api error] getFeed: status=" << response.error().status
                    << " message=" << response.error().message << '\n';
        std::vector<FeedObserver> callbacks;
        std::optional<yani::FeedResponse> updated;
        {
          std::lock_guard lock(mutex_);
          feed_updating_ = false;
          if (response) {
            feed_ = std::move(*response);
            cache_feed_results_locked(*feed_);
            feed_refreshed_at_ = Clock::now();
            updated = feed_;
          }
          callbacks = std::move(feed_callbacks_);
          feed_callbacks_.clear();
        }
        for (auto &notify : callbacks) {
          try {
            if (updated) notify.value(*updated, true);
            else if (notify.error) notify.error(response.error());
          } catch (...) {}
        }
      }))
    return true;
  std::vector<FeedObserver> rejected;
  {
    std::lock_guard lock(mutex_);
    feed_updating_ = false;
    rejected.swap(feed_callbacks_);
  }
  for (auto &observer : rejected) {
    try {
      if (observer.error)
        observer.error(yani::ApiError{.message = "API queue full or stopping"});
    } catch (...) {}
  }
  return false;
}

bool Core::feedNeedsRefresh() const {
  std::lock_guard lock(mutex_);
  return !feed_ || Clock::now() - feed_refreshed_at_ > kRefreshInterval;
}
