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
include(ioc02_legacy_fixture.cmake)
include(pit8254_classic_fixture.cmake)
add_executable(pcs286-kbc8042 kbc8042_test.c)
target_include_directories(pcs286-kbc8042 PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
target_link_libraries(pcs286-kbc8042 PRIVATE blumach_kbc8042 blumach_at_pic
    blumach_platform_null portable-failure-injection-host)
pcs286_test_options(pcs286-kbc8042)
add_test(NAME pcs286-component.kbc8042 COMMAND pcs286-kbc8042)
set_tests_properties(pcs286-component.kbc8042 PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-wd37c65 wd37c65_test.c)
target_link_libraries(pcs286-wd37c65 PRIVATE blumach_pc_components
    blumach_at_dma blumach_storage blumach_platform_null)
pcs286_test_options(pcs286-wd37c65)
add_test(NAME pcs286-component.wd37c65 COMMAND pcs286-wd37c65)
set_tests_properties(pcs286-component.wd37c65 PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-at-rtc-clock rtc_at_clock_test.c)
target_include_directories(pcs286-at-rtc-clock PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine"
    "${PROJECT_SOURCE_DIR}/components/pc/src" "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
target_link_libraries(pcs286-at-rtc-clock PRIVATE blumach_at_engine_adapters blumach_at_pic
    blumach_pcs286_checks blumach_platform_null portable-failure-injection-host)
pcs286_test_options(pcs286-at-rtc-clock)
add_test(NAME pcs286-component.at-rtc-clock COMMAND pcs286-at-rtc-clock)
set_tests_properties(pcs286-component.at-rtc-clock PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-at-rtc rtc_at_test.c)
target_include_directories(pcs286-at-rtc PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine"
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
target_link_libraries(pcs286-at-rtc PRIVATE blumach_at_rtc blumach_at_pic
    blumach_pcs286_checks blumach_platform_null portable-failure-injection-host)
pcs286_test_options(pcs286-at-rtc)
add_test(NAME pcs286-component.at-rtc COMMAND pcs286-at-rtc)
set_tests_properties(pcs286-component.at-rtc PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-checks checks_test.c)
target_include_directories(pcs286-checks PRIVATE
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
target_link_libraries(pcs286-checks PRIVATE blumach_pcs286_checks
    blumach_pcs286_refresh blumach_pcs286_port61 blumach_pcs286_memory
    blumach_cpu_80286 blumach_at_rtc blumach_platform_null)
pcs286_test_options(pcs286-checks)
add_test(NAME pcs286-component.checks-nmi COMMAND pcs286-checks)
set_tests_properties(pcs286-component.checks-nmi PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-refresh refresh_test.c)
target_include_directories(pcs286-refresh PRIVATE
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
target_link_libraries(pcs286-refresh PRIVATE blumach_pcs286_refresh
    blumach_pcs286_port61 blumach_cpu_80286 blumach_platform_null)
pcs286_test_options(pcs286-refresh)
add_test(NAME pcs286-component.refresh COMMAND pcs286-refresh)
set_tests_properties(pcs286-component.refresh PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-port61 port61_test.c)
target_include_directories(pcs286-port61 PRIVATE
    "${PROJECT_SOURCE_DIR}/components/pc/src"
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src")
target_link_libraries(pcs286-port61 PRIVATE blumach_pcs286_port61 blumach_platform_null
    blumach_system_olivetti_pcs286_contracts
    blumach_pcs286_headland_at blumach_headland_legacy_registers blumach_ioc02_legacy_registers
    blumach_at_pic blumach_at_dma blumach_at_bus)
pcs286_test_options(pcs286-port61)
add_test(NAME pcs286-component.port61 COMMAND pcs286-port61)
set_tests_properties(pcs286-component.port61 PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-pit8254-clock pit8254_clock_test.c)
target_include_directories(pcs286-pit8254-clock PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/engine" "${PROJECT_SOURCE_DIR}/components/pc/src")
target_link_libraries(pcs286-pit8254-clock PRIVATE blumach_at_engine_adapters
    blumach_platform_null portable-failure-injection-host)
pcs286_test_options(pcs286-pit8254-clock)
add_test(NAME pcs286-component.pit8254-clock COMMAND pcs286-pit8254-clock)
set_tests_properties(pcs286-component.pit8254-clock PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)

add_executable(pcs286-pit8254 pit8254_test.c)
target_include_directories(pcs286-pit8254 PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/engine"
    "${PROJECT_SOURCE_DIR}/components/pc/src"
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src")
target_link_libraries(pcs286-pit8254 PRIVATE blumach_pit8254 blumach_at_engine_adapters blumach_platform_null
    pcs286-pit-classic-fixture portable-failure-injection-host
    blumach_pcs286_headland_at blumach_system_olivetti_pcs286_contracts
    blumach_headland_legacy_registers blumach_ioc02_legacy_registers
    blumach_at_pic blumach_at_dma blumach_at_bus)
pcs286_test_options(pcs286-pit8254)
add_test(NAME pcs286-component.pit8254 COMMAND pcs286-pit8254)
set_tests_properties(pcs286-component.pit8254 PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 90)
add_executable(pcs286-board-io board_io.c)
target_include_directories(pcs286-board-io PRIVATE
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src")
target_link_libraries(pcs286-board-io PRIVATE blumach_pcs286_headland_at
    blumach_system_olivetti_pcs286_contracts
    blumach_headland_legacy_registers blumach_ioc02_legacy_registers
    blumach_at_pic blumach_at_dma blumach_at_bus blumach_platform_null)
pcs286_test_options(pcs286-board-io)
add_test(NAME pcs286-component.board-io COMMAND pcs286-board-io)
set_tests_properties(pcs286-component.board-io PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 30)
add_executable(pcs286-headland-at-memory headland_at_memory.c)
target_include_directories(pcs286-headland-at-memory PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src"
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src")
target_link_libraries(pcs286-headland-at-memory PRIVATE blumach_pcs286_headland_at
    blumach_ioc02_legacy_registers blumach_at_pic blumach_at_dma
    blumach_headland_legacy_registers blumach_pcs286_memory blumach_at_bus
    blumach_cpu_80286 blumach_platform_null)
pcs286_test_options(pcs286-headland-at-memory)
add_test(NAME pcs286-component.headland-at-memory COMMAND pcs286-headland-at-memory)
set_tests_properties(pcs286-component.headland-at-memory PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 120)
add_executable(pcs286-ioc02-registers ioc02_registers.c)
target_include_directories(pcs286-ioc02-registers PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(pcs286-ioc02-registers PRIVATE
    blumach_ioc02_legacy_registers blumach_bus blumach_platform_null)
pcs286_test_options(pcs286-ioc02-registers)
add_test(NAME pcs286-component.ioc02-registers COMMAND pcs286-ioc02-registers)
set_tests_properties(pcs286-component.ioc02-registers PROPERTIES
    LABELS "pcs286-p0;component-acceptance" TIMEOUT 30)
include(headland_legacy_fixture.cmake)
add_executable(pcs286-headland-registers headland_registers.c)
target_include_directories(pcs286-headland-registers PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(pcs286-headland-registers PRIVATE blumach_headland_legacy_registers blumach_bus)
pcs286_test_options(pcs286-headland-registers)
add_test(NAME pcs286-component.headland-registers COMMAND pcs286-headland-registers)
set_tests_properties(pcs286-component.headland-registers PROPERTIES
    LABELS "pcs286-p0;component-acceptance" TIMEOUT 30)
add_executable(pcs286-headland-control headland_control.c)
add_executable(pcs286-headland-geometry headland_geometry.c)
target_include_directories(pcs286-headland-geometry PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
target_link_libraries(pcs286-headland-geometry PRIVATE blumach_pcs286_headland_at
    blumach_headland_legacy_registers blumach_headland_gc10x_contracts
    blumach_at_bus blumach_pcs286_memory)
pcs286_test_options(pcs286-headland-geometry)
add_test(NAME pcs286-component.headland-geometry COMMAND pcs286-headland-geometry)
set_tests_properties(pcs286-component.headland-geometry PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 60)
target_include_directories(pcs286-headland-control PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src")
target_link_libraries(pcs286-headland-control PRIVATE blumach_headland_legacy_registers
    blumach_headland_gc10x_contracts)
pcs286_test_options(pcs286-headland-control)
add_test(NAME pcs286-component.headland-control COMMAND pcs286-headland-control)
set_tests_properties(pcs286-component.headland-control PROPERTIES
    LABELS "pcs286-p0;component-acceptance" TIMEOUT 30)
add_executable(pcs286-headland-memory headland_memory.c)
target_include_directories(pcs286-headland-memory PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(pcs286-headland-memory PRIVATE blumach_headland_legacy_registers
    blumach_headland_gc10x_contracts blumach_pcs286_memory blumach_platform_null)
pcs286_test_options(pcs286-headland-memory)
add_test(NAME pcs286-component.headland-memory COMMAND pcs286-headland-memory)
set_tests_properties(pcs286-component.headland-memory PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 120)
add_executable(pcs286-headland-context headland_context.c)
target_include_directories(pcs286-headland-context PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src")
target_link_libraries(pcs286-headland-context PRIVATE blumach_headland_legacy_registers
    blumach_headland_gc10x_contracts)
pcs286_test_options(pcs286-headland-context)
add_test(NAME pcs286-component.headland-context COMMAND pcs286-headland-context)
set_tests_properties(pcs286-component.headland-context PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 120)
add_executable(pcs286-headland-shadow headland_shadow.c)
target_include_directories(pcs286-headland-shadow PRIVATE
    "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src")
target_link_libraries(pcs286-headland-shadow PRIVATE blumach_headland_legacy_registers
    blumach_headland_gc10x_contracts blumach_pcs286_memory blumach_platform_null)
pcs286_test_options(pcs286-headland-shadow)
add_test(NAME pcs286-component.headland-shadow COMMAND pcs286-headland-shadow)
set_tests_properties(pcs286-component.headland-shadow PROPERTIES
    LABELS "pcs286-p0;component-integration" TIMEOUT 120)
if(TARGET blumach_cpu_80286)
add_executable(pcs286-protected-full-joint cpu286_protected_full_joint.c)
target_link_libraries(pcs286-protected-full-joint PRIVATE blumach_cpu_80286 blumach_platform_null)
pcs286_test_options(pcs286-protected-full-joint)
add_test(NAME pcs286-component.protected-full-joint COMMAND pcs286-protected-full-joint)
set_tests_properties(pcs286-component.protected-full-joint PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 180)

add_executable(pcs286-protected-tasks cpu286_protected_tasks.c)
target_include_directories(pcs286-protected-tasks PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
target_link_libraries(pcs286-protected-tasks PRIVATE blumach_cpu_80286 blumach_platform_null)
pcs286_test_options(pcs286-protected-tasks)
add_test(NAME pcs286-component.protected-tasks COMMAND pcs286-protected-tasks)
set_tests_properties(pcs286-component.protected-tasks PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 90)

add_executable(pcs286-protected-inner cpu286_protected_inner.c)
    target_link_libraries(pcs286-protected-inner PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-inner PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-inner)
    add_test(NAME pcs286-component.protected-inner COMMAND pcs286-protected-inner)
    set_tests_properties(pcs286-component.protected-inner PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-calls cpu286_protected_calls.c)
    target_link_libraries(pcs286-protected-calls PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-calls PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-calls)
    add_test(NAME pcs286-component.protected-calls COMMAND pcs286-protected-calls)
    set_tests_properties(pcs286-component.protected-calls PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-returns cpu286_protected_returns.c)
    target_link_libraries(pcs286-protected-returns PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-returns PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-returns)
    add_test(NAME pcs286-component.protected-returns COMMAND pcs286-protected-returns)
    set_tests_properties(pcs286-component.protected-returns PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-joint cpu286_protected_joint.c)
    target_link_libraries(pcs286-protected-joint PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-joint PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-joint)
    add_test(NAME pcs286-component.protected-joint COMMAND pcs286-protected-joint)
    set_tests_properties(pcs286-component.protected-joint PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-instruction-faults cpu286_protected_instruction_faults.c)
    target_link_libraries(pcs286-protected-instruction-faults PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-instruction-faults PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-instruction-faults)
    add_test(NAME pcs286-component.protected-instruction-faults COMMAND pcs286-protected-instruction-faults)
    set_tests_properties(pcs286-component.protected-instruction-faults PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-strings cpu286_protected_strings.c)
    target_link_libraries(pcs286-protected-strings PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-strings PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-strings)
    add_test(NAME pcs286-component.protected-strings COMMAND pcs286-protected-strings)
    set_tests_properties(pcs286-component.protected-strings PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-instructions cpu286_protected_instructions.c)
    target_link_libraries(pcs286-protected-instructions PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-instructions PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-instructions)
    add_test(NAME pcs286-component.protected-instructions COMMAND pcs286-protected-instructions)
    set_tests_properties(pcs286-component.protected-instructions PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-enter cpu286_protected_enter.c)
    target_link_libraries(pcs286-protected-enter PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-enter PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-enter)
    add_test(NAME pcs286-component.protected-enter COMMAND pcs286-protected-enter)
    set_tests_properties(pcs286-component.protected-enter PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-stack cpu286_protected_stack.c)
    target_link_libraries(pcs286-protected-stack PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-stack PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-stack)
    add_test(NAME pcs286-component.protected-stack COMMAND pcs286-protected-stack)
    set_tests_properties(pcs286-component.protected-stack PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-iopl cpu286_protected_iopl.c)
    target_link_libraries(pcs286-protected-iopl PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-iopl PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-iopl)
    add_test(NAME pcs286-component.protected-iopl COMMAND pcs286-protected-iopl)
    set_tests_properties(pcs286-component.protected-iopl PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 90)
    add_executable(pcs286-protected-queries cpu286_protected_queries.c)
    target_link_libraries(pcs286-protected-queries PRIVATE blumach_cpu_80286 blumach_platform_null)
    target_include_directories(pcs286-protected-queries PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    pcs286_test_options(pcs286-protected-queries)
    add_test(NAME pcs286-component.protected-queries COMMAND pcs286-protected-queries)
    set_tests_properties(pcs286-component.protected-queries PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 60)
    add_executable(pcs286-protected-transition cpu286_protected_transition.c)
    target_include_directories(pcs286-protected-transition PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-transition PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-protected-transition)
    add_test(NAME pcs286-component.protected-transition COMMAND pcs286-protected-transition)
    set_tests_properties(pcs286-component.protected-transition PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 60)
    add_executable(pcs286-protected-execution cpu286_protected_execution.c)
    target_include_directories(pcs286-protected-execution PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-execution PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-protected-execution)
    add_test(NAME pcs286-component.protected-execution COMMAND pcs286-protected-execution)
    set_tests_properties(pcs286-component.protected-execution PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 60)
    add_executable(pcs286-protected-access cpu286_protected_access.c)
    target_include_directories(pcs286-protected-access PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-access PRIVATE blumach_cpu_80286)
    pcs286_test_options(pcs286-protected-access)
    add_test(NAME pcs286-component.protected-access COMMAND pcs286-protected-access)
    set_tests_properties(pcs286-component.protected-access PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-protected-delivery cpu286_protected_delivery.c)
    target_include_directories(pcs286-protected-delivery PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-delivery PRIVATE blumach_cpu_80286)
    pcs286_test_options(pcs286-protected-delivery)
    add_test(NAME pcs286-component.protected-delivery COMMAND pcs286-protected-delivery)
    set_tests_properties(pcs286-component.protected-delivery PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-protected-iret cpu286_protected_iret.c)
    target_include_directories(pcs286-protected-iret PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-iret PRIVATE blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-protected-iret)
    add_test(NAME pcs286-component.protected-iret COMMAND pcs286-protected-iret)
    set_tests_properties(pcs286-component.protected-iret PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
    add_executable(pcs286-protected-entry cpu286_protected_entry.c)
    target_include_directories(pcs286-protected-entry PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-protected-entry PRIVATE blumach_cpu_80286)
    pcs286_test_options(pcs286-protected-entry)
    add_test(NAME pcs286-component.protected-entry COMMAND pcs286-protected-entry)
    add_executable(pcs286-cpu-descriptors cpu286_descriptors.c)
    target_include_directories(pcs286-cpu-descriptors PRIVATE "${PROJECT_SOURCE_DIR}/components/cpu/80286/src")
    target_link_libraries(pcs286-cpu-descriptors PRIVATE blumach_cpu_80286)
    pcs286_test_options(pcs286-cpu-descriptors)
    add_test(NAME pcs286-component.cpu-descriptors COMMAND pcs286-cpu-descriptors)
    set_tests_properties(pcs286-component.cpu-descriptors PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 60)
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
        add_test(NAME pcs286-component.boot-probe
            COMMAND "${CMAKE_COMMAND}" -E env
                "BM_PCS286_BOOT_PROBE=$<TARGET_FILE:pcs286-boot-probe>"
                "${Python3_EXECUTABLE}" -B -m unittest tools.tests.test_pcs286_boot_probe)
        set_tests_properties(pcs286-component.boot-probe PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 30
            LABELS "pcs286-p0;component-integration")
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
if(TARGET blumach_at_keyboard AND TARGET blumach_kbc8042 AND TARGET blumach_at_pic)
    add_executable(pcs286-keyboard-pair keyboard_pair_test.c)
    target_link_libraries(pcs286-keyboard-pair PRIVATE blumach_at_engine_adapters
        blumach_at_pic portable-failure-injection-host blumach_platform_null)
    target_include_directories(pcs286-keyboard-pair PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    pcs286_test_options(pcs286-keyboard-pair)
    add_test(NAME pcs286-component.keyboard-pair COMMAND pcs286-keyboard-pair)
    set_tests_properties(pcs286-component.keyboard-pair PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 30)
    add_executable(pcs286-keyboard-clock keyboard_clock_test.c)
    target_link_libraries(pcs286-keyboard-clock PRIVATE blumach_at_engine_adapters
        blumach_at_pic portable-failure-injection-host blumach_platform_null)
    target_include_directories(pcs286-keyboard-clock PRIVATE
        "${PROJECT_SOURCE_DIR}/tests/engine" "${PROJECT_SOURCE_DIR}/components/pc/src")
    pcs286_test_options(pcs286-keyboard-clock)
    add_test(NAME pcs286-component.keyboard-clock COMMAND pcs286-keyboard-clock)
    set_tests_properties(pcs286-component.keyboard-clock PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 30)
    add_executable(pcs286-at-keyboard-contract keyboard_at_test.c)
    target_link_libraries(pcs286-at-keyboard-contract PRIVATE blumach_at_keyboard
        blumach_kbc8042 blumach_at_pic portable-failure-injection-host blumach_platform_null)
    target_include_directories(pcs286-at-keyboard-contract PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    pcs286_test_options(pcs286-at-keyboard-contract)
    add_test(NAME pcs286-component.at-keyboard-contract COMMAND pcs286-at-keyboard-contract)
    set_tests_properties(pcs286-component.at-keyboard-contract PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 30)
endif()
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
if(TARGET blumach_at_dma)
    add_executable(pcs286-dma-coordinator dma_coordinator_test.c)
    target_include_directories(pcs286-dma-coordinator PRIVATE
        "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
    target_link_libraries(pcs286-dma-coordinator PRIVATE
        blumach_pcs286_dma_coordinator blumach_at_dma blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-dma-coordinator)
    add_test(NAME pcs286-component.dma-coordinator COMMAND pcs286-dma-coordinator)
    set_tests_properties(pcs286-component.dma-coordinator PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
    add_executable(pcs286-at-dma-mem2mem at_dma_mem2mem.c)
    target_link_libraries(pcs286-at-dma-mem2mem PRIVATE blumach_at_dma blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-at-dma-mem2mem)
    add_test(NAME pcs286-component.at-dma-mem2mem COMMAND pcs286-at-dma-mem2mem)
    set_tests_properties(pcs286-component.at-dma-mem2mem PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 60)
    add_executable(pcs286-at-dma-pair at_dma_pair.c)
    target_include_directories(pcs286-at-dma-pair PRIVATE "${PROJECT_SOURCE_DIR}/components/pc/src")
    target_link_libraries(pcs286-at-dma-pair PRIVATE blumach_at_dma blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-at-dma-pair)
    add_test(NAME pcs286-component.at-dma-pair COMMAND pcs286-at-dma-pair)
    set_tests_properties(pcs286-component.at-dma-pair PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 30)
    add_executable(pcs286-at-dma-registers at_dma_registers.c)
    target_include_directories(pcs286-at-dma-registers PRIVATE "${PROJECT_SOURCE_DIR}/tests/engine")
    target_link_libraries(pcs286-at-dma-registers PRIVATE blumach_at_dma blumach_platform_null portable-failure-injection-host)
    pcs286_test_options(pcs286-at-dma-registers)
    add_test(NAME pcs286-component.at-dma-registers COMMAND pcs286-at-dma-registers)
    set_tests_properties(pcs286-component.at-dma-registers PROPERTIES LABELS "pcs286-p0;component-acceptance" TIMEOUT 30)
    add_executable(pcs286-at-dma-transfers at_dma_transfers.c)
    target_link_libraries(pcs286-at-dma-transfers PRIVATE blumach_at_dma blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-at-dma-transfers)
    add_test(NAME pcs286-component.at-dma-transfers COMMAND pcs286-at-dma-transfers)
    set_tests_properties(pcs286-component.at-dma-transfers PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 30)
    add_executable(pcs286-at-dma-cascade at_dma_cascade.c)
    target_link_libraries(pcs286-at-dma-cascade PRIVATE blumach_at_dma blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-at-dma-cascade)
    add_test(NAME pcs286-component.at-dma-cascade COMMAND pcs286-at-dma-cascade)
    set_tests_properties(pcs286-component.at-dma-cascade PROPERTIES LABELS "pcs286-p0;component-integration" TIMEOUT 30)
endif()

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
    add_executable(pcs286-page-spare-latches page_spare_latches_test.c)
    target_include_directories(pcs286-page-spare-latches PRIVATE
        "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src")
    target_link_libraries(pcs286-page-spare-latches PRIVATE
        blumach_pcs286_page_spare_latches)
    pcs286_test_options(pcs286-page-spare-latches)
    add_test(NAME pcs286-component.page-spare-latches COMMAND pcs286-page-spare-latches)
    set_tests_properties(pcs286-component.page-spare-latches PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
    add_executable(pcs286-board-control board_control_test.c)
    target_include_directories(pcs286-board-control PRIVATE
        "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
        "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
        "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src")
    target_link_libraries(pcs286-board-control PRIVATE blumach_pcs286_control
        blumach_at_engine_adapters blumach_at_dma blumach_ioc02_legacy_registers
        blumach_platform_null)
    pcs286_test_options(pcs286-board-control)
    add_executable(pcs286-board-services board_services_test.c)
    target_include_directories(pcs286-board-services PRIVATE
        "${PROJECT_SOURCE_DIR}/systems/olivetti-pcs286/src"
        "${PROJECT_SOURCE_DIR}/components/chipsets/headland/src"
        "${PROJECT_SOURCE_DIR}/components/chipsets/olivetti-ioc02/src"
        "${PROJECT_SOURCE_DIR}/tests/engine")
    target_link_libraries(pcs286-board-services PRIVATE blumach_pcs286_services
        blumach_at_dma blumach_ioc02_legacy_registers blumach_platform_null
        portable-failure-injection-host)
    pcs286_test_options(pcs286-board-services)
    add_test(NAME pcs286-component.board-services COMMAND pcs286-board-services)
    set_tests_properties(pcs286-component.board-services PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 30)
    add_test(NAME pcs286-component.board-control COMMAND pcs286-board-control)
    set_tests_properties(pcs286-component.board-control PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 30)
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
