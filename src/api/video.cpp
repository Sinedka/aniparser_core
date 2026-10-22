#include <array>
#include <span>

#include "kodik.hpp"
#include "video_extractor.hpp"

namespace yani {
namespace {

std::span<const VideoExtractor* const> extractors() noexcept {
  static const KodikVideoExtractor kodik;
  static const std::array<const VideoExtractor*, 1> registry{&kodik};
  return registry;
}

} // namespace

Result<std::vector<VideoQuality>> getVideoOptions(std::string_view iframe) {
  for (const auto* extractor : extractors())
    if (extractor->supports(iframe)) return extractor->extract(iframe);

  return std::unexpected(ApiError{
      .status = 0,
      .message = "No video extractor supports this iframe URL",
      .data = {},
  });
}

Result<std::string> getVideo(std::string_view iframe,
                             std::string_view quality) {
  auto options = getVideoOptions(iframe);
  if (!options) return std::unexpected(options.error());
  for (const auto &option : *options)
    if (option.quality == quality) return option.url;
  if (options->empty())
    return std::unexpected(ApiError{
        .status = 0, .message = "No stream quality available", .data = {}});
  return options->front().url;
}

bool supportsVideo(std::string_view iframe) noexcept {
  for (const auto* extractor : extractors())
    if (extractor->supports(iframe)) return true;
  return false;
}

} // namespace yani
