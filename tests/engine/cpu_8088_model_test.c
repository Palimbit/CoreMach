/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <assert.h>
#include <string.h>

typedef struct bus_capture {
    bm_bus_transaction_t transactions[32];
    size_t count;
} bus_capture_t;

typedef struct timing_capture {
    bm_808x_timing_observation_t last;
    size_t count;
} timing_capture_t;

typedef struct phase_capture {
    bm_808x_bus_phase_observation_t observations[16];
    size_t count;
} phase_capture_t;

typedef struct queue_capture {
    bm_8088_queue_event_t events[16];
    size_t count;
} queue_capture_t;

static void
capture_bus(void *context, const bm_bus_transaction_t *transaction)
{
    bus_capture_t *capture = context;
    if ((transaction->operation != BM_BUS_FETCH) &&
        (capture->count < 32U))
        capture->transactions[capture->count++] = *transaction;
}

static void
capture_timing(void *context,
               const bm_808x_timing_observation_t *observation)
{
    timing_capture_t *capture = context;
    capture->last = *observation;
    ++capture->count;
}

static void
capture_phase(void *context,
              const bm_808x_bus_phase_observation_t *observation)
{
    phase_capture_t *capture = context;

    assert(capture->count < 16U);
    capture->observations[capture->count++] = *observation;
}

static void
capture_queue(void *context, const bm_8088_queue_event_t *event)
{
    queue_capture_t *capture = context;

    assert(event->size == sizeof(*event));
    assert(event->version == BM_8088_QUEUE_EVENT_VERSION);
    assert(capture->count < 16U);
    capture->events[capture->count++] = *event;
}

static bm_status_t
io_fixture(void *context, bm_bus_transaction_t *transaction)
{
    (void) context;
    if (transaction->operation == BM_BUS_READ)
        transaction->value = transaction->address == 0x20U ? 0x34U : 0x12U;
    return BM_STATUS_OK;
}

static cpu_808x_test_config_t
intel_config(void)
{
    cpu_808x_test_config_t config = { 0 };
    config.model = BM_808X_INTEL_8088;
    return config;
}

static bm_808x_arch_state_t
execution_state(cpu_808x_test_machine_t *machine)
{
    bm_808x_arch_state_t state = cpu_808x_test_get_state(machine);
    state.cs = 0xf000U;
    state.ip = 0U;
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    return state;
}

static void
test_reset_flags_and_base_isa(void)
{
    static const uint8_t program[] = {
        0xb8U, 0x34U, 0x12U, /* MOV AX,1234h. */
        0x05U, 0x02U, 0x00U, /* ADD AX,2. */
        0xf4U
    };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;

    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    state = cpu_808x_test_get_state(&machine);
    assert(state.model == BM_808X_INTEL_8088);
    assert(state.cs == 0xffffU);
    assert(state.ip == 0U);
    assert(state.flags == 0xf002U);
    assert(state.md_write_enabled == 0U);
    assert(cpu_808x_test_run(&machine, 8U) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ax == 0x1236U);
    assert(state.halted == 1U);

    state.flags = 0xffffU;
    state.halted = 0U;
    state.ip = 0U;
    cpu_808x_test_set_state(&machine, &state);
    state = cpu_808x_test_get_state(&machine);
    assert(state.flags == 0xffd7U);
    assert(state.md_write_enabled == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_positive_opcode_map_and_silicon_aliases(void)
{
    cpu_808x_test_config_t config = intel_config();
    static const uint8_t alias_82_or[] = { 0x82U, 0xc8U, 0x01U };
    static const uint8_t alias_jcc[] = { 0x60U, 0x02U, 0x90U, 0x90U };
    static const uint8_t salc[] = { 0xd6U };
    static const uint8_t lock_alias[] = { 0xf1U, 0xa2U, 0x00U, 0x01U };
    static const uint8_t setmo[] = { 0xd0U, 0xf0U };
    static const uint8_t test_alias[] = { 0xf6U, 0xc8U, 0x01U };
    static const uint8_t push_alias[] = { 0xffU, 0xf8U };
    static const uint8_t idiv_minus_128[] = { 0xf6U, 0xfbU };
    static const uint8_t pop_cs[] = { 0x0fU };
    static const uint8_t ret_alias[] = { 0xc0U, 0x02U, 0x00U };
    static const uint8_t native_mode_probe[] = { 0x76U, 0x00U };
    static const uint8_t undefined_mov[] = { 0xc6U, 0xc8U, 0x00U };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bus_capture_t capture;
    bm_tick_t consumed = 0U;
    unsigned int opcode;

    /* Every primary byte has an explicit 8088 decision. Grouped holes are
     * classified separately by their ModR/M operation field. */
    for (opcode = 0U; opcode <= 0xffU; ++opcode)
        assert(bm_8088_classify_opcode((uint8_t) opcode, 0U) !=
               BM_8088_OPCODE_UNDEFINED);
    assert(bm_8088_classify_opcode(0x82U, 0xc8U) ==
           BM_8088_OPCODE_SILICON_ALIAS);
    assert(bm_8088_classify_opcode(0xd0U, 0xf0U) ==
           BM_8088_OPCODE_SILICON_UNDOCUMENTED);
    assert(bm_8088_classify_opcode(0xf6U, 0xc8U) ==
           BM_8088_OPCODE_SILICON_ALIAS);
    assert(bm_8088_classify_opcode(0xffU, 0xf8U) ==
           BM_8088_OPCODE_SILICON_ALIAS);
    assert(bm_8088_classify_opcode(0xc6U, 0xc8U) ==
           BM_8088_OPCODE_UNDEFINED);
    assert(bm_8088_classify_opcode(0xfeU, 0xd0U) ==
           BM_8088_OPCODE_UNDEFINED);

#define RUN_ALIAS(program, setup, assertion) do { \
        cpu_808x_test_machine_create(&machine, &config, (program), \
                                     sizeof(program)); \
        state = execution_state(&machine); \
        setup; \
        cpu_808x_test_set_state(&machine, &state); \
        assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK); \
        state = cpu_808x_test_get_state(&machine); \
        assertion; \
        cpu_808x_test_machine_destroy(&machine); \
    } while (0)

    RUN_ALIAS(alias_82_or, state.ax = 2U, assert((state.ax & 0xffU) == 3U));
    RUN_ALIAS(alias_jcc, state.flags |= 0x0800U, assert(state.ip == 4U));
    RUN_ALIAS(salc, state.flags |= 0x0001U, assert((state.ax & 0xffU) == 0xffU));
    RUN_ALIAS(setmo, state.ax = 1U, assert((state.ax & 0xffU) == 0xffU));
    RUN_ALIAS(test_alias, state.ax = 3U, assert(state.ip == 3U));
    RUN_ALIAS(push_alias, state.ax = 0x1234U,
              assert(state.sp == 0x00feU));
    RUN_ALIAS(idiv_minus_128,
              state.ax = 0xff80U; state.bx = 1U,
              assert(state.ax == 0x0080U));

    cpu_808x_test_machine_create(&machine, &config, lock_alias,
                                 sizeof(lock_alias));
    state = execution_state(&machine);
    state.ax = 0x005aU;
    cpu_808x_test_set_state(&machine, &state);
    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 1U);
    assert(capture.transactions[0].address == 0x0100U);
    assert((capture.transactions[0].attributes & BM_BUS_TRANSACTION_LOCKED) !=
           0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, &config, pop_cs, sizeof(pop_cs));
    state = execution_state(&machine);
    cpu_808x_test_poke(&machine, 0x20100U, 0x34U);
    cpu_808x_test_poke(&machine, 0x20101U, 0x12U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0x1234U);
    assert(state.sp == 0x0102U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, &config, ret_alias,
                                 sizeof(ret_alias));
    state = execution_state(&machine);
    cpu_808x_test_poke(&machine, 0x20100U, 0x78U);
    cpu_808x_test_poke(&machine, 0x20101U, 0x56U);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 0x5678U);
    assert(state.sp == 0x0104U);
    cpu_808x_test_machine_destroy(&machine);

    /* Bit 15 is fixed in the Intel FLAGS image, not a V30 MD mode switch. */
    RUN_ALIAS(native_mode_probe, state.flags = 0U, assert(state.ip == 2U));

    cpu_808x_test_machine_create(&machine, &config, undefined_mov,
                                 sizeof(undefined_mov));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_UNSUPPORTED);
    cpu_808x_test_machine_destroy(&machine);

#undef RUN_ALIAS
}

static void
test_byte_bus_memory_io_wrap_and_escape(void)
{
    static const uint8_t memory_program[] = { 0xa1U, 0x00U, 0x01U };
    static const uint8_t io_program[] = { 0xe5U, 0x20U };
    static const uint8_t wrap_program[] = { 0xa1U, 0x10U, 0x00U };
    static const uint8_t escape_program[] = { 0xd8U, 0x06U, 0x00U, 0x01U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bus_capture_t capture;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, &config, memory_program,
                                 sizeof(memory_program));
    cpu_808x_test_poke(&machine, 0x100U, 0x34U);
    cpu_808x_test_poke(&machine, 0x101U, 0x12U);
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_get_state(&machine).ax == 0x1234U);
    assert(capture.count == 2U);
    assert(capture.transactions[0].address == 0x100U);
    assert(capture.transactions[1].address == 0x101U);
    assert(capture.transactions[0].size == 1U);
    assert(capture.transactions[1].size == 1U);
    cpu_808x_test_machine_destroy(&machine);

    config.bus_capacity = 2U;
    cpu_808x_test_machine_create(&machine, &config, io_program,
                                 sizeof(io_program));
    assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0U, 0xffffU,
                      io_fixture, NULL) == BM_STATUS_OK);
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_get_state(&machine).ax == 0x1234U);
    assert(capture.count == 2U);
    assert(capture.transactions[0].space == BM_ADDRESS_IO);
    assert(capture.transactions[0].address == 0x20U);
    assert(capture.transactions[1].address == 0x21U);
    assert(capture.transactions[0].size == 1U);
    assert(capture.transactions[1].size == 1U);
    cpu_808x_test_machine_destroy(&machine);

    config.bus_capacity = 0U;
    cpu_808x_test_machine_create(&machine, &config, wrap_program,
                                 sizeof(wrap_program));
    cpu_808x_test_poke(&machine, 0U, 0x78U);
    cpu_808x_test_poke(&machine, 1U, 0x56U);
    state = execution_state(&machine);
    state.ds = 0xffffU;
    cpu_808x_test_set_state(&machine, &state);
    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(cpu_808x_test_get_state(&machine).ax == 0x5678U);
    assert(capture.transactions[0].address == 0U);
    assert(capture.transactions[1].address == 1U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, &config, escape_program,
                                 sizeof(escape_program));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    memset(&capture, 0, sizeof(capture));
    bm_bus_set_observer(machine.bus, capture_bus, &capture);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 2U);
    assert(capture.transactions[0].size == 1U);
    assert(capture.transactions[1].size == 1U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_interrupt_halt_queue_and_unknown_timing(void)
{
    static const uint8_t program[] = { 0xebU, 0x00U, 0xf4U };
    static const uint8_t hlt_program[] = { 0xf4U };
    static const uint8_t int3[] = { 0xccU };
    static const uint8_t vector[] = { 0x00U, 0x01U, 0x00U, 0xf0U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    timing_capture_t timing = { 0 };
    bm_tick_t consumed = 0U;
    uint64_t cycles = 99U;

    config.timing = capture_timing;
    config.timing_context = &timing;
    cpu_808x_test_machine_create(&machine, &config, program, sizeof(program));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(timing.count == 1U);
    assert(timing.last.prefetch_queue_capacity ==
           BM_808X_8088_PREFETCH_QUEUE_CAPACITY);
    assert(timing.last.prefetch_queue_flushed == 1U);
    assert(timing.last.execution_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_UNKNOWN);
    assert(timing.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_UNKNOWN);
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U);
    cpu_808x_test_machine_destroy(&machine);

    config.timing = NULL;
    config.timing_context = NULL;
    cpu_808x_test_machine_create(&machine, &config, int3, sizeof(int3));
    cpu_808x_test_write(&machine, 3U * 4U, vector, sizeof(vector));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U);
    assert(state.ip == 0x0100U);
    assert(state.sp == 0x00faU);
    assert((state.flags & 0x0300U) == 0U);
    assert(cpu_808x_test_peek(&machine, 0x200faU) == 0x01U);
    assert(cpu_808x_test_peek(&machine, 0x200fcU) == 0x00U);
    assert(cpu_808x_test_peek(&machine, 0x200fdU) == 0xf0U);
    assert(cpu_808x_test_peek(&machine, 0x200feU) == 0x02U);
    assert(cpu_808x_test_peek(&machine, 0x200ffU) == 0xf0U);
    cpu_808x_test_machine_destroy(&machine);

    cpu_808x_test_machine_create(&machine, &config, hlt_program,
                                 sizeof(hlt_program));
    cpu_808x_test_write(&machine, 2U * 4U, vector, sizeof(vector));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_IDLE);
    assert(cpu_808x_test_get_state(&machine).halted == 1U);
    assert(bm_engine_signal_cpu(machine.engine, 0U, BM_808X_SIGNAL_NMI, 1) ==
           BM_STATUS_OK);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.halted == 0U);
    assert(state.cs == 0xf000U);
    assert(state.ip == 0x0100U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_prefetch_state_round_trip(void)
{
    static const uint8_t empty_program[] = { 0U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t prefetch = {
        .size = sizeof(prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 3U,
        .count = 3U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0x90U, 0x90U, 0xf4U }
    };
    bm_808x_prefetch_state_t observed;
    bm_tick_t consumed = 0U;

    cpu_808x_test_machine_create(&machine, &config, empty_program, 0U);
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_OK);
    assert(bm_808x_get_prefetch_state(&machine.cpu, &observed) ==
           BM_STATUS_OK);
    assert(observed.pointer == 3U);
    assert(observed.count == 3U);
    assert(observed.capacity == BM_808X_8088_PREFETCH_QUEUE_CAPACITY);
    assert(memcmp(observed.bytes, prefetch.bytes, prefetch.count) == 0);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(bm_808x_get_prefetch_state(&machine.cpu, &observed) ==
           BM_STATUS_OK);
    assert(observed.pointer == 3U);
    assert(observed.count == 2U);
    assert(observed.bytes[0] == 0x90U);
    assert(observed.bytes[1] == 0xf4U);

    prefetch.capacity = BM_808X_V30_PREFETCH_QUEUE_CAPACITY;
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_INVALID_ARGUMENT);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_public_bus_phase_observer_survives_state_install(void)
{
    static const uint8_t nop[] = { 0x90U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t empty_prefetch = {
        .size = sizeof(empty_prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 0U,
        .count = 0U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0U }
    };
    phase_capture_t capture = { 0 };
    bm_tick_t consumed = 0U;

    config.bus_phase = capture_phase;
    config.bus_phase_context = &capture;
    cpu_808x_test_machine_create(&machine, &config, nop, sizeof(nop));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &empty_prefetch) ==
           BM_STATUS_OK);

    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 4U);
    assert(capture.observations[0].phase == BM_808X_BUS_PHASE_T1);
    assert(capture.observations[1].phase == BM_808X_BUS_PHASE_T2);
    assert(capture.observations[2].phase == BM_808X_BUS_PHASE_T3);
    assert(capture.observations[3].phase == BM_808X_BUS_PHASE_T4);
    assert(capture.observations[2].transaction.operation == BM_BUS_FETCH);
    assert(capture.observations[2].transaction.address == 0xf0000U);
    assert(capture.observations[2].transaction.value == 0x90U);
    assert(capture.observations[2].response_valid == 1U);
    assert(capture.observations[2].cpu_clock_known == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_intel_queue_events_are_logical_not_clocked(void)
{
    static const uint8_t prefixed_mov[] = {
        0x2eU, 0xb8U, 0x34U, 0x12U
    };
    static const uint8_t short_jump[] = { 0xebU, 0x00U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t prefetch = {
        .size = sizeof(prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 4U,
        .count = 4U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0xebU, 0x00U, 0x90U, 0x90U }
    };
    queue_capture_t capture = { 0 };
    bm_tick_t consumed = 0U;

    config.intel_queue_event = capture_queue;
    config.intel_queue_event_context = &capture;
    cpu_808x_test_machine_create(&machine, &config, prefixed_mov,
                                 sizeof(prefixed_mov));
    assert(capture.count == 0U); /* Host reset is not a guest queue event. */
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(capture.count == 0U);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 4U);
    assert(capture.events[0].kind == BM_8088_QUEUE_READ_FIRST);
    assert(capture.events[1].kind == BM_8088_QUEUE_READ_FIRST);
    assert(capture.events[2].kind == BM_8088_QUEUE_READ_SUBSEQUENT);
    assert(capture.events[3].kind == BM_8088_QUEUE_READ_SUBSEQUENT);
    assert(capture.events[0].value == 0x2eU);
    assert(capture.events[1].value == 0xb8U);
    assert(capture.events[2].value == 0x34U);
    assert(capture.events[3].value == 0x12U);
    assert(capture.events[0].cs == 0xf000U);
    assert(capture.events[0].ip == 0U);
    assert(capture.events[3].ip == 3U);
    assert(capture.events[0].count_before == 1U);
    assert(capture.events[0].count_after == 0U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    cpu_808x_test_machine_create(&machine, &config, short_jump,
                                 sizeof(short_jump));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_OK);
    assert(capture.count == 0U); /* Import is host setup, not a queue read. */
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 3U);
    assert(capture.events[0].kind == BM_8088_QUEUE_READ_FIRST);
    assert(capture.events[1].kind == BM_8088_QUEUE_READ_SUBSEQUENT);
    assert(capture.events[2].kind == BM_8088_QUEUE_FLUSH);
    assert(capture.events[2].ip == 2U);
    assert(capture.events[0].count_before == 4U);
    assert(capture.events[0].count_after == 3U);
    assert(capture.events[2].count_before == 2U);
    assert(capture.events[2].count_after == 0U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&capture, 0, sizeof(capture));
    config.model = BM_808X_NEC_V30;
    cpu_808x_test_machine_create(&machine, &config, prefixed_mov,
                                 sizeof(prefixed_mov));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(cpu_808x_test_step(&machine, &consumed) == BM_STATUS_OK);
    assert(capture.count == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_intel_clocked_prefetched_baseline(void)
{
    static const uint8_t nop[] = { 0x90U };
    static const uint8_t add_al[] = { 0x04U, 0x2dU };
    static const uint8_t jump[] = { 0xebU, 0x00U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t prefetch = {
        .size = sizeof(prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 4U,
        .count = 4U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0x90U, 0x90U, 0x90U, 0x90U }
    };
    timing_capture_t timing = { 0 };
    phase_capture_t phases = { 0 };
    uint64_t cycles = 0U;

    config.timing = capture_timing;
    config.timing_context = &timing;
    config.bus_phase = capture_phase;
    config.bus_phase_context = &phases;
    cpu_808x_test_machine_create(&machine, &config, nop, sizeof(nop));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_OK);
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 3U);
    assert(cpu_808x_test_get_state(&machine).ip == 1U);
    assert(timing.count == 1U);
    assert(timing.last.execution_clock_kind == BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(timing.last.boundary_clock_kind == BM_808X_EXECUTION_CLOCKS_EXACT);
    assert(timing.last.boundary_clocks_min == 3U);
    assert(timing.last.bus_active_clocks == 1U); /* Two Ti, then CODE T1. */
    assert(phases.count == 1U);
    assert(phases.observations[0].phase == BM_808X_BUS_PHASE_T1);
    assert(phases.observations[0].transaction.operation == BM_BUS_FETCH);
    assert(phases.observations[0].cpu_clock_known == 1U);
    assert(phases.observations[0].cpu_clock_index == 2U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&timing, 0, sizeof(timing));
    memset(&phases, 0, sizeof(phases));
    prefetch.bytes[0] = 0x04U;
    prefetch.bytes[1] = 0x2dU;
    cpu_808x_test_machine_create(&machine, &config, add_al, sizeof(add_al));
    state = execution_state(&machine);
    state.ax = 2U;
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_OK);
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_OK);
    assert(cycles == 4U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.ip == 2U);
    assert((state.ax & 0xffU) == 0x2fU);
    assert(timing.last.boundary_clocks_min == 4U);
    assert(timing.last.bus_active_clocks == 2U);
    assert(phases.count == 2U);
    assert(phases.observations[0].phase == BM_808X_BUS_PHASE_T1);
    assert(phases.observations[1].phase == BM_808X_BUS_PHASE_T2);
    assert(phases.observations[0].cpu_clock_known == 1U);
    assert(phases.observations[0].cpu_clock_index == 2U);
    assert(phases.observations[1].cpu_clock_index == 3U);
    cpu_808x_test_machine_destroy(&machine);

    /* All sixteen byte/word accumulator-immediate ALU forms share the
     * documented four-clock full-queue case and CODE T1/T2 positions. */
    for (unsigned int operation = 0U; operation < 16U; ++operation) {
        uint8_t immediate_alu[] = {
            (uint8_t) (0x04U + (operation / 2U) * 8U + operation % 2U),
            0x2dU, 0x12U
        };
        uint16_t expected_ip = (uint16_t) (2U + (operation % 2U));

        memset(&timing, 0, sizeof(timing));
        memset(&phases, 0, sizeof(phases));
        prefetch.bytes[0] = immediate_alu[0];
        prefetch.bytes[1] = immediate_alu[1];
        prefetch.bytes[2] = immediate_alu[2];
        cpu_808x_test_machine_create(&machine, &config, immediate_alu,
                                     sizeof(immediate_alu));
        state = execution_state(&machine);
        state.ax = 2U;
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
               BM_STATUS_OK);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(cycles == 4U);
        assert(cpu_808x_test_get_state(&machine).ip == expected_ip);
        assert(timing.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(timing.last.boundary_clocks_min == 4U);
        assert(phases.count == 2U);
        assert(phases.observations[0].phase == BM_808X_BUS_PHASE_T1);
        assert(phases.observations[0].cpu_clock_index == 2U);
        assert(phases.observations[1].phase == BM_808X_BUS_PHASE_T2);
        assert(phases.observations[1].cpu_clock_index == 3U);
        cpu_808x_test_machine_destroy(&machine);
    }

    /* Clocked mode rejects unproven instructions before changing state. */
    prefetch.bytes[0] = 0xebU;
    prefetch.bytes[1] = 0x00U;
    cpu_808x_test_machine_create(&machine, &config, jump, sizeof(jump));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
           BM_STATUS_OK);
    cycles = 99U;
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U);
    assert(cpu_808x_test_get_state(&machine).ip == 0U);
    cpu_808x_test_machine_destroy(&machine);

    memset(&timing, 0, sizeof(timing));
    memset(&phases, 0, sizeof(phases));
    cpu_808x_test_machine_create(&machine, &config, nop, sizeof(nop));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U);
    assert(cpu_808x_test_get_state(&machine).ip == 0U);
    assert(timing.count == 0U);
    assert(phases.count == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_intel_clocked_nop_stream_keeps_prefetch_in_flight(void)
{
    static const uint8_t program[] = {
        0x90U, 0x90U, 0x90U, 0x90U, 0x90U, 0x90U, 0x90U, 0x90U
    };
    static const bm_808x_bus_phase_t expected_phases[][3] = {
        { BM_808X_BUS_PHASE_T1 },
        { BM_808X_BUS_PHASE_T2, BM_808X_BUS_PHASE_T3,
          BM_808X_BUS_PHASE_T4 },
        { BM_808X_BUS_PHASE_T1, BM_808X_BUS_PHASE_T2,
          BM_808X_BUS_PHASE_T3 },
        { BM_808X_BUS_PHASE_T4, BM_808X_BUS_PHASE_T1,
          BM_808X_BUS_PHASE_T2 }
    };
    static const size_t expected_phase_counts[] = { 1U, 3U, 3U, 3U };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t prefetch = {
        .size = sizeof(prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 4U,
        .count = 4U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0x90U, 0x90U, 0x90U, 0x90U }
    };
    timing_capture_t timing = { 0 };
    phase_capture_t phases = { 0 };
    uint64_t cycles = 0U;

    config.timing = capture_timing;
    config.timing_context = &timing;
    config.bus_phase = capture_phase;
    config.bus_phase_context = &phases;
    cpu_808x_test_machine_create(&machine, &config, program,
                                 sizeof(program));
    state = execution_state(&machine);
    cpu_808x_test_set_state(&machine, &state);
    assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) == BM_STATUS_OK);
    for (size_t step = 0U; step < 4U; ++step) {
        memset(&timing, 0, sizeof(timing));
        memset(&phases, 0, sizeof(phases));
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(cycles == 3U);
        assert(cpu_808x_test_get_state(&machine).ip == step + 1U);
        assert(timing.count == 1U);
        assert(timing.last.boundary_clock_kind == BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(timing.last.boundary_clocks_min == 3U);
        assert(phases.count == expected_phase_counts[step]);
        for (size_t index = 0U; index < phases.count; ++index) {
            assert(phases.observations[index].phase ==
                   expected_phases[step][index]);
            assert(phases.observations[index].cpu_clock_known == 1U);
            assert(phases.observations[index].cpu_clock_index ==
                   step * 3U + (step == 0U ? index + 2U : index));
        }
    }
    cpu_808x_test_machine_destroy(&machine);

    /* A three-byte accumulator operand may consume the remaining queue
     * while a CODE transfer started by the preceding NOP is still in flight. */
    {
        static const uint8_t mixed_program[] = {
            0x90U, 0x05U, 0x2dU, 0x12U, 0x90U, 0x90U
        };

        prefetch.bytes[0] = 0x90U;
        prefetch.bytes[1] = 0x05U;
        prefetch.bytes[2] = 0x2dU;
        prefetch.bytes[3] = 0x12U;
        cpu_808x_test_machine_create(&machine, &config, mixed_program,
                                     sizeof(mixed_program));
        state = execution_state(&machine);
        state.ax = 2U;
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
               BM_STATUS_OK);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(cycles == 3U);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(cycles == 4U);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 4U);
        assert(state.ax == 0x122fU);
        cpu_808x_test_machine_destroy(&machine);
    }

    /* An immediate not yet present in the queue still needs a separately
     * validated demand-prefetch schedule; reject it before changing IP. */
    {
        static const uint8_t incomplete_program[] = {
            0x05U, 0x2dU, 0x12U
        };

        prefetch.pointer = 1U;
        prefetch.count = 1U;
        prefetch.bytes[0] = 0x05U;
        cpu_808x_test_machine_create(&machine, &config, incomplete_program,
                                     sizeof(incomplete_program));
        state = execution_state(&machine);
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
               BM_STATUS_OK);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_UNSUPPORTED);
        assert(cycles == 0U);
        assert(cpu_808x_test_get_state(&machine).ip == 0U);
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_intel_clocked_direct_accumulator_memory_reads(void)
{
    cpu_808x_test_config_t config = intel_config();
    timing_capture_t timing = { 0 };
    phase_capture_t phases = { 0 };
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    bm_808x_prefetch_state_t prefetch = {
        .size = sizeof(prefetch),
        .version = BM_808X_PREFETCH_STATE_VERSION,
        .pointer = 4U,
        .count = 4U,
        .capacity = BM_808X_8088_PREFETCH_QUEUE_CAPACITY,
        .bytes = { 0xa0U, 0x40U, 0x00U, 0x90U }
    };
    uint64_t cycles = 0U;

    config.timing = capture_timing;
    config.timing_context = &timing;
    config.bus_phase = capture_phase;
    config.bus_phase_context = &phases;
    for (unsigned int word = 0U; word < 2U; ++word) {
        uint8_t program[] = {
            (uint8_t) (0xa0U + word), 0x40U, 0x00U, 0x90U, 0x90U
        };

        memset(&timing, 0, sizeof(timing));
        memset(&phases, 0, sizeof(phases));
        prefetch.bytes[0] = program[0];
        cpu_808x_test_machine_create(&machine, &config, program,
                                     sizeof(program));
        cpu_808x_test_poke(&machine, 0x40U, 0x12U);
        cpu_808x_test_poke(&machine, 0x41U, 0x34U);
        state = execution_state(&machine);
        cpu_808x_test_set_state(&machine, &state);
        assert(bm_808x_set_prefetch_state(&machine.cpu, &prefetch) ==
               BM_STATUS_OK);
        assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
               BM_STATUS_OK);
        assert(cycles == (word ? 14U : 10U));
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == 3U);
        assert((state.ax & (word ? 0xffffU : 0xffU)) ==
               (word ? 0x3412U : 0x12U));
        assert(timing.count == 1U);
        assert(timing.last.boundary_clock_kind == BM_808X_EXECUTION_CLOCKS_EXACT);
        assert(timing.last.boundary_clocks_min == cycles);
        assert(timing.last.operand_transactions == (word ? 2U : 1U));
        assert(phases.count == (word ? 12U : 8U));
        for (size_t index = 0U; index < phases.count; ++index) {
            assert(phases.observations[index].cpu_clock_known == 1U);
            assert(phases.observations[index].cpu_clock_index == index + 2U);
            assert(phases.observations[index].transaction.operation ==
                   (index < 4U ? BM_BUS_FETCH : BM_BUS_READ));
        }
        cpu_808x_test_machine_destroy(&machine);
    }
}

static void
test_intel_provisional_clocked_cold_reset_and_control_flow(void)
{
    static const uint8_t program[] = {
        0xb0U, 0x12U,             /* MOV AL,12h. */
        0xa2U, 0x40U, 0x00U,      /* MOV [0040h],AL. */
        0xe4U, 0x20U,             /* IN AL,20h. */
        0xebU, 0x02U,             /* JMP over HLT and NOP. */
        0xf4U, 0x90U,
        0xcdU, 0x20U,             /* INT 20h. */
        0xf4U                      /* HLT after IRET. */
    };
    static const uint16_t expected_ip[] = {
        0U, 2U, 5U, 7U, 11U, 0x0100U, 13U, 14U
    };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    timing_capture_t timing = { 0 };
    uint64_t cycles = 0U;

    config.bus_capacity = 2U;
    config.timing = capture_timing;
    config.timing_context = &timing;
    cpu_808x_test_machine_create(&machine, &config, program,
                                 sizeof(program));
    assert(bm_bus_map(machine.bus, BM_ADDRESS_IO, 0x20U, 0x20U,
                      io_fixture, NULL) == BM_STATUS_OK);
    /* IVT 20h -> F000:0100; the handler is a single IRET. */
    cpu_808x_test_poke(&machine, 0x80U, 0x00U);
    cpu_808x_test_poke(&machine, 0x81U, 0x01U);
    cpu_808x_test_poke(&machine, 0x82U, 0x00U);
    cpu_808x_test_poke(&machine, 0x83U, 0xf0U);
    cpu_808x_test_poke(&machine, 0xf0100U, 0xcfU);
    state = cpu_808x_test_get_state(&machine);
    state.ss = 0x2000U;
    state.sp = 0x0100U;
    cpu_808x_test_set_state(&machine, &state);

    /* The strict path must still reject reset's empty queue unchanged. */
    assert(bm_808x_step_clocked(machine.cpu.context, 0U, &cycles) ==
           BM_STATUS_UNSUPPORTED);
    assert(cycles == 0U);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xffffU && state.ip == 0U);

    for (size_t step = 0U; step < sizeof(expected_ip) /
                                  sizeof(expected_ip[0]); ++step) {
        assert(bm_808x_step_clocked_provisional(
                   machine.cpu.context, 0U, &cycles) == BM_STATUS_OK);
        assert(cycles >= 4U);
        assert(timing.count == step + 1U);
        assert(timing.last.boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_PROVISIONAL);
        assert(timing.last.boundary_clocks_min == cycles);
        assert(timing.last.boundary_clocks_max == cycles);
        assert(cycles == timing.last.prefetch_phase_clocks +
                         timing.last.operand_bus_clocks + 4U);
        assert(timing.last.execution_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_UNKNOWN);
        state = cpu_808x_test_get_state(&machine);
        assert(state.ip == expected_ip[step]);
        if (step == 2U)
            assert(cpu_808x_test_peek(&machine, 0x40U) == 0x12U);
        if (step == 3U)
            assert((state.ax & 0xffU) == 0x34U);
        if (step == 5U)
            assert(state.cs == 0xf000U && state.sp == 0x00faU);
        if (step == 6U)
            assert(state.cs == 0xf000U && state.sp == 0x0100U);
    }
    assert(state.halted == 1U);
    assert(bm_808x_step_clocked_provisional(
               machine.cpu.context, 0U, &cycles) == BM_STATUS_IDLE);
    assert(cycles == 0U);
    cpu_808x_test_machine_destroy(&machine);
}

static void
test_intel_provisional_clocked_engine_integration(void)
{
    static const uint8_t program[] = {
        0xb0U, 0x12U, /* MOV AL,12h. */
        0xf4U         /* HLT. */
    };
    cpu_808x_test_config_t config = intel_config();
    cpu_808x_test_machine_t machine;
    bm_808x_arch_state_t state;
    timing_capture_t timing = { 0 };

    config.provisional_clocked = 1;
    config.timing = capture_timing;
    config.timing_context = &timing;
    cpu_808x_test_machine_create(&machine, &config, program,
                                 sizeof(program));
    assert(bm_engine_run_for(machine.engine, 50000U) == BM_STATUS_OK);
    state = cpu_808x_test_get_state(&machine);
    assert(state.cs == 0xf000U && state.ip == 3U);
    assert((state.ax & 0xffU) == 0x12U);
    assert(state.halted == 1U);
    assert(timing.count == 3U);
    assert(timing.last.boundary_clock_kind ==
           BM_808X_EXECUTION_CLOCKS_PROVISIONAL);
    assert(bm_engine_now(machine.engine) == 50000U);
    cpu_808x_test_machine_destroy(&machine);
}

int
main(void)
{
    test_reset_flags_and_base_isa();
    test_positive_opcode_map_and_silicon_aliases();
    test_byte_bus_memory_io_wrap_and_escape();
    test_interrupt_halt_queue_and_unknown_timing();
    test_prefetch_state_round_trip();
    test_public_bus_phase_observer_survives_state_install();
    test_intel_queue_events_are_logical_not_clocked();
    test_intel_clocked_prefetched_baseline();
    test_intel_clocked_nop_stream_keeps_prefetch_in_flight();
    test_intel_clocked_direct_accumulator_memory_reads();
    test_intel_provisional_clocked_cold_reset_and_control_flow();
    test_intel_provisional_clocked_engine_integration();
    return 0;
}
