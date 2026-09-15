/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited 808x/Vx0 interpreter. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca. This file deliberately implements only the reset bring-up subset.
 */
#include <blumach/components/cpu_808x.h>

#include <string.h>

enum {
    REG_AX = 0,
    REG_CX,
    REG_DX,
    REG_BX,
    REG_SP,
    REG_BP,
    REG_SI,
    REG_DI
};

enum {
    FLAG_CF = 0x0001,
    FLAG_PF = 0x0004,
    FLAG_AF = 0x0010,
    FLAG_ZF = 0x0040,
    FLAG_SF = 0x0080,
    FLAG_TF = 0x0100,
    FLAG_IF = 0x0200,
    FLAG_DF = 0x0400,
    FLAG_OF = 0x0800
};

typedef struct bm_808x_state {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_808x_model_t model;
    uint32_t frequency_hz;
    uint16_t registers[8];
    uint16_t segments[4];
    uint16_t ip;
    uint16_t flags;
    uint32_t last_fetch;
    uint8_t last_opcode;
    int halted;
    int interrupt_asserted;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
} bm_808x_state_t;

static uint32_t
physical_address(uint16_t segment, uint16_t offset)
{
    return ((((uint32_t) segment << 4U) + offset) & 0xfffffU);
}

static bm_status_t
read_byte(bm_808x_state_t *state, uint16_t segment, uint16_t offset,
          bm_bus_operation_t operation, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, operation, physical_address(segment, offset), 0,
        1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(state->bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
fetch_byte(bm_808x_state_t *state, uint8_t *value)
{
    bm_status_t status = read_byte(state, state->segments[1], state->ip,
                                   BM_BUS_FETCH, value);
    if (status == BM_STATUS_OK)
        ++state->ip;
    return status;
}

static bm_status_t
fetch_word(bm_808x_state_t *state, uint16_t *value)
{
    uint8_t low = 0;
    uint8_t high = 0;
    bm_status_t status = fetch_byte(state, &low);
    if (status == BM_STATUS_OK)
        status = fetch_byte(state, &high);
    if (status == BM_STATUS_OK)
        *value = (uint16_t) (low | ((uint16_t) high << 8U));
    return status;
}

static bm_status_t
write_byte(bm_808x_state_t *state, uint16_t segment, uint16_t offset, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, physical_address(segment, offset), value,
        1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(state->bus, &transaction);
}

static bm_status_t
read_word(bm_808x_state_t *state, uint16_t segment, uint16_t offset, uint16_t *value)
{
    uint8_t low = 0;
    uint8_t high = 0;
    bm_status_t status = read_byte(state, segment, offset, BM_BUS_READ, &low);
    if (status == BM_STATUS_OK)
        status = read_byte(state, segment, (uint16_t) (offset + 1U), BM_BUS_READ, &high);
    if (status == BM_STATUS_OK)
        *value = (uint16_t) (low | ((uint16_t) high << 8U));
    return status;
}

static bm_status_t
write_word(bm_808x_state_t *state, uint16_t segment, uint16_t offset, uint16_t value)
{
    bm_status_t status = write_byte(state, segment, offset, (uint8_t) value);
    if (status == BM_STATUS_OK)
        status = write_byte(state, segment, (uint16_t) (offset + 1U), (uint8_t) (value >> 8U));
    return status;
}

static bm_status_t
push_word(bm_808x_state_t *state, uint16_t value)
{
    state->registers[REG_SP] = (uint16_t) (state->registers[REG_SP] - 2U);
    return write_word(state, state->segments[2], state->registers[REG_SP], value);
}

static bm_status_t
pop_word(bm_808x_state_t *state, uint16_t *value)
{
    bm_status_t status = read_word(state, state->segments[2],
                                   state->registers[REG_SP], value);
    if (status == BM_STATUS_OK)
        state->registers[REG_SP] = (uint16_t) (state->registers[REG_SP] + 2U);
    return status;
}

typedef struct bm_808x_operand {
    int is_register;
    unsigned int register_index;
    uint16_t segment;
    uint16_t offset;
} bm_808x_operand_t;

static uint8_t get_register_byte(const bm_808x_state_t *state, unsigned int index);
static void set_register_byte(bm_808x_state_t *state, unsigned int index, uint8_t value);

static bm_status_t
decode_rm_operand(bm_808x_state_t *state, uint8_t modrm, int segment_override,
                  bm_808x_operand_t *operand)
{
    unsigned int mod = modrm >> 6U;
    unsigned int rm = modrm & 7U;
    int32_t address;
    int uses_bp = 0;

    memset(operand, 0, sizeof(*operand));
    if (mod == 3U) {
        operand->is_register = 1;
        operand->register_index = rm;
        return BM_STATUS_OK;
    }
    switch (rm) {
        case 0: address = state->registers[REG_BX] + state->registers[REG_SI]; break;
        case 1: address = state->registers[REG_BX] + state->registers[REG_DI]; break;
        case 2:
            address = state->registers[REG_BP] + state->registers[REG_SI];
            uses_bp = 1;
            break;
        case 3:
            address = state->registers[REG_BP] + state->registers[REG_DI];
            uses_bp = 1;
            break;
        case 4: address = state->registers[REG_SI]; break;
        case 5: address = state->registers[REG_DI]; break;
        case 6:
            if (mod == 0U) {
                uint16_t direct;
                bm_status_t status = fetch_word(state, &direct);
                if (status != BM_STATUS_OK)
                    return status;
                address = direct;
            } else {
                address = state->registers[REG_BP];
                uses_bp = 1;
            }
            break;
        default: address = state->registers[REG_BX]; break;
    }
    if (mod == 1U) {
        uint8_t displacement;
        bm_status_t status = fetch_byte(state, &displacement);
        if (status != BM_STATUS_OK)
            return status;
        address += (int8_t) displacement;
    } else if (mod == 2U) {
        uint16_t displacement;
        bm_status_t status = fetch_word(state, &displacement);
        if (status != BM_STATUS_OK)
            return status;
        address += (int16_t) displacement;
    }
    operand->segment = state->segments[(segment_override >= 0) ?
                                      (unsigned int) segment_override :
                                      (uses_bp ? 2U : 3U)];
    operand->offset = (uint16_t) address;
    return BM_STATUS_OK;
}

static bm_status_t
read_operand_byte(bm_808x_state_t *state, const bm_808x_operand_t *operand, uint8_t *value)
{
    if (operand->is_register) {
        *value = get_register_byte(state, operand->register_index);
        return BM_STATUS_OK;
    }
    return read_byte(state, operand->segment, operand->offset, BM_BUS_READ, value);
}

static bm_status_t
read_operand_word(bm_808x_state_t *state, const bm_808x_operand_t *operand, uint16_t *value)
{
    if (operand->is_register) {
        *value = state->registers[operand->register_index];
        return BM_STATUS_OK;
    }
    return read_word(state, operand->segment, operand->offset, value);
}

static bm_status_t
write_operand_word(bm_808x_state_t *state, const bm_808x_operand_t *operand, uint16_t value)
{
    if (operand->is_register) {
        state->registers[operand->register_index] = value;
        return BM_STATUS_OK;
    }
    return write_word(state, operand->segment, operand->offset, value);
}

static bm_status_t
service_interrupt(bm_808x_state_t *state)
{
    uint8_t vector;
    uint16_t new_ip = 0;
    uint16_t new_cs = 0;
    bm_status_t status;

    if (state->interrupt_ack == NULL)
        return BM_STATUS_INVALID_STATE;
    status = state->interrupt_ack(state->interrupt_context, &vector);
    if (status != BM_STATUS_OK)
        return status;
    status = push_word(state, state->flags);
    if (status == BM_STATUS_OK)
        status = push_word(state, state->segments[1]);
    if (status == BM_STATUS_OK)
        status = push_word(state, state->ip);
    state->flags &= (uint16_t) ~(FLAG_IF | FLAG_TF);
    if (status == BM_STATUS_OK)
        status = read_word(state, 0, (uint16_t) ((uint16_t) vector * 4U), &new_ip);
    if (status == BM_STATUS_OK)
        status = read_word(state, 0, (uint16_t) ((uint16_t) vector * 4U + 2U), &new_cs);
    if (status == BM_STATUS_OK) {
        state->ip = new_ip;
        state->segments[1] = new_cs;
        state->halted = 0;
    }
    return status;
}

static uint8_t
get_register_byte(const bm_808x_state_t *state, unsigned int index)
{
    uint16_t value = state->registers[index & 3U];
    return (index < 4U) ? (uint8_t) value : (uint8_t) (value >> 8U);
}

static void
set_register_byte(bm_808x_state_t *state, unsigned int index, uint8_t value)
{
    uint16_t *word = &state->registers[index & 3U];
    if (index < 4U)
        *word = (uint16_t) ((*word & 0xff00U) | value);
    else
        *word = (uint16_t) ((*word & 0x00ffU) | ((uint16_t) value << 8U));
}

static int
even_parity(uint8_t value)
{
    value ^= value >> 4U;
    value &= 0x0fU;
    return ((0x9669U >> value) & 1U) != 0;
}

static void
set_logic_flags(bm_808x_state_t *state, uint16_t value, unsigned int width)
{
    uint16_t sign = (width == 8U) ? 0x0080U : 0x8000U;
    uint16_t mask = (width == 8U) ? 0x00ffU : 0xffffU;
    value &= mask;
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF);
    if (value == 0)
        state->flags |= FLAG_ZF;
    if ((value & sign) != 0)
        state->flags |= FLAG_SF;
    if (even_parity((uint8_t) value))
        state->flags |= FLAG_PF;
}

static uint16_t
add16(bm_808x_state_t *state, uint16_t left, uint16_t right)
{
    uint32_t wide = (uint32_t) left + right;
    uint16_t result = (uint16_t) wide;
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF);
    if (wide > 0xffffU)
        state->flags |= FLAG_CF;
    if (((left ^ right ^ result) & 0x0010U) != 0)
        state->flags |= FLAG_AF;
    if (result == 0)
        state->flags |= FLAG_ZF;
    if ((result & 0x8000U) != 0)
        state->flags |= FLAG_SF;
    if (even_parity((uint8_t) result))
        state->flags |= FLAG_PF;
    if (((~(left ^ right) & (left ^ result)) & 0x8000U) != 0)
        state->flags |= FLAG_OF;
    return result;
}

static uint8_t
add8(bm_808x_state_t *state, uint8_t left, uint8_t right)
{
    uint16_t wide = (uint16_t) left + right;
    uint8_t result = (uint8_t) wide;
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF);
    if (wide > 0xffU)
        state->flags |= FLAG_CF;
    if (((left ^ right ^ result) & 0x10U) != 0)
        state->flags |= FLAG_AF;
    if (result == 0)
        state->flags |= FLAG_ZF;
    if ((result & 0x80U) != 0)
        state->flags |= FLAG_SF;
    if (even_parity(result))
        state->flags |= FLAG_PF;
    if (((~(left ^ right) & (left ^ result)) & 0x80U) != 0)
        state->flags |= FLAG_OF;
    return result;
}

static void
compare16(bm_808x_state_t *state, uint16_t left, uint16_t right)
{
    uint16_t result = (uint16_t) (left - right);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF);
    if (left < right)
        state->flags |= FLAG_CF;
    if (((left ^ right ^ result) & 0x0010U) != 0)
        state->flags |= FLAG_AF;
    if (result == 0)
        state->flags |= FLAG_ZF;
    if ((result & 0x8000U) != 0)
        state->flags |= FLAG_SF;
    if (even_parity((uint8_t) result))
        state->flags |= FLAG_PF;
    if ((((left ^ right) & (left ^ result)) & 0x8000U) != 0)
        state->flags |= FLAG_OF;
}

static bm_status_t
write_operand_byte(bm_808x_state_t *state, const bm_808x_operand_t *operand, uint8_t value)
{
    if (operand->is_register) {
        set_register_byte(state, operand->register_index, value);
        return BM_STATUS_OK;
    }
    return write_byte(state, operand->segment, operand->offset, value);
}

static void
compare8(bm_808x_state_t *state, uint8_t left, uint8_t right)
{
    uint8_t result = (uint8_t) (left - right);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_OF);
    if (left < right)
        state->flags |= FLAG_CF;
    if (((left ^ right ^ result) & 0x10U) != 0)
        state->flags |= FLAG_AF;
    if (result == 0)
        state->flags |= FLAG_ZF;
    if ((result & 0x80U) != 0)
        state->flags |= FLAG_SF;
    if (even_parity(result))
        state->flags |= FLAG_PF;
    if ((((left ^ right) & (left ^ result)) & 0x80U) != 0)
        state->flags |= FLAG_OF;
}

static bm_status_t
execute_string_word(bm_808x_state_t *state, uint8_t opcode, int repeat)
{
    bm_status_t status;

    if ((opcode != 0xabU) && (opcode != 0xafU))
        return BM_STATUS_UNSUPPORTED;
    while (!repeat || (state->registers[REG_CX] != 0)) {
        if (opcode == 0xabU) { /* STOSW: AX -> ES:DI. */
            status = write_word(state, state->segments[0],
                                state->registers[REG_DI], state->registers[REG_AX]);
        } else { /* SCASW: compare AX with ES:DI. */
            uint16_t value = 0;
            status = read_word(state, state->segments[0],
                               state->registers[REG_DI], &value);
            if (status == BM_STATUS_OK)
                compare16(state, state->registers[REG_AX], value);
        }
        if (status != BM_STATUS_OK)
            return status;
        if ((state->flags & FLAG_DF) != 0)
            state->registers[REG_DI] = (uint16_t) (state->registers[REG_DI] - 2U);
        else
            state->registers[REG_DI] = (uint16_t) (state->registers[REG_DI] + 2U);
        if (!repeat)
            break;
        state->registers[REG_CX] = (uint16_t) (state->registers[REG_CX] - 1U);
        if ((opcode == 0xafU) && ((state->flags & FLAG_ZF) == 0))
            break; /* REP/REPE SCASW stops at the first mismatch. */
    }
    return BM_STATUS_OK;
}

static int
jump_condition(const bm_808x_state_t *state, unsigned int condition)
{
    int cf = (state->flags & FLAG_CF) != 0;
    int pf = (state->flags & FLAG_PF) != 0;
    int zf = (state->flags & FLAG_ZF) != 0;
    int sf = (state->flags & FLAG_SF) != 0;
    int of = (state->flags & FLAG_OF) != 0;
    switch (condition & 0x0fU) {
        case 0: return of;
        case 1: return !of;
        case 2: return cf;
        case 3: return !cf;
        case 4: return zf;
        case 5: return !zf;
        case 6: return cf || zf;
        case 7: return !cf && !zf;
        case 8: return sf;
        case 9: return !sf;
        case 10: return pf;
        case 11: return !pf;
        case 12: return sf != of;
        case 13: return sf == of;
        case 14: return zf || (sf != of);
        default: return !zf && (sf == of);
    }
}

static bm_status_t
io_read_byte(bm_808x_state_t *state, uint16_t port, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(state->bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
io_write_byte(bm_808x_state_t *state, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(state->bus, &transaction);
}

static bm_status_t
io_read_word(bm_808x_state_t *state, uint16_t port, uint16_t *value)
{
    uint8_t low = 0;
    uint8_t high = 0;
    bm_status_t status = io_read_byte(state, port, &low);
    if (status == BM_STATUS_OK)
        status = io_read_byte(state, (uint16_t) (port + 1U), &high);
    if (status == BM_STATUS_OK)
        *value = (uint16_t) (low | ((uint16_t) high << 8U));
    return status;
}

static bm_status_t
io_write_word(bm_808x_state_t *state, uint16_t port, uint16_t value)
{
    bm_status_t status = io_write_byte(state, port, (uint8_t) value);
    if (status == BM_STATUS_OK)
        status = io_write_byte(state, (uint16_t) (port + 1U), (uint8_t) (value >> 8U));
    return status;
}

static bm_status_t
cpu_reset(void *context)
{
    bm_808x_state_t *state = context;
    memset(state->registers, 0, sizeof(state->registers));
    memset(state->segments, 0, sizeof(state->segments));
    state->segments[1] = 0xffffU; /* CS:IP resolves to physical FFFF0h. */
    state->ip = 0;
    state->flags = 0xf002U;
    state->last_fetch = 0xffff0U;
    state->last_opcode = 0;
    state->halted = 0;
    state->interrupt_asserted = 0;
    return BM_STATUS_OK;
}

static bm_status_t
execute_one(bm_808x_state_t *state)
{
    uint16_t instruction_ip = state->ip;
    uint8_t opcode;
    int segment_override = -1;
    int repeat = 0;
    bm_status_t status = fetch_byte(state, &opcode);

    if (status != BM_STATUS_OK)
        return status;
    state->last_fetch = physical_address(state->segments[1], instruction_ip);
    state->last_opcode = opcode;
    if (state->trace != NULL) {
        bm_808x_trace_t trace = {
            state->segments[1], instruction_ip, state->last_fetch, opcode
        };
        state->trace(state->trace_context, &trace);
    }

    for (;;) {
        int prefix_segment = -1;
        switch (opcode) {
            case 0x26: prefix_segment = 0; break; /* ES: */
            case 0x2e: prefix_segment = 1; break; /* CS: */
            case 0x36: prefix_segment = 2; break; /* SS: */
            case 0x3e: prefix_segment = 3; break; /* DS: */
            case 0xf3: repeat = 1; break;          /* REP/REPE */
            default: break;
        }
        if ((prefix_segment < 0) && (opcode != 0xf3U))
            break;
        if (prefix_segment >= 0)
            segment_override = prefix_segment;
        status = fetch_byte(state, &opcode);
        if (status != BM_STATUS_OK)
            return status;
    }

    if (repeat && (opcode != 0xabU) && (opcode != 0xafU))
        return BM_STATUS_UNSUPPORTED;

    if ((opcode >= 0xb8U) && (opcode <= 0xbfU)) {
        uint16_t immediate;
        status = fetch_word(state, &immediate);
        if (status == BM_STATUS_OK)
            state->registers[opcode - 0xb8U] = immediate;
        return status;
    }
    if ((opcode >= 0xb0U) && (opcode <= 0xb7U)) {
        uint8_t immediate;
        status = fetch_byte(state, &immediate);
        if (status == BM_STATUS_OK) {
            set_register_byte(state, opcode - 0xb0U, immediate);
        }
        return status;
    }
    if ((opcode >= 0x70U) && (opcode <= 0x7fU)) {
        uint8_t displacement;
        status = fetch_byte(state, &displacement);
        if ((status == BM_STATUS_OK) && jump_condition(state, opcode - 0x70U))
            state->ip = (uint16_t) (state->ip + (int8_t) displacement);
        return status;
    }
    if ((opcode >= 0x40U) && (opcode <= 0x47U)) {
        unsigned int index = opcode - 0x40U;
        uint16_t carry = state->flags & FLAG_CF;
        state->registers[index] = add16(state, state->registers[index], 1U);
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
        return BM_STATUS_OK;
    }

    switch (opcode) {
        case 0x90: /* NOP */
            return BM_STATUS_OK;
        case 0xab: /* STOSW */
        case 0xaf: /* SCASW */
            return execute_string_word(state, opcode, repeat);
        case 0x05: { /* ADD AX,imm16 */
            uint16_t immediate = 0;
            status = fetch_word(state, &immediate);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = add16(state, state->registers[REG_AX], immediate);
            return status;
        }
        case 0x0c: /* OR AL,imm8 */
        case 0x24: { /* AND AL,imm8 */
            uint8_t immediate;
            uint8_t result;
            status = fetch_byte(state, &immediate);
            if (status != BM_STATUS_OK)
                return status;
            result = (opcode == 0x0cU) ?
                (uint8_t) (get_register_byte(state, 0) | immediate) :
                (uint8_t) (get_register_byte(state, 0) & immediate);
            set_register_byte(state, 0, result);
            set_logic_flags(state, result, 8);
            return BM_STATUS_OK;
        }
        case 0x02: /* ADD r8,r/m8 */
        case 0x0a: { /* OR r8,r/m8 */
            uint8_t modrm;
            uint8_t source = 0;
            uint8_t result;
            unsigned int destination;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &source);
            if (status != BM_STATUS_OK)
                return status;
            destination = (modrm >> 3U) & 7U;
            if (opcode == 0x02U)
                result = add8(state, get_register_byte(state, destination), source);
            else {
                result = (uint8_t) (get_register_byte(state, destination) | source);
                set_logic_flags(state, result, 8);
            }
            set_register_byte(state, destination, result);
            return BM_STATUS_OK;
        }
        case 0x0b: /* OR r16,r/m16; register form. */
        case 0x33: /* XOR r16,r/m16; register form. */
        case 0x3b: /* CMP r16,r/m16; register form. */
        case 0x8b: { /* MOV r16,r/m16; register form. */
            uint8_t modrm;
            unsigned int destination;
            unsigned int source;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if ((modrm & 0xc0U) != 0xc0U)
                return BM_STATUS_UNSUPPORTED;
            destination = (modrm >> 3U) & 7U;
            source = modrm & 7U;
            if (opcode == 0x8bU)
                state->registers[destination] = state->registers[source];
            else if (opcode == 0x3bU)
                compare16(state, state->registers[destination], state->registers[source]);
            else {
                uint16_t value = (opcode == 0x0bU) ?
                    (uint16_t) (state->registers[destination] | state->registers[source]) :
                    (uint16_t) (state->registers[destination] ^ state->registers[source]);
                state->registers[destination] = value;
                set_logic_flags(state, value, 16);
            }
            return BM_STATUS_OK;
        }
        case 0x39: /* CMP r/m16,r16 */
        case 0x89: { /* MOV r/m16,r16 */
            uint8_t modrm;
            uint16_t destination = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x89U)
                return write_operand_word(state, &operand,
                                          state->registers[(modrm >> 3U) & 7U]);
            status = read_operand_word(state, &operand, &destination);
            if (status == BM_STATUS_OK)
                compare16(state, destination, state->registers[(modrm >> 3U) & 7U]);
            return status;
        }
        case 0x32: /* XOR r8,r/m8; register form. */
        case 0x3a: { /* CMP r8,r/m8. */
            uint8_t modrm;
            unsigned int destination;
            uint8_t source = 0;
            uint8_t value;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &source);
            if (status != BM_STATUS_OK)
                return status;
            destination = (modrm >> 3U) & 7U;
            if (opcode == 0x3aU)
                compare8(state, get_register_byte(state, destination), source);
            else {
                value = (uint8_t) (get_register_byte(state, destination) ^ source);
                set_register_byte(state, destination, value);
                set_logic_flags(state, value, 8);
            }
            return BM_STATUS_OK;
        }
        case 0x8a: { /* MOV r8,r/m8. */
            uint8_t modrm;
            uint8_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, (modrm >> 3U) & 7U, value);
            return status;
        }
        case 0x86: { /* XCHG r8,r/m8; register form. */
            uint8_t modrm;
            unsigned int left;
            unsigned int right;
            uint8_t temporary;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if ((modrm & 0xc0U) != 0xc0U)
                return BM_STATUS_UNSUPPORTED;
            left = (modrm >> 3U) & 7U;
            right = modrm & 7U;
            temporary = get_register_byte(state, left);
            set_register_byte(state, left, get_register_byte(state, right));
            set_register_byte(state, right, temporary);
            return BM_STATUS_OK;
        }
        case 0x80: /* Immediate arithmetic group; AND r/m8,imm8 subset. */
        case 0x81: { /* Immediate arithmetic group; AND/CMP r/m16,imm16 subset. */
            uint8_t modrm;
            unsigned int operation;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if (((opcode == 0x80U) && (operation != 4U)) ||
                ((opcode == 0x81U) && (operation != 4U) && (operation != 7U)))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (opcode == 0x80U) {
                uint8_t immediate = 0;
                uint8_t left = 0;
                uint8_t result;
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = read_operand_byte(state, &operand, &left);
                if (status == BM_STATUS_OK) {
                    if (operation == 4U) {
                        result = (uint8_t) (left & immediate);
                        status = write_operand_byte(state, &operand, result);
                        if (status == BM_STATUS_OK)
                            set_logic_flags(state, result, 8);
                    }
                }
            } else {
                uint16_t immediate = 0;
                uint16_t left = 0;
                uint16_t result;
                if (status == BM_STATUS_OK)
                    status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = read_operand_word(state, &operand, &left);
                if (status == BM_STATUS_OK) {
                    if (operation == 4U) {
                        result = (uint16_t) (left & immediate);
                        status = write_operand_word(state, &operand, result);
                        if (status == BM_STATUS_OK)
                            set_logic_flags(state, result, 16);
                    } else {
                        compare16(state, left, immediate);
                    }
                }
            }
            return status;
        }
        case 0xea: { /* JMP ptr16:16 */
            uint16_t offset = 0;
            uint16_t segment = 0;
            status = fetch_word(state, &offset);
            if (status == BM_STATUS_OK)
                status = fetch_word(state, &segment);
            if (status == BM_STATUS_OK) {
                state->segments[1] = segment;
                state->ip = offset;
            }
            return status;
        }
        case 0x8e: { /* MOV Sreg,r16; register form only in this bring-up subset. */
            uint8_t modrm;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if (((modrm & 0xc0U) != 0xc0U) || (((modrm >> 3U) & 3U) == 1U))
                return BM_STATUS_UNSUPPORTED;
            state->segments[(modrm >> 3U) & 3U] = state->registers[modrm & 7U];
            return BM_STATUS_OK;
        }
        case 0x8c: { /* MOV r16,Sreg; register form. */
            uint8_t modrm;
            unsigned int segment;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            segment = (modrm >> 3U) & 3U;
            if ((modrm & 0xc0U) != 0xc0U)
                return BM_STATUS_UNSUPPORTED;
            state->registers[modrm & 7U] = state->segments[segment];
            return BM_STATUS_OK;
        }
        case 0xf7: { /* NOT r16; register form. */
            uint8_t modrm;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if (((modrm & 0xf8U) != 0xd0U))
                return BM_STATUS_UNSUPPORTED;
            state->registers[modrm & 7U] = (uint16_t) ~state->registers[modrm & 7U];
            return BM_STATUS_OK;
        }
        case 0xf6: { /* TEST/NOT r/m8 subset. */
            uint8_t modrm;
            unsigned int operation;
            uint8_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if ((operation != 0U) && (operation != 2U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if ((status == BM_STATUS_OK) && (operation == 0U)) {
                uint8_t immediate = 0;
                status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint8_t) (value & immediate), 8);
            } else if (status == BM_STATUS_OK) {
                status = write_operand_byte(state, &operand, (uint8_t) ~value);
            }
            return status;
        }
        case 0xc7: { /* MOV r/m16,imm16. */
            uint8_t modrm;
            uint16_t immediate = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if (((modrm >> 3U) & 7U) != 0U)
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = fetch_word(state, &immediate);
            if (status == BM_STATUS_OK)
                status = write_operand_word(state, &operand, immediate);
            return status;
        }
        case 0xe9: { /* JMP rel16 */
            uint16_t displacement;
            status = fetch_word(state, &displacement);
            if (status == BM_STATUS_OK)
                state->ip = (uint16_t) (state->ip + (int16_t) displacement);
            return status;
        }
        case 0xeb: { /* JMP rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if (status == BM_STATUS_OK)
                state->ip = (uint16_t) (state->ip + (int8_t) displacement);
            return status;
        }
        case 0xe2: { /* LOOP rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if (status == BM_STATUS_OK) {
                state->registers[REG_CX] = (uint16_t) (state->registers[REG_CX] - 1U);
                if (state->registers[REG_CX] != 0)
                    state->ip = (uint16_t) (state->ip + (int8_t) displacement);
            }
            return status;
        }
        case 0xc3: { /* RET near */
            uint16_t destination;
            status = pop_word(state, &destination);
            if (status == BM_STATUS_OK)
                state->ip = destination;
            return status;
        }
        case 0xa2: { /* MOV moffs8,AL */
            uint16_t offset;
            status = fetch_word(state, &offset);
            if (status != BM_STATUS_OK)
                return status;
            return write_byte(state,
                              state->segments[(segment_override >= 0) ?
                                              (unsigned int) segment_override : 3U],
                              offset,
                              (uint8_t) state->registers[REG_AX]);
        }
        case 0xe4: { /* IN AL,imm8 */
            uint8_t port = 0;
            uint8_t value = 0;
            status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = io_read_byte(state, port, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] =
                    (uint16_t) ((state->registers[REG_AX] & 0xff00U) | value);
            return status;
        }
        case 0xe5: { /* IN AX,imm8 */
            uint8_t port = 0;
            uint16_t value = 0;
            status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = io_read_word(state, port, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = value;
            return status;
        }
        case 0xe6: { /* OUT imm8,AL */
            uint8_t port;
            status = fetch_byte(state, &port);
            if (status != BM_STATUS_OK)
                return status;
            return io_write_byte(state, port, (uint8_t) state->registers[REG_AX]);
        }
        case 0xe7: { /* OUT imm8,AX */
            uint8_t port;
            status = fetch_byte(state, &port);
            if (status != BM_STATUS_OK)
                return status;
            return io_write_word(state, port, state->registers[REG_AX]);
        }
        case 0xec: { /* IN AL,DX */
            uint8_t value = 0;
            status = io_read_byte(state, state->registers[REG_DX], &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] =
                    (uint16_t) ((state->registers[REG_AX] & 0xff00U) | value);
            return status;
        }
        case 0xed: { /* IN AX,DX */
            uint16_t value = 0;
            status = io_read_word(state, state->registers[REG_DX], &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = value;
            return status;
        }
        case 0xee: /* OUT DX,AL */
            return io_write_byte(state, state->registers[REG_DX],
                                 (uint8_t) state->registers[REG_AX]);
        case 0xef: /* OUT DX,AX */
            return io_write_word(state, state->registers[REG_DX], state->registers[REG_AX]);
        case 0xfa: /* CLI */
            state->flags &= (uint16_t) ~FLAG_IF;
            return BM_STATUS_OK;
        case 0xfb: /* STI */
            state->flags |= FLAG_IF;
            return BM_STATUS_OK;
        case 0xfc: /* CLD */
            state->flags &= (uint16_t) ~FLAG_DF;
            return BM_STATUS_OK;
        case 0xf4: /* HLT */
            state->halted = 1;
            return BM_STATUS_OK;
        default:
            return BM_STATUS_UNSUPPORTED;
    }
}

static bm_status_t
cpu_run(void *context, bm_tick_t budget, bm_tick_t *consumed)
{
    bm_808x_state_t *state = context;
    bm_status_t status;

    if (consumed == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *consumed = 0;
    if (state->interrupt_asserted && ((state->flags & FLAG_IF) != 0)) {
        status = service_interrupt(state);
        if (status != BM_STATUS_OK)
            return status;
        ++*consumed;
    }
    if (*consumed >= budget)
        return BM_STATUS_OK;
    if (state->halted)
        return BM_STATUS_IDLE;
    while (*consumed < budget) {
        status = execute_one(state);
        if (status != BM_STATUS_OK)
            return status;
        ++*consumed;
        if (state->halted)
            return BM_STATUS_IDLE;
    }
    return BM_STATUS_OK;
}

static bm_status_t
cpu_signal(void *context, uint32_t line, int asserted)
{
    bm_808x_state_t *state = context;
    if (line != 0)
        return BM_STATUS_UNSUPPORTED;
    state->interrupt_asserted = !!asserted;
    if (asserted && ((state->flags & FLAG_IF) != 0))
        state->halted = 0;
    return BM_STATUS_OK;
}

static bm_status_t
cpu_inspect(const void *context, const char *name, uint64_t *value)
{
    const bm_808x_state_t *state = context;
    if ((name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "ax") == 0)
        *value = state->registers[REG_AX];
    else if (strcmp(name, "bx") == 0)
        *value = state->registers[REG_BX];
    else if (strcmp(name, "cx") == 0)
        *value = state->registers[REG_CX];
    else if (strcmp(name, "cs") == 0)
        *value = state->segments[1];
    else if (strcmp(name, "ds") == 0)
        *value = state->segments[3];
    else if (strcmp(name, "dx") == 0)
        *value = state->registers[REG_DX];
    else if (strcmp(name, "es") == 0)
        *value = state->segments[0];
    else if (strcmp(name, "bp") == 0)
        *value = state->registers[REG_BP];
    else if (strcmp(name, "si") == 0)
        *value = state->registers[REG_SI];
    else if (strcmp(name, "di") == 0)
        *value = state->registers[REG_DI];
    else if (strcmp(name, "ss") == 0)
        *value = state->segments[2];
    else if (strcmp(name, "sp") == 0)
        *value = state->registers[REG_SP];
    else if (strcmp(name, "ip") == 0)
        *value = state->ip;
    else if (strcmp(name, "flags") == 0)
        *value = state->flags;
    else if (strcmp(name, "halted") == 0)
        *value = (uint64_t) state->halted;
    else if (strcmp(name, "last_fetch") == 0)
        *value = state->last_fetch;
    else if (strcmp(name, "last_opcode") == 0)
        *value = state->last_opcode;
    else if (strcmp(name, "frequency_hz") == 0)
        *value = state->frequency_hz;
    else
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static void
cpu_destroy(void *context)
{
    bm_808x_state_t *state = context;
    state->host.release(state->host.context, state);
}

bm_status_t
bm_808x_create(const bm_host_services_t *host,
               const bm_808x_config_t *config,
               bm_cpu_t *out_cpu)
{
    bm_808x_state_t *state;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (config == NULL) ||
        (out_cpu == NULL) || (config->bus == NULL) ||
        (config->model != BM_808X_NEC_V30) || (config->frequency_hz == 0))
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
    state->trace = config->trace;
    state->trace_context = config->trace_context;
    state->interrupt_ack = config->interrupt_ack;
    state->interrupt_context = config->interrupt_context;
    *out_cpu = (bm_cpu_t) {
        "nec-v30-bring-up",
        state,
        { cpu_reset, cpu_run, cpu_signal, cpu_inspect, cpu_destroy }
    };
    return BM_STATUS_OK;
}
