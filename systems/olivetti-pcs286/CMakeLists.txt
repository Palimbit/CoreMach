# SPDX-License-Identifier: GPL-2.0-or-later
# Draft interfaces are deliberately separate from future implementation targets.
add_library(blumach_system_olivetti_pcs286_contracts INTERFACE)
target_include_directories(blumach_system_olivetti_pcs286_contracts INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(blumach_system_olivetti_pcs286_contracts INTERFACE
    blumach_runtime blumach_cpu_80286_contracts blumach_pc_at_contracts
    blumach_headland_gc10x_contracts blumach_olivetti_ioc02_contracts
    blumach_ims_g171_contracts blumach_memory blumach_pc_components blumach_video)

# Compile every public header in isolation: no emulator stub, fake symbols or
# behavioural tests. C++ checks run only when a frontend already enables C++.
set(pcs286_contract_headers
    blumach/components/cpu_80286.h
    blumach/components/headland_gc10x.h
    blumach/components/olivetti_ioc02.h
    blumach/components/at_bus.h
    blumach/components/at_pic.h
    blumach/components/at_dma.h
    blumach/components/pit8254.h
    blumach/components/rtc_at.h
    blumach/components/kbc8042.h
    blumach/components/keyboard_at.h
    blumach/components/wd37c65.h
    blumach/components/ata_pio.h
    blumach/components/at_clock.h
    blumach/components/ims_g171.h
    blumach/components/linear_memory.h
    blumach/components/pvga1a.h
    blumach/components/uart16450.h
    blumach/components/lpt_spp.h
    blumach/systems/pcs286_board.h
    blumach/systems/pcs286_memory.h
    blumach/systems/olivetti_pcs286.h)
set(pcs286_contract_sources)
foreach(header IN LISTS pcs286_contract_headers)
    get_filename_component(stem "${header}" NAME_WE)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/contracts/${stem}.c")
    file(GENERATE OUTPUT "${source}" CONTENT
        "/* SPDX-License-Identifier: GPL-2.0-or-later */\n#include <${header}>\ntypedef int bm_header_check_${stem};\n")
    list(APPEND pcs286_contract_sources "${source}")
    if(CMAKE_CXX_COMPILER_LOADED)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/contracts/${stem}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT
            "/* SPDX-License-Identifier: GPL-2.0-or-later */\n#include <${header}>\ntypedef int bm_header_check_${stem};\n")
        list(APPEND pcs286_contract_sources "${source}")
    endif()
endforeach()
add_library(blumach_pcs286_contract_check OBJECT ${pcs286_contract_sources})
target_link_libraries(blumach_pcs286_contract_check PRIVATE
    blumach_system_olivetti_pcs286_contracts)
target_compile_features(blumach_pcs286_contract_check PRIVATE c_std_11)
set_target_properties(blumach_pcs286_contract_check PROPERTIES C_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_LOADED)
    target_compile_features(blumach_pcs286_contract_check PRIVATE cxx_std_17)
    set_target_properties(blumach_pcs286_contract_check PROPERTIES CXX_EXTENSIONS OFF)
endif()

# Storage is real; the unresolved chipset and machine factory stay absent.
add_library(blumach_pcs286_memory STATIC src/pcs286_memory.c)
target_link_libraries(blumach_pcs286_memory PUBLIC blumach_system_olivetti_pcs286_contracts)
target_compile_features(blumach_pcs286_memory PRIVATE c_std_11)
set_target_properties(blumach_pcs286_memory PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(blumach_pcs286_memory PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_pcs286_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(MSVC)
    target_compile_options(blumach_pcs286_contract_check PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_pcs286_contract_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
