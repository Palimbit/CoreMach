/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <stdint.h>

typedef struct trace_capture {
    bm_808x_trace_t last;
    uint64_t count;
} trace_capture_t;

static void
capture_trace(void *context, const bm_808x_trace_t *trace)
{
    trace_capture_t *capture = context;
    capture->last = *trace;
    ++capture->count;
}

int
main(void)
{
    static const uint8_t program[] = {
        0x2e, 0xff, 0xd8 /* CS: CALL FAR AX: rejected group suboperation. */
    };
    trace_capture_t capture = { 0 };
    cpu_808x_test_config_t config = {
        .bus_capacity = 1U,
        .trace = capture_trace,
        .trace_context = &capture
    };
    cpu_808x_test_machine_t machine;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    assert(cpu_808x_test_run(&machine, 8U) == BM_STATUS_UNSUPPORTED);

    assert(capture.count == 2U); /* Reset jump, then rejected instruction. */
    assert(capture.last.cs == 0xf000U && capture.last.ip == 0U);
    assert(capture.last.opcode == 0x2eU);
    assert(capture.last.effective_opcode == 0xffU);
    assert(capture.last.prefix_count == 1U);
    assert(cpu_808x_test_inspect(&machine, "last_fetch") == 0xf0000U);
    assert(cpu_808x_test_inspect(&machine, "last_opcode") == 0x2eU);
    assert(cpu_808x_test_inspect(&machine, "last_effective_opcode") == 0xffU);
    assert(cpu_808x_test_inspect(&machine, "last_instruction_length") == 3U);
    assert(cpu_808x_test_inspect(&machine, "last_instruction_bytes") ==
           UINT64_C(0x00d8ff2e));

    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
