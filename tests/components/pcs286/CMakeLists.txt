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
    add_executable(pcs286-cpu-data-transfer cpu286_data_transfer.c)
    target_link_libraries(pcs286-cpu-data-transfer PRIVATE
        blumach_cpu_80286 blumach_platform_null)
    pcs286_test_options(pcs286-cpu-data-transfer)
    add_test(NAME pcs286-component.cpu-data-transfer COMMAND pcs286-cpu-data-transfer)
    set_tests_properties(pcs286-component.cpu-data-transfer PROPERTIES
        LABELS "pcs286-p0;component-acceptance" TIMEOUT 15)
endif()
pcs286_acceptance(headland headland_acceptance.c blumach_headland_gc10x)
pcs286_acceptance(at-bus at_bus_acceptance.c blumach_at_bus)
pcs286_acceptance(at-pic at_pic_acceptance.c blumach_at_pic)
pcs286_acceptance(at-dma at_dma_acceptance.c blumach_at_dma)

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
    add_executable(pcs286-cpu-at-bus cpu286_at_bus_test.c)
    target_link_libraries(pcs286-cpu-at-bus PRIVATE
        blumach_cpu_80286 blumach_at_bus blumach_platform_null)
    pcs286_test_options(pcs286-cpu-at-bus)
    add_test(NAME pcs286-component.cpu-at-bus COMMAND pcs286-cpu-at-bus)
    set_tests_properties(pcs286-component.cpu-at-bus PROPERTIES
        LABELS "pcs286-p0;component-integration" TIMEOUT 15)
endif()
