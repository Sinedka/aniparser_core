#include "api.hpp"
// IWYU pragma: begin_exports
#include "json.hpp"
// IWYU pragma: end_exports

#include <memory>
#include <type_traits>

namespace yani {

namespace {

std::string join(const std::vector<std::string> &values) {
  std::string result;

  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) result += ',';

    result += values[i];
  }

  return result;
}

template <typename T> std::string queryValue(const T &value) {
  if constexpr (std::is_same_v<T, bool>) {

    return value ? "true" : "false";

  } else if constexpr (std::is_arithmetic_v<T>) {

    return std::to_string(value);

  } else if constexpr (std::is_same_v<T, std::string>) {

    return value;

  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {

    return join(value);

  } else if constexpr (std::is_same_v<T, std::vector<std::int64_t>>) {

    std::vector<std::string> values;

    values.reserve(value.size());

    for (const auto item : value) values.push_back(std::to_string(item));

    return join(values);

  } else {

    static_assert(std::is_same_v<T, void>, "Unsupported query parameter type");
  }
}

template <typename T>
void addParameter(cpr::Parameters &params,
                  std::string_view name,
                  const std::optional<T> &value) {
  if (!value) return;

  params.Add({std::string(name), queryValue(*value)});
}

Result<nlohmann::json> parse_response(const cpr::Response &response) {
  if (response.error.code != cpr::ErrorCode::OK) {
    return std::unexpected(
        ApiError{.status = 0, .message = response.error.message, .data = {}});
  }

  nlohmann::json data;
  try {
    data = nlohmann::json::parse(response.text);
  } catch (const std::exception &e) {
    return std::unexpected(
        ApiError{.status = static_cast<int>(response.status_code),
                 .message = e.what(),
                 .data = {}});
  }

  if (response.status_code < 200 || response.status_code >= 300) {
    std::string message = "HTTP error";
    if (data.contains("error")) {
      message = data["error"].is_string() ? data["error"].get<std::string>()
                                          : data["error"].dump();
    }
    return std::unexpected(
        ApiError{.status = static_cast<int>(response.status_code),
                 .message = std::move(message),
                 .data = std::move(data)});
  }
  return data;
}

template <typename T, typename Convert>
std::future<Result<T>>
request_async(std::string path, cpr::Parameters params, Convert convert) {
  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto future = promise->get_future();
  try {
    [[maybe_unused]] auto request = cpr::GetCallback(
        [promise, convert = std::move(convert)](cpr::Response response) {
          auto raw = parse_response(response);
          if (!raw) {
            promise->set_value(std::unexpected(raw.error()));
            return;
          }
          try {
            promise->set_value(convert(*raw));
          } catch (const std::exception &e) {
            promise->set_value(std::unexpected(
                ApiError{.status = static_cast<int>(response.status_code),
                         .message = e.what(),
                         .data = std::move(*raw)}));
          } catch (...) {
            promise->set_value(std::unexpected(
                ApiError{.status = static_cast<int>(response.status_code),
                         .message = "Unknown error while decoding API response",
                         .data = std::move(*raw)}));
          }
        },
        cpr::Url{BASE_URL + path}, std::move(params),
        cpr::Header{{"Lang", "ru"}});
  } catch (const std::exception &e) {
    promise->set_value(std::unexpected(
        ApiError{.status = 0, .message = e.what(), .data = {}}));
  }
  return future;
}

cpr::Parameters search_parameters(const SearchInput &input) {
  cpr::Parameters params;
  addParameter(params, "studio_ids", input.studio_ids);
  addParameter(params, "q", input.q);
  addParameter(params, "director_ids", input.director_ids);
  addParameter(params, "worldart_ids", input.worldart_ids);
  addParameter(params, "mal_ids", input.mal_ids);
  addParameter(params, "kp_ids", input.kp_ids);
  addParameter(params, "shikimori_ids", input.shikimori_ids);
  addParameter(params, "min_age", input.min_age);
  addParameter(params, "max_rating_counters", input.max_rating_counters);
  addParameter(params, "min_rating_counters", input.min_rating_counters);
  addParameter(params, "ids", input.ids);
  addParameter(params, "season", input.season);
  addParameter(params, "status", input.status);
  addParameter(params, "types", input.types);
  addParameter(params, "exclude_genres", input.exclude_genres);
  addParameter(params, "genres", input.genres);
  addParameter(params, "max_rating", input.max_rating);
  addParameter(params, "min_rating", input.min_rating);
  addParameter(params, "ep_to", input.ep_to);
  addParameter(params, "ep_from", input.ep_from);
  addParameter(params, "to_year", input.to_year);
  addParameter(params, "sort_forward", input.sort_forward);
  addParameter(params, "sort", input.sort);
  addParameter(params, "offset", input.offset);
  addParameter(params, "limit", input.limit);
  return params;
}

} // namespace
Result<nlohmann::json> get(std::string_view path,
                           const cpr::Parameters &params) {
  const auto response = cpr::Get(cpr::Url{BASE_URL + std::string(path)}, params,
                                 cpr::Header{{"Lang", "ru"}});
  return parse_response(response);
}

std::future<Result<nlohmann::json>> getAsync(std::string path,
                                             cpr::Parameters params) {
  return request_async<nlohmann::json>(std::move(path), std::move(params),
                                       [](nlohmann::json raw) { return raw; });
}

Result<AnimeResult> getAnime(std::string_view alias_or_id, bool need_videos) {
  cpr::Parameters params;

  params.Add({"need_videos", need_videos ? "true" : "false"});

  auto response = get("anime/" + std::string(alias_or_id), params);

  if (!response) return std::unexpected(response.error());

  try {

    return response->at("response").get<AnimeResult>();

  } catch (const std::exception &e) {

    return std::unexpected(
        ApiError{.status = 200, .message = e.what(), .data = *response});
  }
}

std::future<Result<AnimeResult>> getAnimeAsync(std::string alias_or_id,
                                               bool need_videos) {
  cpr::Parameters params;
  params.Add({"need_videos", need_videos ? "true" : "false"});
  return request_async<AnimeResult>(
      "anime/" + alias_or_id, std::move(params), [](const nlohmann::json &raw) {
        return raw.at("response").get<AnimeResult>();
      });
}

// ============================================================
// searchTitles
// ============================================================

Result<std::vector<SearchResult>> searchTitles(const SearchInput &input) {
  auto response = get("anime", search_parameters(input));

  if (!response) return std::unexpected(response.error());

  try {

    return response->at("response").get<std::vector<SearchResult>>();

  } catch (const std::exception &e) {

    return std::unexpected(
        ApiError{.status = 200, .message = e.what(), .data = *response});
  }
}

std::future<Result<std::vector<SearchResult>>>
searchTitlesAsync(SearchInput input) {
  return request_async<std::vector<SearchResult>>(
      "anime", search_parameters(input), [](const nlohmann::json &raw) {
        return raw.at("response").get<std::vector<SearchResult>>();
      });
}

// ============================================================
// getFeed
// ============================================================

Result<FeedResponse> getFeed(const std::vector<std::string> &watches) {
  cpr::Parameters params;

  if (!watches.empty()) { params.Add({"watches", join(watches)}); }

  auto response = get("feed", params);

  if (!response) return std::unexpected(response.error());

  try {

    return response->at("response").get<FeedResponse>();

  } catch (const std::exception &e) {

    return std::unexpected(
        ApiError{.status = 200, .message = e.what(), .data = *response});
  }
}

std::future<Result<FeedResponse>>
getFeedAsync(std::vector<std::string> watches) {
  cpr::Parameters params;
  if (!watches.empty()) params.Add({"watches", join(watches)});
  return request_async<FeedResponse>(
      "feed", std::move(params), [](const nlohmann::json &raw) {
        return raw.at("response").get<FeedResponse>();
      });
}

} // namespace yani
