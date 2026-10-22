#include <nlohmann/json.hpp>

#include <iostream>
#include <string_view>

#include "json.hpp"
#include "server.hpp"

namespace {

int run_self_tests() {
  int failures = 0;
  const auto check = [&failures](std::string_view name, bool passed) {
    std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
    if (!passed) ++failures;
  };

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

  std::cout << (failures == 0
                    ? "All app checks passed\n"
                    : std::to_string(failures) + " app check(s) failed\n");
  return failures == 0 ? 0 : 1;
}

int run_get_anime_test(std::int64_t anime_id) {
  std::cout << "Requesting anime " << anime_id << " from the API...\n";
  const auto result = yani::getAnime(std::to_string(anime_id), true);
  if (!result) {
    std::cerr << "FAIL getAnime(" << anime_id << "): " << result.error().message
              << '\n';
    if (!result.error().data.is_null() && !result.error().data.empty())
      std::cerr << result.error().data.dump(2) << '\n';
    return 1;
  }

  const bool passed = result->anime_id == anime_id && !result->title.empty();
  std::cout << (passed ? "PASS " : "FAIL ") << "getAnime(" << anime_id
            << "): returned anime_id=" << result->anime_id
            << ", title=" << result->title << '\n';
  std::cout << nlohmann::json(*result).dump(2) << '\n';
  return passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--self-test")
    return run_self_tests();
  if (argc > 1 && std::string_view(argv[1]) == "--test-get-anime")
    return run_get_anime_test(771);

  const std::string id = argc > 1 ? argv[1] : "771";
  auto result = yani::getAnime(id, true);
  if (!result) {
    std::cerr << "API request failed: " << result.error().message << '\n';
    return 1;
  }
  std::cout << nlohmann::json(*result).dump(2) << '\n';
  return 0;
}
