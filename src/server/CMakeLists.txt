add_subdirectory(ipc)

add_library(server STATIC
    server.cpp
    $<TARGET_OBJECTS:server_ipc>
)

target_sources(server
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            server.hpp
            ipc_server.hpp
)

target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PUBLIC core)

if(NOT ANDROID AND NOT IOS)
  add_executable(aniparser_server main.cpp)
  target_link_libraries(aniparser_server PRIVATE server)
endif()
