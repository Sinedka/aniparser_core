#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Anime.hpp"
#include "AnimeHeap.hpp"
#include "FeedHeap.hpp"
#include "SearchHeap.hpp"
#include "VariableHeap.hpp"
#include "types.hpp"

class Cache {
  public:
  static bool create(std::string_view name,
                     std::size_t anime_segment_size,
                     std::size_t variable_segment_size,
                     shm::IShmSegmentProvider &provider);

  static bool open(std::string_view name, shm::IShmSegmentProvider &provider);

  static void close();

  static shm::AnimeHeap &anime_heap();

  static shm::VariableHeap &variable_heap();
  static shm::SearchHeap &search_heap();
  static shm::FeedHeap &feed_heap();

  static shm::HeapHandle allocate_anime();

  // Resolving is a short-lived raw pointer operation. Keep a retained handle
  // while accessing the record so an LRU eviction cannot reclaim it.
  static Anime *resolve_anime(shm::HeapHandle handle);

  static bool deallocate_anime(shm::HeapHandle handle);

  // Each client, relationship edge, or search result owns one reference.
  static bool retain_anime(shm::HeapHandle handle);
  static bool release_anime(shm::HeapHandle handle);
  static bool discard_anime(shm::HeapHandle handle);
  static shm::HeapHandle find_or_create_anime(std::int64_t anime_id,
                                               bool *created = nullptr);
  // Physically remove up to max_count zero-reference records, oldest first.
  static std::size_t evict_oldest_unused(std::size_t max_count);
  static std::size_t evict_unused_before(std::uint64_t cutoff_ns,
                                         std::size_t max_count);
  static shm::HeapHandle allocate_search();
  static bool deallocate_search(shm::HeapHandle handle);
  static shm::HeapHandle allocate_feed();
  static bool deallocate_feed(shm::HeapHandle handle);

  private:
  static shm::AnimeHeap anime_heap_;
  static shm::VariableHeap variable_heap_;
  static shm::SearchHeap search_heap_;
  static shm::FeedHeap feed_heap_;
};
