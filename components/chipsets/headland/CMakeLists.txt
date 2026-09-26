# SPDX-License-Identifier: GPL-2.0-or-later
add_library(blumach_headland_gc10x_contracts INTERFACE)
target_include_directories(blumach_headland_gc10x_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_headland_gc10x_contracts INTERFACE blumach_bus)
target_compile_features(blumach_headland_gc10x_contracts INTERFACE c_std_11)

# Private migration blocks. Deliberately NOT blumach_headland_gc10x: inherited
# registers/routes alone do not qualify the public PCS286 profile/wiring.
add_library(blumach_headland_legacy_registers STATIC
    src/legacy_gc103_registers.c src/legacy_gc103_memory.c)
target_link_libraries(blumach_headland_legacy_registers PRIVATE blumach_headland_gc10x_contracts)
target_compile_features(blumach_headland_legacy_registers PRIVATE c_std_11)
set_target_properties(blumach_headland_legacy_registers PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(blumach_headland_legacy_registers PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_headland_legacy_registers PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
