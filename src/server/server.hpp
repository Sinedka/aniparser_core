#pragma once

#include <core.hpp>

#include <functional>
#include <string>
#include <vector>

// Process-local facade used by the IPC layer. Results crossing the process
// boundary are shared-memory handles; clients resolve them after mapping the
// cache segments.
class Server {
  public:
  using AnimeCallback =
      std::function<void(std::int64_t, shm::HeapHandle, bool)>;
  using SearchCallback =
      std::function<bool(const yani::SearchInput &, shm::SearchHandle)>;
  using FeedCallback =
      std::function<void(const std::vector<std::string> &, shm::HeapHandle)>;

  explicit Server(Core &core) : core_(core) {}

  shm::HeapHandle getAnime(std::int64_t anime_id, AnimeCallback callback = {});
  bool animeNeedsRefresh(std::int64_t anime_id);
  bool animeIsLoaded(std::int64_t anime_id);
  bool deleteAnime(shm::HeapHandle anime);

  bool getSearch(yani::SearchInput params, SearchCallback callback);
  bool searchPageNeedsRefresh(yani::SearchInput params);
  bool deleteSearch(shm::SearchHandle search);

  bool getFeed(std::vector<std::string> watches, FeedCallback callback);
  bool feedNeedsRefresh(const std::vector<std::string> &watches);
  bool deleteFeed(shm::HeapHandle feed);

  private:
  Core &core_;
};
