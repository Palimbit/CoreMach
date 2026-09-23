# SPDX-License-Identifier: GPL-2.0-or-later
# Contracts remain usable without linking the partial interpreter.
add_library(blumach_cpu_80286_contracts INTERFACE)
target_include_directories(blumach_cpu_80286_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_cpu_80286_contracts INTERFACE blumach_bus blumach_engine)
target_compile_features(blumach_cpu_80286_contracts INTERFACE c_std_11)

# Partial real-mode interpreter; not a complete or timed 80286 implementation.
add_library(blumach_cpu_80286 STATIC src/cpu_80286.c)
target_link_libraries(blumach_cpu_80286 PUBLIC blumach_cpu_80286_contracts)
set_target_properties(blumach_cpu_80286 PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(blumach_cpu_80286 PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_cpu_80286 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
