# SPDX-License-Identifier: GPL-2.0-or-later
add_library(blumach_olivetti_ioc02_contracts INTERFACE)
target_include_directories(blumach_olivetti_ioc02_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_olivetti_ioc02_contracts INTERFACE blumach_bus)
target_compile_features(blumach_olivetti_ioc02_contracts INTERFACE c_std_11)

# Private inherited policy only. Not the public, qualified IOC02 component.
add_library(blumach_ioc02_legacy_registers STATIC src/legacy_ioc02_registers.c)
target_link_libraries(blumach_ioc02_legacy_registers PRIVATE blumach_olivetti_ioc02_contracts)
target_compile_features(blumach_ioc02_legacy_registers PRIVATE c_std_11)
set_target_properties(blumach_ioc02_legacy_registers PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(blumach_ioc02_legacy_registers PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_ioc02_legacy_registers PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
