#include "FileShmSegmentProvider.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace shm {

FileShmSegmentProvider::FileShmSegmentProvider(std::string directory)
    : directory_(std::move(directory)) {}

FileShmSegmentProvider::~FileShmSegmentProvider() {
  std::lock_guard lock(mutex_);
  for (auto &[_, mapping] : mappings_) {
    if (mapping.base && mapping.base != MAP_FAILED)
      ::munmap(mapping.base, mapping.size);
    if (mapping.fd >= 0) ::close(mapping.fd);
  }
}

std::string FileShmSegmentProvider::path_for(std::string_view name,
                                             SegmentId id) const {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char ch : name) { hash ^= ch; hash *= 1099511628211ULL; }
  return (std::filesystem::path(directory_) /
          ("heap_" + std::to_string(hash) + "_" + std::to_string(id) + ".shm"))
      .string();
}

bool FileShmSegmentProvider::map_file(const std::string &path, int fd,
                                      std::size_t size, SegmentId id,
                                      HeapSegment &result) {
  if (!size) { ::close(fd); return false; }
  std::lock_guard lock(mutex_);
  const auto it = mappings_.find(path);
  if (it != mappings_.end()) {
    ::close(fd);
    ++it->second.references;
    result = {id, it->second.size, it->second.base};
    return true;
  }
  void *base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) { ::close(fd); return false; }
  mappings_.emplace(path, Mapping{fd, base, size, 1});
  result = {id, size, base};
  return true;
}

bool FileShmSegmentProvider::create_segment(std::string_view name, SegmentId id,
                                            std::size_t size,
                                            HeapSegment &result) {
  if (directory_.empty() || name.empty() || !id || !size ||
      size > static_cast<std::size_t>(std::numeric_limits<off_t>::max()))
    return false;
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) return false;
  const auto path = path_for(name, id);
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
    ::close(fd); ::unlink(path.c_str()); return false;
  }
  if (map_file(path, fd, size, id, result)) return true;
  ::unlink(path.c_str());
  return false;
}

bool FileShmSegmentProvider::open_segment(std::string_view name, SegmentId id,
                                          HeapSegment &result) {
  if (directory_.empty() || name.empty() || !id) return false;
  const auto path = path_for(name, id);
  const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat info {};
  if (::fstat(fd, &info) != 0 || info.st_size <= 0 ||
      static_cast<std::uintmax_t>(info.st_size) >
          std::numeric_limits<std::size_t>::max()) {
    ::close(fd); return false;
  }
  return map_file(path, fd, static_cast<std::size_t>(info.st_size), id, result);
}

void FileShmSegmentProvider::close_segment(HeapSegment &segment) {
  if (!segment.base) return;
  std::lock_guard lock(mutex_);
  for (auto it = mappings_.begin(); it != mappings_.end(); ++it) {
    if (it->second.base != segment.base) continue;
    if (--it->second.references == 0) {
      ::munmap(it->second.base, it->second.size);
      ::close(it->second.fd);
      mappings_.erase(it);
    }
    break;
  }
  segment = {};
}

bool FileShmSegmentProvider::remove_segment(std::string_view name, SegmentId id) {
  if (directory_.empty() || name.empty() || !id) return false;
  return ::unlink(path_for(name, id).c_str()) == 0;
}

} // namespace shm
