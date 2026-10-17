#include "cache.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <string>

shm::AnimeHeap Cache::anime_heap_{sizeof(Anime)};
shm::VariableHeap Cache::variable_heap_{};
shm::SearchHeap Cache::search_heap_{};
shm::FeedHeap Cache::feed_heap_{};

void Anime::release_shared_storage(shm::VariableHeap &heap) {
  anime_status.title.clear(heap);
  anime_status.class_.clear(heap);
  anime_status.alias.clear(heap);
  anime_url.clear(heap);
  poster.big.clear(heap);
  poster.fullsize.clear(heap);
  poster.huge.clear(heap);
  poster.medium.clear(heap);
  poster.small.clear(heap);
  title.clear(heap);
  type.name.clear(heap);
  type.shortname.clear(heap);
  description.clear(heap);
  min_age.title.clear(heap);
  min_age.titleLong.clear(heap);
  remote_ids.worldart_type.clear(heap);
  remote_ids.anilibria_alias.clear(heap);
  original.clear(heap);

  if (auto *items = other_titles.data(heap))
    for (std::uint64_t i = 0; i < other_titles.size; ++i) items[i].clear(heap);
  other_titles.release(heap);
  if (auto *items = creators.data(heap))
    for (std::uint64_t i = 0; i < creators.size; ++i) items[i].release(heap);
  creators.release(heap);
  if (auto *items = studios.data(heap))
    for (std::uint64_t i = 0; i < studios.size; ++i) items[i].release(heap);
  studios.release(heap);
  if (auto *items = videos.data(heap))
    for (std::uint64_t i = 0; i < videos.size; ++i) items[i].release(heap);
  videos.release(heap);
  if (auto *items = genres.data(heap))
    for (std::uint64_t i = 0; i < genres.size; ++i) items[i].release(heap);
  genres.release(heap);
  if (auto *items = viewing_order.data(heap))
    for (std::uint64_t i = 0; i < viewing_order.size; ++i)
      if (items[i]) Cache::release_anime(items[i]);
  viewing_order.release(heap);
  if (auto *items = translates.data(heap))
    for (std::uint64_t i = 0; i < translates.size; ++i) items[i].release(heap);
  translates.release(heap);
  if (auto *items = blocked_in.data(heap))
    for (std::uint64_t i = 0; i < blocked_in.size; ++i) items[i].clear(heap);
  blocked_in.release(heap);
  if (auto *items = random_screenshots.data(heap))
    for (std::uint64_t i = 0; i < random_screenshots.size; ++i)
      items[i].release(heap);
  random_screenshots.release(heap);
}

namespace {
// Version 8 stores fixed-size SharedFeed records in FeedHeap and moves their
// vectors and strings to VariableHeap.
constexpr std::string_view cache_layout = ".v8";
constexpr auto evicting = std::numeric_limits<std::uint64_t>::max();

std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void touch(Anime &anime) {
  std::atomic_ref<std::uint64_t>(anime.last_used_ns).store(now_ns(), std::memory_order_relaxed);
}

bool claim_idle(shm::HeapHandle handle) {
  auto *anime = Cache::resolve_anime(handle);
  if (!anime) return false;
  std::uint64_t expected = 0;
  return std::atomic_ref<std::uint64_t>(anime->usage_count).compare_exchange_strong(
      expected, evicting, std::memory_order_acq_rel);
}

bool destroy_claimed(shm::HeapHandle handle) {
  auto *anime = Cache::resolve_anime(handle);
  if (!anime || std::atomic_ref<std::uint64_t>(anime->usage_count).load(std::memory_order_acquire) != evicting)
    return false;
  anime->release_shared_storage(Cache::variable_heap());
  std::destroy_at(anime);
  return Cache::anime_heap().deallocate(handle);
}
} // namespace

bool Cache::create(std::string_view name, std::size_t anime_segment_size,
                   std::size_t variable_segment_size,
                   shm::IShmSegmentProvider &provider) {
  close();
  if (name.empty()) return false;
  const std::string base_name{name};
  const auto anime_name = base_name + std::string(cache_layout) + ".anime";
  const auto variable_name = base_name + std::string(cache_layout) + ".variable";
  const auto search_name = base_name + std::string(cache_layout) + ".search";
  const auto feed_name = base_name + std::string(cache_layout) + ".feed";
  if (!anime_heap_.create(anime_name, anime_segment_size, provider)) return false;
  if (!variable_heap_.create(variable_name, variable_segment_size, provider)) {
    anime_heap_.close(); provider.remove_segment(anime_name, 1); return false;
  }
  if (!search_heap_.create(search_name, anime_segment_size, provider)) {
    variable_heap_.close(); anime_heap_.close();
    provider.remove_segment(variable_name, 1);
    provider.remove_segment(anime_name, 1);
    return false;
  }
  if (!feed_heap_.create(feed_name, anime_segment_size, provider)) {
    search_heap_.close(); variable_heap_.close(); anime_heap_.close();
    provider.remove_segment(search_name, 1);
    provider.remove_segment(variable_name, 1);
    provider.remove_segment(anime_name, 1);
    return false;
  }
  return true;
}

bool Cache::open(std::string_view name, shm::IShmSegmentProvider &provider) {
  close();
  if (name.empty()) return false;
  const std::string base_name{name};
  if (!anime_heap_.open(base_name + std::string(cache_layout) + ".anime", provider)) return false;
  if (!variable_heap_.open(base_name + std::string(cache_layout) + ".variable", provider)) { anime_heap_.close(); return false; }
  if (!search_heap_.open(base_name + std::string(cache_layout) + ".search", provider)) {
    variable_heap_.close(); anime_heap_.close(); return false;
  }
  if (!feed_heap_.open(base_name + std::string(cache_layout) + ".feed", provider)) {
    search_heap_.close(); variable_heap_.close(); anime_heap_.close(); return false;
  }
  return true;
}

void Cache::close() { feed_heap_.close(); search_heap_.close(); variable_heap_.close(); anime_heap_.close(); }
shm::AnimeHeap &Cache::anime_heap() { return anime_heap_; }
shm::VariableHeap &Cache::variable_heap() { return variable_heap_; }
shm::SearchHeap &Cache::search_heap() { return search_heap_; }
shm::FeedHeap &Cache::feed_heap() { return feed_heap_; }

shm::HeapHandle Cache::allocate_search() { return search_heap_.allocate_search(); }

bool Cache::deallocate_search(shm::HeapHandle handle) {
  return search_heap_.deallocate(handle);
}

shm::HeapHandle Cache::allocate_feed() { return feed_heap_.allocate(); }

bool Cache::deallocate_feed(shm::HeapHandle handle) {
  return feed_heap_.deallocate(handle);
}

shm::HeapHandle Cache::allocate_anime() {
  const auto handle = anime_heap_.allocate(sizeof(Anime));
  if (!handle) return {};
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (!anime) { anime_heap_.deallocate(handle); return {}; }
  std::construct_at(anime);
  anime->self = handle;
  // The returned handle is owned by the caller. Keeping this initial
  // reference is required for feeds, searches, and detail requests to safely
  // transfer ownership through retain/release operations.
  anime->usage_count = 1;
  touch(*anime);
  return handle;
}

Anime *Cache::resolve_anime(shm::HeapHandle handle) {
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (anime && std::atomic_ref<std::uint64_t>(anime->usage_count).load(std::memory_order_acquire) != evicting)
    touch(*anime);
  return anime;
}

bool Cache::retain_anime(shm::HeapHandle handle) {
  auto guard = anime_heap_.lock_registry();
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (!anime) return false;
  auto count = std::atomic_ref<std::uint64_t>(anime->usage_count);
  auto current = count.load(std::memory_order_acquire);
  while (current != evicting && current != std::numeric_limits<std::uint64_t>::max() - 1) {
    if (count.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel)) {
      touch(*anime); return true;
    }
  }
  return false;
}

bool Cache::release_anime(shm::HeapHandle handle) {
  auto guard = anime_heap_.lock_registry();
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (!anime) return false;
  auto count = std::atomic_ref<std::uint64_t>(anime->usage_count);
  auto current = count.load(std::memory_order_acquire);
  while (current != 0 && current != evicting) {
    if (count.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
      touch(*anime); return true;
    }
  }
  return false;
}

bool Cache::discard_anime(shm::HeapHandle handle) {
  auto guard = anime_heap_.lock_registry();
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (!anime) return false;
  std::uint64_t expected = 1;
  if (!std::atomic_ref<std::uint64_t>(anime->usage_count).compare_exchange_strong(
          expected, evicting, std::memory_order_acq_rel)) return false;
  // Release the global lock before releasing relationship references.
  guard.unlock();
  return destroy_claimed(handle);
}

bool Cache::deallocate_anime(shm::HeapHandle handle) {
  auto guard = anime_heap_.lock_registry();
  if (!claim_idle(handle)) return false;
  guard.unlock();
  return destroy_claimed(handle);
}

shm::HeapHandle Cache::find_or_create_anime(std::int64_t anime_id, bool *created) {
  if (created) *created = false;
  if (anime_id <= 0 || !anime_heap_.valid()) return {};
  auto guard = anime_heap_.lock_registry();
  for (const auto handle : anime_heap_.allocated_handles()) {
    auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
    if (!anime || anime->anime_id != anime_id ||
        std::atomic_ref<std::uint64_t>(anime->usage_count).load(std::memory_order_acquire) == evicting)
      continue;
    auto count = std::atomic_ref<std::uint64_t>(anime->usage_count);
    auto current = count.load(std::memory_order_acquire);
    while (current != evicting && current < evicting - 1) {
      if (count.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel)) {
        touch(*anime); return handle;
      }
    }
  }
  const auto handle = allocate_anime();
  if (!handle) return {};
  auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
  if (!anime) { anime_heap_.deallocate(handle); return {}; }
  anime->anime_id = anime_id;
  if (created) *created = true;
  return handle;
}

std::size_t Cache::evict_oldest_unused(std::size_t max_count) {
  return evict_unused_before(std::numeric_limits<std::uint64_t>::max() - 1,
                             max_count);
}

std::size_t Cache::evict_unused_before(std::uint64_t cutoff_ns,
                                       std::size_t max_count) {
  if (!max_count) return 0;
  std::size_t removed = 0;
  while (removed < max_count) {
    auto guard = anime_heap_.lock_registry();
    shm::HeapHandle oldest{};
    std::uint64_t oldest_time = std::numeric_limits<std::uint64_t>::max();
    for (const auto handle : anime_heap_.allocated_handles()) {
      auto *anime = static_cast<Anime *>(anime_heap_.resolve(handle));
      if (!anime || std::atomic_ref<std::uint64_t>(anime->usage_count).load(std::memory_order_acquire) != 0) continue;
      const auto used = std::atomic_ref<std::uint64_t>(anime->last_used_ns).load(std::memory_order_relaxed);
      if (used > cutoff_ns) continue;
      if (!oldest || used < oldest_time) { oldest = handle; oldest_time = used; }
    }
    if (!oldest || !claim_idle(oldest)) break;
    guard.unlock();
    if (!destroy_claimed(oldest)) break;
    ++removed;
  }
  return removed;
}
