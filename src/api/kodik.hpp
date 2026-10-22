#pragma once

#include "video_extractor.hpp"

namespace yani {

class KodikVideoExtractor final : public VideoExtractor {
  public:
  bool supports(std::string_view iframe) const noexcept override;
  Result<std::vector<VideoQuality>>
  extract(std::string_view iframe) const override;
};

} // namespace yani
