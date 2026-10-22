# SPDX-License-Identifier: GPL-2.0-or-later

add_library(blumach_system_olivetti_m10 STATIC
    src/olivetti_m10.c
    src/m10_keyboard.c
)
target_include_directories(blumach_system_olivetti_m10
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(blumach_system_olivetti_m10
    PUBLIC
        blumach_runtime
        blumach_upd1990ac
    PRIVATE
        blumach_bus
        blumach_cpu_i8085
        blumach_msm81c55
        blumach_video
)
target_compile_features(blumach_system_olivetti_m10 PUBLIC c_std_11)
set_target_properties(blumach_system_olivetti_m10 PROPERTIES C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(blumach_system_olivetti_m10 PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_system_olivetti_m10 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
