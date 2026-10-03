#pragma once
#include <types.hpp>

#include "models.hpp"
#include <cpr/cpr.h>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace yani {

struct ApiError {
  int status = 0;
  std::string message;
  nlohmann::json data;
};

const std::string BASE_URL = "https://api.yani.tv/";

template <typename T> using Result = std::expected<T, ApiError>;

Result<AnimeResult> getAnime(std::string_view alias_or_id,
                             bool need_videos = false);

Result<std::vector<SearchResult>> searchTitles(const SearchInput &input);

Result<FeedResponse> getFeed(const std::vector<std::string> &watches = {});

Result<nlohmann::json> get(std::string_view path,
                           const cpr::Parameters &params = {});

} // namespace yani
