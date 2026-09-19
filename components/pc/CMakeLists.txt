# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_pc_components STATIC
    src/dma8237.c
    src/dma_page_registers.c
    src/fdc765.c
    src/lpt_spp.c
    src/pic8259.c
    src/pit8253.c
    src/pit8253_exact.c
    src/rtc_mm58167.c
    src/uart16450.c
    src/xta.c
)
target_include_directories(blumach_pc_components
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_pc_components PUBLIC blumach_bus blumach_storage)
target_compile_features(blumach_pc_components PUBLIC c_std_11)
set_target_properties(blumach_pc_components PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_pc_components PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_pc_components PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Optional scheduler wiring remains separate from the reusable chip model.
add_library(blumach_pc_engine_adapters STATIC src/pit8253_clock.c)
target_include_directories(blumach_pc_engine_adapters
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_pc_engine_adapters
    PUBLIC blumach_pc_components blumach_engine
)
target_compile_features(blumach_pc_engine_adapters PUBLIC c_std_11)
set_target_properties(blumach_pc_engine_adapters PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_pc_engine_adapters PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_pc_engine_adapters
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()
