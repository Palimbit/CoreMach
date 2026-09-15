# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_memory STATIC src/linear_memory.c)
target_include_directories(blumach_memory
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_memory PUBLIC blumach_bus)
target_compile_features(blumach_memory PUBLIC c_std_11)
set_target_properties(blumach_memory PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_memory PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
