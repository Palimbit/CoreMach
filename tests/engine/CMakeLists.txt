# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(portable-engine-contract synthetic_machine_test.c)
target_link_libraries(portable-engine-contract
    PRIVATE
        blumach_runtime
        blumach_bus
        blumach_platform_null
)
target_compile_features(portable-engine-contract PRIVATE c_std_11)
set_target_properties(portable-engine-contract PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(portable-engine-contract PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(portable-engine-contract PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()

add_test(NAME portable-engine.synthetic-machine COMMAND portable-engine-contract)
set_tests_properties(portable-engine.synthetic-machine PROPERTIES TIMEOUT 10)
