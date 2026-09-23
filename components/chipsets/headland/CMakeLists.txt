# SPDX-License-Identifier: GPL-2.0-or-later
add_library(blumach_headland_gc10x_contracts INTERFACE)
target_include_directories(blumach_headland_gc10x_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_headland_gc10x_contracts INTERFACE blumach_bus)
target_compile_features(blumach_headland_gc10x_contracts INTERFACE c_std_11)
