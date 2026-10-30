add_library(aniparser_cache STATIC
    anime_cache.cpp
    anime_collection.cpp
    save_manager.cpp
)

target_sources(aniparser_cache
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            anime_cache.hpp
            anime_collection.hpp
            history.hpp
            save_manager.hpp
)

target_include_directories(aniparser_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aniparser_cache PUBLIC api)
