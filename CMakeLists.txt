cmake_minimum_required(VERSION 3.20)
project(AudioEditor C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswresample libswscale)

# Use pkg_search_module to pick whichever Lua pkg-config name exists
pkg_search_module(LUA REQUIRED lua lua5.4 lua-5.4 lua54)

add_executable(audio_editor src/main.c)

target_include_directories(audio_editor PRIVATE 
    include 
    ${SDL2_INCLUDE_DIRS} 
    ${FFMPEG_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
)

target_link_libraries(audio_editor PRIVATE 
    ${SDL2_LIBRARIES} 
    ${FFMPEG_LIBRARIES}
    ${LUA_LIBRARIES}
)

if(WIN32)
    target_link_options(audio_editor PRIVATE -mwindows)
    target_link_libraries(audio_editor PRIVATE comdlg32 dwmapi m)
endif()
