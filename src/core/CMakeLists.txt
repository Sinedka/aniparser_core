add_library(core STATIC)

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

target_link_libraries(core
    PUBLIC
    api
    aniparser::cache
)

