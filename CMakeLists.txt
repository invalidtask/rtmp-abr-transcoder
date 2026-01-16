cmake_minimum_required(VERSION 3.20)
project(rtmp_abr_transcoder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include_directories(${CMAKE_SOURCE_DIR}/include)

# Find required packages for transcoding
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENH264 REQUIRED openh264)
pkg_check_modules(FDK_AAC REQUIRED fdk-aac)
pkg_check_modules(LIBYUV libyuv)

# If libyuv not found via pkg-config, try find_library
if(NOT LIBYUV_FOUND)
    find_library(LIBYUV_LIBRARIES yuv)
    find_path(LIBYUV_INCLUDE_DIRS libyuv.h PATH_SUFFIXES libyuv)
endif()

set(CORE_SOURCES
    src/core/log.cpp
    src/core/cli.cpp
)

set(NET_SOURCES
    src/net/buffer.cpp
    src/net/socket.cpp
    src/net/epoll_loop.cpp
)

set(RTMP_SOURCES
    src/rtmp/amf0.cpp
    src/rtmp/rtmp_chunks.cpp
    src/rtmp/rtmp_messages.cpp
    src/rtmp/rtmp_handshake.cpp
    src/rtmp/rtmp_session.cpp
    src/rtmp/rtmp_server.cpp
    src/rtmp/rtmp_client.cpp
)

set(RELAY_SOURCES
    src/relay/stream_id.cpp
    src/relay/policy.cpp
    src/relay/relay_manager.cpp
)

set(TRANSCODE_SOURCES
    src/transcode/decoder_openh264.cpp
    src/transcode/decoder_fdk_aac.cpp
    src/transcode/encoder_openh264.cpp
    src/transcode/encoder_fdk_aac.cpp
    src/transcode/scaler_libyuv.cpp
    src/transcode/transcoder.cpp
)

set(ALL_LIB_SOURCES
    ${CORE_SOURCES}
    ${NET_SOURCES}
    ${RTMP_SOURCES}
    ${RELAY_SOURCES}
    ${TRANSCODE_SOURCES}
)

add_executable(rtmp_abr_transcoder
    src/main.cpp
    ${ALL_LIB_SOURCES}
)

target_link_libraries(rtmp_abr_transcoder
    pthread
    ${OPENH264_LIBRARIES}
    ${FDK_AAC_LIBRARIES}
    ${LIBYUV_LIBRARIES}
)

target_include_directories(rtmp_abr_transcoder PRIVATE
    ${OPENH264_INCLUDE_DIRS}
    ${FDK_AAC_INCLUDE_DIRS}
    ${LIBYUV_INCLUDE_DIRS}
)

add_subdirectory(tests)
