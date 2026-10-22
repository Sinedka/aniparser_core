#include "core.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

#include "json.hpp"

namespace {
constexpr auto kRefreshInterval = std::chrono::minutes(5);

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

bool has_value(const nlohmann::json &value) {
  if (value.is_null()) return false;
  if (value.is_string()) return !value.get_ref<const std::string &>().empty();
  if (value.is_array() || value.is_object()) return !value.empty();
  if (value.is_number()) return value.get<double>() != 0.0;
  return true;
}

void merge_json(nlohmann::json &target, const nlohmann::json &update) {
  if (target.is_object() && update.is_object()) {
    for (auto it = update.begin(); it != update.end(); ++it) {
      if (!has_value(it.value())) continue;
      if (!target.contains(it.key())) target[it.key()] = it.value();
      else merge_json(target[it.key()], it.value());
    }
  } else if (has_value(update)) {
    target = update;
  }
}

template <typename Model> void merge_model(Model &target, const Model &update) {
  nlohmann::json merged = target;
  merge_json(merged, nlohmann::json(update));
  target = merged.get<Model>();
}

void copy_search_fields(yani::AnimeResult &anime,
                        const yani::SearchResult &search_result) {
  yani::AnimeResult partial;
  partial.anime_id = search_result.anime_id;
  partial.anime_status = search_result.anime_status;
  partial.anime_url = search_result.anime_url;
  partial.description = search_result.description;
  partial.min_age = search_result.min_age;
  partial.poster = search_result.poster;
  partial.rating = search_result.rating;
  partial.remote_ids = search_result.remote_ids;
  partial.season = search_result.season;
  partial.title = search_result.title;
  partial.top = search_result.top;
  partial.type = search_result.type;
  partial.views = search_result.views;
  partial.year = search_result.year;
  merge_model(anime, partial);
}

void copy_feed_fields(yani::AnimeResult &anime,
                      const yani::FeedAnimeItem &feed_item) {
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
  merge_model(anime, partial);
}
} // namespace

TaskPool::TaskPool(std::size_t workers, std::size_t queue_limit)
  : queue_limit_(std::max<std::size_t>(queue_limit, 1)) {
  workers = std::max<std::size_t>(workers, 1);
  workers_.reserve(workers);
  try {
    for (std::size_t i = 0; i < workers; ++i)
      workers_.emplace_back([this] { worker_loop(); });
    std::cerr << "[api queue] started pool=" << this
              << " workers=" << workers_.size() << " capacity=" << queue_limit_
              << '\n';
  } catch (...) {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_all();
    for (auto &worker : workers_)
      if (worker.joinable()) worker.join();
    throw;
  }
}

TaskPool::~TaskPool() { shutdown(); }

bool TaskPool::submit(std::function<void()> task) {
  if (!task) return false;
  {
    std::unique_lock lock(mutex_);
    std::cerr << "[api queue] submit pool=" << this << " stopping=" << stopping_
              << " queued=" << queue_.size() << '\n';
    if (!stopping_ && queue_.size() >= queue_limit_)
      std::cerr << "[api queue] saturated (" << queue_.size() << '/'
                << queue_limit_ << "); waiting for a free slot\n";
    space_available_.wait(
        lock, [this] { return stopping_ || queue_.size() < queue_limit_; });
    if (stopping_) {
      std::cerr << "[api queue] rejected task: pool is stopping, queued="
                << queue_.size() << '\n';
      return false;
    }
    queue_.push_back(std::move(task));
    std::cerr << "[api queue] accepted task pool=" << this
              << " queued=" << queue_.size() << '\n';
  }
  condition_.notify_one();
  return true;
}

void TaskPool::shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  std::cerr << "[api queue] shutdown requested pool=" << this << '\n';
  condition_.notify_all();
  space_available_.notify_all();
  for (auto &worker : workers_)
    if (worker.joinable()) worker.join();
  workers_.clear();
}

void TaskPool::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    space_available_.notify_one();
    try {
      task();
    } catch (...) {
      // A failed task must not terminate a worker or prevent later requests.
    }
  }
}

Core::Core() { std::cerr << "[core] created instance=" << this << '\n'; }
Core::~Core() {
  std::cerr << "[core] destroying instance=" << this << '\n';
  tasks_.shutdown();
}

void Core::getAnime(std::int64_t anime_id,
                    AnimeCallback callback,
                    bool need_videos) {
  if (anime_id <= 0) {
    if (callback)
      callback(anime_id, std::nullopt, false,
               yani::ApiError{.status = 0, .message = "Invalid anime ID"});
    return;
  }

  std::optional<yani::AnimeResult> snapshot;
  bool start_update = false;
  {
    std::lock_guard lock(mutex_);
    if (!anime_.contains(anime_id) && anime_.size() >= 1024) {
      auto victim = anime_.end();
      for (auto it = anime_.begin(); it != anime_.end(); ++it) {
        if (it->second.updating || !it->second.callbacks.empty()) continue;
        if (victim == anime_.end() ||
            it->second.refreshed_at < victim->second.refreshed_at)
          victim = it;
      }
      if (victim != anime_.end()) anime_.erase(victim);
    }
    auto &entry = anime_[anime_id];
    if (!entry.value.title.empty() || entry.value.anime_id != 0)
      snapshot = entry.value;
    const bool expired = entry.refreshed_at == Clock::time_point{} ||
                         Clock::now() - entry.refreshed_at > kRefreshInterval;
    if (callback && (expired || entry.updating))
      entry.callbacks.push_back(callback);
    if (expired && !entry.updating) {
      entry.updating = true;
      start_update = true;
    }
  }
  if (callback) callback(anime_id, snapshot, false, std::nullopt);
  if (!start_update) return;

  const bool accepted = tasks_.submit([this, anime_id, need_videos] {
    auto response = safe_api_call<yani::AnimeResult>(
        [&] { return yani::getAnime(std::to_string(anime_id), need_videos); });
    if (!response)
      std::cerr << "[api error] getAnime id=" << anime_id
                << " status=" << response.error().status
                << " message=" << response.error().message << '\n'
                << "[api error] reproduce with: curl --show-error "
                   "--connect-timeout 15 --max-time 30 "
                   "-H 'Lang: ru' --get "
                   "--data-urlencode 'need_videos=true' "
                   "'https://api.yani.tv/anime/"
                << anime_id << "'\n";
    std::vector<AnimeCallback> callbacks;
    std::optional<yani::AnimeResult> updated;
    std::optional<yani::ApiError> error;
    {
      std::lock_guard lock(mutex_);
      auto it = anime_.find(anime_id);
      if (it == anime_.end()) return;
      it->second.updating = false;
      if (response) {
        it->second.value = std::move(*response);
        it->second.refreshed_at = Clock::now();
        it->second.has_full_details = true;
        updated = it->second.value;
      } else error = response.error();
      callbacks = std::move(it->second.callbacks);
      it->second.callbacks.clear();
    }
    for (auto &notify : callbacks) {
      try {
        notify(anime_id, updated, true, error);
      } catch (...) {}
    }
  });
  if (!accepted) {
    std::cerr << "[core] getAnime scheduling failed core=" << this
              << " pool=" << &tasks_ << " anime_id=" << anime_id << '\n';
    std::vector<AnimeCallback> callbacks;
    const yani::ApiError error{.status = 0,
                               .message = "API task pool rejected getAnime " +
                                          std::to_string(anime_id)};
    {
      std::lock_guard lock(mutex_);
      auto it = anime_.find(anime_id);
      if (it != anime_.end()) {
        it->second.updating = false;
        callbacks = std::move(it->second.callbacks);
        it->second.callbacks.clear();
      }
    }
    for (auto &notify : callbacks) {
      try {
        notify(anime_id, std::nullopt, true, error);
      } catch (...) {}
    }
  }
}

bool Core::animeNeedsRefresh(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  const auto it = anime_.find(anime_id);
  return it == anime_.end() || it->second.refreshed_at == Clock::time_point{} ||
         Clock::now() - it->second.refreshed_at > kRefreshInterval;
}

bool Core::animeIsLoaded(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  const auto it = anime_.find(anime_id);
  return it != anime_.end() && it->second.value.anime_id != 0;
}

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

Core::AnimeEntry* Core::find_or_create_anime_locked(std::int64_t anime_id) {
  auto it = anime_.find(anime_id);
  if (it == anime_.end() && anime_.size() >= 1024) {
    auto victim = anime_.end();
    for (auto candidate = anime_.begin(); candidate != anime_.end();
         ++candidate) {
      if (candidate->second.updating || !candidate->second.callbacks.empty())
        continue;
      if (victim == anime_.end() ||
          candidate->second.refreshed_at < victim->second.refreshed_at)
        victim = candidate;
    }
    if (victim == anime_.end()) return nullptr;
    anime_.erase(victim);
  }
  return &anime_.try_emplace(anime_id).first->second;
}

void Core::cache_search_results_locked(
    const std::vector<yani::SearchResult> &results) {
  for (const auto &result : results) {
    if (result.anime_id <= 0) continue;
    auto* entry = find_or_create_anime_locked(result.anime_id);
    if (entry && !entry->has_full_details)
      copy_search_fields(entry->value, result);
  }
}

void Core::cache_feed_results_locked(const yani::FeedResponse &feed) {
  const auto cache_partial = [this](std::int64_t anime_id,
                                    const yani::AnimeResult &partial) {
    if (anime_id <= 0) return;
    auto* entry = find_or_create_anime_locked(anime_id);
    if (entry && !entry->has_full_details) merge_model(entry->value, partial);
  };
  const auto cache_items = [&cache_partial](const auto &items) {
    for (const auto &item : items) {
      yani::AnimeResult partial;
      copy_feed_fields(partial, item);
      cache_partial(item.anime_id, partial);
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

bool Core::getSearch(yani::SearchInput params, SearchCallback callback) {
  if (!callback) return false;
  const auto key = search_key(params);
  std::vector<yani::SearchResult> snapshot;
  bool start_update = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = searches_.find(key);
    if (it != searches_.end()) snapshot = it->second.value;
    const bool expired =
        it == searches_.end() ||
        Clock::now() - it->second.refreshed_at > kRefreshInterval;
    if (expired) {
      search_callbacks_[key].push_back(callback);
      if (!search_updating_.contains(key)) {
        search_updating_.insert(key);
        start_update = true;
      }
    }
  }
  callback(params, snapshot, false);
  if (!start_update) return true;
  if (tasks_.submit([this, params = std::move(params), key]() mutable {
        auto response = safe_api_call<std::vector<yani::SearchResult>>(
            [&] { return yani::searchTitles(params); });
        if (!response)
          std::cerr << "[api error] getSearch: status="
                    << response.error().status
                    << " message=" << response.error().message << '\n';
        std::vector<SearchCallback> callbacks;
        std::vector<yani::SearchResult> value;
        {
          std::lock_guard lock(mutex_);
          search_updating_.erase(key);
          if (response) {
            value = std::move(*response);
            cache_search_results_locked(value);
            for (auto it = searches_.begin(); it != searches_.end();) {
              if (it->first != key &&
                  Clock::now() - it->second.refreshed_at > kRefreshInterval * 2)
                it = searches_.erase(it);
              else ++it;
            }
            if (!searches_.contains(key) && searches_.size() >= 256) {
              auto oldest = searches_.begin();
              for (auto it = searches_.begin(); it != searches_.end(); ++it)
                if (it->second.refreshed_at < oldest->second.refreshed_at)
                  oldest = it;
              if (oldest != searches_.end()) searches_.erase(oldest);
            }
            searches_[key] = SearchEntry{value, Clock::now()};
          }
          if (const auto it = search_callbacks_.find(key);
              it != search_callbacks_.end()) {
            callbacks = std::move(it->second);
            search_callbacks_.erase(it);
          }
        }
        if (!response) return;
        for (auto &notify : callbacks) {
          try {
            notify(params, value, true);
          } catch (...) {}
        }
      }))
    return true;
  std::lock_guard lock(mutex_);
  search_updating_.erase(key);
  search_callbacks_.erase(key);
  return false;
}

bool Core::searchPageNeedsRefresh(const yani::SearchInput &params) const {
  std::lock_guard lock(mutex_);
  const auto it = searches_.find(search_key(params));
  return it == searches_.end() ||
         Clock::now() - it->second.refreshed_at > kRefreshInterval;
}

bool Core::getFeed(FeedCallback callback) {
  if (!callback) return false;
  std::optional<yani::FeedResponse> snapshot;
  bool start_update = false;
  {
    std::lock_guard lock(mutex_);
    snapshot = feed_;
    const bool expired =
        !feed_ || Clock::now() - feed_refreshed_at_ > kRefreshInterval;
    if (feed_updating_) feed_callbacks_.push_back(callback);
    else if (expired) {
      feed_updating_ = true;
      feed_callbacks_.push_back(callback);
      start_update = true;
    }
  }
  callback(snapshot.value_or(yani::FeedResponse{}), false);
  if (!start_update) return true;
  if (tasks_.submit([this] {
        auto response =
            safe_api_call<yani::FeedResponse>([] { return yani::getFeed(); });
        if (!response)
          std::cerr << "[api error] getFeed: status=" << response.error().status
                    << " message=" << response.error().message << '\n';
        std::vector<FeedCallback> callbacks;
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
            if (updated) notify(*updated, true);
          } catch (...) {}
        }
      }))
    return true;
  std::lock_guard lock(mutex_);
  feed_updating_ = false;
  feed_callbacks_.clear();
  return false;
}

bool Core::feedNeedsRefresh() const {
  std::lock_guard lock(mutex_);
  return !feed_ || Clock::now() - feed_refreshed_at_ > kRefreshInterval;
}
