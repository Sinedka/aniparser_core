#include "anime_collection.hpp"

bool AnimeCollection::add(Id anime_id, std::size_t limit) {
  if (anime_id <= 0 || limit == 0) return false;
  if (const auto found = index_.find(anime_id); found != index_.end()) {
    ids_.erase(found->second);
    index_.erase(found);
  }
  ids_.push_front(anime_id);
  index_.emplace(anime_id, ids_.begin());
  while (ids_.size() > limit) {
    index_.erase(ids_.back());
    ids_.pop_back();
  }
  return true;
}

bool AnimeCollection::remove(Id anime_id) {
  const auto found = index_.find(anime_id);
  if (found == index_.end()) return false;
  ids_.erase(found->second);
  index_.erase(found);
  return true;
}

bool AnimeCollection::contains(Id anime_id) const {
  return index_.contains(anime_id);
}

std::vector<AnimeCollection::Id> AnimeCollection::ids() const {
  return {ids_.begin(), ids_.end()};
}

void AnimeCollection::load(const std::vector<Id> &ids, std::size_t limit) {
  ids_.clear();
  index_.clear();
  for (const auto anime_id : ids) {
    if (anime_id <= 0 || index_.contains(anime_id) || ids_.size() >= limit)
      continue;
    ids_.push_back(anime_id);
    index_.emplace(anime_id, std::prev(ids_.end()));
  }
}
