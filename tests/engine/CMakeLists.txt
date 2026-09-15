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

add_executable(portable-engine-pc-platform pc_platform_test.c)
target_link_libraries(portable-engine-pc-platform
    PRIVATE blumach_pc_components blumach_platform_null
)

add_executable(portable-engine-808x-post cpu_808x_post_test.c)
target_link_libraries(portable-engine-808x-post
    PRIVATE
        blumach_cpu_808x
        blumach_memory
        blumach_engine
        blumach_platform_null
)

add_executable(portable-engine-808x-checksum cpu_808x_checksum_test.c)
target_link_libraries(portable-engine-808x-checksum
    PRIVATE
        blumach_cpu_808x
        blumach_memory
        blumach_engine
        blumach_platform_null
)

# Manual, ROM-free diagnostic utility. It is intentionally not a CTest because
# firmware remains a local-only input and must never be present in CI artifacts.
add_executable(portable-engine-pcs86-firmware-probe pcs86_firmware_probe.c)
target_link_libraries(portable-engine-pcs86-firmware-probe
    PRIVATE blumach_system_olivetti_pcs86 blumach_platform_null
)

foreach(target portable-engine-memory portable-engine-pcs86-reset portable-engine-pc-platform portable-engine-808x-post portable-engine-808x-checksum)
    target_compile_features(${target} PRIVATE c_std_11)
    set_target_properties(${target} PROPERTIES C_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    endif()
endforeach()

target_compile_features(portable-engine-pcs86-firmware-probe PRIVATE c_std_11)
set_target_properties(portable-engine-pcs86-firmware-probe PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(portable-engine-pcs86-firmware-probe PRIVATE /W4 /WX)
else()
    target_compile_options(portable-engine-pcs86-firmware-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME portable-engine.linear-memory COMMAND portable-engine-memory)
add_test(NAME portable-engine.pcs86-reset-vector COMMAND portable-engine-pcs86-reset)
add_test(NAME portable-engine.pc-platform COMMAND portable-engine-pc-platform)
add_test(NAME portable-engine.808x-post-self-test COMMAND portable-engine-808x-post)
add_test(NAME portable-engine.808x-rom-checksum COMMAND portable-engine-808x-checksum)
set_tests_properties(
    portable-engine.linear-memory
    portable-engine.pcs86-reset-vector
    portable-engine.pc-platform
    portable-engine.808x-post-self-test
    portable-engine.808x-rom-checksum
    PROPERTIES TIMEOUT 10
)
