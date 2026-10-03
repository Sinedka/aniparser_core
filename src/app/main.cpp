#include "api.hpp"
#include "models.hpp"
#include <iostream>

int main(int argc, char *argv[]) {
  yani::SearchInput input{.q = "Naruto"};
  auto ans = yani::searchTitles(input);
  if (ans) {
    for (auto e : *ans) {
      std::cout << e.anime_url << "\n";
    }
  } else {
    std::cout << "ERROR: " << ans.error().message << "\n";
  }
  return 0;
}
