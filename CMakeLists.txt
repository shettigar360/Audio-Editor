cmake_minimum_required(VERSION 3.20)
project(AudioVideoEditor C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)

# 1. FFmpeg
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)

# 2. Lua (standard CMake module search, independent of pkg-config names)
find_package(Lua REQUIRED)

# 3. SDL2 (for video popup window)
find_package(SDL2 REQUIRED)

# 4. Embedded Python
find_package(Python3 COMPONENTS Development REQUIRED)

add_executable(audio_editor src/main.c)

target_include_directories(audio_editor PRIVATE
    include
    ${LUA_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(audio_editor PRIVATE
    PkgConfig::FFMPEG
    ${LUA_LIBRARIES}
    ${SDL2_LIBRARIES}
    Python3::Python
)
