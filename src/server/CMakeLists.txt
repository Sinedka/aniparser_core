add_library(api STATIC)

target_sources(server
    PRIVATE
        api.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            api.hpp
)

target_include_directories(api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
