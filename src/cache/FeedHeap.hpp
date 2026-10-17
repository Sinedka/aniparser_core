#pragma once

#include <string_view>

#include "AnimeHeap.hpp"
#include "SharedFeed.hpp"

namespace shm {

// Stores fixed-size feed records in its own heap. Vectors and strings live in
// the shared VariableHeap, while anime entries reference AnimeHeap records.
class FeedHeap {
 public:
  bool create(std::string_view name, std::size_t initial_size,
              IShmSegmentProvider &provider);
  bool open(std::string_view name, IShmSegmentProvider &provider);
  void close();
  bool valid() const;
  AnimeHeap &storage();
  const AnimeHeap &storage() const;

  HeapHandle allocate();
  SharedFeed *resolve(HeapHandle handle) const;
  bool deallocate(HeapHandle handle);
  bool append_anime(SharedVector<HeapHandle> &target, HeapHandle anime);
  bool append_collection(SharedFeed &feed, std::int64_t id,
                        std::string_view title,
                        std::string_view description,
                        SharedFeedCollection **result = nullptr);

 private:
  AnimeHeap heap_{sizeof(SharedFeed)};
};

} // namespace shm
