# BluMach host-side contract tests. These tests exercise deterministic emulator
# logic with bounded test doubles and do not require firmware or guest media.

if(BLUMACH_BUILD_LEGACY)

function(blumach_add_host_contract target source)
    add_executable(${target} ${source} ${ARGN})
    target_compile_features(${target} PRIVATE c_std_11)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/include"
        "${PROJECT_SOURCE_DIR}/src/cpu"
        "${PROJECT_SOURCE_DIR}/src/codegen"
        "${PROJECT_SOURCE_DIR}/src/codegen_new"
        "${PROJECT_BINARY_DIR}/src/include"
    )
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        target_compile_definitions(${target} PRIVATE
            _FILE_OFFSET_BITS=64
            _LARGEFILE_SOURCE=1
            _LARGEFILE64_SOURCE=1
        )
    endif()

    # The contracts use assert() as their test oracle, including in Release
    # configurations where the application itself defines NDEBUG.
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endif()

    add_test(NAME "host-contract.${target}" COMMAND ${target})
    set_tests_properties("host-contract.${target}" PROPERTIES TIMEOUT 30)
endfunction()

function(blumach_enable_dead_code_elimination target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Gy)
        target_link_options(${target} PRIVATE /OPT:REF)
    else()
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(${target} PRIVATE "LINKER:-dead_strip")
        else()
            target_link_options(${target} PRIVATE "LINKER:--gc-sections")
        endif()
    endif()
endfunction()

blumach_add_host_contract(machine-runtime-controls machine_runtime_controls_test.c)
blumach_add_host_contract(toshiba-aform-slot toshiba_aform_slot_test.c)
blumach_add_host_contract(t5100-platform t5100_platform_test.c)
blumach_enable_dead_code_elimination(t5100-platform)

blumach_add_host_contract(t5200-display-hotkey t5200_display_hotkey_test.c)
if(NOT MSVC)
    target_compile_options(t5200-display-hotkey PRIVATE -Wno-ignored-qualifiers)
endif()

blumach_add_host_contract(t3200-mfm t3200_mfm_test.c)
blumach_enable_dead_code_elimination(t3200-mfm)
if(NOT MSVC)
    target_compile_options(t3200-mfm PRIVATE -Wno-empty-body)
endif()

blumach_add_host_contract(t3200-ags
    t3200_ags_test.c
    "${PROJECT_SOURCE_DIR}/src/io.c"
)
blumach_enable_dead_code_elimination(t3200-ags)

endif()

if(BLUMACH_BUILD_ENGINE)
    add_subdirectory(engine)
endif()
