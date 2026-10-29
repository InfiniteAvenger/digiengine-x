# The portable engine, compiled unchanged from the repo's engine/ folder.
set(root ${CMAKE_CURRENT_LIST_DIR}/../../../..)
file(GLOB srcs ${root}/engine/src/*.cpp)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${root}/engine/include
    PRIV_INCLUDE_DIRS ${root}/engine/src)
