add_library(common INTERFACE)


target_sources(common INTERFACE
    types.hpp
)

target_include_directories(common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
