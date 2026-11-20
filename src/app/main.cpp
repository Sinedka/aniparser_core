#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

#include "api.hpp"
#include "json.hpp"

int main(int argc, char** argv) {
  const std::string id = argc > 1 ? argv[1] : "771";
  auto result = yani::getAnime(id, true);
  if (!result) {
    std::cerr << "API request failed: " << result.error().message << '\n';
    return 1;
  }
  std::cout << nlohmann::json(*result).dump(2) << '\n';
  return 0;
}
