# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_frontend STATIC
    src/frontend.c
    src/pcs86_adapter.c
    src/m15_adapter.c
)
target_include_directories(blumach_frontend PUBLIC include PRIVATE src)
target_link_libraries(blumach_frontend PUBLIC
    blumach_runtime
    PRIVATE blumach_system_olivetti_pcs86
            blumach_system_olivetti_m15
)
target_compile_features(blumach_frontend PUBLIC c_std_11)
set_target_properties(blumach_frontend PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_frontend PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_frontend PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_library(blumach_frontend_files STATIC src/file_inputs.c)
target_include_directories(blumach_frontend_files PUBLIC include)
target_link_libraries(blumach_frontend_files PUBLIC blumach_engine)
target_compile_features(blumach_frontend_files PUBLIC c_std_11)
set_target_properties(blumach_frontend_files PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(blumach_frontend_files PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_frontend_files PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_TESTING)
    add_executable(blumach_frontend_test tests/frontend_test.c)
    target_link_libraries(blumach_frontend_test PRIVATE
        blumach_frontend
        blumach_platform_null
        blumach_system_olivetti_pcs86
    )
    target_compile_features(blumach_frontend_test PRIVATE c_std_11)
    set_target_properties(blumach_frontend_test PROPERTIES C_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(blumach_frontend_test PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(blumach_frontend_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-frontend.adapters COMMAND blumach_frontend_test)

    add_executable(blumach_frontend_file_inputs_test tests/file_inputs_test.c)
    target_link_libraries(blumach_frontend_file_inputs_test PRIVATE
        blumach_frontend_files
    )
    target_compile_features(blumach_frontend_file_inputs_test PRIVATE c_std_11)
    set_target_properties(blumach_frontend_file_inputs_test PROPERTIES
        C_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_frontend_file_inputs_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_frontend_file_inputs_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-frontend.file-inputs
        COMMAND blumach_frontend_file_inputs_test
            "${CMAKE_CURRENT_BINARY_DIR}/portable-frontend-media-test.img"
    )
endif()
