cmake_minimum_required (VERSION 3.3)

project (P2P-Live C CXX)

set(CMAKE_CXX_STANDARD 17)

option(USE_CLANG "build with clang" OFF)
option(MMDBG "memory debug" OFF)


set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(DEBUG_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/debug)
    
add_definitions(-D__STDC_CONSTANT_MACROS)

include_directories("include/")

find_path( AVCODEC_INCLUDE_DIR libavcodec/avcodec.h )
find_library( AVCODEC_LIBRARY avcodec )

find_path( AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h )
find_library( AVDEVICE_LIBRARY avdevice)

find_path( AVFILTER_INCLUDE_DIR libavfilter/avfilter.h )
find_library( AVFILTER_LIBRARY avfilter )

find_path( AVFORMAT_INCLUDE_DIR libavformat/avformat.h )
find_library( AVFORMAT_LIBRARY avformat )

find_path( AVUTIL_INCLUDE_DIR libavutil/time.h )
find_library( AVUTIL_LIBRARY avutil )

find_path( SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_library( SWRESAMPLE_LIBRARY swresample )

find_path( SWSCALE_INCLUDE_DIR libswscale/swscale.h)
find_library( SWSCALE_LIBRARY swscale )

set (FFMPEG_INCLUDE_DIR ${AVCODEC_INCLUDE_DIR})
set (FFMPEG_LIBRARY ${AVFORMAT_LIBRARY} ${AVDEVICE_LIBRARY} ${AVCODEC_LIBRARY} ${SWRESAMPLE_LIBRARY} ${SWSCALE_LIBRARY} ${AVFILTER_LIBRARY} ${AVUTIL_LIBRARY}
    pthread va m z)

if (MMDBG)
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif(MMDBG)

if (USE_CLANG)
    SET (CMAKE_C_COMPILER        "/usr/bin/clang")
    SET (CMAKE_CXX_COMPILER      "/usr/bin/clang++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument -Wno-unknown-warning-option -Wno-unused-const-variable")
    SET (CMAKE_LINKER  "/usr/bin/llvm-link")
else()
    SET (CMAKE_C_COMPILER        "/usr/bin/gcc")
    SET (CMAKE_CXX_COMPILER      "/usr/bin/g++")
    SET (CMAKE_LINKER  "/usr/bin/ld")
endif(USE_CLANG)


add_subdirectory(src)
add_subdirectory(lib)
add_subdirectory(test)
