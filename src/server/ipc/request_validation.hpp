#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace aniparser::ipc {
inline std::int64_t integer(const nlohmann::json &value,
                            std::int64_t minimum = 0,
                            std::int64_t maximum = INT64_MAX) {
  if (!value.is_number_integer() ||
      (value.is_number_unsigned() &&
       value.get<std::uint64_t>() > static_cast<std::uint64_t>(maximum)))
    throw std::runtime_error("Expected an in-range integer");
  const auto number = value.get<std::int64_t>();
  if (number < minimum || number > maximum)
    throw std::runtime_error("Integer is outside the permitted range");
  return number;
}

inline void request_id(const nlohmann::json &value) {
  if (value.is_string()) {
    if (value.get_ref<const std::string &>().size() > 128)
      throw std::runtime_error("Request ID is too long");
  } else integer(value);
}

inline void validate_request(const nlohmann::json &request) {
  if (!request.is_object())
    throw std::runtime_error("Request must be an object");
  request_id(request.at("id"));
  if (!request.at("method").is_string())
    throw std::runtime_error("method must be a string");
  if (request.contains("target_id")) request_id(request.at("target_id"));
  for (const auto* key : {"anime_id", "time", "offset", "limit"}) {
    if (!request.contains(key)) continue;
    integer(request.at(key),
            std::string_view(key) == "anime_id" ||
                    std::string_view(key) == "limit"
                ? 1
                : 0,
            std::string_view(key) == "offset"
                ? static_cast<std::int64_t>(std::min<std::uint64_t>(
                      std::numeric_limits<std::size_t>::max(), INT64_MAX))
                : INT64_MAX);
  }
  for (const auto* key : {"player", "dubber", "episode", "iframe", "quality"}) {
    if (!request.contains(key)) continue;
    if (!request.at(key).is_string() ||
        request.at(key).get_ref<const std::string &>().size() > 4096)
      throw std::runtime_error(std::string("Invalid string: ") + key);
  }
  if (!request.contains("params")) return;
  const auto &params = request.at("params");
  if (!params.is_object()) throw std::runtime_error("params must be an object");
  for (auto it = params.begin(); it != params.end(); ++it) {
    const auto &value = it.value();
    if (value.is_null()) continue;
    const auto &key = it.key();
    if (key == "q" || key == "sort") {
      if (!value.is_string() ||
          value.get_ref<const std::string &>().size() > 4096)
        throw std::runtime_error("Invalid search string");
    } else if (key == "sort_forward") {
      if (!value.is_boolean())
        throw std::runtime_error("sort_forward must be boolean");
    } else if (key == "min_rating" || key == "max_rating") {
      if (!value.is_number() || !std::isfinite(value.get<double>()) ||
          value.get<double>() < 0 || value.get<double>() > 10)
        throw std::runtime_error("Invalid rating filter");
    } else if (key == "limit") integer(value, 1, 50);
    else if (key == "offset" || key == "ep_to" || key == "ep_from" ||
             key == "to_year" || key == "min_rating_counters" ||
             key == "max_rating_counters")
      integer(value);
    else if (key == "ids" || key.ends_with("_ids") || key == "min_age") {
      if (!value.is_array() || value.size() > 1000)
        throw std::runtime_error("Invalid ID filter array");
      for (const auto &item : value) integer(item, key == "min_age" ? 0 : 1);
    } else if (key == "season" || key == "status" || key == "types" ||
               key == "exclude_genres" || key == "genres") {
      if (!value.is_array() || value.size() > 1000)
        throw std::runtime_error("Invalid search string array");
      for (const auto &item : value)
        if (!item.is_string() ||
            item.get_ref<const std::string &>().size() > 256)
          throw std::runtime_error("Invalid search array item");
    } else throw std::runtime_error("Unknown search parameter: " + key);
  }
  for (const auto &[lower, upper] :
       {std::pair{"min_rating", "max_rating"},
        {"min_rating_counters", "max_rating_counters"},
        {"ep_from", "ep_to"}})
    if (params.contains(lower) && params.contains(upper) &&
        !params[lower].is_null() && !params[upper].is_null() &&
        params[lower].get<long double>() > params[upper].get<long double>())
      throw std::runtime_error("Search range is reversed");
}
} // namespace aniparser::ipc
