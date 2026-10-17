// ============================================================
// VariableHeap
// Блоки данных произвольного размера.
// ============================================================
#pragma once

#include "HeapManager.hpp"
#include "IHeap.hpp"
namespace shm {

class VariableHeap final : public IHeap {
  public:
  VariableHeap();

  bool create(std::string_view name,
              std::size_t initial_segment_size,
              IShmSegmentProvider &provider) override;

  bool open(std::string_view name, IShmSegmentProvider &provider) override;

  void close() override;

  HeapHandle allocate(std::size_t size) override;

  bool deallocate(HeapHandle handle) override;

  void* resolve(HeapHandle handle) const override;

  std::size_t allocation_size(HeapHandle handle) const override;

  std::size_t segment_count() const override;

  HeapStatistics statistics() const;

  bool valid() const override;

  private:
  HeapManager manager_;
};

} // namespace shm
