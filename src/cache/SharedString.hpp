#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "VariableHeap.hpp"
#include "types.hpp"

namespace shm {

// The string bytes live in VariableHeap. This object stores only a
// process-independent handle and lengths, so it can itself live in SHM.
struct SharedString {
  SegmentId segment_id = invalid_segment_id;
  Offset offset = invalid_offset;
  std::uint64_t size = 0;
  std::uint64_t capacity = 0;

  bool set(VariableHeap &heap, std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint64_t>::max() ||
        value.size() > std::numeric_limits<std::size_t>::max())
      return false;

    const auto new_size = static_cast<std::uint64_t>(value.size());
    if (has_storage() && new_size <= capacity) {
      if (new_size != 0) {
        auto *data = static_cast<char *>(heap.resolve(handle()));
        if (!data) return false;
        std::memmove(data, value.data(), value.size());
      }
      size = new_size;
      return true;
    }

    if (new_size == 0) {
      size = 0;
      return true;
    }

    const auto new_handle = heap.allocate(value.size());
    if (new_handle.segment_id == invalid_segment_id) return false;
    auto *new_data = static_cast<char *>(heap.resolve(new_handle));
    if (!new_data) {
      heap.deallocate(new_handle);
      return false;
    }
    std::memcpy(new_data, value.data(), value.size());

    const auto old_handle = handle();
    const bool had_storage = has_storage();
    segment_id = new_handle.segment_id;
    offset = new_handle.offset;
    size = new_size;
    capacity = heap.allocation_size(new_handle);
    if (had_storage) heap.deallocate(old_handle);
    return true;
  }

  bool append(VariableHeap &heap, std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint64_t>::max() - size ||
        value.size() > std::numeric_limits<std::size_t>::max() - size)
      return false;
    if (value.empty()) return true;

    const auto new_size = size + static_cast<std::uint64_t>(value.size());
    if (has_storage() && new_size <= capacity) {
      auto *data = static_cast<char *>(heap.resolve(handle()));
      if (!data) return false;
      std::memmove(data + size, value.data(), value.size());
      size = new_size;
      return true;
    }

    const auto new_handle = heap.allocate(static_cast<std::size_t>(new_size));
    if (new_handle.segment_id == invalid_segment_id) return false;
    auto *new_data = static_cast<char *>(heap.resolve(new_handle));
    if (!new_data) {
      heap.deallocate(new_handle);
      return false;
    }

    const auto old_handle = handle();
    const bool had_storage = has_storage();
    if (size != 0) {
      const auto *old_data = static_cast<const char *>(heap.resolve(old_handle));
      if (!old_data) {
        heap.deallocate(new_handle);
        return false;
      }
      std::memcpy(new_data, old_data, static_cast<std::size_t>(size));
    }
    // Copy before releasing the old allocation; value may refer to this string.
    std::memmove(new_data + size, value.data(), value.size());

    segment_id = new_handle.segment_id;
    offset = new_handle.offset;
    size = new_size;
    capacity = heap.allocation_size(new_handle);
    if (had_storage) heap.deallocate(old_handle);
    return true;
  }

  void clear(VariableHeap &heap) {
    if (has_storage()) heap.deallocate(handle());
    segment_id = invalid_segment_id;
    offset = invalid_offset;
    size = 0;
    capacity = 0;
  }

  std::string_view view(const VariableHeap &heap) const {
    if (size == 0) return {};
    if (!has_storage() || size > capacity) return {};
    const auto *data = static_cast<const char *>(heap.resolve(handle()));
    if (!data) return {};
    return {data, static_cast<std::size_t>(size)};
  }

  bool empty() const noexcept { return size == 0; }

 private:
  HeapHandle handle() const noexcept { return {segment_id, offset}; }

  bool has_storage() const noexcept {
    return segment_id != invalid_segment_id;
  }
};

} // namespace shm
