# SPDX-License-Identifier: GPL-2.0-or-later
add_library(blumach_pc_at_contracts INTERFACE)
target_include_directories(blumach_pc_at_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(blumach_pc_at_contracts INTERFACE
    blumach_bus blumach_storage blumach_engine)
target_compile_features(blumach_pc_at_contracts INTERFACE c_std_11)
