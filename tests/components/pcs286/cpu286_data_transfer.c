/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored synthetic instruction and bus fixtures; no ROM/media vectors.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CODE_BASE = 0x30000, TRACE_MAX = 64 };

typedef struct test_bus {
    uint8_t bytes[0x40000];
    bm_bus_transaction_t trace[TRACE_MAX];
    unsigned count;
    unsigned fail_at;
} test_bus_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *request)
{
    test_bus_t *bus = context;
    unsigned i;
    if (bus == NULL || request == NULL || request->size == 0U ||
        request->size > 2U || bus->count == TRACE_MAX ||
        request->address > sizeof(bus->bytes) - request->size)
        return BM_STATUS_UNMAPPED;
    bus->trace[bus->count++] = *request;
    if (bus->fail_at == bus->count)
        return BM_STATUS_DEVICE_ERROR;
    if (request->operation != BM_BUS_WRITE)
        request->value = 0U;
    for (i = 0U; i < request->size; ++i) {
        unsigned shift = i * 8U;
        unsigned address = (unsigned) request->address + i;
        if (request->operation == BM_BUS_WRITE)
            bus->bytes[address] = (uint8_t) (request->value >> shift);
        else
            request->value |= (uint64_t) bus->bytes[address] << shift;
    }
    request->wait_states = 2U;
    return BM_STATUS_OK;
}

static bm_cpu_t make_cpu(test_bus_t *bus)
{
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    bm_cpu_t cpu = {0};
    config.size = sizeof(config);
    config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus;
    config.access_context = bus;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    return cpu;
}

static bm_286_arch_state_t state_of(bm_cpu_t *cpu)
{
    bm_286_arch_state_t state = {0};
    assert(bm_286_get_arch_state(cpu, &state) == BM_STATUS_OK);
    return state;
}

static uint16_t word_value(const bm_286_arch_state_t *state, unsigned number)
{
    switch (number) {
    case 0U: return state->ax;
    case 1U: return state->cx;
    case 2U: return state->dx;
    case 3U: return state->bx;
    case 4U: return state->sp;
    case 5U: return state->bp;
    case 6U: return state->si;
    default: return state->di;
    }
}

static void prepare(bm_cpu_t *cpu, test_bus_t *bus,
                    const uint8_t *code, size_t length,
                    bm_286_arch_state_t *state)
{
    assert(length < 32U);
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    *state = state_of(cpu);
    state->cs.selector = 0x3000U;
    state->cs.base = CODE_BASE;
    state->ds.selector = 0x0000U;
    state->ds.base = 0U;
    state->es.selector = 0x1000U;
    state->es.base = 0x10000U;
    state->ss.selector = 0x2000U;
    state->ss.base = 0x20000U;
    state->flags = 0x0ad7U; /* Nonzero arithmetic flags must survive moves. */
    state->ip = 0U;
    memset(bus->bytes + CODE_BASE, 0, 32U);
    memcpy(bus->bytes + CODE_BASE, code, length);
    bus->count = 0U;
    bus->fail_at = 0U;
}

static void run_ok(bm_cpu_t *cpu, bm_286_arch_state_t *state,
                   bm_286_boundary_t *boundary)
{
    assert(bm_286_set_arch_state(cpu, state) == BM_STATUS_OK);
    assert(bm_286_step(cpu, boundary) == BM_STATUS_OK);
    assert(boundary->kind == BM_286_BOUNDARY_INSTRUCTION);
    assert(boundary->timing == BM_286_TIMING_UNKNOWN);
    assert(boundary->cpu_cycles == boundary->bus_wait_cycles);
    assert(state_of(cpu).flags == state->flags);
}

static void test_registers(void)
{
    test_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary;
    unsigned n;
    assert(bus != NULL);
    cpu = make_cpu(bus);
    for (n = 0U; n < 8U; ++n) {
        uint8_t code[] = {(uint8_t) (0xb0U + n), 0xa5U};
        prepare(&cpu, bus, code, sizeof(code), &state);
        state.ax = state.cx = state.dx = state.bx = 0x1234U;
        run_ok(&cpu, &state, &boundary);
        state = state_of(&cpu);
        assert((n < 4U ? word_value(&state, n) & 0xffU :
                word_value(&state, n - 4U) >> 8) == 0xa5U);
        assert((n < 4U ? word_value(&state, n) >> 8 :
                word_value(&state, n - 4U) & 0xffU) ==
               (n < 4U ? 0x12U : 0x34U));
        assert(state.ip == 2U && bus->count == 2U);
    }
    for (n = 0U; n < 8U; ++n) {
        uint8_t code[] = {(uint8_t) (0xb8U + n), 0x78U, 0x56U};
        prepare(&cpu, bus, code, sizeof(code), &state);
        run_ok(&cpu, &state, &boundary);
        state = state_of(&cpu);
        assert(word_value(&state, n) == 0x5678U && state.ip == 3U);
    }
    {
        const uint8_t mov8[] = {0x88U, 0xe0U}; /* MOV AL,AH */
        prepare(&cpu, bus, mov8, sizeof(mov8), &state);
        state.ax = 0x9a12U;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x9a9aU);
    }
    {
        const uint8_t mov16[] = {0x8bU, 0xc3U}; /* MOV AX,BX */
        prepare(&cpu, bus, mov16, sizeof(mov16), &state);
        state.bx = 0x3456U;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x3456U);
    }
    {
        const uint8_t xchg8[] = {0x86U, 0xc4U}; /* XCHG AH,AL */
        prepare(&cpu, bus, xchg8, sizeof(xchg8), &state);
        state.ax = 0x1234U;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x3412U);
    }
    {
        const uint8_t xchg16[] = {0x87U, 0xc3U}; /* XCHG BX,AX */
        prepare(&cpu, bus, xchg16, sizeof(xchg16), &state);
        state.ax = 0x1111U; state.bx = 0x2222U;
        run_ok(&cpu, &state, &boundary);
        state = state_of(&cpu);
        assert(state.ax == 0x2222U && state.bx == 0x1111U);
    }
    {
        const uint8_t xchg_ax[] = {0x93U}; /* XCHG AX,BX */
        prepare(&cpu, bus, xchg_ax, sizeof(xchg_ax), &state);
        state.ax = 0xaaaaU; state.bx = 0xbbbbU;
        run_ok(&cpu, &state, &boundary);
        state = state_of(&cpu);
        assert(state.ax == 0xbbbbU && state.bx == 0xaaaaU);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_effective_addresses(void)
{
    test_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary;
    unsigned mode, rm;
    static const uint16_t base[8] = {0x70U, 0x80U, 0x40U, 0x50U,
                                     0x20U, 0x30U, 0x20U, 0x50U};
    assert(bus != NULL);
    cpu = make_cpu(bus);
    for (mode = 0U; mode < 3U; ++mode) {
        for (rm = 0U; rm < 8U; ++rm) {
            uint8_t code[] = {0x8aU, (uint8_t) ((mode << 6) | rm),
                              0xf0U, 0xffU};
            unsigned offset = mode == 0U && rm == 6U ? 0xfff0U :
                (unsigned) (uint16_t) (base[rm] +
                (mode == 1U ? (uint16_t) -16 :
                 mode == 2U ? 0xfff0U : 0U));
            int bp = (rm == 2U || rm == 3U || (rm == 6U && mode != 0U));
            unsigned address = (bp ? 0x20000U : 0U) + offset;
            prepare(&cpu, bus, code, sizeof(code), &state);
            state.bx = 0x50U; state.bp = 0x20U;
            state.si = 0x20U; state.di = 0x30U;
            bus->bytes[address] = 0x5aU;
            bus->bytes[(bp ? 0U : 0x20000U) + offset] = 0xa5U;
            run_ok(&cpu, &state, &boundary);
            assert((state_of(&cpu).ax & 0xffU) == 0x5aU);
            assert(bus->trace[bus->count - 1U].address == address);
            assert(bus->trace[bus->count - 1U].operation == BM_BUS_READ);
        }
    }
    {
        const uint8_t wrap[] = {0x8aU, 0x00U}; /* [BX+SI] */
        prepare(&cpu, bus, wrap, sizeof(wrap), &state);
        state.bx = 0xffffU; state.si = 2U;
        bus->bytes[1] = 0x77U;
        run_ok(&cpu, &state, &boundary);
        assert((state_of(&cpu).ax & 0xffU) == 0x77U);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_overrides_moffs_and_segments(void)
{
    test_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary;
    unsigned segment;
    const uint8_t prefixes[4] = {0x26U, 0x2eU, 0x36U, 0x3eU};
    const unsigned bases[4] = {0x10000U, CODE_BASE, 0x20000U, 0U};
    assert(bus != NULL);
    cpu = make_cpu(bus);
    for (segment = 0U; segment < 4U; ++segment) {
        uint8_t code[] = {prefixes[segment], 0xa0U, 0x80U, 0x00U};
        prepare(&cpu, bus, code, sizeof(code), &state);
        bus->bytes[bases[segment] + 0x80U] = 0x61U;
        run_ok(&cpu, &state, &boundary);
        assert((state_of(&cpu).ax & 0xffU) == 0x61U);
        assert(bus->trace[bus->count - 1U].address == bases[segment] + 0x80U);
    }
    {
        const uint8_t last_wins[] = {0x26U, 0x36U, 0xa0U, 0x40U, 0U};
        prepare(&cpu, bus, last_wins, sizeof(last_wins), &state);
        bus->bytes[0x20040U] = 0x27U;
        run_ok(&cpu, &state, &boundary);
        assert((state_of(&cpu).ax & 0xffU) == 0x27U);
        assert(state_of(&cpu).ip == 5U);
    }
    {
        const uint8_t move_ds[] = {0x8eU, 0xd8U}; /* MOV DS,AX */
        prepare(&cpu, bus, move_ds, sizeof(move_ds), &state);
        state.ax = 0x1234U;
        run_ok(&cpu, &state, &boundary);
        state = state_of(&cpu);
        assert(state.ds.selector == 0x1234U && state.ds.base == 0x12340U);
        assert(state.ds.limit == 0xffffU && state.cs.base == CODE_BASE);
    }
    {
        const uint8_t save_cs[] = {0x8cU, 0xc8U}; /* MOV AX,CS */
        prepare(&cpu, bus, save_cs, sizeof(save_cs), &state);
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x3000U);
    }
    {
        const uint8_t move_es[] = {0x8eU, 0x06U, 0x00U, 0x01U};
        prepare(&cpu, bus, move_es, sizeof(move_es), &state);
        bus->bytes[0x100U] = 0x21U; bus->bytes[0x101U] = 0x43U;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).es.base == 0x43210U);
    }
    {
        const uint8_t override_modrm[] = {0x26U, 0x89U, 0x07U};
        prepare(&cpu, bus, override_modrm, sizeof(override_modrm), &state);
        state.bx = 0x101U; state.ax = 0x4567U;
        run_ok(&cpu, &state, &boundary);
        assert(bus->bytes[0x10101U] == 0x67U);
        assert(bus->bytes[0x10102U] == 0x45U);
        assert(bus->trace[bus->count - 1U].address == 0x10102U);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_words_and_failures(void)
{
    test_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary;
    assert(bus != NULL);
    cpu = make_cpu(bus);
    {
        const uint8_t odd_read[] = {0xa1U, 0x01U, 0x01U};
        prepare(&cpu, bus, odd_read, sizeof(odd_read), &state);
        bus->bytes[0x101U] = 0x34U; bus->bytes[0x102U] = 0x12U;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x1234U && bus->count == 5U);
        assert(bus->trace[3].size == 1U && bus->trace[4].size == 1U);
        assert(boundary.bus_wait_cycles == 10U);
    }
    {
        const uint8_t even_write[] = {0xa3U, 0x00U, 0x01U};
        prepare(&cpu, bus, even_write, sizeof(even_write), &state);
        state.ax = 0xabcdU;
        run_ok(&cpu, &state, &boundary);
        assert(bus->bytes[0x100U] == 0xcdU && bus->bytes[0x101U] == 0xabU);
        assert(bus->count == 4U && bus->trace[3].size == 2U);
    }
    {
        const uint8_t odd_read_failure[] = {0xa1U, 0x01U, 0x01U};
        prepare(&cpu, bus, odd_read_failure, sizeof(odd_read_failure), &state);
        state.ax = 0x9999U;
        bus->fail_at = 5U;
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->count == 5U && state_of(&cpu).ax == 0x9999U);
        assert(state_of(&cpu).ip == 0U);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 5U);
    }
    {
        const uint8_t imm_memory[] = {0xc7U, 0x06U, 0x01U, 0x01U,
                                      0x78U, 0x56U};
        prepare(&cpu, bus, imm_memory, sizeof(imm_memory), &state);
        run_ok(&cpu, &state, &boundary);
        assert(bus->bytes[0x101U] == 0x78U && bus->bytes[0x102U] == 0x56U);
        assert(bus->trace[6].operation == BM_BUS_WRITE);
    }
    {
        const uint8_t imm_register[] = {0xc7U, 0xc0U, 0x34U, 0x12U};
        prepare(&cpu, bus, imm_register, sizeof(imm_register), &state);
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0x1234U && bus->count == 4U);
    }
    {
        const uint8_t byte_store[] = {0xa2U, 0x00U, 0x01U};
        prepare(&cpu, bus, byte_store, sizeof(byte_store), &state);
        state.ax = 0x125aU;
        run_ok(&cpu, &state, &boundary);
        assert(bus->bytes[0x100U] == 0x5aU);
        assert(bus->trace[3].operation == BM_BUS_WRITE);
    }
    {
        const uint8_t byte_immediate[] = {0xc6U, 0x06U, 0x00U,
                                          0x01U, 0x22U};
        prepare(&cpu, bus, byte_immediate, sizeof(byte_immediate), &state);
        run_ok(&cpu, &state, &boundary);
        assert(bus->bytes[0x100U] == 0x22U);
    }
    {
        const uint8_t memory_load[] = {0x8bU, 0x07U};
        prepare(&cpu, bus, memory_load, sizeof(memory_load), &state);
        state.bx = 0x100U;
        bus->bytes[0x100U] = 0xefU; bus->bytes[0x101U] = 0xbeU;
        run_ok(&cpu, &state, &boundary);
        assert(state_of(&cpu).ax == 0xbeefU);
        assert(bus->trace[2].size == 2U);
    }
    {
        const uint8_t odd_write[] = {0xa3U, 0x01U, 0x01U};
        prepare(&cpu, bus, odd_write, sizeof(odd_write), &state);
        state.ax = 0x1234U;
        bus->bytes[0x102U] = 0xeeU;
        bus->fail_at = 5U;
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->bytes[0x101U] == 0x34U && bus->bytes[0x102U] == 0xeeU);
        assert(state_of(&cpu).ip == 0U && state_of(&cpu).ax == 0x1234U);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 5U);
    }
    {
        const uint8_t limit[] = {0xa1U, 0xffU, 0xffU};
        prepare(&cpu, bus, limit, sizeof(limit), &state);
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_OK);
        assert(boundary.has_vector && boundary.vector == 13 && boundary.kind == BM_286_BOUNDARY_EXCEPTION);
        assert(bus->count == 8U && state_of(&cpu).ax == state.ax);
    }
    {
        const uint8_t last_byte[] = {0xa0U, 0xffU, 0xffU};
        prepare(&cpu, bus, last_byte, sizeof(last_byte), &state);
        bus->bytes[0xffffU] = 0x6dU;
        run_ok(&cpu, &state, &boundary);
        assert((state_of(&cpu).ax & 0xffU) == 0x6dU);
    }
    {
        const uint8_t fetch_fail[] = {0xb8U, 0x34U, 0x12U};
        prepare(&cpu, bus, fetch_fail, sizeof(fetch_fail), &state);
        state.ax = 0x9999U;
        bus->fail_at = 3U;
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->count == 3U && state_of(&cpu).ax == 0x9999U);
        assert(state_of(&cpu).ip == 0U);
    }
    {
        const uint8_t final_immediate[] = {0xc6U, 0x06U, 0x00U,
                                           0x01U, 0x5aU};
        prepare(&cpu, bus, final_immediate, sizeof(final_immediate), &state);
        bus->bytes[0x100U] = 0x99U;
        bus->fail_at = 5U;
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->bytes[0x100U] == 0x99U && state_of(&cpu).ip == 0U);
        assert(bus->count == 5U);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 5U);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_rejections_and_instances(void)
{
    test_bus_t *a = calloc(1U, sizeof(*a));
    test_bus_t *b = calloc(1U, sizeof(*b));
    bm_cpu_t ca, cb;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary;
    static const uint8_t rejected[][4] = {
        {0xf0U, 0xa0U, 0x00U, 0U}, /* LOCK path not yet modelled */
        {0xf3U, 0xa0U, 0x00U, 0U}, /* REP path not yet modelled */
        {0xc7U, 0xc8U, 0x00U, 0U}, /* invalid C7 /1 */
        {0x8eU, 0xc8U, 0x00U, 0U}, /* MOV CS invalid */
        {0x87U, 0x07U, 0x00U, 0U}, /* memory XCHG requires LOCK */
        {0x66U, 0xb8U, 0x00U, 0U}  /* not a 286 operand prefix */
    };
    unsigned i;
    assert(a != NULL && b != NULL);
    ca = make_cpu(a); cb = make_cpu(b);
    for (i = 0U; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        prepare(&ca, a, rejected[i], sizeof(rejected[i]), &state);
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(state_of(&ca).ip == 0U);
        assert(a->count <= 2U); /* never reaches a guest data transaction */
    }
    {
        const uint8_t protected_mov[] = {0xa2U, 0x00U, 0x01U};
        prepare(&ca, a, protected_mov, sizeof(protected_mov), &state);
        state.msw |= 1U;
        state.ax = 0x42U;
        a->bytes[0x100U] = 0x99U;
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(a->bytes[0x100U] == 0x99U && a->count == 0U);
    }
    {
        const uint8_t invalid_cs[] = {0x90U};
        prepare(&ca, a, invalid_cs, sizeof(invalid_cs), &state);
        state.cs.valid = 0U;
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(a->count == 0U && state_of(&ca).ip == 0U);
    }
    {
        const uint8_t truncated_fetch[] = {0xb8U, 0x34U, 0x12U};
        prepare(&ca, a, truncated_fetch, sizeof(truncated_fetch), &state);
        state.ax = 0x9999U;
        state.cs.limit = 1U;
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(a->count == 2U && state_of(&ca).ip == 0U);
        assert(state_of(&ca).ax == 0x9999U);
    }
    {
        const uint8_t exact_length[] = {0x26U, 0x26U, 0x26U, 0x26U,
                                        0x26U, 0x26U, 0x26U, 0x26U,
                                        0x26U, 0x90U};
        prepare(&ca, a, exact_length, sizeof(exact_length), &state);
        run_ok(&ca, &state, &boundary);
        assert(a->count == 10U && state_of(&ca).ip == 10U);
    }
    {
        const uint8_t too_long[] = {0x26U, 0x26U, 0x26U, 0x26U, 0x26U,
                                    0x26U, 0x26U, 0x26U, 0x26U, 0x26U, 0x90U};
        prepare(&ca, a, too_long, sizeof(too_long), &state);
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(a->count == 10U && state_of(&ca).ip == 0U);
    }
    {
        const uint8_t left[] = {0xb8U, 0x11U, 0x11U};
        const uint8_t right[] = {0xb8U, 0x22U, 0x22U};
        prepare(&ca, a, left, sizeof(left), &state);
        assert(bm_286_set_arch_state(&ca, &state) == BM_STATUS_OK);
        prepare(&cb, b, right, sizeof(right), &state);
        assert(bm_286_set_arch_state(&cb, &state) == BM_STATUS_OK);
        assert(bm_286_step(&ca, &boundary) == BM_STATUS_OK);
        assert(bm_286_step(&cb, &boundary) == BM_STATUS_OK);
        assert(state_of(&ca).ax == 0x1111U && state_of(&cb).ax == 0x2222U);
    }
    ca.ops.destroy(ca.context); cb.ops.destroy(cb.context);
    free(a); free(b);
}

int main(void)
{
    test_registers();
    test_effective_addresses();
    test_overrides_moffs_and_segments();
    test_words_and_failures();
    test_rejections_and_instances();
    return 0;
}
