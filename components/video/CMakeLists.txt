# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_video STATIC src/pvga1a.c src/v6355d.c)
target_include_directories(blumach_video
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_video PUBLIC blumach_bus)
target_compile_features(blumach_video PUBLIC c_std_11)
set_target_properties(blumach_video PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_video PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_video PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
