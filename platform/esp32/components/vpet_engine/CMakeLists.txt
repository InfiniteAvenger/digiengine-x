# The portable engine, compiled unchanged from the repo's engine/ folder.
set(root ${CMAKE_CURRENT_LIST_DIR}/../../../..)
# New engine files need `idf.py reconfigure` (ESP-IDF evaluates this file in script mode).
file(GLOB srcs ${root}/engine/src/*.cpp)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${root}/engine/include
    PRIV_INCLUDE_DIRS ${root}/engine/src)
