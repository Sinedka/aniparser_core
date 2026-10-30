#pragma once

#include <filesystem>

#include "history.hpp"

class SaveManager {
  public:
  SaveManager();

  aniparser::PersistentState loadState() const;
  bool saveState(const aniparser::PersistentState &state) const;
  const std::filesystem::path &dataDirectory() const { return data_directory_; }

  private:
  static std::filesystem::path resolveDataDirectory();
  std::filesystem::path data_directory_;
};
