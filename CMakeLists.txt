cmake_minimum_required(VERSION 3.16)

project(rpi5_av1_decoder LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(RPI5_AV1_USE_LOCAL_FFMPEG "Build against a local FFmpeg installation" OFF)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    add_compile_options(-O3 -Wall -Wextra -Wpedantic)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        add_compile_options(-mcpu=native)
    endif()
endif()

find_package(PkgConfig REQUIRED)

# Prefer libdav1d / FFmpeg on Raspberry Pi 5.
# This project is CPU-only: AV1 hardware decode is not available through
# the VideoCore VII pipeline on Raspberry Pi OS / BCM2712 at present.
if(RPI5_AV1_USE_LOCAL_FFMPEG)
    find_package(FFMPEG REQUIRED)
    set(FFMPEG_LIBS ${FFMPEG_LIBRARIES})
    include_directories(${FFMPEG_INCLUDE_DIRS})
else()
    pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(DAV1D REQUIRED IMPORTED_TARGET dav1d)
    set(FFMPEG_LIBS
        PkgConfig::AVCODEC
        PkgConfig::AVFORMAT
        PkgConfig::AVUTIL
        PkgConfig::DAV1D
    )
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(rpi5_av1_decoder
    src/main.c
    src/av1_decoder.c
)

target_link_libraries(rpi5_av1_decoder PRIVATE ${FFMPEG_LIBS})

target_include_directories(rpi5_av1_decoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
