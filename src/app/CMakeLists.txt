add_executable(my_app
    main.cpp
)

target_link_libraries(my_app
    PRIVATE
    server
)

add_executable(aniparser_checks regression_tests.cpp)
target_link_libraries(aniparser_checks PRIVATE server)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(nlohmann_json CONFIG REQUIRED)
  add_executable(tui_client_checks tui_client_checks.cpp)
  target_link_libraries(tui_client_checks PRIVATE nlohmann_json::nlohmann_json)
endif()
