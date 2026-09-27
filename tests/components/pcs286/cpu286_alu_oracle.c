/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Independent mathematical ALU oracle: authored instructions, no external
 * vectors. Signed-range and nibble arithmetic intentionally avoid copying
 * the interpreter's flag implementation. This does not certify timing.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

enum {
    CF = 0x0001U, PF = 0x0004U, AF = 0x0010U,
    ZF = 0x0040U, SF = 0x0080U, OF = 0x0800U,
    ARITHMETIC_FLAGS = CF | PF | AF | ZF | SF | OF
};

typedef struct instruction_source {
    uint8_t opcode;
    uint8_t modrm;
    unsigned fetches;
} instruction_source_t;

typedef struct expected_result {
    uint16_t result;
    uint16_t flags;
    uint16_t defined_mask;
} expected_result_t;

static bm_status_t fetch(void *context, bm_bus_transaction_t *transfer)
{
    instruction_source_t *source = context;
    assert(transfer->space == BM_ADDRESS_PROGRAM);
    assert(transfer->operation == BM_BUS_FETCH);
    assert(transfer->size == 1U && transfer->address < 2U);
    assert(transfer->wait_states == 0U);
    ++source->fetches;
    transfer->value = transfer->address == 0U ? source->opcode : source->modrm;
    transfer->wait_states = 1U;
    return BM_STATUS_OK;
}

static expected_result_t mathematical_result(unsigned operation, unsigned bits,
                                             unsigned left, unsigned right,
                                             unsigned incoming, uint16_t flags)
{
    const unsigned modulus = 1U << bits;
    const unsigned mask = modulus - 1U;
    const int32_t lower = -(int32_t) (modulus / 2U);
    const int32_t upper = (int32_t) (modulus / 2U) - 1;
    const int32_t signed_left = left > (unsigned) upper ?
        (int32_t) left - (int32_t) modulus : (int32_t) left;
    const int32_t signed_right = right > (unsigned) upper ?
        (int32_t) right - (int32_t) modulus : (int32_t) right;
    const unsigned carry = (operation == 2U || operation == 3U) ? incoming : 0U;
    const int logical = operation == 1U || operation == 4U || operation == 6U;
    int32_t signed_total = 0;
    int32_t total = 0;
    unsigned result, ones = 0U, bit;
    int auxiliary = 0, carry_out = 0;
    expected_result_t expected;

    switch (operation) {
    case 0U: /* ADD */
    case 2U: /* ADC */
        total = (int32_t) left + (int32_t) right + (int32_t) carry;
        signed_total = signed_left + signed_right + (int32_t) carry;
        carry_out = (unsigned) total >= modulus;
        auxiliary = (left % 16U) + (right % 16U) + carry >= 16U;
        break;
    case 3U: /* SBB */
    case 5U: /* SUB */
    case 7U: /* CMP */
        total = (int32_t) left - (int32_t) right - (int32_t) carry;
        signed_total = signed_left - signed_right - (int32_t) carry;
        carry_out = left < right + carry;
        auxiliary = left % 16U < (right % 16U) + carry;
        break;
    case 1U: total = (int32_t) (left | right); break;
    case 4U: total = (int32_t) (left & right); break;
    case 6U: total = (int32_t) (left ^ right); break;
    default: assert(0); break;
    }
    result = (uint32_t) total & mask;
    for (bit = 0U; bit < 8U; ++bit)
        ones += (result >> bit) & 1U;
    expected.defined_mask = (uint16_t) (logical ? 0xffffU & ~AF : 0xffffU);
    expected.flags = (uint16_t) (flags & ~ARITHMETIC_FLAGS);
    if (carry_out) expected.flags |= CF;
    if (auxiliary) expected.flags |= AF;
    if (!logical && (signed_total < lower || signed_total > upper))
        expected.flags |= OF;
    if (result == 0U) expected.flags |= ZF;
    if (result >= modulus / 2U) expected.flags |= SF;
    if (ones % 2U == 0U) expected.flags |= PF;
    expected.result = (uint16_t) (operation == 7U ? left : result);
    return expected;
}

static void check_case(bm_cpu_t *cpu, instruction_source_t *source,
                       const bm_286_arch_state_t *initial,
                       unsigned operation, unsigned bits, unsigned left,
                       unsigned right, unsigned incoming)
{
    bm_286_arch_state_t before = *initial, after;
    bm_286_boundary_t boundary;
    expected_result_t expected;
    bm_status_t status;
    uint16_t actual_result;

    source->opcode = (uint8_t) ((operation << 3) | (bits == 16U ? 1U : 0U));
    source->modrm = 0xc8U;
    source->fetches = 0U;
    /* Alternate preexisting status bits to catch failure to clear as well as
     * failure to set. IF/DF and fixed bit must survive; TF is deliberately off. */
    before.flags = (uint16_t) (0x0602U |
        ((left ^ right) & 1U ? ARITHMETIC_FLAGS & ~CF : 0U) | incoming);
    before.ax = (uint16_t) (left | (bits == 8U ? 0xa500U : 0U));
    before.cx = (uint16_t) (right | (bits == 8U ? 0x5a00U : 0U));
    before.bx = 0x1357U;
    before.dx = 0x2468U;
    expected = mathematical_result(operation, bits, left, right, incoming,
                                    before.flags);
    assert(bm_286_set_arch_state(cpu, &before) == BM_STATUS_OK);
    status = bm_286_step(cpu, &boundary);
    assert(bm_286_get_arch_state(cpu, &after) == BM_STATUS_OK);
    actual_result = bits == 8U ? (uint16_t) (after.ax & 0xffU) : after.ax;
    if (status != BM_STATUS_OK || actual_result != expected.result ||
        (after.flags & expected.defined_mask) !=
        (expected.flags & expected.defined_mask)) {
        fprintf(stderr, "ALU op=%u bits=%u a=%x b=%x carry=%u status=%d "
                "result=%x expected=%x flags=%x expected=%x mask=%x\n",
                operation, bits, left, right, incoming, (int) status,
                (unsigned) actual_result, (unsigned) expected.result,
                (unsigned) after.flags, (unsigned) expected.flags,
                (unsigned) expected.defined_mask);
        assert(0);
    }
    assert(after.ip == 2U && source->fetches == 2U);
    assert(after.cx == before.cx && after.bx == before.bx && after.dx == before.dx);
    if (bits == 8U) assert((after.ax & 0xff00U) == 0xa500U);
    assert(boundary.kind == BM_286_BOUNDARY_INSTRUCTION);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.bus_wait_cycles == 2U && boundary.cpu_cycles == 2U);
}

static void check_oracle_examples(void)
{
    expected_result_t value = mathematical_result(0U, 8U, 0x7fU, 1U, 0U, 2U);
    assert(value.result == 0x80U && value.flags == (2U | OF | SF | AF));
    value = mathematical_result(5U, 8U, 0U, 1U, 0U, 2U);
    assert(value.result == 0xffU && value.flags == (2U | CF | PF | AF | SF));
    value = mathematical_result(2U, 8U, 0xffU, 0U, 1U, 2U);
    assert(value.result == 0U && value.flags == (2U | CF | PF | AF | ZF));
    value = mathematical_result(3U, 8U, 0x80U, 0x7fU, 1U, 2U);
    assert(value.result == 0U && value.flags == (2U | OF | PF | AF | ZF));
    value = mathematical_result(0U, 16U, 0x7fffU, 1U, 0U, 2U);
    assert(value.result == 0x8000U && value.flags == (2U | OF | PF | AF | SF));
}

static void check_unary(bm_cpu_t *cpu, instruction_source_t *source,
                        const bm_286_arch_state_t *initial, unsigned operation,
                        unsigned bits, unsigned value, unsigned incoming)
{
    bm_286_arch_state_t before = *initial, after;
    bm_286_boundary_t boundary;
    expected_result_t expected;
    unsigned mask = (1U << bits) - 1U;
    before.flags = (uint16_t) (0x0602U | incoming |
        (value & 1U ? ARITHMETIC_FLAGS & ~CF : 0U));
    before.ax = (uint16_t) (value | (bits == 8U ? 0xa500U : 0U));
    source->fetches = 0U;
    if (operation < 2U) { /* INC / DEC */
        source->opcode = (uint8_t) (bits == 8U ? 0xfeU : 0xffU);
        source->modrm = (uint8_t) (0xc0U | (operation << 3));
        expected = mathematical_result(operation == 0U ? 0U : 5U,
            bits, value, 1U, 0U, before.flags);
        expected.flags = (uint16_t) ((expected.flags & ~CF) | incoming);
    } else if (operation == 2U) { /* NEG */
        source->opcode = (uint8_t) (bits == 8U ? 0xf6U : 0xf7U);
        source->modrm = 0xd8U;
        expected = mathematical_result(5U, bits, 0U, value, 0U, before.flags);
    } else { /* NOT */
        source->opcode = (uint8_t) (bits == 8U ? 0xf6U : 0xf7U);
        source->modrm = 0xd0U;
        expected.result = (uint16_t) (value ^ mask);
        expected.flags = before.flags;
    }
    assert(bm_286_set_arch_state(cpu, &before) == BM_STATUS_OK);
    assert(bm_286_step(cpu, &boundary) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(cpu, &after) == BM_STATUS_OK);
    if ((after.ax & mask) != expected.result || after.flags != expected.flags) {
        fprintf(stderr, "Unary op=%u bits=%u value=%x carry=%u result=%x "
                "expected=%x flags=%x expected=%x\n", operation, bits,
                value, incoming, (unsigned) (after.ax & mask),
                (unsigned) expected.result, (unsigned) after.flags,
                (unsigned) expected.flags);
        assert(0);
    }
    assert(after.ip == 2U && source->fetches == 2U);
    assert(after.cx == before.cx && after.bx == before.bx && after.dx == before.dx);
    if (bits == 8U) assert((after.ax & 0xff00U) == 0xa500U);
    assert(boundary.kind == BM_286_BOUNDARY_INSTRUCTION);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.bus_wait_cycles == 2U && boundary.cpu_cycles == 2U);
}

int main(void)
{
    static const unsigned word_edges[] = {
        0U, 1U, 2U, 15U, 16U, 127U, 128U, 255U, 256U,
        0x7ffeU, 0x7fffU, 0x8000U, 0x8001U, 0xfff0U, 0xfffeU, 0xffffU
    };
    bm_host_services_t host = bm_null_host_services();
    instruction_source_t source = {0};
    bm_286_config_t config = {0};
    bm_286_arch_state_t initial;
    bm_cpu_t cpu;
    unsigned operation, left, right, carry;
    size_t i, j;
    check_oracle_examples();
    config.size = sizeof(config);
    config.version = BM_286_CONTRACT_VERSION;
    config.access = fetch;
    config.access_context = &source;
    assert(bm_286_create(&host, &config, &cpu) == BM_STATUS_OK);
    assert(bm_286_get_arch_state(&cpu, &initial) == BM_STATUS_OK);
    initial.cs.selector = 0U;
    initial.cs.base = 0U;
    initial.ip = 0U;
    for (operation = 0U; operation < 8U; ++operation) {
        for (left = 0U; left < 256U; ++left)
            for (right = 0U; right < 256U; ++right)
                for (carry = 0U; carry < 2U; ++carry)
                    check_case(&cpu, &source, &initial, operation, 8U,
                               left, right, carry);
        for (i = 0U; i < sizeof(word_edges) / sizeof(word_edges[0]); ++i)
            for (j = 0U; j < sizeof(word_edges) / sizeof(word_edges[0]); ++j)
                for (carry = 0U; carry < 2U; ++carry)
                    check_case(&cpu, &source, &initial, operation, 16U,
                               word_edges[i], word_edges[j], carry);
    }
    for (operation = 0U; operation < 4U; ++operation) {
        for (left = 0U; left < 256U; ++left)
            for (carry = 0U; carry < 2U; ++carry)
                check_unary(&cpu, &source, &initial, operation, 8U, left, carry);
        for (i = 0U; i < sizeof(word_edges) / sizeof(word_edges[0]); ++i)
            for (carry = 0U; carry < 2U; ++carry)
                check_unary(&cpu, &source, &initial, operation, 16U,
                            word_edges[i], carry);
    }
    cpu.ops.destroy(cpu.context);
    return 0;
}
