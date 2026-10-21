/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Architectural effects of the 8080-compatible instruction set on the 8085,
 * following the Intel MCS-80/85 Family User's Manual §5.6. */
#include "cpu_i8085_test_harness.h"

#include <assert.h>
#include <string.h>

static void
start(i8085_test_machine_t *machine, const uint8_t *program, size_t size)
{
    bm_i8085_arch_state_t state;

    i8085_test_create(machine, NULL);
    i8085_test_load(machine, 0x0000U, program, size);
    state = i8085_test_state(machine);
    state.sp = 0x8000U;
    i8085_test_set_state(machine, &state);
}

static void
run(i8085_test_machine_t *machine, unsigned int boundaries)
{
    while (boundaries-- != 0U)
        i8085_test_step(machine);
}

static void
test_data_transfer(void)
{
    static const uint8_t program[] = {
        0x01, 0x34, 0x12,       /* LXI B,1234h */
        0x11, 0x78, 0x56,       /* LXI D,5678h */
        0x21, 0x00, 0x90,       /* LXI H,9000h */
        0x3e, 0xa5,             /* MVI A,A5h */
        0x77,                   /* MOV M,A */
        0x36, 0x5a,             /* MVI M,5Ah */
        0x46,                   /* MOV B,M */
        0x48,                   /* MOV C,B */
        0x32, 0x10, 0x90,       /* STA 9010h */
        0x22, 0x20, 0x90,       /* SHLD 9020h */
        0x2a, 0x10, 0x90,       /* LHLD 9010h -> L=A5h, H=mem[9011h] */
        0xeb,                   /* XCHG */
        0x02,                   /* STAX B (BC = 5A5Ah) */
        0x1a,                   /* LDAX D (DE = 00A5h) */
        0x3a, 0x20, 0x90,       /* LDA 9020h */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    start(&machine, program, sizeof(program));
    machine.memory[0x9011U] = 0x00U;
    machine.memory[0x00a5U] = 0xc3U;
    run(&machine, 14U);
    state = i8085_test_state(&machine);
    assert(machine.memory[0x9000U] == 0x5aU);
    assert(state.b == 0x5aU && state.c == 0x5aU);
    assert(machine.memory[0x9010U] == 0xa5U);
    assert(machine.memory[0x9020U] == 0x00U && machine.memory[0x9021U] == 0x90U);
    assert(state.d == 0x00U && state.e == 0xa5U);
    assert(state.h == 0x56U && state.l == 0x78U);
    assert(machine.memory[0x5a5aU] == 0xa5U);
    assert(state.a == 0xc3U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x00U);
    assert(state.pc == sizeof(program));
    i8085_test_destroy(&machine);
}

static void
test_arithmetic_and_logic(void)
{
    static const uint8_t program[] = {
        0x3e, 0x3c,             /* MVI A,3Ch */
        0x06, 0xc8,             /* MVI B,C8h */
        0x80,                   /* ADD B   -> 04h, CY */
        0xce, 0x10,             /* ACI 10h -> 15h */
        0xd6, 0x20,             /* SUI 20h -> F5h, CY (borrow) */
        0xde, 0x00,             /* SBI 00h -> F4h */
        0xe6, 0x0f,             /* ANI 0Fh -> 04h */
        0xf6, 0x80,             /* ORI 80h -> 84h */
        0xee, 0xff,             /* XRI FFh -> 7Bh */
        0xfe, 0x7b,             /* CPI 7Bh -> Z, A unchanged */
        0x2f,                   /* CMA     -> 84h */
        0x37,                   /* STC */
        0x3f,                   /* CMC */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    start(&machine, program, sizeof(program));
    run(&machine, 3U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x04U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0x15U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0xf5U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0xf4U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0x04U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0x84U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0x7bU);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x7bU && (state.f & BM_I8085_FLAG_Z));
    assert((state.f & BM_I8085_FLAG_CY) == 0U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).a == 0x84U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).f & BM_I8085_FLAG_CY);
    run(&machine, 1U);
    assert((i8085_test_state(&machine).f & BM_I8085_FLAG_CY) == 0U);
    i8085_test_destroy(&machine);
}

static void
test_increments_rotates_and_dad(void)
{
    static const uint8_t program[] = {
        0x21, 0xff, 0x00,       /* LXI H,00FFh */
        0x34,                   /* INR M (mem[00FFh] 7Fh -> 80h) */
        0x23,                   /* INX H -> 0100h */
        0x2b,                   /* DCX H -> 00FFh */
        0x35,                   /* DCR M -> 7Fh */
        0x01, 0x01, 0xff,       /* LXI B,FF01h */
        0x09,                   /* DAD B -> 0000h, CY */
        0x3e, 0x81,             /* MVI A,81h */
        0x07,                   /* RLC -> 03h, CY */
        0x0f,                   /* RRC -> 81h, CY */
        0x17,                   /* RAL -> 03h, CY */
        0x1f,                   /* RAR -> 81h, CY */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    start(&machine, program, sizeof(program));
    machine.memory[0x00ffU] = 0x7fU;
    run(&machine, 2U);
    assert(machine.memory[0x00ffU] == 0x80U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.h == 0x01U && state.l == 0x00U);
    run(&machine, 2U);
    assert(machine.memory[0x00ffU] == 0x7fU);
    run(&machine, 2U);
    state = i8085_test_state(&machine);
    assert(state.h == 0x00U && state.l == 0x00U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 2U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x03U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x81U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x03U && (state.f & BM_I8085_FLAG_CY));
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.a == 0x81U && (state.f & BM_I8085_FLAG_CY));
    i8085_test_destroy(&machine);
}

static void
test_branches_calls_and_stack(void)
{
    static const uint8_t program[] = {
        0xaf,                   /* 0000 XRA A (Z=1) */
        0xc2, 0x00, 0x00,       /* 0001 JNZ 0000h (not taken) */
        0xca, 0x10, 0x00,       /* 0004 JZ 0010h (taken) */
    };
    static const uint8_t at_0010[] = {
        0xcd, 0x20, 0x00,       /* 0010 CALL 0020h */
        0xc4, 0x00, 0x00,       /* 0013 CNZ 0000h (not taken) */
        0xff,                   /* 0016 RST 7 */
    };
    static const uint8_t at_0020[] = {
        0x01, 0xcd, 0xab,       /* 0020 LXI B,ABCDh */
        0xc5,                   /* 0023 PUSH B */
        0xd1,                   /* 0024 POP D */
        0xe3,                   /* 0025 XTHL */
        0xe3,                   /* 0026 XTHL */
        0xc0,                   /* 0027 RNZ (not taken) */
        0xc9,                   /* 0028 RET */
    };
    static const uint8_t at_0038[] = {
        0x21, 0x40, 0x00,       /* 0038 LXI H,0040h */
        0xf9,                   /* 003B SPHL */
        0xe9,                   /* 003C PCHL */
    };
    i8085_test_machine_t machine;
    bm_i8085_arch_state_t state;

    start(&machine, program, sizeof(program));
    i8085_test_load(&machine, 0x0010U, at_0010, sizeof(at_0010));
    i8085_test_load(&machine, 0x0020U, at_0020, sizeof(at_0020));
    i8085_test_load(&machine, 0x0038U, at_0038, sizeof(at_0038));
    run(&machine, 2U);
    assert(i8085_test_state(&machine).pc == 0x0004U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).pc == 0x0010U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.pc == 0x0020U && state.sp == 0x7ffeU);
    assert(machine.memory[0x7fffU] == 0x00U && machine.memory[0x7ffeU] == 0x13U);
    run(&machine, 3U);
    state = i8085_test_state(&machine);
    assert(state.d == 0xabU && state.e == 0xcdU && state.sp == 0x7ffeU);
    state.h = 0x55U;
    state.l = 0x66U;
    i8085_test_set_state(&machine, &state);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.h == 0x00U && state.l == 0x13U);
    assert(machine.memory[0x7ffeU] == 0x66U && machine.memory[0x7fffU] == 0x55U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.h == 0x55U && state.l == 0x66U);
    run(&machine, 2U);
    state = i8085_test_state(&machine);
    assert(state.pc == 0x0013U && state.sp == 0x8000U);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).pc == 0x0016U);
    run(&machine, 1U);
    state = i8085_test_state(&machine);
    assert(state.pc == 0x0038U && state.sp == 0x7ffeU);
    assert(machine.memory[0x7ffeU] == 0x17U);
    run(&machine, 3U);
    state = i8085_test_state(&machine);
    assert(state.sp == 0x0040U && state.pc == 0x0040U);
    i8085_test_destroy(&machine);
}

static void
test_io_and_halt(void)
{
    static const uint8_t program[] = {
        0xdb, 0x42,             /* IN 42h */
        0xd3, 0x99,             /* OUT 99h */
        0x76,                   /* HLT */
    };
    i8085_test_machine_t machine;
    bm_tick_t consumed = 7U;

    start(&machine, program, sizeof(program));
    machine.io_input[0x42U] = 0x5eU;
    i8085_test_clear_log(&machine);
    run(&machine, 2U);
    assert(i8085_test_state(&machine).a == 0x5eU);
    assert(machine.io_output[0x99U] == 0x5eU);
    /* Fetch, port byte, I/O cycle for each instruction. */
    assert(machine.log_count == 6U);
    assert(machine.log[2].space == BM_ADDRESS_IO &&
           machine.log[2].operation == BM_BUS_READ &&
           machine.log[2].address == 0x42U);
    assert(machine.log[5].space == BM_ADDRESS_IO &&
           machine.log[5].operation == BM_BUS_WRITE &&
           machine.log[5].address == 0x99U && machine.log[5].value == 0x5eU);
    run(&machine, 1U);
    assert(i8085_test_state(&machine).halted == 1U);
    assert(i8085_test_state(&machine).pc == 0x0005U);
    assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_IDLE);
    assert(consumed == 0U);
    i8085_test_destroy(&machine);
}

/* Undocumented opcodes are rejected after the opcode fetch with no
 * architectural change. */
static void
test_undocumented_rejected(void)
{
    static const uint8_t opcodes[] = {
        0x08, 0x10, 0x18, 0x28, 0x38, 0xcb, 0xd9, 0xdd, 0xed, 0xfd
    };
    size_t index;

    for (index = 0U; index < sizeof(opcodes); ++index) {
        i8085_test_machine_t machine;
        bm_i8085_arch_state_t before;
        bm_i8085_arch_state_t after;
        bm_tick_t consumed = 9U;
        uint8_t program[4] = { 0, 0x34, 0x12, 0 };

        program[0] = opcodes[index];
        start(&machine, program, sizeof(program));
        before = i8085_test_state(&machine);
        i8085_test_clear_log(&machine);
        assert(bm_i8085_step(&machine.cpu, &consumed) == BM_STATUS_UNSUPPORTED);
        assert(consumed == 0U);
        after = i8085_test_state(&machine);
        assert(memcmp(&before, &after, sizeof(before)) == 0);
        assert(machine.log_count == 1U &&
               machine.log[0].operation == BM_BUS_FETCH);
        i8085_test_destroy(&machine);
    }
}

int
main(void)
{
    test_data_transfer();
    test_arithmetic_and_logic();
    test_increments_rotates_and_dad();
    test_branches_calls_and_stack();
    test_io_and_halt();
    test_undocumented_rejected();
    return 0;
}
