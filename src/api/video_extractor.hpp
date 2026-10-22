#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "api.hpp"

namespace yani {

struct VideoQuality {
  std::string quality;
  std::string url;
};

// Implementations can be added per video provider and registered in video.cpp.
class VideoExtractor {
  public:
  virtual ~VideoExtractor() = default;
  virtual bool supports(std::string_view iframe) const noexcept = 0;
  virtual Result<std::vector<VideoQuality>>
  extract(std::string_view iframe) const = 0;
};

Result<std::vector<VideoQuality>> getVideoOptions(std::string_view iframe);
Result<std::string> getVideo(std::string_view iframe,
                             std::string_view quality = "720");
bool supportsVideo(std::string_view iframe) noexcept;

} // namespace yani
