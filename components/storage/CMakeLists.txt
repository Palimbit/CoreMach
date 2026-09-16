# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_storage STATIC src/floppy_drive.c)
target_include_directories(blumach_storage
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_storage PUBLIC blumach_engine)
target_compile_features(blumach_storage PUBLIC c_std_11)
set_target_properties(blumach_storage PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_storage PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_storage PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
