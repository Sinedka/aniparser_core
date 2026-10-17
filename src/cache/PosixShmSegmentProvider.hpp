#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "IShmSegmentProvider.hpp"

namespace shm {

// Linux/POSIX shared-memory implementation of IShmSegmentProvider.
// Segment names are persistent until unlink_segment() is called.
class PosixShmSegmentProvider final : public IShmSegmentProvider {
 public:
  PosixShmSegmentProvider() = default;
  ~PosixShmSegmentProvider() override;

  PosixShmSegmentProvider(const PosixShmSegmentProvider &) = delete;
  PosixShmSegmentProvider &operator=(const PosixShmSegmentProvider &) = delete;

  bool create_segment(std::string_view name,
                      SegmentId id,
                      std::size_t size,
                      HeapSegment &result) override;

  bool open_segment(std::string_view name,
                    SegmentId id,
                    HeapSegment &result) override;

  void close_segment(HeapSegment &segment) override;

  bool remove_segment(std::string_view name, SegmentId id) override;

 private:
  struct Mapping {
    int fd = -1;
    void *base = nullptr;
    std::size_t size = 0;
    std::size_t references = 1;
  };

  static std::string os_name(std::string_view name, SegmentId id);
  bool map_segment(const std::string &key, int fd, std::size_t size,
                   SegmentId id, HeapSegment &result);

  std::mutex mutex_;
  std::map<std::string, Mapping> mappings_;
};

} // namespace shm
