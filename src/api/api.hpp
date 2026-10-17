#pragma once
#include <types.hpp>

#include "models.hpp"
#include <cpr/cpr.h>
#include <expected>
#include <future>
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

// Asynchronous variants return immediately; call future.get() to obtain the
// same Result type as the corresponding synchronous function.
std::future<Result<nlohmann::json>> getAsync(
    std::string path, cpr::Parameters params = {});
std::future<Result<AnimeResult>> getAnimeAsync(std::string alias_or_id,
                                               bool need_videos = false);
std::future<Result<std::vector<SearchResult>>>
searchTitlesAsync(SearchInput input);
std::future<Result<FeedResponse>>
getFeedAsync(std::vector<std::string> watches = {});

} // namespace yani
