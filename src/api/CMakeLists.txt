find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

add_library(api STATIC)

target_sources(api
    PRIVATE
        api.cpp
        kodik.cpp
        video.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            api.hpp
            kodik.hpp
            video_extractor.hpp
)

target_include_directories(api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(api
    PUBLIC
        nlohmann_json::nlohmann_json
        cpr::cpr
        common
        
)
