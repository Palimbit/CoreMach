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
    uint8_t last_effective_opcode;
    uint64_t last_instruction_bytes;
    uint8_t last_instruction_length;
    int halted;
    int interrupt_asserted;
    uint8_t interrupt_inhibit;
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
    if (status == BM_STATUS_OK) {
        if (state->last_instruction_length < 8U)
            state->last_instruction_bytes |=
                (uint64_t) *value << (state->last_instruction_length * 8U);
        if (state->last_instruction_length != UINT8_MAX)
            ++state->last_instruction_length;
        ++state->ip;
    }
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
enter_interrupt(bm_808x_state_t *state, uint8_t vector)
{
    uint16_t new_ip = 0;
    uint16_t new_cs = 0;
    bm_status_t status;

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

static bm_status_t
service_interrupt_at(bm_808x_state_t *state, uint16_t return_ip)
{
    uint8_t vector;
    bm_status_t status;

    if (state->interrupt_ack == NULL)
        return BM_STATUS_INVALID_STATE;
    status = state->interrupt_ack(state->interrupt_context, &vector);
    if (status != BM_STATUS_OK)
        return status;
    state->ip = return_ip;
    return enter_interrupt(state, vector);
}

static bm_status_t
service_interrupt(bm_808x_state_t *state)
{
    return service_interrupt_at(state, state->ip);
}

static int
maskable_interrupt_ready(const bm_808x_state_t *state)
{
    return state->interrupt_asserted &&
           ((state->flags & FLAG_IF) != 0U) &&
           (state->interrupt_inhibit == 0U);
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

static int32_t
signed_byte(uint8_t value)
{
    return (value & 0x80U) != 0U ? (int32_t) value - 0x100L : value;
}

static int32_t
signed_word(uint16_t value)
{
    return (value & 0x8000U) != 0U ? (int32_t) value - 0x10000L : value;
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

static uint16_t
adc16(bm_808x_state_t *state, uint16_t left, uint16_t right)
{
    uint32_t carry = (state->flags & FLAG_CF) != 0U ? 1U : 0U;
    uint32_t wide = (uint32_t) left + right + carry;
    int32_t signed_wide = (int32_t) (int16_t) left +
                          (int32_t) (int16_t) right + (int32_t) carry;
    uint16_t result = (uint16_t) wide;
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF |
                                 FLAG_SF | FLAG_OF);
    if (wide > 0xffffU)
        state->flags |= FLAG_CF;
    if (((left & 0x0fU) + (right & 0x0fU) + carry) > 0x0fU)
        state->flags |= FLAG_AF;
    if (result == 0U)
        state->flags |= FLAG_ZF;
    if ((result & 0x8000U) != 0U)
        state->flags |= FLAG_SF;
    if (even_parity((uint8_t) result))
        state->flags |= FLAG_PF;
    if ((signed_wide < -32768) || (signed_wide > 32767))
        state->flags |= FLAG_OF;
    return result;
}

static uint8_t
adc8(bm_808x_state_t *state, uint8_t left, uint8_t right)
{
    uint16_t carry = (state->flags & FLAG_CF) != 0U ? 1U : 0U;
    uint16_t wide = (uint16_t) left + right + carry;
    int16_t signed_wide = (int16_t) (int8_t) left +
                          (int16_t) (int8_t) right + (int16_t) carry;
    uint8_t result = (uint8_t) wide;
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF |
                                 FLAG_SF | FLAG_OF);
    if (wide > 0xffU)
        state->flags |= FLAG_CF;
    if (((left & 0x0fU) + (right & 0x0fU) + carry) > 0x0fU)
        state->flags |= FLAG_AF;
    if (result == 0U)
        state->flags |= FLAG_ZF;
    if ((result & 0x80U) != 0U)
        state->flags |= FLAG_SF;
    if (even_parity(result))
        state->flags |= FLAG_PF;
    if ((signed_wide < -128) || (signed_wide > 127))
        state->flags |= FLAG_OF;
    return result;
}

static uint16_t
sbb16(bm_808x_state_t *state, uint16_t left, uint16_t right)
{
    uint32_t borrow = (state->flags & FLAG_CF) != 0U ? 1U : 0U;
    uint32_t subtrahend = (uint32_t) right + borrow;
    int32_t signed_wide = (int32_t) (int16_t) left -
                          (int32_t) (int16_t) right - (int32_t) borrow;
    uint16_t result = (uint16_t) ((uint32_t) left - subtrahend);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF |
                                 FLAG_SF | FLAG_OF);
    if ((uint32_t) left < subtrahend)
        state->flags |= FLAG_CF;
    if ((left & 0x0fU) < ((right & 0x0fU) + borrow))
        state->flags |= FLAG_AF;
    if (result == 0U)
        state->flags |= FLAG_ZF;
    if ((result & 0x8000U) != 0U)
        state->flags |= FLAG_SF;
    if (even_parity((uint8_t) result))
        state->flags |= FLAG_PF;
    if ((signed_wide < -32768) || (signed_wide > 32767))
        state->flags |= FLAG_OF;
    return result;
}

static uint8_t
sbb8(bm_808x_state_t *state, uint8_t left, uint8_t right)
{
    uint16_t borrow = (state->flags & FLAG_CF) != 0U ? 1U : 0U;
    uint16_t subtrahend = (uint16_t) right + borrow;
    int16_t signed_wide = (int16_t) (int8_t) left -
                          (int16_t) (int8_t) right - (int16_t) borrow;
    uint8_t result = (uint8_t) ((uint16_t) left - subtrahend);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF |
                                 FLAG_SF | FLAG_OF);
    if ((uint16_t) left < subtrahend)
        state->flags |= FLAG_CF;
    if ((left & 0x0fU) < ((right & 0x0fU) + borrow))
        state->flags |= FLAG_AF;
    if (result == 0U)
        state->flags |= FLAG_ZF;
    if ((result & 0x80U) != 0U)
        state->flags |= FLAG_SF;
    if (even_parity(result))
        state->flags |= FLAG_PF;
    if ((signed_wide < -128) || (signed_wide > 127))
        state->flags |= FLAG_OF;
    return result;
}

static void
decimal_adjust_add(bm_808x_state_t *state)
{
    uint8_t value = get_register_byte(state, 0U);
    int old_auxiliary = (state->flags & FLAG_AF) != 0U;
    int auxiliary = ((value & 0x0fU) > 9U) ||
                    old_auxiliary;
    int carry = (state->flags & FLAG_CF) != 0U ||
                value > (old_auxiliary ? 0x9fU : 0x99U);

    if (auxiliary)
        value = (uint8_t) (value + 0x06U);
    if (carry)
        value = (uint8_t) (value + 0x60U);
    set_register_byte(state, 0U, value);
    set_logic_flags(state, value, 8U);
    if (auxiliary)
        state->flags |= FLAG_AF;
    if (carry)
        state->flags |= FLAG_CF;
}

static void
decimal_adjust_subtract(bm_808x_state_t *state)
{
    uint8_t value = get_register_byte(state, 0U);
    int old_auxiliary = (state->flags & FLAG_AF) != 0U;
    int auxiliary = ((value & 0x0fU) > 9U) ||
                    old_auxiliary;
    int carry = (state->flags & FLAG_CF) != 0U ||
                value > (old_auxiliary ? 0x9fU : 0x99U);

    if (auxiliary)
        value = (uint8_t) (value - 0x06U);
    if (carry)
        value = (uint8_t) (value - 0x60U);
    set_register_byte(state, 0U, value);
    set_logic_flags(state, value, 8U);
    if (auxiliary)
        state->flags |= FLAG_AF;
    if (carry)
        state->flags |= FLAG_CF;
}

static void
ascii_adjust(bm_808x_state_t *state, int subtract)
{
    uint8_t value = get_register_byte(state, 0U);
    uint8_t high = get_register_byte(state, 4U);
    int adjust = ((value & 0x0fU) > 9U) ||
                 ((state->flags & FLAG_AF) != 0U);

    state->flags &= (uint16_t) ~(FLAG_AF | FLAG_CF);
    if (adjust) {
        value = subtract ? (uint8_t) (value - 6U) :
                           (uint8_t) (value + 6U);
        high = subtract ? (uint8_t) (high - 1U) :
                          (uint8_t) (high + 1U);
        set_register_byte(state, 4U, high);
        state->flags |= FLAG_AF | FLAG_CF;
    }
    set_register_byte(state, 0U, (uint8_t) (value & 0x0fU));
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

static uint8_t
rotate_shift8(bm_808x_state_t *state, uint8_t value,
              unsigned int operation, uint8_t count)
{
    uint8_t original = value;
    uint16_t original_flags = state->flags;
    unsigned int index;
    if (operation == 6U)
        operation = 4U; /* Undocumented SAL alias on the V30/8086 family. */
    for (index = 0; index < count; ++index) {
        unsigned int old_carry = (state->flags & FLAG_CF) != 0U;
        unsigned int carry;
        switch (operation) {
            case 0: /* ROL */
                carry = value >> 7U;
                value = (uint8_t) ((value << 1U) | carry);
                break;
            case 1: /* ROR */
                carry = value & 1U;
                value = (uint8_t) ((value >> 1U) | (carry << 7U));
                break;
            case 2: /* RCL */
                carry = value >> 7U;
                value = (uint8_t) ((value << 1U) | old_carry);
                break;
            case 3: /* RCR */
                carry = value & 1U;
                value = (uint8_t) ((value >> 1U) | (old_carry << 7U));
                break;
            case 4: /* SHL/SAL */
                carry = value >> 7U;
                value = (uint8_t) (value << 1U);
                break;
            case 5: /* SHR */
                carry = value & 1U;
                value = (uint8_t) (value >> 1U);
                break;
            default: /* SAR */
                carry = value & 1U;
                value = (uint8_t) ((value >> 1U) | (value & 0x80U));
                break;
        }
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                   (carry ? FLAG_CF : 0U));
    }
    if (operation >= 4U) {
        uint16_t carry = state->flags & FLAG_CF;
        uint16_t preserved = original_flags & (FLAG_AF | FLAG_OF);
        set_logic_flags(state, value, 8U);
        state->flags = (uint16_t) ((state->flags & ~(FLAG_AF | FLAG_OF)) |
                                  preserved | carry);
    }
    if (count == 1U) {
        state->flags &= (uint16_t) ~FLAG_OF;
        if ((((operation == 0U) || (operation == 2U) || (operation == 4U)) &&
             ((((value >> 7U) & 1U) ^ ((state->flags & FLAG_CF) != 0U)) != 0U)) ||
            (((operation == 1U) || (operation == 3U)) &&
             (((value >> 7U) ^ (value >> 6U)) & 1U)) ||
            ((operation == 5U) && ((original & 0x80U) != 0U)))
            state->flags |= FLAG_OF;
    }
    return value;
}

static uint16_t
rotate_shift16(bm_808x_state_t *state, uint16_t value,
               unsigned int operation, uint8_t count)
{
    uint16_t original = value;
    uint16_t original_flags = state->flags;
    unsigned int index;
    if (operation == 6U)
        operation = 4U;
    for (index = 0; index < count; ++index) {
        unsigned int old_carry = (state->flags & FLAG_CF) != 0U;
        unsigned int carry;
        switch (operation) {
            case 0:
                carry = value >> 15U;
                value = (uint16_t) ((value << 1U) | carry);
                break;
            case 1:
                carry = value & 1U;
                value = (uint16_t) ((value >> 1U) | (carry << 15U));
                break;
            case 2:
                carry = value >> 15U;
                value = (uint16_t) ((value << 1U) | old_carry);
                break;
            case 3:
                carry = value & 1U;
                value = (uint16_t) ((value >> 1U) | (old_carry << 15U));
                break;
            case 4:
                carry = value >> 15U;
                value = (uint16_t) (value << 1U);
                break;
            case 5:
                carry = value & 1U;
                value = (uint16_t) (value >> 1U);
                break;
            default:
                carry = value & 1U;
                value = (uint16_t) ((value >> 1U) | (value & 0x8000U));
                break;
        }
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                   (carry ? FLAG_CF : 0U));
    }
    if (operation >= 4U) {
        uint16_t carry = state->flags & FLAG_CF;
        uint16_t preserved = original_flags & (FLAG_AF | FLAG_OF);
        set_logic_flags(state, value, 16U);
        state->flags = (uint16_t) ((state->flags & ~(FLAG_AF | FLAG_OF)) |
                                  preserved | carry);
    }
    if (count == 1U) {
        state->flags &= (uint16_t) ~FLAG_OF;
        if ((((operation == 0U) || (operation == 2U) || (operation == 4U)) &&
             ((((value >> 15U) & 1U) ^ ((state->flags & FLAG_CF) != 0U)) != 0U)) ||
            (((operation == 1U) || (operation == 3U)) &&
             (((value >> 15U) ^ (value >> 14U)) & 1U)) ||
            ((operation == 5U) && ((original & 0x8000U) != 0U)))
            state->flags |= FLAG_OF;
    }
    return value;
}

static bm_status_t
execute_string(bm_808x_state_t *state, uint8_t opcode, int repeat_mode,
               int segment_override, uint16_t repeat_ip)
{
    bm_status_t status;
    unsigned int width = (opcode & 1U) != 0U ? 2U : 1U;
    unsigned int source_segment = (segment_override >= 0) ?
                                  (unsigned int) segment_override : 3U;

    if ((opcode < 0xa4U) || (opcode > 0xa7U)) {
        if ((opcode < 0xaaU) || (opcode > 0xafU))
            return BM_STATUS_UNSUPPORTED;
    }
    while ((repeat_mode == 0) || (state->registers[REG_CX] != 0)) {
        uint16_t value = 0;
        if ((opcode == 0xa4U) || (opcode == 0xa5U)) { /* MOVS */
            if (width == 1U) {
                uint8_t byte = 0;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &byte);
                if (status == BM_STATUS_OK)
                    status = write_byte(state, state->segments[0],
                                        state->registers[REG_DI], byte);
            } else {
                status = read_word(state, state->segments[source_segment],
                                   state->registers[REG_SI], &value);
                if (status == BM_STATUS_OK)
                    status = write_word(state, state->segments[0],
                                        state->registers[REG_DI], value);
            }
        } else if ((opcode == 0xa6U) || (opcode == 0xa7U)) { /* CMPS */
            if (width == 1U) {
                uint8_t source = 0;
                uint8_t destination = 0;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &source);
                if (status == BM_STATUS_OK)
                    status = read_byte(state, state->segments[0],
                                       state->registers[REG_DI], BM_BUS_READ,
                                       &destination);
                if (status == BM_STATUS_OK)
                    compare8(state, source, destination);
            } else {
                uint16_t destination = 0;
                status = read_word(state, state->segments[source_segment],
                                   state->registers[REG_SI], &value);
                if (status == BM_STATUS_OK)
                    status = read_word(state, state->segments[0],
                                       state->registers[REG_DI], &destination);
                if (status == BM_STATUS_OK)
                    compare16(state, value, destination);
            }
        } else if ((opcode == 0xaaU) || (opcode == 0xabU)) { /* STOS */
            if (width == 1U)
                status = write_byte(state, state->segments[0],
                                    state->registers[REG_DI],
                                    (uint8_t) state->registers[REG_AX]);
            else
                status = write_word(state, state->segments[0],
                                    state->registers[REG_DI], state->registers[REG_AX]);
        } else if ((opcode == 0xacU) || (opcode == 0xadU)) { /* LODS */
            if (width == 1U) {
                uint8_t byte = 0;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &byte);
                if (status == BM_STATUS_OK)
                    set_register_byte(state, 0U, byte);
            } else {
                status = read_word(state, state->segments[source_segment],
                                   state->registers[REG_SI], &state->registers[REG_AX]);
            }
        } else { /* SCAS */
            if (width == 1U) {
                uint8_t byte = 0;
                status = read_byte(state, state->segments[0],
                                   state->registers[REG_DI], BM_BUS_READ, &byte);
                if (status == BM_STATUS_OK)
                    compare8(state, (uint8_t) state->registers[REG_AX], byte);
            } else {
                status = read_word(state, state->segments[0],
                                   state->registers[REG_DI], &value);
                if (status == BM_STATUS_OK)
                    compare16(state, state->registers[REG_AX], value);
            }
        }
        if (status != BM_STATUS_OK)
            return status;
        if (((opcode >= 0xa4U) && (opcode <= 0xa7U)) ||
            (opcode == 0xacU) || (opcode == 0xadU))
            state->registers[REG_SI] = (uint16_t) (state->registers[REG_SI] +
                (((state->flags & FLAG_DF) != 0) ? -(int) width : (int) width));
        if (((opcode >= 0xa4U) && (opcode <= 0xa7U)) ||
            (opcode == 0xaaU) || (opcode == 0xabU) ||
            (opcode == 0xaeU) || (opcode == 0xafU))
            state->registers[REG_DI] = (uint16_t) (state->registers[REG_DI] +
                (((state->flags & FLAG_DF) != 0) ? -(int) width : (int) width));
        if (repeat_mode == 0)
            break;
        state->registers[REG_CX] = (uint16_t) (state->registers[REG_CX] - 1U);
        if ((((opcode == 0xa6U) || (opcode == 0xa7U)) ||
             ((opcode == 0xaeU) || (opcode == 0xafU))) &&
            (((repeat_mode == 1) && ((state->flags & FLAG_ZF) == 0)) ||
             ((repeat_mode == 2) && ((state->flags & FLAG_ZF) != 0))))
            break;
        if ((state->registers[REG_CX] != 0U) &&
            maskable_interrupt_ready(state))
            return service_interrupt_at(state, repeat_ip);
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
execute_io_string(bm_808x_state_t *state, uint8_t opcode, int repeat_mode,
                  int segment_override, uint16_t repeat_ip)
{
    unsigned int width = (opcode & 1U) != 0U ? 2U : 1U;
    unsigned int source_segment = (segment_override >= 0) ?
                                  (unsigned int) segment_override : 3U;

    while ((repeat_mode == 0) || (state->registers[REG_CX] != 0U)) {
        bm_status_t status;
        uint16_t value = 0U;

        if ((opcode == 0x6cU) || (opcode == 0x6dU)) { /* INSB/INSW. */
            if (width == 1U) {
                uint8_t byte = 0U;
                status = io_read_byte(state, state->registers[REG_DX], &byte);
                if (status == BM_STATUS_OK)
                    status = write_byte(state, state->segments[0],
                                        state->registers[REG_DI], byte);
            } else {
                status = io_read_word(state, state->registers[REG_DX], &value);
                if (status == BM_STATUS_OK)
                    status = write_word(state, state->segments[0],
                                        state->registers[REG_DI], value);
            }
        } else { /* OUTSB/OUTSW. */
            if (width == 1U) {
                uint8_t byte = 0U;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &byte);
                if (status == BM_STATUS_OK)
                    status = io_write_byte(state, state->registers[REG_DX], byte);
            } else {
                status = read_word(state, state->segments[source_segment],
                                   state->registers[REG_SI], &value);
                if (status == BM_STATUS_OK)
                    status = io_write_word(state, state->registers[REG_DX], value);
            }
        }
        if (status != BM_STATUS_OK)
            return status;
        if ((opcode == 0x6cU) || (opcode == 0x6dU))
            state->registers[REG_DI] = (uint16_t) (state->registers[REG_DI] +
                (((state->flags & FLAG_DF) != 0U) ? -(int) width : (int) width));
        else
            state->registers[REG_SI] = (uint16_t) (state->registers[REG_SI] +
                (((state->flags & FLAG_DF) != 0U) ? -(int) width : (int) width));
        if (repeat_mode == 0)
            break;
        state->registers[REG_CX] = (uint16_t) (state->registers[REG_CX] - 1U);
        if ((state->registers[REG_CX] != 0U) &&
            maskable_interrupt_ready(state))
            return service_interrupt_at(state, repeat_ip);
    }
    return BM_STATUS_OK;
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
    state->last_effective_opcode = 0;
    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    state->halted = 0;
    state->interrupt_asserted = 0;
    state->interrupt_inhibit = 0U;
    return BM_STATUS_OK;
}

static bm_status_t
execute_one(bm_808x_state_t *state)
{
    uint16_t instruction_ip = state->ip;
    uint8_t opcode;
    uint8_t first_opcode;
    uint8_t prefix_count = 0U;
    int segment_override = -1;
    int repeat = 0;
    bm_status_t status;
    uint16_t repeat_ip;

    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    status = fetch_byte(state, &opcode);

    if (status != BM_STATUS_OK)
        return status;
    state->last_fetch = physical_address(state->segments[1], instruction_ip);
    state->last_opcode = opcode;
    first_opcode = opcode;

    for (;;) {
        int prefix_segment = -1;
        switch (opcode) {
            case 0x26: prefix_segment = 0; break; /* ES: */
            case 0x2e: prefix_segment = 1; break; /* CS: */
            case 0x36: prefix_segment = 2; break; /* SS: */
            case 0x3e: prefix_segment = 3; break; /* DS: */
            case 0xf2: repeat = 2; break;          /* REPNE */
            case 0xf3: repeat = 1; break;          /* REP/REPE */
            default: break;
        }
        if ((prefix_segment < 0) && (opcode != 0xf2U) && (opcode != 0xf3U))
            break;
        if (prefix_segment >= 0)
            segment_override = prefix_segment;
        ++prefix_count;
        status = fetch_byte(state, &opcode);
        if (status != BM_STATUS_OK)
            return status;
    }

    state->last_effective_opcode = opcode;
    repeat_ip = (uint16_t) (instruction_ip +
                 (prefix_count > 3U ? prefix_count - 3U : 0U));
    if (state->trace != NULL) {
        bm_808x_trace_t trace = {
            .cs = state->segments[1],
            .ip = instruction_ip,
            .physical_address = state->last_fetch,
            .opcode = first_opcode,
            .effective_opcode = opcode,
            .prefix_count = prefix_count
        };
        state->trace(state->trace_context, &trace);
    }

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
    if ((opcode >= 0x48U) && (opcode <= 0x4fU)) {
        unsigned int index = opcode - 0x48U;
        uint16_t carry = state->flags & FLAG_CF;
        uint16_t result = (uint16_t) (state->registers[index] - 1U);
        compare16(state, state->registers[index], 1U);
        state->registers[index] = result;
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
        return BM_STATUS_OK;
    }
    if ((opcode >= 0x50U) && (opcode <= 0x57U))
        return push_word(state, state->registers[opcode - 0x50U]);
    if ((opcode >= 0x58U) && (opcode <= 0x5fU)) {
        uint16_t value;
        status = pop_word(state, &value);
        if (status == BM_STATUS_OK)
            state->registers[opcode - 0x58U] = value;
        return status;
    }
    if ((opcode >= 0x91U) && (opcode <= 0x97U)) {
        unsigned int index = opcode - 0x90U;
        uint16_t value = state->registers[REG_AX];
        state->registers[REG_AX] = state->registers[index];
        state->registers[index] = value;
        return BM_STATUS_OK;
    }

    switch (opcode) {
        case 0x60: { /* PUSHA. */
            uint16_t original_sp = state->registers[REG_SP];
            unsigned int index;
            static const unsigned int order[] = {
                REG_AX, REG_CX, REG_DX, REG_BX
            };
            for (index = 0U; index < sizeof(order) / sizeof(order[0]); ++index) {
                status = push_word(state, state->registers[order[index]]);
                if (status != BM_STATUS_OK)
                    return status;
            }
            status = push_word(state, original_sp);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->registers[REG_BP]);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->registers[REG_SI]);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->registers[REG_DI]);
            return status;
        }
        case 0x61: { /* POPA. */
            uint16_t value = 0U;
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_DI] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_SI] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_BP] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value); /* Discard saved SP. */
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_BX] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_DX] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_CX] = value;
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = value;
            return status;
        }
        case 0x62: { /* CHKIND/BOUND r16,m16:m16. */
            uint8_t modrm;
            uint16_t lower = 0U;
            uint16_t upper = 0U;
            int32_t index;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override,
                                           &operand);
            if ((status == BM_STATUS_OK) && operand.is_register)
                return BM_STATUS_UNSUPPORTED;
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment, operand.offset,
                                   &lower);
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment,
                                   (uint16_t) (operand.offset + 2U), &upper);
            if (status != BM_STATUS_OK)
                return status;
            index = signed_word(state->registers[(modrm >> 3U) & 7U]);
            if ((index < signed_word(lower)) || (index > signed_word(upper)))
                return enter_interrupt(state, 5U);
            return BM_STATUS_OK;
        }
        case 0x63:
            /* Undefined in the NEC native-mode instruction map. */
            return BM_STATUS_UNSUPPORTED;
        case 0x68: { /* PUSH imm16. */
            uint16_t immediate = 0U;
            status = fetch_word(state, &immediate);
            return status == BM_STATUS_OK ? push_word(state, immediate) : status;
        }
        case 0x6a: { /* PUSH sign-extended imm8. */
            uint8_t immediate = 0U;
            status = fetch_byte(state, &immediate);
            return status == BM_STATUS_OK ?
                push_word(state, (uint16_t) signed_byte(immediate)) : status;
        }
        case 0x69: /* IMUL r16,r/m16,imm16. */
        case 0x6b: { /* IMUL r16,r/m16,sign-extended imm8. */
            uint8_t modrm;
            uint16_t source = 0U;
            int32_t immediate;
            int32_t result;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override,
                                           &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &source);
            if (opcode == 0x69U) {
                uint16_t word = 0U;
                if (status == BM_STATUS_OK)
                    status = fetch_word(state, &word);
                immediate = signed_word(word);
            } else {
                uint8_t byte = 0U;
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &byte);
                immediate = signed_byte(byte);
            }
            if (status != BM_STATUS_OK)
                return status;
            result = signed_word(source) * immediate;
            state->registers[(modrm >> 3U) & 7U] = (uint16_t) result;
            state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF);
            if ((result < -32768L) || (result > 32767L))
                state->flags |= FLAG_CF | FLAG_OF;
            return BM_STATUS_OK;
        }
        case 0x6c: /* INSB. */
        case 0x6d: /* INSW. */
        case 0x6e: /* OUTSB. */
        case 0x6f: /* OUTSW. */
            return execute_io_string(state, opcode, repeat, segment_override,
                                     repeat_ip);
        case 0x0f: { /* NEC V30 bit operations. */
            uint8_t extension;
            uint8_t modrm;
            uint8_t bit;
            uint16_t value = 0;
            uint16_t mask;
            unsigned int operation;
            unsigned int width;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &extension);
            if (status != BM_STATUS_OK)
                return status;
            if ((extension < 0x10U) || (extension > 0x1fU))
                return BM_STATUS_UNSUPPORTED;
            operation = (extension >> 1U) & 3U;
            width = (extension & 1U) != 0U ? 16U : 8U;
            status = fetch_byte(state, &modrm);
            if ((status == BM_STATUS_OK) && ((extension & 8U) != 0U))
                status = fetch_byte(state, &bit);
            else
                bit = get_register_byte(state, 1U);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK) {
                if (width == 8U) {
                    uint8_t byte = 0;
                    status = read_operand_byte(state, &operand, &byte);
                    value = byte;
                } else {
                    status = read_operand_word(state, &operand, &value);
                }
            }
            if (status != BM_STATUS_OK)
                return status;
            bit &= (uint8_t) (width - 1U);
            mask = (uint16_t) (1U << bit);
            if (operation == 0U) { /* TEST1 */
                state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF | FLAG_ZF);
                if ((value & mask) == 0U)
                    state->flags |= FLAG_ZF;
                return BM_STATUS_OK;
            }
            if (operation == 1U) /* CLR1 */
                value &= (uint16_t) ~mask;
            else if (operation == 2U) /* SET1 */
                value |= mask;
            else /* NOT1 */
                value ^= mask;
            if (width == 8U)
                return write_operand_byte(state, &operand, (uint8_t) value);
            return write_operand_word(state, &operand, value);
        }
        case 0x05: /* ADD AX,imm16. */
        case 0x0d: /* OR AX,imm16. */
        case 0x15: /* ADC AX,imm16. */
        case 0x1d: /* SBB AX,imm16. */
        case 0x25: /* AND AX,imm16. */
        case 0x2d: /* SUB AX,imm16. */
        case 0x35: /* XOR AX,imm16. */
        case 0x3d: { /* CMP AX,imm16. */
            uint16_t immediate = 0;
            uint16_t result;
            status = fetch_word(state, &immediate);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x05U)
                result = add16(state, state->registers[REG_AX], immediate);
            else if (opcode == 0x15U)
                result = adc16(state, state->registers[REG_AX], immediate);
            else if (opcode == 0x1dU)
                result = sbb16(state, state->registers[REG_AX], immediate);
            else if (opcode == 0x2dU) {
                result = (uint16_t) (state->registers[REG_AX] - immediate);
                compare16(state, state->registers[REG_AX], immediate);
            } else if (opcode == 0x3dU) {
                compare16(state, state->registers[REG_AX], immediate);
                return BM_STATUS_OK;
            } else {
                result = opcode == 0x0dU ?
                    (uint16_t) (state->registers[REG_AX] | immediate) :
                    opcode == 0x25U ?
                    (uint16_t) (state->registers[REG_AX] & immediate) :
                    (uint16_t) (state->registers[REG_AX] ^ immediate);
                set_logic_flags(state, result, 16);
            }
            state->registers[REG_AX] = result;
            return BM_STATUS_OK;
        }
        case 0x06: /* PUSH ES */
            return push_word(state, state->segments[0]);
        case 0x0e: /* PUSH CS */
            return push_word(state, state->segments[1]);
        case 0x16: /* PUSH SS */
            return push_word(state, state->segments[2]);
        case 0x1e: /* PUSH DS */
            return push_word(state, state->segments[3]);
        case 0x07: { /* POP ES */
            uint16_t value;
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[0] = value;
                state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0x1f: { /* POP DS */
            uint16_t value;
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[3] = value;
                state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0x17: { /* POP SS */
            uint16_t value;
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[2] = value;
                state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0x90: /* NOP */
            return BM_STATUS_OK;
        case 0x98: { /* CBW */
            uint16_t value = state->registers[REG_AX] & 0x00ffU;
            state->registers[REG_AX] = (value & 0x0080U) != 0U ?
                                       (uint16_t) (value | 0xff00U) : value;
            return BM_STATUS_OK;
        }
        case 0x99: /* CWD */
            state->registers[REG_DX] =
                (state->registers[REG_AX] & 0x8000U) != 0U ? 0xffffU : 0U;
            return BM_STATUS_OK;
        case 0x27: /* DAA */
            decimal_adjust_add(state);
            return BM_STATUS_OK;
        case 0x2f: /* DAS */
            decimal_adjust_subtract(state);
            return BM_STATUS_OK;
        case 0x37: /* AAA */
            ascii_adjust(state, 0);
            return BM_STATUS_OK;
        case 0x3f: /* AAS */
            ascii_adjust(state, 1);
            return BM_STATUS_OK;
        case 0xd4: { /* AAM imm8 (NEC V20/V30 measured semantics). */
            uint8_t base = 0U;
            uint8_t value;
            status = fetch_byte(state, &base);
            if (status != BM_STATUS_OK)
                return status;
            value = get_register_byte(state, 0U);
            if (base == 0U) {
                /* V20 hardware produces FF:AL instead of interrupt zero. */
                set_register_byte(state, 4U, 0xffU);
            } else {
                set_register_byte(state, 4U, (uint8_t) (value / base));
                set_register_byte(state, 0U, (uint8_t) (value % base));
            }
            set_logic_flags(state, get_register_byte(state, 0U), 8U);
            return BM_STATUS_OK;
        }
        case 0xd5: { /* AAD imm8; NEC V20/V30 always uses decimal base 10. */
            uint8_t encoded_base = 0U;
            uint8_t value;
            status = fetch_byte(state, &encoded_base);
            if (status != BM_STATUS_OK)
                return status;
            (void) encoded_base;
            value = (uint8_t) (get_register_byte(state, 0U) +
                               get_register_byte(state, 4U) * 10U);
            state->registers[REG_AX] = value;
            set_logic_flags(state, value, 8U);
            return BM_STATUS_OK;
        }
        case 0x9e: { /* SAHF */
            uint16_t mask = FLAG_SF | FLAG_ZF | FLAG_AF | FLAG_PF | FLAG_CF;
            uint16_t ah = get_register_byte(state, 4U);
            state->flags = (uint16_t) ((state->flags & ~mask) | (ah & mask) |
                                       0x0002U);
            return BM_STATUS_OK;
        }
        case 0x9f: /* LAHF */
            set_register_byte(state, 4U,
                              (uint8_t) ((state->flags & 0x00d5U) | 0x02U));
            return BM_STATUS_OK;
        case 0x9c: /* PUSHF (NEC V30 reserved-bit image). */
            return push_word(state, (uint16_t) ((state->flags & 0x8fd7U) | 0x7000U));
        case 0x9d: { /* POPF */
            uint16_t value;
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                state->flags = value | 0x0002U;
            return status;
        }
        case 0xa4: /* MOVSB */
        case 0xa5: /* MOVSW */
        case 0xa6: /* CMPSB */
        case 0xa7: /* CMPSW */
        case 0xaa: /* STOSB */
        case 0xab: /* STOSW */
        case 0xac: /* LODSB */
        case 0xad: /* LODSW */
        case 0xae: /* SCASB */
        case 0xaf: /* SCASW */
            return execute_string(state, opcode, repeat, segment_override,
                                  repeat_ip);
        case 0x04: /* ADD AL,imm8 */
        case 0x0c: /* OR AL,imm8 */
        case 0x14: /* ADC AL,imm8 */
        case 0x1c: /* SBB AL,imm8 */
        case 0x24: /* AND AL,imm8 */
        case 0x2c: /* SUB AL,imm8 */
        case 0x34: /* XOR AL,imm8 */
        case 0x3c: { /* CMP AL,imm8 */
            uint8_t immediate;
            uint8_t result;
            status = fetch_byte(state, &immediate);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x04U)
                result = add8(state, get_register_byte(state, 0), immediate);
            else if (opcode == 0x14U)
                result = adc8(state, get_register_byte(state, 0), immediate);
            else if (opcode == 0x1cU)
                result = sbb8(state, get_register_byte(state, 0), immediate);
            else if (opcode == 0x2cU) {
                result = (uint8_t) (get_register_byte(state, 0) - immediate);
                compare8(state, get_register_byte(state, 0), immediate);
            } else if (opcode == 0x3cU) {
                compare8(state, get_register_byte(state, 0), immediate);
                return BM_STATUS_OK;
            } else {
                result = opcode == 0x0cU ?
                    (uint8_t) (get_register_byte(state, 0) | immediate) :
                    opcode == 0x24U ?
                    (uint8_t) (get_register_byte(state, 0) & immediate) :
                    (uint8_t) (get_register_byte(state, 0) ^ immediate);
                set_logic_flags(state, result, 8);
            }
            set_register_byte(state, 0, result);
            return BM_STATUS_OK;
        }
        case 0xa8: /* TEST AL,imm8. */
        case 0xa9: { /* TEST AX,imm16. */
            if (opcode == 0xa8U) {
                uint8_t immediate = 0U;
                status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state,
                                    (uint8_t) (get_register_byte(state, 0) &
                                               immediate),
                                    8);
            } else {
                uint16_t immediate = 0U;
                status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state,
                                    (uint16_t) (state->registers[REG_AX] &
                                                immediate),
                                    16);
            }
            return status;
        }
        case 0x00: /* ADD r/m8,r8 */
        case 0x08: /* OR r/m8,r8 */
        case 0x10: /* ADC r/m8,r8 */
        case 0x18: /* SBB r/m8,r8 */
        case 0x20: /* AND r/m8,r8 */
        case 0x28: /* SUB r/m8,r8 */
        case 0x30: /* XOR r/m8,r8 */
        case 0x38: { /* CMP r/m8,r8 */
            uint8_t modrm;
            uint8_t destination = 0;
            uint8_t source;
            uint8_t result;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &destination);
            if (status != BM_STATUS_OK)
                return status;
            source = get_register_byte(state, (modrm >> 3U) & 7U);
            if (opcode == 0x00U)
                result = add8(state, destination, source);
            else if (opcode == 0x10U)
                result = adc8(state, destination, source);
            else if (opcode == 0x18U)
                result = sbb8(state, destination, source);
            else if (opcode == 0x28U) {
                result = (uint8_t) (destination - source);
                compare8(state, destination, source);
            } else if (opcode == 0x38U) {
                compare8(state, destination, source);
                return BM_STATUS_OK;
            } else {
                result = opcode == 0x08U ? (uint8_t) (destination | source) :
                         opcode == 0x20U ? (uint8_t) (destination & source) :
                                           (uint8_t) (destination ^ source);
                set_logic_flags(state, result, 8);
            }
            return write_operand_byte(state, &operand, result);
        }
        case 0x02: /* ADD r8,r/m8 */
        case 0x0a: /* OR r8,r/m8 */
        case 0x12: /* ADC r8,r/m8 */
        case 0x1a: /* SBB r8,r/m8 */
        case 0x22: /* AND r8,r/m8 */
        case 0x2a: { /* SUB r8,r/m8 */
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
            else if (opcode == 0x12U)
                result = adc8(state, get_register_byte(state, destination), source);
            else if (opcode == 0x1aU)
                result = sbb8(state, get_register_byte(state, destination), source);
            else if (opcode == 0x2aU) {
                result = (uint8_t) (get_register_byte(state, destination) - source);
                compare8(state, get_register_byte(state, destination), source);
            }
            else {
                result = (opcode == 0x0aU) ?
                    (uint8_t) (get_register_byte(state, destination) | source) :
                    (uint8_t) (get_register_byte(state, destination) & source);
                set_logic_flags(state, result, 8);
            }
            set_register_byte(state, destination, result);
            return BM_STATUS_OK;
        }
        case 0x03: /* ADD r16,r/m16. */
        case 0x0b: /* OR r16,r/m16. */
        case 0x13: /* ADC r16,r/m16. */
        case 0x1b: /* SBB r16,r/m16. */
        case 0x23: /* AND r16,r/m16. */
        case 0x2b: /* SUB r16,r/m16. */
        case 0x33: /* XOR r16,r/m16. */
        case 0x3b: { /* CMP r16,r/m16. */
            uint8_t modrm;
            unsigned int destination;
            uint16_t source = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &source);
            if (status != BM_STATUS_OK)
                return status;
            destination = (modrm >> 3U) & 7U;
            if (opcode == 0x3bU)
                compare16(state, state->registers[destination], source);
            else if (opcode == 0x03U)
                state->registers[destination] =
                    add16(state, state->registers[destination], source);
            else if (opcode == 0x13U)
                state->registers[destination] =
                    adc16(state, state->registers[destination], source);
            else if (opcode == 0x1bU)
                state->registers[destination] =
                    sbb16(state, state->registers[destination], source);
            else if (opcode == 0x2bU) {
                uint16_t value = (uint16_t) (state->registers[destination] -
                                              source);
                compare16(state, state->registers[destination], source);
                state->registers[destination] = value;
            }
            else {
                uint16_t value = (opcode == 0x0bU) ?
                    (uint16_t) (state->registers[destination] | source) :
                    opcode == 0x23U ?
                    (uint16_t) (state->registers[destination] & source) :
                    (uint16_t) (state->registers[destination] ^ source);
                state->registers[destination] = value;
                set_logic_flags(state, value, 16);
            }
            return BM_STATUS_OK;
        }
        case 0x8b: { /* MOV r16,r/m16. */
            uint8_t modrm;
            uint16_t source = 0;
            bm_808x_operand_t operand = { 0, 0, 0, 0 };
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &source);
            if (status == BM_STATUS_OK)
                state->registers[(modrm >> 3U) & 7U] = source;
            return status;
        }
        case 0x8d: { /* LEA r16,m. */
            uint8_t modrm;
            bm_808x_operand_t operand = { 0, 0, 0, 0 };
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) && operand.is_register)
                return BM_STATUS_UNSUPPORTED;
            if (status == BM_STATUS_OK)
                state->registers[(modrm >> 3U) & 7U] = operand.offset;
            return status;
        }
        case 0x88: { /* MOV r/m8,r8 */
            uint8_t modrm;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = write_operand_byte(state, &operand,
                                            get_register_byte(state,
                                                              (modrm >> 3U) & 7U));
            return status;
        }
        case 0x01: /* ADD r/m16,r16 */
        case 0x09: /* OR r/m16,r16 */
        case 0x11: /* ADC r/m16,r16 */
        case 0x19: /* SBB r/m16,r16 */
        case 0x21: /* AND r/m16,r16 */
        case 0x29: /* SUB r/m16,r16 */
        case 0x31: /* XOR r/m16,r16 */
        case 0x39: { /* CMP r/m16,r16 */
            uint8_t modrm;
            uint16_t destination = 0;
            uint16_t source;
            uint16_t result;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &destination);
            if (status != BM_STATUS_OK)
                return status;
            source = state->registers[(modrm >> 3U) & 7U];
            if (opcode == 0x39U) {
                compare16(state, destination, source);
                return BM_STATUS_OK;
            }
            if (opcode == 0x01U)
                result = add16(state, destination, source);
            else if (opcode == 0x11U)
                result = adc16(state, destination, source);
            else if (opcode == 0x19U)
                result = sbb16(state, destination, source);
            else if (opcode == 0x29U) {
                result = (uint16_t) (destination - source);
                compare16(state, destination, source);
            } else {
                result = opcode == 0x09U ? (uint16_t) (destination | source) :
                         opcode == 0x21U ? (uint16_t) (destination & source) :
                                           (uint16_t) (destination ^ source);
                set_logic_flags(state, result, 16);
            }
            return write_operand_word(state, &operand, result);
        }
        case 0x89: { /* MOV r/m16,r16 */
            uint8_t modrm;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = write_operand_word(state, &operand,
                                            state->registers[(modrm >> 3U) & 7U]);
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
        case 0x86: /* XCHG r/m8,r8. */
        case 0x87: { /* XCHG r/m16,r16. */
            uint8_t modrm;
            unsigned int register_index;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            register_index = (modrm >> 3U) & 7U;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (opcode == 0x86U) {
                uint8_t value = 0U;
                uint8_t register_value = get_register_byte(state, register_index);
                if (status == BM_STATUS_OK)
                    status = read_operand_byte(state, &operand, &value);
                if (status == BM_STATUS_OK)
                    status = write_operand_byte(state, &operand, register_value);
                if (status == BM_STATUS_OK)
                    set_register_byte(state, register_index, value);
            } else {
                uint16_t value = 0U;
                uint16_t register_value = state->registers[register_index];
                if (status == BM_STATUS_OK)
                    status = read_operand_word(state, &operand, &value);
                if (status == BM_STATUS_OK)
                    status = write_operand_word(state, &operand, register_value);
                if (status == BM_STATUS_OK)
                    state->registers[register_index] = value;
            }
            return status;
        }
        case 0x84: /* TEST r/m8,r8. */
        case 0x85: { /* TEST r/m16,r16. */
            uint8_t modrm;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x84U) {
                uint8_t value = 0;
                status = read_operand_byte(state, &operand, &value);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint8_t)
                        (value & get_register_byte(state, (modrm >> 3U) & 7U)), 8U);
            } else {
                uint16_t value = 0;
                status = read_operand_word(state, &operand, &value);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint16_t)
                        (value & state->registers[(modrm >> 3U) & 7U]), 16U);
            }
            return status;
        }
        case 0x80: /* Immediate arithmetic group; basic r/m8 operations. */
        case 0x82: /* NEC byte-immediate alias of 80h. */
        case 0x81: { /* Immediate arithmetic group; basic r/m16 operations. */
            uint8_t modrm;
            unsigned int operation;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (opcode != 0x81U) {
                uint8_t immediate = 0;
                uint8_t left = 0;
                uint8_t result;
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = read_operand_byte(state, &operand, &left);
                if (status == BM_STATUS_OK) {
                    if (operation == 0U)
                        result = add8(state, left, immediate);
                    else if (operation == 2U)
                        result = adc8(state, left, immediate);
                    else if (operation == 3U)
                        result = sbb8(state, left, immediate);
                    else if (operation == 5U) {
                        result = (uint8_t) (left - immediate);
                        compare8(state, left, immediate);
                    } else if (operation == 7U) {
                        compare8(state, left, immediate);
                        return BM_STATUS_OK;
                    } else {
                        result = operation == 1U ? (uint8_t) (left | immediate) :
                                 operation == 4U ? (uint8_t) (left & immediate) :
                                                   (uint8_t) (left ^ immediate);
                        set_logic_flags(state, result, 8);
                    }
                    status = write_operand_byte(state, &operand, result);
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
                    if (operation == 0U) {
                        result = add16(state, left, immediate);
                        status = write_operand_word(state, &operand, result);
                    } else if (operation == 2U) {
                        result = adc16(state, left, immediate);
                        status = write_operand_word(state, &operand, result);
                    } else if (operation == 3U) {
                        result = sbb16(state, left, immediate);
                        status = write_operand_word(state, &operand, result);
                    } else if (operation == 5U) {
                        result = (uint16_t) (left - immediate);
                        compare16(state, left, immediate);
                        status = write_operand_word(state, &operand, result);
                    } else if (operation == 7U) {
                        compare16(state, left, immediate);
                    } else {
                        result = operation == 1U ? (uint16_t) (left | immediate) :
                                 operation == 4U ? (uint16_t) (left & immediate) :
                                                   (uint16_t) (left ^ immediate);
                        status = write_operand_word(state, &operand, result);
                        if (status == BM_STATUS_OK)
                            set_logic_flags(state, result, 16);
                    }
                }
            }
            return status;
        }
        case 0x83: { /* Sign-extended immediate arithmetic on r/m16. */
            uint8_t modrm;
            uint8_t immediate = 0;
            uint16_t left = 0;
            unsigned int operation;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if ((operation != 0U) && (operation != 1U) &&
                (operation != 2U) && (operation != 3U) &&
                (operation != 4U) && (operation != 5U) &&
                (operation != 6U) && (operation != 7U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &immediate);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &left);
            if (status == BM_STATUS_OK) {
                uint16_t extended = (uint16_t) (int16_t) (int8_t) immediate;
                uint16_t result;
                if (operation == 0U)
                    result = add16(state, left, extended);
                else if (operation == 2U)
                    result = adc16(state, left, extended);
                else if (operation == 3U)
                    result = sbb16(state, left, extended);
                else if (operation == 5U) {
                    result = (uint16_t) (left - extended);
                    compare16(state, left, extended);
                } else if (operation == 7U) {
                    compare16(state, left, extended);
                    return BM_STATUS_OK;
                } else {
                    result = operation == 1U ? (uint16_t) (left | extended) :
                             operation == 4U ? (uint16_t) (left & extended) :
                                               (uint16_t) (left ^ extended);
                    set_logic_flags(state, result, 16);
                }
                status = write_operand_word(state, &operand, result);
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
        case 0x8e: { /* MOV Sreg,r/m16. */
            uint8_t modrm;
            unsigned int segment;
            uint16_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            segment = (modrm >> 3U) & 7U;
            if ((segment >= 4U) || (segment == 1U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if (status == BM_STATUS_OK) {
                state->segments[segment] = value;
                state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0x8c: { /* MOV r/m16,Sreg. */
            uint8_t modrm;
            unsigned int segment;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            segment = (modrm >> 3U) & 7U;
            if (segment >= 4U)
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK) {
                status = write_operand_word(state, &operand, state->segments[segment]);
                if (status == BM_STATUS_OK)
                    state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0x8f: { /* POP r/m16. */
            uint8_t modrm;
            uint16_t value = 0U;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if (((modrm >> 3U) & 7U) != 0U)
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                status = write_operand_word(state, &operand, value);
            return status;
        }
        case 0xf7: { /* TEST/NOT/NEG/MUL/IMUL/DIV/IDIV r/m16. */
            uint8_t modrm;
            unsigned int operation;
            uint16_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if ((status == BM_STATUS_OK) &&
                ((operation == 0U) || (operation == 1U))) {
                uint16_t immediate = 0;
                status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint16_t) (value & immediate), 16);
            } else if ((status == BM_STATUS_OK) && (operation == 2U)) {
                status = write_operand_word(state, &operand, (uint16_t) ~value);
            } else if ((status == BM_STATUS_OK) && (operation == 3U)) {
                uint16_t result = (uint16_t) (0U - value);
                compare16(state, 0U, value);
                status = write_operand_word(state, &operand, result);
            } else if ((status == BM_STATUS_OK) && (operation == 4U)) {
                uint32_t result = (uint32_t) state->registers[REG_AX] * value;
                state->registers[REG_AX] = (uint16_t) result;
                state->registers[REG_DX] = (uint16_t) (result >> 16U);
                state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF);
                if ((result & 0xffff0000UL) != 0U)
                    state->flags |= FLAG_CF | FLAG_OF;
            } else if ((status == BM_STATUS_OK) && (operation == 5U)) {
                int32_t result = signed_word(state->registers[REG_AX]) *
                                 signed_word(value);
                uint32_t bits = (uint32_t) result;
                state->registers[REG_AX] = (uint16_t) bits;
                state->registers[REG_DX] = (uint16_t) (bits >> 16U);
                state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF);
                if ((result < -32768L) || (result > 32767L))
                    state->flags |= FLAG_CF | FLAG_OF;
            } else if ((status == BM_STATUS_OK) && (operation == 6U)) {
                uint32_t dividend = ((uint32_t) state->registers[REG_DX] << 16U) |
                                    state->registers[REG_AX];
                uint32_t quotient;
                if (value == 0U)
                    return enter_interrupt(state, 0U);
                quotient = dividend / value;
                if (quotient > 0xffffU)
                    return enter_interrupt(state, 0U);
                state->registers[REG_AX] = (uint16_t) quotient;
                state->registers[REG_DX] = (uint16_t) (dividend % value);
            } else if (status == BM_STATUS_OK) {
                uint32_t dividend_bits =
                    ((uint32_t) state->registers[REG_DX] << 16U) |
                    state->registers[REG_AX];
                int64_t dividend = (dividend_bits & 0x80000000UL) != 0U ?
                    (int64_t) dividend_bits - 0x100000000LL :
                    (int64_t) dividend_bits;
                int64_t divisor = signed_word(value);
                int64_t quotient;
                int64_t remainder;
                if (divisor == 0)
                    return enter_interrupt(state, 0U);
                quotient = dividend / divisor;
                remainder = dividend % divisor;
                if ((quotient < -32768L) || (quotient > 32767L))
                    return enter_interrupt(state, 0U);
                state->registers[REG_AX] = (uint16_t) quotient;
                state->registers[REG_DX] = (uint16_t) remainder;
            }
            return status;
        }
        case 0xff: { /* INC/DEC/CALL/JMP/PUSH r/m16 subset. */
            uint8_t modrm;
            unsigned int operation;
            uint16_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if ((operation != 0U) && (operation != 1U) &&
                (operation != 2U) && (operation != 3U) &&
                (operation != 4U) && (operation != 5U) &&
                (operation != 6U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) &&
                ((operation == 3U) || (operation == 5U))) {
                uint16_t segment = 0U;
                if (operand.is_register)
                    return BM_STATUS_UNSUPPORTED;
                status = read_word(state, operand.segment, operand.offset, &value);
                if (status == BM_STATUS_OK)
                    status = read_word(state, operand.segment,
                                       (uint16_t) (operand.offset + 2U), &segment);
                if ((status == BM_STATUS_OK) && (operation == 3U))
                    status = push_word(state, state->segments[1]);
                if ((status == BM_STATUS_OK) && (operation == 3U))
                    status = push_word(state, state->ip);
                if (status == BM_STATUS_OK) {
                    state->ip = value;
                    state->segments[1] = segment;
                }
                return status;
            }
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if (status != BM_STATUS_OK)
                return status;
            if ((operation == 0U) || (operation == 1U)) {
                uint16_t carry = state->flags & FLAG_CF;
                uint16_t result;
                if (operation == 0U)
                    result = add16(state, value, 1U);
                else {
                    result = (uint16_t) (value - 1U);
                    compare16(state, value, 1U);
                }
                state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
                return write_operand_word(state, &operand, result);
            }
            if (operation == 2U) {
                status = push_word(state, state->ip);
                if (status == BM_STATUS_OK)
                    state->ip = value;
                return status;
            }
            if (operation == 4U) {
                state->ip = value;
                return BM_STATUS_OK;
            }
            return push_word(state, value);
        }
        case 0xf6: { /* TEST/NOT/NEG/MUL/IMUL/DIV/IDIV r/m8. */
            uint8_t modrm;
            unsigned int operation;
            uint8_t value = 0;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if ((status == BM_STATUS_OK) &&
                ((operation == 0U) || (operation == 1U))) {
                uint8_t immediate = 0;
                status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint8_t) (value & immediate), 8);
            } else if ((status == BM_STATUS_OK) && (operation == 2U)) {
                status = write_operand_byte(state, &operand, (uint8_t) ~value);
            } else if ((status == BM_STATUS_OK) && (operation == 3U)) {
                uint8_t result = (uint8_t) (0U - value);
                compare8(state, 0U, value);
                status = write_operand_byte(state, &operand, result);
            } else if ((status == BM_STATUS_OK) && (operation == 4U)) {
                uint16_t result = (uint16_t) ((uint8_t) state->registers[REG_AX] * value);
                state->registers[REG_AX] = result;
                state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF);
                if ((result & 0xff00U) != 0U)
                    state->flags |= FLAG_CF | FLAG_OF;
            } else if ((status == BM_STATUS_OK) && (operation == 5U)) {
                int32_t result = signed_byte((uint8_t) state->registers[REG_AX]) *
                                 signed_byte(value);
                state->registers[REG_AX] = (uint16_t) result;
                state->flags &= (uint16_t) ~(FLAG_CF | FLAG_OF);
                if ((result < -128) || (result > 127))
                    state->flags |= FLAG_CF | FLAG_OF;
            } else if ((status == BM_STATUS_OK) && (operation == 6U)) {
                uint16_t dividend = state->registers[REG_AX];
                uint16_t quotient;
                if (value == 0U)
                    return enter_interrupt(state, 0U);
                quotient = (uint16_t) (dividend / value);
                if (quotient > 0xffU)
                    return enter_interrupt(state, 0U);
                set_register_byte(state, 0U, (uint8_t) quotient);
                set_register_byte(state, 4U, (uint8_t) (dividend % value));
            } else if (status == BM_STATUS_OK) {
                int32_t dividend = signed_word(state->registers[REG_AX]);
                int32_t divisor = signed_byte(value);
                int32_t quotient;
                int32_t remainder;
                if (divisor == 0)
                    return enter_interrupt(state, 0U);
                quotient = dividend / divisor;
                remainder = dividend % divisor;
                /* Hardware V20 vectors report divide error for -128 too. */
                if ((quotient <= -128) || (quotient > 127))
                    return enter_interrupt(state, 0U);
                set_register_byte(state, 0U, (uint8_t) quotient);
                set_register_byte(state, 4U, (uint8_t) remainder);
            }
            return status;
        }
        case 0xfe: { /* INC/DEC r/m8. */
            uint8_t modrm;
            unsigned int operation;
            uint16_t carry;
            uint8_t value = 0;
            uint8_t result;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if (operation > 1U)
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if (status != BM_STATUS_OK)
                return status;
            carry = state->flags & FLAG_CF;
            if (operation == 0U)
                result = add8(state, value, 1U);
            else {
                result = (uint8_t) (value - 1U);
                compare8(state, value, 1U);
            }
            state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
            return write_operand_byte(state, &operand, result);
        }
        case 0xc6: /* MOV r/m8,imm8. */
        case 0xc7: { /* MOV r/m16,imm16. */
            uint8_t modrm;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            if (((modrm >> 3U) & 7U) != 0U)
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if (opcode == 0xc6U) {
                uint8_t immediate = 0;
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = write_operand_byte(state, &operand, immediate);
            } else {
                uint16_t immediate = 0;
                if (status == BM_STATUS_OK)
                    status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = write_operand_word(state, &operand, immediate);
            }
            return status;
        }
        case 0xe8: { /* CALL rel16. */
            uint16_t displacement;
            status = fetch_word(state, &displacement);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->ip);
            if (status == BM_STATUS_OK)
                state->ip = (uint16_t) (state->ip + (int16_t) displacement);
            return status;
        }
        case 0x9a: { /* CALL ptr16:16. */
            uint16_t destination = 0U;
            uint16_t segment = 0U;
            status = fetch_word(state, &destination);
            if (status == BM_STATUS_OK)
                status = fetch_word(state, &segment);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->segments[1]);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->ip);
            if (status == BM_STATUS_OK) {
                state->ip = destination;
                state->segments[1] = segment;
            }
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
        case 0xe0: /* LOOPNE rel8 */
        case 0xe1: /* LOOPE rel8 */
        case 0xe2: { /* LOOP rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if (status == BM_STATUS_OK) {
                state->registers[REG_CX] = (uint16_t) (state->registers[REG_CX] - 1U);
                if ((state->registers[REG_CX] != 0) &&
                    ((opcode == 0xe2U) ||
                     ((opcode == 0xe0U) && ((state->flags & FLAG_ZF) == 0U)) ||
                     ((opcode == 0xe1U) && ((state->flags & FLAG_ZF) != 0U))))
                    state->ip = (uint16_t) (state->ip + (int8_t) displacement);
            }
            return status;
        }
        case 0xe3: { /* JCXZ rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if ((status == BM_STATUS_OK) && (state->registers[REG_CX] == 0U))
                state->ip = (uint16_t) (state->ip + (int8_t) displacement);
            return status;
        }
        case 0xc2: /* RET near imm16 */
        case 0xc3: { /* RET near */
            uint16_t destination = 0U;
            uint16_t adjustment = 0U;
            if (opcode == 0xc2U)
                status = fetch_word(state, &adjustment);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &destination);
            if (status == BM_STATUS_OK) {
                state->ip = destination;
                state->registers[REG_SP] =
                    (uint16_t) (state->registers[REG_SP] + adjustment);
            }
            return status;
        }
        case 0xc4: /* LES r16,m16:16 */
        case 0xc5: { /* LDS r16,m16:16 */
            uint8_t modrm;
            uint16_t offset = 0;
            uint16_t segment = 0;
            bm_808x_operand_t operand = { 0, 0, 0, 0 };
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) && operand.is_register)
                return BM_STATUS_UNSUPPORTED;
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment, operand.offset, &offset);
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment,
                                   (uint16_t) (operand.offset + 2U), &segment);
            if (status == BM_STATUS_OK) {
                state->registers[(modrm >> 3U) & 7U] = offset;
                state->segments[opcode == 0xc4U ? 0U : 3U] = segment;
                state->interrupt_inhibit = 2U;
            }
            return status;
        }
        case 0xc8: { /* PREPARE/ENTER imm16,imm8. */
            uint16_t allocation = 0U;
            uint16_t frame_pointer;
            uint16_t frame_walk;
            uint8_t nesting = 0U;
            status = fetch_word(state, &allocation);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &nesting);
            if (status != BM_STATUS_OK)
                return status;
            frame_walk = state->registers[REG_BP];
            status = push_word(state, frame_walk);
            frame_pointer = state->registers[REG_SP];
            if ((status == BM_STATUS_OK) && (nesting != 0U)) {
                uint8_t remaining = nesting;
                while ((status == BM_STATUS_OK) && (--remaining != 0U)) {
                    uint16_t ancestor = 0U;
                    frame_walk = (uint16_t) (frame_walk - 2U);
                    status = read_word(state, state->segments[2], frame_walk,
                                       &ancestor);
                    if (status == BM_STATUS_OK)
                        status = push_word(state, ancestor);
                }
                if (status == BM_STATUS_OK)
                    status = push_word(state, frame_pointer);
            }
            if (status == BM_STATUS_OK) {
                state->registers[REG_BP] = frame_pointer;
                state->registers[REG_SP] =
                    (uint16_t) (state->registers[REG_SP] - allocation);
            }
            return status;
        }
        case 0xc9: { /* DISPOSE/LEAVE. */
            uint16_t original_sp = state->registers[REG_SP];
            uint16_t frame_pointer = 0U;
            state->registers[REG_SP] = state->registers[REG_BP];
            status = pop_word(state, &frame_pointer);
            if (status == BM_STATUS_OK)
                state->registers[REG_BP] = frame_pointer;
            else
                state->registers[REG_SP] = original_sp;
            return status;
        }
        case 0xca: /* RET far imm16 */
        case 0xcb: { /* RET far */
            uint16_t destination = 0U;
            uint16_t segment = 0U;
            uint16_t adjustment = 0U;
            if (opcode == 0xcaU)
                status = fetch_word(state, &adjustment);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &destination);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &segment);
            if (status == BM_STATUS_OK) {
                state->ip = destination;
                state->segments[1] = segment;
                state->registers[REG_SP] =
                    (uint16_t) (state->registers[REG_SP] + adjustment);
            }
            return status;
        }
        case 0xcc: /* INT3 */
            return enter_interrupt(state, 3U);
        case 0xcd: { /* INT imm8 */
            uint8_t vector;
            status = fetch_byte(state, &vector);
            if (status != BM_STATUS_OK)
                return status;
            return enter_interrupt(state, vector);
        }
        case 0xce: /* INTO */
            return (state->flags & FLAG_OF) != 0U ?
                   enter_interrupt(state, 4U) : BM_STATUS_OK;
        case 0xcf: { /* IRET */
            uint16_t new_ip = 0;
            uint16_t new_cs = 0;
            uint16_t new_flags = 0;
            status = pop_word(state, &new_ip);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &new_cs);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &new_flags);
            if (status == BM_STATUS_OK) {
                state->ip = new_ip;
                state->segments[1] = new_cs;
                state->flags = new_flags | 0x0002U;
            }
            return status;
        }
        case 0xc0: /* Shift r/m8 by immediate count. */
        case 0xd0: /* Shift r/m8 by one. */
        case 0xd2: { /* Shift r/m8 by CL. */
            uint8_t modrm;
            uint8_t value = 0;
            uint8_t count;
            unsigned int operation;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if ((opcode == 0xc0U) && (operation == 6U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) && (opcode == 0xc0U))
                status = fetch_byte(state, &count);
            else
                count = opcode == 0xd0U ? 1U : get_register_byte(state, 1U);
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if (status != BM_STATUS_OK)
                return status;
            if (count == 0U)
                return BM_STATUS_OK;
            value = rotate_shift8(state, value, operation, count);
            return write_operand_byte(state, &operand, value);
        }
        case 0xc1: /* Shift r/m16 by immediate count. */
        case 0xd1: /* Shift r/m16 by one. */
        case 0xd3: { /* Shift r/m16 by CL. */
            uint8_t modrm;
            uint16_t value = 0;
            uint8_t count;
            unsigned int operation;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status != BM_STATUS_OK)
                return status;
            operation = (modrm >> 3U) & 7U;
            if ((opcode == 0xc1U) && (operation == 6U))
                return BM_STATUS_UNSUPPORTED;
            status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) && (opcode == 0xc1U))
                status = fetch_byte(state, &count);
            else
                count = opcode == 0xd1U ? 1U : get_register_byte(state, 1U);
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if (status != BM_STATUS_OK)
                return status;
            if (count == 0U)
                return BM_STATUS_OK;
            value = rotate_shift16(state, value, operation, count);
            return write_operand_word(state, &operand, value);
        }
        case 0xd7: { /* XLAT. */
            unsigned int segment = (segment_override >= 0) ?
                                   (unsigned int) segment_override : 3U;
            uint8_t value = 0U;
            uint16_t offset = (uint16_t) (state->registers[REG_BX] +
                                          get_register_byte(state, 0U));
            status = read_byte(state, state->segments[segment], offset,
                               BM_BUS_READ, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, 0U, value);
            return status;
        }
        case 0xa0: /* MOV AL,moffs8 */
        case 0xa1: /* MOV AX,moffs16 */
        case 0xa2: /* MOV moffs8,AL */
        case 0xa3: { /* MOV moffs16,AX */
            uint16_t offset;
            unsigned int segment = (segment_override >= 0) ?
                                   (unsigned int) segment_override : 3U;
            status = fetch_word(state, &offset);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0xa0U) {
                uint8_t value = 0;
                status = read_byte(state, state->segments[segment], offset,
                                   BM_BUS_READ, &value);
                if (status == BM_STATUS_OK)
                    set_register_byte(state, 0U, value);
                return status;
            }
            if (opcode == 0xa1U)
                return read_word(state, state->segments[segment], offset,
                                 &state->registers[REG_AX]);
            if (opcode == 0xa2U)
                return write_byte(state, state->segments[segment], offset,
                                  (uint8_t) state->registers[REG_AX]);
            return write_word(state, state->segments[segment], offset,
                              state->registers[REG_AX]);
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
        case 0xf5: /* CMC */
            state->flags ^= FLAG_CF;
            return BM_STATUS_OK;
        case 0xf8: /* CLC */
            state->flags &= (uint16_t) ~FLAG_CF;
            return BM_STATUS_OK;
        case 0xf9: /* STC */
            state->flags |= FLAG_CF;
            return BM_STATUS_OK;
        case 0xfa: /* CLI */
            state->flags &= (uint16_t) ~FLAG_IF;
            return BM_STATUS_OK;
        case 0xfb: /* STI */
            state->flags |= FLAG_IF;
            state->interrupt_inhibit = 2U;
            return BM_STATUS_OK;
        case 0xfc: /* CLD */
            state->flags &= (uint16_t) ~FLAG_DF;
            return BM_STATUS_OK;
        case 0xfd: /* STD */
            state->flags |= FLAG_DF;
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
    while (*consumed < budget) {
        if (maskable_interrupt_ready(state)) {
            status = service_interrupt(state);
            if (status != BM_STATUS_OK)
                return status;
            ++*consumed;
            continue;
        }
        if (state->halted)
            return BM_STATUS_IDLE;
        status = execute_one(state);
        if (status != BM_STATUS_OK)
            return status;
        if (state->interrupt_inhibit != 0U)
            --state->interrupt_inhibit;
        ++*consumed;
        if (state->halted) {
            if (maskable_interrupt_ready(state)) {
                state->halted = 0;
                continue;
            }
            return BM_STATUS_IDLE;
        }
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
    if (asserted && ((state->flags & FLAG_IF) != 0U) &&
        (state->interrupt_inhibit == 0U))
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
    else if (strcmp(name, "last_effective_opcode") == 0)
        *value = state->last_effective_opcode;
    else if (strcmp(name, "last_instruction_bytes") == 0)
        *value = state->last_instruction_bytes;
    else if (strcmp(name, "last_instruction_length") == 0)
        *value = state->last_instruction_length;
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

static int
is_808x_cpu(const bm_cpu_t *cpu)
{
    return (cpu != NULL) && (cpu->context != NULL) &&
           (cpu->ops.reset == cpu_reset) && (cpu->ops.run == cpu_run) &&
           (cpu->ops.signal == cpu_signal) &&
           (cpu->ops.inspect == cpu_inspect) &&
           (cpu->ops.destroy == cpu_destroy);
}

bm_status_t
bm_808x_get_arch_state(const bm_cpu_t *cpu, bm_808x_arch_state_t *out_state)
{
    const bm_808x_state_t *state;

    if (!is_808x_cpu(cpu) || (out_state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state = cpu->context;
    *out_state = (bm_808x_arch_state_t) {
        sizeof(*out_state), BM_808X_ARCH_STATE_VERSION, state->model,
        state->registers[REG_AX], state->registers[REG_CX],
        state->registers[REG_DX], state->registers[REG_BX],
        state->registers[REG_SP], state->registers[REG_BP],
        state->registers[REG_SI], state->registers[REG_DI],
        state->segments[0], state->segments[1], state->segments[2],
        state->segments[3], state->ip, state->flags,
        (uint8_t) !!state->halted, state->interrupt_inhibit
    };
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_set_arch_state(bm_cpu_t *cpu, const bm_808x_arch_state_t *state_image)
{
    bm_808x_state_t *state;

    if (!is_808x_cpu(cpu) || (state_image == NULL) ||
        (state_image->size != sizeof(*state_image)) ||
        (state_image->version != BM_808X_ARCH_STATE_VERSION) ||
        (state_image->model != BM_808X_NEC_V30) ||
        (state_image->halted > 1U) ||
        (state_image->interrupt_inhibit > 1U))
        return BM_STATUS_INVALID_ARGUMENT;
    state = cpu->context;
    state->registers[REG_AX] = state_image->ax;
    state->registers[REG_CX] = state_image->cx;
    state->registers[REG_DX] = state_image->dx;
    state->registers[REG_BX] = state_image->bx;
    state->registers[REG_SP] = state_image->sp;
    state->registers[REG_BP] = state_image->bp;
    state->registers[REG_SI] = state_image->si;
    state->registers[REG_DI] = state_image->di;
    state->segments[0] = state_image->es;
    state->segments[1] = state_image->cs;
    state->segments[2] = state_image->ss;
    state->segments[3] = state_image->ds;
    state->ip = state_image->ip;
    state->flags = state_image->flags;
    state->halted = state_image->halted;
    state->interrupt_inhibit = state_image->interrupt_inhibit;
    state->last_fetch = physical_address(state_image->cs, state_image->ip);
    state->last_opcode = 0U;
    state->last_effective_opcode = 0U;
    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_step(bm_cpu_t *cpu, bm_tick_t *consumed)
{
    if (!is_808x_cpu(cpu))
        return BM_STATUS_INVALID_ARGUMENT;
    return cpu_run(cpu->context, 1U, consumed);
}
