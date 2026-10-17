#include "FeedHeap.hpp"

#include <memory>

#include "cache.hpp"

namespace shm {
namespace {

void release_anime_vector(SharedVector<HeapHandle> &items,
                          VariableHeap &heap) {
  if (auto *handles = items.data(heap))
    for (std::uint64_t i = 0; i < items.size; ++i)
      if (handles[i]) Cache::release_anime(handles[i]);
  items.release(heap);
}

} // namespace

bool FeedHeap::create(std::string_view name, std::size_t initial_size,
                      IShmSegmentProvider &provider) {
  return heap_.create(name, initial_size, provider);
}

bool FeedHeap::open(std::string_view name, IShmSegmentProvider &provider) {
  return heap_.open(name, provider);
}

void FeedHeap::close() { heap_.close(); }
bool FeedHeap::valid() const { return heap_.valid(); }
AnimeHeap &FeedHeap::storage() { return heap_; }
const AnimeHeap &FeedHeap::storage() const { return heap_; }

HeapHandle FeedHeap::allocate() {
  if (!valid()) return {};
  const auto handle = heap_.allocate(sizeof(SharedFeed));
  if (!handle) return {};
  auto *feed = static_cast<SharedFeed *>(heap_.resolve(handle));
  if (!feed) {
    heap_.deallocate(handle);
    return {};
  }
  std::construct_at(feed);
  feed->self = handle;
  return handle;
}

SharedFeed *FeedHeap::resolve(HeapHandle handle) const {
  if (heap_.allocation_size(handle) < sizeof(SharedFeed)) return nullptr;
  return static_cast<SharedFeed *>(heap_.resolve(handle));
}

bool FeedHeap::deallocate(HeapHandle handle) {
  auto *feed = resolve(handle);
  if (!feed) return false;
  auto &variable_heap = Cache::variable_heap();
  release_anime_vector(feed->new_anime, variable_heap);
  release_anime_vector(feed->announcements, variable_heap);
  release_anime_vector(feed->recommendations, variable_heap);
  release_anime_vector(feed->carousel_items, variable_heap);
  release_anime_vector(feed->schedule_items, variable_heap);
  if (auto *collections = feed->collections.data(variable_heap)) {
    for (std::uint64_t i = 0; i < feed->collections.size; ++i) {
      release_anime_vector(collections[i].anime, variable_heap);
      collections[i].title.clear(variable_heap);
      collections[i].description.clear(variable_heap);
    }
  }
  feed->collections.release(variable_heap);
  std::destroy_at(feed);
  return heap_.deallocate(handle);
}

bool FeedHeap::append_anime(SharedVector<HeapHandle> &target,
                            HeapHandle anime) {
  if (!anime || !Cache::retain_anime(anime)) return false;
  if (target.push_back(Cache::variable_heap(), anime)) return true;
  Cache::release_anime(anime);
  return false;
}

bool FeedHeap::append_collection(SharedFeed &feed, std::int64_t id,
                                 std::string_view title,
                                 std::string_view description,
                                 SharedFeedCollection **result) {
  SharedFeedCollection collection{};
  collection.id = id;
  auto &variable_heap = Cache::variable_heap();
  if (!collection.title.set(variable_heap, title) ||
      !collection.description.set(variable_heap, description)) {
    collection.title.clear(variable_heap);
    collection.description.clear(variable_heap);
    return false;
  }
  if (!feed.collections.push_back(variable_heap, collection)) {
    collection.title.clear(variable_heap);
    collection.description.clear(variable_heap);
    return false;
  }
  if (result) *result = feed.collections.at(variable_heap, feed.collections.size - 1);
  return !result || *result != nullptr;
}

} // namespace shm
