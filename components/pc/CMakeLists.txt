# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_pc_components STATIC
    src/dma8237.c
    src/dma_page_registers.c
    src/pic8259.c
    src/pit8253.c
    src/pit8253_exact.c
    src/rtc_mm58167.c
)
target_include_directories(blumach_pc_components
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_pc_components PUBLIC blumach_bus)
target_compile_features(blumach_pc_components PUBLIC c_std_11)
set_target_properties(blumach_pc_components PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_pc_components PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_pc_components PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
