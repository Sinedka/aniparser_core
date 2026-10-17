// ============================================================
// IShmSegmentProvider
// Низкоуровневый интерфейс работы с SHM операционной системы.
// ============================================================

#pragma once
#include <string_view>

#include "types.hpp"

namespace shm {

class IShmSegmentProvider {
  public:
  virtual ~IShmSegmentProvider() = default;

  virtual bool create_segment(std::string_view name,
                              SegmentId id,
                              std::size_t size,
                              HeapSegment &result) = 0;

  virtual bool
  open_segment(std::string_view name, SegmentId id, HeapSegment &result) = 0;

  // Providers that map/unmap OS resources should release them here. The
  // default keeps compatibility with providers whose mappings are permanent.
  virtual void close_segment(HeapSegment &) {}

  // Explicitly remove a persistent segment. Implementations that cannot unlink
  // storage may keep the default no-op result.
  virtual bool remove_segment(std::string_view, SegmentId) { return false; }
};
} // namespace shm
