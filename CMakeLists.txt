cmake_minimum_required(VERSION 3.20)
project(AudioVideoEditor C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_package(Python3 COMPONENTS Development REQUIRED)

add_executable(audio_editor src/main.c)

target_include_directories(audio_editor PRIVATE
    include
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(audio_editor PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::LUA
    PkgConfig::SDL2
    Python3::Python
)
