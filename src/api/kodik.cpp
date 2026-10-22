#include "kodik.hpp"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace yani {
namespace {

ApiError error(std::string message, int status = 0) {
  return ApiError{.status = status, .message = std::move(message), .data = {}};
}

std::string capture(const std::string &text, const std::regex &pattern) {
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) return {};
  for (std::size_t i = 1; i < match.size(); ++i)
    if (match[i].matched) return match[i].str();
  return {};
}

std::string variable(const std::string &html, std::string_view name) {
  const std::regex pattern("var\\s*" + std::string(name) +
                           "\\s*=\\s*['\\\"](.*?)['\\\"];");
  return capture(html, pattern);
}

std::string decode_base64(std::string_view input) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(input.size() * 3 / 4);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char ch : input) {
    if (ch == '=') break;
    if (ch == '-' || ch == '_') {
      const auto value = ch == '-' ? 62 : 63;
      buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
      bits += 6;
    } else {
      const auto pos = alphabet.find(ch);
      if (pos == std::string_view::npos) continue;
      buffer = (buffer << 6) | static_cast<std::uint32_t>(pos);
      bits += 6;
    }
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return output;
}

std::string decrypt_url(std::string_view value) {
  std::string shifted(value);
  for (auto &ch : shifted) {
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(((ch - 'A' + 18) % 26) + 'A');
    else if (ch >= 'a' && ch <= 'z')
      ch = static_cast<char>(((ch - 'a' + 18) % 26) + 'a');
  }
  if (shifted.ends_with(".m3u8"))
    return shifted.starts_with("https") ? shifted : "https:" + shifted;
  auto decoded = decode_base64(shifted);
  if (decoded.empty()) return {};
  return decoded.starts_with("https") ? decoded : "https:" + decoded;
}

std::vector<VideoQuality> source_urls(const nlohmann::json &links) {
  std::vector<VideoQuality> result;
  for (auto it = links.begin(); it != links.end(); ++it) {
    int numeric_quality = 0;
    const auto parsed_quality = std::from_chars(
        it.key().data(), it.key().data() + it.key().size(), numeric_quality);
    if (parsed_quality.ec != std::errc{} ||
        parsed_quality.ptr != it.key().data() + it.key().size())
      continue;
    if (!it.value().is_array() || it.value().empty() ||
        !it.value().front().is_object())
      continue;
    const auto src = it.value().front().find("src");
    if (src == it.value().front().end() || !src->is_string()) continue;
    auto url = decrypt_url(src->get<std::string>());
    if (!url.empty())
      result.push_back({.quality = it.key(), .url = std::move(url)});
  }
  std::sort(
      result.begin(), result.end(), [](const auto &left, const auto &right) {
        int left_quality = 0;
        int right_quality = 0;
        const auto left_result = std::from_chars(
            left.quality.data(), left.quality.data() + left.quality.size(),
            left_quality);
        const auto right_result = std::from_chars(
            right.quality.data(), right.quality.data() + right.quality.size(),
            right_quality);
        if (left_result.ec == std::errc{} && right_result.ec == std::errc{})
          return left_quality > right_quality;
        return left.quality > right.quality;
      });
  return result;
}

bool is_kodik_url(std::string_view iframe) noexcept {
  constexpr std::string_view https_scheme = "https://";
  constexpr std::string_view protocol_relative = "//";
  std::size_t authority_start = 0;
  if (iframe.starts_with(https_scheme)) {
    authority_start = https_scheme.size();
  } else if (iframe.starts_with(protocol_relative)) {
    authority_start = protocol_relative.size();
  } else {
    return false;
  }
  const auto authority_end = iframe.find_first_of("/?#", authority_start);
  const auto authority =
      iframe.substr(authority_start, authority_end == std::string_view::npos
                                         ? std::string_view::npos
                                         : authority_end - authority_start);
  if (authority.find('@') != std::string_view::npos) return false;

  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  constexpr std::string_view kodik_host = "kodikplayer.com";
  if (host.size() != kodik_host.size()) return false;
  for (std::size_t i = 0; i < host.size(); ++i) {
    const auto ch = host[i] >= 'A' && host[i] <= 'Z'
                        ? static_cast<char>(host[i] - 'A' + 'a')
                        : host[i];
    if (ch != kodik_host[i]) return false;
  }
  return colon == std::string_view::npos ||
         authority.substr(colon + 1) == "443";
}

} // namespace

bool KodikVideoExtractor::supports(std::string_view iframe) const noexcept {
  return is_kodik_url(iframe);
}

Result<std::vector<VideoQuality>>
KodikVideoExtractor::extract(std::string_view iframe) const {
  try {
    const std::string iframe_url = iframe.starts_with("//")
                                       ? "https:" + std::string{iframe}
                                       : std::string{iframe};
    if (!supports(iframe_url))
      return std::unexpected(error("Unsupported video iframe URL"));
    const cpr::Url parsed_url{iframe_url};
    const std::string host = "kodikplayer.com";

    const auto page =
        cpr::Get(parsed_url, cpr::ConnectTimeout{std::chrono::seconds(5)},
                 cpr::Timeout{std::chrono::seconds(30)});
    if (page.error || page.status_code < 200 || page.status_code >= 300)
      return std::unexpected(error("Failed to fetch Kodik iframe",
                                   static_cast<int>(page.status_code)));
    if (page.text.find("<div class=\"message\">Видео не найдено</div>") !=
        std::string::npos)
      return std::unexpected(error("Kodik video was not found"));

    const auto &html = page.text;
    const std::string domain = variable(html, "domain");
    const std::string d_sign = variable(html, "d_sign");
    const std::string pd = variable(html, "pd");
    const std::string pd_sign = variable(html, "pd_sign");
    const std::string ref = variable(html, "ref");
    const std::string ref_sign = variable(html, "ref_sign");
    const std::regex type_pattern(
        R"rx((?:videoInfo|vInfo)\.type\s*=\s*['"](.*?)['"]|var\s+type\s*=\s*['"](.*?)['"])rx");
    const std::regex hash_pattern(
        R"rx((?:videoInfo|vInfo)\.hash\s*=\s*['"](.*?)['"])rx");
    const std::regex id_pattern(
        R"rx((?:videoInfo|vInfo)\.id\s*=\s*['"](.*?)['"]|var\s+videoId\s*=\s*['"](.*?)['"])rx");
    const auto type = capture(html, type_pattern);
    const auto hash = capture(html, hash_pattern);
    const auto id = capture(html, id_pattern);
    if (domain.empty() || d_sign.empty() || pd.empty() || pd_sign.empty() ||
        ref_sign.empty() || type.empty() || hash.empty() || id.empty())
      return std::unexpected(error("Kodik iframe is missing API parameters"));

    const std::regex player_pattern(
        R"rx(<script\s*type="text/javascript"\s*src="(/assets/js/app\.[^"]+\.js)"|var\s+playerLink\s*=\s*\[\s*['"](/assets/js/app\.[^'"]+\.js)['"])rx");
    std::smatch player_match;
    if (!std::regex_search(html, player_match, player_pattern))
      return std::unexpected(error("Kodik player script was not found"));
    const auto player_path =
        player_match[1].matched ? player_match[1].str() : player_match[2].str();
    const auto player = cpr::Get(cpr::Url{"https://" + host + player_path},
                                 cpr::ConnectTimeout{std::chrono::seconds(5)},
                                 cpr::Timeout{std::chrono::seconds(30)});
    if (player.error || player.status_code < 200 || player.status_code >= 300)
      return std::unexpected(error("Failed to fetch Kodik player script",
                                   static_cast<int>(player.status_code)));

    const std::regex api_pattern(
        R"rx(\$\.ajax\([^>]+,\s*url:\s*atob\(['"]([^'"]+)['"]\))rx");
    const auto api_path = decode_base64(capture(player.text, api_pattern));
    if (api_path.empty() || api_path.front() != '/')
      return std::unexpected(error("Kodik API path was not found"));

    const auto response = cpr::Post(
        cpr::Url{"https://" + host + api_path},
        cpr::Payload{{"d", domain},
                     {"d_sign", d_sign},
                     {"pd", pd},
                     {"pd_sign", pd_sign},
                     {"ref", ref},
                     {"ref_sign", ref_sign},
                     {"type", type},
                     {"hash", hash},
                     {"id", id},
                     {"bad_user", "false"},
                     {"info", "{}"},
                     {"cdn_is_working", "true"}},
        cpr::Header{
            {"Origin", "https://" + host},
            {"Referer", iframe_url},
            {"Accept", "application/json, text/javascript, */*; q=0.01"},
            {"Content-Type", "application/x-www-form-urlencoded"}},
        cpr::ConnectTimeout{std::chrono::seconds(5)},
        cpr::Timeout{std::chrono::seconds(30)});
    if (response.error || response.status_code < 200 ||
        response.status_code >= 300)
      return std::unexpected(error("Kodik API request failed",
                                   static_cast<int>(response.status_code)));

    const auto json = nlohmann::json::parse(response.text);
    const auto links = json.find("links");
    if (links == json.end() || !links->is_object())
      return std::unexpected(error("Kodik API response has no video links"));
    auto options = source_urls(*links);
    if (options.empty())
      return std::unexpected(error("Kodik API response has no usable m3u8"));
    return options;
  } catch (const std::exception &exception) {
    return std::unexpected(error(exception.what()));
  }
}

} // namespace yani
