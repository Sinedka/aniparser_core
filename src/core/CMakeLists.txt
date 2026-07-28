add_library(api STATIC)

target_sources(core
    PRIVATE
        core.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            core.hpp
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
