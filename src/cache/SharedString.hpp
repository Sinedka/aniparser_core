#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include "cache.hpp"

struct SharedString {
  private:
  uint64_t offset = 0;
  uint64_t size = 0;
  uint64_t capacity = 0;

  public:
  bool set(class SharedHeap &heap, std::string_view value);

  void clear();

  const char* data(const SharedHeap &heap) const;
  char* data(SharedHeap &heap);

  std::string_view view(const SharedHeap &heap = cache) const;

  uint64_t length() const { return size; }

  uint64_t max_size() const { return capacity; }

  bool empty() const { return size == 0; }

  void destroy(SharedHeap &heap);
};
