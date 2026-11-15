#pragma once

#include <api.hpp>

#include <filesystem>

#include "history.hpp"

class SaveManager {
  public:
  SaveManager();

  yani::Result<aniparser::PersistentState> loadState() const;
  yani::Result<void> saveState(const aniparser::PersistentState &state) const;
  const std::filesystem::path &dataDirectory() const { return data_directory_; }

  private:
  static std::filesystem::path resolveDataDirectory();
  std::filesystem::path data_directory_;
  mutable std::optional<yani::ApiError> load_error_;
};
