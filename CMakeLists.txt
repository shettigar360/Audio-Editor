cmake_minimum_required(VERSION 3.20)
project(AudioEditor C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------
# Source Files & Targets
# ---------------------------------------------------------
file(GLOB_RECURSE SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(audio_engine ${SRC_FILES})

# ---------------------------------------------------------
# Include Directories
# ---------------------------------------------------------
target_include_directories(audio_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------
# Dependencies: SDL2
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
# Dependencies: FFmpeg (avformat, avcodec, swscale, avutil)
# ---------------------------------------------------------
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libswscale libswresample libavutil)
endif()

if(FFMPEG_FOUND)
    target_include_directories(audio_engine PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(audio_engine PRIVATE ${FFMPEG_LIBRARIES})
else()
    # Fallback direct linking for standard MinGW / GCC environments
    target_link_libraries(audio_engine PRIVATE
        avformat
        avcodec
        swscale
        swresample
        avutil
    )
endif()

# ---------------------------------------------------------
# Platform-Specific Linker Libraries & Flags
# ---------------------------------------------------------
if(WIN32)
    # Leave console active in DEBUG builds to view FFmpeg logs; hide only in RELEASE
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(audio_engine PRIVATE -mwindows)
    endif()

    target_compile_definitions(audio_engine PRIVATE 
        NOMINMAX 
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(audio_engine PRIVATE 
        dwmapi
        user32
        gdi32
        winmm
        shell32
        comdlg32
    )

    # Automatically copy runtime DLLs to binary directory on successful build
    add_custom_command(TARGET audio_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/bin"
        $<TARGET_FILE_DIR:audio_engine>
        COMMENT "Copying necessary runtime DLLs to build directory..."
    )
else()
    target_link_libraries(audio_engine PRIVATE 
        m 
        pthread 
        dl
    )
endif()

# ---------------------------------------------------------
# Output Directory Configuration
# ---------------------------------------------------------
set_target_properties(audio_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)
