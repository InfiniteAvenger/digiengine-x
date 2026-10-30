# Builds a portable Windows folder and zip of the game: DigiEngineX.exe with
# its DLLs and packs, runnable anywhere by double-clicking.
# Run by the `package` target (cmake --build build --target package); set
# VPET_DIST_PACKS (e.g. "demo") to ship only some packs, as for a public zip.
#
#   cmake -DEXE=... -DDLLS="a.dll|b.dll" -DPACKS_DIR=... -DOUT=... [-DONLY=demo,other] -P package_windows.cmake

string(REPLACE "|" ";" DLLS "${DLLS}")
string(REPLACE "," ";" ONLY "${ONLY}")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/packs")
file(COPY_FILE "${EXE}" "${OUT}/DigiEngineX.exe")
foreach(dll IN LISTS DLLS)
    file(COPY "${dll}" DESTINATION "${OUT}")
endforeach()

# Only real packs (folders with a pack.json); source folders stay out.
file(GLOB entries LIST_DIRECTORIES true "${PACKS_DIR}/*")
set(shipped "")
foreach(dir IN LISTS entries)
    get_filename_component(name "${dir}" NAME)
    if(NOT EXISTS "${dir}/pack.json")
        continue()
    endif()
    if(ONLY AND NOT name IN_LIST ONLY)
        continue()
    endif()
    file(COPY "${dir}" DESTINATION "${OUT}/packs")
    list(APPEND shipped "${name}")
endforeach()
if(NOT shipped)
    message(FATAL_ERROR "no packs to package (only: ${ONLY})")
endif()

file(WRITE "${OUT}/README.txt"
"DigiEngine X\n\n"
"Double-click DigiEngineX.exe to play.\n\n"
"Keys: A = Z / 1 / Left, B = X / 2 / Down, C = C / 3 / Right. Esc quits.\n"
"Hold A+C for 3 seconds for settings.\n\n"
"Packs live in the packs folder; copy a pack folder (with pack.json) in to add one.\n"
"Saves are in %APPDATA%\\vpet\\vpet\\.\n")

get_filename_component(parent "${OUT}" DIRECTORY)
get_filename_component(folder "${OUT}" NAME)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUT}.zip" --format=zip "${folder}"
                WORKING_DIRECTORY "${parent}" RESULT_VARIABLE zip_result)
if(NOT zip_result EQUAL 0)
    message(FATAL_ERROR "zipping failed")
endif()
string(REPLACE ";" ", " shipped "${shipped}")
message(STATUS "Packaged ${OUT} and ${OUT}.zip with packs: ${shipped}")
