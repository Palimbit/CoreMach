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

add_executable(portable-engine-dma8237 dma8237_test.c)
target_link_libraries(portable-engine-dma8237
    PRIVATE blumach_pc_components blumach_platform_null
)

add_executable(portable-engine-dma-pages dma_page_registers_test.c)
target_link_libraries(portable-engine-dma-pages
    PRIVATE blumach_pc_components blumach_platform_null
)

add_executable(portable-engine-mm58167-irq rtc_mm58167_test.c)
target_link_libraries(portable-engine-mm58167-irq
    PRIVATE blumach_pc_components blumach_platform_null
)

add_executable(portable-engine-pvga1a pvga1a_test.c)
target_link_libraries(portable-engine-pvga1a
    PRIVATE blumach_video blumach_platform_null
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

add_executable(portable-engine-808x-segments cpu_808x_segment_test.c)
target_link_libraries(portable-engine-808x-segments
    PRIVATE
        blumach_cpu_808x
        blumach_memory
        blumach_engine
        blumach_platform_null
)

add_executable(portable-engine-808x-compare cpu_808x_compare_test.c)
target_link_libraries(portable-engine-808x-compare
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

foreach(target portable-engine-memory portable-engine-pcs86-reset portable-engine-pc-platform portable-engine-dma8237 portable-engine-dma-pages portable-engine-mm58167-irq portable-engine-pvga1a portable-engine-808x-post portable-engine-808x-checksum portable-engine-808x-segments portable-engine-808x-compare)
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
add_test(NAME portable-engine.dma8237-programming COMMAND portable-engine-dma8237)
add_test(NAME portable-engine.dma-page-registers COMMAND portable-engine-dma-pages)
add_test(NAME portable-engine.mm58167-irq-control COMMAND portable-engine-mm58167-irq)
add_test(NAME portable-engine.pvga1a-registers-memory COMMAND portable-engine-pvga1a)
add_test(NAME portable-engine.808x-post-self-test COMMAND portable-engine-808x-post)
add_test(NAME portable-engine.808x-rom-checksum COMMAND portable-engine-808x-checksum)
add_test(NAME portable-engine.808x-segment-overrides COMMAND portable-engine-808x-segments)
add_test(NAME portable-engine.808x-byte-compare COMMAND portable-engine-808x-compare)
set_tests_properties(
    portable-engine.linear-memory
    portable-engine.pcs86-reset-vector
    portable-engine.pc-platform
    portable-engine.dma8237-programming
    portable-engine.dma-page-registers
    portable-engine.mm58167-irq-control
    portable-engine.pvga1a-registers-memory
    portable-engine.808x-post-self-test
    portable-engine.808x-rom-checksum
    portable-engine.808x-segment-overrides
    portable-engine.808x-byte-compare
    PROPERTIES TIMEOUT 10
)
