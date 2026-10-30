# Native part of the Android app: SDL2 (built from its source folder) and
# libmain.so with the engine, the handheld shell and the SDL frontend.
cmake_minimum_required(VERSION 3.18)
project(vpet_android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT SDL2_SOURCE_DIR)
    message(FATAL_ERROR "SDL2_SOURCE_DIR is not set (sdl2.dir in local.properties)")
endif()
add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2)

set(VPET_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
file(GLOB VPET_ENGINE_SOURCES ${VPET_ROOT}/engine/src/*.cpp)

# SDLActivity loads "main" and calls SDL_main (SDL.h renames main()).
add_library(main SHARED
    ${VPET_ENGINE_SOURCES}
    ${VPET_ROOT}/platform/shell/shell.cpp
    ${VPET_ROOT}/platform/sdl/main.cpp)
target_include_directories(main PRIVATE
    ${VPET_ROOT}/engine/include
    ${VPET_ROOT}/engine/src
    ${VPET_ROOT}/platform/shell)
target_link_libraries(main PRIVATE SDL2 SDL2main)
