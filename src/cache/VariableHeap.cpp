#include "VariableHeap.hpp"

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

VariableHeap::VariableHeap() = default;

bool VariableHeap::create(std::string_view name,
                          std::size_t initial_segment_size,
                          IShmSegmentProvider &provider) {
  if (!manager_.create(name, initial_segment_size, provider)) return false;
  const auto *segment = manager_.get_segment(1);
  if (segment && detail::initialize_segment(*segment)) return true;
  manager_.close();
  provider.remove_segment(name, 1);
  return false;
}

bool VariableHeap::open(std::string_view name, IShmSegmentProvider &provider) {
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

void VariableHeap::close() { manager_.close(); }

HeapHandle VariableHeap::allocate(std::size_t size) {
  if (!manager_.valid() || size == 0) return {};
  manager_.refresh();

  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    const auto count = manager_.segment_count();
    for (SegmentId id = 1; id <= count; ++id) {
      const auto *segment = manager_.get_segment(id);
      if (!segment) continue;
      const auto handle = detail::try_allocate(*segment, size);
      if (handle) return handle;
    }

    const auto minimum = minimum_segment_size(size);
    if (minimum == 0 || !manager_.add_segment(minimum)) {
      manager_.refresh();
      continue;
    }
    const auto id = static_cast<SegmentId>(manager_.segment_count());
    const auto *segment = manager_.get_segment(id);
    if (!segment || !detail::initialize_segment(*segment)) return {};
    const auto handle = detail::try_allocate(*segment, size);
    if (handle) return handle;
  }
  return {};
}

bool VariableHeap::deallocate(HeapHandle handle) {
  const auto *segment = manager_.get_segment(handle.segment_id);
  return segment && detail::deallocate(*segment, handle);
}

void *VariableHeap::resolve(HeapHandle handle) const {
  if (allocation_size(handle) == 0) return nullptr;
  return manager_.resolve(handle);
}

std::size_t VariableHeap::allocation_size(HeapHandle handle) const {
  const auto *segment = manager_.get_segment(handle.segment_id);
  return segment ? detail::allocation_size(*segment, handle) : 0;
}

std::size_t VariableHeap::segment_count() const {
  return manager_.segment_count();
}

HeapStatistics VariableHeap::statistics() const {
  return manager_.statistics();
}

bool VariableHeap::valid() const { return manager_.valid(); }

} // namespace shm
