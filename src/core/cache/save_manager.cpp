#include "save_manager.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::filesystem::path environment_path(const char* name) {
  const auto* value = std::getenv(name);
  return value && *value ? std::filesystem::path(value)
                         : std::filesystem::path{};
}
} // namespace

SaveManager::SaveManager() : data_directory_(resolveDataDirectory()) {
  if (data_directory_.empty())
    std::cerr
        << "[save manager] no writable data directory; collections are not "
           "persistent\n";
}

std::filesystem::path SaveManager::resolveDataDirectory() {
  if (auto override = environment_path("ANIPARSER_DATA_DIR"); !override.empty())
    return override;

#if defined(_WIN32)
  if (auto path = environment_path("APPDATA"); !path.empty())
    return path / "AniParser";
  if (auto path = environment_path("LOCALAPPDATA"); !path.empty())
    return path / "AniParser";
  return {};
#elif defined(__APPLE__)
  if (auto home = environment_path("HOME"); !home.empty())
    return home / "Library" / "Application Support" / "AniParser";
  return {};
#elif defined(__ANDROID__)
  // Android apps should set ANIPARSER_DATA_DIR to their private files dir.
  // HOME is a useful fallback for command-line and test environments.
  if (auto home = environment_path("HOME"); !home.empty())
    return home / ".local" / "share" / "aniparser";
  return {};
#else
  if (auto data_home = environment_path("XDG_DATA_HOME");
      !data_home.empty() && data_home.is_absolute())
    return data_home / "aniparser";
  if (auto home = environment_path("HOME"); !home.empty())
    return home / ".local" / "share" / "aniparser";
  return {};
#endif
}

aniparser::PersistentState SaveManager::loadState() const {
  if (data_directory_.empty()) return {};
  const auto file = data_directory_ / "history.json";
  std::ifstream input(file);
  if (!input) return {};

  try {
    const auto document = nlohmann::json::parse(input);
    const auto version = document.value("version", 0);
    if (!document.is_object() || (version < 1 || version > 4) ||
        !document.contains("history") || !document["history"].is_array()) {
      std::cerr << "[save manager] unsupported history file format: " << file
                << '\n';
      return {};
    }

    aniparser::PersistentState state;
    std::unordered_set<std::int64_t> seen;
    for (const auto &item : document["history"]) {
      try {
        const auto anime_id = item.is_object()
                                  ? item.at("anime_id").get<std::int64_t>()
                                  : item.get<std::int64_t>();
        if (anime_id <= 0 || !seen.insert(anime_id).second) continue;
        state.history.push_back(anime_id);
        if (version == 1) {
          aniparser::AnimeSaveData saved{
              .anime_id = anime_id,
              .player = item.value("player", std::string{}),
              .dubber = item.value("dubber", std::string{}),
              .episode = {},
              .time = item.value("time", std::int64_t{0})};
          if (!saved.player.empty() && !saved.dubber.empty() && saved.time >= 0)
            state.save_data.push_back(std::move(saved));
        }
      } catch (const nlohmann::json::exception &) {
        // Ignore malformed entries while preserving the rest of the history.
      }
    }
    if (version >= 3 && document.contains("favorites") &&
        document["favorites"].is_array()) {
      std::unordered_set<std::int64_t> favorite_ids;
      for (const auto &item : document["favorites"]) {
        try {
          const auto anime_id = item.get<std::int64_t>();
          if (anime_id > 0 && favorite_ids.insert(anime_id).second)
            state.favorites.push_back(anime_id);
        } catch (const nlohmann::json::exception &) {}
      }
    }
    if (version >= 4 && document.contains("anime_states") &&
        document["anime_states"].is_object()) {
      static constexpr std::array<const char*, 4> state_names{
          "planned", "watched", "completed", "dropped"};
      for (std::size_t state_index = 0; state_index < state_names.size();
           ++state_index) {
        const auto state_it =
            document["anime_states"].find(state_names[state_index]);
        if (state_it == document["anime_states"].end() || !state_it->is_array())
          continue;
        std::unordered_set<std::int64_t> state_ids;
        for (const auto &item : *state_it) {
          try {
            const auto anime_id = item.get<std::int64_t>();
            if (anime_id > 0 && state_ids.insert(anime_id).second)
              state.anime_states[state_index].push_back(anime_id);
          } catch (const nlohmann::json::exception &) {}
        }
      }
    }
    if (version >= 2 && document.contains("save_data") &&
        document["save_data"].is_array()) {
      std::unordered_set<std::int64_t> saved_ids;
      for (const auto &item : document["save_data"]) {
        if (!item.is_object()) continue;
        try {
          aniparser::AnimeSaveData saved{
              .anime_id = item.at("anime_id").get<std::int64_t>(),
              .player = item.at("player").get<std::string>(),
              .dubber = item.at("dubber").get<std::string>(),
              .episode = item.value("episode", std::string{}),
              .time = item.at("time").get<std::int64_t>()};
          if (saved.anime_id <= 0 || saved.time < 0 || saved.player.empty() ||
              saved.dubber.empty() || !saved_ids.insert(saved.anime_id).second)
            continue;
          state.save_data.push_back(std::move(saved));
        } catch (const nlohmann::json::exception &) {}
      }
    }
    return state;
  } catch (const std::exception &error) {
    std::cerr << "[save manager] failed to read " << file << ": "
              << error.what() << '\n';
    return {};
  }
}

bool SaveManager::saveState(const aniparser::PersistentState &state) const {
  if (data_directory_.empty()) return false;

  std::error_code error;
  std::filesystem::create_directories(data_directory_, error);
  if (error) {
    std::cerr << "[save manager] cannot create " << data_directory_ << ": "
              << error.message() << '\n';
    return false;
  }
#if !defined(_WIN32)
  std::filesystem::permissions(data_directory_,
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
#endif

  nlohmann::json items = nlohmann::json::array();
  for (const auto anime_id : state.history) items.push_back(anime_id);
  nlohmann::json favorites = nlohmann::json::array();
  for (const auto anime_id : state.favorites) favorites.push_back(anime_id);
  static constexpr std::array<const char*, 4> state_names{
      "planned", "watched", "completed", "dropped"};
  nlohmann::json anime_states = nlohmann::json::object();
  for (std::size_t state_index = 0; state_index < state_names.size();
       ++state_index) {
    auto &items = anime_states[state_names[state_index]] =
        nlohmann::json::array();
    for (const auto anime_id : state.anime_states[state_index])
      items.push_back(anime_id);
  }
  nlohmann::json save_data = nlohmann::json::array();
  for (const auto &entry : state.save_data)
    save_data.push_back({{"anime_id", entry.anime_id},
                         {"player", entry.player},
                         {"dubber", entry.dubber},
                         {"episode", entry.episode},
                         {"time", entry.time}});
  const nlohmann::json document{{"version", 4},
                                {"history", std::move(items)},
                                {"favorites", std::move(favorites)},
                                {"anime_states", std::move(anime_states)},
                                {"save_data", std::move(save_data)}};
  const auto destination = data_directory_ / "history.json";
  auto temporary = destination;
  temporary += ".tmp";

  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      std::cerr << "[save manager] cannot open " << temporary
                << " for writing\n";
      return false;
    }
    output << document.dump(2) << '\n';
    output.flush();
    if (!output) {
      std::cerr << "[save manager] failed to write " << temporary << '\n';
      output.close();
      std::filesystem::remove(temporary, error);
      return false;
    }
  }

#if defined(_WIN32)
  if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::cerr << "[save manager] cannot replace " << destination
              << " (Windows error " << ::GetLastError() << ")\n";
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  std::filesystem::permissions(temporary,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
  error.clear();
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::cerr << "[save manager] cannot replace " << destination << ": "
              << error.message() << '\n';
    std::filesystem::remove(temporary, error);
    return false;
  }
  std::filesystem::permissions(destination,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
#endif
  return true;
}
