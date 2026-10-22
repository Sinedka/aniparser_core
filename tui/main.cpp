#include <string>

#include "tui_app.hpp"

int main(int argc, char** argv) {
  const std::string endpoint = argc > 1 ? argv[1] : "/tmp/aniparser.sock";
  return aniparser::tui::run(endpoint);
}
