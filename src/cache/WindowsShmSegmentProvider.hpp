#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "IShmSegmentProvider.hpp"

namespace shm {

// Windows backend: persistent, app/user-private files mapped with
// CreateFileMapping/MapViewOfFile. Use the same directory in every process.
class WindowsShmSegmentProvider final : public IShmSegmentProvider {
 public:
  explicit WindowsShmSegmentProvider(std::string directory);
  ~WindowsShmSegmentProvider() override;
  WindowsShmSegmentProvider(const WindowsShmSegmentProvider &) = delete;
  WindowsShmSegmentProvider &operator=(const WindowsShmSegmentProvider &) = delete;

  bool create_segment(std::string_view name, SegmentId id, std::size_t size,
                      HeapSegment &result) override;
  bool open_segment(std::string_view name, SegmentId id,
                    HeapSegment &result) override;
  void close_segment(HeapSegment &segment) override;
  bool remove_segment(std::string_view name, SegmentId id) override;

 private:
  struct Mapping {
    void *file = nullptr;
    void *mapping = nullptr;
    void *base = nullptr;
    std::size_t size = 0;
    std::size_t references = 0;
  };
  std::string path_for(std::string_view name, SegmentId id) const;
  bool map_file(const std::string &path, void *file, std::size_t size,
                SegmentId id, HeapSegment &result);

  std::string directory_;
  std::mutex mutex_;
  std::map<std::string, Mapping> mappings_;
};

} // namespace shm
