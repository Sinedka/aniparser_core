#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <utility>

#include "json.hpp"
#include "server.hpp"

namespace {
void check(const char* name, bool success) { EXPECT_TRUE(success) << name; }

TEST(Video, KodikUrls) {
  check("Kodik accepts HTTPS URL",
        yani::supportsVideo("https://kodikplayer.com/season/1"));
  check("Kodik accepts protocol-relative URL",
        yani::supportsVideo("//kodikplayer.com/season/1"));
  check("Kodik rejects HTTP URL",
        !yani::supportsVideo("http://kodikplayer.com/season/1"));
  check("Kodik rejects lookalike host",
        !yani::supportsVideo("https://kodikplayer.com.example.org/season/1"));
  check("Kodik rejects userinfo host confusion",
        !yani::supportsVideo("https://kodikplayer.com@evil.test/season/1"));
}

TEST(Video, AnimeSerialization) {
  yani::AnimeResult anime;
  anime.title = "Fixture";
  const auto add_video = [&anime](std::string number, std::string iframe,
                                  std::string player = "Kodik",
                                  std::string dubbing = "Dub") {
    yani::VideoInfo video;
    video.number = std::move(number);
    video.iframe_url = std::move(iframe);
    video.data.player = std::move(player);
    video.data.dubbing = std::move(dubbing);
    anime.videos.push_back(std::move(video));
  };
  add_video("10", "//kodikplayer.com/season/1");
  add_video("2", "https://kodikplayer.com/season/2");
  add_video("1", "https://kodikplayer.com/season/3", "Kodik", "Alt Dub");
  add_video("0", "https://unsupported.example/video");

  const auto serialized = Server::animeToJson(anime);
  const auto players = serialized.value("players", nlohmann::json::array());
  check("anime serialization removes legacy videos field",
        !serialized.contains("videos"));
  check("anime serialization groups videos under existing player",
        players.size() == 1 &&
            players[0].value("name", std::string{}) == "Kodik");
  check("anime serialization groups multiple dubbings",
        players.size() == 1 && players[0]["dubbers"].size() == 2);
  check(
      "anime serialization sorts episode numbers numerically",
      players.size() == 1 && players[0]["dubbers"][0]["episodes"].size() == 2 &&
          players[0]["dubbers"][0]["episodes"][0].value("number",
                                                        std::string{}) == "2" &&
          players[0]["dubbers"][0]["episodes"][1].value("number",
                                                        std::string{}) == "10");
  check("anime serialization filters unsupported players",
        players.size() == 1 &&
            players[0]["dubbers"][0]["episodes"].size() == 2);
}
TEST(Video, AksorUrlsRequireSupportedHostAndVideoPath) {
  EXPECT_TRUE(yani::supportsVideo("https://player.aksor.tv/video/abc-123"));
  EXPECT_TRUE(yani::supportsVideo("//player.aksor.tv/video/abc_123?x=1"));
  for (const auto* url :
       {"https://player.aksor.tv.example.org/video/abc",
        "https://player.aksor.tv/embed/abc", "https://player.aksor.tv/video/",
        "https://player.aksor.tv/video/abc/extra",
        "https://player.aksor.tv:8080/video/abc"}) {
    EXPECT_FALSE(yani::supportsVideo(url)) << url;
  }
}

TEST(Video, UnsupportedIframeReturnsErrorWithoutFetching) {
  constexpr auto* url = "https://example.org/video/abc";
  const auto options = yani::getVideoOptions(url);
  const auto video = yani::getVideo(url, "720");
  ASSERT_FALSE(options.has_value());
  ASSERT_FALSE(video.has_value());
  EXPECT_FALSE(options.error().message.empty());
  EXPECT_EQ(video.error().message, options.error().message);
}
} // namespace
