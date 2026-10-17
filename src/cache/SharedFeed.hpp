#pragma once

#include <cstdint>
#include <type_traits>

#include "SharedString.hpp"
#include "SharedVector.hpp"

struct SharedFeedCollection {
  std::int64_t id{};
  shm::SharedString title{};
  shm::SharedString description{};
  shm::SharedVector<shm::HeapHandle> anime{};
};

struct SharedFeed {
  shm::HeapHandle self{};
  std::int64_t carousel_season{};
  std::int64_t carousel_year{};
  shm::SharedVector<shm::HeapHandle> new_anime{};
  shm::SharedVector<shm::HeapHandle> announcements{};
  shm::SharedVector<shm::HeapHandle> recommendations{};
  shm::SharedVector<shm::HeapHandle> carousel_items{};
  shm::SharedVector<shm::HeapHandle> schedule_items{};
  shm::SharedVector<SharedFeedCollection> collections{};
};

static_assert(std::is_trivially_copyable_v<SharedFeedCollection>);
static_assert(std::is_trivially_destructible_v<SharedFeedCollection>);
static_assert(std::is_trivially_copyable_v<SharedFeed>);
static_assert(std::is_trivially_destructible_v<SharedFeed>);
