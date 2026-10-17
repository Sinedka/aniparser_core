#include "SearchHeap.hpp"

#include <memory>
#include <atomic>
#include <chrono>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include "SegmentAllocator.hpp"
#include "cache.hpp"

namespace shm {
namespace {
constexpr auto claimed = std::numeric_limits<std::uint64_t>::max();
std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool same(HeapHandle a, HeapHandle b) {
  return a.segment_id == b.segment_id && a.offset == b.offset;
}
}

SearchHeap::SearchHeap() = default;

bool SearchHeap::create(std::string_view name, std::size_t initial_segment_size,
                        IShmSegmentProvider &provider) {
  if (!manager_.create(name, initial_segment_size, provider)) return false;
  const auto *segment = manager_.get_segment(1);
  if (segment && detail::initialize_segment(*segment)) return true;
  manager_.close();
  provider.remove_segment(name, 1);
  return false;
}

bool SearchHeap::open(std::string_view name, IShmSegmentProvider &provider) {
  if (!manager_.open(name, provider)) return false;
  for (SegmentId id = 1; id <= manager_.segment_count(); ++id) {
    const auto *segment = manager_.get_segment(id);
    if (!segment || !detail::valid_segment(*segment)) {
      manager_.close();
      return false;
    }
  }
  return true;
}

void SearchHeap::close() { manager_.close(); }

HeapHandle SearchHeap::allocate(std::size_t size) {
  if (size != sizeof(SharedSearch) || !manager_.valid()) return {};
  manager_.refresh();
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    const auto count = manager_.segment_count();
    for (SegmentId id = 1; id <= count; ++id) {
      const auto *segment = manager_.get_segment(id);
      if (!segment) continue;
      const auto handle = detail::try_allocate(*segment, sizeof(SharedSearch));
      if (handle) {
        auto *search = static_cast<SharedSearch *>(manager_.resolve(handle));
        if (!search) {
          detail::deallocate(*segment, handle);
          return {};
        }
        std::construct_at(search);
        search->self = handle;
        search->root = handle;
        search->last_used_ns = now_ns();
        return handle;
      }
    }

    constexpr std::size_t overhead = sizeof(detail::SegmentHeader) +
                                    sizeof(detail::BlockHeader) + 16;
    const auto minimum = sizeof(SharedSearch) + overhead;
    if (!manager_.add_segment(minimum)) {
      manager_.refresh();
      continue;
    }
    const auto id = static_cast<SegmentId>(manager_.segment_count());
    const auto *segment = manager_.get_segment(id);
    if (!segment || !detail::initialize_segment(*segment)) return {};
  }
  return {};
}

bool SearchHeap::deallocate(HeapHandle handle) {
  auto guard = manager_.lock_registry();
  auto *search = static_cast<SharedSearch *>(resolve(handle));
  if (!search) return false;
  const auto root = search->root ? search->root : handle;
  auto *root_page = static_cast<SharedSearch *>(resolve(root));
  if (!root_page) return false;
  std::uint64_t expected = std::atomic_ref<std::uint64_t>(root_page->last_used_ns)
                               .load(std::memory_order_acquire);
  if (expected == claimed || !std::atomic_ref<std::uint64_t>(root_page->last_used_ns)
                                  .compare_exchange_strong(expected, claimed)) return false;
  guard.unlock();
  return destroy_claimed(root);
}

bool SearchHeap::destroy_claimed(HeapHandle root) {
  auto current = root;
  std::set<std::pair<SegmentId, Offset>> visited;
  bool released = false;
  while (current && visited.emplace(current.segment_id, current.offset).second) {
    auto *page = static_cast<SharedSearch *>(resolve(current));
    if (!page) break;
    const auto next = page->continuation;
    for (std::uint64_t i = 0; i < page->anime_count && i < SharedSearch::anime_capacity; ++i)
      if (page->anime[i]) Cache::release_anime(page->anime[i]);
    std::destroy_at(page);
    const auto *segment = manager_.get_segment(current.segment_id);
    if (segment && detail::deallocate(*segment, current)) released = true;
    current = next;
  }
  return released;
}

void *SearchHeap::resolve(HeapHandle handle) const {
  return allocation_size(handle) >= sizeof(SharedSearch)
             ? manager_.resolve(handle)
             : nullptr;
}

std::size_t SearchHeap::allocation_size(HeapHandle handle) const {
  const auto *segment = manager_.get_segment(handle.segment_id);
  if (!segment) return 0;
  const auto bytes = detail::allocation_size(*segment, handle);
  return bytes >= sizeof(SharedSearch) ? sizeof(SharedSearch) : 0;
}

std::size_t SearchHeap::segment_count() const { return manager_.segment_count(); }
bool SearchHeap::valid() const { return manager_.valid(); }

HeapHandle SearchHeap::allocate_search() { return allocate(sizeof(SharedSearch)); }

SharedSearch *SearchHeap::resolve_search(HeapHandle handle) const {
  auto guard = manager_.lock_registry();
  auto *page = static_cast<SharedSearch *>(resolve(handle));
  if (!page) return nullptr;
  auto *root = static_cast<SharedSearch *>(resolve(page->root ? page->root : handle));
  if (!root || std::atomic_ref<std::uint64_t>(root->last_used_ns).load(std::memory_order_acquire) == claimed)
    return nullptr;
  const auto now = now_ns();
  std::atomic_ref<std::uint64_t>(page->last_used_ns).store(now, std::memory_order_relaxed);
  std::atomic_ref<std::uint64_t>(root->last_used_ns).store(now, std::memory_order_relaxed);
  return page;
}

bool SearchHeap::append_anime(HeapHandle search_handle, HeapHandle anime) {
  auto guard = manager_.lock_registry();
  auto *search = static_cast<SharedSearch *>(resolve(search_handle));
  auto *root = search ? static_cast<SharedSearch *>(resolve(search->root ? search->root : search_handle)) : nullptr;
  if (!root || std::atomic_ref<std::uint64_t>(root->last_used_ns).load() == claimed) return false;
  if (!search || search->anime_count >= SharedSearch::anime_capacity ||
      !Cache::retain_anime(anime))
    return false;
  search->anime[search->anime_count++] = anime;
  const auto now = now_ns();
  search->last_used_ns = now;
  root->last_used_ns = now;
  return true;
}

bool SearchHeap::set_continuation(HeapHandle search_handle,
                                  HeapHandle continuation) {
  auto guard = manager_.lock_registry();
  auto *search = static_cast<SharedSearch *>(resolve(search_handle));
  auto *root = search ? static_cast<SharedSearch *>(resolve(search->root ? search->root : search_handle)) : nullptr;
  if (!root || std::atomic_ref<std::uint64_t>(root->last_used_ns).load() == claimed) return false;
  if (!search || (continuation &&
                  (search_handle.segment_id == continuation.segment_id &&
                   search_handle.offset == continuation.offset)))
    return false;
  if (continuation && !resolve(continuation)) return false;
  auto cursor = continuation;
  std::set<std::pair<SegmentId, Offset>> visited;
  while (cursor) {
    if (cursor.segment_id == search_handle.segment_id &&
        cursor.offset == search_handle.offset)
      return false;
    if (!visited.emplace(cursor.segment_id, cursor.offset).second) return false;
    const auto *node = static_cast<const SharedSearch *>(resolve(cursor));
    if (!node) return false;
    cursor = node->continuation;
  }
  search->continuation = continuation;
  if (continuation) {
    auto *child = static_cast<SharedSearch *>(resolve(continuation));
    child->root = search->root ? search->root : search_handle;
  }
  const auto now = now_ns();
  search->last_used_ns = now;
  root->last_used_ns = now;
  return true;
}

std::vector<HeapHandle> SearchHeap::allocated_handles() const { return manager_.allocated_handles(); }
HeapRegistryGuard SearchHeap::lock_registry() const { return manager_.lock_registry(); }

std::size_t SearchHeap::evict_unused_before(std::uint64_t cutoff_ns, std::size_t max_pages) {
  if (!max_pages) return 0;
  std::vector<std::pair<HeapHandle, std::size_t>> roots;
  {
    auto guard = manager_.lock_registry();
    std::size_t selected_pages = 0;
    for (const auto handle : manager_.allocated_handles()) {
      auto *page = static_cast<SharedSearch *>(resolve(handle));
      if (!page || !same(page->self, handle) || !same(page->root, handle)) continue;
      std::vector<HeapHandle> chain;
      auto cursor = handle;
      std::uint64_t newest = 0;
      std::set<std::pair<SegmentId, Offset>> visited;
      bool valid = true;
      while (cursor && visited.emplace(cursor.segment_id, cursor.offset).second) {
        auto *node = static_cast<SharedSearch *>(resolve(cursor));
        if (!node || !same(node->root, handle)) { valid = false; break; }
        newest = std::max(newest, node->last_used_ns);
        chain.push_back(cursor);
        cursor = node->continuation;
      }
      if (!valid || cursor || chain.empty() || newest > cutoff_ns ||
          selected_pages + chain.size() > max_pages) continue;
      std::uint64_t expected = newest;
      if (std::atomic_ref<std::uint64_t>(page->last_used_ns).compare_exchange_strong(expected, claimed)) {
        roots.emplace_back(handle, chain.size());
        selected_pages += chain.size();
      }
    }
  }
  std::size_t removed = 0;
  for (const auto &[root, count] : roots) {
    if (destroy_claimed(root)) removed += count;
  }
  return removed;
}

} // namespace shm
