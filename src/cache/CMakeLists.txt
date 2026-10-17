set(cache_sources
    AnimeHeap.cpp
    FeedHeap.cpp
    HeapManager.cpp
    SearchHeap.cpp
    VariableHeap.cpp
    cache.cpp
    SegmentAllocator.hpp
)

set(cache_public_headers
    Anime.hpp
    AnimeHeap.hpp
    FeedHeap.hpp
    HeapManager.hpp
    IHeap.hpp
    IShmSegmentProvider.hpp
    SharedString.hpp
    SharedSearch.hpp
    SharedFeed.hpp
    SharedVector.hpp
    SearchHeap.hpp
    VariableHeap.hpp
    cache.hpp
    types.hpp
)

if(WIN32)
    list(APPEND cache_sources WindowsShmSegmentProvider.cpp)
    list(APPEND cache_public_headers WindowsShmSegmentProvider.hpp)
elseif(ANDROID OR IOS)
    list(APPEND cache_sources FileShmSegmentProvider.cpp)
    list(APPEND cache_public_headers FileShmSegmentProvider.hpp)
elseif(UNIX)
    list(APPEND cache_sources PosixShmSegmentProvider.cpp)
    list(APPEND cache_public_headers PosixShmSegmentProvider.hpp)
endif()

add_library(cache STATIC ${cache_sources})
add_library(aniparser::cache ALIAS cache)

target_sources(cache
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${cache_public_headers}
)

target_compile_features(cache PUBLIC cxx_std_23)

target_include_directories(cache
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

find_package(Threads REQUIRED)
target_link_libraries(cache PUBLIC Threads::Threads)

# shm_open lived in librt on older Linux/glibc systems; newer versions provide
# it in libc. Avoid adding an unrelated librt path to the runtime search path.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(shm_open "sys/mman.h" ANIPARSER_SHM_OPEN_IN_LIBC)
    if(NOT ANIPARSER_SHM_OPEN_IN_LIBC)
        find_library(ANIPARSER_RT_LIBRARY rt)
        if(ANIPARSER_RT_LIBRARY)
            target_link_libraries(cache PRIVATE ${ANIPARSER_RT_LIBRARY})
        endif()
    endif()
endif()
