#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
#include <limits>

#include <Anime.hpp>
#include <SharedFeed.hpp>
#include <SharedSearch.hpp>

#include <nlohmann/json_fwd.hpp>

class CacheReader {
 public:
  CacheReader() = default;
  ~CacheReader();
  CacheReader(const CacheReader &) = delete;
  CacheReader &operator=(const CacheReader &) = delete;

  bool open(const nlohmann::json &hello_heaps);
  const Anime *anime(shm::HeapHandle handle) const;
  const SharedFeed *feed(shm::HeapHandle handle) const;
  const SharedSearch *search(shm::SearchHandle handle) const;
  std::string string(const shm::SharedString &value) const;

  template <typename T>
  const T *vector(const shm::SharedVector<T> &value) const {
    if (value.size == 0 || value.segment_id == shm::invalid_segment_id ||
        value.size > value.capacity || value.size > max_elements<T>())
      return nullptr;
    const auto count = static_cast<std::size_t>(value.size);
    const auto *bytes = variable_.resolve({value.segment_id, value.offset},
                                           count * sizeof(T));
    return static_cast<const T *>(bytes);
  }

 private:
  class ReadOnlyHeap {
   public:
    ~ReadOnlyHeap();
    ReadOnlyHeap() = default;
    ReadOnlyHeap(const ReadOnlyHeap &) = delete;
    ReadOnlyHeap &operator=(const ReadOnlyHeap &) = delete;
    bool open(std::string name, std::size_t initial_segments);
    void close();
    const void *resolve(shm::HeapHandle handle, std::size_t bytes) const;

   private:
    bool map_segment(shm::SegmentId id) const;
    std::string name_;
    mutable std::vector<shm::HeapSegment> segments_;
  };

  template <typename T>
  static constexpr std::size_t max_elements() {
    return std::numeric_limits<std::size_t>::max() / sizeof(T);
  }

  ReadOnlyHeap anime_;
  ReadOnlyHeap variable_;
  ReadOnlyHeap search_;
  ReadOnlyHeap feed_;
};
