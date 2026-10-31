/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 8085 flag contracts. Documented flags follow Intel MCS-80/85 §5.6; the
 * undocumented V and K flags follow K. Shirriff's silicon analysis of the
 * 8085 (righto.com, February 2013), including its note 5 comparison table. */
#include "cpu_i8085_test_harness.h"

#include <assert.h>

#define CY BM_I8085_FLAG_CY
#define V BM_I8085_FLAG_V
#define P BM_I8085_FLAG_P
#define AC BM_I8085_FLAG_AC
#define K BM_I8085_FLAG_K
#define Z BM_I8085_FLAG_Z
#define S BM_I8085_FLAG_S

/* Execute one instruction at 0000h from the given state. */
static bm_i8085_arch_state_t
execute(const uint8_t *program, size_t size, bm_i8085_arch_state_t *setup,
        i8085_test_machine_t *machine_out)
{
    i8085_test_machine_t local;
    i8085_test_machine_t *machine = machine_out != NULL ? machine_out : &local;
    bm_i8085_arch_state_t state;

    i8085_test_create(machine, NULL);
    i8085_test_load(machine, 0x0000U, program, size);
    state = i8085_test_state(machine);
    state.a = setup->a;
    state.f = setup->f;
    state.b = setup->b;
    state.c = setup->c;
    state.d = setup->d;
    state.e = setup->e;
    state.h = setup->h;
    state.l = setup->l;
    state.sp = setup->sp;
    i8085_test_set_state(machine, &state);
    i8085_test_step(machine);
    state = i8085_test_state(machine);
    if (machine_out == NULL)
        i8085_test_destroy(machine);
    return state;
}

static bm_i8085_arch_state_t
registers(uint8_t a, uint8_t b, uint8_t f)
{
    bm_i8085_arch_state_t state = { 0 };
    state.a = a;
    state.b = b;
    state.f = f;
    state.sp = 0x8000U;
    return state;
}

/* [KS1302] note 5: A - B for the eight sign/carry combinations. CY is the
 * borrow; K is set exactly when A < B as signed numbers. */
static void
test_signed_compare_table(void)
{
    static const struct {
        uint8_t a, b, borrow, s, v, k;
    } rows[] = {
        { 0x50, 0xf0, 1, 0, 0, 0 },
        { 0x50, 0xb0, 1, 1, 1, 0 },
        { 0x50, 0x70, 1, 1, 0, 1 },
        { 0x50, 0x30, 0, 0, 0, 0 },
        { 0xd0, 0xf0, 1, 1, 0, 1 },
        { 0xd0, 0xb0, 0, 0, 0, 0 },
        { 0xd0, 0x70, 0, 0, 1, 1 },
        { 0xd0, 0x30, 0, 1, 0, 1 },
    };
    static const uint8_t cmp_b[] = { 0xb8 };
    static const uint8_t sub_b[] = { 0x90 };
    size_t index;

    for (index = 0U; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        bm_i8085_arch_state_t setup = registers(rows[index].a, rows[index].b, 0U);
        bm_i8085_arch_state_t compared = execute(cmp_b, 1U, &setup, NULL);
        bm_i8085_arch_state_t subtracted = execute(sub_b, 1U, &setup, NULL);

        assert(compared.a == rows[index].a);
        assert(subtracted.a == (uint8_t) (rows[index].a - rows[index].b));
        assert(!!(compared.f & CY) == rows[index].borrow);
        assert(!!(compared.f & S) == rows[index].s);
        assert(!!(compared.f & V) == rows[index].v);
        assert(!!(compared.f & K) == rows[index].k);
        assert((compared.f & ~0x08U) == compared.f);
        assert(subtracted.f == compared.f);
    }
}

static void
test_add_and_logic_flags(void)
{
    static const uint8_t add_b[] = { 0x80 };
    static const uint8_t ana_b[] = { 0xa0 };
    static const uint8_t xra_b[] = { 0xa8 };
    static const uint8_t ora_b[] = { 0xb0 };
    bm_i8085_arch_state_t setup = registers(0x7fU, 0x01U, CY);
    bm_i8085_arch_state_t state = execute(add_b, 1U, &setup, NULL);

    /* 7Fh + 01h: signed overflow into a negative, odd-parity result. */
    assert(state.a == 0x80U);
    assert(state.f == (S | AC | V));

    /* ANA sets AC on the 8085 and clears CY ([MCS85] p. 5-9). */
    setup = registers(0xf0U, 0x8fU, CY | V);
    state = execute(ana_b, 1U, &setup, NULL);
    assert(state.a == 0x80U);
    assert(state.f == (S | AC | K));
    setup = registers(0x0fU, 0x0fU, CY | AC | V | K);
    state = execute(xra_b, 1U, &setup, NULL);
    assert(state.a == 0x00U && state.f == (Z | P));
    setup = registers(0x80U, 0x01U, CY | AC);
    state = execute(ora_b, 1U, &setup, NULL);
    assert(state.a == 0x81U && state.f == (S | P | K));
}

static void
test_increment_decrement_flags(void)
{
    static const uint8_t inr_b[] = { 0x04 };
    static const uint8_t dcr_b[] = { 0x05 };
    bm_i8085_arch_state_t setup = registers(0U, 0x7fU, CY);
    bm_i8085_arch_state_t state = execute(inr_b, 1U, &setup, NULL);

    /* INR/DCR keep CY; V only for 7Fh->80h and 80h->7Fh ([KS1302] note 4). */
    assert(state.b == 0x80U && state.f == (S | AC | V | CY));
    setup = registers(0U, 0xffU, 0U);
    state = execute(inr_b, 1U, &setup, NULL);
    assert(state.b == 0x00U && state.f == (Z | P | AC));
    setup = registers(0U, 0x80U, 0U);
    state = execute(dcr_b, 1U, &setup, NULL);
    assert(state.b == 0x7fU && state.f == (V | K));
    setup = registers(0U, 0x00U, CY);
    state = execute(dcr_b, 1U, &setup, NULL);
    assert(state.b == 0xffU && state.f == (S | P | K | CY));
}

static void
test_decimal_adjust(void)
{
    static const uint8_t add_b_daa[] = { 0x80, 0x27 };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t setup = registers(0x38U, 0x45U, 0U);
    bm_i8085_arch_state_t state = execute(add_b_daa, 2U, &setup, &machine);

    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert(state.a == 0x83U && (state.f & CY) == 0U);
    i8085_test_destroy(&machine);

    setup = registers(0x99U, 0x01U, 0U);
    (void) execute(add_b_daa, 2U, &setup, &machine);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert(state.a == 0x00U && (state.f & (Z | CY)) == (Z | CY));
    i8085_test_destroy(&machine);
}

/* INX/DCX set K from the incrementer carry and leave every other flag. */
static void
test_register_pair_k_flag(void)
{
    static const uint8_t inx_b[] = { 0x03 };
    static const uint8_t dcx_d[] = { 0x1b };
    bm_i8085_arch_state_t setup = registers(0U, 0xffU, S | V | CY);
    bm_i8085_arch_state_t state;

    setup.c = 0xffU;
    state = execute(inx_b, 1U, &setup, NULL);
    assert(state.b == 0x00U && state.c == 0x00U);
    assert(state.f == (S | V | CY | K));
    setup = registers(0U, 0x12U, K | Z);
    setup.c = 0x34U;
    state = execute(inx_b, 1U, &setup, NULL);
    assert(state.c == 0x35U && state.f == Z);
    setup = registers(0U, 0U, 0U);
    state = execute(dcx_d, 1U, &setup, NULL);
    assert(state.d == 0xffU && state.e == 0xffU && state.f == K);
}

/* DAD changes CY and the 16-bit signed overflow V; Z and S are unchanged
 * and K is left as it was because [KS1302] does not establish it. */
static void
test_dad_flags(void)
{
    static const uint8_t dad_d[] = { 0x19 };
    bm_i8085_arch_state_t setup = registers(0U, 0U, Z | K);
    bm_i8085_arch_state_t state;

    setup.h = 0x70U;
    setup.d = 0x10U;
    state = execute(dad_d, 1U, &setup, NULL);
    assert(state.h == 0x80U && state.l == 0x00U);
    assert(state.f == (Z | K | V));
    setup = registers(0U, 0U, V);
    setup.h = 0xffU;
    setup.l = 0xffU;
    setup.e = 0x01U;
    state = execute(dad_d, 1U, &setup, NULL);
    assert(state.h == 0x00U && state.l == 0x00U && state.f == CY);
}

static void
test_rotate_flags(void)
{
    static const uint8_t rlc[] = { 0x07 };
    static const uint8_t rrc[] = { 0x0f };
    static const uint8_t ral[] = { 0x17 };
    bm_i8085_arch_state_t setup = registers(0x40U, 0U, K);
    bm_i8085_arch_state_t state = execute(rlc, 1U, &setup, NULL);

    /* Left rotates behave as A + A for V; K is left unchanged. */
    assert(state.a == 0x80U && state.f == (V | K));
    setup = registers(0xc0U, 0U, 0U);
    state = execute(ral, 1U, &setup, NULL);
    assert(state.a == 0x80U && state.f == CY);
    setup = registers(0x01U, 0U, V);
    state = execute(rrc, 1U, &setup, NULL);
    assert(state.a == 0x80U && state.f == CY);
}

/* PUSH PSW stores V and K; POP PSW loads them and forces bit 3 to zero. */
static void
test_psw_round_trip(void)
{
    static const uint8_t push_psw[] = { 0xf5 };
    static const uint8_t pop_psw[] = { 0xf1 };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t setup = registers(0x5aU, 0U, S | K | V | CY);
    bm_i8085_arch_state_t state;

    (void) execute(push_psw, 1U, &setup, &machine);
    assert(machine.memory[0x7fffU] == 0x5aU);
    assert(machine.memory[0x7ffeU] == (S | K | V | CY));
    i8085_test_destroy(&machine);

    setup = registers(0U, 0U, 0U);
    setup.sp = 0x7ffeU;
    i8085_test_create(&machine, NULL);
    i8085_test_load(&machine, 0x0000U, pop_psw, 1U);
    machine.memory[0x7ffeU] = 0xffU;
    machine.memory[0x7fffU] = 0xa5U;
    state = i8085_test_state(&machine);
    state.sp = 0x7ffeU;
    i8085_test_set_state(&machine, &state);
    i8085_test_step(&machine);
    state = i8085_test_state(&machine);
    assert(state.a == 0xa5U && state.f == 0xf7U && state.sp == 0x8000U);
    i8085_test_destroy(&machine);
}

/* Flag-only instructions do not touch V or K. */
static void
test_carry_instructions_leave_v_k(void)
{
    static const uint8_t stc[] = { 0x37 };
    static const uint8_t cmc[] = { 0x3f };
    static const uint8_t cma[] = { 0x2f };
    bm_i8085_arch_state_t setup = registers(0x0fU, 0U, V | K);

    assert(execute(stc, 1U, &setup, NULL).f == (V | K | CY));
    assert(execute(cmc, 1U, &setup, NULL).f == (V | K | CY));
    assert(execute(cma, 1U, &setup, NULL).f == (V | K));
}

int
main(void)
{
    test_signed_compare_table();
    test_add_and_logic_flags();
    test_increment_decrement_flags();
    test_decimal_adjust();
    test_register_pair_k_flag();
    test_dad_flags();
    test_rotate_flags();
    test_psw_round_trip();
    test_carry_instructions_leave_v_k();
    return 0;
}
