// ============================================================
// IHeap
// ============================================================
#pragma once
#include <cstddef>
#include <string_view>

#include "IShmSegmentProvider.hpp"
#include "types.hpp"


namespace shm {

class IHeap {
  public:
  virtual ~IHeap() = default;

  virtual bool create(std::string_view name,
                      std::size_t initial_segment_size,
                      IShmSegmentProvider &provider) = 0;

  virtual bool open(std::string_view name, IShmSegmentProvider &provider) = 0;

  virtual void close() = 0;

  virtual HeapHandle allocate(std::size_t size) = 0;

  virtual bool deallocate(HeapHandle handle) = 0;

  virtual void* resolve(HeapHandle handle) const = 0;

  virtual std::size_t allocation_size(HeapHandle handle) const = 0;

  virtual std::size_t segment_count() const = 0;

  virtual bool valid() const = 0;
};
} // namespace shm
