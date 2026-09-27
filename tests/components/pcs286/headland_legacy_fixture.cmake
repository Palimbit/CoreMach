# SPDX-License-Identifier: GPL-2.0-or-later
# Compile the actual, unedited classic register handlers as a test oracle.
# Nothing generated here is linked into the portable component.
set(classic_path "${PROJECT_SOURCE_DIR}/src/chipset/headland.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${classic_path}")
file(READ "${classic_path}" classic)
string(REPLACE "\r\n" "\n" classic "${classic}")
string(SHA256 classic_hash "${classic}")
if(NOT classic_hash STREQUAL "7a608fd0d3e58b29e738dbf502eadc556fb99f5167d683132cf78c3d10b68a13")
    message(FATAL_ERROR "Headland classic reference changed; review the port/oracle before updating its pinned hash")
endif()
function(headland_extract first last output)
    string(FIND "${classic}" "${first}" begin)
    string(FIND "${classic}" "${last}" end)
    if(begin LESS 0 OR end LESS_EQUAL begin)
        message(FATAL_ERROR "Headland classic fixture extraction boundary missing")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${classic}" ${begin} ${length} body)
    set(${output} "${body}" PARENT_SCOPE)
endfunction()
headland_extract("/*\n * 86Box" "#ifdef ENABLE_HEADLAND_LOG" notice)
headland_extract("static const int mem_conf_cr0" "static uint32_t\nget_addr" tables)
headland_extract("static void\nhl_write(" "static uint8_t\nmem_read_b(" handlers)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/headland_classic_registers.inc"
    "/* SPDX-License-Identifier: GPL-2.0-or-later; generated test-only reference. */\n${notice}\n${tables}\n${handlers}")
headland_extract("static uint32_t\nget_addr" "static void\nhl_write(" mapping)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/headland_classic_memory.inc"
    "/* SPDX-License-Identifier: GPL-2.0-or-later; generated test-only reference. */\n${notice}\n${tables}\n${mapping}\n${handlers}")
