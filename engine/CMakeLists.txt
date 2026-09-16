# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_engine STATIC
    src/engine.c
    src/storage.c
)

target_include_directories(blumach_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_features(blumach_engine PUBLIC c_std_11)
set_target_properties(blumach_engine PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_engine PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
