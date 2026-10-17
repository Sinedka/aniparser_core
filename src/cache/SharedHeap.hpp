#pragma once

#include <cstdint>

// Legacy single-segment allocator interface retained for existing callers.
// New cache code should use shm::VariableHeap from VariableHeap.hpp.
class VariableHeap {
  public:
  static constexpr uint64_t MAGIC = 0x5641524845415031ULL;
  static constexpr uint32_t VERSION = 1;
  static constexpr uint64_t ALIGNMENT = 8;

  VariableHeap(void* memory, uint64_t size)
    : memory_(static_cast<uint8_t*>(memory))
    , size_(size) {}

  void* ptr(uint64_t offset) {
    if (offset >= size_) return nullptr;
    return memory_ + offset;
  }

  const void* ptr(uint64_t offset) const {
    if (offset >= size_) return nullptr;
    return memory_ + offset;
  }

  uint64_t allocate(uint64_t size);
  void deallocate(uint64_t offset);
  uint8_t* getmemory();

  private:
  uint8_t* memory_;
  uint64_t size_;
};
