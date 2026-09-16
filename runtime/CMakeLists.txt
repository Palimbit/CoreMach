# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_runtime STATIC
    src/machine_registry.c
    src/runtime.c
)
target_include_directories(blumach_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_runtime PUBLIC blumach_engine)
target_compile_features(blumach_runtime PUBLIC c_std_11)
set_target_properties(blumach_runtime PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_runtime PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
