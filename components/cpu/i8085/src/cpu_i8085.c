/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Intel 8085A / OKI MSM80C85A CPU component.
 *
 * The 8080-compatible instruction semantics are adapted from the NEC V30
 * 8080 emulation mode in components/cpu/808x/src/cpu_808x.c; no code is
 * shared with that component. Sources are listed in cpu_i8085.h.
 */
#include <blumach/components/cpu_i8085.h>

#include <string.h>

#define REG_M 6U
#define REG_A 7U

typedef struct bm_i8085_state {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_i8085_model_t model;
    uint32_t frequency_hz;
    bm_i8085_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
    bm_i8085_sod_fn sod_output;
    void *sod_context;
    bm_i8085_timing_fn timing;
    void *timing_context;
    /* Non-NULL when the host accepts the extended instruction set. */
    const bm_host_tolerance_services_t *tolerance;
    /* Extended opcodes already reported, one bit per opcode index. */
    uint16_t extended_reported;

    /* Register file indexed by the instruction encoding: B C D E H L - A. */
    uint8_t registers[8];
    uint8_t f;
    uint16_t sp;
    uint16_t pc;
    uint8_t interrupt_enable;
    uint8_t interrupt_enable_delay;
    uint8_t halted;
    uint8_t interrupt_mask;
    uint8_t rst75_pending;
    uint8_t trap_pending;
    uint8_t trap_saved_interrupt_enable;
    uint8_t trap_saved_valid;
    uint8_t sod;

    /* External pin levels; not part of the architectural snapshot. */
    uint8_t intr_line;
    uint8_t trap_line;
    uint8_t rst55_line;
    uint8_t rst65_line;
    uint8_t rst75_line;
    uint8_t reset_line;
    uint8_t wait_line;
    uint8_t sid_line;

    /* Accounting for the boundary in progress. */
    uint16_t boundary_pc;
    uint8_t boundary_opcode;
    uint8_t boundary_taken;
    uint64_t boundary_transactions;
    uint64_t boundary_wait_states;
    bm_i8085_timing_observation_t last;
} bm_i8085_state_t;

static const char *
model_name(bm_i8085_model_t model)
{
    switch (model) {
        case BM_I8085_INTEL_8085A: return "Intel 8085A";
        case BM_I8085_OKI_MSM80C85A: return "OKI MSM80C85A";
        default: return NULL;
    }
}

/* ---------------------------------------------------------------------
 * Documented timing.
 *
 * T-states and machine cycles for the Intel 8085A from [MCS85] §5.6,
 * pp. 5-4..5-18, where each instruction lists "Cycles" and "States" with
 * the 8085 value first. [OKI86] Table 8, pp. 30-32, lists identical clock
 * counts for the MSM80C85A. Conditional instructions have a not-taken and
 * a taken value (Note 2 of both tables). HLT is listed as "1+" cycles and
 * 5 states (p. 5-17); Figure 2-20 (p. 2-16) shows M1 and the first T-state
 * of M2 before THALT, reported here as two machine cycles.
 * Returns zero when no cited value exists.
 * --------------------------------------------------------------------- */
static int
documented_timing(uint8_t op, int taken, uint8_t *t_states, uint8_t *cycles)
{
    uint8_t t = 0U;
    uint8_t m = 0U;

    if (bm_i8085_classify_opcode(op) == BM_I8085_OPCODE_UNDOCUMENTED)
        return 0;
    if (op == 0x76U) {                                  /* HLT, p. 5-17 */
        t = 5U; m = 2U;
    } else if ((op & 0xc0U) == 0x40U) {                 /* MOV, p. 5-4 */
        int memory = ((op & 7U) == REG_M) || (((op >> 3U) & 7U) == REG_M);
        t = memory ? 7U : 4U; m = memory ? 2U : 1U;
    } else if ((op & 0xc0U) == 0x80U) {                 /* ALU r/M, pp. 5-6..5-11 */
        t = (op & 7U) == REG_M ? 7U : 4U; m = (op & 7U) == REG_M ? 2U : 1U;
    } else if (((op & 0xc7U) == 0x04U) || ((op & 0xc7U) == 0x05U)) {
        int memory = ((op >> 3U) & 7U) == REG_M;        /* INR/DCR, p. 5-8 */
        t = memory ? 10U : 4U; m = memory ? 3U : 1U;
    } else if ((op & 0xc7U) == 0x06U) {                 /* MVI, p. 5-4 */
        int memory = ((op >> 3U) & 7U) == REG_M;
        t = memory ? 10U : 7U; m = memory ? 3U : 2U;
    } else if ((op & 0xcfU) == 0x01U) {                 /* LXI, p. 5-5 */
        t = 10U; m = 3U;
    } else if (((op & 0xcfU) == 0x03U) || ((op & 0xcfU) == 0x0bU)) {
        t = 6U; m = 1U;                                 /* INX/DCX, p. 5-9 */
    } else if ((op & 0xcfU) == 0x09U) {                 /* DAD, p. 5-9 */
        t = 10U; m = 3U;
    } else if ((op & 0xc7U) == 0xc0U) {                 /* Rcc, p. 5-14 */
        t = taken ? 12U : 6U; m = taken ? 3U : 1U;
    } else if ((op & 0xc7U) == 0xc2U) {                 /* Jcc, p. 5-13 */
        t = taken ? 10U : 7U; m = taken ? 3U : 2U;
    } else if ((op & 0xc7U) == 0xc4U) {                 /* Ccc, p. 5-14 */
        t = taken ? 18U : 9U; m = taken ? 5U : 2U;
    } else if ((op & 0xc7U) == 0xc6U) {                 /* ALU imm, pp. 5-6..5-11 */
        t = 7U; m = 2U;
    } else if ((op & 0xc7U) == 0xc7U) {                 /* RST, p. 5-14 */
        t = 12U; m = 3U;
    } else if ((op & 0xcfU) == 0xc1U) {                 /* POP, pp. 5-15..5-16 */
        t = 10U; m = 3U;
    } else if ((op & 0xcfU) == 0xc5U) {                 /* PUSH, p. 5-15 */
        t = 12U; m = 3U;
    } else {
        switch (op) {
            case 0x02U: case 0x12U:                     /* STAX, p. 5-6 */
            case 0x0aU: case 0x1aU:                     /* LDAX, p. 5-5 */
                t = 7U; m = 2U; break;
            case 0x22U: case 0x2aU:                     /* SHLD/LHLD, p. 5-5 */
                t = 16U; m = 5U; break;
            case 0x32U: case 0x3aU:                     /* STA/LDA, p. 5-5 */
                t = 13U; m = 4U; break;
            case 0x00U:                                 /* NOP, p. 5-17 */
            case 0x07U: case 0x0fU: case 0x17U: case 0x1fU: /* p. 5-11..5-12 */
            case 0x27U:                                 /* DAA, p. 5-9 */
            case 0x2fU: case 0x37U: case 0x3fU:         /* CMA/STC/CMC, p. 5-12 */
            case 0x20U: case 0x30U:                     /* RIM/SIM, pp. 5-17..5-18 */
            case 0xebU:                                 /* XCHG, p. 5-6 */
            case 0xf3U: case 0xfbU:                     /* DI/EI, p. 5-17 */
                t = 4U; m = 1U; break;
            case 0xc3U: case 0xc9U:                     /* JMP p. 5-13, RET p. 5-14 */
                t = 10U; m = 3U; break;
            case 0xcdU:                                 /* CALL, p. 5-13 */
                t = 18U; m = 5U; break;
            case 0xd3U: case 0xdbU:                     /* OUT/IN, p. 5-16 */
                t = 10U; m = 3U; break;
            case 0xe3U:                                 /* XTHL, p. 5-16 */
                t = 16U; m = 5U; break;
            case 0xe9U: case 0xf9U:                     /* PCHL p. 5-15, SPHL p. 5-16 */
                t = 6U; m = 1U; break;
            default:
                return 0;
        }
    }
    *t_states = t;
    *cycles = m;
    return 1;
}

/* Extended (undocumented) instructions, executed only for a tolerant host.
 * Bytes, machine cycles and states from [DS79] p. 145, which agree with
 * [TUN97] Table 2-10 p. 2-16. Conditional forms report the realised path
 * as their documented counterparts do. */
static int
extended_timing(uint8_t op, int taken, uint8_t *t_states, uint8_t *cycles)
{
    uint8_t t;
    uint8_t m;

    switch (op) {
        case 0x10U:                                     /* ARHL */
            t = 7U; m = 2U; break;
        case 0x08U:                                     /* DSUB */
        case 0x18U:                                     /* RDEL */
        case 0x28U: case 0x38U:                         /* LDHI, LDSI */
        case 0xd9U: case 0xedU:                         /* SHLX, LHLX */
            t = 10U; m = 3U; break;
        case 0xcbU:                                     /* RSTV */
            t = taken ? 12U : 6U; m = taken ? 3U : 1U; break;
        case 0xddU: case 0xfdU:                         /* JNK, JK */
            t = taken ? 10U : 7U; m = taken ? 3U : 2U; break;
        default:
            return 0;
    }
    *t_states = t;
    *cycles = m;
    return 1;
}

/* ---------------------------------------------------------------------
 * Bus access. Every transfer is one byte; endpoint waits count CLK periods.
 * --------------------------------------------------------------------- */
static bm_status_t
bus_transfer(bm_i8085_state_t *state, bm_address_space_t space,
             bm_bus_operation_t operation, uint16_t address, uint8_t *value)
{
    bm_bus_transaction_t txn;
    bm_status_t status;

    memset(&txn, 0, sizeof(txn));
    txn.space = space;
    txn.operation = operation;
    txn.address = address;
    txn.value = operation == BM_BUS_WRITE ? *value : 0U;
    txn.size = 1U;
    txn.alignment = 1U;
    txn.endianness = BM_ENDIAN_LITTLE;
    status = bm_bus_transact(state->bus, &txn);
    if (status != BM_STATUS_OK)
        return status;
    ++state->boundary_transactions;
    state->boundary_wait_states += txn.wait_states;
    if (operation != BM_BUS_WRITE)
        *value = (uint8_t) txn.value;
    return BM_STATUS_OK;
}

static bm_status_t
memory_read(bm_i8085_state_t *state, uint16_t address, uint8_t *value)
{
    return bus_transfer(state, BM_ADDRESS_MEMORY, BM_BUS_READ, address, value);
}

static bm_status_t
memory_write(bm_i8085_state_t *state, uint16_t address, uint8_t value)
{
    return bus_transfer(state, BM_ADDRESS_MEMORY, BM_BUS_WRITE, address,
                        &value);
}

/* Operand bytes are ordinary memory-read machine cycles. */
static bm_status_t
fetch_byte(bm_i8085_state_t *state, uint8_t *value)
{
    bm_status_t status = memory_read(state, state->pc, value);
    if (status == BM_STATUS_OK)
        state->pc = (uint16_t) (state->pc + 1U);
    return status;
}

static bm_status_t
fetch_word(bm_i8085_state_t *state, uint16_t *value)
{
    uint8_t low = 0U;
    uint8_t high = 0U;
    bm_status_t status = fetch_byte(state, &low);
    if (status == BM_STATUS_OK)
        status = fetch_byte(state, &high);
    if (status == BM_STATUS_OK)
        *value = (uint16_t) (((unsigned int) high << 8U) | low);
    return status;
}

/* ---------------------------------------------------------------------
 * Registers and flags.
 * --------------------------------------------------------------------- */
static uint16_t
get_pair(const bm_i8085_state_t *state, unsigned int pair)
{
    if (pair == 3U)
        return state->sp;
    return (uint16_t) (((unsigned int) state->registers[pair * 2U] << 8U) |
                       state->registers[pair * 2U + 1U]);
}

static void
set_pair(bm_i8085_state_t *state, unsigned int pair, uint16_t value)
{
    if (pair == 3U) {
        state->sp = value;
        return;
    }
    state->registers[pair * 2U] = (uint8_t) (value >> 8U);
    state->registers[pair * 2U + 1U] = (uint8_t) value;
}

static uint16_t
get_hl(const bm_i8085_state_t *state)
{
    return get_pair(state, 2U);
}

static bm_status_t
read_operand(bm_i8085_state_t *state, unsigned int code, uint8_t *value)
{
    if (code == REG_M)
        return memory_read(state, get_hl(state), value);
    *value = state->registers[code];
    return BM_STATUS_OK;
}

static bm_status_t
write_operand(bm_i8085_state_t *state, unsigned int code, uint8_t value)
{
    if (code == REG_M)
        return memory_write(state, get_hl(state), value);
    state->registers[code] = value;
    return BM_STATUS_OK;
}

static int
even_parity(uint8_t value)
{
    value = (uint8_t) (value ^ (value >> 4U));
    value = (uint8_t) (value ^ (value >> 2U));
    value = (uint8_t) (value ^ (value >> 1U));
    return (value & 1U) == 0U;
}

static void
set_flag(bm_i8085_state_t *state, uint8_t flag, int condition)
{
    if (condition)
        state->f = (uint8_t) (state->f | flag);
    else
        state->f = (uint8_t) (state->f & (uint8_t) ~flag);
}

static void
set_szp(bm_i8085_state_t *state, uint8_t value)
{
    set_flag(state, BM_I8085_FLAG_S, (value & 0x80U) != 0U);
    set_flag(state, BM_I8085_FLAG_Z, value == 0U);
    set_flag(state, BM_I8085_FLAG_P, even_parity(value));
}

/* The ALU adds both operands and a carry input; subtraction adds the
 * complement with an inverted borrow ([KS1302]). V is the carry into bit 7
 * XOR the carry out of bit 7 and K is V XOR S for these operations.
 * Returns the result and the raw carry out of bit 7. */
static uint8_t
alu_sum(bm_i8085_state_t *state, uint8_t left, uint8_t right,
        unsigned int carry_in, unsigned int *carry_out)
{
    unsigned int sum = (unsigned int) left + right + carry_in;
    unsigned int carry6 = (((left & 0x7fU) + (right & 0x7fU) + carry_in) >> 7U) & 1U;
    unsigned int carry7 = (sum >> 8U) & 1U;
    uint8_t result = (uint8_t) sum;
    int overflow = (carry6 ^ carry7) != 0U;

    set_szp(state, result);
    set_flag(state, BM_I8085_FLAG_AC,
             ((left & 0x0fU) + (right & 0x0fU) + carry_in) > 0x0fU);
    set_flag(state, BM_I8085_FLAG_V, overflow);
    set_flag(state, BM_I8085_FLAG_K, overflow != ((result & 0x80U) != 0U));
    *carry_out = carry7;
    return result;
}

static uint8_t
alu_add(bm_i8085_state_t *state, uint8_t left, uint8_t right,
        unsigned int carry_in)
{
    unsigned int carry = 0U;
    uint8_t result = alu_sum(state, left, right, carry_in, &carry);
    set_flag(state, BM_I8085_FLAG_CY, carry != 0U);
    return result;
}

/* CY reports the borrow; AC keeps the carry produced by the two's-complement
 * addition, as on the 8080 ([KS1302] flag-circuit notes). */
static uint8_t
alu_subtract(bm_i8085_state_t *state, uint8_t left, uint8_t right,
             unsigned int borrow)
{
    unsigned int carry = 0U;
    uint8_t result = alu_sum(state, left, (uint8_t) ~right,
                             borrow ? 0U : 1U, &carry);
    set_flag(state, BM_I8085_FLAG_CY, carry == 0U);
    return result;
}

/* ANA sets AC on the 8085 ([MCS85] p. 5-9); XRA and ORA clear it. All three
 * clear CY. V is zero and therefore K equals S ([KS1302] note 4). */
static void
alu_logic(bm_i8085_state_t *state, unsigned int operation, uint8_t value)
{
    uint8_t accumulator = state->registers[REG_A];

    if (operation == 4U)
        accumulator = (uint8_t) (accumulator & value);
    else if (operation == 5U)
        accumulator = (uint8_t) (accumulator ^ value);
    else
        accumulator = (uint8_t) (accumulator | value);
    state->registers[REG_A] = accumulator;
    set_szp(state, accumulator);
    set_flag(state, BM_I8085_FLAG_CY, 0);
    set_flag(state, BM_I8085_FLAG_AC, operation == 4U);
    set_flag(state, BM_I8085_FLAG_V, 0);
    set_flag(state, BM_I8085_FLAG_K, (accumulator & 0x80U) != 0U);
}

static void
alu_execute(bm_i8085_state_t *state, unsigned int operation, uint8_t value)
{
    uint8_t accumulator = state->registers[REG_A];
    unsigned int carry = (state->f & BM_I8085_FLAG_CY) != 0U;

    switch (operation) {
        case 0U: state->registers[REG_A] = alu_add(state, accumulator, value, 0U); break;
        case 1U: state->registers[REG_A] = alu_add(state, accumulator, value, carry); break;
        case 2U: state->registers[REG_A] = alu_subtract(state, accumulator, value, 0U); break;
        case 3U: state->registers[REG_A] = alu_subtract(state, accumulator, value, carry); break;
        case 7U: (void) alu_subtract(state, accumulator, value, 0U); break;
        default: alu_logic(state, operation, value); break;
    }
}

/* DAA as described in [MCS85] p. 5-9; the correction goes through the
 * adder, which also produces AC, V and K. */
static void
decimal_adjust(bm_i8085_state_t *state)
{
    uint8_t accumulator = state->registers[REG_A];
    uint8_t correction = 0U;
    int carry = (state->f & BM_I8085_FLAG_CY) != 0U;
    unsigned int ignored = 0U;

    if (((accumulator & 0x0fU) > 9U) || ((state->f & BM_I8085_FLAG_AC) != 0U))
        correction = (uint8_t) (correction | 0x06U);
    if ((accumulator > 0x99U) || carry) {
        correction = (uint8_t) (correction | 0x60U);
        carry = 1;
    }
    state->registers[REG_A] = alu_sum(state, accumulator, correction, 0U,
                                      &ignored);
    set_flag(state, BM_I8085_FLAG_CY, carry);
}

static int
condition_true(const bm_i8085_state_t *state, unsigned int condition)
{
    switch (condition & 7U) {
        case 0U: return (state->f & BM_I8085_FLAG_Z) == 0U;
        case 1U: return (state->f & BM_I8085_FLAG_Z) != 0U;
        case 2U: return (state->f & BM_I8085_FLAG_CY) == 0U;
        case 3U: return (state->f & BM_I8085_FLAG_CY) != 0U;
        case 4U: return (state->f & BM_I8085_FLAG_P) == 0U;
        case 5U: return (state->f & BM_I8085_FLAG_P) != 0U;
        case 6U: return (state->f & BM_I8085_FLAG_S) == 0U;
        default: return (state->f & BM_I8085_FLAG_S) != 0U;
    }
}

/* PUSH writes the high byte to SP-1 first, then the low byte to SP-2
 * ([MCS85] p. 5-15; Figure 2-18 shows the same order for CALL). */
static bm_status_t
push_word(bm_i8085_state_t *state, uint16_t value)
{
    bm_status_t status;

    state->sp = (uint16_t) (state->sp - 1U);
    status = memory_write(state, state->sp, (uint8_t) (value >> 8U));
    if (status != BM_STATUS_OK)
        return status;
    state->sp = (uint16_t) (state->sp - 1U);
    return memory_write(state, state->sp, (uint8_t) value);
}

static bm_status_t
pop_word(bm_i8085_state_t *state, uint16_t *value)
{
    uint8_t low = 0U;
    uint8_t high = 0U;
    bm_status_t status = memory_read(state, state->sp, &low);

    if (status == BM_STATUS_OK)
        status = memory_read(state, (uint16_t) (state->sp + 1U), &high);
    if (status != BM_STATUS_OK)
        return status;
    state->sp = (uint16_t) (state->sp + 2U);
    *value = (uint16_t) (((unsigned int) high << 8U) | low);
    return BM_STATUS_OK;
}

static bm_status_t
call_address(bm_i8085_state_t *state, uint16_t address)
{
    bm_status_t status = push_word(state, state->pc);
    if (status == BM_STATUS_OK)
        state->pc = address;
    return status;
}

/* ---------------------------------------------------------------------
 * RIM and SIM ([MCS85] pp. 5-17..5-18).
 * --------------------------------------------------------------------- */

/* RIM loads SID, the RST 7.5 flip-flop, the RST 6.5/5.5 pins, the interrupt
 * enable and the three masks. Only the first RIM after a TRAP reports the
 * enable that existed before the TRAP. */
static void
read_interrupt_masks(bm_i8085_state_t *state)
{
    uint8_t enable = state->trap_saved_valid ?
                     state->trap_saved_interrupt_enable :
                     state->interrupt_enable;

    state->trap_saved_valid = 0U;
    state->registers[REG_A] = (uint8_t) (
        ((unsigned int) state->sid_line << 7U) |
        ((unsigned int) state->rst75_pending << 6U) |
        ((unsigned int) state->rst65_line << 5U) |
        ((unsigned int) state->rst55_line << 4U) |
        ((unsigned int) enable << 3U) |
        state->interrupt_mask);
}

/* SIM: bit 3 (MSE) loads the masks from bits 0-2, bit 4 resets the RST 7.5
 * flip-flop and bit 6 (SOE) loads bit 7 into the SOD latch. The SOD pin
 * changes during M1-T2 with CLK low of the next machine cycle ([MCS85]
 * Figure 2-25, p. 2-20): three half clocks after this boundary ends. */
static void
set_interrupt_masks(bm_i8085_state_t *state)
{
    uint8_t a = state->registers[REG_A];

    if ((a & 0x08U) != 0U)
        state->interrupt_mask = (uint8_t) (a & 0x07U);
    if ((a & 0x10U) != 0U)
        state->rst75_pending = 0U;
    if ((a & 0x40U) != 0U) {
        state->sod = (uint8_t) (a >> 7U);
        if (state->sod_output != NULL)
            state->sod_output(state->sod_context, state->sod, 3U);
    }
}

/* ---------------------------------------------------------------------
 * Instruction execution.
 * --------------------------------------------------------------------- */
static unsigned int
extended_index(uint8_t op)
{
    static const uint8_t opcodes[10] = {
        0x08U, 0x10U, 0x18U, 0x28U, 0x38U, 0xcbU, 0xd9U, 0xddU, 0xedU, 0xfdU
    };
    unsigned int index;

    for (index = 0U; index < 10U; ++index)
        if (opcodes[index] == op)
            return index;
    return 0U;
}

/* Reports each extended opcode once per CPU. Intel NMOS behaviour is
 * described by [DS79]; for the OKI MSM80C85A it is assumed (see
 * doc/architecture/intel-8085-cpu.md). */
static void
report_extended(bm_i8085_state_t *state, uint8_t op, uint16_t pc)
{
    const uint16_t bit = (uint16_t) (1U << extended_index(op));
    const int assumed = state->model == BM_I8085_OKI_MSM80C85A;
    bm_tolerance_event_t event;

    if ((state->extended_reported & bit) != 0U)
        return;
    state->extended_reported = (uint16_t) (state->extended_reported | bit);
    memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.kind = BM_TOLERANCE_UNDOCUMENTED_INSTRUCTION;
    event.flags = BM_TOLERANCE_FLAG_HAS_PC |
                  (assumed ? BM_TOLERANCE_FLAG_ASSUMED_BEHAVIOUR : 0U);
    event.device = model_name(state->model);
    event.note = assumed ?
        "executed with Intel NMOS 8085 semantics, assumed for this CMOS part" :
        "executed with Intel NMOS 8085 semantics";
    event.pc = pc;
    event.opcode = op;
    bm_tolerance_report(state->tolerance, &event);
}

/* The ten extended instructions after the opcode fetch. Operations from
 * [DS79] p. 145; names as in bm_i8085_classify_opcode(), with the [DS79]
 * jumps JX5/JNX5 and the [TUN97] JUI/JNUI as aliases of JK/JNK. Flags follow
 * [KS1302] where it covers the operation class; cases it does not cover are
 * marked here and in doc/architecture/intel-8085-cpu.md as unconfirmed. */
static bm_status_t
execute_extended(bm_i8085_state_t *state, uint8_t op)
{
    bm_status_t status = BM_STATUS_OK;

    switch (op) {
        case 0x08U: {                                   /* DSUB: HL -= BC */
            const uint16_t left = get_hl(state);
            const uint16_t right = get_pair(state, 0U);
            uint8_t low;
            uint8_t high;
            /* [DS79]: every flag is affected. Unconfirmed: S, P, AC, CY, V
             * and K come from the high-order subtraction, as V does for DAD
             * ([KS1302] note 4), and Z reports the 16-bit result. */
            low = alu_subtract(state, (uint8_t) left, (uint8_t) right, 0U);
            high = alu_subtract(state, (uint8_t) (left >> 8U),
                                (uint8_t) (right >> 8U),
                                (state->f & BM_I8085_FLAG_CY) != 0U);
            set_pair(state, 2U, (uint16_t) (((unsigned int) high << 8U) | low));
            set_flag(state, BM_I8085_FLAG_Z, (high | low) == 0U);
            return BM_STATUS_OK;
        }
        case 0x10U: {                                   /* ARHL */
            const uint16_t value = get_hl(state);
            /* Only CY is affected ([DS79]). */
            set_flag(state, BM_I8085_FLAG_CY, (value & 1U) != 0U);
            set_pair(state, 2U, (uint16_t) ((value >> 1U) | (value & 0x8000U)));
            return BM_STATUS_OK;
        }
        case 0x18U: {                                   /* RDEL */
            const uint16_t value = get_pair(state, 1U);
            const unsigned int carry = (state->f & BM_I8085_FLAG_CY) != 0U;
            const uint16_t result = (uint16_t) ((value << 1U) | carry);
            /* Only CY and V are affected ([DS79]). Unconfirmed: V is set as
             * for RAL, a rotate treated as adding the value to itself
             * ([KS1302]), that is when bit 15 changes. */
            set_flag(state, BM_I8085_FLAG_CY, (value & 0x8000U) != 0U);
            set_flag(state, BM_I8085_FLAG_V, ((value ^ result) & 0x8000U) != 0U);
            set_pair(state, 1U, result);
            return BM_STATUS_OK;
        }
        case 0x28U:                                     /* LDHI d8 */
        case 0x38U: {                                   /* LDSI d8 */
            uint8_t offset = 0U;
            /* No flags are affected ([DS79]). */
            status = fetch_byte(state, &offset);
            if (status == BM_STATUS_OK)
                set_pair(state, 1U,
                         (uint16_t) ((op == 0x28U ? get_hl(state) : state->sp) +
                                     offset));
            return status;
        }
        case 0xcbU:                                     /* RSTV */
            if ((state->f & BM_I8085_FLAG_V) == 0U)
                return BM_STATUS_OK;
            state->boundary_taken = 1U;
            return call_address(state, 0x0040U);
        case 0xd9U: {                                   /* SHLX */
            const uint16_t address = get_pair(state, 1U);
            status = memory_write(state, address, state->registers[5U]);
            if (status == BM_STATUS_OK)
                status = memory_write(state, (uint16_t) (address + 1U),
                                      state->registers[4U]);
            return status;
        }
        case 0xedU: {                                   /* LHLX */
            const uint16_t address = get_pair(state, 1U);
            uint8_t low = 0U;
            uint8_t high = 0U;
            status = memory_read(state, address, &low);
            if (status == BM_STATUS_OK)
                status = memory_read(state, (uint16_t) (address + 1U), &high);
            if (status == BM_STATUS_OK) {
                state->registers[5U] = low;
                state->registers[4U] = high;
            }
            return status;
        }
        case 0xddU:                                     /* JNK a16 */
        case 0xfdU: {                                   /* JK a16 */
            const int k = (state->f & BM_I8085_FLAG_K) != 0U;
            uint8_t low = 0U;
            uint8_t high = 0U;
            /* As Jcc, a false condition skips the third byte ([DS79]: 2 or
             * 3 cycles, 7 or 10 states). */
            status = fetch_byte(state, &low);
            if (status != BM_STATUS_OK)
                return status;
            if ((op == 0xfdU) != k) {
                state->pc = (uint16_t) (state->pc + 1U);
                return BM_STATUS_OK;
            }
            state->boundary_taken = 1U;
            status = fetch_byte(state, &high);
            if (status == BM_STATUS_OK)
                state->pc = (uint16_t) (((unsigned int) high << 8U) | low);
            return status;
        }
        default:
            return BM_STATUS_UNSUPPORTED;
    }
}

static bm_status_t
execute_instruction(bm_i8085_state_t *state)
{
    uint16_t start = state->pc;
    uint8_t op = 0U;
    bm_status_t status;

    state->boundary_pc = start;
    status = bus_transfer(state, BM_ADDRESS_MEMORY, BM_BUS_FETCH, state->pc,
                          &op);
    if (status != BM_STATUS_OK)
        return status;
    state->pc = (uint16_t) (state->pc + 1U);
    state->boundary_opcode = op;
    state->boundary_taken = 0U;

    if (bm_i8085_classify_opcode(op) == BM_I8085_OPCODE_UNDOCUMENTED) {
        if (state->tolerance == NULL) {
            /* Strict: no architectural change after the opcode fetch. */
            state->pc = start;
            return BM_STATUS_UNSUPPORTED;
        }
        report_extended(state, op, start);
        return execute_extended(state, op);
    }
    if (op == 0x76U) {                                  /* HLT */
        state->halted = 1U;
        return BM_STATUS_OK;
    }
    if ((op & 0xc0U) == 0x40U) {                        /* MOV dst,src */
        uint8_t value = 0U;
        status = read_operand(state, op & 7U, &value);
        return status == BM_STATUS_OK ?
               write_operand(state, (op >> 3U) & 7U, value) : status;
    }
    if ((op & 0xc0U) == 0x80U) {                        /* ALU r/M */
        uint8_t value = 0U;
        status = read_operand(state, op & 7U, &value);
        if (status == BM_STATUS_OK)
            alu_execute(state, (op >> 3U) & 7U, value);
        return status;
    }
    if (((op & 0xc7U) == 0x04U) || ((op & 0xc7U) == 0x05U)) { /* INR/DCR */
        unsigned int code = (op >> 3U) & 7U;
        uint8_t carry = (uint8_t) (state->f & BM_I8085_FLAG_CY);
        uint8_t value = 0U;
        status = read_operand(state, code, &value);
        if (status != BM_STATUS_OK)
            return status;
        value = (op & 1U) == 0U ? alu_add(state, value, 1U, 0U) :
                                  alu_subtract(state, value, 1U, 0U);
        state->f = (uint8_t) ((state->f & ~BM_I8085_FLAG_CY) | carry);
        return write_operand(state, code, value);
    }
    if ((op & 0xc7U) == 0x06U) {                        /* MVI */
        uint8_t value = 0U;
        status = fetch_byte(state, &value);
        return status == BM_STATUS_OK ?
               write_operand(state, (op >> 3U) & 7U, value) : status;
    }
    if ((op & 0xcfU) == 0x01U) {                        /* LXI */
        uint16_t value = 0U;
        status = fetch_word(state, &value);
        if (status == BM_STATUS_OK)
            set_pair(state, (op >> 4U) & 3U, value);
        return status;
    }
    if (((op & 0xcfU) == 0x03U) || ((op & 0xcfU) == 0x0bU)) { /* INX/DCX */
        unsigned int pair = (op >> 4U) & 3U;
        int increment = (op & 0x08U) == 0U;
        uint16_t value = (uint16_t) (increment ? get_pair(state, pair) + 1U :
                                                 get_pair(state, pair) - 1U);
        set_pair(state, pair, value);
        /* K receives the incrementer carry ([KS1302]); V is not stated and
         * remains unchanged. */
        set_flag(state, BM_I8085_FLAG_K,
                 increment ? value == 0x0000U : value == 0xffffU);
        return BM_STATUS_OK;
    }
    if ((op & 0xcfU) == 0x09U) {                        /* DAD */
        uint16_t left = get_hl(state);
        uint16_t right = get_pair(state, (op >> 4U) & 3U);
        uint32_t sum = (uint32_t) left + right;
        uint16_t result = (uint16_t) sum;
        set_pair(state, 2U, result);
        set_flag(state, BM_I8085_FLAG_CY, sum > 0xffffU);
        /* V is the 16-bit signed overflow of the high-order addition
         * ([KS1302] note 4); K is not stated and remains unchanged. */
        set_flag(state, BM_I8085_FLAG_V,
                 (((left ^ result) & (right ^ result)) & 0x8000U) != 0U);
        return BM_STATUS_OK;
    }
    if ((op & 0xc7U) == 0xc0U) {                        /* Rcc */
        uint16_t address = 0U;
        if (!condition_true(state, (op >> 3U) & 7U))
            return BM_STATUS_OK;
        state->boundary_taken = 1U;
        status = pop_word(state, &address);
        if (status == BM_STATUS_OK)
            state->pc = address;
        return status;
    }
    if (((op & 0xc7U) == 0xc2U) || ((op & 0xc7U) == 0xc4U)) { /* Jcc/Ccc */
        uint8_t low = 0U;
        uint8_t high = 0U;
        uint16_t address;

        /* The 8085 evaluates the condition while fetching the second byte
         * and skips the third when it is false ([ASM85] p. 1-25). */
        status = fetch_byte(state, &low);
        if (status != BM_STATUS_OK)
            return status;
        if (!condition_true(state, (op >> 3U) & 7U)) {
            state->pc = (uint16_t) (state->pc + 1U);
            return BM_STATUS_OK;
        }
        state->boundary_taken = 1U;
        status = fetch_byte(state, &high);
        if (status != BM_STATUS_OK)
            return status;
        address = (uint16_t) (((unsigned int) high << 8U) | low);
        if ((op & 0xc7U) == 0xc4U)
            return call_address(state, address);
        state->pc = address;
        return BM_STATUS_OK;
    }
    if ((op & 0xc7U) == 0xc6U) {                        /* ALU immediate */
        uint8_t value = 0U;
        status = fetch_byte(state, &value);
        if (status == BM_STATUS_OK)
            alu_execute(state, (op >> 3U) & 7U, value);
        return status;
    }
    if ((op & 0xc7U) == 0xc7U)                          /* RST n */
        return call_address(state, (uint16_t) (op & 0x38U));
    if ((op & 0xcfU) == 0xc1U) {                        /* POP */
        uint16_t value = 0U;
        unsigned int pair = (op >> 4U) & 3U;
        status = pop_word(state, &value);
        if (status != BM_STATUS_OK)
            return status;
        if (pair == 3U) {
            state->registers[REG_A] = (uint8_t) (value >> 8U);
            /* Bit 3 is driven to zero ([KS1302]); V and K are loaded. */
            state->f = (uint8_t) (value & 0xf7U);
        } else {
            set_pair(state, pair, value);
        }
        return BM_STATUS_OK;
    }
    if ((op & 0xcfU) == 0xc5U) {                        /* PUSH */
        unsigned int pair = (op >> 4U) & 3U;
        uint16_t value = pair == 3U ?
            (uint16_t) (((unsigned int) state->registers[REG_A] << 8U) | state->f) :
            get_pair(state, pair);
        return push_word(state, value);
    }

    switch (op) {
        case 0x00U:                                     /* NOP */
            return BM_STATUS_OK;
        case 0x02U: case 0x12U:                         /* STAX B/D */
            return memory_write(state, get_pair(state, (op >> 4U) & 1U),
                                state->registers[REG_A]);
        case 0x0aU: case 0x1aU:                         /* LDAX B/D */
            return memory_read(state, get_pair(state, (op >> 4U) & 1U),
                               &state->registers[REG_A]);
        case 0x07U: {                                   /* RLC */
            uint8_t a = state->registers[REG_A];
            state->registers[REG_A] = (uint8_t) ((a << 1U) | (a >> 7U));
            set_flag(state, BM_I8085_FLAG_CY, (a & 0x80U) != 0U);
            /* RLC adds A to itself: V = A7 XOR A6 ([KS1302] note 4). */
            set_flag(state, BM_I8085_FLAG_V, ((a ^ (a << 1U)) & 0x80U) != 0U);
            return BM_STATUS_OK;
        }
        case 0x0fU: {                                   /* RRC */
            uint8_t a = state->registers[REG_A];
            state->registers[REG_A] = (uint8_t) ((a >> 1U) | (a << 7U));
            set_flag(state, BM_I8085_FLAG_CY, (a & 1U) != 0U);
            set_flag(state, BM_I8085_FLAG_V, 0);
            return BM_STATUS_OK;
        }
        case 0x17U: {                                   /* RAL */
            uint8_t a = state->registers[REG_A];
            unsigned int carry = (state->f & BM_I8085_FLAG_CY) != 0U;
            state->registers[REG_A] = (uint8_t) ((a << 1U) | carry);
            set_flag(state, BM_I8085_FLAG_CY, (a & 0x80U) != 0U);
            set_flag(state, BM_I8085_FLAG_V, ((a ^ (a << 1U)) & 0x80U) != 0U);
            return BM_STATUS_OK;
        }
        case 0x1fU: {                                   /* RAR */
            uint8_t a = state->registers[REG_A];
            unsigned int carry = (state->f & BM_I8085_FLAG_CY) != 0U;
            state->registers[REG_A] = (uint8_t) ((a >> 1U) | (carry << 7U));
            set_flag(state, BM_I8085_FLAG_CY, (a & 1U) != 0U);
            set_flag(state, BM_I8085_FLAG_V, 0);
            return BM_STATUS_OK;
        }
        case 0x22U: case 0x2aU: {                       /* SHLD/LHLD */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            if (status != BM_STATUS_OK)
                return status;
            if (op == 0x22U) {
                status = memory_write(state, address, state->registers[5]);
                return status == BM_STATUS_OK ?
                       memory_write(state, (uint16_t) (address + 1U),
                                    state->registers[4]) : status;
            }
            status = memory_read(state, address, &state->registers[5]);
            return status == BM_STATUS_OK ?
                   memory_read(state, (uint16_t) (address + 1U),
                               &state->registers[4]) : status;
        }
        case 0x27U:                                     /* DAA */
            decimal_adjust(state);
            return BM_STATUS_OK;
        case 0x2fU:                                     /* CMA */
            state->registers[REG_A] = (uint8_t) ~state->registers[REG_A];
            return BM_STATUS_OK;
        case 0x32U: case 0x3aU: {                       /* STA/LDA */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            if (status != BM_STATUS_OK)
                return status;
            return op == 0x32U ?
                   memory_write(state, address, state->registers[REG_A]) :
                   memory_read(state, address, &state->registers[REG_A]);
        }
        case 0x37U:                                     /* STC */
            set_flag(state, BM_I8085_FLAG_CY, 1);
            return BM_STATUS_OK;
        case 0x3fU:                                     /* CMC */
            state->f = (uint8_t) (state->f ^ BM_I8085_FLAG_CY);
            return BM_STATUS_OK;
        case 0xc3U: {                                   /* JMP */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            if (status == BM_STATUS_OK)
                state->pc = address;
            return status;
        }
        case 0xc9U: {                                   /* RET */
            uint16_t address = 0U;
            status = pop_word(state, &address);
            if (status == BM_STATUS_OK)
                state->pc = address;
            return status;
        }
        case 0xcdU: {                                   /* CALL */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            return status == BM_STATUS_OK ? call_address(state, address) :
                                            status;
        }
        case 0xd3U: case 0xdbU: {                       /* OUT/IN port */
            uint8_t port = 0U;
            status = fetch_byte(state, &port);
            if (status != BM_STATUS_OK)
                return status;
            return bus_transfer(state, BM_ADDRESS_IO,
                                op == 0xd3U ? BM_BUS_WRITE : BM_BUS_READ,
                                port, &state->registers[REG_A]);
        }
        case 0xe3U: {                                   /* XTHL */
            uint8_t low = 0U;
            uint8_t high = 0U;
            /* The order of the two stack writes is not stated in the cited
             * pages; only the exchanged values are part of the contract. */
            status = memory_read(state, state->sp, &low);
            if (status == BM_STATUS_OK)
                status = memory_read(state, (uint16_t) (state->sp + 1U), &high);
            if (status == BM_STATUS_OK)
                status = memory_write(state, (uint16_t) (state->sp + 1U),
                                      state->registers[4]);
            if (status == BM_STATUS_OK)
                status = memory_write(state, state->sp, state->registers[5]);
            if (status == BM_STATUS_OK) {
                state->registers[4] = high;
                state->registers[5] = low;
            }
            return status;
        }
        case 0xe9U:                                     /* PCHL */
            state->pc = get_hl(state);
            return BM_STATUS_OK;
        case 0xebU: {                                   /* XCHG */
            uint16_t de = get_pair(state, 1U);
            set_pair(state, 1U, get_hl(state));
            set_pair(state, 2U, de);
            return BM_STATUS_OK;
        }
        case 0xf3U:                                     /* DI */
            state->interrupt_enable = 0U;
            state->interrupt_enable_delay = 0U;
            return BM_STATUS_OK;
        case 0xf9U:                                     /* SPHL */
            state->sp = get_hl(state);
            return BM_STATUS_OK;
        case 0xfbU:                                     /* EI */
            state->interrupt_enable = 1U;
            state->interrupt_enable_delay = 1U;
            return BM_STATUS_OK;
        case 0x20U:                                     /* RIM */
            read_interrupt_masks(state);
            return BM_STATUS_OK;
        case 0x30U:                                     /* SIM */
            set_interrupt_masks(state);
            return BM_STATUS_OK;
        default:
            state->pc = start;
            return BM_STATUS_UNSUPPORTED;
    }
}

/* ---------------------------------------------------------------------
 * Boundaries.
 * --------------------------------------------------------------------- */
static void
begin_boundary(bm_i8085_state_t *state)
{
    state->boundary_opcode = 0U;
    state->boundary_taken = 0U;
    state->boundary_transactions = 0U;
    state->boundary_wait_states = 0U;
}

static void
finish_boundary(bm_i8085_state_t *state, bm_i8085_boundary_kind_t kind,
                bm_i8085_interrupt_source_t source, uint8_t t_states,
                uint8_t machine_cycles, int known)
{
    bm_i8085_timing_observation_t *last = &state->last;

    memset(last, 0, sizeof(*last));
    last->size = sizeof(*last);
    last->version = BM_I8085_TIMING_OBSERVATION_VERSION;
    last->kind = kind;
    last->interrupt_source = source;
    last->opcode = state->boundary_opcode;
    last->machine_cycles = known ? machine_cycles : 0U;
    last->branch_taken = state->boundary_taken;
    last->clock_kind = known ? BM_I8085_CLOCKS_EXACT : BM_I8085_CLOCKS_UNKNOWN;
    last->t_states = known ? t_states : 0U;
    last->logical_bus_transactions = state->boundary_transactions;
    last->reported_wait_states = state->boundary_wait_states;
    if (state->timing != NULL)
        state->timing(state->timing_context, last);
}

static bm_status_t
run_instruction_boundary(bm_i8085_state_t *state)
{
    uint8_t t_states = 0U;
    uint8_t machine_cycles = 0U;
    uint8_t delayed = state->interrupt_enable_delay;
    int known;
    bm_status_t status;

    begin_boundary(state);
    status = execute_instruction(state);
    if (status != BM_STATUS_OK)
        return status;
    /* EI takes effect after the following instruction ([MCS85] p. 5-17). */
    if (delayed && (state->boundary_opcode != 0xfbU))
        state->interrupt_enable_delay = 0U;
    known = documented_timing(state->boundary_opcode, state->boundary_taken,
                              &t_states, &machine_cycles) ||
            ((state->tolerance != NULL) &&
             extended_timing(state->boundary_opcode, state->boundary_taken,
                             &t_states, &machine_cycles));
    finish_boundary(state, BM_I8085_BOUNDARY_INSTRUCTION,
                    BM_I8085_INTERRUPT_NONE, t_states, machine_cycles, known);
    return BM_STATUS_OK;
}

/* Fixed priority TRAP > RST 7.5 > RST 6.5 > RST 5.5 > INTR ([ASM85]
 * p. 1-24; [OKI86] Table 1). TRAP needs its latched edge and a high level;
 * the others need IE (after any EI delay) and, for RST n.5, a clear mask. */
static bm_i8085_interrupt_source_t
pending_interrupt(const bm_i8085_state_t *state)
{
    if (state->trap_pending && state->trap_line)
        return BM_I8085_INTERRUPT_TRAP;
    if (!state->interrupt_enable || state->interrupt_enable_delay)
        return BM_I8085_INTERRUPT_NONE;
    if (state->rst75_pending && !(state->interrupt_mask & BM_I8085_MASK_RST75))
        return BM_I8085_INTERRUPT_RST75;
    if (state->rst65_line && !(state->interrupt_mask & BM_I8085_MASK_RST65))
        return BM_I8085_INTERRUPT_RST65;
    if (state->rst55_line && !(state->interrupt_mask & BM_I8085_MASK_RST55))
        return BM_I8085_INTERRUPT_RST55;
    if (state->intr_line && (state->interrupt_ack != NULL))
        return BM_I8085_INTERRUPT_INTR;
    return BM_I8085_INTERRUPT_NONE;
}

/* Accepting any interrupt disables further maskable interrupts; TRAP keeps
 * the previous enable for the next RIM ([MCS85] p. 5-17). TRAP and RST n.5
 * use an internal RESTART: a six-state Bus Idle M1 and two stack writes,
 * 12 T-states ([MCS85] §2.3.5, Figure 2-19, p. 2-15). INTR reads RST n or
 * CALL through INTA cycles: a six-state M1 then RST pushes in two writes
 * (12 T-states) and CALL reads two more INTA cycles before them
 * (18 T-states, Figures 2-17 and 2-18, pp. 2-13..2-14). Leaving HALT adds
 * the THALT state that ends after the request is sampled (p. 2-16). */
static bm_status_t
run_interrupt_boundary(bm_i8085_state_t *state,
                       bm_i8085_interrupt_source_t source)
{
    uint8_t t_states = 12U;
    uint8_t machine_cycles = 3U;
    uint8_t was_halted = state->halted;
    uint8_t enable = state->interrupt_enable;
    uint16_t target;
    bm_status_t status;

    begin_boundary(state);
    state->interrupt_enable = 0U;
    state->interrupt_enable_delay = 0U;
    switch (source) {
        case BM_I8085_INTERRUPT_TRAP:
            state->trap_pending = 0U;
            state->trap_saved_interrupt_enable = enable;
            state->trap_saved_valid = 1U;
            target = 0x0024U;
            break;
        case BM_I8085_INTERRUPT_RST75:
            state->rst75_pending = 0U;
            target = 0x003cU;
            break;
        case BM_I8085_INTERRUPT_RST65:
            target = 0x0034U;
            break;
        case BM_I8085_INTERRUPT_RST55:
            target = 0x002cU;
            break;
        default: {
            uint8_t opcode = 0U;
            uint8_t low = 0U;
            uint8_t high = 0U;

            status = state->interrupt_ack(state->interrupt_context, 0U, &opcode);
            if ((status == BM_STATUS_OK) && (opcode == 0xcdU)) {
                status = state->interrupt_ack(state->interrupt_context, 1U, &low);
                if (status == BM_STATUS_OK)
                    status = state->interrupt_ack(state->interrupt_context, 2U,
                                                  &high);
                t_states = 18U;
                machine_cycles = 5U;
                target = (uint16_t) (((unsigned int) high << 8U) | low);
            } else if ((status == BM_STATUS_OK) && ((opcode & 0xc7U) == 0xc7U)) {
                target = (uint16_t) (opcode & 0x38U);
            } else if (status == BM_STATUS_OK) {
                status = BM_STATUS_UNSUPPORTED;
                target = 0U;
            } else {
                target = 0U;
            }
            if (status != BM_STATUS_OK) {
                state->interrupt_enable = enable;
                return status;
            }
            state->boundary_opcode = opcode;
            break;
        }
    }
    status = push_word(state, state->pc);
    if (status != BM_STATUS_OK)
        return status;
    state->pc = target;
    state->halted = 0U;
    if (was_halted)
        t_states = (uint8_t) (t_states + 1U);
    finish_boundary(state, BM_I8085_BOUNDARY_INTERRUPT, source, t_states,
                    machine_cycles, 1);
    return BM_STATUS_OK;
}

/* RESET IN and READY low each hold the CPU for one documented clock per
 * boundary (TRESET, TWAIT). */
static void
run_hold_boundary(bm_i8085_state_t *state, bm_i8085_boundary_kind_t kind)
{
    begin_boundary(state);
    finish_boundary(state, kind, BM_I8085_INTERRUPT_NONE, 1U, 0U, 1);
}

/* RESET IN clears PC, the interrupt enable, the RST 7.5 and TRAP flip-flops
 * and SOD, and sets all three RST masks. General registers and flags are
 * not explicitly changed ([MCS85] §2.3.7, p. 2-18). */
static void
apply_reset_in(bm_i8085_state_t *state)
{
    state->pc = 0U;
    state->interrupt_enable = 0U;
    state->interrupt_enable_delay = 0U;
    state->halted = 0U;
    state->interrupt_mask = BM_I8085_MASK_RST55 | BM_I8085_MASK_RST65 |
                            BM_I8085_MASK_RST75;
    state->rst75_pending = 0U;
    state->trap_pending = 0U;
    state->trap_saved_interrupt_enable = 0U;
    state->trap_saved_valid = 0U;
    state->sod = 0U;
}

static bm_status_t
cpu_reset(void *context)
{
    bm_i8085_state_t *state = context;

    if (state == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    apply_reset_in(state);
    return BM_STATUS_OK;
}

static bm_status_t
cpu_run(void *context, bm_tick_t budget, bm_tick_t *consumed)
{
    bm_i8085_state_t *state = context;
    bm_status_t status;

    if ((state == NULL) || (consumed == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *consumed = 0U;
    while (*consumed < budget) {
        bm_i8085_interrupt_source_t source;

        if (state->reset_line) {
            run_hold_boundary(state, BM_I8085_BOUNDARY_RESET);
            ++*consumed;
            continue;
        }
        source = pending_interrupt(state);
        if (state->halted && (source == BM_I8085_INTERRUPT_NONE))
            return BM_STATUS_IDLE;
        if (state->wait_line) {
            run_hold_boundary(state, BM_I8085_BOUNDARY_WAIT);
            ++*consumed;
            continue;
        }
        status = source != BM_I8085_INTERRUPT_NONE ?
                 run_interrupt_boundary(state, source) :
                 run_instruction_boundary(state);
        if (status != BM_STATUS_OK)
            return status;
        ++*consumed;
    }
    return BM_STATUS_OK;
}

static bm_status_t
cpu_signal(void *context, uint32_t line, int asserted)
{
    bm_i8085_state_t *state = context;
    uint8_t level = (uint8_t) (asserted != 0);

    if (state == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    switch (line) {
        case BM_I8085_SIGNAL_INTR:
            state->intr_line = level;
            break;
        case BM_I8085_SIGNAL_TRAP:
            /* Edge and level sensitive ([OKI86] Table 1, p. 19). */
            if (level && !state->trap_line)
                state->trap_pending = 1U;
            state->trap_line = level;
            break;
        case BM_I8085_SIGNAL_RST55:
            state->rst55_line = level;
            break;
        case BM_I8085_SIGNAL_RST65:
            state->rst65_line = level;
            break;
        case BM_I8085_SIGNAL_RST75:
            /* The flip-flop is set by a rising edge even while masked
             * ([MCS85] p. 5-18). */
            if (level && !state->rst75_line)
                state->rst75_pending = 1U;
            state->rst75_line = level;
            break;
        case BM_I8085_SIGNAL_RESET:
            state->reset_line = level;
            if (level)
                apply_reset_in(state);
            break;
        case BM_I8085_SIGNAL_WAIT:
            state->wait_line = level;
            break;
        case BM_I8085_SIGNAL_SID:
            state->sid_line = level;
            break;
        default:
            return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static bm_status_t
cpu_inspect(const void *context, const char *name, uint64_t *value)
{
    static const char *const register_names[8] = {
        "b", "c", "d", "e", "h", "l", NULL, "a"
    };
    const bm_i8085_state_t *state = context;
    unsigned int index;

    if ((state == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < 8U; ++index) {
        if ((register_names[index] != NULL) &&
            (strcmp(name, register_names[index]) == 0)) {
            *value = state->registers[index];
            return BM_STATUS_OK;
        }
    }
    if (strcmp(name, "f") == 0)
        *value = state->f;
    else if (strcmp(name, "bc") == 0)
        *value = get_pair(state, 0U);
    else if (strcmp(name, "de") == 0)
        *value = get_pair(state, 1U);
    else if (strcmp(name, "hl") == 0)
        *value = get_pair(state, 2U);
    else if (strcmp(name, "sp") == 0)
        *value = state->sp;
    else if (strcmp(name, "pc") == 0)
        *value = state->pc;
    else if (strcmp(name, "ie") == 0)
        *value = state->interrupt_enable;
    else if (strcmp(name, "halted") == 0)
        *value = state->halted;
    else if (strcmp(name, "interrupt_mask") == 0)
        *value = state->interrupt_mask;
    else if (strcmp(name, "rst75_pending") == 0)
        *value = state->rst75_pending;
    else if (strcmp(name, "trap_pending") == 0)
        *value = state->trap_pending;
    else if (strcmp(name, "sod") == 0)
        *value = state->sod;
    else if (strcmp(name, "last_t_states") == 0)
        *value = state->last.t_states;
    /* Address and opcode of the instruction in progress, for devices that
     * report what the program was doing when it reached them. */
    else if (strcmp(name, "instruction_pc") == 0)
        *value = state->boundary_pc;
    else if (strcmp(name, "instruction_opcode") == 0)
        *value = state->boundary_opcode;
    else
        return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static void
cpu_destroy(void *context)
{
    bm_i8085_state_t *state = context;

    if (state != NULL)
        state->host.release(state->host.context, state);
}

bm_status_t
bm_i8085_create(const bm_host_services_t *host,
                const bm_i8085_config_t *config,
                bm_cpu_t *out_cpu)
{
    bm_i8085_state_t *state;
    const char *name = config != NULL ? model_name(config->model) : NULL;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (config == NULL) ||
        (out_cpu == NULL) || (config->bus == NULL) || (name == NULL) ||
        (config->frequency_hz == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    memset(out_cpu, 0, sizeof(*out_cpu));
    state = host->allocate(host->context, sizeof(*state));
    if (state == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(state, 0, sizeof(*state));
    state->host = *host;
    state->bus = config->bus;
    state->model = config->model;
    state->frequency_hz = config->frequency_hz;
    state->interrupt_ack = config->interrupt_ack;
    state->interrupt_context = config->interrupt_context;
    state->sod_output = config->sod;
    state->sod_context = config->sod_context;
    state->timing = config->timing;
    state->timing_context = config->timing_context;
    state->tolerance = bm_host_tolerance(host);
    apply_reset_in(state);
    *out_cpu = (bm_cpu_t) {
        name,
        state,
        { cpu_reset, cpu_run, cpu_signal, cpu_inspect, cpu_destroy }
    };
    return BM_STATUS_OK;
}

static int
is_i8085_cpu(const bm_cpu_t *cpu)
{
    return (cpu != NULL) && (cpu->context != NULL) &&
           (cpu->ops.reset == cpu_reset) && (cpu->ops.run == cpu_run) &&
           (cpu->ops.signal == cpu_signal) &&
           (cpu->ops.inspect == cpu_inspect) &&
           (cpu->ops.destroy == cpu_destroy);
}

bm_status_t
bm_i8085_get_arch_state(const bm_cpu_t *cpu, bm_i8085_arch_state_t *out_state)
{
    const bm_i8085_state_t *state;

    if (!is_i8085_cpu(cpu) || (out_state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state = cpu->context;
    memset(out_state, 0, sizeof(*out_state));
    out_state->size = sizeof(*out_state);
    out_state->version = BM_I8085_ARCH_STATE_VERSION;
    out_state->model = state->model;
    out_state->a = state->registers[REG_A];
    out_state->f = state->f;
    out_state->b = state->registers[0];
    out_state->c = state->registers[1];
    out_state->d = state->registers[2];
    out_state->e = state->registers[3];
    out_state->h = state->registers[4];
    out_state->l = state->registers[5];
    out_state->sp = state->sp;
    out_state->pc = state->pc;
    out_state->interrupt_enable = state->interrupt_enable;
    out_state->interrupt_enable_delay = state->interrupt_enable_delay;
    out_state->halted = state->halted;
    out_state->interrupt_mask = state->interrupt_mask;
    out_state->rst75_pending = state->rst75_pending;
    out_state->trap_pending = state->trap_pending;
    out_state->trap_saved_interrupt_enable = state->trap_saved_interrupt_enable;
    out_state->trap_saved_valid = state->trap_saved_valid;
    out_state->sod = state->sod;
    return BM_STATUS_OK;
}

bm_status_t
bm_i8085_set_arch_state(bm_cpu_t *cpu, const bm_i8085_arch_state_t *image)
{
    bm_i8085_state_t *state;

    if (!is_i8085_cpu(cpu) || (image == NULL) ||
        (image->size != sizeof(*image)) ||
        (image->version != BM_I8085_ARCH_STATE_VERSION) ||
        (image->model != ((bm_i8085_state_t *) cpu->context)->model) ||
        ((image->f & 0x08U) != 0U) ||
        (image->interrupt_enable > 1U) ||
        (image->interrupt_enable_delay > 1U) ||
        (image->halted > 1U) ||
        (image->interrupt_mask > 7U) ||
        (image->rst75_pending > 1U) ||
        (image->trap_pending > 1U) ||
        (image->trap_saved_interrupt_enable > 1U) ||
        (image->trap_saved_valid > 1U) ||
        (image->sod > 1U) ||
        (image->interrupt_enable_delay && !image->interrupt_enable))
        return BM_STATUS_INVALID_ARGUMENT;
    state = cpu->context;
    state->registers[REG_A] = image->a;
    state->f = image->f;
    state->registers[0] = image->b;
    state->registers[1] = image->c;
    state->registers[2] = image->d;
    state->registers[3] = image->e;
    state->registers[4] = image->h;
    state->registers[5] = image->l;
    state->sp = image->sp;
    state->pc = image->pc;
    state->interrupt_enable = image->interrupt_enable;
    state->interrupt_enable_delay = image->interrupt_enable_delay;
    state->halted = image->halted;
    state->interrupt_mask = image->interrupt_mask;
    state->rst75_pending = image->rst75_pending;
    state->trap_pending = image->trap_pending;
    state->trap_saved_interrupt_enable = image->trap_saved_interrupt_enable;
    state->trap_saved_valid = image->trap_saved_valid;
    state->sod = image->sod;
    return BM_STATUS_OK;
}

bm_i8085_opcode_class_t
bm_i8085_classify_opcode(uint8_t opcode)
{
    switch (opcode) {
        case 0x08U: /* DSUB */
        case 0x10U: /* ARHL */
        case 0x18U: /* RDEL */
        case 0x28U: /* LDHI */
        case 0x38U: /* LDSI */
        case 0xcbU: /* RSTV */
        case 0xd9U: /* SHLX */
        case 0xddU: /* JNK */
        case 0xedU: /* LHLX */
        case 0xfdU: /* JK */
            return BM_I8085_OPCODE_UNDOCUMENTED;
        default:
            return BM_I8085_OPCODE_DOCUMENTED;
    }
}

bm_status_t
bm_i8085_step(bm_cpu_t *cpu, bm_tick_t *consumed)
{
    if (!is_i8085_cpu(cpu))
        return BM_STATUS_INVALID_ARGUMENT;
    return cpu_run(cpu->context, 1U, consumed);
}

bm_status_t
bm_i8085_step_clocked(void *context, bm_tick_t start_ns, uint64_t *cycles)
{
    bm_i8085_state_t *state = context;
    bm_tick_t consumed = 0U;
    bm_status_t status;

    (void) start_ns;
    if ((state == NULL) || (cycles == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *cycles = 0U;
    status = cpu_run(state, 1U, &consumed);
    if (status != BM_STATUS_OK)
        return status;
    if ((consumed != 1U) || (state->last.clock_kind != BM_I8085_CLOCKS_EXACT) ||
        (state->last.t_states == 0U))
        return BM_STATUS_UNSUPPORTED;
    *cycles = state->last.t_states + state->last.reported_wait_states;
    return BM_STATUS_OK;
}
