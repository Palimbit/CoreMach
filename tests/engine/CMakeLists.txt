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

add_executable(portable-engine-memory linear_memory_test.c)
target_link_libraries(portable-engine-memory
    PRIVATE blumach_memory blumach_platform_null
)

add_executable(portable-engine-pcs86-reset pcs86_reset_test.c)
target_link_libraries(portable-engine-pcs86-reset
    PRIVATE blumach_system_olivetti_pcs86 blumach_platform_null
)

foreach(target portable-engine-memory portable-engine-pcs86-reset)
    target_compile_features(${target} PRIVATE c_std_11)
    set_target_properties(${target} PROPERTIES C_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    endif()
endforeach()

add_test(NAME portable-engine.linear-memory COMMAND portable-engine-memory)
add_test(NAME portable-engine.pcs86-reset-vector COMMAND portable-engine-pcs86-reset)
set_tests_properties(
    portable-engine.linear-memory
    portable-engine.pcs86-reset-vector
    PROPERTIES TIMEOUT 10
)
