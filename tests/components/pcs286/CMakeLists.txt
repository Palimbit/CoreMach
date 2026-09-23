# SPDX-License-Identifier: GPL-2.0-or-later
# Green harness tests are NOT evidence of implemented 286/Headland/AT chips.
option(BLUMACH_REQUIRE_PCS286_COMPONENTS
    "Fail configuration if a P0 component implementation is absent" OFF)

function(pcs286_test_options target)
    target_compile_features(${target} PRIVATE c_std_11)
    set_target_properties(${target} PROPERTIES C_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    endif()
endfunction()

add_library(pcs286-test-fixtures STATIC bus_fixture.c)
target_include_directories(pcs286-test-fixtures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pcs286-test-fixtures PUBLIC blumach_bus)
pcs286_test_options(pcs286-test-fixtures)

add_executable(pcs286-p0-foundation foundation_test.c)
target_include_directories(pcs286-p0-foundation PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_link_libraries(pcs286-p0-foundation PRIVATE pcs286-test-fixtures
    blumach_memory blumach_platform_null)
pcs286_test_options(pcs286-p0-foundation)
add_test(NAME pcs286-p0.foundation COMMAND pcs286-p0-foundation)
set_tests_properties(pcs286-p0.foundation PROPERTIES LABELS "pcs286-p0;harness" TIMEOUT 15)

add_executable(pcs286-component-pending pending.c)
pcs286_test_options(pcs286-component-pending)

function(pcs286_acceptance name source implementation)
    # Always compile the real acceptance source; never supply fake chip symbols.
    add_library(pcs286-${name}-spec OBJECT ${source})
    target_link_libraries(pcs286-${name}-spec PRIVATE
        blumach_system_olivetti_pcs286_contracts pcs286-test-fixtures
        portable-failure-injection-host blumach_platform_null)
    target_include_directories(pcs286-${name}-spec PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    pcs286_test_options(pcs286-${name}-spec)
    if(TARGET ${implementation})
        get_target_property(kind ${implementation} TYPE)
        if(kind STREQUAL "INTERFACE_LIBRARY")
            message(FATAL_ERROR "${implementation} must implement the component, not declarations")
        endif()
        add_executable(pcs286-${name} $<TARGET_OBJECTS:pcs286-${name}-spec>)
        target_link_libraries(pcs286-${name} PRIVATE ${implementation}
            pcs286-test-fixtures portable-failure-injection-host blumach_platform_null)
        add_test(NAME pcs286-component.${name} COMMAND pcs286-${name})
    else()
        if(BLUMACH_REQUIRE_PCS286_COMPONENTS)
            message(FATAL_ERROR "PCS286 component missing: ${implementation}")
        endif()
        add_test(NAME pcs286-component.${name}
            COMMAND pcs286-component-pending ${implementation})
        set_tests_properties(pcs286-component.${name} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    set_tests_properties(pcs286-component.${name} PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
endfunction()

pcs286_acceptance(cpu286 cpu286_acceptance.c blumach_cpu_80286)
if(TARGET blumach_cpu_80286)
    add_executable(pcs286-cpu-table-faults cpu286_table_faults.c)
    target_link_libraries(pcs286-cpu-table-faults PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-table-faults)
    add_test(NAME pcs286-component.cpu-table-faults COMMAND pcs286-cpu-table-faults)
    set_tests_properties(pcs286-component.cpu-table-faults PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-lock cpu286_lock.c)
    target_link_libraries(pcs286-cpu-lock PRIVATE
        blumach_cpu_80286 blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-cpu-lock)
    add_test(NAME pcs286-component.cpu-lock COMMAND pcs286-cpu-lock)
    set_tests_properties(pcs286-component.cpu-lock PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 60)
    add_executable(pcs286-cpu-string-io cpu286_string_io.c)
    target_link_libraries(pcs286-cpu-string-io PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-string-io)
    add_test(NAME pcs286-component.cpu-string-io COMMAND pcs286-cpu-string-io)
    set_tests_properties(pcs286-component.cpu-string-io PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-repeat cpu286_repeat.c)
    target_link_libraries(pcs286-cpu-repeat PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-repeat)
    add_test(NAME pcs286-component.cpu-repeat COMMAND pcs286-cpu-repeat)
    set_tests_properties(pcs286-component.cpu-repeat PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-strings cpu286_strings.c)
    target_link_libraries(pcs286-cpu-strings PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-strings)
    add_test(NAME pcs286-component.cpu-strings COMMAND pcs286-cpu-strings)
    set_tests_properties(pcs286-component.cpu-strings PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-decimal cpu286_decimal.c)
    target_link_libraries(pcs286-cpu-decimal PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-decimal)
    add_test(NAME pcs286-component.cpu-decimal COMMAND pcs286-cpu-decimal)
    set_tests_properties(pcs286-component.cpu-decimal PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-bound cpu286_bound.c)
    add_executable(pcs286-cpu-extension cpu286_extension.c)
    target_link_libraries(pcs286-cpu-extension PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-extension)
    add_test(NAME pcs286-component.cpu-extension COMMAND pcs286-cpu-extension)
    set_tests_properties(pcs286-component.cpu-extension PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    target_link_libraries(pcs286-cpu-bound PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-bound)
    add_test(NAME pcs286-component.cpu-bound COMMAND pcs286-cpu-bound)
    set_tests_properties(pcs286-component.cpu-bound PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-divide cpu286_divide.c)
    target_link_libraries(pcs286-cpu-divide PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-divide)
    add_test(NAME pcs286-component.cpu-divide COMMAND pcs286-cpu-divide)
    set_tests_properties(pcs286-component.cpu-divide PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-multiply cpu286_multiply.c)
    target_link_libraries(pcs286-cpu-multiply PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-multiply)
    add_test(NAME pcs286-component.cpu-multiply COMMAND pcs286-cpu-multiply)
    set_tests_properties(pcs286-component.cpu-multiply PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-shifts cpu286_shifts.c)
    target_link_libraries(pcs286-cpu-shifts PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-shifts)
    add_test(NAME pcs286-component.cpu-shifts COMMAND pcs286-cpu-shifts)
    set_tests_properties(pcs286-component.cpu-shifts PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-address-loads cpu286_address_loads.c)
    target_link_libraries(pcs286-cpu-address-loads PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-address-loads)
    add_test(NAME pcs286-component.cpu-address-loads COMMAND pcs286-cpu-address-loads)
    set_tests_properties(pcs286-component.cpu-address-loads PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-far-procedures cpu286_far_procedures.c)
    target_link_libraries(pcs286-cpu-far-procedures PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-far-procedures)
    add_test(NAME pcs286-component.cpu-far-procedures COMMAND pcs286-cpu-far-procedures)
    set_tests_properties(pcs286-component.cpu-far-procedures PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-flags-interrupts cpu286_flags_interrupts.c)
    target_link_libraries(pcs286-cpu-flags-interrupts PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-flags-interrupts)
    add_test(NAME pcs286-component.cpu-flags-interrupts COMMAND pcs286-cpu-flags-interrupts)
    set_tests_properties(pcs286-component.cpu-flags-interrupts PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-io cpu286_io.c)
    target_link_libraries(pcs286-cpu-io PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-io)
    add_test(NAME pcs286-component.cpu-io COMMAND pcs286-cpu-io)
    set_tests_properties(pcs286-component.cpu-io PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-ss-load cpu286_ss_load.c)
    target_link_libraries(pcs286-cpu-ss-load PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-ss-load)
    add_test(NAME pcs286-component.cpu-ss-load COMMAND pcs286-cpu-ss-load)
    set_tests_properties(pcs286-component.cpu-ss-load PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-far-jump cpu286_far_jump.c)
    target_link_libraries(pcs286-cpu-far-jump PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-far-jump)
    add_test(NAME pcs286-component.cpu-far-jump COMMAND pcs286-cpu-far-jump)
    set_tests_properties(pcs286-component.cpu-far-jump PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    # Test-only single-boundary probe. No downloaded data or parser in the core.
    add_executable(pcs286-sst-probe cpu286_sst_probe.c)
    target_link_libraries(pcs286-sst-probe PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-sst-probe)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME pcs286-component.sst-adapter
            COMMAND "${CMAKE_COMMAND}" -E env
                "BM_SST286_PROBE=$<TARGET_FILE:pcs286-sst-probe>"
                "${Python3_EXECUTABLE}" -B -m unittest tools.tests.test_sst286)
        set_tests_properties(pcs286-component.sst-adapter PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 30
            LABELS "pcs286-p0;test-infrastructure")
        set(BLUMACH_SST286_DATA_DIR "" CACHE PATH "Optional offline pinned SST286 corpus")
        if(BLUMACH_SST286_DATA_DIR)
            add_test(NAME pcs286-component.sst-functional-subset
                COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tools/sst286.py"
                    --root "${BLUMACH_SST286_DATA_DIR}"
                    --probe "$<TARGET_FILE:pcs286-sst-probe>"
                    --report "${CMAKE_CURRENT_BINARY_DIR}/sst286-$<CONFIG>-report.json")
            set_tests_properties(pcs286-component.sst-functional-subset PROPERTIES
                TIMEOUT 180 LABELS "pcs286-p0;external-hardware-subset")
        endif()
    else()
        add_test(NAME pcs286-component.sst-adapter
            COMMAND pcs286-component-pending "Python3 interpreter for SST adapter self-tests")
        set_tests_properties(pcs286-component.sst-adapter PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(pcs286-cpu-data-transfer cpu286_data_transfer.c)
    target_link_libraries(pcs286-cpu-data-transfer PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-data-transfer)
    add_test(NAME pcs286-component.cpu-data-transfer COMMAND pcs286-cpu-data-transfer)
    set_tests_properties(pcs286-component.cpu-data-transfer PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-alu-oracle cpu286_alu_oracle.c)
    target_link_libraries(pcs286-cpu-alu-oracle PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-alu-oracle)
    add_test(NAME pcs286-component.cpu-alu-oracle COMMAND pcs286-cpu-alu-oracle)
    set_tests_properties(pcs286-component.cpu-alu-oracle PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-cpu-arithmetic cpu286_arithmetic.c)
    target_link_libraries(pcs286-cpu-arithmetic PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-arithmetic)
    add_test(NAME pcs286-component.cpu-arithmetic COMMAND pcs286-cpu-arithmetic)
    set_tests_properties(pcs286-component.cpu-arithmetic PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
    add_executable(pcs286-cpu-stack-control cpu286_stack_control.c)
    target_link_libraries(pcs286-cpu-stack-control PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-stack-control)
    add_test(NAME pcs286-component.cpu-stack-control COMMAND pcs286-cpu-stack-control)
    set_tests_properties(pcs286-component.cpu-stack-control PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
endif()
pcs286_acceptance(headland headland_acceptance.c blumach_headland_gc10x)
pcs286_acceptance(at-bus at_bus_acceptance.c blumach_at_bus)
pcs286_acceptance(at-pic at_pic_acceptance.c blumach_at_pic)
if(TARGET blumach_at_pic)
    add_executable(pcs286-at-pic-contract at_pic_test.c)
    target_link_libraries(pcs286-at-pic-contract PRIVATE blumach_at_pic
        portable-failure-injection-host blumach_platform_null)
    target_include_directories(pcs286-at-pic-contract PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    pcs286_test_options(pcs286-at-pic-contract)
    add_test(NAME pcs286-component.at-pic-contract COMMAND pcs286-at-pic-contract)
    set_tests_properties(pcs286-component.at-pic-contract PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
endif()
pcs286_acceptance(at-dma at_dma_acceptance.c blumach_at_dma)

if(TARGET blumach_cpu_80286 AND TARGET blumach_at_pic)
    add_executable(pcs286-cpu-interrupts cpu286_interrupts.c)
    target_link_libraries(pcs286-cpu-interrupts PRIVATE
        blumach_cpu_80286 blumach_at_pic blumach_platform_null)
    pcs286_test_options(pcs286-cpu-interrupts)
    add_test(NAME pcs286-component.cpu-interrupts COMMAND pcs286-cpu-interrupts)
    set_tests_properties(pcs286-component.cpu-interrupts PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
endif()

if(TARGET blumach_at_bus)
    add_executable(pcs286-at-bus-contract at_bus_contract_test.c)
    target_link_libraries(pcs286-at-bus-contract PRIVATE
        blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-at-bus-contract)
    add_test(NAME pcs286-component.at-bus-contract COMMAND pcs286-at-bus-contract)
    set_tests_properties(pcs286-component.at-bus-contract PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
endif()

if(TARGET blumach_cpu_80286 AND TARGET blumach_at_bus)
    if(TARGET blumach_at_pic)
        add_executable(pcs286-board-interrupts board_interrupts_test.c)
        target_link_libraries(pcs286-board-interrupts PRIVATE blumach_pcs286_memory
            blumach_cpu_80286 blumach_at_bus blumach_at_pic
            blumach_platform_null portable-failure-injection-host)
        target_include_directories(pcs286-board-interrupts PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
        pcs286_test_options(pcs286-board-interrupts)
        add_test(NAME pcs286-component.board-interrupts COMMAND pcs286-board-interrupts)
        set_tests_properties(pcs286-component.board-interrupts PROPERTIES
            LABELS "pcs286-p0;component-integration" TIMEOUT 15)
    endif()
    add_executable(pcs286-board-memory board_memory_test.c)
    target_link_libraries(pcs286-board-memory PRIVATE blumach_pcs286_memory
        blumach_cpu_80286 blumach_at_bus blumach_platform_null portable-failure-injection-host)
    target_include_directories(pcs286-board-memory PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    pcs286_test_options(pcs286-board-memory)
    add_test(NAME pcs286-component.board-memory COMMAND pcs286-board-memory)
    set_tests_properties(pcs286-component.board-memory PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
    add_executable(pcs286-cpu-at-bus cpu286_at_bus_test.c)
    target_link_libraries(pcs286-cpu-at-bus PRIVATE
        blumach_cpu_80286 blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-cpu-at-bus)
    add_test(NAME pcs286-component.cpu-at-bus COMMAND pcs286-cpu-at-bus)
    set_tests_properties(pcs286-component.cpu-at-bus PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
endif()
