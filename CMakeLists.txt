cmake_minimum_required(VERSION 3.20)
project(AudioEditor C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------
# Include Directories
# ---------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------
# Source Files
# ---------------------------------------------------------
# Automatically picks up all .c files in src/ (main.c, daw_tabs.c, daw_render.c, etc.)
file(GLOB_RECURSE SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# ---------------------------------------------------------
# Executable Target
# ---------------------------------------------------------
add_executable(audio_engine ${SRC_FILES})

# ---------------------------------------------------------
# Find Dependencies
# ---------------------------------------------------------
find_package(SDL2 REQUIRED)

if(TARGET SDL2::SDL2)
    target_link_libraries(audio_engine PRIVATE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(audio_engine PRIVATE SDL2::SDL2-static)
else()
    target_include_directories(audio_engine PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(audio_engine PRIVATE ${SDL2_LIBRARIES})
endif()

# ---------------------------------------------------------
# Platform-Specific Linker Libraries & Flags
# ---------------------------------------------------------
if(WIN32)
    # Hide the black terminal window on startup
    target_link_options(audio_engine PRIVATE -mwindows)

    # Windows system libraries
    target_link_libraries(audio_engine PRIVATE 
        dwmapi
        user32
        gdi32
        winmm
        shell32
        comdlg32
    )

    # Prevent standard Windows min/max macro collisions
    target_compile_definitions(audio_engine PRIVATE NOMINMAX)
else()
    # Linux / macOS standard math and pthread libraries
    target_link_libraries(audio_engine PRIVATE 
        m 
        pthread 
        dl
    )
endif()

# ---------------------------------------------------------
# Optional: FFmpeg linking (if using external shared libraries)
# ---------------------------------------------------------
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libswscale libavutil libswresample)
    if(FFMPEG_FOUND)
        target_include_directories(audio_engine PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(audio_engine PRIVATE ${FFMPEG_LIBRARIES})
    endif()
endif()

# ---------------------------------------------------------
# Output Directory Configuration
# ---------------------------------------------------------
set_target_properties(audio_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)
