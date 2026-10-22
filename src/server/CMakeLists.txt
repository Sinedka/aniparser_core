add_library(server STATIC)

if(WIN32)
  set(server_ipc_source ipc_server_windows.cpp)
else()
  set(server_ipc_source ipc_server.cpp)
endif()

target_sources(server
  PRIVATE
    server.cpp
    ${server_ipc_source}
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
