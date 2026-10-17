#include "CacheReader.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::string shared_object_name(std::string_view heap_name, shm::SegmentId id) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char ch : heap_name) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return "/aniparser_" + std::to_string(hash) + "_" + std::to_string(id);
}
}  // namespace

CacheReader::~CacheReader() {
  anime_.close(); variable_.close(); search_.close(); feed_.close();
}

bool CacheReader::ReadOnlyHeap::open(std::string name,
                                     std::size_t initial_segments) {
  close();
  name_ = std::move(name);
  if (name_.empty() || initial_segments == 0) return false;
  for (std::size_t i = 1; i <= initial_segments; ++i) {
    if (!map_segment(static_cast<shm::SegmentId>(i))) {
      close(); return false;
    }
  }
  return true;
}

void CacheReader::ReadOnlyHeap::close() {
  for (auto &segment : segments_) {
    if (segment.base && segment.size)
      ::munmap(segment.base, segment.size);
  }
  segments_.clear();
  name_.clear();
}

CacheReader::ReadOnlyHeap::~ReadOnlyHeap() { close(); }

bool CacheReader::ReadOnlyHeap::map_segment(shm::SegmentId id) const {
  if (id == shm::invalid_segment_id) return false;
  while (segments_.size() < id) {
    const auto next = static_cast<shm::SegmentId>(segments_.size() + 1);
    const auto object_name = shared_object_name(name_, next);
    const int fd = ::shm_open(object_name.c_str(), O_RDONLY, 0);
    if (fd < 0) return false;

    struct stat info {};
    if (::fstat(fd, &info) != 0 || info.st_size <= 0 ||
        static_cast<std::uintmax_t>(info.st_size) >
            std::numeric_limits<std::size_t>::max()) {
      ::close(fd); return false;
    }
    const auto size = static_cast<std::size_t>(info.st_size);
    void *base = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (base == MAP_FAILED) return false;
    segments_.push_back({next, size, base});
  }
  return true;
}

const void *CacheReader::ReadOnlyHeap::resolve(shm::HeapHandle handle,
                                              std::size_t bytes) const {
  if (!handle || handle.offset == shm::invalid_offset ||
      !map_segment(handle.segment_id)) return nullptr;
  const auto &segment = segments_[handle.segment_id - 1];
  if (handle.offset > segment.size || bytes > segment.size - handle.offset)
    return nullptr;
  return static_cast<const std::byte *>(segment.base) + handle.offset;
}

bool CacheReader::open(const nlohmann::json &heaps) {
  try {
    return anime_.open(heaps.at("anime").at("name").get<std::string>(),
                       heaps.at("anime").at("segments").get<std::size_t>()) &&
           variable_.open(heaps.at("variable").at("name").get<std::string>(),
                          heaps.at("variable").at("segments").get<std::size_t>()) &&
           search_.open(heaps.at("search").at("name").get<std::string>(),
                        heaps.at("search").at("segments").get<std::size_t>()) &&
           feed_.open(heaps.at("feed").at("name").get<std::string>(),
                      heaps.at("feed").at("segments").get<std::size_t>());
  } catch (...) { return false; }
}

const Anime *CacheReader::anime(shm::HeapHandle handle) const {
  return static_cast<const Anime *>(anime_.resolve(handle, sizeof(Anime)));
}

const SharedFeed *CacheReader::feed(shm::HeapHandle handle) const {
  return static_cast<const SharedFeed *>(feed_.resolve(handle, sizeof(SharedFeed)));
}

const SharedSearch *CacheReader::search(shm::SearchHandle handle) const {
  return static_cast<const SharedSearch *>(search_.resolve(handle, sizeof(SharedSearch)));
}

std::string CacheReader::string(const shm::SharedString &value) const {
  if (value.size == 0) return {};
  if (value.size > value.capacity ||
      value.size > std::numeric_limits<std::size_t>::max()) return {};
  const auto *data = static_cast<const char *>(variable_.resolve(
      {value.segment_id, value.offset}, static_cast<std::size_t>(value.size)));
  return data ? std::string(data, static_cast<std::size_t>(value.size)) : std::string{};
}

