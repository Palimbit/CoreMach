# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 CoreMach contributors
# Export immutable historical source ONLY into the build's test fixtures.
include_guard(GLOBAL)
find_package(Git REQUIRED)
function(coremach_classic_test_reference source_path output)
    set(origin "a71e403189bd516c0f25c19014ee9748b2793d0d")
    set(local_origin "${origin}")
    set(map "${PROJECT_SOURCE_DIR}/provenance/history-commit-map.txt")
    if(EXISTS "${map}")
        file(STRINGS "${map}" row REGEX "^${origin} ")
        if(NOT row)
            message(FATAL_ERROR "Historical test origin is missing from commit map")
        endif()
        string(REGEX REPLACE "^[0-9a-f]+ " "" local_origin "${row}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" show "${local_origin}:${source_path}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE content
        ERROR_VARIABLE failure
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "Cannot read immutable test reference ${origin}:${source_path}. "
            "Use a full Git clone (not a shallow clone or source ZIP). ${failure}")
    endif()
    set(path "${PROJECT_BINARY_DIR}/classic-test-reference/${source_path}")
    get_filename_component(directory "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${path}" "${content}")
    set(${output} "${path}" PARENT_SCOPE)
endfunction()
