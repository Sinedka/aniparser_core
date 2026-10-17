#pragma once

#include "HeapManager.hpp"
#include "IHeap.hpp"
#include "SharedSearch.hpp"

#include <cstdint>
#include <cstddef>

class Anime;

namespace shm {

class SearchHeap final : public IHeap {
 public:
  SearchHeap();

  bool create(std::string_view name, std::size_t initial_segment_size,
              IShmSegmentProvider &provider) override;
  bool open(std::string_view name, IShmSegmentProvider &provider) override;
  void close() override;
  HeapHandle allocate(std::size_t size) override;
  bool deallocate(HeapHandle handle) override;
  void *resolve(HeapHandle handle) const override;
  std::size_t allocation_size(HeapHandle handle) const override;
  std::size_t segment_count() const override;
  bool valid() const override;

  HeapHandle allocate_search();
  SharedSearch *resolve_search(HeapHandle handle) const;
  bool append_anime(HeapHandle search, HeapHandle anime);
  bool set_continuation(HeapHandle search, HeapHandle continuation);
  std::size_t evict_unused_before(std::uint64_t cutoff_ns,
                                  std::size_t max_pages);
  std::vector<HeapHandle> allocated_handles() const;
  HeapRegistryGuard lock_registry() const;

 private:
  bool destroy_claimed(HeapHandle root);
  HeapManager manager_;
};

} // namespace shm
