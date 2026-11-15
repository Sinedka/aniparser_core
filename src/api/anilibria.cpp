#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api.hpp"

namespace yani {
namespace {

constexpr std::array<std::string_view, 2> kApiBases{
    "https://anilibria.top/api/v1",
    "https://api.anilibria.app/api/v1",
};

struct QualityField {
  std::string_view name;
  std::string_view field;
};

constexpr std::array<QualityField, 3> kQualityFields{{
    {"fhd", "hls_1080"},
    {"hd", "hls_720"},
    {"sd", "hls_480"},
}};

ApiError make_error(int status, std::string message, nlohmann::json data = {}) {
  return ApiError{
      .status = status, .message = std::move(message), .data = std::move(data)};
}

std::string encode_path_segment(std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char byte : value) {
    const bool unreserved = (byte >= 'a' && byte <= 'z') ||
                            (byte >= 'A' && byte <= 'Z') ||
                            (byte >= '0' && byte <= '9') || byte == '-' ||
                            byte == '.' || byte == '_' || byte == '~';
    if (unreserved) {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[byte >> 4]);
      encoded.push_back(hex[byte & 0x0f]);
    }
  }
  return encoded;
}

std::int64_t episode_ordinal(const nlohmann::json &episode) {
  const auto it = episode.find("ordinal");
  if (it == episode.end()) return std::numeric_limits<std::int64_t>::max();
  if (it->is_number_integer()) return it->get<std::int64_t>();
  if (!it->is_string()) return std::numeric_limits<std::int64_t>::max();
  const auto &text = it->get_ref<const std::string &>();
  std::int64_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size()
             ? value
             : std::numeric_limits<std::int64_t>::max();
}

std::string absolute_hls_url(std::string url, std::string_view host) {
  if (url.starts_with("https://") || url.starts_with("http://")) return url;
  if (url.starts_with("//")) return "https:" + url;
  if (!url.empty() && url.front() == '/' && !host.empty()) {
    std::string result = "https://";
    result.append(host);
    result += url;
    return result;
  }
  return url;
}

Result<nlohmann::json> request_release(std::string_view base,
                                       std::string_view alias) {
  const auto url =
      std::string(base) + "/anime/releases/" + encode_path_segment(alias);
  const auto response =
      cpr::Get(cpr::Url{url}, cpr::Header{{"User-Agent", "Mozilla/5.0"}},
               cpr::ConnectTimeout{std::chrono::seconds(10)},
               cpr::Timeout{std::chrono::seconds(20)});
  if (response.error.code != cpr::ErrorCode::OK)
    return std::unexpected(make_error(0, response.error.message));

  nlohmann::json document;
  try {
    document = nlohmann::json::parse(response.text);
  } catch (const std::exception &error) {
    return std::unexpected(
        make_error(static_cast<int>(response.status_code),
                   std::string("Invalid AniLibria JSON: ") + error.what()));
  }
  if (response.status_code < 200 || response.status_code >= 300) {
    std::string message = "AniLibria HTTP error";
    if (document.is_object() && document.contains("message") &&
        document["message"].is_string())
      message = document["message"].get<std::string>();
    return std::unexpected(make_error(static_cast<int>(response.status_code),
                                      std::move(message), std::move(document)));
  }
  if (!document.is_object() || !document.contains("episodes") ||
      !document["episodes"].is_array())
    return std::unexpected(make_error(
        static_cast<int>(response.status_code),
        "AniLibria response has no episodes array", std::move(document)));
  return document;
}

nlohmann::json normalize_episodes(const nlohmann::json &release) {
  struct OrderedEpisode {
    std::int64_t ordinal;
    nlohmann::json episode;
  };
  std::vector<OrderedEpisode> episodes;
  const auto &source = release.at("episodes");
  episodes.reserve(source.size());
  std::string host;
  if (const auto it = release.find("host");
      it != release.end() && it->is_string())
    host = it->get<std::string>();

  for (std::size_t index = 0; index < source.size(); ++index) {
    const auto &item = source[index];
    if (!item.is_object()) continue;
    const auto ordinal = episode_ordinal(item);
    nlohmann::json qualities = nlohmann::json::array();
    for (const auto &field : kQualityFields) {
      const auto link = item.find(std::string(field.field));
      if (link == item.end() || !link->is_string() ||
          link->get_ref<const std::string &>().empty())
        continue;
      qualities.push_back({
          {"quality", field.name},
          {"url", absolute_hls_url(link->get<std::string>(), host)},
      });
    }

    nlohmann::json episode{{"qualities", std::move(qualities)}};
    if (ordinal != std::numeric_limits<std::int64_t>::max())
      episode["ordinal"] = ordinal;
    if (const auto name = item.find("name");
        name != item.end() && name->is_string())
      episode["name"] = *name;
    episodes.push_back({ordinal, std::move(episode)});
  }

  std::stable_sort(episodes.begin(), episodes.end(),
                   [](const auto &left, const auto &right) {
                     return left.ordinal < right.ordinal;
                   });
  nlohmann::json result = nlohmann::json::array();
  for (auto &episode : episodes) result.push_back(std::move(episode.episode));
  return result;
}

} // namespace

Result<nlohmann::json> getAnilibriaVideos(std::int64_t anime_id) {
  if (anime_id <= 0)
    return std::unexpected(make_error(0, "Anime ID must be positive"));

  const auto anime = getAnime(std::to_string(anime_id), false);
  if (!anime) return std::unexpected(anime.error());
  const auto &alias = anime->remote_ids.anilibria_alias;
  if (alias.empty())
    return std::unexpected(make_error(0, "Anime has no AniLibria alias"));

  std::string errors;
  int last_status = 0;
  auto attempts = nlohmann::json::array();
  for (const auto base : kApiBases) {
    auto release = request_release(base, alias);
    if (!release) {
      last_status = release.error().status;
      attempts.push_back({{"base", base},
                          {"status", last_status},
                          {"message", release.error().message},
                          {"data", release.error().data}});
      if (!errors.empty()) errors += "; ";
      errors += std::string(base) + ": " + release.error().message;
      continue;
    }
    return nlohmann::json{{"episodes", normalize_episodes(*release)}};
  }
  return std::unexpected(make_error(last_status,
                                    "AniLibria request failed: " + errors,
                                    {{"attempts", std::move(attempts)}}));
}

} // namespace yani
