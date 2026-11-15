add_subdirectory(cache)

add_library(core STATIC
    core.cpp
    task_pool.cpp
    anime_service.cpp
    search_feed.cpp
    history.cpp
    collection_changes.cpp
)

target_sources(core
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES core.hpp callback_queue.hpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC api aniparser_cache)
