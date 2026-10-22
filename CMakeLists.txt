cmake_minimum_required(VERSION 3.20)
project(AudioEditor C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

file(GLOB_RECURSE SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
    list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
endif()

add_executable(audio_engine ${SRC_FILES})

find_package(SDL2 REQUIRED)

if(TARGET SDL2::SDL2)
    target_link_libraries(audio_engine PRIVATE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(audio_engine PRIVATE SDL2::SDL2-static)
else()
    target_include_directories(audio_engine PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(audio_engine PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32)
    target_link_options(audio_engine PRIVATE -mwindows)
    target_link_libraries(audio_engine PRIVATE 
        dwmapi
        user32
        gdi32
        winmm
        shell32
        comdlg32
        avformat
        avcodec
        swscale
        swresample
        avutil
    )
    target_compile_definitions(audio_engine PRIVATE NOMINMAX)
else()
    target_link_libraries(audio_engine PRIVATE 
        m 
        pthread 
        dl
        avformat
        avcodec
        swscale
        swresample
        avutil
    )
endif()

set_target_properties(audio_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)
