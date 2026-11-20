find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(video_serialization_checks video_serialization_checks.cpp)
target_link_libraries(video_serialization_checks PRIVATE server GTest::gtest_main)

add_executable(aniparser_checks regression_tests.cpp)
target_link_libraries(aniparser_checks PRIVATE server GTest::gtest)

gtest_discover_tests(video_serialization_checks WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTIES TIMEOUT 30 LABELS offline)
gtest_discover_tests(aniparser_checks WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTIES TIMEOUT 30 LABELS offline)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(ipc_server_checks ipc_server_checks.cpp)
  target_link_libraries(ipc_server_checks PRIVATE server GTest::gtest_main)
  gtest_discover_tests(ipc_server_checks WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTIES TIMEOUT 15 LABELS ipc)

  find_package(nlohmann_json CONFIG REQUIRED)
  add_executable(tui_client_checks tui_client_checks.cpp)
  target_link_libraries(tui_client_checks PRIVATE nlohmann_json::nlohmann_json GTest::gtest_main)
  gtest_discover_tests(tui_client_checks WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTIES TIMEOUT 10 LABELS ipc)
endif()
