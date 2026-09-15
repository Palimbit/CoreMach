# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(blumach_headless src/main.c)
target_link_libraries(blumach_headless PRIVATE blumach_runtime blumach_platform_null)
target_compile_features(blumach_headless PRIVATE c_std_11)
set_target_properties(blumach_headless PROPERTIES C_EXTENSIONS OFF OUTPUT_NAME BluMach-headless)

if(MSVC)
    target_compile_options(blumach_headless PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_headless PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
