cmake_minimum_required(VERSION 3.20)
project(AudioDawEngine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)

find_package(Python3 COMPONENTS Development REQUIRED)

add_executable(audio_engine src/main.c)

target_include_directories(audio_engine PRIVATE
    include
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(audio_engine PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::LUA
    Python3::Python
)
