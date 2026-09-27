/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored synthetic real-mode arithmetic tests; no firmware or media.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    CODE_BASE = 0x30000,
    CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040,
    SF = 0x0080, OF = 0x0800,
    STATUS_FLAGS = CF | PF | AF | ZF | SF | OF
};

typedef struct arithmetic_bus {
    uint8_t bytes[0x40000];
    bm_bus_transaction_t trace[64];
    unsigned count;
    unsigned fail_at;
} arithmetic_bus_t;

static bm_status_t access_bus(void *context, bm_bus_transaction_t *request)
{
    arithmetic_bus_t *bus = context;
    unsigned i;
    if (bus == NULL || request == NULL || request->size == 0U ||
        request->size > 2U || bus->count == 64U ||
        request->address > sizeof(bus->bytes) - request->size)
        return BM_STATUS_UNMAPPED;
    bus->trace[bus->count++] = *request;
    if (bus->count == bus->fail_at)
        return BM_STATUS_DEVICE_ERROR;
    if (request->operation != BM_BUS_WRITE)
        request->value = 0U;
    for (i = 0U; i < request->size; ++i) {
        unsigned address = (unsigned) request->address + i;
        if (request->operation == BM_BUS_WRITE)
            bus->bytes[address] = (uint8_t) (request->value >> (i * 8U));
        else
            request->value |= (uint64_t) bus->bytes[address] << (i * 8U);
    }
    request->wait_states = 3U;
    return BM_STATUS_OK;
}

static bm_cpu_t make_cpu(arithmetic_bus_t *bus)
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

static bm_286_arch_state_t load_code(bm_cpu_t *cpu, arithmetic_bus_t *bus,
                                     const uint8_t *code, size_t length)
{
    bm_286_arch_state_t state;
    assert(length <= 16U);
    assert(cpu->ops.reset(cpu->context) == BM_STATUS_OK);
    state = state_of(cpu);
    state.cs.selector = 0x3000U;
    state.cs.base = CODE_BASE;
    state.ds.selector = 0U;
    state.ds.base = 0U;
    state.ip = 0U;
    state.flags = 0x0402U; /* DF remains untouched by all tested forms. */
    memset(bus->bytes + CODE_BASE, 0, 16U);
    memcpy(bus->bytes + CODE_BASE, code, length);
    bus->count = 0U;
    bus->fail_at = 0U;
    return state;
}

static void step_ok(bm_cpu_t *cpu, arithmetic_bus_t *bus,
                    bm_286_arch_state_t *input)
{
    bm_286_boundary_t boundary = {0};
    assert(bm_286_set_arch_state(cpu, input) == BM_STATUS_OK);
    assert(bm_286_step(cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.kind == BM_286_BOUNDARY_INSTRUCTION);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.bus_wait_cycles == (uint64_t) bus->count * 3U);
    assert(boundary.cpu_cycles == boundary.bus_wait_cycles);
    assert((state_of(cpu).flags & 0x0402U) == 0x0402U);
}

static void test_binary_families(void)
{
    static const struct {
        uint8_t base;
        uint16_t destination, source, expected, flags;
        unsigned compare;
    } cases[] = {
        {0x00U, 0x7fffU, 1U,      0x8000U, SF | OF | AF | PF, 0U},
        {0x08U, 0xf000U, 0x0f0fU, 0xff0fU, SF | PF,           0U},
        {0x10U, 0xffffU, 0U,      0x0000U, CF | ZF | AF | PF, 0U},
        {0x18U, 0U,      0U,      0xffffU, CF | SF | AF | PF, 0U},
        {0x20U, 0x1234U, 0x00ffU, 0x0034U, 0U,                0U},
        {0x28U, 0x8000U, 1U,      0x7fffU, OF | AF | PF,      0U},
        {0x30U, 0xffffU, 0xffffU, 0U,      ZF | PF,           0U},
        {0x38U, 0x8000U, 1U,      0x7fffU, OF | AF | PF,      1U}
    };
    arithmetic_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    size_t i;
    unsigned form;
    assert(bus != NULL);
    cpu = make_cpu(bus);
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (form = 0U; form < 3U; ++form) {
            uint8_t code[3] = {
                (uint8_t) (cases[i].base + (form == 0U ? 1U :
                                               form == 1U ? 3U : 5U)),
                form == 0U ? 0xd8U :
                    form == 1U ? 0xc3U : (uint8_t) cases[i].source,
                (uint8_t) (cases[i].source >> 8)
            };
            size_t length = form == 2U ? 3U : 2U;
            bm_286_arch_state_t state = load_code(&cpu, bus, code, length);
            state.ax = cases[i].destination;
            state.bx = cases[i].source;
            if (cases[i].base == 0x10U || cases[i].base == 0x18U)
                state.flags |= CF;
            step_ok(&cpu, bus, &state);
            state = state_of(&cpu);
            assert(state.ax == (cases[i].compare ? cases[i].destination :
                                cases[i].expected));
            assert(state.bx == cases[i].source);
            assert((state.flags & (STATUS_FLAGS & ~AF)) ==
                   (cases[i].flags & (STATUS_FLAGS & ~AF)));
            if (cases[i].base != 0x08U && cases[i].base != 0x20U &&
                cases[i].base != 0x30U)
                assert((state.flags & AF) == (cases[i].flags & AF));
            assert(state.ip == length);
        }
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_forms_and_sign_extension(void)
{
    arithmetic_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    assert(bus != NULL);
    cpu = make_cpu(bus);
    {
        static const uint16_t results[8] = {
            0x100eU, 0x0fffU, 0x100fU, 0x0e0fU,
            0x000fU, 0x0e10U, 0x0ff0U, 0x0f0fU
        };
        unsigned kind;
        for (kind = 0U; kind < 8U; ++kind) {
            uint8_t group81[] = {0x81U, (uint8_t) (0xc0U | (kind << 3)),
                                 0xffU, 0x00U};
            state = load_code(&cpu, bus, group81, sizeof(group81));
            state.ax = 0x0f0fU;
            if (kind == 2U || kind == 3U)
                state.flags |= CF;
            step_ok(&cpu, bus, &state);
            assert(state_of(&cpu).ax == results[kind]);
            assert(state_of(&cpu).ip == 4U);
            assert(bus->count == 4U); /* CMP also has no write. */
        }
    }
    {
        const uint8_t reg_dest[] = {0x03U, 0xc3U}; /* ADD AX,BX */
        state = load_code(&cpu, bus, reg_dest, sizeof(reg_dest));
        state.ax = 2U; state.bx = 3U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 5U);
    }
    {
        const uint8_t acc_imm[] = {0x2dU, 0x01U, 0x00U};
        state = load_code(&cpu, bus, acc_imm, sizeof(acc_imm));
        state.ax = 0U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0xffffU);
        assert((state_of(&cpu).flags & CF) != 0U);
    }
    {
        const uint8_t group81[] = {0x81U, 0xc0U, 0xffU, 0x7fU};
        state = load_code(&cpu, bus, group81, sizeof(group81));
        state.ax = 1U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x8000U);
        assert((state_of(&cpu).flags & OF) != 0U);
    }
    {
        const uint8_t group83[] = {0x83U, 0xe8U, 0xffU}; /* SUB AX,-1 */
        state = load_code(&cpu, bus, group83, sizeof(group83));
        state.ax = 0U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 1U);
        assert((state_of(&cpu).flags & CF) != 0U);
    }
    {
        const uint8_t group80[] = {0x80U, 0xd0U, 0x01U}; /* ADC AL,1 */
        state = load_code(&cpu, bus, group80, sizeof(group80));
        state.ax = 0x12feU; state.flags |= CF;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x1200U);
        assert((state_of(&cpu).flags & (CF | ZF | AF)) == (CF | ZF | AF));
    }
    {
        const uint8_t xor_reg[] = {0x31U, 0xc0U}; /* XOR AX,AX */
        uint16_t cleared_af_flags;
        state = load_code(&cpu, bus, xor_reg, sizeof(xor_reg));
        state.ax = 0x1234U;
        step_ok(&cpu, bus, &state);
        cleared_af_flags = state_of(&cpu).flags;
        state = load_code(&cpu, bus, xor_reg, sizeof(xor_reg));
        state.ax = 0x1234U;
        state.flags |= AF;
        step_ok(&cpu, bus, &state);
        /* AF is architecturally undefined: compare defined flags and test
         * only that the chosen emulator policy is deterministic. */
        assert((state_of(&cpu).flags & ~AF) == (cleared_af_flags & ~AF));
        assert(state_of(&cpu).flags == cleared_af_flags);
    }
    {
        const uint8_t acc_byte[] = {0x1cU, 0U}; /* SBB AL,0 */
        state = load_code(&cpu, bus, acc_byte, sizeof(acc_byte));
        state.ax = 0x3400U; state.flags |= CF;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x34ffU);
        assert((state_of(&cpu).flags & CF) != 0U);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_test_cmp_and_unary(void)
{
    arithmetic_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    assert(bus != NULL);
    cpu = make_cpu(bus);
    {
        const uint8_t test_rm[] = {0x85U, 0x07U}; /* TEST [BX],AX */
        state = load_code(&cpu, bus, test_rm, sizeof(test_rm));
        state.bx = 0x100U; state.ax = 0x0f0fU;
        bus->bytes[0x100U] = 0xf0U; bus->bytes[0x101U] = 0xf0U;
        step_ok(&cpu, bus, &state);
        assert(bus->count == 3U && bus->trace[2].operation == BM_BUS_READ);
        assert((state_of(&cpu).flags & (ZF | PF | CF | OF)) == (ZF | PF));
        assert(state_of(&cpu).ax == 0x0f0fU);
    }
    {
        const uint8_t cmp_rm[] = {0x39U, 0x07U}; /* CMP [BX],AX */
        state = load_code(&cpu, bus, cmp_rm, sizeof(cmp_rm));
        state.bx = 0x100U; state.ax = 1U;
        bus->bytes[0x100U] = 0U; bus->bytes[0x101U] = 0U;
        step_ok(&cpu, bus, &state);
        assert(bus->count == 3U && bus->trace[2].operation == BM_BUS_READ);
        assert((state_of(&cpu).flags & CF) != 0U);
        assert(bus->bytes[0x100U] == 0U);
    }
    {
        const uint8_t test_acc[] = {0xa9U, 0x00U, 0x80U};
        state = load_code(&cpu, bus, test_acc, sizeof(test_acc));
        state.ax = 0x8001U;
        step_ok(&cpu, bus, &state);
        assert((state_of(&cpu).flags & SF) != 0U);
        assert(state_of(&cpu).ax == 0x8001U);
    }
    {
        const uint8_t test_group[] = {0xf7U, 0xc0U, 0xffU, 0xffU};
        state = load_code(&cpu, bus, test_group, sizeof(test_group));
        state.ax = 0U;
        step_ok(&cpu, bus, &state);
        assert((state_of(&cpu).flags & ZF) != 0U);
        assert(state_of(&cpu).ax == 0U);
    }
    {
        const uint8_t test_byte[] = {0xf6U, 0xc0U, 0x80U};
        state = load_code(&cpu, bus, test_byte, sizeof(test_byte));
        state.ax = 0x1280U;
        step_ok(&cpu, bus, &state);
        assert((state_of(&cpu).flags & SF) != 0U);
        assert(state_of(&cpu).ax == 0x1280U);
    }
    {
        const uint8_t test_acc_byte[] = {0xa8U, 0x01U};
        state = load_code(&cpu, bus, test_acc_byte, sizeof(test_acc_byte));
        state.ax = 0x1200U;
        step_ok(&cpu, bus, &state);
        assert((state_of(&cpu).flags & (ZF | PF)) == (ZF | PF));
    }
    {
        const uint8_t inc[] = {0x40U};
        state = load_code(&cpu, bus, inc, sizeof(inc));
        state.ax = 0x7fffU; state.flags |= CF;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x8000U);
        assert((state_of(&cpu).flags & (CF | OF | SF | AF)) ==
               (CF | OF | SF | AF));
    }
    {
        const uint8_t dec[] = {0x48U};
        state = load_code(&cpu, bus, dec, sizeof(dec));
        state.ax = 0x8000U; state.flags |= CF;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x7fffU);
        assert((state_of(&cpu).flags & (CF | OF | AF)) ==
               (CF | OF | AF));
    }
    {
        const uint8_t not_word[] = {0xf7U, 0xd0U}; /* NOT AX */
        state = load_code(&cpu, bus, not_word, sizeof(not_word));
        state.ax = 0x1234U; state.flags |= (CF | OF | AF);
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0xedcbU);
        assert(state_of(&cpu).flags == state.flags);
    }
    {
        const uint8_t neg_word[] = {0xf7U, 0xd8U}; /* NEG AX */
        state = load_code(&cpu, bus, neg_word, sizeof(neg_word));
        state.ax = 0x8000U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x8000U);
        assert((state_of(&cpu).flags & (CF | OF)) == (CF | OF));
    }
    {
        const uint8_t neg_zero[] = {0xf6U, 0xd8U}; /* NEG AL */
        state = load_code(&cpu, bus, neg_zero, sizeof(neg_zero));
        state.ax = 0x1200U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 0x1200U);
        assert((state_of(&cpu).flags & (CF | ZF | PF)) == (ZF | PF));
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

static void test_memory_commit_and_rejections(void)
{
    arithmetic_bus_t *bus = calloc(1U, sizeof(*bus));
    bm_cpu_t cpu;
    bm_286_arch_state_t state;
    bm_286_boundary_t boundary = {0};
    assert(bus != NULL);
    cpu = make_cpu(bus);
    {
        const uint8_t add_memory[] = {0x01U, 0x07U}; /* ADD [BX],AX */
        state = load_code(&cpu, bus, add_memory, sizeof(add_memory));
        state.bx = 0x101U; state.ax = 1U;
        state.flags |= OF;
        bus->bytes[0x101U] = 0xffU; bus->bytes[0x102U] = 0U;
        bus->fail_at = 6U; /* fetch 2, read 2, write first, fail second */
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->count == 6U);
        assert(bus->bytes[0x101U] == 0U && bus->bytes[0x102U] == 0U);
        assert(state_of(&cpu).ax == 1U && state_of(&cpu).flags == state.flags);
        assert(state_of(&cpu).ip == 0U);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 6U);
    }
    {
        const uint8_t failed_immediate[] = {0x83U, 0x07U, 0x01U};
        state = load_code(&cpu, bus, failed_immediate,
                          sizeof(failed_immediate));
        state.bx = 0x100U; state.ax = 0x7777U; state.flags |= CF | OF;
        bus->bytes[0x100U] = 0x34U;
        bus->fail_at = 3U;
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->count == 3U && bus->bytes[0x100U] == 0x34U);
        assert(state_of(&cpu).ip == 0U && state_of(&cpu).ax == 0x7777U);
        assert(state_of(&cpu).flags == state.flags);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 3U);
    }
    {
        const uint8_t failed_read[] = {0xf7U, 0x1fU}; /* NEG word [BX] */
        state = load_code(&cpu, bus, failed_read, sizeof(failed_read));
        state.bx = 0x101U; state.flags |= CF | OF;
        bus->bytes[0x101U] = 0x34U; bus->bytes[0x102U] = 0x12U;
        bus->fail_at = 4U; /* fetch 2, read first, fail second */
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
        assert(bus->count == 4U);
        assert(bus->bytes[0x101U] == 0x34U && bus->bytes[0x102U] == 0x12U);
        assert(state_of(&cpu).ip == 0U && state_of(&cpu).flags == state.flags);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
        assert(bus->count == 4U);
    }
    {
        static const uint8_t unary[][2] = {
            {0xfeU, 0x07U}, /* INC byte [BX] */
            {0xfeU, 0x0fU}, /* DEC byte [BX] */
            {0xf6U, 0x1fU}  /* NEG byte [BX] */
        };
        size_t i;
        for (i = 0U; i < sizeof(unary) / sizeof(unary[0]); ++i) {
            state = load_code(&cpu, bus, unary[i], sizeof(unary[i]));
            state.bx = 0x100U; state.ax = 0x7777U;
            state.flags |= CF | OF | AF;
            bus->bytes[0x100U] = 0x10U;
            bus->fail_at = 4U; /* fetch 2, read, fail write */
            assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
            assert(bm_286_step(&cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
            assert(bus->count == 4U && bus->bytes[0x100U] == 0x10U);
            assert(state_of(&cpu).ip == 0U && state_of(&cpu).ax == 0x7777U);
            assert(state_of(&cpu).flags == state.flags);
            assert(bm_286_step(&cpu, &boundary) == BM_STATUS_INVALID_STATE);
            assert(bus->count == 4U);
        }
    }
    {
        static const uint8_t read_only[][4] = {
            {0x83U, 0x3fU, 0x01U, 0U}, /* CMP word [BX],+1 */
            {0xf6U, 0x07U, 0xffU, 0U}, /* TEST byte [BX],FF */
            {0xf7U, 0x07U, 0xffU, 0xffU} /* TEST word [BX],FFFF */
        };
        size_t i, j;
        for (i = 0U; i < sizeof(read_only) / sizeof(read_only[0]); ++i) {
            size_t length = i == 2U ? 4U : 3U;
            state = load_code(&cpu, bus, read_only[i], length);
            state.bx = 0x100U;
            bus->bytes[0x100U] = 0x34U; bus->bytes[0x101U] = 0x12U;
            step_ok(&cpu, bus, &state);
            assert(bus->count == length + 1U);
            for (j = 0U; j < bus->count; ++j)
                assert(bus->trace[j].operation != BM_BUS_WRITE);
            assert(bus->bytes[0x100U] == 0x34U &&
                   bus->bytes[0x101U] == 0x12U);
        }
    }
    {
        const uint8_t inc_memory[] = {0xfeU, 0x07U}; /* INC byte [BX] */
        state = load_code(&cpu, bus, inc_memory, sizeof(inc_memory));
        state.bx = 0x100U; state.flags |= CF;
        bus->bytes[0x100U] = 0xffU;
        step_ok(&cpu, bus, &state);
        assert(bus->bytes[0x100U] == 0U);
        assert((state_of(&cpu).flags & (CF | ZF)) == (CF | ZF));
    }
    {
        const uint8_t dec_memory[] = {0xffU, 0x0fU}; /* DEC word [BX] */
        state = load_code(&cpu, bus, dec_memory, sizeof(dec_memory));
        state.bx = 0x100U;
        bus->bytes[0x100U] = 0U; bus->bytes[0x101U] = 0U;
        step_ok(&cpu, bus, &state);
        assert(bus->bytes[0x100U] == 0xffU && bus->bytes[0x101U] == 0xffU);
    }
    {
        const uint8_t inc_word[] = {0xffU, 0x07U}; /* INC word [BX] */
        state = load_code(&cpu, bus, inc_word, sizeof(inc_word));
        state.bx = 0x100U; state.flags |= CF;
        bus->bytes[0x100U] = 0xffU; bus->bytes[0x101U] = 0xffU;
        step_ok(&cpu, bus, &state);
        assert(bus->bytes[0x100U] == 0U && bus->bytes[0x101U] == 0U);
        assert((state_of(&cpu).flags & (CF | ZF)) == (CF | ZF));
    }
    {
        const uint8_t dec_byte[] = {0xfeU, 0x0fU}; /* DEC byte [BX] */
        state = load_code(&cpu, bus, dec_byte, sizeof(dec_byte));
        state.bx = 0x100U;
        bus->bytes[0x100U] = 1U;
        step_ok(&cpu, bus, &state);
        assert(bus->bytes[0x100U] == 0U);
        assert((state_of(&cpu).flags & ZF) != 0U);
    }
    {
        const uint8_t not_memory[] = {0xf6U, 0x17U}; /* NOT byte [BX] */
        state = load_code(&cpu, bus, not_memory, sizeof(not_memory));
        state.bx = 0x100U; state.flags |= (CF | AF | OF);
        bus->bytes[0x100U] = 0x55U;
        step_ok(&cpu, bus, &state);
        assert(bus->bytes[0x100U] == 0xaaU);
        assert(state_of(&cpu).flags == state.flags);
    }
    {
        const uint8_t byte_alias[] = {0x82U, 0xc0U, 1U};
        state = load_code(&cpu, bus, byte_alias, sizeof(byte_alias));
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(bus->count == 1U && state_of(&cpu).ip == 0U);
    }
    {
        const uint8_t invalid_group[] = {0xf7U, 0xc8U}; /* /1 undefined */
        state = load_code(&cpu, bus, invalid_group, sizeof(invalid_group));
        assert(bm_286_set_arch_state(&cpu, &state) == BM_STATUS_OK);
        assert(bm_286_step(&cpu, &boundary) == BM_STATUS_UNSUPPORTED);
        assert(bus->count == 2U && state_of(&cpu).ip == 0U);
    }
    {
        const uint8_t multiply_group[] = {0xf7U, 0xe0U}; /* MUL AX now implemented. */
        state = load_code(&cpu, bus, multiply_group, sizeof(multiply_group));
        state.ax = 0xffffU;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 1U && state_of(&cpu).dx == 0xfffeU);
        assert(state_of(&cpu).flags == (state.flags | CF | OF));
        assert(bus->count == 2U && state_of(&cpu).ip == 2U);
    }
    {
        const uint8_t divide_group[] = {0xf7U, 0xf0U}; /* DIV AX now implemented. */
        state = load_code(&cpu, bus, divide_group, sizeof(divide_group));
        state.ax = 7U; state.dx = 0U;
        step_ok(&cpu, bus, &state);
        assert(state_of(&cpu).ax == 1U && state_of(&cpu).dx == 0U);
        assert(state_of(&cpu).flags == state.flags);
        assert(bus->count == 2U && state_of(&cpu).ip == 2U);
    }
    cpu.ops.destroy(cpu.context);
    free(bus);
}

int main(void)
{
    test_binary_families();
    test_forms_and_sign_extension();
    test_test_cmp_and_unary();
    test_memory_commit_and_rejections();
    return 0;
}
