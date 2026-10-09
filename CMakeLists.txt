cmake_minimum_required(VERSION 3.20)
project(AudioEditor C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)

# 1. Dependency Discovery
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswresample libswscale)
pkg_search_module(LUA REQUIRED lua lua5.4 lua-5.4 lua54)
pkg_search_module(PYTHON REQUIRED python3-embed python3 python-3.12 python-3.11)

# 2. Target Executable
add_executable(audio_editor src/main.c)

# 3. Header Includes
target_include_directories(audio_editor PRIVATE 
    include 
    ${SDL2_INCLUDE_DIRS} 
    ${FFMPEG_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
)

# 4. Link Libraries
target_link_libraries(audio_editor PRIVATE 
    ${SDL2_LIBRARIES} 
    ${FFMPEG_LIBRARIES}
    ${LUA_LIBRARIES}
    ${PYTHON_LIBRARIES}
)

# 5. Windows System Libraries (Console-Enabled for printf verification)
if(WIN32)
    # NOTE: Keep -mwindows commented out so you can see your printf statements.
    # When you are ready to ship the final GUI app without a black cmd box, uncomment it.
    # target_link_options(audio_editor PRIVATE -mwindows)

    target_link_libraries(audio_editor PRIVATE comdlg32 dwmapi m)
endif()
