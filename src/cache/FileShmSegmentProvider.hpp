#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "IShmSegmentProvider.hpp"

namespace shm {

// Android/iOS backend: maps private or App Group files with MAP_SHARED. Pass
// the same directory to every process that shares the cache.
class FileShmSegmentProvider final : public IShmSegmentProvider {
 public:
  explicit FileShmSegmentProvider(std::string directory);
  ~FileShmSegmentProvider() override;
  FileShmSegmentProvider(const FileShmSegmentProvider &) = delete;
  FileShmSegmentProvider &operator=(const FileShmSegmentProvider &) = delete;

  bool create_segment(std::string_view name, SegmentId id, std::size_t size,
                      HeapSegment &result) override;
  bool open_segment(std::string_view name, SegmentId id,
                    HeapSegment &result) override;
  void close_segment(HeapSegment &segment) override;
  bool remove_segment(std::string_view name, SegmentId id) override;

 private:
  struct Mapping { int fd = -1; void *base = nullptr; std::size_t size = 0; std::size_t references = 0; };
  std::string path_for(std::string_view name, SegmentId id) const;
  bool map_file(const std::string &path, int fd, std::size_t size,
                SegmentId id, HeapSegment &result);

  std::string directory_;
  std::mutex mutex_;
  std::map<std::string, Mapping> mappings_;
};

} // namespace shm
