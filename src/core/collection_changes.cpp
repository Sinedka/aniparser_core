#include <algorithm>
#include <exception>
#include <iostream>

#include "core.hpp"

yani::Result<bool>
Core::change_state(std::function<bool(aniparser::PersistentState &)> change) {
  std::unique_lock save_lock(collection_save_mutex_);
  aniparser::PersistentState previous;
  {
    std::lock_guard lock(mutex_);
    previous = persistent_state_locked();
  }
  auto candidate = previous;
  try {
    if (!change(candidate)) return false;
  } catch (const std::exception &error) {
    return std::unexpected(yani::ApiError{.message = error.what()});
  }

  // Allocate the entire replacement before touching the file or live state.
  HistoryCollection history;
  FavoritesCollection favorites;
  std::array<AnimeStatusCollection, 4> states;
  std::unordered_map<std::int64_t, AnimeSaveData> progress;
  history.load(candidate.history, kHistoryLimit);
  favorites.load(candidate.favorites, kHistoryLimit);
  for (std::size_t i = 0; i < states.size(); ++i)
    states[i].load(candidate.anime_states[i], kHistoryLimit);
  for (const auto &entry : candidate.save_data)
    progress.emplace(entry.anime_id, entry);
  std::vector<std::int64_t> progress_changed;
  {
    std::lock_guard lock(mutex_);
    for (const auto id : candidate.history) {
      const auto before = anime_save_data_.find(id);
      const auto after = progress.find(id);
      if ((before == anime_save_data_.end()) != (after == progress.end()) ||
          (before != anime_save_data_.end() && after != progress.end() &&
           before->second != after->second))
        progress_changed.push_back(id);
    }
  }
  auto saved = save_manager_.saveState(candidate);
  // A directory sync error occurs after rename. Publish the committed file's
  // state, but tell the caller durability is uncertain rather than acknowledge.
  if (!saved && !(saved.error().data.is_object() &&
                  saved.error().data.value("committed", false)))
    return std::unexpected(saved.error());
  {
    std::lock_guard lock(mutex_);
    history_.swap(history);
    favorites_.swap(favorites);
    for (std::size_t i = 0; i < states.size(); ++i) {
      anime_states_[i].swap(states[i]);
      if (previous.anime_states[i] != candidate.anime_states[i])
        ++anime_status_revisions_[i];
    }
    anime_save_data_.swap(progress);
    if (previous.history != candidate.history || !progress_changed.empty())
      ++history_revision_;
    if (previous.favorites != candidate.favorites) ++favorites_revision_;
  }
  // Persistence has committed. A notification preparation failure must not
  // report a rolled-back mutation or undo durable state.
  try {
    if (previous.history != candidate.history)
      notify_history_subscribers("history");
    else if (!progress_changed.empty())
      notify_history_subscribers("save_data", progress_changed);
    if (previous.favorites != candidate.favorites)
      notify_favorites_subscribers("favorites");
    for (std::size_t i = 0; i < states.size(); ++i)
      if (previous.anime_states[i] != candidate.anime_states[i])
        notify_anime_status_subscribers(static_cast<AnimeStatus>(i), "state");
  } catch (const std::exception &error) {
    std::cerr << "[core] committed mutation; notification preparation failed: "
              << error.what() << '\n';
  } catch (...) {
    std::cerr << "[core] committed mutation; notification preparation failed\n";
  }
  save_lock.unlock();
  drain_notifications();
  if (!saved) return std::unexpected(saved.error());
  return true;
}

void Core::drain_notifications() {
  std::unique_lock lock(mutex_);
  if (dispatching_notifications_) return;
  dispatching_notifications_ = true;
  while (!notifications_.empty()) {
    auto action = std::move(notifications_.front());
    notifications_.pop_front();
    lock.unlock();
    try {
      action();
    } catch (...) {}
    lock.lock();
  }
  dispatching_notifications_ = false;
}
