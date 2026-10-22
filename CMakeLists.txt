cmake_minimum_required(VERSION 3.20)
project(AudioEditor C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

file(GLOB_RECURSE SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# Generate clean studio waveform app_icon.ico (without top-right red recording dot) so windres always succeeds
if(WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "
import math, struct
px = []
for y in range(64):
    for x in range(64):
        r, g, b, a = 22, 20, 18, 255
        dx = (8 - x) if x < 8 else ((x - 55) if x > 55 else 0)
        dy = (8 - y) if y < 8 else ((y - 55) if y > 55 else 0)
        if dx*dx + dy*dy > 64:
            px.append(0)
            continue
        if x <= 2 or x >= 61 or y <= 2 or y >= 61 or (dx*dx + dy*dy >= 42):
            r, g, b = 215, 138, 62
        else:
            if y == 34: r, g, b = 90, 62, 38
            if 8 <= x <= 55:
                u = (x - 8) / 47.0
                h = int(math.sin(u * math.pi) * abs(math.sin(u * 18.85) * 0.7 + math.cos(u * 9.42) * 0.3) * 20.0) + 2
                if abs(y - 34) <= h:
                    r, g, b = (255, 185, 75) if abs(y - 34) <= h // 2 else (215, 125, 45)
        px.append((a << 24) | (r << 16) | (g << 8) | b)
with open(r'${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico', 'wb') as f:
    f.write(bytes([0,0,1,0,1,0,64,64,0,0,1,0,32,0,0x28,0x42,0,0,22,0,0,0]))
    f.write(bytes([40,0,0,0,64,0,0,0,128,0,0,0,1,0,32,0,0,0,0,0,0,0x42,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]))
    for row in range(63, -1, -1):
        for col in range(64):
            f.write(struct.pack('<I', px[row * 64 + col]))
    f.write(bytes(64 * 8))
")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico" DESTINATION "${CMAKE_BINARY_DIR}")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.ico")
        list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
    endif()
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
