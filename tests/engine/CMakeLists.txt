# SPDX-License-Identifier: GPL-2.0-or-later

add_library(portable-failure-injection-host STATIC failure_injection_host.c)
target_link_libraries(portable-failure-injection-host PUBLIC blumach_engine)
target_compile_features(portable-failure-injection-host PRIVATE c_std_11)
set_target_properties(portable-failure-injection-host PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(portable-failure-injection-host PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(portable-failure-injection-host
        PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()

add_executable(portable-engine-contract synthetic_machine_test.c)
target_link_libraries(portable-engine-contract
    PRIVATE
        blumach_runtime
        blumach_bus
        blumach_platform_null
        portable-failure-injection-host
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

add_executable(portable-runtime-session-contracts runtime_session_contract_test.c)
target_link_libraries(portable-runtime-session-contracts
    PRIVATE
        blumach_runtime
        blumach_platform_null
        portable-failure-injection-host
)

add_executable(portable-runtime-machine-registry-contracts
    machine_registry_contract_test.c
)
target_link_libraries(portable-runtime-machine-registry-contracts
    PRIVATE
        blumach_runtime
        blumach_platform_null
        portable-failure-injection-host
)

add_executable(portable-engine-memory linear_memory_test.c)
target_link_libraries(portable-engine-memory
    PRIVATE blumach_memory blumach_platform_null
)

add_executable(portable-engine-bus-contracts bus_contract_test.c)
target_link_libraries(portable-engine-bus-contracts
    PRIVATE blumach_bus blumach_platform_null portable-failure-injection-host
)

add_executable(portable-engine-scheduler-contracts engine_scheduler_contract_test.c)
target_link_libraries(portable-engine-scheduler-contracts
    PRIVATE blumach_engine blumach_platform_null
)

add_executable(portable-host-capability-contracts host_capability_contract_test.c)
target_link_libraries(portable-host-capability-contracts
    PRIVATE blumach_engine blumach_platform_null
)

add_executable(portable-engine-pcs86-reset pcs86_reset_test.c)
target_link_libraries(portable-engine-pcs86-reset
    PRIVATE
        blumach_system_olivetti_pcs86
        blumach_platform_null
        portable-failure-injection-host
)

add_executable(portable-engine-pcs86-user-io pcs86_user_io_test.c)
target_link_libraries(portable-engine-pcs86-user-io
    PRIVATE blumach_system_olivetti_pcs86 blumach_platform_null
)

add_executable(portable-engine-pc-platform pc_platform_test.c)
target_link_libraries(portable-engine-pc-platform
    PRIVATE blumach_pc_components blumach_platform_null
)

add_executable(portable-engine-dma8237 dma8237_test.c)
target_link_libraries(portable-engine-dma8237
    PRIVATE blumach_pc_components blumach_memory blumach_platform_null
)

add_library(portable-engine-fdc765-test-support STATIC fdc765_test_harness.c)
target_link_libraries(portable-engine-fdc765-test-support
    PUBLIC
        blumach_pc_components
        blumach_storage
        blumach_memory
        blumach_platform_null
)
target_compile_features(portable-engine-fdc765-test-support PRIVATE c_std_11)
set_target_properties(portable-engine-fdc765-test-support
    PROPERTIES C_EXTENSIONS OFF
)
if(MSVC)
    target_compile_options(portable-engine-fdc765-test-support
        PRIVATE /W4 /WX /UNDEBUG
    )
else()
    target_compile_options(portable-engine-fdc765-test-support
        PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()

add_executable(portable-engine-fdc765 fdc765_test.c)
target_link_libraries(portable-engine-fdc765
    PRIVATE portable-engine-fdc765-test-support
)

add_executable(portable-engine-fdc765-contracts fdc765_contract_test.c)
target_link_libraries(portable-engine-fdc765-contracts
    PRIVATE portable-engine-fdc765-test-support
)

add_executable(portable-engine-storage-contracts storage_contract_test.c)
target_link_libraries(portable-engine-storage-contracts
    PRIVATE blumach_storage blumach_platform_null
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

add_library(portable-engine-808x-test-support STATIC cpu_808x_test_harness.c)
target_link_libraries(portable-engine-808x-test-support
    PUBLIC
        blumach_cpu_808x
        blumach_memory
        blumach_engine
        blumach_platform_null
)
target_compile_features(portable-engine-808x-test-support PRIVATE c_std_11)
set_target_properties(portable-engine-808x-test-support
    PROPERTIES C_EXTENSIONS OFF
)
if(MSVC)
    target_compile_options(portable-engine-808x-test-support
        PRIVATE /W4 /WX /UNDEBUG
    )
else()
    target_compile_options(portable-engine-808x-test-support
        PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()

add_executable(portable-engine-808x-post cpu_808x_post_test.c)
target_link_libraries(portable-engine-808x-post
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-checksum cpu_808x_checksum_test.c)
target_link_libraries(portable-engine-808x-checksum
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-segments cpu_808x_segment_test.c)
target_link_libraries(portable-engine-808x-segments
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-compare cpu_808x_compare_test.c)
target_link_libraries(portable-engine-808x-compare
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-shifts cpu_808x_shift_test.c)
target_link_libraries(portable-engine-808x-shifts
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-returns cpu_808x_return_test.c)
target_link_libraries(portable-engine-808x-returns
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-sign-extend cpu_808x_sign_extend_test.c)
target_link_libraries(portable-engine-808x-sign-extend
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-string-compare cpu_808x_string_compare_test.c)
target_link_libraries(portable-engine-808x-string-compare
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-trace cpu_808x_trace_test.c)
target_link_libraries(portable-engine-808x-trace
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-indirect-far-call cpu_808x_indirect_far_call_test.c)
target_link_libraries(portable-engine-808x-indirect-far-call
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-pop-rm cpu_808x_pop_rm_test.c)
target_link_libraries(portable-engine-808x-pop-rm
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-exchange cpu_808x_exchange_test.c)
target_link_libraries(portable-engine-808x-exchange
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-negate cpu_808x_negate_test.c)
target_link_libraries(portable-engine-808x-negate
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-xlat cpu_808x_xlat_test.c)
target_link_libraries(portable-engine-808x-xlat
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-immediate-carry cpu_808x_immediate_carry_test.c)
target_link_libraries(portable-engine-808x-immediate-carry
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-byte-divide cpu_808x_byte_divide_test.c)
target_link_libraries(portable-engine-808x-byte-divide
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-reverse-carry cpu_808x_reverse_carry_test.c)
target_link_libraries(portable-engine-808x-reverse-carry
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-adjust cpu_808x_adjust_test.c)
target_link_libraries(portable-engine-808x-adjust
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-base-isa cpu_808x_base_isa_test.c)
target_link_libraries(portable-engine-808x-base-isa
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-signed-math cpu_808x_signed_math_test.c)
target_link_libraries(portable-engine-808x-signed-math
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-80186-stack cpu_808x_80186_stack_test.c)
target_link_libraries(portable-engine-808x-80186-stack
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-80186-control cpu_808x_80186_control_test.c)
target_link_libraries(portable-engine-808x-80186-control
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-80186-io-string cpu_808x_80186_io_string_test.c)
target_link_libraries(portable-engine-808x-80186-io-string
    PRIVATE portable-engine-808x-test-support
)

add_executable(portable-engine-808x-interrupt-boundaries cpu_808x_interrupt_boundary_test.c)
target_link_libraries(portable-engine-808x-interrupt-boundaries
    PRIVATE portable-engine-808x-test-support
)

# Manual conformance adapter. External hardware vectors remain outside the
# repository and are supplied to tools/run_v20_conformance.py by path.
add_executable(portable-engine-808x-vector-runner cpu_808x_vector_runner.c)
target_link_libraries(portable-engine-808x-vector-runner
    PRIVATE portable-engine-808x-test-support
)

# Manual, ROM-free diagnostic utility. It is intentionally not a CTest because
# firmware remains a local-only input and must never be present in CI artifacts.
add_executable(portable-engine-pcs86-firmware-probe pcs86_firmware_probe.c)
target_link_libraries(portable-engine-pcs86-firmware-probe
    PRIVATE blumach_system_olivetti_pcs86 blumach_platform_null
)

add_executable(portable-engine-legacy-io legacy_io_test.c)
target_link_libraries(portable-engine-legacy-io
    PRIVATE blumach_pc_components blumach_platform_null
)

foreach(target portable-runtime-session-contracts portable-runtime-machine-registry-contracts portable-engine-memory portable-engine-bus-contracts portable-engine-scheduler-contracts portable-host-capability-contracts portable-engine-pcs86-reset portable-engine-pcs86-user-io portable-engine-pc-platform portable-engine-dma8237 portable-engine-fdc765 portable-engine-fdc765-contracts portable-engine-storage-contracts portable-engine-dma-pages portable-engine-mm58167-irq portable-engine-pvga1a portable-engine-808x-post portable-engine-808x-checksum portable-engine-808x-segments portable-engine-808x-compare portable-engine-808x-shifts portable-engine-808x-returns portable-engine-808x-sign-extend portable-engine-808x-string-compare portable-engine-808x-trace portable-engine-808x-indirect-far-call portable-engine-808x-pop-rm portable-engine-808x-exchange portable-engine-808x-negate portable-engine-808x-xlat portable-engine-808x-immediate-carry portable-engine-808x-byte-divide portable-engine-808x-reverse-carry portable-engine-808x-adjust portable-engine-808x-base-isa portable-engine-808x-signed-math portable-engine-808x-80186-stack portable-engine-808x-80186-control portable-engine-808x-80186-io-string portable-engine-808x-interrupt-boundaries portable-engine-808x-vector-runner portable-engine-legacy-io)
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
add_test(NAME portable-engine.bus-contracts COMMAND portable-engine-bus-contracts)
add_test(NAME portable-runtime.session-contracts COMMAND portable-runtime-session-contracts)
add_test(NAME portable-runtime.machine-registry-contracts
    COMMAND portable-runtime-machine-registry-contracts
)
add_test(NAME portable-engine.scheduler-contracts COMMAND portable-engine-scheduler-contracts)
add_test(NAME portable-host.capability-contracts COMMAND portable-host-capability-contracts)
add_test(NAME portable-engine.pcs86-reset-vector COMMAND portable-engine-pcs86-reset)
add_test(NAME portable-engine.pcs86-user-io COMMAND portable-engine-pcs86-user-io)
add_test(NAME portable-engine.pc-platform COMMAND portable-engine-pc-platform)
add_test(NAME portable-engine.dma8237-programming COMMAND portable-engine-dma8237)
add_test(NAME portable-engine.fdc765-dma COMMAND portable-engine-fdc765)
add_test(NAME portable-engine.fdc765-contracts COMMAND portable-engine-fdc765-contracts)
add_test(NAME portable-engine.storage-contracts COMMAND portable-engine-storage-contracts)
add_test(NAME portable-engine.dma-page-registers COMMAND portable-engine-dma-pages)
add_test(NAME portable-engine.mm58167-irq-control COMMAND portable-engine-mm58167-irq)
add_test(NAME portable-engine.pvga1a-registers-memory COMMAND portable-engine-pvga1a)
add_test(NAME portable-engine.808x-post-self-test COMMAND portable-engine-808x-post)
add_test(NAME portable-engine.808x-rom-checksum COMMAND portable-engine-808x-checksum)
add_test(NAME portable-engine.808x-segment-overrides COMMAND portable-engine-808x-segments)
add_test(NAME portable-engine.808x-byte-compare COMMAND portable-engine-808x-compare)
add_test(NAME portable-engine.808x-rotate-shift-groups COMMAND portable-engine-808x-shifts)
add_test(NAME portable-engine.808x-return-forms COMMAND portable-engine-808x-returns)
add_test(NAME portable-engine.808x-sign-extension COMMAND portable-engine-808x-sign-extend)
add_test(NAME portable-engine.808x-string-compare COMMAND portable-engine-808x-string-compare)
add_test(NAME portable-engine.808x-decode-trace COMMAND portable-engine-808x-trace)
add_test(NAME portable-engine.808x-indirect-far-control-flow COMMAND portable-engine-808x-indirect-far-call)
add_test(NAME portable-engine.808x-pop-rm COMMAND portable-engine-808x-pop-rm)
add_test(NAME portable-engine.808x-exchange COMMAND portable-engine-808x-exchange)
add_test(NAME portable-engine.808x-negate COMMAND portable-engine-808x-negate)
add_test(NAME portable-engine.808x-xlat COMMAND portable-engine-808x-xlat)
add_test(NAME portable-engine.808x-immediate-carry COMMAND portable-engine-808x-immediate-carry)
add_test(NAME portable-engine.808x-byte-divide COMMAND portable-engine-808x-byte-divide)
add_test(NAME portable-engine.808x-reverse-carry COMMAND portable-engine-808x-reverse-carry)
add_test(NAME portable-engine.808x-adjust COMMAND portable-engine-808x-adjust)
add_test(NAME portable-engine.808x-base-isa COMMAND portable-engine-808x-base-isa)
add_test(NAME portable-engine.808x-signed-math COMMAND portable-engine-808x-signed-math)
add_test(NAME portable-engine.808x-80186-stack COMMAND portable-engine-808x-80186-stack)
add_test(NAME portable-engine.808x-80186-control COMMAND portable-engine-808x-80186-control)
add_test(NAME portable-engine.808x-80186-io-string COMMAND portable-engine-808x-80186-io-string)
add_test(NAME portable-engine.808x-interrupt-boundaries COMMAND portable-engine-808x-interrupt-boundaries)
add_test(NAME portable-engine.legacy-io COMMAND portable-engine-legacy-io)
set_tests_properties(
    portable-engine.linear-memory
    portable-engine.bus-contracts
    portable-runtime.session-contracts
    portable-runtime.machine-registry-contracts
    portable-engine.scheduler-contracts
    portable-host.capability-contracts
    portable-engine.pcs86-reset-vector
    portable-engine.pcs86-user-io
    portable-engine.pc-platform
    portable-engine.dma8237-programming
    portable-engine.fdc765-dma
    portable-engine.fdc765-contracts
    portable-engine.storage-contracts
    portable-engine.dma-page-registers
    portable-engine.mm58167-irq-control
    portable-engine.pvga1a-registers-memory
    portable-engine.808x-post-self-test
    portable-engine.808x-rom-checksum
    portable-engine.808x-segment-overrides
    portable-engine.808x-byte-compare
    portable-engine.808x-rotate-shift-groups
    portable-engine.808x-return-forms
    portable-engine.808x-sign-extension
    portable-engine.808x-string-compare
    portable-engine.808x-decode-trace
    portable-engine.808x-indirect-far-control-flow
    portable-engine.808x-pop-rm
    portable-engine.808x-exchange
    portable-engine.808x-negate
    portable-engine.808x-xlat
    portable-engine.808x-immediate-carry
    portable-engine.808x-byte-divide
    portable-engine.808x-reverse-carry
    portable-engine.808x-adjust
    portable-engine.808x-base-isa
    portable-engine.808x-signed-math
    portable-engine.808x-80186-stack
    portable-engine.808x-80186-control
    portable-engine.808x-80186-io-string
    portable-engine.808x-interrupt-boundaries
    portable-engine.legacy-io
    PROPERTIES TIMEOUT 10
)
