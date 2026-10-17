#define NOMINMAX
#include "WindowsShmSegmentProvider.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <utility>

#include <windows.h>

namespace shm {
namespace {
std::wstring wide_path(const std::string &path) {
  return std::filesystem::u8path(path.begin(), path.end()).wstring();
}

} // namespace

WindowsShmSegmentProvider::WindowsShmSegmentProvider(std::string directory)
    : directory_(std::move(directory)) {}

WindowsShmSegmentProvider::~WindowsShmSegmentProvider() {
  std::lock_guard lock(mutex_);
  for (auto &[_, mapping] : mappings_) {
    if (mapping.base) ::UnmapViewOfFile(mapping.base);
    if (mapping.mapping) ::CloseHandle(static_cast<HANDLE>(mapping.mapping));
    if (mapping.file) ::CloseHandle(static_cast<HANDLE>(mapping.file));
  }
}

std::string WindowsShmSegmentProvider::path_for(std::string_view name,
                                                SegmentId id) const {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char ch : name) { hash ^= ch; hash *= 1099511628211ULL; }
  const auto path = (std::filesystem::u8path(directory_.begin(), directory_.end()) /
      ("heap_" + std::to_string(hash) + "_" + std::to_string(id) + ".shm"))
      .u8string();
  return {reinterpret_cast<const char *>(path.data()), path.size()};
}

bool WindowsShmSegmentProvider::map_file(const std::string &path, void *file,
                                         std::size_t size, SegmentId id,
                                         HeapSegment &result) {
  if (!size) { ::CloseHandle(static_cast<HANDLE>(file)); return false; }
  std::lock_guard lock(mutex_);
  if (const auto it = mappings_.find(path); it != mappings_.end()) {
    ::CloseHandle(static_cast<HANDLE>(file));
    ++it->second.references;
    result = {id, it->second.size, it->second.base};
    return true;
  }
  const auto mapping = ::CreateFileMappingW(static_cast<HANDLE>(file), nullptr,
      PAGE_READWRITE, 0, 0, nullptr);
  if (!mapping) { ::CloseHandle(static_cast<HANDLE>(file)); return false; }
  void *base = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!base) {
    ::CloseHandle(mapping); ::CloseHandle(static_cast<HANDLE>(file)); return false;
  }
  mappings_.emplace(path, Mapping{file, mapping, base, size, 1});
  result = {id, size, base};
  return true;
}

bool WindowsShmSegmentProvider::create_segment(std::string_view name,
                                               SegmentId id,
                                               std::size_t size,
                                               HeapSegment &result) {
  if (directory_.empty() || name.empty() || !id || !size ||
      size > static_cast<std::size_t>(std::numeric_limits<LONGLONG>::max()))
    return false;
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) return false;
  const auto path = path_for(name, id);
  const auto wide = wide_path(path);
  HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER file_size{};
  file_size.QuadPart = static_cast<LONGLONG>(size);
  if (!::SetFilePointerEx(file, file_size, nullptr, FILE_BEGIN) ||
      !::SetEndOfFile(file)) {
    ::CloseHandle(file); ::DeleteFileW(wide.c_str()); return false;
  }
  if (map_file(path, file, size, id, result)) return true;
  ::DeleteFileW(wide.c_str());
  return false;
}

bool WindowsShmSegmentProvider::open_segment(std::string_view name, SegmentId id,
                                             HeapSegment &result) {
  if (directory_.empty() || name.empty() || !id) return false;
  const auto path = path_for(name, id);
  const auto wide = wide_path(path);
  HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      static_cast<unsigned long long>(size.QuadPart) >
          std::numeric_limits<std::size_t>::max()) {
    ::CloseHandle(file); return false;
  }
  return map_file(path, file, static_cast<std::size_t>(size.QuadPart), id, result);
}

void WindowsShmSegmentProvider::close_segment(HeapSegment &segment) {
  if (!segment.base) return;
  std::lock_guard lock(mutex_);
  for (auto it = mappings_.begin(); it != mappings_.end(); ++it) {
    if (it->second.base != segment.base) continue;
    if (--it->second.references == 0) {
      if (it->second.base) ::UnmapViewOfFile(it->second.base);
      if (it->second.mapping) ::CloseHandle(static_cast<HANDLE>(it->second.mapping));
      if (it->second.file) ::CloseHandle(static_cast<HANDLE>(it->second.file));
      mappings_.erase(it);
    }
    break;
  }
  segment = {};
}

bool WindowsShmSegmentProvider::remove_segment(std::string_view name,
                                                SegmentId id) {
  if (directory_.empty() || name.empty() || !id) return false;
  return ::DeleteFileW(wide_path(path_for(name, id)).c_str()) != 0;
}

} // namespace shm
