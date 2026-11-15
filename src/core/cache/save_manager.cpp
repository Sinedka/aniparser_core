#include "save_manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
template <typename Call> class ScopeExit {
  public:
  explicit ScopeExit(Call call) : call_(std::move(call)) {}
  ScopeExit(const ScopeExit &) = delete;
  ScopeExit &operator=(const ScopeExit &) = delete;
  ~ScopeExit() { call_(); }

  private:
  Call call_;
};

std::int64_t stored_integer(const nlohmann::json &value) {
  if (!value.is_number_integer() ||
      (value.is_number_unsigned() &&
       value.get<std::uint64_t>() >
           static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max())))
    throw std::runtime_error("Invalid integer in storage");
  return value.get<std::int64_t>();
}

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

yani::Result<aniparser::PersistentState> SaveManager::loadState() const {
  const auto fail =
      [this](std::string message) -> yani::Result<aniparser::PersistentState> {
    load_error_ = yani::ApiError{.message = std::move(message)};
    return std::unexpected(*load_error_);
  };
  load_error_.reset();
  if (data_directory_.empty()) return fail("No data directory configured");
  const auto file = data_directory_ / "history.json";
  std::ifstream input(file);
  if (!input) {
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (!error && !exists) return aniparser::PersistentState{};
    return fail("Cannot read storage: " + file.string());
  }

  try {
    const auto document = nlohmann::json::parse(input);
    if (!document.is_object() || !document.contains("version") ||
        !document["version"].is_number_integer() ||
        (document["version"].is_number_unsigned() &&
         document["version"].get<std::uint64_t>() > 4))
      return fail("Invalid storage version");
    const auto version = document["version"].get<std::int64_t>();
    if (!document.is_object() || (version < 1 || version > 4) ||
        !document.contains("history") || !document["history"].is_array()) {
      std::cerr << "[save manager] unsupported history file format: " << file
                << '\n';
      return fail("Unsupported or invalid storage format: " + file.string());
    }
    for (const auto* key : {"favorites", "save_data"})
      if (document.contains(key) && !document[key].is_array())
        return fail(std::string("Invalid storage array: ") + key);
    if (document.contains("anime_states") &&
        !document["anime_states"].is_object())
      return fail("Invalid anime_states storage object");

    aniparser::PersistentState state;
    std::unordered_set<std::int64_t> seen;
    for (const auto &item : document["history"]) {
      try {
        const auto anime_id = item.is_object()
                                  ? stored_integer(item.at("anime_id"))
                                  : stored_integer(item);
        if (anime_id <= 0) return fail("Invalid history ID");
        if (!seen.insert(anime_id).second) continue;
        state.history.push_back(anime_id);
        if (version == 1 && item.is_object()) {
          aniparser::AnimeSaveData saved{
              .anime_id = anime_id,
              .player = item.value("player", std::string{}),
              .dubber = item.value("dubber", std::string{}),
              .episode = {},
              .time = (item.contains("time") ? stored_integer(item.at("time"))
                                             : 0)};
          if (!saved.player.empty() && !saved.dubber.empty() && saved.time >= 0)
            state.save_data.push_back(std::move(saved));
        }
      } catch (const std::exception &error) {
        return fail(std::string("Invalid history entry: ") + error.what());
      }
    }
    if (version >= 3 && document.contains("favorites") &&
        document["favorites"].is_array()) {
      std::unordered_set<std::int64_t> favorite_ids;
      for (const auto &item : document["favorites"]) {
        try {
          const auto anime_id = stored_integer(item);
          if (anime_id <= 0) return fail("Invalid favorite ID");
          if (favorite_ids.insert(anime_id).second)
            state.favorites.push_back(anime_id);
        } catch (const std::exception &error) {
          return fail(std::string("Invalid favorite entry: ") + error.what());
        }
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
        if (state_it == document["anime_states"].end()) continue;
        if (!state_it->is_array()) return fail("Invalid anime state array");
        std::unordered_set<std::int64_t> state_ids;
        for (const auto &item : *state_it) {
          try {
            const auto anime_id = stored_integer(item);
            if (anime_id <= 0) return fail("Invalid anime state ID");
            if (state_ids.insert(anime_id).second)
              state.anime_states[state_index].push_back(anime_id);
          } catch (const std::exception &error) {
            return fail(std::string("Invalid anime state ID: ") + error.what());
          }
        }
      }
    }
    if (version >= 2 && document.contains("save_data") &&
        document["save_data"].is_array()) {
      std::unordered_set<std::int64_t> saved_ids;
      for (const auto &item : document["save_data"]) {
        if (!item.is_object()) return fail("Invalid progress entry");
        try {
          aniparser::AnimeSaveData saved{
              .anime_id = stored_integer(item.at("anime_id")),
              .player = item.at("player").get<std::string>(),
              .dubber = item.at("dubber").get<std::string>(),
              .episode = item.value("episode", std::string{}),
              .time = stored_integer(item.at("time"))};
          if (saved.anime_id <= 0 || saved.time < 0 || saved.player.empty() ||
              saved.dubber.empty())
            return fail("Invalid progress values");
          if (!saved_ids.insert(saved.anime_id).second) continue;
          state.save_data.push_back(std::move(saved));
        } catch (const std::exception &error) {
          return fail(std::string("Invalid progress entry: ") + error.what());
        }
      }
    }
    if (input.bad()) return fail("I/O error while reading storage");
    return state;
  } catch (const std::exception &error) {
    std::cerr << "[save manager] failed to read " << file << ": "
              << error.what() << '\n';
    return fail(std::string("Cannot decode storage: ") + error.what());
  }
}

yani::Result<void>
SaveManager::saveState(const aniparser::PersistentState &state) const {
  if (load_error_) return std::unexpected(*load_error_);
  const auto fail = [](std::string message,
                       bool committed = false) -> yani::Result<void> {
    return std::unexpected(yani::ApiError{.message = std::move(message),
                                          .data = {{"committed", committed}}});
  };
  if (data_directory_.empty()) return fail("No data directory configured");
  std::error_code error;
  std::filesystem::create_directories(data_directory_, error);
  if (error) return fail("Cannot create storage directory: " + error.message());

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
  const auto bytes = document.dump(2) + '\n';
#if defined(_WIN32)
  auto temporary = destination;
  temporary += L".tmp." + std::to_wstring(::GetCurrentProcessId()) + L"." +
               std::to_wstring(::GetTickCount64());
  auto handle = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return fail("Cannot create temporary storage file");
  bool temporary_exists = true;
  const ScopeExit cleanup([&] {
    if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    if (temporary_exists) ::DeleteFileW(temporary.c_str());
  });
  bool written = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD count = 0;
    const auto size = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
    if (!::WriteFile(handle, bytes.data() + offset, size, &count, nullptr) ||
        count == 0) {
      written = false;
      break;
    }
    offset += count;
  }
  written = written && ::FlushFileBuffers(handle);
  const bool closed =
      ::CloseHandle(std::exchange(handle, INVALID_HANDLE_VALUE));
  if (!written || !closed) {
    return fail("Cannot write or flush storage file");
  }
  if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return fail("Cannot replace storage file");
  }
  temporary_exists = false;
#else
  // Exclusive creation avoids following an existing .tmp symlink.
  auto pattern = destination.string() + ".tmp.XXXXXX";
  int fd = ::mkstemp(pattern.data());
  if (fd < 0)
    return fail("Cannot create temporary storage file: " +
                std::string(std::strerror(errno)));
  bool temporary_exists = true;
  const ScopeExit cleanup([&] {
    if (fd >= 0) ::close(fd);
    if (temporary_exists) ::unlink(pattern.c_str());
  });
  const std::filesystem::path temporary(pattern);
  bool written = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count =
        ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      written = false;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  if (written) {
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    written = result == 0;
  }
  const bool closed = ::close(std::exchange(fd, -1)) == 0;
  if (!written || !closed) { return fail("Cannot write or sync storage file"); }
  std::filesystem::rename(temporary, destination, error);
  if (error) { return fail("Cannot replace storage file: " + error.message()); }
  temporary_exists = false;
  const int directory =
      ::open(data_directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0)
    return fail("Storage replaced, but cannot open directory for sync", true);
  const bool synced = ::fsync(directory) == 0;
  const bool directory_closed = ::close(directory) == 0;
  if (!synced || !directory_closed)
    return fail(
        "Storage replaced, but directory sync failed; durability uncertain",
        true);
#endif
  return {};
}
