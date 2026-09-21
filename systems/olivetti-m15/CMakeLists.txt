# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_system_olivetti_m15 STATIC src/olivetti_m15.c)
target_include_directories(blumach_system_olivetti_m15
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_system_olivetti_m15
    PUBLIC
        blumach_runtime
        blumach_cpu_808x
        blumach_memory
        blumach_pc_components
        blumach_pc_engine_adapters
        blumach_storage
)
target_compile_features(blumach_system_olivetti_m15 PUBLIC c_std_11)
set_target_properties(blumach_system_olivetti_m15 PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_system_olivetti_m15 PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_system_olivetti_m15 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
