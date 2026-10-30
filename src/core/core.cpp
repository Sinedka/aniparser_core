#include "core.hpp"

#include <algorithm>
#include <iostream>

Core::Core() {
  auto saved_state = save_manager_.loadState();
  bool normalize_state =
      saved_state.history.size() > kHistoryLimit ||
      saved_state.favorites.size() > kHistoryLimit ||
      std::any_of(saved_state.anime_states.begin(),
                  saved_state.anime_states.end(),
                  [](const auto &ids) { return ids.size() > kHistoryLimit; });
  history_.load(saved_state.history, kHistoryLimit);
  favorites_.load(saved_state.favorites, kHistoryLimit);
  std::unordered_set<std::int64_t> state_ids;
  for (std::size_t index = 0; index < anime_states_.size(); ++index) {
    auto &ids = saved_state.anime_states[index];
    const auto original = ids;
    std::vector<std::int64_t> valid_ids;
    valid_ids.reserve(std::min(ids.size(), kHistoryLimit));
    for (const auto id : ids) {
      if (id <= 0 || valid_ids.size() >= kHistoryLimit ||
          !state_ids.insert(id).second)
        continue;
      valid_ids.push_back(id);
    }
    normalize_state = normalize_state || valid_ids != original;
    ids = std::move(valid_ids);
    anime_states_[index].load(ids, kHistoryLimit);
  }
  for (auto &data : saved_state.save_data)
    if (data.anime_id > 0 && !data.player.empty() && !data.dubber.empty() &&
        data.time >= 0)
      anime_save_data_.insert_or_assign(data.anime_id, std::move(data));
  std::cerr << "[core] created instance=" << this
            << " history=" << history_.size()
            << " favorites=" << favorites_.size()
            << " states=" << state_ids.size() << '\n';
  if (normalize_state) {
    if (!save_manager_.saveState(persistent_state_locked()))
      std::cerr << "[core] failed to persist trimmed collections\n";
  }
}
Core::~Core() {
  std::cerr << "[core] destroying instance=" << this << '\n';
  tasks_.shutdown();
}
