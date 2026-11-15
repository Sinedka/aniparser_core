add_library(server_ipc OBJECT)
target_sources(server_ipc PRIVATE request_validation.hpp protocol.hpp)

if(WIN32)
  target_sources(server_ipc PRIVATE windows.cpp)
else()
  target_sources(server_ipc PRIVATE unix.cpp)
endif()

target_include_directories(server_ipc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(server_ipc PRIVATE core)
