#include "AnimeHeap.hpp"

#include <limits>

#include "SegmentAllocator.hpp"

namespace shm {
namespace {

std::size_t minimum_segment_size(std::size_t payload_size) {
  constexpr auto overhead = sizeof(detail::SegmentHeader) +
                            sizeof(detail::BlockHeader) + 16;
  if (payload_size > std::numeric_limits<std::size_t>::max() - overhead)
    return 0;
  return payload_size + overhead;
}

} // namespace

AnimeHeap::AnimeHeap(std::size_t anime_size) : anime_size_(anime_size) {}

bool AnimeHeap::create(std::string_view name,
                       std::size_t initial_segment_size,
                       IShmSegmentProvider &provider) {
  if (anime_size_ == 0 || !manager_.create(name, initial_segment_size, provider))
    return false;
  const auto *segment = manager_.get_segment(1);
  if (segment && detail::initialize_segment(*segment)) return true;
  manager_.close();
  provider.remove_segment(name, 1);
  return false;
}

bool AnimeHeap::open(std::string_view name, IShmSegmentProvider &provider) {
  if (anime_size_ == 0 || !manager_.open(name, provider)) return false;
  for (SegmentId id = 1; id <= manager_.segment_count(); ++id) {
    const auto *segment = manager_.get_segment(id);
    if (!segment || !detail::valid_segment(*segment)) {
      manager_.close();
      return false;
    }
  }
  return true;
}

void AnimeHeap::close() { manager_.close(); }

HeapHandle AnimeHeap::allocate(std::size_t size) {
  if (size != anime_size_ || !manager_.valid()) return {};
  manager_.refresh();

  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    const auto count = manager_.segment_count();
    for (SegmentId id = 1; id <= count; ++id) {
      const auto *segment = manager_.get_segment(id);
      if (!segment) continue;
      const auto handle = detail::try_allocate(*segment, anime_size_);
      if (handle) return handle;
    }

    const auto minimum = minimum_segment_size(anime_size_);
    if (minimum == 0 || !manager_.add_segment(minimum)) {
      manager_.refresh();
      continue;
    }
    const auto id = static_cast<SegmentId>(manager_.segment_count());
    const auto *segment = manager_.get_segment(id);
    if (!segment || !detail::initialize_segment(*segment)) return {};
    const auto handle = detail::try_allocate(*segment, anime_size_);
    if (handle) return handle;
  }
  return {};
}

bool AnimeHeap::deallocate(HeapHandle handle) {
  const auto *segment = manager_.get_segment(handle.segment_id);
  return segment && detail::deallocate(*segment, handle);
}

void *AnimeHeap::resolve(HeapHandle handle) const {
  if (allocation_size(handle) < anime_size_ || anime_size_ == 0) return nullptr;
  return manager_.resolve(handle);
}

std::size_t AnimeHeap::allocation_size(HeapHandle handle) const {
  const auto *segment = manager_.get_segment(handle.segment_id);
  if (!segment) return 0;
  const auto allocated = detail::allocation_size(*segment, handle);
  return allocated >= anime_size_ ? anime_size_ : 0;
}

std::size_t AnimeHeap::segment_count() const {
  return manager_.segment_count();
}

HeapStatistics AnimeHeap::statistics() const {
  return manager_.statistics();
}

std::vector<HeapHandle> AnimeHeap::allocated_handles() const {
  return manager_.allocated_handles();
}

HeapRegistryGuard AnimeHeap::lock_registry() const {
  return manager_.lock_registry();
}

bool AnimeHeap::valid() const {
  return anime_size_ != 0 && manager_.valid();
}

std::size_t AnimeHeap::anime_size() const noexcept { return anime_size_; }

} // namespace shm
