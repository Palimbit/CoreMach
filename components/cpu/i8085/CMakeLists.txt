# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_cpu_i8085 STATIC
    src/cpu_i8085.c
)
target_include_directories(blumach_cpu_i8085
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_cpu_i8085 PUBLIC blumach_bus)
target_compile_features(blumach_cpu_i8085 PUBLIC c_std_11)
set_target_properties(blumach_cpu_i8085 PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_cpu_i8085 PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_cpu_i8085 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
