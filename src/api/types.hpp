#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yani {

struct SearchInput {
  std::optional<std::vector<std::int64_t>> studio_ids;
  std::optional<std::string> q;
  std::optional<std::vector<std::int64_t>> director_ids;
  std::optional<std::vector<std::int64_t>> worldart_ids;
  std::optional<std::vector<std::int64_t>> mal_ids;
  std::optional<std::vector<std::int64_t>> kp_ids;
  std::optional<std::vector<std::int64_t>> shikimori_ids;
  std::optional<std::vector<std::int64_t>> min_age;
  std::optional<std::int64_t> max_rating_counters;
  std::optional<std::int64_t> min_rating_counters;
  std::optional<std::vector<std::int64_t>> ids;
  std::optional<std::vector<std::string>> season;
  std::optional<std::vector<std::string>> status;
  std::optional<std::vector<std::string>> types;
  std::optional<std::vector<std::string>> exclude_genres;
  std::optional<std::vector<std::string>> genres;
  std::optional<double> max_rating;
  std::optional<double> min_rating;
  std::optional<std::int64_t> ep_to;
  std::optional<std::int64_t> ep_from;
  std::optional<std::int64_t> to_year;
  std::optional<bool> sort_forward;
  std::optional<std::string> sort;
  std::optional<std::int64_t> offset;
  std::optional<std::int64_t> limit;
};
} // namespace yani
