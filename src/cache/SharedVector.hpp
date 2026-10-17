#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

#include "VariableHeap.hpp"
#include "types.hpp"

namespace shm {

// A process-independent vector descriptor. Its elements live in VariableHeap;
// this object stores only a handle and element counts.
// T must not contain process-local pointers or owning resources.
template <typename T> struct SharedVector {
  static_assert(std::is_trivially_copyable_v<T>,
                "SharedVector elements must be trivially copyable");
  static_assert(std::is_trivially_destructible_v<T>,
                "SharedVector elements must be trivially destructible");

  SegmentId segment_id = invalid_segment_id;
  Offset offset = invalid_offset;
  std::uint64_t size = 0;
  std::uint64_t capacity = 0;

  bool reserve(VariableHeap &heap, std::size_t requested) {
    if (requested <= capacity) return true;
    if (requested > max_size()) return false;

    std::size_t new_capacity = static_cast<std::size_t>(capacity);
    if (new_capacity == 0) new_capacity = 1;
    while (new_capacity < requested) {
      if (new_capacity > max_size() / 2) {
        new_capacity = requested;
        break;
      }
      new_capacity *= 2;
    }

    const auto bytes = new_capacity * sizeof(T);
    const auto new_handle = heap.allocate(bytes);
    if (new_handle.segment_id == invalid_segment_id) return false;

    void *new_memory = heap.resolve(new_handle);
    if (!new_memory || !is_aligned(new_memory)) {
      heap.deallocate(new_handle);
      return false;
    }
    auto *new_data = static_cast<T *>(new_memory);

    const auto allocated_bytes = heap.allocation_size(new_handle);
    const auto actual_capacity = allocated_bytes / sizeof(T);
    if (actual_capacity < requested || actual_capacity < size) {
      heap.deallocate(new_handle);
      return false;
    }

    const auto old_handle = handle();
    const bool had_storage = has_storage();
    if (size != 0) {
      const auto *old_data = static_cast<const T *>(heap.resolve(old_handle));
      if (!old_data) {
        heap.deallocate(new_handle);
        return false;
      }
      std::memcpy(new_data, old_data, static_cast<std::size_t>(size) * sizeof(T));
    }

    segment_id = new_handle.segment_id;
    offset = new_handle.offset;
    capacity = actual_capacity;
    if (had_storage) heap.deallocate(old_handle);
    return true;
  }

  bool push_back(VariableHeap &heap, const T &value) {
    if (size >= max_size()) return false;

    // Keep a copy because value may refer to an element invalidated by reserve.
    const T value_copy = value;
    const auto new_size = static_cast<std::size_t>(size + 1);
    if (!reserve(heap, new_size)) return false;

    auto *items = data(heap);
    if (!items) return false;
    std::construct_at(items + size, value_copy);
    size = new_size;
    return true;
  }

  bool pop_back() noexcept {
    if (size == 0) return false;
    --size;
    return true;
  }

  // Like std::vector::clear, this keeps the allocated capacity for reuse.
  void clear() noexcept { size = 0; }

  // Release the backing block and reset the descriptor.
  void release(VariableHeap &heap) {
    if (has_storage()) heap.deallocate(handle());
    segment_id = invalid_segment_id;
    offset = invalid_offset;
    size = 0;
    capacity = 0;
  }

  T *data(const VariableHeap &heap) {
    if (!has_storage()) return nullptr;
    void *memory = heap.resolve(handle());
    return memory && is_aligned(memory) ? static_cast<T *>(memory) : nullptr;
  }

  const T *data(const VariableHeap &heap) const {
    if (!has_storage()) return nullptr;
    const void *memory = heap.resolve(handle());
    return memory && is_aligned(memory) ? static_cast<const T *>(memory)
                                        : nullptr;
  }

  T *at(const VariableHeap &heap, std::size_t index) {
    if (index >= size) return nullptr;
    auto *items = data(heap);
    return items ? items + index : nullptr;
  }

  const T *at(const VariableHeap &heap, std::size_t index) const {
    if (index >= size) return nullptr;
    const auto *items = data(heap);
    return items ? items + index : nullptr;
  }

  bool empty() const noexcept { return size == 0; }

 private:
  HeapHandle handle() const noexcept { return {segment_id, offset}; }

  bool has_storage() const noexcept {
    return segment_id != invalid_segment_id;
  }

  static constexpr std::size_t max_size() noexcept {
    return std::numeric_limits<std::size_t>::max() / sizeof(T);
  }

  static bool is_aligned(const void *pointer) noexcept {
    return reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0;
  }

};

} // namespace shm
