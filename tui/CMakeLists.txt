find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses)

add_executable(aniparser_tui main.cpp CacheReader.cpp)
target_compile_features(aniparser_tui PRIVATE cxx_std_23)
target_include_directories(aniparser_tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache)
target_link_libraries(aniparser_tui PRIVATE
  nlohmann_json::nlohmann_json
  cpr::cpr
  PkgConfig::NOTCURSES
)
