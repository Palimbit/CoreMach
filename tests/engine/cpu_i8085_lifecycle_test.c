/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_i8085_test_harness.h"

#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

static void
test_create_rejects_invalid_configuration(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_i8085_config_t config;
    bm_cpu_t cpu;

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    config = (bm_i8085_config_t) {
        .model = BM_I8085_INTEL_8085A, .frequency_hz = 2457600U, .bus = bus
    };
    assert(bm_i8085_create(NULL, &config, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_i8085_create(&host, NULL, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_i8085_create(&host, &config, NULL) == BM_STATUS_INVALID_ARGUMENT);
    config.bus = NULL;
    assert(bm_i8085_create(&host, &config, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    config.bus = bus;
    config.frequency_hz = 0U;
    assert(bm_i8085_create(&host, &config, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    config.frequency_hz = 2457600U;
    config.model = (bm_i8085_model_t) 7;
    assert(bm_i8085_create(&host, &config, &cpu) == BM_STATUS_INVALID_ARGUMENT);
    config.model = BM_I8085_OKI_MSM80C85A;
    assert(bm_i8085_create(&host, &config, &cpu) == BM_STATUS_OK);
    assert(strcmp(cpu.name, "OKI MSM80C85A") == 0);
    cpu.ops.destroy(cpu.context);
    bm_bus_destroy(bus);
}

/* RESET IN clears PC, IE, RST 7.5, TRAP and SOD and sets the three masks
 * ([MCS85] p. 2-18). */
static void
test_reset_in_actions(void)
{
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    i8085_test_create(&machine, NULL);
    assert(strcmp(machine.cpu.name, "Intel 8085A") == 0);
    state = i8085_test_state(&machine);
    assert(state.pc == 0U && state.interrupt_enable == 0U);
    assert(state.interrupt_mask == 7U && state.sod == 0U);
    assert(state.rst75_pending == 0U && state.trap_pending == 0U);

    state.a = 0x12U;
    state.b = 0x34U;
    state.f = BM_I8085_FLAG_CY | BM_I8085_FLAG_K;
    state.pc = 0x1234U;
    state.sp = 0xfff0U;
    state.interrupt_enable = 1U;
    state.interrupt_mask = 0U;
    state.rst75_pending = 1U;
    state.trap_pending = 1U;
    state.sod = 1U;
    state.halted = 1U;
    i8085_test_set_state(&machine, &state);
    assert(machine.cpu.ops.reset(machine.cpu.context) == BM_STATUS_OK);
    state = i8085_test_state(&machine);
    assert(state.pc == 0U && state.interrupt_enable == 0U);
    assert(state.interrupt_mask == 7U && state.sod == 0U && state.halted == 0U);
    assert(state.rst75_pending == 0U && state.trap_pending == 0U);
    /* Registers and flags are not explicitly changed by RESET IN. */
    assert(state.a == 0x12U && state.b == 0x34U && state.sp == 0xfff0U);
    assert(state.f == (BM_I8085_FLAG_CY | BM_I8085_FLAG_K));

    state.pc = 0x4000U;
    state.interrupt_enable = 1U;
    i8085_test_set_state(&machine, &state);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RESET, 1);
    state = i8085_test_state(&machine);
    assert(state.pc == 0U && state.interrupt_enable == 0U);
    i8085_test_destroy(&machine);
}

static void
test_arch_state_validation(void)
{
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;
    bm_i8085_arch_state_t bad;

    i8085_test_create(&machine, NULL);
    state = i8085_test_state(&machine);
    assert(state.size == sizeof(state));
    assert(state.version == BM_I8085_ARCH_STATE_VERSION);
    state.f = 0xf7U;
    state.d = 0x56U;
    state.e = 0x78U;
    state.h = 0x9aU;
    state.l = 0xbcU;
    state.interrupt_enable = 1U;
    state.interrupt_enable_delay = 1U;
    state.trap_saved_interrupt_enable = 1U;
    state.trap_saved_valid = 1U;
    i8085_test_set_state(&machine, &state);
    bad = i8085_test_state(&machine);
    assert(memcmp(&bad, &state, sizeof(state)) == 0);

    bad = state;
    bad.f = 0x08U; /* Bit 3 always reads as zero. */
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    bad = state;
    bad.version = 2U;
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    bad = state;
    bad.size = 4U;
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    bad = state;
    bad.model = BM_I8085_OKI_MSM80C85A;
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    bad = state;
    bad.interrupt_mask = 8U;
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    bad = state;
    bad.interrupt_enable = 0U;
    assert(bm_i8085_set_arch_state(&machine.cpu, &bad) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_i8085_get_arch_state(NULL, &bad) == BM_STATUS_INVALID_ARGUMENT);
    i8085_test_destroy(&machine);
}

/* RST 7.5 and TRAP latch rising edges; the RST 7.5 flip-flop is set even
 * while masked ([MCS85] p. 5-18; [OKI86] Table 1). */
static void
test_edge_latches(void)
{
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    i8085_test_create(&machine, NULL);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    state = i8085_test_state(&machine);
    assert(state.interrupt_mask == 7U && state.rst75_pending == 1U);
    state.rst75_pending = 0U;
    i8085_test_set_state(&machine, &state);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    assert(i8085_test_state(&machine).rst75_pending == 0U);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 0);
    i8085_test_signal(&machine, BM_I8085_SIGNAL_RST75, 1);
    assert(i8085_test_state(&machine).rst75_pending == 1U);

    i8085_test_signal(&machine, BM_I8085_SIGNAL_TRAP, 1);
    assert(i8085_test_state(&machine).trap_pending == 1U);
    assert(machine.cpu.ops.signal(machine.cpu.context, 99U, 1) ==
           BM_STATUS_UNSUPPORTED);
    i8085_test_destroy(&machine);
}

static void
test_opcode_classification(void)
{
    unsigned int opcode;
    unsigned int undocumented = 0U;

    for (opcode = 0U; opcode < 256U; ++opcode)
        if (bm_i8085_classify_opcode((uint8_t) opcode) ==
            BM_I8085_OPCODE_UNDOCUMENTED)
            ++undocumented;
    assert(undocumented == 10U);
    assert(bm_i8085_classify_opcode(0x20U) == BM_I8085_OPCODE_DOCUMENTED);
    assert(bm_i8085_classify_opcode(0x30U) == BM_I8085_OPCODE_DOCUMENTED);
    assert(bm_i8085_classify_opcode(0xfdU) == BM_I8085_OPCODE_UNDOCUMENTED);
}

static void
test_inspect(void)
{
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;
    uint64_t value = 0U;

    i8085_test_create(&machine, NULL);
    state = i8085_test_state(&machine);
    state.h = 0x12U;
    state.l = 0x34U;
    state.sp = 0xabcdU;
    i8085_test_set_state(&machine, &state);
    assert(machine.cpu.ops.inspect(machine.cpu.context, "hl", &value) ==
           BM_STATUS_OK && value == 0x1234U);
    assert(machine.cpu.ops.inspect(machine.cpu.context, "sp", &value) ==
           BM_STATUS_OK && value == 0xabcdU);
    assert(machine.cpu.ops.inspect(machine.cpu.context, "interrupt_mask",
                                   &value) == BM_STATUS_OK && value == 7U);
    assert(machine.cpu.ops.inspect(machine.cpu.context, "nope", &value) ==
           BM_STATUS_UNSUPPORTED);
    i8085_test_destroy(&machine);
}

int
main(void)
{
    test_create_rejects_invalid_configuration();
    test_reset_in_actions();
    test_arch_state_validation();
    test_edge_latches();
    test_opcode_classification();
    test_inspect();
    return 0;
}
