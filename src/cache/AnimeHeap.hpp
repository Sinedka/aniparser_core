// ============================================================
// AnimeHeap
// Все объекты имеют одинаковый размер.
// ============================================================
#pragma once

#include <vector>

#include "HeapManager.hpp"
#include "IHeap.hpp"

namespace shm {

class AnimeHeap final : public IHeap {
  public:
  explicit AnimeHeap(std::size_t anime_size);

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

  std::vector<HeapHandle> allocated_handles() const;
  HeapRegistryGuard lock_registry() const;

  bool valid() const override;

  std::size_t anime_size() const noexcept;

  private:
  std::size_t anime_size_ = 0;
  HeapManager manager_;
};
} // namespace shm
