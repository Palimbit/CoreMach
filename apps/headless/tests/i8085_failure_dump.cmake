# SPDX-License-Identifier: GPL-2.0-or-later
# A strict M10 session stops on the first undocumented 8085 opcode of a
# synthetic image (32 KiB of DSUB, 08h; no firmware). The failure dump
# must name the 8085 registers and the PC of the rejected instruction.
#
# cmake -DHEADLESS=<executable> -DWORK_DIR=<dir> -P i8085_failure_dump.cmake

file(MAKE_DIRECTORY "${WORK_DIR}")
string(ASCII 8 dsub)
string(REPEAT "${dsub}" 32768 image)
set(firmware "${WORK_DIR}/i8085-dsub.bin")
file(WRITE "${firmware}" "${image}")
file(SIZE "${firmware}" size)
if(NOT size EQUAL 32768)
    message(FATAL_ERROR "synthetic image is ${size} bytes")
endif()

execute_process(
    COMMAND "${HEADLESS}" --machine olivetti-m10 --firmware "${firmware}"
            --machine-option rtc_time=1 --ticks 1000000
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    TIMEOUT 60)
if(NOT output MATCHES "status=-8")
    message(FATAL_ERROR "expected a strict BM_STATUS_UNSUPPORTED stop\n${output}")
endif()
if(NOT output MATCHES "failure_cpu a=[0-9a-f]+ f=[0-9a-f]+ b=")
    message(FATAL_ERROR "the failure dump does not name the 8085 registers\n${output}")
endif()
if(NOT output MATCHES " pc=0000 ")
    message(FATAL_ERROR "the failure dump does not show PC 0000h\n${output}")
endif()
if(output MATCHES " ax=")
    message(FATAL_ERROR "the failure dump uses x86 register names\n${output}")
endif()
