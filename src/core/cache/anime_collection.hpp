#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

// Shared ordered, unique anime-ID storage for history, favorites and states.
class AnimeCollection {
  public:
  using Id = std::int64_t;

  AnimeCollection() = default;
  AnimeCollection(const AnimeCollection &) = delete;
  AnimeCollection &operator=(const AnimeCollection &) = delete;
  void swap(AnimeCollection &other) noexcept {
    ids_.swap(other.ids_);
    index_.swap(other.index_);
  }

  bool add(Id anime_id, std::size_t limit);
  bool remove(Id anime_id);
  bool contains(Id anime_id) const;
  std::size_t size() const { return ids_.size(); }
  std::vector<Id> ids() const;
  void load(const std::vector<Id> &ids, std::size_t limit);

  protected:
  std::list<Id> ids_;
  std::unordered_map<Id, std::list<Id>::iterator> index_;
};

class HistoryCollection final : public AnimeCollection {};
class FavoritesCollection final : public AnimeCollection {};
class AnimeStatusCollection final : public AnimeCollection {};
