#include <algorithm>
#include <iostream>
#include <utility>

#include "core.hpp"

bool Core::addHistory(std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  {
    std::lock_guard lock(mutex_);
    history_.add(anime_id, kHistoryLimit);
    ++history_revision_;
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist collections\n";
  notify_history_subscribers("history");
  return true;
}

bool Core::removeHistory(std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  {
    std::lock_guard lock(mutex_);
    if (!history_.remove(anime_id)) return false;
    ++history_revision_;
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist collections\n";
  notify_history_subscribers("history");
  return true;
}

bool Core::updateAnimeSaveData(AnimeSaveData data) {
  if (data.anime_id <= 0 || data.player.empty() || data.dubber.empty() ||
      data.episode.empty() || data.time < 0)
    return false;
  const auto anime_id = data.anime_id;
  std::unique_lock save_lock(collection_save_mutex_);
  bool in_history = false;
  {
    std::lock_guard lock(mutex_);
    anime_save_data_.insert_or_assign(anime_id, std::move(data));
    in_history = history_.contains(anime_id);
    if (in_history) ++history_revision_;
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  const bool saved = save_manager_.saveState(state);
  if (!saved) std::cerr << "[core] failed to persist anime save data\n";
  if (in_history) notify_history_subscribers("save_data", {anime_id});
  return true;
}

std::optional<Core::AnimeSaveData>
Core::getAnimeSaveData(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  const auto found = anime_save_data_.find(anime_id);
  if (found == anime_save_data_.end()) return std::nullopt;
  return found->second;
}

std::vector<Core::HistoryEntry> Core::getHistory() const {
  std::lock_guard lock(mutex_);
  std::vector<HistoryEntry> result;
  for (const auto id : history_.ids()) result.push_back({id});
  return result;
}

Core::HistorySnapshot Core::history_page_locked(std::size_t offset,
                                                std::size_t limit,
                                                std::uint64_t sequence,
                                                std::string reason) const {
  HistorySnapshot snapshot;
  snapshot.revision = history_revision_;
  snapshot.sequence = sequence;
  snapshot.update_reason = std::move(reason);
  const auto ids = history_.ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto index = begin; index < end; ++index) {
    HistoryItem item;
    item.anime_id = ids[index];
    item.anime = anime_cache_.get(item.anime_id);
    if (anime_cache_.cardNeedsRefresh(item.anime_id))
      snapshot.refreshing = true;
    if (const auto save_data = anime_save_data_.find(item.anime_id);
        save_data != anime_save_data_.end())
      item.save_data = save_data->second;
    snapshot.items.push_back(std::move(item));
  }
  return snapshot;
}

std::vector<std::int64_t>
Core::stale_history_ids_locked(std::size_t offset, std::size_t limit) const {
  std::vector<std::int64_t> stale;
  const auto ids = history_.ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto index = begin; index < end; ++index) {
    if (anime_cache_.cardNeedsRefresh(ids[index])) stale.push_back(ids[index]);
  }
  return stale;
}

aniparser::PersistentState Core::persistent_state_locked() const {
  aniparser::PersistentState state;
  state.history = history_.ids();
  state.favorites = favorites_.ids();
  for (std::size_t index = 0; index < anime_states_.size(); ++index)
    state.anime_states[index] = anime_states_[index].ids();
  state.save_data.reserve(anime_save_data_.size());
  for (const auto &[anime_id, data] : anime_save_data_)
    state.save_data.push_back(data);
  return state;
}

Core::HistorySnapshot Core::getHistorySnapshot(std::size_t offset,
                                               std::size_t limit) const {
  limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);
  std::lock_guard lock(mutex_);
  return history_page_locked(offset, limit);
}

std::uint64_t Core::getHistoryRevision() const {
  std::lock_guard lock(mutex_);
  return history_revision_;
}

Core::HistorySubscriptionId Core::subscribeHistory(HistoryCallback callback,
                                                   std::size_t offset,
                                                   std::size_t limit) {
  if (!callback) return 0;
  limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);
  HistorySubscriptionId subscription_id;
  HistorySnapshot snapshot;
  std::vector<std::int64_t> stale_ids;
  {
    std::lock_guard lock(mutex_);
    subscription_id = ++next_history_subscription_id_;
    if (subscription_id == 0) subscription_id = ++next_history_subscription_id_;
    stale_ids = stale_history_ids_locked(offset, limit);
    snapshot = history_page_locked(offset, limit);
    snapshot.refreshing = !stale_ids.empty();
    std::vector<std::int64_t> anime_ids;
    anime_ids.reserve(snapshot.items.size());
    for (const auto &item : snapshot.items) anime_ids.push_back(item.anime_id);
    history_callbacks_.emplace(
        subscription_id,
        HistorySubscription{offset, limit, 0, std::move(anime_ids), callback});
  }
  callback(std::move(snapshot), false);
  refresh_history_metadata(std::move(stale_ids));
  return subscription_id;
}

void Core::notify_history_subscribers(
    std::string reason, const std::vector<std::int64_t> &anime_ids) {
  std::unordered_set<std::int64_t> affected(anime_ids.begin(), anime_ids.end());
  std::vector<std::pair<HistoryCallback, HistorySnapshot>> notifications;
  std::unordered_set<std::int64_t> stale_ids;
  {
    std::lock_guard lock(mutex_);
    for (auto &[subscription_id, subscription] : history_callbacks_) {
      auto snapshot =
          history_page_locked(subscription.offset, subscription.limit,
                              subscription.sequence + 1, reason);
      std::vector<std::int64_t> anime_ids;
      anime_ids.reserve(snapshot.items.size());
      for (const auto &item : snapshot.items)
        anime_ids.push_back(item.anime_id);
      const bool page_changed = anime_ids != subscription.anime_ids;
      const bool contains_affected =
          !affected.empty() &&
          std::any_of(anime_ids.begin(), anime_ids.end(),
                      [&affected](auto id) { return affected.contains(id); });
      if ((!affected.empty() && !contains_affected) ||
          (affected.empty() && !page_changed))
        continue;
      ++subscription.sequence;
      subscription.anime_ids = std::move(anime_ids);
      const auto stale =
          stale_history_ids_locked(subscription.offset, subscription.limit);
      stale_ids.insert(stale.begin(), stale.end());
      notifications.emplace_back(subscription.callback, std::move(snapshot));
    }
  }
  for (auto &[callback, snapshot] : notifications) {
    try {
      callback(std::move(snapshot), true);
    } catch (...) {}
  }
  refresh_history_metadata(
      std::vector<std::int64_t>(stale_ids.begin(), stale_ids.end()));
}

void Core::refresh_history_metadata(std::vector<std::int64_t> anime_ids) {
  std::sort(anime_ids.begin(), anime_ids.end());
  anime_ids.erase(std::unique(anime_ids.begin(), anime_ids.end()),
                  anime_ids.end());
  if (anime_ids.empty()) return;
  for (std::size_t begin = 0; begin < anime_ids.size();
       begin += kHistoryPageLimit) {
    const auto end = std::min(anime_ids.size(), begin + kHistoryPageLimit);
    std::vector<std::int64_t> batch(anime_ids.begin() + begin,
                                    anime_ids.begin() + end);
    yani::SearchInput params;
    params.ids = batch;
    params.limit = static_cast<std::int64_t>(batch.size());
    subscribeSearch(std::move(params),
                    [this, batch = std::move(batch)](
                        yani::SearchInput,
                        std::vector<yani::SearchResult> results, bool updated) {
                      if (!updated) return;
                      std::unordered_set<std::int64_t> returned;
                      for (const auto &result : results)
                        returned.insert(result.anime_id);
                      {
                        std::lock_guard lock(mutex_);
                        for (const auto anime_id : batch)
                          if (!returned.contains(anime_id))
                            anime_cache_.markCardChecked(anime_id);
                      }
                      notify_history_subscribers("metadata", batch);
                    });
  }
}

void Core::cancelHistorySubscription(HistorySubscriptionId subscription_id) {
  if (subscription_id == 0) return;
  std::lock_guard lock(mutex_);
  history_callbacks_.erase(subscription_id);
}

bool Core::addFavorite(std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  {
    std::lock_guard lock(mutex_);
    favorites_.add(anime_id, kHistoryLimit);
    ++favorites_revision_;
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist collections\n";
  notify_favorites_subscribers("favorites");
  return true;
}

bool Core::removeFavorite(std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  {
    std::lock_guard lock(mutex_);
    if (!favorites_.remove(anime_id)) return false;
    ++favorites_revision_;
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist collections\n";
  notify_favorites_subscribers("favorites");
  return true;
}

bool Core::isFavorite(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  return favorites_.contains(anime_id);
}

Core::HistorySnapshot Core::favorites_page_locked(std::size_t offset,
                                                  std::size_t limit,
                                                  std::uint64_t sequence,
                                                  std::string reason) const {
  HistorySnapshot snapshot;
  snapshot.revision = favorites_revision_;
  snapshot.sequence = sequence;
  snapshot.update_reason = std::move(reason);
  const auto ids = favorites_.ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto index = begin; index < end; ++index) {
    HistoryItem item;
    item.anime_id = ids[index];
    item.anime = anime_cache_.get(item.anime_id);
    if (anime_cache_.cardNeedsRefresh(item.anime_id))
      snapshot.refreshing = true;
    snapshot.items.push_back(std::move(item));
  }
  return snapshot;
}

std::vector<std::int64_t>
Core::stale_favorites_ids_locked(std::size_t offset, std::size_t limit) const {
  std::vector<std::int64_t> stale;
  const auto ids = favorites_.ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto index = begin; index < end; ++index)
    if (anime_cache_.cardNeedsRefresh(ids[index])) stale.push_back(ids[index]);
  return stale;
}

Core::HistorySnapshot Core::getFavoritesSnapshot(std::size_t offset,
                                                 std::size_t limit) const {
  limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);
  std::lock_guard lock(mutex_);
  return favorites_page_locked(offset, limit);
}

std::uint64_t Core::getFavoritesRevision() const {
  std::lock_guard lock(mutex_);
  return favorites_revision_;
}

Core::HistorySubscriptionId Core::subscribeFavorites(HistoryCallback callback,
                                                     std::size_t offset,
                                                     std::size_t limit) {
  if (!callback) return 0;
  limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);
  HistorySubscriptionId subscription_id;
  HistorySnapshot snapshot;
  std::vector<std::int64_t> stale_ids;
  {
    std::lock_guard lock(mutex_);
    subscription_id = ++next_favorites_subscription_id_;
    if (subscription_id == 0)
      subscription_id = ++next_favorites_subscription_id_;
    stale_ids = stale_favorites_ids_locked(offset, limit);
    snapshot = favorites_page_locked(offset, limit);
    snapshot.refreshing = !stale_ids.empty();
    std::vector<std::int64_t> anime_ids;
    anime_ids.reserve(snapshot.items.size());
    for (const auto &item : snapshot.items) anime_ids.push_back(item.anime_id);
    favorites_callbacks_.emplace(
        subscription_id,
        HistorySubscription{offset, limit, 0, std::move(anime_ids), callback});
  }
  callback(std::move(snapshot), false);
  refresh_favorites_metadata(std::move(stale_ids));
  return subscription_id;
}

void Core::notify_favorites_subscribers(
    std::string reason, const std::vector<std::int64_t> &anime_ids) {
  std::unordered_set<std::int64_t> affected(anime_ids.begin(), anime_ids.end());
  std::vector<std::pair<HistoryCallback, HistorySnapshot>> notifications;
  std::unordered_set<std::int64_t> stale_ids;
  {
    std::lock_guard lock(mutex_);
    for (auto &[subscription_id, subscription] : favorites_callbacks_) {
      auto snapshot =
          favorites_page_locked(subscription.offset, subscription.limit,
                                subscription.sequence + 1, reason);
      std::vector<std::int64_t> page_ids;
      page_ids.reserve(snapshot.items.size());
      for (const auto &item : snapshot.items) page_ids.push_back(item.anime_id);
      const bool page_changed = page_ids != subscription.anime_ids;
      const bool contains_affected =
          !affected.empty() &&
          std::any_of(page_ids.begin(), page_ids.end(),
                      [&affected](auto id) { return affected.contains(id); });
      if ((!affected.empty() && !contains_affected) ||
          (affected.empty() && !page_changed))
        continue;
      ++subscription.sequence;
      subscription.anime_ids = std::move(page_ids);
      const auto stale =
          stale_favorites_ids_locked(subscription.offset, subscription.limit);
      stale_ids.insert(stale.begin(), stale.end());
      notifications.emplace_back(subscription.callback, std::move(snapshot));
    }
  }
  for (auto &[callback, snapshot] : notifications) {
    try {
      callback(std::move(snapshot), true);
    } catch (...) {}
  }
  refresh_favorites_metadata(
      std::vector<std::int64_t>(stale_ids.begin(), stale_ids.end()));
}

void Core::refresh_favorites_metadata(std::vector<std::int64_t> anime_ids) {
  std::sort(anime_ids.begin(), anime_ids.end());
  anime_ids.erase(std::unique(anime_ids.begin(), anime_ids.end()),
                  anime_ids.end());
  if (anime_ids.empty()) return;
  for (std::size_t begin = 0; begin < anime_ids.size();
       begin += kHistoryPageLimit) {
    const auto end = std::min(anime_ids.size(), begin + kHistoryPageLimit);
    std::vector<std::int64_t> batch(anime_ids.begin() + begin,
                                    anime_ids.begin() + end);
    yani::SearchInput params;
    params.ids = batch;
    params.limit = static_cast<std::int64_t>(batch.size());
    subscribeSearch(std::move(params),
                    [this, batch = std::move(batch)](
                        yani::SearchInput,
                        std::vector<yani::SearchResult> results, bool updated) {
                      if (!updated) return;
                      std::unordered_set<std::int64_t> returned;
                      for (const auto &result : results)
                        returned.insert(result.anime_id);
                      {
                        std::lock_guard lock(mutex_);
                        for (const auto anime_id : batch)
                          if (!returned.contains(anime_id))
                            anime_cache_.markCardChecked(anime_id);
                      }
                      notify_favorites_subscribers("metadata", batch);
                    });
  }
}

void Core::cancelFavoritesSubscription(HistorySubscriptionId subscription_id) {
  if (subscription_id == 0) return;
  std::lock_guard lock(mutex_);
  favorites_callbacks_.erase(subscription_id);
}

bool Core::add_anime_status(AnimeStatus status, std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  const auto destination = static_cast<std::size_t>(status);
  if (destination >= anime_states_.size()) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  std::vector<AnimeStatus> changed{status};
  {
    std::lock_guard lock(mutex_);
    anime_states_[destination].add(anime_id, kHistoryLimit);
    ++anime_status_revisions_[destination];
    for (std::size_t index = 0; index < anime_states_.size(); ++index) {
      if (index == destination || !anime_states_[index].remove(anime_id))
        continue;
      ++anime_status_revisions_[index];
      changed.push_back(static_cast<AnimeStatus>(index));
    }
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist anime states\n";
  for (const auto changed_status : changed)
    notify_anime_status_subscribers(changed_status, "state");
  return true;
}

bool Core::remove_anime_status(AnimeStatus status, std::int64_t anime_id) {
  if (anime_id <= 0) return false;
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size()) return false;
  std::unique_lock save_lock(collection_save_mutex_);
  {
    std::lock_guard lock(mutex_);
    if (!anime_states_[index].remove(anime_id)) return false;
    ++anime_status_revisions_[index];
  }
  aniparser::PersistentState state;
  {
    std::lock_guard lock(mutex_);
    state = persistent_state_locked();
  }
  if (!save_manager_.saveState(state))
    std::cerr << "[core] failed to persist anime states\n";
  notify_anime_status_subscribers(status, "state");
  return true;
}

bool Core::is_anime_status(AnimeStatus status, std::int64_t anime_id) const {
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size()) return false;
  std::lock_guard lock(mutex_);
  return anime_states_[index].contains(anime_id);
}

int Core::getStatus(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  if (anime_states_[static_cast<std::size_t>(AnimeStatus::Planned)].contains(
          anime_id))
    return 1;
  if (anime_states_[static_cast<std::size_t>(AnimeStatus::Watched)].contains(
          anime_id))
    return 2;
  if (anime_states_[static_cast<std::size_t>(AnimeStatus::Completed)].contains(
          anime_id))
    return 3;
  if (anime_states_[static_cast<std::size_t>(AnimeStatus::Dropped)].contains(
          anime_id))
    return 5;
  return 0;
}

Core::HistorySnapshot Core::anime_status_page_locked(AnimeStatus status,
                                                     std::size_t offset,
                                                     std::size_t limit,
                                                     std::uint64_t sequence,
                                                     std::string reason) const {
  HistorySnapshot snapshot;
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size()) return snapshot;
  snapshot.revision = anime_status_revisions_[index];
  snapshot.sequence = sequence;
  snapshot.update_reason = std::move(reason);
  const auto ids = anime_states_[index].ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto pos = begin; pos < end; ++pos) {
    HistoryItem item;
    item.anime_id = ids[pos];
    item.anime = anime_cache_.get(item.anime_id);
    if (anime_cache_.cardNeedsRefresh(item.anime_id))
      snapshot.refreshing = true;
    snapshot.items.push_back(std::move(item));
  }
  return snapshot;
}

std::vector<std::int64_t> Core::stale_anime_status_ids_locked(
    AnimeStatus status, std::size_t offset, std::size_t limit) const {
  std::vector<std::int64_t> stale;
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size()) return stale;
  const auto ids = anime_states_[index].ids();
  const auto begin = std::min(offset, ids.size());
  const auto end = begin + std::min(ids.size() - begin, limit);
  for (auto pos = begin; pos < end; ++pos)
    if (anime_cache_.cardNeedsRefresh(ids[pos])) stale.push_back(ids[pos]);
  return stale;
}

Core::HistorySubscriptionId
Core::subscribe_anime_status(AnimeStatus status,
                             HistoryCallback callback,
                             std::size_t offset,
                             std::size_t limit) {
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size() || !callback) return 0;
  limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);
  HistorySubscriptionId id;
  HistorySnapshot snapshot;
  std::vector<std::int64_t> stale;
  {
    std::lock_guard lock(mutex_);
    id = ++next_anime_status_subscription_id_[index];
    if (id == 0) id = ++next_anime_status_subscription_id_[index];
    stale = stale_anime_status_ids_locked(status, offset, limit);
    snapshot = anime_status_page_locked(status, offset, limit);
    snapshot.refreshing = !stale.empty();
    std::vector<std::int64_t> ids;
    for (const auto &item : snapshot.items) ids.push_back(item.anime_id);
    anime_status_callbacks_[index].emplace(
        id, HistorySubscription{offset, limit, 0, std::move(ids), callback});
  }
  callback(std::move(snapshot), false);
  refresh_anime_status_metadata(status, std::move(stale));
  return id;
}

void Core::notify_anime_status_subscribers(
    AnimeStatus status,
    std::string reason,
    const std::vector<std::int64_t> &anime_ids) {
  const auto index = static_cast<std::size_t>(status);
  if (index >= anime_states_.size()) return;
  std::unordered_set<std::int64_t> affected(anime_ids.begin(), anime_ids.end());
  std::vector<std::pair<HistoryCallback, HistorySnapshot>> notifications;
  std::unordered_set<std::int64_t> stale;
  {
    std::lock_guard lock(mutex_);
    for (auto &[id, subscription] : anime_status_callbacks_[index]) {
      auto snapshot = anime_status_page_locked(
          status, subscription.offset, subscription.limit,
          subscription.sequence + 1, reason);
      std::vector<std::int64_t> page_ids;
      for (const auto &item : snapshot.items) page_ids.push_back(item.anime_id);
      const bool changed = page_ids != subscription.anime_ids;
      const bool affected_page =
          !affected.empty() && std::any_of(page_ids.begin(), page_ids.end(),
                                           [&affected](auto value) {
                                             return affected.contains(value);
                                           });
      if ((!affected.empty() && !affected_page) ||
          (affected.empty() && !changed))
        continue;
      ++subscription.sequence;
      subscription.anime_ids = std::move(page_ids);
      const auto page_stale = stale_anime_status_ids_locked(
          status, subscription.offset, subscription.limit);
      stale.insert(page_stale.begin(), page_stale.end());
      notifications.emplace_back(subscription.callback, std::move(snapshot));
    }
  }
  for (auto &[callback, snapshot] : notifications) {
    try {
      callback(std::move(snapshot), true);
    } catch (...) {}
  }
  refresh_anime_status_metadata(
      status, std::vector<std::int64_t>(stale.begin(), stale.end()));
}

void Core::refresh_anime_status_metadata(AnimeStatus status,
                                         std::vector<std::int64_t> ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  for (std::size_t begin = 0; begin < ids.size(); begin += kHistoryPageLimit) {
    const auto end = std::min(ids.size(), begin + kHistoryPageLimit);
    std::vector<std::int64_t> batch(ids.begin() + begin, ids.begin() + end);
    yani::SearchInput params;
    params.ids = batch;
    params.limit = static_cast<std::int64_t>(batch.size());
    subscribeSearch(std::move(params),
                    [this, status, batch = std::move(batch)](
                        yani::SearchInput,
                        std::vector<yani::SearchResult> results, bool updated) {
                      if (!updated) return;
                      std::unordered_set<std::int64_t> returned;
                      for (const auto &result : results)
                        returned.insert(result.anime_id);
                      {
                        std::lock_guard lock(mutex_);
                        for (const auto anime_id : batch)
                          if (!returned.contains(anime_id))
                            anime_cache_.markCardChecked(anime_id);
                      }
                      notify_anime_status_subscribers(status, "metadata",
                                                      batch);
                    });
  }
}

void Core::cancel_anime_status_subscription(AnimeStatus status,
                                            HistorySubscriptionId id) {
  const auto index = static_cast<std::size_t>(status);
  if (id == 0 || index >= anime_states_.size()) return;
  std::lock_guard lock(mutex_);
  anime_status_callbacks_[index].erase(id);
}

#define DEFINE_ANIME_STATUS_METHODS(Name, Value)                               \
  bool Core::add##Name(std::int64_t id) {                                      \
    return add_anime_status(AnimeStatus::Value, id);                           \
  }                                                                            \
  bool Core::remove##Name(std::int64_t id) {                                   \
    return remove_anime_status(AnimeStatus::Value, id);                        \
  }                                                                            \
  bool Core::is##Name(std::int64_t id) const {                                 \
    return is_anime_status(AnimeStatus::Value, id);                            \
  }                                                                            \
  Core::HistorySnapshot Core::get##Name##Snapshot(std::size_t offset,          \
                                                  std::size_t limit) const {   \
    limit = std::clamp<std::size_t>(limit, 1, kHistoryPageLimit);              \
    std::lock_guard lock(mutex_);                                              \
    return anime_status_page_locked(AnimeStatus::Value, offset, limit);        \
  }                                                                            \
  std::uint64_t Core::get##Name##Revision() const {                            \
    std::lock_guard lock(mutex_);                                              \
    return anime_status_revisions_[static_cast<std::size_t>(                   \
        AnimeStatus::Value)];                                                  \
  }                                                                            \
  Core::HistorySubscriptionId Core::subscribe##Name(                           \
      HistoryCallback callback, std::size_t offset, std::size_t limit) {       \
    return subscribe_anime_status(AnimeStatus::Value, std::move(callback),     \
                                  offset, limit);                              \
  }                                                                            \
  void Core::cancel##Name##Subscription(HistorySubscriptionId id) {            \
    cancel_anime_status_subscription(AnimeStatus::Value, id);                  \
  }

DEFINE_ANIME_STATUS_METHODS(Planned, Planned)
DEFINE_ANIME_STATUS_METHODS(Watched, Watched)
DEFINE_ANIME_STATUS_METHODS(Completed, Completed)
DEFINE_ANIME_STATUS_METHODS(Dropped, Dropped)
#undef DEFINE_ANIME_STATUS_METHODS
