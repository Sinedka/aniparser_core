#include "PosixShmSegmentProvider.hpp"

#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace shm {

PosixShmSegmentProvider::~PosixShmSegmentProvider() {
  std::lock_guard lock(mutex_);
  for (auto &[_, mapping] : mappings_) {
    if (mapping.base && mapping.base != MAP_FAILED)
      ::munmap(mapping.base, mapping.size);
    if (mapping.fd >= 0) ::close(mapping.fd);
  }
}

std::string PosixShmSegmentProvider::os_name(std::string_view name,
                                              SegmentId id) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char ch : name) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return "/aniparser_" + std::to_string(hash) + "_" + std::to_string(id);
}

bool PosixShmSegmentProvider::map_segment(const std::string &key,
                                          int fd,
                                          std::size_t size,
                                          SegmentId id,
                                          HeapSegment &result) {
  if (size == 0) {
    ::close(fd);
    return false;
  }

  void *base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    ::close(fd);
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    const auto existing = mappings_.find(key);
    if (existing != mappings_.end()) {
      ::munmap(base, size);
      ::close(fd);
      ++existing->second.references;
      result = {id, existing->second.size, existing->second.base};
      return true;
    }
    mappings_.emplace(key, Mapping{fd, base, size, 1});
  }
  result = {id, size, base};
  return true;
}

bool PosixShmSegmentProvider::create_segment(std::string_view name,
                                            SegmentId id,
                                            std::size_t size,
                                            HeapSegment &result) {
  if (name.empty() || id == invalid_segment_id || size == 0 ||
      size > static_cast<std::size_t>(std::numeric_limits<off_t>::max()))
    return false;

  const auto key = os_name(name, id);
  const int fd = ::shm_open(key.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return false;
  if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
    ::close(fd);
    ::shm_unlink(key.c_str());
    return false;
  }
  if (map_segment(key, fd, size, id, result)) return true;
  ::shm_unlink(key.c_str());
  return false;
}

bool PosixShmSegmentProvider::open_segment(std::string_view name,
                                          SegmentId id,
                                          HeapSegment &result) {
  if (name.empty() || id == invalid_segment_id) return false;
  const auto key = os_name(name, id);
  const int fd = ::shm_open(key.c_str(), O_RDWR, 0600);
  if (fd < 0) return false;

  struct stat info {};
  if (::fstat(fd, &info) != 0 || info.st_size <= 0 ||
      static_cast<std::uintmax_t>(info.st_size) >
          std::numeric_limits<std::size_t>::max()) {
    ::close(fd);
    return false;
  }
  return map_segment(key, fd, static_cast<std::size_t>(info.st_size), id, result);
}

void PosixShmSegmentProvider::close_segment(HeapSegment &segment) {
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

bool PosixShmSegmentProvider::remove_segment(std::string_view name,
                                            SegmentId id) {
  if (name.empty() || id == invalid_segment_id) return false;
  const auto key = os_name(name, id);
  return ::shm_unlink(key.c_str()) == 0;
}

} // namespace shm
