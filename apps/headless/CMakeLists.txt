# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(blumach_headless
    src/file_inputs.c
    src/main.c
    src/pcs86_frontend.c
)
target_link_libraries(blumach_headless PRIVATE
    blumach_runtime
    blumach_platform_null
    blumach_system_olivetti_pcs86
)
target_compile_features(blumach_headless PRIVATE c_std_11)
set_target_properties(blumach_headless PROPERTIES C_EXTENSIONS OFF OUTPUT_NAME BluMach-headless)

if(MSVC)
    target_compile_options(blumach_headless PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_headless PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_test(NAME portable-headless.version COMMAND blumach_headless --version)
    set_tests_properties(portable-headless.version PROPERTIES
        PASS_REGULAR_EXPRESSION "BluMach portable engine"
    )
    add_test(NAME portable-headless.list-machines
        COMMAND blumach_headless --list-machines
    )
    set_tests_properties(portable-headless.list-machines PROPERTIES
        PASS_REGULAR_EXPRESSION "olivetti-pcs86"
    )
    add_test(NAME portable-headless.describe
        COMMAND blumach_headless --describe olivetti-pcs86
    )
    set_tests_properties(portable-headless.describe PROPERTIES
        PASS_REGULAR_EXPRESSION
            "id=olivetti-pcs86 config_type=blumach.system.olivetti-pcs86.config"
    )
    add_test(NAME portable-headless.reject-unknown-machine
        COMMAND blumach_headless --describe not-a-machine
    )
    set_tests_properties(portable-headless.reject-unknown-machine PROPERTIES
        WILL_FAIL TRUE
    )
endif()
