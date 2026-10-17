#pragma once
#include <cstddef>
#include <cstdint>

namespace shm {
using SegmentId = std::uint32_t;
using Offset = std::uint64_t;

inline constexpr SegmentId invalid_segment_id = 0;
inline constexpr Offset invalid_offset = 0;

struct HeapHandle
{
    SegmentId segment_id = invalid_segment_id;
    Offset offset = invalid_offset;

    explicit operator bool() const noexcept {
        return segment_id != invalid_segment_id;
    }
};

struct HeapStatistics {
    std::size_t segment_bytes = 0;
    std::size_t allocated_bytes = 0;
    std::size_t free_bytes = 0;
    std::size_t allocation_count = 0;
};

// ============================================================
// HeapSegment
// Локальное описание отображённого SHM-сегмента.
// ============================================================

struct HeapSegment {

  SegmentId id = invalid_segment_id;
  std::size_t size = 0;
  void* base = nullptr;
};

} // namespace shm
