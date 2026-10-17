#include "HeapManager.hpp"

#include "SegmentAllocator.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace shm {

HeapManager::HeapManager() = default;

bool HeapManager::create(std::string_view name,
                         std::size_t initial_segment_size,
                         IShmSegmentProvider &provider) {
  close();
  if (name.empty() || initial_segment_size == 0) return false;

  name_.assign(name);
  initial_segment_size_ = initial_segment_size;
  next_segment_size_ = initial_segment_size;
  provider_ = &provider;

  HeapSegment segment{};
  const bool created =
      provider.create_segment(name_, 1, initial_segment_size_, segment);
  if (!created || segment.id != 1 || !segment.base ||
      segment.size < initial_segment_size_) {
    if (segment.base) provider.close_segment(segment);
    if (created) provider.remove_segment(name_, 1);
    provider_ = nullptr;
    name_.clear();
    return false;
  }

  {
    std::lock_guard lock(segments_mutex_);
    segments_.push_back(segment);
  }
  if (initial_segment_size_ <= std::numeric_limits<std::size_t>::max() / 2)
    next_segment_size_ = initial_segment_size_ * 2;
  return true;
}

bool HeapManager::open(std::string_view name, IShmSegmentProvider &provider) {
  close();
  if (name.empty()) return false;
  name_.assign(name);
  provider_ = &provider;

  while (open_next_segment()) {}
  if (segment_count() == 0) {
    provider_ = nullptr;
    name_.clear();
    return false;
  }

  initial_segment_size_ = segments_.front().size;
  const auto last_size = segments_.back().size;
  next_segment_size_ = last_size <= std::numeric_limits<std::size_t>::max() / 2
                           ? last_size * 2
                           : last_size;
  return true;
}

void HeapManager::close() {
  std::deque<HeapSegment> old_segments;
  IShmSegmentProvider *provider = provider_;
  {
    std::lock_guard lock(segments_mutex_);
    old_segments.swap(segments_);
  }
  if (provider) {
    for (auto &segment : old_segments) provider->close_segment(segment);
  }
  provider_ = nullptr;
  name_.clear();
  initial_segment_size_ = 0;
  next_segment_size_ = 0;
}

bool HeapManager::refresh() {
  if (!provider_) return false;
  while (open_next_segment()) {}
  return true;
}

bool HeapManager::open_next_segment() const {
  std::lock_guard lock(segments_mutex_);
  if (!provider_ || segments_.size() >= invalid_segment_id - 1) return false;

  const auto id = static_cast<SegmentId>(segments_.size() + 1);
  HeapSegment segment{};
  if (!provider_->open_segment(name_, id, segment)) return false;
  if (segment.id != id || !segment.base || segment.size == 0) {
    provider_->close_segment(segment);
    return false;
  }
  segments_.push_back(segment);
  return true;
}

bool HeapManager::add_segment(std::size_t minimum_size) {
  if (!provider_ || name_.empty()) return false;
  refresh();

  std::lock_guard lock(segments_mutex_);
  if (segments_.size() >= invalid_segment_id - 1) return false;
  const auto id = static_cast<SegmentId>(segments_.size() + 1);
  const auto size = std::max(next_segment_size_, minimum_size);

  HeapSegment segment{};
  if (!provider_->create_segment(name_, id, size, segment)) return false;
  if (segment.id != id || !segment.base || segment.size < size) {
    provider_->close_segment(segment);
    provider_->remove_segment(name_, id);
    return false;
  }

  segments_.push_back(segment);
  next_segment_size_ = segment.size <= std::numeric_limits<std::size_t>::max() / 2
                           ? segment.size * 2
                           : segment.size;
  return true;
}

const HeapSegment *HeapManager::get_segment(SegmentId id) const {
  if (id == invalid_segment_id) return nullptr;
  {
    std::lock_guard lock(segments_mutex_);
    for (const auto &segment : segments_) {
      if (segment.id == id) return &segment;
    }
  }

  while (segment_count() < id) {
    if (!open_next_segment()) return nullptr;
  }

  std::lock_guard lock(segments_mutex_);
  for (const auto &segment : segments_) {
    if (segment.id == id) return &segment;
  }
  return nullptr;
}

HeapSegment *HeapManager::get_segment(SegmentId id) {
  return const_cast<HeapSegment *>(std::as_const(*this).get_segment(id));
}

void *HeapManager::resolve(HeapHandle handle) const {
  const auto *segment = get_segment(handle.segment_id);
  if (!segment || !segment->base || handle.offset >= segment->size) return nullptr;
  return static_cast<std::byte *>(segment->base) + handle.offset;
}

std::size_t HeapManager::segment_count() const noexcept {
  std::lock_guard lock(segments_mutex_);
  return segments_.size();
}

HeapStatistics HeapManager::statistics() const {
  HeapStatistics total{};
  const auto count = segment_count();
  for (SegmentId id = 1; id <= count; ++id) {
    const auto *segment = get_segment(id);
    if (!segment) continue;
    const auto current = detail::statistics(*segment);
    total.segment_bytes += current.segment_bytes;
    total.allocated_bytes += current.allocated_bytes;
    total.free_bytes += current.free_bytes;
    total.allocation_count += current.allocation_count;
  }
  return total;
}

std::vector<HeapHandle> HeapManager::allocated_handles() const {
  std::vector<HeapHandle> result;
  const auto count = segment_count();
  for (SegmentId id = 1; id <= count; ++id) {
    const auto *segment = get_segment(id);
    if (!segment) continue;
    auto handles = detail::allocated_handles(*segment);
    result.insert(result.end(), handles.begin(), handles.end());
  }
  return result;
}

HeapRegistryGuard HeapManager::lock_registry() const {
  const auto *segment = get_segment(1);
  if (!segment || !detail::valid_segment(*segment)) return HeapRegistryGuard{};
  auto *header = static_cast<detail::SegmentHeader *>(segment->base);
  return HeapRegistryGuard{&header->registry_lock};
}

bool HeapManager::valid() const noexcept {
  std::lock_guard lock(segments_mutex_);
  return provider_ != nullptr && !segments_.empty();
}

} // namespace shm
