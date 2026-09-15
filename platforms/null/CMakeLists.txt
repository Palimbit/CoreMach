# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_platform_null STATIC src/null_host.c)
target_include_directories(blumach_platform_null
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_platform_null PUBLIC blumach_engine)
target_compile_features(blumach_platform_null PUBLIC c_std_11)
set_target_properties(blumach_platform_null PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_platform_null PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_platform_null PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
