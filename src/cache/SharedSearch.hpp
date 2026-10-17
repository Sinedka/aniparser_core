#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "types.hpp"

namespace shm {
using SearchHandle = HeapHandle;
}

// A fixed-size shared-memory page for a search result. Handles are used in
// place of process-local pointers so the page works across mappings/processes.
struct SharedSearch {
  static constexpr std::size_t anime_capacity = 10;

  shm::SearchHandle self{};
  shm::SearchHandle root{};
  std::array<shm::HeapHandle, anime_capacity> anime{};
  std::uint64_t anime_count = 0;
  shm::SearchHandle continuation{};
  // Monotonic nanoseconds shared by processes on this host.
  std::uint64_t last_used_ns{};
};

static_assert(std::is_trivially_copyable_v<SharedSearch>);
static_assert(std::is_trivially_destructible_v<SharedSearch>);
