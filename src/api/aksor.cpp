#include "aksor.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace yani {
namespace {

struct AksorUrl {
  std::string video_id;
};

bool equals_ascii_case_insensitive(std::string_view left,
                                   std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    auto ch = left[index];
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (ch != right[index]) return false;
  }
  return true;
}

ApiError error(std::string message, int status = 0, nlohmann::json data = {}) {
  return ApiError{
      .status = status, .message = std::move(message), .data = std::move(data)};
}

bool parse_aksor_url(std::string_view input, AksorUrl &parsed) {
  constexpr std::string_view https_prefix = "https://";
  constexpr std::string_view http_prefix = "http://";
  constexpr std::string_view protocol_relative = "//";
  std::string_view remainder = input;
  std::string_view allowed_port = ":443";
  if (remainder.starts_with(https_prefix)) {
    remainder.remove_prefix(https_prefix.size());
  } else if (remainder.starts_with(http_prefix)) {
    remainder.remove_prefix(http_prefix.size());
    allowed_port = ":80";
  } else if (remainder.starts_with(protocol_relative)) {
    remainder.remove_prefix(protocol_relative.size());
  } else {
    return false;
  }

  const auto path_start = remainder.find_first_of("/?#");
  const auto authority = remainder.substr(0, path_start);
  const auto port_separator = authority.find(':');
  const auto host = authority.substr(0, port_separator);
  const auto port = port_separator == std::string_view::npos
                        ? std::string_view{}
                        : authority.substr(port_separator);
  if (!equals_ascii_case_insensitive(host, "player.aksor.tv") ||
      (!port.empty() && port != allowed_port))
    return false;

  auto path = path_start == std::string_view::npos
                  ? std::string_view{}
                  : remainder.substr(path_start);
  if (!path.starts_with("/video/")) return false;
  path.remove_prefix(7);
  const auto id_end = path.find_first_of("/?#");
  const auto id = path.substr(0, id_end);
  if (id.empty()) return false;
  if (id_end != std::string_view::npos) {
    auto suffix = path.substr(id_end);
    if (suffix.starts_with('/')) {
      suffix.remove_prefix(1);
      if (!suffix.empty() && suffix.front() != '?' && suffix.front() != '#')
        return false;
    }
  }
  for (const unsigned char ch : id)
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
      return false;

  parsed.video_id = id;
  return true;
}

constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
    kQualityFields{{{"4k", "q4k"},
                    {"2k", "q2k"},
                    {"1080", "q1080"},
                    {"720", "q720"},
                    {"480", "q480"},
                    {"360", "q360"}}};

} // namespace

bool AksorVideoExtractor::supports(std::string_view iframe) const noexcept {
  try {
    AksorUrl parsed;
    return parse_aksor_url(iframe, parsed);
  } catch (...) { return false; }
}

Result<std::vector<VideoQuality>>
AksorVideoExtractor::extract(std::string_view iframe) const {
  AksorUrl parsed;
  if (!parse_aksor_url(iframe, parsed))
    return std::unexpected(error("Unsupported Aksor iframe URL"));

  const auto response =
      cpr::Get(cpr::Url{"https://player.aksor.tv/api/video/" + parsed.video_id},
               cpr::Header{{"Accept", "application/json"},
                           {"Referer", "https://old.yummyani.me/"}},
               cpr::ConnectTimeout{std::chrono::seconds(10)},
               cpr::Timeout{std::chrono::seconds(20)});
  if (response.error)
    return std::unexpected(
        error("Aksor API request failed: " + response.error.message,
              static_cast<int>(response.status_code)));

  nlohmann::json document;
  try {
    document = nlohmann::json::parse(response.text);
  } catch (const std::exception &exception) {
    return std::unexpected(
        error("Invalid Aksor API JSON: " + std::string(exception.what()),
              static_cast<int>(response.status_code)));
  }
  if (response.status_code < 200 || response.status_code >= 300) {
    std::string message = "Aksor API returned HTTP error";
    if (document.is_object() && document.contains("detail") &&
        document["detail"].is_string())
      message = document["detail"].get<std::string>();
    return std::unexpected(error(std::move(message),
                                 static_cast<int>(response.status_code),
                                 std::move(document)));
  }

  if (!document.is_object())
    return std::unexpected(error("Aksor API response is not an object",
                                 static_cast<int>(response.status_code),
                                 std::move(document)));
  const auto qualities = document.find("qualities");
  if (qualities == document.end() || !qualities->is_object())
    return std::unexpected(error("Aksor API response has no qualities object",
                                 static_cast<int>(response.status_code),
                                 std::move(document)));

  std::vector<VideoQuality> result;
  for (const auto &[name, field] : kQualityFields) {
    const auto url = qualities->find(std::string(field));
    if (url == qualities->end() || !url->is_string() ||
        url->get_ref<const std::string &>().empty())
      continue;
    result.push_back({std::string(name), url->get<std::string>()});
  }
  if (result.empty())
    return std::unexpected(error("Aksor API returned no usable video links",
                                 static_cast<int>(response.status_code),
                                 std::move(document)));
  return result;
}

} // namespace yani
