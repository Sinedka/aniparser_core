#include "server.hpp"

shm::HeapHandle Server::getAnime(std::int64_t anime_id,
                                 AnimeCallback callback) {
  auto* anime =
      core_.getAnime(anime_id, [callback = std::move(callback)](
                                   std::int64_t updated_id, Anime* updated) {
        if (callback)
          callback(updated_id, updated ? updated->self : shm::HeapHandle{},
                   updated != nullptr);
      });
  return anime ? anime->self : shm::HeapHandle{};
}

bool Server::animeNeedsRefresh(std::int64_t anime_id) {
  return core_.animeNeedsRefresh(anime_id);
}

bool Server::animeIsLoaded(std::int64_t anime_id) {
  return core_.animeIsLoaded(anime_id);
}

bool Server::deleteAnime(shm::HeapHandle anime) {
  return anime && core_.releaseAnime(anime);
}

bool Server::getSearch(yani::SearchInput params, SearchCallback callback) {
  if (!callback) return false;
  return core_.searchTitles(
      std::move(params),
      [callback = std::move(callback)](const yani::SearchInput &page_params,
                                       SharedSearch* page) {
        return callback(page_params, page ? page->root : shm::SearchHandle{});
      });
}

bool Server::searchPageNeedsRefresh(yani::SearchInput params) {
  return core_.searchPageNeedsRefresh(std::move(params));
}

bool Server::deleteSearch(shm::SearchHandle search) {
  return search && core_.releaseSearch(search);
}

bool Server::getFeed(std::vector<std::string> watches, FeedCallback callback) {
  if (!callback) return false;
  return core_.getFeed(
      std::move(watches),
      [callback = std::move(callback)](
          const std::vector<std::string> &requested_watches, SharedFeed* feed) {
        callback(requested_watches, feed ? feed->self : shm::HeapHandle{});
      });
}

bool Server::feedNeedsRefresh(const std::vector<std::string> &watches) {
  return core_.feedNeedsRefresh(watches);
}

bool Server::deleteFeed(shm::HeapHandle feed) {
  return feed && core_.releaseFeed(feed);
}
