# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(blumach_headless
    src/main.c
    src/machine_runner.c
    src/scenario_file.c
    src/text_input.c
)
target_link_libraries(blumach_headless PRIVATE
    blumach_frontend
    blumach_frontend_files
    blumach_runtime
    blumach_platform_null
)
target_compile_features(blumach_headless PRIVATE c_std_11)
set_target_properties(blumach_headless PROPERTIES C_EXTENSIONS OFF OUTPUT_NAME BluMach-headless)

if(MSVC)
    target_compile_options(blumach_headless PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_headless PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(blumach_headless_text_input_test
        src/text_input.c
        tests/text_input_test.c
    )
    target_include_directories(blumach_headless_text_input_test PRIVATE src)
    target_link_libraries(blumach_headless_text_input_test PRIVATE
        blumach_runtime
    )
    target_compile_features(blumach_headless_text_input_test PRIVATE c_std_11)
    if(MSVC)
        target_compile_options(blumach_headless_text_input_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_headless_text_input_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-headless.text-input
        COMMAND blumach_headless_text_input_test
    )
    add_executable(blumach_headless_scenario_file_test
        src/scenario_file.c
        src/text_input.c
        tests/scenario_file_test.c
    )
    target_include_directories(blumach_headless_scenario_file_test PRIVATE src)
    target_link_libraries(blumach_headless_scenario_file_test PRIVATE
        blumach_runtime
    )
    target_compile_features(blumach_headless_scenario_file_test PRIVATE c_std_11)
    if(MSVC)
        target_compile_options(blumach_headless_scenario_file_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_headless_scenario_file_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-headless.scenario-file
        COMMAND blumach_headless_scenario_file_test
    )
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
            "id=olivetti-pcs86 scheduler_hz=2000000 config_type=blumach.system.olivetti-pcs86.config"
    )
    add_test(NAME portable-headless.reject-unknown-machine
        COMMAND blumach_headless --describe not-a-machine
    )
    set_tests_properties(portable-headless.reject-unknown-machine PROPERTIES
        WILL_FAIL TRUE
    )
endif()
