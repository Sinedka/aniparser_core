#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include "types.hpp"

namespace shm::detail {

inline constexpr std::uint64_t segment_magic = 0x5641524845415031ULL; // VARHEAP1
inline constexpr std::uint32_t segment_version = 2;
inline constexpr std::uint32_t block_allocated = 1;

struct alignas(8) SegmentHeader {
  std::atomic<std::uint64_t> magic{0};
  std::uint32_t version = 0;
  std::uint32_t reserved = 0;
  std::uint64_t segment_size = 0;
  std::atomic_flag lock = ATOMIC_FLAG_INIT;
  std::atomic_flag registry_lock = ATOMIC_FLAG_INIT;
};

struct alignas(8) BlockHeader {
  std::uint64_t payload_size = 0;
  std::uint32_t flags = 0;
  std::uint32_t reserved = 0;
};

class SegmentLock {
 public:
  explicit SegmentLock(SegmentHeader &header) : header_(header) {
    while (header_.lock.test_and_set(std::memory_order_acquire)) {}
  }
  ~SegmentLock() { header_.lock.clear(std::memory_order_release); }
  SegmentLock(const SegmentLock &) = delete;
  SegmentLock &operator=(const SegmentLock &) = delete;

 private:
  SegmentHeader &header_;
};

inline std::size_t align_up(std::size_t value) {
  constexpr auto alignment = alignof(BlockHeader);
  if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1))
    return 0;
  return (value + alignment - 1) & ~(alignment - 1);
}

inline bool initialize_segment(const HeapSegment &segment) {
  if (!segment.base || segment.size < sizeof(SegmentHeader) +
                                          sizeof(BlockHeader) + alignof(BlockHeader))
    return false;

  auto *header = ::new (segment.base) SegmentHeader{};
  header->version = segment_version;
  header->segment_size = segment.size;
  header->lock.clear(std::memory_order_relaxed);

  const auto first_block_offset = align_up(sizeof(SegmentHeader));
  if (first_block_offset == 0 ||
      first_block_offset + sizeof(BlockHeader) >= segment.size)
    return false;

  auto *first = ::new (static_cast<std::byte *>(segment.base) + first_block_offset)
      BlockHeader{};
  const auto available = segment.size - first_block_offset - sizeof(BlockHeader);
  first->payload_size = available & ~(alignof(BlockHeader) - 1);
  header->magic.store(segment_magic, std::memory_order_release);
  return first->payload_size != 0;
}

inline bool valid_segment(const HeapSegment &segment) {
  if (!segment.base || segment.size < sizeof(SegmentHeader)) return false;
  auto *header = static_cast<SegmentHeader *>(segment.base);
  return header->magic.load(std::memory_order_acquire) == segment_magic &&
         header->version == segment_version &&
         header->segment_size <= segment.size;
}

inline HeapHandle try_allocate(const HeapSegment &segment,
                               std::size_t requested_size) {
  if (!valid_segment(segment)) return {};
  const auto payload_size = align_up(requested_size == 0 ? 1 : requested_size);
  if (payload_size == 0) return {};

  auto *header = static_cast<SegmentHeader *>(segment.base);
  SegmentLock lock(*header);
  auto *base = static_cast<std::byte *>(segment.base);
  const auto first_offset = align_up(sizeof(SegmentHeader));
  auto offset = first_offset;

  while (offset <= segment.size - sizeof(BlockHeader)) {
    auto *block = reinterpret_cast<BlockHeader *>(base + offset);
    if (block->payload_size > segment.size - offset - sizeof(BlockHeader))
      return {};
    if (block->flags == 0 && block->payload_size >= payload_size) {
      const auto remaining = block->payload_size - payload_size;
      if (remaining >= sizeof(BlockHeader) + alignof(BlockHeader)) {
        const auto next_offset = offset + sizeof(BlockHeader) + payload_size;
        auto *next = ::new (base + next_offset) BlockHeader{};
        next->payload_size = remaining - sizeof(BlockHeader);
        block->payload_size = payload_size;
      }
      block->flags = block_allocated;
      return {segment.id, offset + sizeof(BlockHeader)};
    }

    const auto step = sizeof(BlockHeader) + block->payload_size;
    if (step > segment.size - offset) return {};
    offset += step;
  }
  return {};
}

inline bool deallocate(const HeapSegment &segment, HeapHandle handle) {
  if (!valid_segment(segment) || handle.segment_id != segment.id) return false;
  auto *header = static_cast<SegmentHeader *>(segment.base);
  SegmentLock lock(*header);
  auto *base = static_cast<std::byte *>(segment.base);
  const auto first_offset = align_up(sizeof(SegmentHeader));
  auto offset = first_offset;
  BlockHeader *previous = nullptr;
  BlockHeader *target = nullptr;

  while (offset <= segment.size - sizeof(BlockHeader)) {
    auto *block = reinterpret_cast<BlockHeader *>(base + offset);
    if (block->payload_size > segment.size - offset - sizeof(BlockHeader))
      return false;
    if (offset + sizeof(BlockHeader) == handle.offset) {
      target = block;
      break;
    }
    const auto step = sizeof(BlockHeader) + block->payload_size;
    if (step > segment.size - offset) return false;
    previous = block;
    offset += step;
  }

  if (!target || target->flags != block_allocated) return false;
  target->flags = 0;

  const auto next_offset = offset + sizeof(BlockHeader) + target->payload_size;
  if (next_offset <= segment.size - sizeof(BlockHeader)) {
    auto *next = reinterpret_cast<BlockHeader *>(base + next_offset);
    if (next->flags == 0 &&
        next->payload_size <= segment.size - next_offset - sizeof(BlockHeader))
      target->payload_size += sizeof(BlockHeader) + next->payload_size;
  }
  if (previous && previous->flags == 0)
    previous->payload_size += sizeof(BlockHeader) + target->payload_size;
  return true;
}

inline std::size_t allocation_size(const HeapSegment &segment,
                                   HeapHandle handle) {
  if (!valid_segment(segment) || handle.segment_id != segment.id) return 0;
  auto *header = static_cast<SegmentHeader *>(segment.base);
  SegmentLock lock(*header);
  auto *base = static_cast<std::byte *>(segment.base);
  auto offset = align_up(sizeof(SegmentHeader));

  while (offset <= segment.size - sizeof(BlockHeader)) {
    auto *block = reinterpret_cast<BlockHeader *>(base + offset);
    if (block->payload_size > segment.size - offset - sizeof(BlockHeader))
      return 0;
    if (offset + sizeof(BlockHeader) == handle.offset)
      return block->flags == block_allocated
                 ? static_cast<std::size_t>(block->payload_size)
                 : 0;
    const auto step = sizeof(BlockHeader) + block->payload_size;
    if (step > segment.size - offset) return 0;
    offset += step;
  }
  return 0;
}

inline HeapStatistics statistics(const HeapSegment &segment) {
  HeapStatistics result{};
  if (!valid_segment(segment)) return result;

  auto *header = static_cast<SegmentHeader *>(segment.base);
  SegmentLock lock(*header);
  result.segment_bytes = segment.size;

  auto *base = static_cast<std::byte *>(segment.base);
  auto offset = align_up(sizeof(SegmentHeader));
  while (offset <= segment.size - sizeof(BlockHeader)) {
    const auto *block = reinterpret_cast<const BlockHeader *>(base + offset);
    if (block->payload_size > segment.size - offset - sizeof(BlockHeader))
      return {};

    if (block->flags == block_allocated) {
      result.allocated_bytes += static_cast<std::size_t>(block->payload_size);
      ++result.allocation_count;
    } else {
      result.free_bytes += static_cast<std::size_t>(block->payload_size);
    }

    const auto step = sizeof(BlockHeader) + block->payload_size;
    if (step > segment.size - offset) break;
    offset += step;
  }
  return result;
}

inline std::vector<HeapHandle> allocated_handles(const HeapSegment &segment) {
  std::vector<HeapHandle> handles;
  if (!valid_segment(segment)) return handles;

  auto *header = static_cast<SegmentHeader *>(segment.base);
  SegmentLock lock(*header);
  auto *base = static_cast<std::byte *>(segment.base);
  auto offset = align_up(sizeof(SegmentHeader));
  while (offset <= segment.size - sizeof(BlockHeader)) {
    const auto *block = reinterpret_cast<const BlockHeader *>(base + offset);
    if (block->payload_size > segment.size - offset - sizeof(BlockHeader))
      return {};
    if (block->flags == block_allocated)
      handles.push_back(
          {segment.id, offset + sizeof(BlockHeader)});
    const auto step = sizeof(BlockHeader) + block->payload_size;
    if (step > segment.size - offset) return {};
    offset += step;
  }
  return handles;
}

} // namespace shm::detail
