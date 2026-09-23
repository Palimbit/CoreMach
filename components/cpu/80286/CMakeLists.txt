# SPDX-License-Identifier: GPL-2.0-or-later
# Draft declarations only; deliberately not named blumach_cpu_80286 yet.
add_library(blumach_cpu_80286_contracts INTERFACE)
target_include_directories(blumach_cpu_80286_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_cpu_80286_contracts INTERFACE blumach_bus blumach_engine)
target_compile_features(blumach_cpu_80286_contracts INTERFACE c_std_11)
