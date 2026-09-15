# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_bus STATIC src/bus.c)
target_include_directories(blumach_bus
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_bus PUBLIC blumach_engine)
target_compile_features(blumach_bus PUBLIC c_std_11)
set_target_properties(blumach_bus PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_bus PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_bus PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
