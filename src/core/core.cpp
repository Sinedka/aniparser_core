#include "core.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

using Heap = shm::VariableHeap;

bool store_string(shm::SharedString &target,
                  const std::string &source,
                  Heap &heap) {
  return target.set(heap, source);
}

bool store_status(Anime::AnimeStatus &target,
                  const yani::AnimeStatus &source,
                  Heap &heap) {
  target.value = source.value;
  return store_string(target.title, source.title, heap) &&
         store_string(target.class_, source.class_, heap) &&
         store_string(target.alias, source.alias, heap);
}

bool store_poster(Anime::Poster &target,
                  const yani::Poster &source,
                  Heap &heap) {
  return store_string(target.big, source.big, heap) &&
         store_string(target.fullsize, source.fullsize, heap) &&
         store_string(target.huge, source.huge, heap) &&
         store_string(target.medium, source.medium, heap) &&
         store_string(target.small, source.small, heap);
}

bool store_rating(Anime::Rating &target, const yani::Rating &source) {
  target.average = source.average;
  target.kp_rating = source.kp_rating;
  target.anidub_rating = source.anidub_rating;
  target.counters = source.counters;
  target.myanimelist_rating = source.myanimelist_rating;
  target.shikimori_rating = source.shikimori_rating;
  target.worldart_rating = source.worldart_rating;
  return true;
}

bool store_type(Anime::AnimeType &target,
                const yani::AnimeType &source,
                Heap &heap) {
  target.value = source.value;
  return store_string(target.name, source.name, heap) &&
         store_string(target.shortname, source.shortname, heap);
}

bool store_min_age(Anime::MinAge &target,
                   const yani::MinAge &source,
                   Heap &heap) {
  target.value = source.value;
  return store_string(target.title, source.title, heap) &&
         store_string(target.titleLong, source.titleLong, heap);
}

bool store_remote_ids(Anime::RemoteIds &target,
                      const yani::RemoteIds &source,
                      Heap &heap) {
  target.worldart_id = source.worldart_id;
  target.kp_id = source.kp_id;
  target.anidb_id = source.anidb_id;
  target.sr_id = source.sr_id;
  target.shikimori_id = source.shikimori_id;
  target.myanimelist_id = source.myanimelist_id;
  return store_string(target.worldart_type, source.worldart_type, heap) &&
         store_string(target.anilibria_alias, source.anilibria_alias, heap);
}

template <typename Destination,
          typename Source,
          typename Convert,
          typename Release>
bool store_vector(shm::SharedVector<Destination> &target,
                  const std::vector<Source> &source,
                  Heap &heap,
                  Convert convert,
                  Release release) {
  if (!target.reserve(heap, source.size())) return false;
  for (const auto &source_item : source) {
    Destination item{};
    if (!convert(item, source_item, heap)) {
      release(item, heap);
      return false;
    }
    if (!target.push_back(heap, item)) {
      release(item, heap);
      return false;
    }
  }
  return true;
}

bool store_creator(Anime::Creator &target,
                   const yani::Creator &source,
                   Heap &heap) {
  target.id = source.id;
  return store_string(target.title, source.title, heap) &&
         store_string(target.url, source.url, heap);
}

bool store_studio(Anime::Studio &target,
                  const yani::Studio &source,
                  Heap &heap) {
  target.id = source.id;
  return store_string(target.title, source.title, heap) &&
         store_string(target.url, source.url, heap);
}

bool store_video(Anime::VideoInfo &target,
                 const yani::VideoInfo &source,
                 Heap &heap) {
  target.video_id = source.video_id;
  target.date = source.date;
  target.index = source.index;
  return store_string(target.iframe_url, source.iframe_url, heap) &&
         store_string(target.data.dubbing, source.data.dubbing, heap) &&
         store_string(target.data.player, source.data.player, heap) &&
         store_string(target.number, source.number, heap) &&
         store_string(target.skips.opening.time, source.skips.opening.time,
                      heap) &&
         store_string(target.skips.opening.length, source.skips.opening.length,
                      heap) &&
         store_string(target.skips.ending.time, source.skips.ending.time,
                      heap) &&
         store_string(target.skips.ending.length, source.skips.ending.length,
                      heap);
}

bool store_genre(Anime::Genre &target, const yani::Genre &source, Heap &heap) {
  target.id = source.id;
  return store_string(target.title, source.title, heap) &&
         store_string(target.alias, source.alias, heap) &&
         store_string(target.url, source.url, heap);
}

bool store_translate(Anime::Translates &target,
                     const yani::Translates &source,
                     Heap &heap) {
  return store_string(target.title, source.title, heap) &&
         store_string(target.href, source.href, heap) &&
         store_string(target.value, source.value, heap);
}

bool store_viewing_order(shm::HeapHandle &target,
                         const yani::ViewingOrder &source,
                         Heap &heap) {
  target = Cache::allocate_anime();
  if (!target) return false;

  auto* related = Cache::resolve_anime(target);
  if (!related) {
    Cache::discard_anime(target);
    target = {};
    return false;
  }

  related->anime_id = source.anime_id;
  related->year = source.year;
  related->date = source.date;
  const bool stored =
      store_status(related->anime_status, source.anime_status, heap) &&
      store_string(related->title, source.title, heap) &&
      store_type(related->type, source.type, heap) &&
      store_string(related->anime_url, source.anime_url, heap) &&
      store_string(related->description, source.description, heap) &&
      store_poster(related->poster, source.poster, heap);
  if (stored) return true;

  Cache::discard_anime(target);
  target = {};
  return false;
}

bool store_screenshot(Anime::RandomScreenshot &target,
                      const yani::RandomScreenshot &source,
                      Heap &heap) {
  target.id = source.id;
  target.time = source.time;
  target.episode = source.episode;
  return store_string(target.sizes.small, source.sizes.small, heap) &&
         store_string(target.sizes.full, source.sizes.full, heap);
}

bool store_strings(shm::SharedVector<shm::SharedString> &target,
                   const std::vector<std::string> &source,
                   Heap &heap) {
  if (!target.reserve(heap, source.size())) return false;
  for (const auto &text : source) {
    shm::SharedString item{};
    if (!item.set(heap, text)) return false;
    if (!target.push_back(heap, item)) {
      item.clear(heap);
      return false;
    }
  }
  return true;
}

bool store_anime(Anime &target, const yani::AnimeResult &source, Heap &heap) {
  target.anime_id = source.anime_id;
  target.year = source.year;
  target.views = source.views;
  target.season = source.season;
  target.episodes.aired = source.episodes.aired;
  target.episodes.count = source.episodes.count;
  target.episodes.next_date = source.episodes.next_date;
  target.comments_count = source.comments_count;
  target.reviews_count = source.reviews_count;
  target.top.category = source.top.category;
  target.top.global = source.top.global;
  target.posts_count = source.posts_count;

  if (!store_status(target.anime_status, source.anime_status, heap) ||
      !store_string(target.anime_url, source.anime_url, heap) ||
      !store_poster(target.poster, source.poster, heap) ||
      !store_rating(target.rating, source.rating) ||
      !store_string(target.title, source.title, heap) ||
      !store_type(target.type, source.type, heap) ||
      !store_string(target.description, source.description, heap) ||
      !store_min_age(target.min_age, source.min_age, heap) ||
      !store_remote_ids(target.remote_ids, source.remote_ids, heap) ||
      !store_string(target.original, source.original, heap) ||
      !store_strings(target.other_titles, source.other_titles, heap))
    return false;

  if (!store_vector(target.creators, source.creators, heap, store_creator,
                    [](auto &item, Heap &h) { item.release(h); }) ||
      !store_vector(target.studios, source.studios, heap, store_studio,
                    [](auto &item, Heap &h) { item.release(h); }) ||
      !store_vector(target.videos, source.videos, heap, store_video,
                    [](auto &item, Heap &h) { item.release(h); }) ||
      !store_vector(target.genres, source.genres, heap, store_genre,
                    [](auto &item, Heap &h) { item.release(h); }) ||
      !store_vector(target.viewing_order, source.viewing_order, heap,
                    store_viewing_order,
                    [](auto &item, Heap &) {
                      if (item) Cache::release_anime(item);
                    }) ||
      !store_vector(target.translates, source.translates, heap, store_translate,
                    [](auto &item, Heap &h) { item.release(h); }) ||
      !store_strings(target.blocked_in, source.blocked_in, heap) ||
      !store_vector(target.random_screenshots, source.random_screenshots, heap,
                    store_screenshot,
                    [](auto &item, Heap &h) { item.release(h); }))
    return false;
  return true;
}

bool store_search_result(Anime &target,
                         const yani::SearchResult &source,
                         Heap &heap) {
  target.anime_id = source.anime_id;
  target.season = source.season;
  target.year = source.year;
  target.views = source.views;
  target.top.category = source.top.category;
  target.top.global = source.top.global;
  return store_status(target.anime_status, source.anime_status, heap) &&
         store_string(target.anime_url, source.anime_url, heap) &&
         store_string(target.description, source.description, heap) &&
         store_min_age(target.min_age, source.min_age, heap) &&
         store_poster(target.poster, source.poster, heap) &&
         store_rating(target.rating, source.rating) &&
         store_remote_ids(target.remote_ids, source.remote_ids, heap) &&
         store_string(target.title, source.title, heap) &&
         store_type(target.type, source.type, heap);
}

bool store_feed_item(Anime &target,
                     const yani::FeedAnimeItem &source,
                     Heap &heap) {
  target.anime_id = source.anime_id;
  target.year = source.year;
  target.views = source.views;
  target.season = source.season;
  target.rating.average = source.rating.average;
  target.rating.counters = source.rating.counters;
  target.top.category = source.top.category;
  target.top.global = source.top.global;
  target.min_age.value = source.min_age.value;
  target.remote_ids.worldart_id = source.remote_ids.worldart_id;
  target.remote_ids.kp_id = source.remote_ids.kp_id;
  target.remote_ids.anidub_id = source.remote_ids.anidub_id;
  target.remote_ids.sr_id = source.remote_ids.sr_id;
  target.remote_ids.shikimori_id = source.remote_ids.shikimori_id;
  target.remote_ids.myanimelist_id = source.remote_ids.myanimelist_id;
  return store_status(target.anime_status, source.anime_status, heap) &&
         store_string(target.anime_url, source.anime_url, heap) &&
         store_poster(target.poster, source.poster, heap) &&
         store_string(target.title, source.title, heap) &&
         store_type(target.type, source.type, heap) &&
         store_string(target.description, source.description, heap) &&
         store_string(target.min_age.title, source.min_age.title, heap) &&
         store_string(target.min_age.titleLong, source.min_age.title_long,
                      heap) &&
         store_string(target.remote_ids.worldart_type,
                      source.remote_ids.worldart_type, heap) &&
         store_string(target.remote_ids.anilibria_alias,
                      source.remote_ids.anilibria_alias, heap) &&
         store_strings(target.blocked_in, source.blocked_in, heap);
}

bool store_feed_schedule_item(Anime &target,
                              const yani::FeedScheduleItem &source,
                              Heap &heap) {
  target.anime_id = source.anime_id;
  target.episodes.aired = source.episodes.aired;
  target.episodes.count = source.episodes.count;
  target.episodes.next_date = source.episodes.next_date;
  return store_string(target.title, source.title, heap) &&
         store_string(target.description, source.description, heap) &&
         store_string(target.anime_url, source.anime_url, heap) &&
         store_poster(target.poster, source.poster, heap);
}

yani::ApiError cache_error(std::string message) {
  return yani::ApiError{.status = 0, .message = std::move(message), .data = {}};
}

template <typename T>
void append_search_key(nlohmann::json &key, const std::optional<T> &value) {
  if (value) key.push_back(*value);
  else key.push_back(nullptr);
}

std::string search_page_key(const yani::SearchInput &params) {
  nlohmann::json key = nlohmann::json::array();
  append_search_key(key, params.studio_ids);
  append_search_key(key, params.q);
  append_search_key(key, params.director_ids);
  append_search_key(key, params.worldart_ids);
  append_search_key(key, params.mal_ids);
  append_search_key(key, params.kp_ids);
  append_search_key(key, params.shikimori_ids);
  append_search_key(key, params.min_age);
  append_search_key(key, params.max_rating_counters);
  append_search_key(key, params.min_rating_counters);
  append_search_key(key, params.ids);
  append_search_key(key, params.season);
  append_search_key(key, params.status);
  append_search_key(key, params.types);
  append_search_key(key, params.exclude_genres);
  append_search_key(key, params.genres);
  append_search_key(key, params.max_rating);
  append_search_key(key, params.min_rating);
  append_search_key(key, params.ep_to);
  append_search_key(key, params.ep_from);
  append_search_key(key, params.to_year);
  append_search_key(key, params.sort_forward);
  append_search_key(key, params.sort);
  append_search_key(key, params.offset);
  append_search_key(key, params.limit);
  return key.dump();
}

std::string feed_cache_key(const std::vector<std::string> &watches) {
  return nlohmann::json(watches).dump();
}

SharedFeed* store_feed_response(const yani::FeedResponse &response) {
  const auto feed_handle = Cache::allocate_feed();
  if (!feed_handle) return nullptr;
  auto* feed = Cache::feed_heap().resolve(feed_handle);
  if (!feed) {
    Cache::deallocate_feed(feed_handle);
    return nullptr;
  }
  feed->carousel_season = response.top_carousel.season;
  feed->carousel_year = response.top_carousel.year;

  const auto append_item = [](const yani::FeedAnimeItem &item,
                              shm::SharedVector<shm::HeapHandle> &items) {
    bool created = false;
    const auto handle = Cache::find_or_create_anime(item.anime_id, &created);
    if (!handle) return false;
    auto* anime = Cache::resolve_anime(handle);
    if (!anime ||
        (created && !store_feed_item(*anime, item, Cache::variable_heap()))) {
      if (created) Cache::discard_anime(handle);
      else Cache::release_anime(handle);
      return false;
    }
    if (!Cache::feed_heap().append_anime(items, handle)) {
      if (created) Cache::discard_anime(handle);
      else Cache::release_anime(handle);
      return false;
    }
    Cache::release_anime(handle);
    return true;
  };
  const auto append_schedule = [](const yani::FeedScheduleItem &item,
                                  shm::SharedVector<shm::HeapHandle> &items) {
    bool created = false;
    const auto handle = Cache::find_or_create_anime(item.anime_id, &created);
    if (!handle) return false;
    auto* anime = Cache::resolve_anime(handle);
    if (!anime || (created && !store_feed_schedule_item(
                                  *anime, item, Cache::variable_heap()))) {
      if (created) Cache::discard_anime(handle);
      else Cache::release_anime(handle);
      return false;
    }
    if (!Cache::feed_heap().append_anime(items, handle)) {
      if (created) Cache::discard_anime(handle);
      else Cache::release_anime(handle);
      return false;
    }
    Cache::release_anime(handle);
    return true;
  };

  bool stored = true;
  for (const auto &item : response.new_)
    if (!append_item(item, feed->new_anime)) {
      stored = false;
      break;
    }
  for (const auto &item : response.announcements)
    if (stored && !append_item(item, feed->announcements)) stored = false;
  for (const auto &item : response.recommends)
    if (stored && !append_item(item, feed->recommendations)) stored = false;
  for (const auto &item : response.top_carousel.items)
    if (stored && !append_item(item, feed->carousel_items)) stored = false;
  for (const auto &item : response.schedule)
    if (stored && !append_schedule(item, feed->schedule_items)) stored = false;

  for (const auto &source_collection : response.collections) {
    if (!stored) break;
    SharedFeedCollection* collection = nullptr;
    if (!Cache::feed_heap().append_collection(
            *feed, source_collection.id, source_collection.title,
            source_collection.description, &collection) ||
        !collection) {
      stored = false;
      break;
    }
    for (const auto &item : source_collection.animes) {
      if (!append_item(item, collection->anime)) {
        stored = false;
        break;
      }
    }
  }

  if (!stored) {
    Cache::deallocate_feed(feed_handle);
    return nullptr;
  }
  return feed;
}

} // namespace

Core::Core(shm::IShmSegmentProvider &provider,
           std::string cache_name,
           std::size_t anime_segment_size,
           std::size_t variable_segment_size)
  : provider_(provider)
  , cache_name_(std::move(cache_name))
  , anime_segment_size_(anime_segment_size)
  , variable_segment_size_(variable_segment_size) {}

Core::~Core() {
  {
    std::lock_guard lock(entries_mutex_);
    shutting_down_ = true;
  }
  if (maintenance_thread_.joinable()) {
    maintenance_thread_.request_stop();
    maintenance_cv_.notify_all();
    maintenance_thread_.join();
  }
  std::vector<std::future<void>> tasks;
  {
    std::lock_guard lock(tasks_mutex_);
    tasks.swap(update_tasks_);
  }
  for (auto &task : tasks) {
    if (!task.valid()) continue;
    try {
      task.get();
    } catch (...) {}
  }
  if (initialized_) Cache::close();
}

bool Core::initialize() {
  if (initialized_) return true;
  initialized_ = Cache::open(cache_name_, provider_);
  if (!initialized_)
    initialized_ = Cache::create(cache_name_, anime_segment_size_,
                                 variable_segment_size_, provider_);
  if (initialized_ && !maintenance_thread_.joinable())
    maintenance_thread_ = std::jthread(
        [this](std::stop_token token) { maintenance_loop(token); });
  return initialized_;
}

void Core::maintenance_loop(std::stop_token token) {
  std::unique_lock lock(maintenance_mutex_);
  while (!token.stop_requested()) {
    maintenance_cv_.wait_for(lock, token, std::chrono::minutes(5),
                             [] { return false; });
    if (token.stop_requested()) break;
    lock.unlock();
    cleanup_idle_cache();
    lock.lock();
  }
}

void Core::cleanup_idle_cache() {
  constexpr auto idle = std::chrono::minutes(20);
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto now_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  const auto idle_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(idle).count();
  if (now_ns <= idle_ns) return;
  const auto cutoff = static_cast<std::uint64_t>(now_ns - idle_ns);
  (void)Cache::search_heap().evict_unused_before(
      cutoff, std::numeric_limits<std::size_t>::max());
  (void)Cache::evict_unused_before(cutoff,
                                   std::numeric_limits<std::size_t>::max());
}

yani::Result<shm::HeapHandle> Core::getAnime(std::string_view alias_or_id,
                                             bool need_videos) {
  if (!initialized_)
    return std::unexpected(cache_error("Core cache is not initialized"));

  auto response = yani::getAnime(alias_or_id, need_videos);
  if (!response) return std::unexpected(response.error());

  const auto handle = Cache::allocate_anime();
  if (!handle)
    return std::unexpected(cache_error("Failed to allocate Anime in cache"));

  auto* anime = Cache::resolve_anime(handle);
  if (!anime || !store_anime(*anime, *response, Cache::variable_heap())) {
    Cache::discard_anime(handle);
    return std::unexpected(cache_error("Failed to store Anime in cache"));
  }
  return handle;
}

Anime* Core::getAnime(std::int64_t anime_id, AnimeUpdateCallback callback) {
  if (!initialized_ || anime_id <= 0) return nullptr;

  constexpr auto refresh_ttl = std::chrono::minutes(5);
  shm::HeapHandle handle{};
  Anime* anime = nullptr;
  bool schedule_update = false;
  {
    std::lock_guard lock(entries_mutex_);
    if (shutting_down_) return nullptr;
    auto entry = anime_by_id_.find(anime_id);
    if (entry == anime_by_id_.end()) {
      bool created = false;
      handle = Cache::find_or_create_anime(anime_id, &created);
      if (!handle) return nullptr;
      anime = Cache::resolve_anime(handle);
      if (!anime) {
        Cache::release_anime(handle);
        return nullptr;
      }
      AnimeEntry new_entry{.handle = handle, .updating = true};
      if (callback) new_entry.callbacks.push_back(std::move(callback));
      anime_by_id_.emplace(anime_id, std::move(new_entry));
      schedule_update = true;
    } else {
      handle = entry->second.handle;
      if (!Cache::retain_anime(handle)) {
        anime_by_id_.erase(entry);
        bool created = false;
        handle = Cache::find_or_create_anime(anime_id, &created);
        if (!handle) return nullptr;
        anime = Cache::resolve_anime(handle);
        if (!anime) {
          Cache::release_anime(handle);
          return nullptr;
        }
        AnimeEntry new_entry{.handle = handle, .updating = true};
        if (callback) new_entry.callbacks.push_back(std::move(callback));
        anime_by_id_.emplace(anime_id, std::move(new_entry));
        schedule_update = true;
      } else {
        anime = Cache::resolve_anime(handle);
        if (!entry->second.updating) {
          const auto now = std::chrono::steady_clock::now();
          if (now - entry->second.last_api_request > refresh_ttl) {
            entry->second.updating = true;
            schedule_update = true;
          }
        }
        if (callback && (entry->second.updating || schedule_update))
          entry->second.callbacks.push_back(std::move(callback));
      }
      if (!anime) {
        Cache::release_anime(handle);
        return nullptr;
      }
    }
  }

  if (!schedule_update) return anime;

  try {
    auto task = std::async(std::launch::async, [this, anime_id, handle] {
      {
        std::lock_guard lock(entries_mutex_);
        const auto entry = anime_by_id_.find(anime_id);
        if (entry == anime_by_id_.end() ||
            entry->second.handle.segment_id != handle.segment_id ||
            entry->second.handle.offset != handle.offset)
          return;
        entry->second.last_api_request = std::chrono::steady_clock::now();
      }
      std::optional<yani::AnimeResult> api_result;
      try {
        auto result = yani::getAnimeAsync(std::to_string(anime_id)).get();
        if (result) api_result.emplace(std::move(*result));
      } catch (...) {}

      std::vector<AnimeUpdateCallback> callbacks;
      Anime* updated_anime = nullptr;
      {
        std::lock_guard lock(entries_mutex_);
        const auto entry = anime_by_id_.find(anime_id);
        if (entry == anime_by_id_.end() ||
            entry->second.handle.segment_id != handle.segment_id ||
            entry->second.handle.offset != handle.offset)
          return;

        if (api_result) {
          auto* target = Cache::resolve_anime(handle);
          if (target) {
            Anime replacement{};
            if (store_anime(replacement, *api_result, Cache::variable_heap())) {
              const auto usage =
                  std::atomic_ref<std::uint64_t>(target->usage_count)
                      .load(std::memory_order_acquire);
              const auto last_used =
                  std::atomic_ref<std::uint64_t>(target->last_used_ns)
                      .load(std::memory_order_relaxed);
              target->release_shared_storage(Cache::variable_heap());
              std::memcpy(target, &replacement, offsetof(Anime, usage_count));
              std::atomic_ref<std::uint64_t>(target->usage_count)
                  .store(usage, std::memory_order_release);
              std::atomic_ref<std::uint64_t>(target->last_used_ns)
                  .store(last_used, std::memory_order_relaxed);
              replacement = Anime{};
              updated_anime = target;
              entry->second.loaded_full = true;
            } else {
              replacement.release_shared_storage(Cache::variable_heap());
            }
          }
        }
        callbacks = std::move(entry->second.callbacks);
        entry->second.updating = false;
      }

      for (auto &notify : callbacks) {
        try {
          notify(anime_id, updated_anime);
        } catch (...) {
          // A caller callback must not prevent other subscribers from firing.
        }
      }
    });

    std::lock_guard lock(tasks_mutex_);
    for (auto task_it = update_tasks_.begin();
         task_it != update_tasks_.end();) {
      if (task_it->valid() && task_it->wait_for(std::chrono::seconds(0)) ==
                                  std::future_status::ready) {
        try {
          task_it->get();
        } catch (...) {}
        task_it = update_tasks_.erase(task_it);
      } else {
        ++task_it;
      }
    }
    update_tasks_.push_back(std::move(task));
  } catch (...) {
    std::vector<AnimeUpdateCallback> callbacks;
    {
      std::lock_guard lock(entries_mutex_);
      const auto entry = anime_by_id_.find(anime_id);
      if (entry != anime_by_id_.end() &&
          entry->second.handle.segment_id == handle.segment_id &&
          entry->second.handle.offset == handle.offset) {
        entry->second.updating = false;
        callbacks = std::move(entry->second.callbacks);
      }
    }
    for (auto &notify : callbacks) {
      try {
        notify(anime_id, nullptr);
      } catch (...) {}
    }
  }

  return anime;
}

bool Core::animeNeedsRefresh(std::int64_t anime_id) {
  constexpr auto refresh_ttl = std::chrono::minutes(5);
  std::lock_guard lock(entries_mutex_);
  const auto entry = anime_by_id_.find(anime_id);
  if (entry == anime_by_id_.end()) return true;
  if (entry->second.updating) return true;
  return std::chrono::steady_clock::now() - entry->second.last_api_request >
         refresh_ttl;
}

bool Core::animeIsLoaded(std::int64_t anime_id) {
  std::lock_guard lock(entries_mutex_);
  const auto entry = anime_by_id_.find(anime_id);
  return entry != anime_by_id_.end() && entry->second.loaded_full;
}

bool Core::searchTitles(yani::SearchInput params, SearchPageCallback callback) {
  if (!initialized_ || !callback) return false;
  const auto requested_offset = params.offset.value_or(0);
  const std::int64_t first_offset = std::max<std::int64_t>(0, requested_offset);

  try {
    auto task = std::async(std::launch::async, [this,
                                                params = std::move(params),
                                                callback = std::move(callback),
                                                first_offset]() mutable {
      constexpr std::int64_t page_size = 10;
      constexpr auto search_ttl = std::chrono::minutes(5);
      auto offset = first_offset;
      shm::SearchHandle previous{};

      while (true) {
        auto page_params = params;
        page_params.offset = offset;
        page_params.limit = page_size;

        yani::Result<std::vector<yani::SearchResult>> response =
            std::unexpected(cache_error("Search request failed"));
        std::optional<std::vector<yani::SearchResult>> stale_results;
        const auto key = search_page_key(page_params);
        const auto now = std::chrono::steady_clock::now();
        {
          std::lock_guard lock(search_cache_mutex_);
          const auto cached = search_cache_.find(key);
          if (cached != search_cache_.end()) {
            if (now - cached->second.last_requested_at <= search_ttl)
              response = cached->second.results;
            else stale_results = cached->second.results;
          }
        }
        const auto make_page = [](const std::vector<yani::SearchResult> &items)
            -> std::pair<shm::SearchHandle, SharedSearch*> {
          const auto handle = Cache::allocate_search();
          if (!handle) return {};
          auto* page = Cache::search_heap().resolve_search(handle);
          if (!page) {
            Cache::deallocate_search(handle);
            return {};
          }
          const auto count =
              std::min<std::size_t>(items.size(), SharedSearch::anime_capacity);
          for (std::size_t i = 0; i < count; ++i) {
            const auto &item = items[i];
            bool created = false;
            const auto anime_handle =
                Cache::find_or_create_anime(item.anime_id, &created);
            if (!anime_handle) {
              Cache::deallocate_search(handle);
              return {};
            }
            auto* anime = Cache::resolve_anime(anime_handle);
            if (!anime ||
                (created &&
                 !store_search_result(*anime, item, Cache::variable_heap()))) {
              if (created) Cache::discard_anime(anime_handle);
              else Cache::release_anime(anime_handle);
              Cache::deallocate_search(handle);
              return {};
            }
            if (!Cache::search_heap().append_anime(handle, anime_handle)) {
              if (created) Cache::discard_anime(anime_handle);
              else Cache::release_anime(anime_handle);
              Cache::deallocate_search(handle);
              return {};
            }
            Cache::release_anime(anime_handle);
          }
          return {handle, page};
        };

        if (stale_results) {
          const auto [cached_handle, cached_page] = make_page(*stale_results);
          if (!cached_page) {
            try {
              (void)callback(page_params, nullptr);
            } catch (...) {}
            return;
          }
          try {
            (void)callback(page_params, cached_page);
          } catch (...) {
            Cache::deallocate_search(cached_handle);
            return;
          }
        }

        // A stale page is delivered above immediately. Refresh it from the
        // API in the background and deliver the updated page when ready.
        std::chrono::steady_clock::time_point api_requested_at{};
        if (stale_results || !response) {
          api_requested_at = std::chrono::steady_clock::now();
          if (stale_results) {
            std::lock_guard lock(search_cache_mutex_);
            if (const auto cached = search_cache_.find(key);
                cached != search_cache_.end())
              cached->second.last_requested_at = api_requested_at;
          }
          try {
            response = yani::searchTitlesAsync(page_params).get();
          } catch (const std::exception &error) {
            response = std::unexpected(cache_error(error.what()));
          } catch (...) {
            response = std::unexpected(cache_error("Unknown search error"));
          }
          if (response) {
            std::lock_guard lock(search_cache_mutex_);
            for (auto it = search_cache_.begin(); it != search_cache_.end();) {
              if (api_requested_at - it->second.last_requested_at > search_ttl)
                it = search_cache_.erase(it);
              else ++it;
            }
            search_cache_[key] = CachedSearchPage{api_requested_at, *response};
          }
        }

        if (!response) {
          try {
            (void)callback(page_params, nullptr);
          } catch (...) {}
          return;
        }

        const auto [page_handle, page] = make_page(*response);
        if (!page) {
          try {
            (void)callback(page_params, nullptr);
          } catch (...) {}
          return;
        }

        if (previous &&
            !Cache::search_heap().set_continuation(previous, page_handle)) {
          Cache::deallocate_search(page_handle);
          try {
            (void)callback(page_params, nullptr);
          } catch (...) {}
          return;
        }

        bool continue_search = false;
        try {
          continue_search = callback(page_params, page);
        } catch (...) { return; }
        previous = page_handle;
        if (!continue_search ||
            response->size() < SharedSearch::anime_capacity ||
            offset > std::numeric_limits<std::int64_t>::max() - page_size)
          return;
        offset += page_size;
      }
    });

    std::lock_guard lock(tasks_mutex_);
    for (auto it = update_tasks_.begin(); it != update_tasks_.end();) {
      if (it->valid() &&
          it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          it->get();
        } catch (...) {}
        it = update_tasks_.erase(it);
      } else {
        ++it;
      }
    }
    update_tasks_.push_back(std::move(task));
  } catch (...) { return false; }
  return true;
}

bool Core::searchPageNeedsRefresh(yani::SearchInput params) {
  constexpr auto search_ttl = std::chrono::minutes(5);
  params.offset = std::max<std::int64_t>(0, params.offset.value_or(0));
  params.limit = 10;
  const auto key = search_page_key(params);
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(search_cache_mutex_);
  const auto cached = search_cache_.find(key);
  return cached != search_cache_.end() &&
         now - cached->second.last_requested_at > search_ttl;
}

SharedSearch* Core::resolveSearch(shm::SearchHandle handle) {
  return initialized_ ? Cache::search_heap().resolve_search(handle) : nullptr;
}

bool Core::releaseSearch(shm::SearchHandle handle) {
  return initialized_ && Cache::deallocate_search(handle);
}

bool Core::getFeed(std::vector<std::string> watches, FeedCallback callback) {
  if (!initialized_ || !callback) return false;
  try {
    auto task = std::async(
        std::launch::async, [this, watches = std::move(watches),
                             callback = std::move(callback)]() mutable {
          constexpr auto refresh_ttl = std::chrono::minutes(5);
          const auto key = feed_cache_key(watches);
          std::optional<yani::FeedResponse> response;
          std::optional<yani::FeedResponse> stale_response;
          const auto now = std::chrono::steady_clock::now();
          {
            std::lock_guard lock(feed_cache_mutex_);
            const auto cached = feed_cache_.find(key);
            if (cached != feed_cache_.end()) {
              if (now - cached->second.last_api_request <= refresh_ttl)
                response = cached->second.response;
              else stale_response = cached->second.response;
            }
          }

          if (stale_response) {
            auto* feed = store_feed_response(*stale_response);
            if (!feed) {
              try {
                callback(watches, nullptr);
              } catch (...) {}
              return;
            }
            try {
              callback(watches, feed);
            } catch (...) {
              Cache::deallocate_feed(feed->self);
              return;
            }
          }

          std::chrono::steady_clock::time_point api_requested_at{};
          if (stale_response || !response) {
            api_requested_at = std::chrono::steady_clock::now();
            if (stale_response) {
              std::lock_guard lock(feed_cache_mutex_);
              if (const auto cached = feed_cache_.find(key);
                  cached != feed_cache_.end())
                cached->second.last_api_request = api_requested_at;
            }
            try {
              auto result = yani::getFeedAsync(watches).get();
              if (result) response.emplace(std::move(*result));
            } catch (...) {}
            if (response) {
              std::lock_guard lock(feed_cache_mutex_);
              for (auto it = feed_cache_.begin(); it != feed_cache_.end();) {
                if (api_requested_at - it->second.last_api_request >
                    refresh_ttl)
                  it = feed_cache_.erase(it);
                else ++it;
              }
              feed_cache_[key] = CachedFeed{api_requested_at, *response};
            }
          }

          if (!response) {
            try {
              callback(watches, nullptr);
            } catch (...) {}
            return;
          }
          auto* feed = store_feed_response(*response);
          if (!feed) {
            try {
              callback(watches, nullptr);
            } catch (...) {}
            return;
          }
          try {
            callback(watches, feed);
          } catch (...) { Cache::deallocate_feed(feed->self); }
        });

    std::lock_guard lock(tasks_mutex_);
    for (auto it = update_tasks_.begin(); it != update_tasks_.end();) {
      if (it->valid() &&
          it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          it->get();
        } catch (...) {}
        it = update_tasks_.erase(it);
      } else {
        ++it;
      }
    }
    update_tasks_.push_back(std::move(task));
  } catch (...) { return false; }
  return true;
}

bool Core::feedNeedsRefresh(const std::vector<std::string> &watches) {
  constexpr auto refresh_ttl = std::chrono::minutes(5);
  const auto key = feed_cache_key(watches);
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(feed_cache_mutex_);
  const auto cached = feed_cache_.find(key);
  return cached != feed_cache_.end() &&
         now - cached->second.last_api_request > refresh_ttl;
}

SharedFeed* Core::resolveFeed(shm::HeapHandle handle) {
  return initialized_ ? Cache::feed_heap().resolve(handle) : nullptr;
}

bool Core::releaseFeed(shm::HeapHandle handle) {
  return initialized_ && Cache::deallocate_feed(handle);
}

Anime* Core::resolveAnime(shm::HeapHandle handle) {
  return initialized_ ? Cache::resolve_anime(handle) : nullptr;
}

bool Core::releaseAnime(shm::HeapHandle handle) {
  if (!initialized_) return false;
  return Cache::release_anime(handle);
}

bool Core::releaseAnime(Anime* anime) {
  if (!initialized_ || !anime) return false;
  std::lock_guard lock(entries_mutex_);
  for (const auto &entry : anime_by_id_) {
    auto* resolved = Cache::resolve_anime(entry.second.handle);
    if (resolved == anime) { return Cache::release_anime(entry.second.handle); }
  }
  return false;
}
