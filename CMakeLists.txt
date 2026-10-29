cmake_minimum_required(VERSION 3.21)
project(vpet CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable game core: no platform headers allowed in here.
add_library(vpet_engine STATIC
    engine/src/bag_scene.cpp
    engine/src/care_scenes.cpp
    engine/src/cryo_scene.cpp
    engine/src/font.cpp
    engine/src/framebuffer.cpp
    engine/src/game.cpp
    engine/src/garden_scene.cpp
    engine/src/home_scene.cpp
    engine/src/icons.cpp
    engine/src/json.cpp
    engine/src/minigame.cpp
    engine/src/png.cpp
    engine/src/pack.cpp
    engine/src/pet.cpp
    engine/src/quest_scene.cpp
    engine/src/rpg.cpp
    engine/src/scenery.cpp
    engine/src/settings_scene.cpp
    engine/src/sprite.cpp
)
target_include_directories(vpet_engine PUBLIC engine/include)
if(NOT MSVC)
    target_compile_options(vpet_engine PRIVATE -Wall -Wextra)
endif()

# Pack baker: PNG sheets + CSV tables -> pack folder (desktop tool only).
add_library(vpet_baker_lib STATIC tools/baker/baker.cpp)
target_link_libraries(vpet_baker_lib PUBLIC vpet_engine)
target_include_directories(vpet_baker_lib PUBLIC tools/baker PRIVATE engine/src)
add_executable(vpet_baker tools/baker/main.cpp)
target_link_libraries(vpet_baker PRIVATE vpet_baker_lib)
if(MINGW)
    target_link_options(vpet_baker PRIVATE -static)
endif()

# Headless screenshot runner for checking screens without a window.
add_executable(vpet_snapshot tools/snapshot/main.cpp)
target_link_libraries(vpet_snapshot PRIVATE vpet_engine vpet_baker_lib)
if(MINGW)
    target_link_options(vpet_snapshot PRIVATE -static)
endif()

option(VPET_BUILD_SDL "Build the SDL2 desktop frontend" ON)
if(VPET_BUILD_SDL)
    find_package(SDL2 REQUIRED CONFIG)
    # WIN32: a normal windowed app on Windows, with no console window.
    add_executable(vpet WIN32 platform/sdl/main.cpp)
    target_link_libraries(vpet PRIVATE vpet_engine SDL2::SDL2main SDL2::SDL2)
    if(MINGW)
        # Bundle the GCC runtime so the .exe runs outside the MSYS2 shell.
        target_link_options(vpet PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    endif()

    # Ship SDL2.dll next to the executable.
    add_custom_command(TARGET vpet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:vpet> $<TARGET_FILE_DIR:vpet>
        COMMAND_EXPAND_LISTS)

    # Copy all of packs/ on every build (not only when the game relinks), so
    # newly added or re-baked local packs always show up in the game.
    add_custom_target(copy_packs ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/packs $<TARGET_FILE_DIR:vpet>/packs
        COMMENT "Copying packs next to the game")
    add_dependencies(copy_packs vpet)
endif()
