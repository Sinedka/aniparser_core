#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aniparser {
struct AnimeSaveData {
  std::int64_t anime_id = 0;
  std::string player;
  std::string dubber;
  std::string episode;
  std::int64_t time = 0;
};

struct PersistentState {
  std::vector<std::int64_t> history;
  std::vector<std::int64_t> favorites;
  std::array<std::vector<std::int64_t>, 4> anime_states;
  std::vector<AnimeSaveData> save_data;
};
} // namespace aniparser
