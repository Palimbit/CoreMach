/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Derived rewrite of the inherited 808x/Vx0 interpreter. The original work
 * includes Copyright 2015-2020 Andrew Jenner and Copyright 2016-2020 Miran
 * Grca. This file implements a portable functional NEC V30 core whose
 * remaining timing limitations are reported explicitly.
 */
#include <blumach/components/cpu_808x.h>

#include "v30_bcu.h"

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
    FLAG_OF = 0x0800,
    FLAG_MD = 0x8000,
    PSW_WRITABLE = 0x8fd5,
    PSW_FIXED_ONE = 0x7002
};

typedef struct bm_808x_state {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_808x_model_t model;
    uint32_t frequency_hz;
    uint16_t registers[8];
    uint16_t segments[4];
    uint16_t ip;
    bm_v30_bcu_t bcu;
    uint16_t flags;
    uint32_t last_fetch;
    uint8_t last_opcode;
    uint8_t last_effective_opcode;
    uint64_t last_instruction_bytes;
    uint8_t last_instruction_length;
    uint8_t last_prefix_count;
    int halted;
    int interrupt_asserted;
    uint8_t interrupt_inhibit;
    uint8_t boundary_inhibit;
    int nmi_line_asserted;
    int nmi_pending;
    int trap_pending;
    int interrupt_entered;
    int bus_lock_active;
    int md_write_enabled;
    bm_808x_trace_fn trace;
    void *trace_context;
    bm_808x_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
    bm_808x_fpo_fn fpo;
    bm_808x_poll_fn poll;
    void *coprocessor_context;
    bm_808x_timing_fn timing;
    void *timing_context;
    int boundary_rm_valid;
    int boundary_rm_memory;
    uint16_t boundary_rm_offset;
    uint16_t boundary_initial_sp;
    uint16_t boundary_initial_bp;
    uint16_t boundary_initial_si;
    uint16_t boundary_initial_di;
    uint16_t boundary_initial_dx;
    uint32_t boundary_string_iterations;
    uint8_t boundary_repeat_mode;
    int boundary_string_valid;
    uint8_t boundary_shift_count;
    int boundary_shift_count_valid;
    uint32_t boundary_execution_clocks_placed;
    int boundary_execution_timeline_active;
    int boundary_execution_timeline_complete;
    int boundary_operand_timeline_supported;
    int boundary_flush_timeline_supported;
    uint32_t boundary_interrupt_execution_clocks;
    int boundary_poll_ready;
    bm_808x_execution_clock_kind_t last_boundary_clock_kind;
    uint64_t last_boundary_clocks_min;
    uint64_t last_boundary_clocks_max;
    int last_boundary_observed;
} bm_808x_state_t;

static void
mark_prefetch_flush(bm_808x_state_t *state)
{
    bm_v30_bcu_flush(&state->bcu, state->ip);
}

static uint16_t
psw_image(uint16_t value)
{
    return (uint16_t) ((value & PSW_WRITABLE) | PSW_FIXED_ONE);
}

static void
restore_psw(bm_808x_state_t *state, uint16_t value)
{
    uint16_t mode = state->flags & FLAG_MD;
    state->flags = psw_image(value);
    if (!state->md_write_enabled)
        state->flags = (uint16_t) ((state->flags & ~FLAG_MD) | mode);
}

static uint32_t
physical_address(uint16_t segment, uint16_t offset)
{
    return ((((uint32_t) segment << 4U) + offset) & 0xfffffU);
}

static void
begin_operand_execution_timeline(bm_808x_state_t *state)
{
    state->boundary_execution_timeline_active = 1;
    state->boundary_operand_timeline_supported = 1;
}

static bm_status_t
place_execution_clocks(bm_808x_state_t *state, uint32_t clocks)
{
    bm_status_t status;

    if (!state->boundary_execution_timeline_active)
        return BM_STATUS_OK;
    if (state->boundary_execution_clocks_placed > UINT32_MAX - clocks)
        return BM_STATUS_INVALID_STATE;
    status = bm_v30_bcu_advance_prefetch(
        &state->bcu, state->bus, state->segments[1], 0U, clocks);
    if (status == BM_STATUS_OK)
        state->boundary_execution_clocks_placed += clocks;
    return status;
}

static bm_status_t
place_suspended_execution_clocks(bm_808x_state_t *state, uint32_t clocks)
{
    if (!state->boundary_execution_timeline_active)
        return BM_STATUS_OK;
    if (state->boundary_execution_clocks_placed > UINT32_MAX - clocks)
        return BM_STATUS_INVALID_STATE;
    state->boundary_execution_clocks_placed += clocks;
    return BM_STATUS_OK;
}

static bm_status_t
transact_operand(bm_808x_state_t *state,
                 bm_bus_transaction_t *transaction)
{
    bm_status_t status = bm_v30_bcu_transact(
        &state->bcu, state->bus, transaction);

    /* NEC's documented execution interval includes the four base clocks of
     * each external operand transfer, but not device wait states. */
    if ((status == BM_STATUS_OK) &&
        state->boundary_operand_timeline_supported) {
        if (state->boundary_execution_clocks_placed > UINT32_MAX - 4U)
            return BM_STATUS_INVALID_STATE;
        state->boundary_execution_clocks_placed += 4U;
    }
    return status;
}

static bm_status_t
read_byte(bm_808x_state_t *state, uint16_t segment, uint16_t offset,
          bm_bus_operation_t operation, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, operation, physical_address(segment, offset), 0,
        1, 1, 0, BM_ENDIAN_LITTLE,
        state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
    };
    bm_status_t status = transact_operand(state, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
fill_prefetch_queue(bm_808x_state_t *state)
{
    return bm_v30_bcu_fill_on_demand(
        &state->bcu, state->bus, state->segments[1], 0U);
}

static bm_status_t
fetch_byte(bm_808x_state_t *state, uint8_t *value)
{
    bm_status_t status = BM_STATUS_OK;

    if (bm_v30_bcu_queue_count(&state->bcu) == 0U)
        status = fill_prefetch_queue(state);
    if (status == BM_STATUS_OK) {
        status = bm_v30_bcu_dequeue_byte(&state->bcu, value);
    }
    if (status == BM_STATUS_OK) {
        if (state->last_instruction_length < 8U)
            state->last_instruction_bytes |=
                (uint64_t) *value << (state->last_instruction_length * 8U);
        if (state->last_instruction_length != UINT8_MAX)
            ++state->last_instruction_length;
        ++state->ip;
        /* The V30 predecoder consumes one clock for every byte removed from
         * the instruction queue.  The BCU remains active during that clock,
         * so an idle queue may start a speculative fetch and an in-flight
         * fetch advances by one phase.  Keep the dequeue visible before the
         * BCU step: the freed byte is what can make room for that fetch. */
        status = bm_v30_bcu_advance_prefetch(
            &state->bcu, state->bus, state->segments[1], 0U, 1U);
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
        1, 1, 0, BM_ENDIAN_LITTLE,
        state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
    };
    bm_status_t status = transact_operand(state, &transaction);
    return status;
}

static bm_status_t
read_word(bm_808x_state_t *state, uint16_t segment, uint16_t offset, uint16_t *value)
{
    if ((offset & 1U) == 0U) {
        bm_bus_transaction_t transaction = {
            BM_ADDRESS_MEMORY, BM_BUS_READ, physical_address(segment, offset),
            0, 2, 2, 0, BM_ENDIAN_LITTLE,
            state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
        };
        bm_status_t status = transact_operand(state, &transaction);

        if (status == BM_STATUS_OK)
            *value = (uint16_t) transaction.value;
        return status;
    }

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
    if ((offset & 1U) == 0U) {
        bm_bus_transaction_t transaction = {
            BM_ADDRESS_MEMORY, BM_BUS_WRITE, physical_address(segment, offset),
            value, 2, 2, 0, BM_ENDIAN_LITTLE,
            state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
        };
        bm_status_t status = transact_operand(state, &transaction);

        return status;
    }

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
        state->boundary_rm_valid = 1;
        state->boundary_rm_memory = 0;
        state->boundary_rm_offset = 0U;
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
    state->boundary_rm_valid = 1;
    state->boundary_rm_memory = 1;
    state->boundary_rm_offset = operand->offset;
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
execute_fpo(bm_808x_state_t *state, uint8_t opcode, int segment_override,
            bm_808x_fpo_family_t family)
{
    bm_808x_fpo_request_t request = {
        .size = sizeof(request),
        .family = family,
        .opcode = opcode
    };
    bm_808x_operand_t operand;
    bm_status_t status;

    status = fetch_byte(state, &request.modrm);
    if (status != BM_STATUS_OK)
        return status;
    status = decode_rm_operand(state, request.modrm, segment_override, &operand);
    if (status != BM_STATUS_OK)
        return status;
    if (!operand.is_register) {
        request.memory_operand = 1U;
        request.segment = operand.segment;
        request.offset = operand.offset;
        request.physical_address = physical_address(operand.segment,
                                                    operand.offset);
        status = read_word(state, operand.segment, operand.offset,
                           &request.memory_value);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (state->fpo == NULL)
        return BM_STATUS_OK;
    return state->fpo(state->coprocessor_context, &request);
}

static bm_status_t
execute_poll(bm_808x_state_t *state, uint16_t instruction_ip, int bus_lock)
{
    bm_status_t status;
    int ready = -1;

    if (bus_lock)
        return BM_STATUS_UNSUPPORTED;
    if (state->poll == NULL)
        return BM_STATUS_UNSUPPORTED;
    status = state->poll(state->coprocessor_context, &ready);
    if (status != BM_STATUS_OK)
        return status;
    if ((ready != 0) && (ready != 1))
        return BM_STATUS_DEVICE_ERROR;
    state->boundary_poll_ready = ready;
    if (!ready) {
        state->ip = instruction_ip;
        /* The current portable POLL contract exposes each pin sample as a
         * boundary. Re-entering that boundary must therefore discard bytes
         * fetched beyond the restored architectural IP. A future resumable
         * POLL state will keep the instruction internal instead. */
        mark_prefetch_flush(state);
    }
    return BM_STATUS_OK;
}

static bm_status_t
enter_interrupt(bm_808x_state_t *state, uint8_t vector)
{
    uint16_t new_ip = 0;
    uint16_t new_cs = 0;
    uint16_t old_ip = state->ip;
    uint16_t old_cs = state->segments[1];
    uint16_t old_flags = psw_image(state->flags);
    bm_status_t status;

    /* Preserve the V30 microcode order.  The vector is latched before the
     * interrupt frame can overwrite memory, then prefetch is suspended while
     * the three stack words are written. */
    status = place_execution_clocks(state, 3U);
    if (status == BM_STATUS_OK)
        status = read_word(state, 0, (uint16_t) ((uint16_t) vector * 4U),
                           &new_ip);
    if (status == BM_STATUS_OK)
        status = place_execution_clocks(state, 1U);
    if (status == BM_STATUS_OK)
        status = read_word(state, 0,
                           (uint16_t) ((uint16_t) vector * 4U + 2U),
                           &new_cs);
    if (status == BM_STATUS_OK) {
        bm_v30_bcu_suspend_prefetch(&state->bcu);
        status = place_suspended_execution_clocks(state, 2U);
    }
    if (status == BM_STATUS_OK)
        status = push_word(state, old_flags);
    if (status == BM_STATUS_OK) {
        state->flags = (uint16_t) ((old_flags | FLAG_MD) &
                                   ~(FLAG_IF | FLAG_TF));
        status = place_suspended_execution_clocks(state, 4U);
    }
    if (status == BM_STATUS_OK)
        status = push_word(state, old_cs);
    if (status == BM_STATUS_OK) {
        state->segments[1] = new_cs;
        status = place_suspended_execution_clocks(state, 1U);
    }
    if (status == BM_STATUS_OK)
        status = place_suspended_execution_clocks(state, 2U);
    if (status == BM_STATUS_OK) {
        state->ip = new_ip;
        mark_prefetch_flush(state);
        state->boundary_flush_timeline_supported = 1;
        status = place_execution_clocks(state, 3U);
    }
    if (status == BM_STATUS_OK)
        status = push_word(state, old_ip);
    if (status == BM_STATUS_OK) {
        state->halted = 0;
        state->trap_pending = 0;
        state->interrupt_entered = 1;
    }
    return status;
}

static bm_status_t
service_nmi_at(bm_808x_state_t *state, uint16_t return_ip)
{
    bm_status_t status;

    state->ip = return_ip;
    state->nmi_pending = 0;
    status = enter_interrupt(state, 2U);
    if (status != BM_STATUS_OK)
        state->nmi_pending = 1;
    return status;
}

static bm_status_t
service_nmi(bm_808x_state_t *state)
{
    return service_nmi_at(state, state->ip);
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

static int
nmi_ready(const bm_808x_state_t *state)
{
    return state->nmi_pending && (state->boundary_inhibit == 0U);
}

static int
trap_ready(const bm_808x_state_t *state)
{
    return state->trap_pending && (state->boundary_inhibit == 0U);
}

static int
boundary_interrupt_ready(const bm_808x_state_t *state)
{
    return nmi_ready(state) || maskable_interrupt_ready(state) ||
           trap_ready(state);
}

static bm_status_t
service_boundary_interrupt(bm_808x_state_t *state)
{
    bm_status_t status;
    uint32_t acceptance_clocks;
    uint32_t expected_clocks;

    if (nmi_ready(state))
        acceptance_clocks = 2U;
    else if (maskable_interrupt_ready(state))
        acceptance_clocks = 13U;
    else
        return enter_interrupt(state, 1U);

    expected_clocks = (acceptance_clocks == 2U ? 38U : 49U) +
                      ((state->boundary_initial_sp & 1U) != 0U ? 12U : 0U);
    begin_operand_execution_timeline(state);
    bm_v30_bcu_suspend_prefetch(&state->bcu);
    status = place_suspended_execution_clocks(state, acceptance_clocks);
    if (status == BM_STATUS_OK)
        status = acceptance_clocks == 2U ?
                 service_nmi(state) : service_interrupt(state);
    if ((status == BM_STATUS_OK) &&
        (state->boundary_execution_clocks_placed != expected_clocks))
        return BM_STATUS_INVALID_STATE;
    if (status == BM_STATUS_OK) {
        state->boundary_interrupt_execution_clocks = expected_clocks;
        state->boundary_execution_timeline_complete = 1;
    }
    return status;
}

static int
external_interrupt_ready(const bm_808x_state_t *state)
{
    return nmi_ready(state) || maskable_interrupt_ready(state);
}

static bm_status_t
service_external_interrupt_at(bm_808x_state_t *state, uint16_t return_ip)
{
    if (nmi_ready(state))
        return service_nmi_at(state, return_ip);
    return service_interrupt_at(state, return_ip);
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

static bm_status_t
execute_nec_bit_operation(bm_808x_state_t *state, uint8_t extension,
                          int segment_override)
{
    uint8_t modrm;
    uint8_t bit;
    uint16_t value = 0U;
    uint16_t mask;
    unsigned int operation = (extension >> 1U) & 3U;
    unsigned int width = (extension & 1U) != 0U ? 16U : 8U;
    bm_808x_operand_t operand;
    bm_status_t status = fetch_byte(state, &modrm);

    if (status != BM_STATUS_OK)
        return status;
    if ((modrm & 0x38U) != 0U)
        return BM_STATUS_UNSUPPORTED;
    status = decode_rm_operand(state, modrm, segment_override, &operand);
    if ((status == BM_STATUS_OK) && ((extension & 8U) != 0U))
        status = fetch_byte(state, &bit);
    else
        bit = get_register_byte(state, 1U);
    if (status == BM_STATUS_OK) {
        if (width == 8U) {
            uint8_t byte = 0U;
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
    if (operation == 0U) { /* TEST1 resets CY/V and defines only Z. */
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

static bm_status_t
execute_nec_bcd_string(bm_808x_state_t *state, uint8_t extension,
                       int segment_override)
{
    unsigned int digits = get_register_byte(state, 1U);
    unsigned int bytes;
    unsigned int byte_index;
    unsigned int carry = 0U;
    int zero = 1;
    unsigned int source_segment = segment_override >= 0 ?
                                  (unsigned int) segment_override : 3U;

    if ((digits == 0U) || (digits == 255U))
        return BM_STATUS_UNSUPPORTED;
    bytes = (digits + 1U) / 2U;
    for (byte_index = 0U; byte_index < bytes; ++byte_index) {
        uint8_t source = 0U;
        uint8_t destination = 0U;
        uint8_t result = 0U;
        unsigned int nibble;
        bm_status_t status = read_byte(
            state, state->segments[source_segment],
            (uint16_t) (state->registers[REG_SI] + byte_index),
            BM_BUS_READ, &source);
        if (status == BM_STATUS_OK)
            status = read_byte(
                state, state->segments[0],
                (uint16_t) (state->registers[REG_DI] + byte_index),
                BM_BUS_READ, &destination);
        if (status != BM_STATUS_OK)
            return status;
        for (nibble = 0U; nibble < 2U; ++nibble) {
            unsigned int shift = nibble * 4U;
            unsigned int source_digit = (source >> shift) & 0x0fU;
            unsigned int destination_digit = (destination >> shift) & 0x0fU;
            unsigned int result_digit;

            if (extension == 0x20U) {
                result_digit = destination_digit + source_digit + carry;
                carry = 0U;
                while (result_digit >= 10U) {
                    result_digit -= 10U;
                    ++carry;
                }
            } else {
                int difference = (int) destination_digit -
                                 (int) source_digit - (int) carry;
                carry = 0U;
                while (difference < 0) {
                    difference += 10;
                    ++carry;
                }
                result_digit = (unsigned int) difference;
            }
            if (result_digit != 0U)
                zero = 0;
            result |= (uint8_t) (result_digit << shift);
        }
        if (extension != 0x26U) {
            status = write_byte(
                state, state->segments[0],
                (uint16_t) (state->registers[REG_DI] + byte_index), result);
            if (status != BM_STATUS_OK)
                return status;
        }
    }
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_ZF);
    if (carry != 0U)
        state->flags |= FLAG_CF;
    if (zero)
        state->flags |= FLAG_ZF;
    return BM_STATUS_OK;
}

static bm_status_t
execute_nec_nibble_rotate(bm_808x_state_t *state, uint8_t extension,
                          int segment_override)
{
    uint8_t modrm;
    uint8_t destination = 0U;
    uint8_t old_al = get_register_byte(state, 0U);
    uint8_t new_destination;
    uint8_t new_al;
    bm_808x_operand_t operand;
    bm_status_t status = fetch_byte(state, &modrm);

    if (status != BM_STATUS_OK)
        return status;
    if ((modrm & 0x38U) != 0U)
        return BM_STATUS_UNSUPPORTED;
    status = decode_rm_operand(state, modrm, segment_override, &operand);
    if (status == BM_STATUS_OK)
        status = read_operand_byte(state, &operand, &destination);
    if (status != BM_STATUS_OK)
        return status;
    if (extension == 0x28U) {
        new_destination = (uint8_t) ((destination << 4U) | (old_al & 0x0fU));
        new_al = (uint8_t) (destination >> 4U);
    } else {
        new_destination = (uint8_t) ((destination >> 4U) |
                                     ((old_al & 0x0fU) << 4U));
        new_al = (uint8_t) (destination & 0x0fU);
    }
    if (operand.is_register && (operand.register_index == 0U)) {
        set_register_byte(state, 0U,
                          extension == 0x28U ? new_al : new_destination);
        return BM_STATUS_OK;
    }
    status = write_operand_byte(state, &operand, new_destination);
    if (status == BM_STATUS_OK)
        set_register_byte(state, 0U, new_al);
    return status;
}

static bm_status_t
enter_emulation(bm_808x_state_t *state, uint8_t vector)
{
    uint16_t new_ip = 0U;
    uint16_t new_cs = 0U;
    bm_status_t status;

    /* A native call made from an emulation-mode interrupt/call may not nest
     * BRKEM: NEC documents the resulting MD operation as undefined. */
    if (state->md_write_enabled)
        return BM_STATUS_UNSUPPORTED;
    status = read_word(state, 0U, (uint16_t) ((uint16_t) vector * 4U),
                       &new_ip);
    if (status == BM_STATUS_OK)
        status = read_word(state, 0U,
                           (uint16_t) ((uint16_t) vector * 4U + 2U),
                           &new_cs);
    if (status == BM_STATUS_OK)
        status = push_word(state, psw_image(state->flags));
    if (status == BM_STATUS_OK)
        status = push_word(state, state->segments[1]);
    if (status == BM_STATUS_OK)
        status = push_word(state, state->ip);
    if (status == BM_STATUS_OK) {
        state->flags = (uint16_t) (psw_image(state->flags) & ~FLAG_MD);
        state->md_write_enabled = 1;
        state->segments[1] = new_cs;
        state->ip = new_ip;
        mark_prefetch_flush(state);
        state->halted = 0;
        state->interrupt_entered = 1;
    }
    return status;
}

static bm_status_t
return_from_emulation(bm_808x_state_t *state)
{
    uint16_t new_ip = 0U;
    uint16_t new_cs = 0U;
    uint16_t new_flags = 0U;
    bm_status_t status = pop_word(state, &new_ip);

    if (status == BM_STATUS_OK)
        status = pop_word(state, &new_cs);
    if (status == BM_STATUS_OK)
        status = pop_word(state, &new_flags);
    if (status == BM_STATUS_OK) {
        state->ip = new_ip;
        state->segments[1] = new_cs;
        mark_prefetch_flush(state);
        state->flags = (uint16_t) (psw_image(new_flags) | FLAG_MD);
        state->md_write_enabled = 0;
        state->halted = 0;
        state->trap_pending = 0;
        state->interrupt_entered = 1;
    }
    return status;
}

static bm_status_t
read_nec_bit_window(bm_808x_state_t *state, uint16_t segment, uint16_t offset,
                    unsigned int byte_count, uint32_t *value)
{
    unsigned int index;
    uint32_t result = 0U;

    for (index = 0U; index < byte_count; ++index) {
        uint8_t byte = 0U;
        bm_status_t status = read_byte(state, segment,
                                       (uint16_t) (offset + index),
                                       BM_BUS_READ, &byte);
        if (status != BM_STATUS_OK)
            return status;
        result |= (uint32_t) byte << (index * 8U);
    }
    *value = result;
    return BM_STATUS_OK;
}

static bm_status_t
write_nec_bit_window(bm_808x_state_t *state, uint16_t segment, uint16_t offset,
                     unsigned int byte_count, uint32_t value)
{
    unsigned int index;

    for (index = 0U; index < byte_count; ++index) {
        bm_status_t status = write_byte(state, segment,
                                        (uint16_t) (offset + index),
                                        (uint8_t) (value >> (index * 8U)));
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}

static bm_status_t
execute_nec_bit_field(bm_808x_state_t *state, uint8_t extension,
                      int segment_override)
{
    uint8_t modrm;
    uint8_t immediate = 0U;
    unsigned int offset_register;
    unsigned int length_code;
    unsigned int bit_offset;
    unsigned int bit_length;
    unsigned int byte_count;
    unsigned int index_register;
    unsigned int segment_index;
    uint16_t memory_offset;
    uint32_t window = 0U;
    uint32_t value_mask;
    uint32_t field_mask;
    bm_status_t status = fetch_byte(state, &modrm);

    if (status != BM_STATUS_OK)
        return status;
    if ((modrm >> 6U) != 3U)
        return BM_STATUS_UNSUPPORTED;
    offset_register = modrm & 7U;
    if ((extension & 8U) != 0U) {
        if ((modrm & 0x38U) != 0U)
            return BM_STATUS_UNSUPPORTED;
        status = fetch_byte(state, &immediate);
        if (status != BM_STATUS_OK)
            return status;
        if ((immediate & 0xf0U) != 0U)
            return BM_STATUS_UNSUPPORTED;
        length_code = immediate;
    } else {
        length_code = get_register_byte(state, (modrm >> 3U) & 7U);
    }
    bit_offset = get_register_byte(state, offset_register);
    if ((bit_offset > 15U) || (length_code > 15U))
        return BM_STATUS_UNSUPPORTED;
    bit_length = length_code + 1U;
    byte_count = (bit_offset + bit_length + 7U) / 8U;
    index_register = (extension == 0x31U || extension == 0x39U) ?
                     REG_DI : REG_SI;
    segment_index = segment_override >= 0 ?
                    (unsigned int) segment_override :
                    ((extension == 0x31U || extension == 0x39U) ? 0U : 3U);
    memory_offset = state->registers[index_register];
    status = read_nec_bit_window(state, state->segments[segment_index],
                                 memory_offset, byte_count, &window);
    if (status != BM_STATUS_OK)
        return status;
    value_mask = (1UL << bit_length) - 1UL;
    field_mask = value_mask << bit_offset;
    if ((extension == 0x31U) || (extension == 0x39U)) {
        uint32_t inserted = state->registers[REG_AX] & value_mask;
        window = (window & ~field_mask) | (inserted << bit_offset);
        status = write_nec_bit_window(state, state->segments[segment_index],
                                      memory_offset, byte_count, window);
        if (status != BM_STATUS_OK)
            return status;
    } else {
        state->registers[REG_AX] =
            (uint16_t) ((window >> bit_offset) & value_mask);
    }
    bit_offset += bit_length;
    if (bit_offset > 15U) {
        bit_offset -= 16U;
        state->registers[index_register] =
            (uint16_t) (state->registers[index_register] + 2U);
    }
    set_register_byte(state, offset_register, (uint8_t) bit_offset);
    return BM_STATUS_OK;
}

static bm_status_t
execute_nec_extension(bm_808x_state_t *state, int segment_override)
{
    uint8_t extension;
    bm_status_t status = fetch_byte(state, &extension);

    if (status != BM_STATUS_OK)
        return status;
    if ((extension >= 0x10U) && (extension <= 0x1fU))
        return execute_nec_bit_operation(state, extension, segment_override);
    if ((extension == 0x20U) || (extension == 0x22U) ||
        (extension == 0x26U))
        return execute_nec_bcd_string(state, extension, segment_override);
    if ((extension == 0x28U) || (extension == 0x2aU))
        return execute_nec_nibble_rotate(state, extension, segment_override);
    if ((extension == 0x31U) || (extension == 0x33U) ||
        (extension == 0x39U) || (extension == 0x3bU))
        return execute_nec_bit_field(state, extension, segment_override);
    if (extension == 0xffU) { /* BRKEM imm8. */
        uint8_t vector = 0U;
        status = fetch_byte(state, &vector);
        return status == BM_STATUS_OK ?
               enter_emulation(state, vector) : status;
    }
    return BM_STATUS_UNSUPPORTED;
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
    state->boundary_string_valid = 1;
    state->boundary_repeat_mode = (uint8_t) repeat_mode;
    if ((opcode == 0xa4U) || (opcode == 0xa5U) ||
        (opcode == 0xa6U) || (opcode == 0xa7U) ||
        (opcode == 0xaaU) || (opcode == 0xabU) ||
        (opcode == 0xacU) || (opcode == 0xadU) ||
        (opcode == 0xaeU) || (opcode == 0xafU)) {
        begin_operand_execution_timeline(state);
        /* NEC's repeated primitive formula includes its repeat-prefix setup.
         * Preserve the inherited one-clock setup before the first iteration;
         * the fixed tail is completed after the final bus access. */
        if (repeat_mode != 0) {
            status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
        }
    }
    while ((repeat_mode == 0) || (state->registers[REG_CX] != 0)) {
        uint16_t value = 0;
        if ((opcode == 0xa4U) || (opcode == 0xa5U)) { /* MOVS */
            if (width == 1U) {
                uint8_t byte = 0;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &byte);
                if ((status == BM_STATUS_OK) && (repeat_mode == 0))
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    status = write_byte(state, state->segments[0],
                                        state->registers[REG_DI], byte);
            } else {
                status = read_word(state, state->segments[source_segment],
                                   state->registers[REG_SI], &value);
                if ((status == BM_STATUS_OK) && (repeat_mode == 0))
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    status = write_word(state, state->segments[0],
                                        state->registers[REG_DI], value);
            }
        } else if ((opcode == 0xa6U) || (opcode == 0xa7U)) { /* CMPS */
            /* Preserve the inherited operand order.  The final comparison
             * interval is one clock shorter for the non-repeated primitive,
             * whose documented total is thirteen clocks. */
            status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
            if (width == 1U) {
                uint8_t source = 0;
                uint8_t destination = 0;
                status = read_byte(state, state->segments[source_segment],
                                   state->registers[REG_SI], BM_BUS_READ, &source);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 2U);
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
                    status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
                    status = read_word(state, state->segments[0],
                                       state->registers[REG_DI], &destination);
                if (status == BM_STATUS_OK)
                    compare16(state, value, destination);
            }
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(
                    state, repeat_mode != 0 ? 3U : 2U);
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
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(
                    state, repeat_mode != 0 ? 5U : 3U);
        } else { /* SCAS */
            /* The inherited V30 path performs its comparison setup before
             * acquiring ES:DI.  Repeated forms then have four internal clocks
             * before the next iteration; the non-repeated primitive has one. */
            status = place_execution_clocks(state, 2U);
            if (status != BM_STATUS_OK)
                return status;
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
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(
                    state, repeat_mode != 0 ? 4U : 1U);
        }
        if (status != BM_STATUS_OK)
            return status;
        ++state->boundary_string_iterations;
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
             ((repeat_mode == 2) && ((state->flags & FLAG_ZF) != 0)) ||
             ((repeat_mode == 3) && ((state->flags & FLAG_CF) == 0)) ||
             ((repeat_mode == 4) && ((state->flags & FLAG_CF) != 0))))
            break;
        if ((state->registers[REG_CX] != 0U) &&
            !state->bus_lock_active &&
            external_interrupt_ready(state))
            return service_external_interrupt_at(state, repeat_ip);
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
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE,
        state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
    };
    bm_status_t status = transact_operand(state, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
io_write_byte(bm_808x_state_t *state, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE,
        state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
    };
    bm_status_t status = transact_operand(state, &transaction);
    return status;
}

static bm_status_t
io_read_word(bm_808x_state_t *state, uint16_t port, uint16_t *value)
{
    if ((port & 1U) == 0U) {
        bm_bus_transaction_t transaction = {
            BM_ADDRESS_IO, BM_BUS_READ, port, 0, 2, 2, 0,
            BM_ENDIAN_LITTLE,
            state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
        };
        bm_status_t status = transact_operand(state, &transaction);

        if (status == BM_STATUS_OK)
            *value = (uint16_t) transaction.value;
        return status;
    }

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
    if ((port & 1U) == 0U) {
        bm_bus_transaction_t transaction = {
            BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 2, 2, 0,
            BM_ENDIAN_LITTLE,
            state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U
        };
        bm_status_t status = transact_operand(state, &transaction);

        return status;
    }

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

    state->boundary_string_valid = 1;
    state->boundary_repeat_mode = (uint8_t) repeat_mode;
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
        ++state->boundary_string_iterations;
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
            !state->bus_lock_active &&
            external_interrupt_ready(state))
            return service_external_interrupt_at(state, repeat_ip);
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
    bm_v30_bcu_reset(&state->bcu, state->ip);
    state->flags = 0xf002U;
    state->last_fetch = 0xffff0U;
    state->last_opcode = 0;
    state->last_effective_opcode = 0;
    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    state->last_prefix_count = 0U;
    state->halted = 0;
    state->interrupt_asserted = 0;
    state->interrupt_inhibit = 0U;
    state->boundary_inhibit = 0U;
    state->nmi_line_asserted = 0;
    state->nmi_pending = 0;
    state->trap_pending = 0;
    state->interrupt_entered = 0;
    state->bus_lock_active = 0;
    state->md_write_enabled = 0;
    state->boundary_rm_valid = 0;
    state->boundary_rm_memory = 0;
    state->boundary_rm_offset = 0U;
    state->boundary_initial_sp = 0U;
    state->boundary_initial_bp = 0U;
    state->boundary_initial_si = 0U;
    state->boundary_initial_di = 0U;
    state->boundary_initial_dx = 0U;
    state->boundary_string_iterations = 0U;
    state->boundary_repeat_mode = 0U;
    state->boundary_string_valid = 0;
    state->boundary_shift_count = 0U;
    state->boundary_shift_count_valid = 0;
    state->boundary_execution_clocks_placed = 0U;
    state->boundary_execution_timeline_active = 0;
    state->boundary_execution_timeline_complete = 0;
    state->boundary_operand_timeline_supported = 0;
    state->boundary_flush_timeline_supported = 0;
    state->boundary_interrupt_execution_clocks = 0U;
    state->last_boundary_clock_kind = BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    state->last_boundary_clocks_min = 0U;
    state->last_boundary_clocks_max = 0U;
    state->last_boundary_observed = 0;
    return BM_STATUS_OK;
}

static unsigned int
i8080_register_index(unsigned int code)
{
    static const unsigned int registers[] = { 5U, 1U, 6U, 2U, 7U, 3U, 0U };
    return registers[code < 6U ? code : 6U];
}

static bm_status_t
i8080_read_register(bm_808x_state_t *state, unsigned int code, uint8_t *value)
{
    if (code == 6U)
        return read_byte(state, state->segments[3], state->registers[REG_BX],
                         BM_BUS_READ, value);
    *value = get_register_byte(state, i8080_register_index(code));
    return BM_STATUS_OK;
}

static bm_status_t
i8080_write_register(bm_808x_state_t *state, unsigned int code, uint8_t value)
{
    if (code == 6U)
        return write_byte(state, state->segments[3], state->registers[REG_BX],
                          value);
    set_register_byte(state, i8080_register_index(code), value);
    return BM_STATUS_OK;
}

static uint16_t *
i8080_pair(bm_808x_state_t *state, unsigned int code)
{
    static const unsigned int registers[] = {
        REG_CX, REG_DX, REG_BX, REG_BP
    };
    return &state->registers[registers[code & 3U]];
}

static bm_status_t
i8080_push(bm_808x_state_t *state, uint16_t value)
{
    state->registers[REG_BP] = (uint16_t) (state->registers[REG_BP] - 2U);
    return write_word(state, state->segments[3], state->registers[REG_BP],
                      value);
}

static bm_status_t
i8080_pop(bm_808x_state_t *state, uint16_t *value)
{
    bm_status_t status = read_word(state, state->segments[3],
                                   state->registers[REG_BP], value);
    if (status == BM_STATUS_OK)
        state->registers[REG_BP] =
            (uint16_t) (state->registers[REG_BP] + 2U);
    return status;
}

static void
i8080_set_zsp(bm_808x_state_t *state, uint8_t value)
{
    state->flags &= (uint16_t) ~(FLAG_ZF | FLAG_SF | FLAG_PF);
    if (value == 0U)
        state->flags |= FLAG_ZF;
    if ((value & 0x80U) != 0U)
        state->flags |= FLAG_SF;
    if (even_parity(value))
        state->flags |= FLAG_PF;
}

static uint8_t
i8080_add(bm_808x_state_t *state, uint8_t left, uint8_t right,
          unsigned int carry)
{
    uint16_t result = (uint16_t) ((unsigned int) left + right + carry);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_AF);
    if (result > 0xffU)
        state->flags |= FLAG_CF;
    if (((left & 0x0fU) + (right & 0x0fU) + carry) > 0x0fU)
        state->flags |= FLAG_AF;
    i8080_set_zsp(state, (uint8_t) result);
    return (uint8_t) result;
}

static uint8_t
i8080_subtract(bm_808x_state_t *state, uint8_t left, uint8_t right,
               unsigned int borrow)
{
    uint16_t subtrahend = (uint16_t) ((unsigned int) right + borrow);
    uint16_t half_sum = (uint16_t) (left & 0x0fU) +
                        ((uint8_t) ~right & 0x0fU) +
                        (borrow ? 0U : 1U);
    uint8_t result = (uint8_t) (left - subtrahend);
    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_AF);
    if ((uint16_t) left < subtrahend)
        state->flags |= FLAG_CF;
    /* 8080 AC reports the carry produced by two's-complement subtraction,
     * not the borrow sense exposed through CY. */
    if (half_sum > 0x0fU)
        state->flags |= FLAG_AF;
    i8080_set_zsp(state, result);
    return result;
}

static void
i8080_logic(bm_808x_state_t *state, unsigned int operation, uint8_t value)
{
    uint8_t accumulator = get_register_byte(state, 0U);

    state->flags &= (uint16_t) ~(FLAG_CF | FLAG_AF);
    if (operation == 4U) {
        if (((accumulator | value) & 0x08U) != 0U)
            state->flags |= FLAG_AF;
        accumulator &= value;
    } else if (operation == 5U) {
        accumulator ^= value;
    } else {
        accumulator |= value;
    }
    set_register_byte(state, 0U, accumulator);
    i8080_set_zsp(state, accumulator);
}

static bm_status_t
i8080_execute_alu(bm_808x_state_t *state, unsigned int operation,
                  uint8_t value)
{
    uint8_t accumulator = get_register_byte(state, 0U);
    uint8_t result;

    switch (operation) {
        case 0U:
            result = i8080_add(state, accumulator, value, 0U);
            set_register_byte(state, 0U, result);
            break;
        case 1U:
            result = i8080_add(state, accumulator, value,
                               (state->flags & FLAG_CF) != 0U);
            set_register_byte(state, 0U, result);
            break;
        case 2U:
            result = i8080_subtract(state, accumulator, value, 0U);
            set_register_byte(state, 0U, result);
            break;
        case 3U:
            result = i8080_subtract(state, accumulator, value,
                                    (state->flags & FLAG_CF) != 0U);
            set_register_byte(state, 0U, result);
            break;
        case 4U:
        case 5U:
        case 6U:
            i8080_logic(state, operation, value);
            break;
        default:
            (void) i8080_subtract(state, accumulator, value, 0U);
            break;
    }
    return BM_STATUS_OK;
}

static int
i8080_condition(const bm_808x_state_t *state, unsigned int condition)
{
    switch (condition & 7U) {
        case 0U: return (state->flags & FLAG_ZF) == 0U;
        case 1U: return (state->flags & FLAG_ZF) != 0U;
        case 2U: return (state->flags & FLAG_CF) == 0U;
        case 3U: return (state->flags & FLAG_CF) != 0U;
        case 4U: return (state->flags & FLAG_PF) == 0U;
        case 5U: return (state->flags & FLAG_PF) != 0U;
        case 6U: return (state->flags & FLAG_SF) == 0U;
        default: return (state->flags & FLAG_SF) != 0U;
    }
}

static bm_status_t
i8080_call(bm_808x_state_t *state, uint16_t address)
{
    bm_status_t status = i8080_push(state, state->ip);
    if (status == BM_STATUS_OK) {
        state->ip = address;
        mark_prefetch_flush(state);
    }
    return status;
}

static bm_status_t
i8080_return(bm_808x_state_t *state)
{
    uint16_t address = 0U;
    bm_status_t status = i8080_pop(state, &address);
    if (status == BM_STATUS_OK) {
        state->ip = address;
        mark_prefetch_flush(state);
    }
    return status;
}

static void
i8080_decimal_adjust(bm_808x_state_t *state)
{
    uint8_t accumulator = get_register_byte(state, 0U);
    uint8_t correction = 0U;
    int carry = (state->flags & FLAG_CF) != 0U;

    if (((accumulator & 0x0fU) > 9U) ||
        ((state->flags & FLAG_AF) != 0U))
        correction |= 0x06U;
    if ((accumulator > 0x99U) || carry) {
        correction |= 0x60U;
        carry = 1;
    }
    accumulator = i8080_add(state, accumulator, correction, 0U);
    state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                               (carry ? FLAG_CF : 0U));
    set_register_byte(state, 0U, accumulator);
}

static bm_status_t
execute_8080_one(bm_808x_state_t *state)
{
    uint16_t instruction_ip = state->ip;
    uint8_t opcode = 0U;
    bm_status_t status;

    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    status = fetch_byte(state, &opcode);
    if (status != BM_STATUS_OK)
        return status;
    state->last_fetch = physical_address(state->segments[1], instruction_ip);
    state->last_opcode = opcode;
    state->last_effective_opcode = opcode;
    state->last_prefix_count = 0U;
    if (state->trace != NULL) {
        bm_808x_trace_t trace = {
            .cs = state->segments[1],
            .ip = instruction_ip,
            .ds = state->segments[3],
            .es = state->segments[0],
            .ss = state->segments[2],
            .sp = state->registers[REG_SP],
            .ax = state->registers[REG_AX],
            .bx = state->registers[REG_BX],
            .cx = state->registers[REG_CX],
            .dx = state->registers[REG_DX],
            .bp = state->registers[REG_BP],
            .si = state->registers[REG_SI],
            .di = state->registers[REG_DI],
            .flags = psw_image(state->flags),
            .physical_address = state->last_fetch,
            .opcode = opcode,
            .effective_opcode = opcode,
            .prefix_count = 0U
        };
        state->trace(state->trace_context, &trace);
    }

    if ((opcode >= 0x40U) && (opcode <= 0x7fU)) {
        uint8_t value = 0U;
        if (opcode == 0x76U) {
            state->halted = 1;
            return BM_STATUS_OK;
        }
        status = i8080_read_register(state, opcode & 7U, &value);
        return status == BM_STATUS_OK ?
               i8080_write_register(state, (opcode >> 3U) & 7U, value) :
               status;
    }
    if ((opcode >= 0x80U) && (opcode <= 0xbfU)) {
        uint8_t value = 0U;
        status = i8080_read_register(state, opcode & 7U, &value);
        return status == BM_STATUS_OK ?
               i8080_execute_alu(state, (opcode >> 3U) & 7U, value) : status;
    }
    if ((opcode & 0xc7U) == 0x04U) { /* INR r/M. */
        uint8_t value = 0U;
        uint16_t carry = state->flags & FLAG_CF;
        unsigned int code = (opcode >> 3U) & 7U;
        status = i8080_read_register(state, code, &value);
        if (status == BM_STATUS_OK)
            value = i8080_add(state, value, 1U, 0U);
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
        return status == BM_STATUS_OK ?
               i8080_write_register(state, code, value) : status;
    }
    if ((opcode & 0xc7U) == 0x05U) { /* DCR r/M. */
        uint8_t value = 0U;
        uint16_t carry = state->flags & FLAG_CF;
        unsigned int code = (opcode >> 3U) & 7U;
        status = i8080_read_register(state, code, &value);
        if (status == BM_STATUS_OK)
            value = i8080_subtract(state, value, 1U, 0U);
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) | carry);
        return status == BM_STATUS_OK ?
               i8080_write_register(state, code, value) : status;
    }
    if ((opcode & 0xc7U) == 0x06U) { /* MVI r/M,imm8. */
        uint8_t value = 0U;
        status = fetch_byte(state, &value);
        return status == BM_STATUS_OK ?
               i8080_write_register(state, (opcode >> 3U) & 7U, value) :
               status;
    }
    if ((opcode & 0xcfU) == 0x01U) { /* LXI rp,imm16. */
        uint16_t value = 0U;
        status = fetch_word(state, &value);
        if (status == BM_STATUS_OK)
            *i8080_pair(state, (opcode >> 4U) & 3U) = value;
        return status;
    }
    if ((opcode & 0xcfU) == 0x03U) { /* INX rp. */
        uint16_t *pair = i8080_pair(state, (opcode >> 4U) & 3U);
        *pair = (uint16_t) (*pair + 1U);
        return BM_STATUS_OK;
    }
    if ((opcode & 0xcfU) == 0x0bU) { /* DCX rp. */
        uint16_t *pair = i8080_pair(state, (opcode >> 4U) & 3U);
        *pair = (uint16_t) (*pair - 1U);
        return BM_STATUS_OK;
    }
    if ((opcode & 0xcfU) == 0x09U) { /* DAD rp. */
        uint32_t value = (uint32_t) state->registers[REG_BX] +
                         *i8080_pair(state, (opcode >> 4U) & 3U);
        state->registers[REG_BX] = (uint16_t) value;
        state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                   ((value > 0xffffUL) ? FLAG_CF : 0U));
        return BM_STATUS_OK;
    }
    if ((opcode & 0xc7U) == 0xc0U) { /* Conditional RET. */
        return i8080_condition(state, (opcode >> 3U) & 7U) ?
               i8080_return(state) : BM_STATUS_OK;
    }
    if ((opcode & 0xc7U) == 0xc2U) { /* Conditional JMP. */
        uint16_t address = 0U;
        status = fetch_word(state, &address);
        if ((status == BM_STATUS_OK) &&
            i8080_condition(state, (opcode >> 3U) & 7U)) {
            state->ip = address;
            mark_prefetch_flush(state);
        }
        return status;
    }
    if ((opcode & 0xc7U) == 0xc4U) { /* Conditional CALL. */
        uint16_t address = 0U;
        status = fetch_word(state, &address);
        if ((status == BM_STATUS_OK) &&
            i8080_condition(state, (opcode >> 3U) & 7U))
            status = i8080_call(state, address);
        return status;
    }
    if ((opcode & 0xc7U) == 0xc7U) /* RST n. */
        return i8080_call(state, (uint16_t) (opcode & 0x38U));
    if ((opcode & 0xcfU) == 0xc1U) { /* POP rp/PSW. */
        uint16_t value = 0U;
        unsigned int pair = (opcode >> 4U) & 3U;
        status = i8080_pop(state, &value);
        if (status == BM_STATUS_OK) {
            if (pair == 3U) {
                set_register_byte(state, 0U, (uint8_t) (value >> 8U));
                state->flags = (uint16_t) ((state->flags & 0xff00U) |
                                           (value & 0x00d5U) | 0x0002U);
            } else {
                *i8080_pair(state, pair) = value;
            }
        }
        return status;
    }
    if ((opcode & 0xcfU) == 0xc5U) { /* PUSH rp/PSW. */
        unsigned int pair = (opcode >> 4U) & 3U;
        uint16_t value = pair == 3U ?
            (uint16_t) (((uint16_t) get_register_byte(state, 0U) << 8U) |
                        (state->flags & 0x00d5U) | 0x0002U) :
            *i8080_pair(state, pair);
        return i8080_push(state, value);
    }
    if ((opcode & 0xc7U) == 0xc6U) { /* Immediate ALU. */
        uint8_t value = 0U;
        status = fetch_byte(state, &value);
        return status == BM_STATUS_OK ?
               i8080_execute_alu(state, (opcode >> 3U) & 7U, value) : status;
    }

    switch (opcode) {
        case 0x00: /* NOP. */
            return BM_STATUS_OK;
        case 0x02: /* STAX B. */
        case 0x12: /* STAX D. */
            return write_byte(state, state->segments[3],
                              state->registers[opcode == 0x02U ? REG_CX : REG_DX],
                              get_register_byte(state, 0U));
        case 0x0a: /* LDAX B. */
        case 0x1a: { /* LDAX D. */
            uint8_t value = 0U;
            status = read_byte(state, state->segments[3],
                               state->registers[opcode == 0x0aU ? REG_CX : REG_DX],
                               BM_BUS_READ, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, 0U, value);
            return status;
        }
        case 0x07: { /* RLC. */
            uint8_t value = get_register_byte(state, 0U);
            unsigned int carry = value >> 7U;
            set_register_byte(state, 0U, (uint8_t) ((value << 1U) | carry));
            state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                       (carry ? FLAG_CF : 0U));
            return BM_STATUS_OK;
        }
        case 0x0f: { /* RRC. */
            uint8_t value = get_register_byte(state, 0U);
            unsigned int carry = value & 1U;
            set_register_byte(state, 0U,
                              (uint8_t) ((value >> 1U) | (carry << 7U)));
            state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                       (carry ? FLAG_CF : 0U));
            return BM_STATUS_OK;
        }
        case 0x17: { /* RAL. */
            uint8_t value = get_register_byte(state, 0U);
            unsigned int carry_in = (state->flags & FLAG_CF) != 0U;
            unsigned int carry_out = value >> 7U;
            set_register_byte(state, 0U,
                              (uint8_t) ((value << 1U) | carry_in));
            state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                       (carry_out ? FLAG_CF : 0U));
            return BM_STATUS_OK;
        }
        case 0x1f: { /* RAR. */
            uint8_t value = get_register_byte(state, 0U);
            unsigned int carry_in = (state->flags & FLAG_CF) != 0U;
            unsigned int carry_out = value & 1U;
            set_register_byte(state, 0U,
                              (uint8_t) ((value >> 1U) | (carry_in << 7U)));
            state->flags = (uint16_t) ((state->flags & ~FLAG_CF) |
                                       (carry_out ? FLAG_CF : 0U));
            return BM_STATUS_OK;
        }
        case 0x22: { /* SHLD addr. */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            return status == BM_STATUS_OK ?
                   write_word(state, state->segments[3], address,
                              state->registers[REG_BX]) : status;
        }
        case 0x2a: { /* LHLD addr. */
            uint16_t address = 0U;
            uint16_t value = 0U;
            status = fetch_word(state, &address);
            if (status == BM_STATUS_OK)
                status = read_word(state, state->segments[3], address, &value);
            if (status == BM_STATUS_OK)
                state->registers[REG_BX] = value;
            return status;
        }
        case 0x27: /* DAA. */
            i8080_decimal_adjust(state);
            return BM_STATUS_OK;
        case 0x2f: /* CMA. */
            set_register_byte(state, 0U,
                              (uint8_t) ~get_register_byte(state, 0U));
            return BM_STATUS_OK;
        case 0x32: /* STA addr. */
        case 0x3a: { /* LDA addr. */
            uint16_t address = 0U;
            uint8_t value = 0U;
            status = fetch_word(state, &address);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x32U)
                return write_byte(state, state->segments[3], address,
                                  get_register_byte(state, 0U));
            status = read_byte(state, state->segments[3], address,
                               BM_BUS_READ, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, 0U, value);
            return status;
        }
        case 0x37: /* STC. */
            state->flags |= FLAG_CF;
            return BM_STATUS_OK;
        case 0x3f: /* CMC. */
            state->flags ^= FLAG_CF;
            return BM_STATUS_OK;
        case 0xc3: { /* JMP addr. */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            if (status == BM_STATUS_OK) {
                state->ip = address;
                mark_prefetch_flush(state);
            }
            return status;
        }
        case 0xc9: /* RET. */
            return i8080_return(state);
        case 0xcd: { /* CALL addr. */
            uint16_t address = 0U;
            status = fetch_word(state, &address);
            return status == BM_STATUS_OK ? i8080_call(state, address) : status;
        }
        case 0xd3: { /* OUT port. */
            uint8_t port = 0U;
            status = fetch_byte(state, &port);
            return status == BM_STATUS_OK ?
                   io_write_byte(state, port, get_register_byte(state, 0U)) :
                   status;
        }
        case 0xdb: { /* IN port. */
            uint8_t port = 0U;
            uint8_t value = 0U;
            status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = io_read_byte(state, port, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, 0U, value);
            return status;
        }
        case 0xe3: { /* XTHL. */
            uint16_t memory = 0U;
            status = read_word(state, state->segments[3],
                               state->registers[REG_BP], &memory);
            if (status == BM_STATUS_OK)
                status = write_word(state, state->segments[3],
                                    state->registers[REG_BP],
                                    state->registers[REG_BX]);
            if (status == BM_STATUS_OK)
                state->registers[REG_BX] = memory;
            return status;
        }
        case 0xe9: /* PCHL. */
            state->ip = state->registers[REG_BX];
            mark_prefetch_flush(state);
            return BM_STATUS_OK;
        case 0xeb: { /* XCHG. */
            uint16_t value = state->registers[REG_DX];
            state->registers[REG_DX] = state->registers[REG_BX];
            state->registers[REG_BX] = value;
            return BM_STATUS_OK;
        }
        case 0xed: { /* NEC emulation-mode group. */
            uint8_t extension = 0U;
            status = fetch_byte(state, &extension);
            if (status != BM_STATUS_OK)
                return status;
            if (extension == 0xedU) { /* CALLN imm8. */
                uint8_t vector = 0U;
                status = fetch_byte(state, &vector);
                return status == BM_STATUS_OK ?
                       enter_interrupt(state, vector) : status;
            }
            if (extension == 0xfdU) /* RETEM. */
                return return_from_emulation(state);
            return BM_STATUS_UNSUPPORTED;
        }
        case 0xf3: /* DI. */
            state->flags &= (uint16_t) ~FLAG_IF;
            return BM_STATUS_OK;
        case 0xf9: /* SPHL. */
            state->registers[REG_BP] = state->registers[REG_BX];
            return BM_STATUS_OK;
        case 0xfb: /* EI. */
            state->flags |= FLAG_IF;
            state->interrupt_inhibit = 2U;
            return BM_STATUS_OK;
        default:
            /* NEC marks the remaining 8080-map holes undefined; do not adopt
             * undocumented Intel aliases as supported instructions. */
            return BM_STATUS_UNSUPPORTED;
    }
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
    int bus_lock = 0;
    bm_status_t status;
    uint16_t repeat_ip;

    if ((state->flags & FLAG_MD) == 0U)
        return execute_8080_one(state);

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
            case 0x64: repeat = 4; break;          /* REPNC */
            case 0x65: repeat = 3; break;          /* REPC */
            case 0xf0: bus_lock = 1; break;        /* BUSLOCK */
            case 0xf2: repeat = 2; break;          /* REPNE */
            case 0xf3: repeat = 1; break;          /* REP/REPE */
            default: break;
        }
        if ((prefix_segment < 0) && (opcode != 0x64U) &&
            (opcode != 0x65U) && (opcode != 0xf0U) &&
            (opcode != 0xf2U) && (opcode != 0xf3U))
            break;
        if (prefix_segment >= 0)
            segment_override = prefix_segment;
        if (bus_lock)
            state->bus_lock_active = 1;
        ++prefix_count;
        state->boundary_execution_timeline_active = 1;
        status = place_execution_clocks(state, 2U);
        if (status != BM_STATUS_OK)
            return status;
        status = fetch_byte(state, &opcode);
        if (status != BM_STATUS_OK)
            return status;
    }

    state->last_effective_opcode = opcode;
    state->last_prefix_count = prefix_count;
    repeat_ip = (uint16_t) (instruction_ip +
                 (prefix_count > 3U ? prefix_count - 3U : 0U));
    if (state->trace != NULL) {
        bm_808x_trace_t trace = {
            .cs = state->segments[1],
            .ip = instruction_ip,
            .ds = state->segments[3],
            .es = state->segments[0],
            .ss = state->segments[2],
            .sp = state->registers[REG_SP],
            .ax = state->registers[REG_AX],
            .bx = state->registers[REG_BX],
            .cx = state->registers[REG_CX],
            .dx = state->registers[REG_DX],
            .bp = state->registers[REG_BP],
            .si = state->registers[REG_SI],
            .di = state->registers[REG_DI],
            .flags = psw_image(state->flags),
            .physical_address = state->last_fetch,
            .opcode = first_opcode,
            .effective_opcode = opcode,
            .prefix_count = prefix_count
        };
        state->trace(state->trace_context, &trace);
    }
    if (((repeat == 3) || (repeat == 4)) &&
        (opcode != 0xa6U) && (opcode != 0xa7U) &&
        (opcode != 0xaeU) && (opcode != 0xafU))
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
        if ((status == BM_STATUS_OK) && jump_condition(state, opcode - 0x70U)) {
            state->ip = (uint16_t) (state->ip + (int8_t) displacement);
            mark_prefetch_flush(state);
        }
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
    if ((opcode >= 0x50U) && (opcode <= 0x57U)) {
        unsigned int index = opcode - 0x50U;
        uint16_t value = state->registers[index];

        begin_operand_execution_timeline(state);
        status = place_execution_clocks(state, 3U);
        /* V30 stack semantics decrement SP before the source is observed.
         * This matters only for the SP encoding itself. */
        if ((status == BM_STATUS_OK) && (index == REG_SP))
            value = (uint16_t) (state->registers[REG_SP] - 2U);
        if (status == BM_STATUS_OK)
            status = push_word(state, value);
        return status;
    }
    if ((opcode >= 0x58U) && (opcode <= 0x5fU)) {
        uint16_t value;
        begin_operand_execution_timeline(state);
        status = pop_word(state, &value);
        if (status == BM_STATUS_OK)
            status = place_execution_clocks(state, 1U);
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
            bm_808x_operand_t operand = { 0 };
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
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 4U);
            }
            if (status == BM_STATUS_OK)
                status = push_word(state, immediate);
            return status;
        }
        case 0x6a: { /* PUSH sign-extended imm8. */
            uint8_t immediate = 0U;
            status = fetch_byte(state, &immediate);
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 3U);
            }
            if (status == BM_STATUS_OK)
                status = push_word(state, (uint16_t) signed_byte(immediate));
            return status;
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
        case 0x0f: /* NEC V30 native extension map. */
            return execute_nec_extension(state, segment_override);
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
        case 0x0e: /* PUSH CS */
        case 0x16: /* PUSH SS */
        case 0x1e: { /* PUSH DS */
            unsigned int segment = (opcode >> 3U) & 3U;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 3U);
            if (status == BM_STATUS_OK)
                status = push_word(state, state->segments[segment]);
            return status;
        }
        case 0x07: { /* POP ES */
            uint16_t value;
            begin_operand_execution_timeline(state);
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[0] = value;
                state->interrupt_inhibit = 2U;
                state->boundary_inhibit = 2U;
            }
            return status;
        }
        case 0x1f: { /* POP DS */
            uint16_t value;
            begin_operand_execution_timeline(state);
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[3] = value;
                state->interrupt_inhibit = 2U;
                state->boundary_inhibit = 2U;
            }
            return status;
        }
        case 0x17: { /* POP SS */
            uint16_t value;
            begin_operand_execution_timeline(state);
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK) {
                state->segments[2] = value;
                state->interrupt_inhibit = 2U;
                state->boundary_inhibit = 2U;
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
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 3U);
            if (status == BM_STATUS_OK)
                status = push_word(state, psw_image(state->flags));
            return status;
        case 0x9d: { /* POPF */
            uint16_t value;
            begin_operand_execution_timeline(state);
            status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                restore_psw(state, value);
            return status;
        }
        case 0x9b: /* POLL/WAIT: external active-low input. */
            return execute_poll(state, instruction_ip, bus_lock);
        case 0x66: /* FPO2 fp-op,reg/mem. */
        case 0x67:
            return execute_fpo(state, opcode, segment_override, BM_808X_FPO2);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
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
            status = place_execution_clocks(state, 4U);
            if (status != BM_STATUS_OK)
                return status;
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(
                    state, opcode == 0x3bU ? 2U : 1U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 3U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 5U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register &&
                (opcode == 0x39U)) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            } else if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
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
            status = place_execution_clocks(state, 4U);
            if (status != BM_STATUS_OK)
                return status;
            return write_operand_word(state, &operand, result);
        }
        case 0x89: { /* MOV r/m16,r16 */
            uint8_t modrm;
            bm_808x_operand_t operand;
            status = fetch_byte(state, &modrm);
            if (status == BM_STATUS_OK)
                status = decode_rm_operand(state, modrm, segment_override, &operand);
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 5U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 3U);
            }
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
            if (opcode == 0x86U) {
                uint8_t value = 0U;
                uint8_t register_value = get_register_byte(state, register_index);
                if (status == BM_STATUS_OK)
                    status = read_operand_byte(state, &operand, &value);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 5U);
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
                    status = place_execution_clocks(state, 5U);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
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
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 2U);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
            if (opcode != 0x81U) {
                uint8_t immediate = 0;
                uint8_t left = 0;
                uint8_t result = 0U;
                if (status == BM_STATUS_OK)
                    status = read_operand_byte(state, &operand, &left);
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 1U);
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
                    } else {
                        result = operation == 1U ? (uint8_t) (left | immediate) :
                                 operation == 4U ? (uint8_t) (left & immediate) :
                                                   (uint8_t) (left ^ immediate);
                        set_logic_flags(state, result, 8);
                    }
                    status = place_execution_clocks(state, 2U);
                    if ((status == BM_STATUS_OK) && (operation != 7U))
                        status = write_operand_byte(state, &operand, result);
                }
            } else {
                uint16_t immediate = 0;
                uint16_t left = 0;
                uint16_t result = 0U;
                if (status == BM_STATUS_OK)
                    status = read_operand_word(state, &operand, &left);
                if (status == BM_STATUS_OK)
                    status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK) {
                    if (operation == 0U)
                        result = add16(state, left, immediate);
                    else if (operation == 2U)
                        result = adc16(state, left, immediate);
                    else if (operation == 3U)
                        result = sbb16(state, left, immediate);
                    else if (operation == 5U) {
                        result = (uint16_t) (left - immediate);
                        compare16(state, left, immediate);
                    } else if (operation == 7U) {
                        compare16(state, left, immediate);
                    } else {
                        result = operation == 1U ? (uint16_t) (left | immediate) :
                                 operation == 4U ? (uint16_t) (left & immediate) :
                                                   (uint16_t) (left ^ immediate);
                        set_logic_flags(state, result, 16);
                    }
                    status = place_execution_clocks(state, 2U);
                    if ((status == BM_STATUS_OK) && (operation != 7U))
                        status = write_operand_word(state, &operand, result);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &left);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &immediate);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK) {
                uint16_t extended = (uint16_t) (int16_t) (int8_t) immediate;
                uint16_t result = 0U;
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
                } else {
                    result = operation == 1U ? (uint16_t) (left | extended) :
                             operation == 4U ? (uint16_t) (left & extended) :
                                               (uint16_t) (left ^ extended);
                    set_logic_flags(state, result, 16);
                }
                status = place_execution_clocks(state, 2U);
                if ((status == BM_STATUS_OK) && (operation != 7U))
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
                mark_prefetch_flush(state);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if (status == BM_STATUS_OK) {
                state->segments[segment] = value;
                state->interrupt_inhibit = 2U;
                state->boundary_inhibit = 2U;
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 3U);
            }
            if (status == BM_STATUS_OK) {
                status = write_operand_word(state, &operand, state->segments[segment]);
                if (status == BM_STATUS_OK)
                    state->interrupt_inhibit = 2U;
                if (status == BM_STATUS_OK)
                    state->boundary_inhibit = 2U;
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
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
            if (status == BM_STATUS_OK)
                status = pop_word(state, &value);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if ((status == BM_STATUS_OK) && !operand.is_register)
                status = place_execution_clocks(state, 2U);
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
            if (status == BM_STATUS_OK)
                status = read_operand_word(state, &operand, &value);
            if ((status == BM_STATUS_OK) &&
                ((operation == 0U) || (operation == 1U))) {
                uint16_t immediate = 0;
                status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint16_t) (value & immediate), 16);
            } else if ((status == BM_STATUS_OK) && (operation == 2U)) {
                status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
                    status = write_operand_word(state, &operand,
                                                (uint16_t) ~value);
            } else if ((status == BM_STATUS_OK) && (operation == 3U)) {
                uint16_t result = (uint16_t) (0U - value);
                compare16(state, 0U, value);
                status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
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
            if ((status == BM_STATUS_OK) && (operation <= 1U) &&
                !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
            if ((status == BM_STATUS_OK) && (operation == 2U)) {
                begin_operand_execution_timeline(state);
                if (!operand.is_register)
                    status = place_execution_clocks(state, 1U);
            }
            if ((status == BM_STATUS_OK) && (operation == 4U))
                begin_operand_execution_timeline(state);
            if ((status == BM_STATUS_OK) && (operation == 6U))
                begin_operand_execution_timeline(state);
            if ((status == BM_STATUS_OK) &&
                ((operation == 3U) || (operation == 5U))) {
                uint16_t segment = 0U;
                if (operand.is_register)
                    return BM_STATUS_UNSUPPORTED;
                if (operation == 3U)
                    begin_operand_execution_timeline(state);
                status = read_word(state, operand.segment, operand.offset, &value);
                if ((status == BM_STATUS_OK) && (operation == 3U))
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    status = read_word(state, operand.segment,
                                       (uint16_t) (operand.offset + 2U), &segment);
                if ((status == BM_STATUS_OK) && (operation == 3U)) {
                    uint16_t return_ip = state->ip;
                    uint16_t return_cs = state->segments[1];

                    status = place_execution_clocks(state, 1U);
                    if (status == BM_STATUS_OK) {
                        bm_v30_bcu_suspend_prefetch(&state->bcu);
                        status = place_suspended_execution_clocks(state, 4U);
                    }
                    if (status == BM_STATUS_OK)
                        status = push_word(state, return_cs);
                    if (status == BM_STATUS_OK) {
                        state->segments[1] = segment;
                        status = place_suspended_execution_clocks(state, 2U);
                    }
                    if (status == BM_STATUS_OK)
                        status = place_suspended_execution_clocks(state, 1U);
                    if (status == BM_STATUS_OK) {
                        state->ip = value;
                        mark_prefetch_flush(state);
                        state->boundary_flush_timeline_supported = 1;
                        status = place_execution_clocks(state, 3U);
                    }
                    if (status == BM_STATUS_OK)
                        status = push_word(state, return_ip);
                } else if (status == BM_STATUS_OK) {
                    state->ip = value;
                    state->segments[1] = segment;
                    mark_prefetch_flush(state);
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
                status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
                    status = write_operand_word(state, &operand, result);
                return status;
            }
            if (operation == 2U) {
                uint16_t return_ip = state->ip;
                bm_v30_bcu_suspend_prefetch(&state->bcu);
                status = place_suspended_execution_clocks(state, 4U);
                if (status == BM_STATUS_OK) {
                    state->ip = value;
                    mark_prefetch_flush(state);
                    state->boundary_flush_timeline_supported = 1;
                    status = place_execution_clocks(state, 3U);
                }
                if (status == BM_STATUS_OK)
                    status = push_word(state, return_ip);
                return status;
            }
            if (operation == 4U) {
                /* Register targets consume one extra inherited entry state;
                 * memory targets have already occupied that position with
                 * their source read.  Both then suspend sequential prefetch
                 * before committing the new instruction pointer. */
                if (operand.is_register)
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK) {
                    bm_v30_bcu_suspend_prefetch(&state->bcu);
                    status = place_suspended_execution_clocks(state, 1U);
                }
                if (status == BM_STATUS_OK) {
                    state->ip = value;
                    mark_prefetch_flush(state);
                    state->boundary_flush_timeline_supported = 1;
                }
                return status;
            }
            if (operation == 6U) {
                /* The inherited V30 microcode performs three internal states
                 * between reading the source and writing the stack word. */
                status = place_execution_clocks(state, 3U);
                if ((status == BM_STATUS_OK) && operand.is_register &&
                    (operand.register_index == REG_SP))
                    value = (uint16_t) (state->registers[REG_SP] - 2U);
                if (status == BM_STATUS_OK)
                    status = push_word(state, value);
                return status;
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
            if (status == BM_STATUS_OK)
                status = read_operand_byte(state, &operand, &value);
            if ((status == BM_STATUS_OK) &&
                ((operation == 0U) || (operation == 1U))) {
                uint8_t immediate = 0;
                status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    set_logic_flags(state, (uint8_t) (value & immediate), 8);
            } else if ((status == BM_STATUS_OK) && (operation == 2U)) {
                status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
                    status = write_operand_byte(state, &operand,
                                                (uint8_t) ~value);
            } else if ((status == BM_STATUS_OK) && (operation == 3U)) {
                uint8_t result = (uint8_t) (0U - value);
                compare8(state, 0U, value);
                status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 1U);
            }
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
            status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = write_operand_byte(state, &operand, result);
            return status;
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
            if ((status == BM_STATUS_OK) && !operand.is_register) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 2U);
            }
            if (opcode == 0xc6U) {
                uint8_t immediate = 0;
                if (status == BM_STATUS_OK)
                    status = fetch_byte(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 2U);
                if (status == BM_STATUS_OK)
                    status = write_operand_byte(state, &operand, immediate);
            } else {
                uint16_t immediate = 0;
                if (status == BM_STATUS_OK)
                    status = fetch_word(state, &immediate);
                if (status == BM_STATUS_OK)
                    status = place_execution_clocks(state, 1U);
                if (status == BM_STATUS_OK)
                    status = write_operand_word(state, &operand, immediate);
            }
            return status;
        }
        case 0xe8: { /* CALL rel16. */
            uint16_t displacement;
            uint16_t return_ip;
            status = fetch_word(state, &displacement);
            if (status == BM_STATUS_OK) {
                return_ip = state->ip;
                begin_operand_execution_timeline(state);
            }
            if (status == BM_STATUS_OK) {
                bm_v30_bcu_suspend_prefetch(&state->bcu);
                status = place_suspended_execution_clocks(state, 4U);
            }
            if (status == BM_STATUS_OK) {
                state->ip = (uint16_t) (state->ip + (int16_t) displacement);
                mark_prefetch_flush(state);
                state->boundary_flush_timeline_supported = 1;
                status = place_execution_clocks(state, 3U);
            }
            if (status == BM_STATUS_OK)
                status = push_word(state, return_ip);
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
                mark_prefetch_flush(state);
            }
            return status;
        }
        case 0xe9: { /* JMP rel16 */
            uint16_t displacement;
            status = fetch_word(state, &displacement);
            if (status == BM_STATUS_OK) {
                state->ip = (uint16_t) (state->ip + (int16_t) displacement);
                mark_prefetch_flush(state);
            }
            return status;
        }
        case 0xeb: { /* JMP rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if (status == BM_STATUS_OK) {
                state->ip = (uint16_t) (state->ip + (int8_t) displacement);
                mark_prefetch_flush(state);
            }
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
                     ((opcode == 0xe1U) && ((state->flags & FLAG_ZF) != 0U)))) {
                    state->ip = (uint16_t) (state->ip + (int8_t) displacement);
                    mark_prefetch_flush(state);
                }
            }
            return status;
        }
        case 0xe3: { /* JCXZ rel8 */
            uint8_t displacement;
            status = fetch_byte(state, &displacement);
            if ((status == BM_STATUS_OK) && (state->registers[REG_CX] == 0U)) {
                state->ip = (uint16_t) (state->ip + (int8_t) displacement);
                mark_prefetch_flush(state);
            }
            return status;
        }
        case 0xc2: /* RET near imm16 */
        case 0xc3: { /* RET near */
            uint16_t destination = 0U;
            uint16_t adjustment = 0U;
            if (opcode == 0xc2U)
                status = fetch_word(state, &adjustment);
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                if (opcode == 0xc2U)
                    status = place_execution_clocks(state, 1U);
            }
            if (status == BM_STATUS_OK)
                status = pop_word(state, &destination);
            if (status == BM_STATUS_OK) {
                bm_v30_bcu_suspend_prefetch(&state->bcu);
                state->ip = destination;
                status = place_suspended_execution_clocks(
                    state, opcode == 0xc2U ? 2U : 1U);
            }
            if (status == BM_STATUS_OK) {
                mark_prefetch_flush(state);
                state->boundary_flush_timeline_supported = 1;
                status = place_execution_clocks(
                    state, opcode == 0xc2U ? 3U : 2U);
            }
            if (status == BM_STATUS_OK) {
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
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                status = place_execution_clocks(state, 4U);
            }
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment, operand.offset, &offset);
            if (status == BM_STATUS_OK)
                status = read_word(state, operand.segment,
                                   (uint16_t) (operand.offset + 2U), &segment);
            if (status == BM_STATUS_OK) {
                state->registers[(modrm >> 3U) & 7U] = offset;
                state->segments[opcode == 0xc4U ? 0U : 3U] = segment;
                state->interrupt_inhibit = 2U;
                state->boundary_inhibit = 2U;
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
            if (status == BM_STATUS_OK) {
                begin_operand_execution_timeline(state);
                /* RETF without an immediate enters the inherited far-return
                 * micro-routine through one additional internal state. */
                if (opcode == 0xcbU)
                    status = place_execution_clocks(state, 1U);
            }
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &destination);
            if (status == BM_STATUS_OK) {
                bm_v30_bcu_suspend_prefetch(&state->bcu);
                status = place_suspended_execution_clocks(state, 2U);
            }
            if (status == BM_STATUS_OK)
                status = place_suspended_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &segment);
            if (status == BM_STATUS_OK) {
                state->ip = destination;
                state->segments[1] = segment;
                mark_prefetch_flush(state);
                state->boundary_flush_timeline_supported = 1;
                status = place_execution_clocks(state, 2U);
            }
            if ((status == BM_STATUS_OK) && (opcode == 0xcaU))
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK) {
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
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
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
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &new_ip);
            if (status == BM_STATUS_OK) {
                bm_v30_bcu_suspend_prefetch(&state->bcu);
                status = place_suspended_execution_clocks(state, 2U);
            }
            if (status == BM_STATUS_OK)
                status = place_suspended_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = pop_word(state, &new_cs);
            if (status == BM_STATUS_OK) {
                state->ip = new_ip;
                state->segments[1] = new_cs;
                mark_prefetch_flush(state);
                state->boundary_flush_timeline_supported = 1;
                status = place_execution_clocks(state, 4U);
            }
            if (status == BM_STATUS_OK)
                status = pop_word(state, &new_flags);
            if (status == BM_STATUS_OK) {
                restore_psw(state, new_flags);
                status = place_execution_clocks(state, 1U);
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
            if (status == BM_STATUS_OK) {
                state->boundary_shift_count = count;
                state->boundary_shift_count_valid = 1;
            }
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
            if (status == BM_STATUS_OK) {
                state->boundary_shift_count = count;
                state->boundary_shift_count_valid = 1;
            }
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
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 3U);
            if (status == BM_STATUS_OK)
                status = read_byte(state, state->segments[segment], offset,
                                   BM_BUS_READ, &value);
            if (status == BM_STATUS_OK)
                set_register_byte(state, 0U, value);
            return status;
        }
        case 0xd8: /* FPO1/ESC fp-op,reg/mem. */
        case 0xd9:
        case 0xda:
        case 0xdb:
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf:
            return execute_fpo(state, opcode, segment_override, BM_808X_FPO1);
        case 0xa0: /* MOV AL,moffs8 */
        case 0xa1: /* MOV AX,moffs16 */
        case 0xa2: /* MOV moffs8,AL */
        case 0xa3: { /* MOV moffs16,AX */
            uint16_t offset;
            unsigned int segment = (segment_override >= 0) ?
                                   (unsigned int) segment_override : 3U;
            begin_operand_execution_timeline(state);
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
            status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0xa2U)
                return write_byte(state, state->segments[segment], offset,
                                  (uint8_t) state->registers[REG_AX]);
            return write_word(state, state->segments[segment], offset,
                              state->registers[REG_AX]);
        }
        case 0xe4: { /* IN AL,imm8 */
            uint8_t port = 0;
            uint8_t value = 0;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = io_read_byte(state, port, &value);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] =
                    (uint16_t) ((state->registers[REG_AX] & 0xff00U) | value);
            return status;
        }
        case 0xe5: { /* IN AX,imm8 */
            uint8_t port = 0;
            uint16_t value = 0;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = io_read_word(state, port, &value);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = value;
            return status;
        }
        case 0xe6: { /* OUT imm8,AL */
            uint8_t port = 0U;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
            return io_write_byte(state, port, (uint8_t) state->registers[REG_AX]);
        }
        case 0xe7: { /* OUT imm8,AX */
            uint8_t port = 0U;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                status = fetch_byte(state, &port);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
            return io_write_word(state, port, state->registers[REG_AX]);
        }
        case 0xec: { /* IN AL,DX */
            uint8_t value = 0;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = io_read_byte(state, state->registers[REG_DX], &value);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] =
                    (uint16_t) ((state->registers[REG_AX] & 0xff00U) | value);
            return status;
        }
        case 0xed: { /* IN AX,DX */
            uint16_t value = 0;
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 2U);
            if (status == BM_STATUS_OK)
                status = io_read_word(state, state->registers[REG_DX], &value);
            if (status == BM_STATUS_OK)
                status = place_execution_clocks(state, 1U);
            if (status == BM_STATUS_OK)
                state->registers[REG_AX] = value;
            return status;
        }
        case 0xee: /* OUT DX,AL */
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
            return io_write_byte(state, state->registers[REG_DX],
                                 (uint8_t) state->registers[REG_AX]);
        case 0xef: /* OUT DX,AX */
            begin_operand_execution_timeline(state);
            status = place_execution_clocks(state, 1U);
            if (status != BM_STATUS_OK)
                return status;
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

static int
instruction_byte(const bm_808x_state_t *state, unsigned int index,
                 uint8_t *value)
{
    if ((value == NULL) || (index >= state->last_instruction_length) ||
        (index >= 8U))
        return 0;
    *value = (uint8_t) (state->last_instruction_bytes >> (index * 8U));
    return 1;
}

static int
rm_execution_clocks(const bm_808x_state_t *state, int word,
                    uint32_t register_clocks, uint32_t byte_memory_clocks,
                    uint32_t even_word_memory_clocks,
                    uint32_t odd_word_memory_clocks, uint32_t *clocks)
{
    if (!state->boundary_rm_valid)
        return 0;
    if (!state->boundary_rm_memory) {
        *clocks = register_clocks;
        return 1;
    }
    if (!word) {
        *clocks = byte_memory_clocks;
        return 1;
    }
    *clocks = (state->boundary_rm_offset & 1U) != 0U ?
              odd_word_memory_clocks : even_word_memory_clocks;
    return 1;
}

static int
rm_execution_clock_range(const bm_808x_state_t *state, int word,
                         uint32_t register_min, uint32_t register_max,
                         uint32_t byte_memory_min, uint32_t byte_memory_max,
                         uint32_t even_word_memory_min,
                         uint32_t even_word_memory_max,
                         uint32_t odd_word_memory_min,
                         uint32_t odd_word_memory_max,
                         uint32_t *minimum, uint32_t *maximum)
{
    if (!state->boundary_rm_valid)
        return 0;
    if (!state->boundary_rm_memory) {
        *minimum = register_min;
        *maximum = register_max;
        return 1;
    }
    if (!word) {
        *minimum = byte_memory_min;
        *maximum = byte_memory_max;
        return 1;
    }
    if ((state->boundary_rm_offset & 1U) != 0U) {
        *minimum = odd_word_memory_min;
        *maximum = odd_word_memory_max;
    } else {
        *minimum = even_word_memory_min;
        *maximum = even_word_memory_max;
    }
    return 1;
}

static int
direct_address(const bm_808x_state_t *state, uint16_t *offset)
{
    uint8_t low = 0U;
    uint8_t high = 0U;
    unsigned int index = state->last_prefix_count + 1U;

    if (!instruction_byte(state, index, &low) ||
        !instruction_byte(state, index + 1U, &high))
        return 0;
    *offset = (uint16_t) (low | ((uint16_t) high << 8U));
    return 1;
}

static bm_808x_execution_clock_kind_t
documented_nec_extension_execution_clocks(const bm_808x_state_t *state,
                                          uint32_t *minimum,
                                          uint32_t *maximum)
{
    uint8_t extension = 0U;
    unsigned int extension_index = state->last_prefix_count + 1U;
    uint32_t clocks = 0U;

    if (!instruction_byte(state, extension_index, &extension))
        return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    if ((extension >= 0x10U) && (extension <= 0x1fU)) {
        unsigned int operation = (extension >> 1U) & 3U;
        int immediate = (extension & 8U) != 0U;
        int word = (extension & 1U) != 0U;
        uint32_t register_clocks;
        uint32_t byte_memory_clocks;
        uint32_t even_word_memory_clocks;
        uint32_t odd_word_memory_clocks;

        if (operation == 0U) { /* TEST1. */
            register_clocks = immediate ? 4U : 3U;
            byte_memory_clocks = immediate ? 9U : 8U;
            even_word_memory_clocks = immediate ? 9U : 8U;
            odd_word_memory_clocks = immediate ? 13U : 12U;
        } else if (operation == 1U) { /* CLR1. */
            register_clocks = immediate ? 6U : 5U;
            byte_memory_clocks = immediate ? 15U : 14U;
            even_word_memory_clocks = immediate ? 15U : 14U;
            odd_word_memory_clocks = immediate ? 23U : 22U;
        } else { /* SET1 and NOT1. */
            register_clocks = immediate ? 5U : 4U;
            byte_memory_clocks = immediate ? 14U : 13U;
            even_word_memory_clocks = immediate ? 14U : 13U;
            odd_word_memory_clocks = immediate ? 22U : 21U;
        }
        if (!rm_execution_clocks(state, word, register_clocks,
                                 byte_memory_clocks,
                                 even_word_memory_clocks,
                                 odd_word_memory_clocks, &clocks))
            return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
        *minimum = clocks;
        *maximum = clocks;
        return BM_808X_EXECUTION_CLOCKS_EXACT;
    }
    if ((extension == 0x20U) || (extension == 0x22U) ||
        (extension == 0x26U)) {
        unsigned int digits = get_register_byte(state, 1U);
        unsigned int packed_bytes;

        if ((digits == 0U) || (digits == 255U))
            return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
        packed_bytes = (digits + 1U) / 2U;
        *minimum = 19U * packed_bytes + 7U;
        *maximum = *minimum;
        return BM_808X_EXECUTION_CLOCKS_EXACT;
    }
    if ((extension == 0x28U) || (extension == 0x2aU)) {
        if (!rm_execution_clocks(state, 0,
                                 extension == 0x28U ? 13U : 17U,
                                 extension == 0x28U ? 28U : 32U,
                                 0U, 0U, &clocks))
            return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
        *minimum = clocks;
        *maximum = clocks;
        return BM_808X_EXECUTION_CLOCKS_EXACT;
    }
    if ((extension == 0x31U) || (extension == 0x33U) ||
        (extension == 0x39U) || (extension == 0x3bU)) {
        unsigned int index_register =
            ((extension == 0x31U) || (extension == 0x39U)) ? REG_DI : REG_SI;

        if ((state->registers[index_register] & 1U) != 0U) {
            *minimum = 35U;
            *maximum = 133U;
        } else {
            *minimum = 31U;
            *maximum = 117U;
        }
        return BM_808X_EXECUTION_CLOCKS_RANGE;
    }
    if (extension == 0xffU) {
        *minimum = (state->boundary_initial_sp & 1U) ? 50U : 38U;
        *maximum = *minimum;
        return BM_808X_EXECUTION_CLOCKS_EXACT;
    }
    return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
}

static bm_808x_execution_clock_kind_t
documented_string_execution_clocks(const bm_808x_state_t *state,
                                   uint8_t opcode,
                                   uint32_t *minimum,
                                   uint32_t *maximum)
{
    uint32_t base = 0U;
    uint32_t per_iteration = 0U;
    uint32_t single = 0U;
    uint32_t prefix_clocks;
    unsigned int prefix_count = state->last_prefix_count;
    int repeated;
    int word = (opcode & 1U) != 0U;
    int source_odd = (state->boundary_initial_si & 1U) != 0U;
    int destination_odd = (state->boundary_initial_di & 1U) != 0U;
    int port_odd = (state->boundary_initial_dx & 1U) != 0U;

    if (!state->boundary_string_valid || state->interrupt_entered)
        return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    repeated = state->boundary_repeat_mode != 0U;
    if (repeated) {
        if (prefix_count == 0U)
            return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
        /* NEC's primitive string formulas already include one repeat prefix. */
        --prefix_count;
    }
    prefix_clocks = (uint32_t) prefix_count * 2U;

    switch (opcode) {
        case 0xa4: case 0xa5: /* MOVS/MOVBK. */
            base = 11U;
            if (!word) {
                per_iteration = 8U;
                single = 11U;
            } else {
                unsigned int odd_addresses =
                    (unsigned int) source_odd + (unsigned int) destination_odd;
                per_iteration = 8U + 4U * odd_addresses;
                single = 11U + 4U * odd_addresses;
            }
            break;
        case 0xa6: case 0xa7: /* CMPS/CMPBK. */
            base = 7U;
            if (!word) {
                per_iteration = 14U;
                single = 13U;
            } else {
                unsigned int odd_addresses =
                    (unsigned int) source_odd + (unsigned int) destination_odd;
                per_iteration = 14U + 4U * odd_addresses;
                single = 13U + 4U * odd_addresses;
            }
            break;
        case 0xaa: case 0xab: /* STOS/STM. */
            base = 7U;
            per_iteration = (!word || !destination_odd) ? 4U : 8U;
            single = (!word || !destination_odd) ? 7U : 11U;
            break;
        case 0xac: case 0xad: /* LODS/LDM. */
            base = 7U;
            per_iteration = (!word || !source_odd) ? 9U : 13U;
            single = (!word || !source_odd) ? 7U : 11U;
            break;
        case 0xae: case 0xaf: /* SCAS/CMPM. */
            base = 7U;
            per_iteration = (!word || !destination_odd) ? 10U : 14U;
            single = (!word || !destination_odd) ? 7U : 11U;
            break;
        case 0x6c: case 0x6d: { /* INS/INM. */
            unsigned int odd_addresses = word ?
                (unsigned int) destination_odd + (unsigned int) port_odd : 0U;
            base = 9U;
            per_iteration = 8U + 4U * odd_addresses;
            single = 10U + 4U * odd_addresses;
            break;
        }
        case 0x6e: case 0x6f: { /* OUTS/OUTM. */
            unsigned int odd_addresses = word ?
                (unsigned int) source_odd + (unsigned int) port_odd : 0U;
            base = 9U;
            per_iteration = 8U + 4U * odd_addresses;
            single = 10U + 4U * odd_addresses;
            break;
        }
        default:
            return BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    }
    *minimum = prefix_clocks + (repeated ?
        base + per_iteration * state->boundary_string_iterations : single);
    *maximum = *minimum;
    return BM_808X_EXECUTION_CLOCKS_EXACT;
}

static bm_808x_execution_clock_kind_t
documented_native_execution_clocks(const bm_808x_state_t *state,
                                   uint32_t *minimum, uint32_t *maximum)
{
    uint8_t opcode = state->last_effective_opcode;
    uint32_t base = 0U;
    int known = 1;
    bm_808x_execution_clock_kind_t kind = BM_808X_EXECUTION_CLOCKS_EXACT;

    if ((opcode >= 0x40U) && (opcode <= 0x4fU))
        base = 2U; /* INC/DEC reg16. */
    else if ((opcode >= 0x50U) && (opcode <= 0x57U))
        base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
    else if ((opcode >= 0x58U) && (opcode <= 0x5fU))
        base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
    else if ((opcode >= 0x70U) && (opcode <= 0x7fU))
        base = state->bcu.boundary_prefetch_flushed ? 14U : 4U;
    else if ((opcode >= 0x91U) && (opcode <= 0x97U))
        base = 3U; /* XCHG AW,reg16. */
    else if ((opcode >= 0xb0U) && (opcode <= 0xbfU))
        base = 4U; /* MOV reg,imm. */
    else if (((opcode & 0xc4U) == 0x00U) && (opcode <= 0x3bU)) {
        int word = (opcode & 1U) != 0U;
        int compare = (opcode & 0x38U) == 0x38U;
        int memory_destination = (opcode & 2U) == 0U;

        if (compare || !memory_destination)
            known = rm_execution_clocks(state, word, 2U, 11U, 11U, 15U,
                                        &base);
        else
            known = rm_execution_clocks(state, word, 2U, 16U, 16U, 24U,
                                        &base);
    } else {
        switch (opcode) {
            case 0x0f: {
                kind = documented_nec_extension_execution_clocks(
                    state, minimum, maximum);
                if (kind != BM_808X_EXECUTION_CLOCKS_UNKNOWN) {
                    uint32_t prefix_clocks =
                        (uint32_t) state->last_prefix_count * 2U;
                    *minimum += prefix_clocks;
                    *maximum += prefix_clocks;
                    return kind;
                }
                known = 0;
                break;
            }
            case 0x06: case 0x0e: case 0x16: case 0x1e:
            case 0x07: case 0x17: case 0x1f:
            case 0x9c: case 0x9d:
                base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
                break;
            case 0x04: case 0x05: case 0x0c: case 0x0d:
            case 0x14: case 0x15: case 0x1c: case 0x1d:
            case 0x24: case 0x25: case 0x2c: case 0x2d:
            case 0x34: case 0x35: case 0x3c: case 0x3d:
            case 0xa8: case 0xa9:
                base = 4U; /* ALU accumulator,immediate. */
                break;
            case 0x27: case 0x2f:
                base = 3U; /* ADJ4A/ADJ4S. */
                break;
            case 0x37: case 0x3f:
                base = 7U; /* ADJBA/ADJBS. */
                break;
            case 0x60:
                base = (state->boundary_initial_sp & 1U) ? 67U : 35U;
                break;
            case 0x61:
                base = (state->boundary_initial_sp & 1U) ? 75U : 43U;
                break;
            case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            case 0xa4: case 0xa5: case 0xa6: case 0xa7:
            case 0xaa: case 0xab: case 0xac: case 0xad:
            case 0xae: case 0xaf:
                return documented_string_execution_clocks(
                    state, opcode, minimum, maximum);
            case 0x62:
                if (!state->boundary_rm_valid || !state->boundary_rm_memory) {
                    known = 0;
                } else if (state->interrupt_entered) {
                    if ((state->boundary_rm_offset & 1U) != 0U) {
                        *minimum = 73U;
                        *maximum = 76U;
                    } else {
                        *minimum = 53U;
                        *maximum = 56U;
                    }
                    *minimum += (uint32_t) state->last_prefix_count * 2U;
                    *maximum += (uint32_t) state->last_prefix_count * 2U;
                    return BM_808X_EXECUTION_CLOCKS_RANGE;
                } else {
                    base = (state->boundary_rm_offset & 1U) ? 26U : 18U;
                }
                break;
            case 0x66: case 0x67:
                known = rm_execution_clocks(state, 1, 2U, 0U, 11U, 15U,
                                            &base);
                break;
            case 0x68:
                base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
                break;
            case 0x69: case 0x6b: {
                uint32_t range_min = 0U;
                uint32_t range_max = 0U;
                if (opcode == 0x6bU)
                    known = rm_execution_clock_range(
                        state, 1, 28U, 34U, 0U, 0U, 34U, 40U, 38U, 44U,
                        &range_min, &range_max);
                else
                    known = rm_execution_clock_range(
                        state, 1, 36U, 42U, 0U, 0U, 42U, 48U, 46U, 52U,
                        &range_min, &range_max);
                if (known) {
                    *minimum = range_min + (uint32_t) state->last_prefix_count * 2U;
                    *maximum = range_max + (uint32_t) state->last_prefix_count * 2U;
                    return BM_808X_EXECUTION_CLOCKS_RANGE;
                }
                break;
            }
            case 0x6a:
                base = (state->boundary_initial_sp & 1U) ? 11U : 7U;
                break;
            case 0x80: case 0x81: case 0x82: case 0x83: {
                uint8_t modrm = 0U;
                int word = (opcode == 0x81U) || (opcode == 0x83U);
                if (!instruction_byte(state, state->last_prefix_count + 1U,
                                      &modrm)) {
                    known = 0;
                    break;
                }
                if (((modrm >> 3U) & 7U) == 7U)
                    known = rm_execution_clocks(state, word, 4U, 13U, 13U,
                                                17U, &base);
                else
                    known = rm_execution_clocks(state, word, 4U, 18U, 18U,
                                                26U, &base);
                break;
            }
            case 0x86: case 0x87: {
                known = rm_execution_clocks(state, opcode == 0x87U, 3U, 16U,
                                            16U, 24U, &base);
                break;
            }
            case 0x84: case 0x85:
                known = rm_execution_clocks(state, opcode == 0x85U, 2U, 10U,
                                            10U, 14U, &base);
                break;
            case 0x88: case 0x89:
                known = rm_execution_clocks(state, opcode == 0x89U, 2U, 9U,
                                            9U, 13U, &base);
                break;
            case 0x8a: case 0x8b:
                known = rm_execution_clocks(state, opcode == 0x8bU, 2U, 11U,
                                            11U, 15U, &base);
                break;
            case 0x8c:
                known = rm_execution_clocks(state, 1, 2U, 0U, 10U, 14U,
                                            &base);
                break;
            case 0x8d:
                base = 4U; /* LDEA/LEA does not transfer the operand. */
                break;
            case 0x8e:
                known = rm_execution_clocks(state, 1, 2U, 0U, 11U, 15U,
                                            &base);
                break;
            case 0x8f: {
                if (state->boundary_rm_valid && !state->boundary_rm_memory)
                    base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
                else if (state->boundary_rm_valid) {
                    base = 17U;
                    if ((state->boundary_initial_sp & 1U) != 0U)
                        base += 4U;
                    if ((state->boundary_rm_offset & 1U) != 0U)
                        base += 4U;
                } else
                    known = 0;
                break;
            }
            case 0x90: base = 3U; break; /* NOP. */
            case 0x98: base = 2U; break; /* CVTBW/CBW. */
            case 0x99:
                *minimum = 4U + (uint32_t) state->last_prefix_count * 2U;
                *maximum = 5U + (uint32_t) state->last_prefix_count * 2U;
                return BM_808X_EXECUTION_CLOCKS_RANGE;
            case 0x9a:
                base = (state->boundary_initial_sp & 1U) ? 29U : 21U;
                break;
            case 0x9b:
                if (state->boundary_poll_ready)
                    base = 7U;
                else
                    known = 0;
                break;
            case 0x9e: base = 3U; break; /* MOV PSW,AH. */
            case 0x9f: base = 2U; break; /* MOV AH,PSW. */
            case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
                uint16_t address = 0U;
                int word = (opcode & 1U) != 0U;
                int store = (opcode & 2U) != 0U;
                if (!direct_address(state, &address)) {
                    known = 0;
                    break;
                }
                if (!word)
                    base = store ? 9U : 10U;
                else if (address & 1U)
                    base = store ? 13U : 14U;
                else
                    base = store ? 9U : 10U;
                break;
            }
            case 0xc0: case 0xc1: case 0xd0: case 0xd1:
            case 0xd2: case 0xd3: {
                uint32_t count;
                int word = (opcode & 1U) != 0U;
                if (!state->boundary_shift_count_valid) {
                    known = 0;
                    break;
                }
                count = state->boundary_shift_count;
                if ((opcode == 0xd0U) || (opcode == 0xd1U)) {
                    known = rm_execution_clocks(state, word, 6U, 16U, 16U,
                                                24U, &base);
                    break;
                }
                known = rm_execution_clocks(state, word, 7U + count,
                                            19U + count, 19U + count,
                                            27U + count, &base);
                break;
            }
            case 0xc2: base = (state->boundary_initial_sp & 1U) ? 24U : 20U; break;
            case 0xc3: base = (state->boundary_initial_sp & 1U) ? 19U : 15U; break;
            case 0xc4: case 0xc5:
                if (!state->boundary_rm_valid || !state->boundary_rm_memory)
                    known = 0;
                else
                    base = (state->boundary_rm_offset & 1U) ? 26U : 18U;
                break;
            case 0xc6: case 0xc7:
                known = rm_execution_clocks(state, opcode == 0xc7U, 4U, 11U,
                                            11U, 15U, &base);
                break;
            case 0xc8: {
                uint8_t nesting = 0U;
                unsigned int index = state->last_prefix_count + 3U;
                if (!instruction_byte(state, index, &nesting)) {
                    known = 0;
                } else if (nesting == 0U) {
                    base = (state->boundary_initial_sp & 1U) ? 16U : 12U;
                } else if ((state->boundary_initial_sp & 1U) != 0U) {
                    base = 21U + 16U * (uint32_t) (nesting - 1U);
                } else {
                    base = 17U + 8U * (uint32_t) (nesting - 1U);
                }
                break;
            }
            case 0xc9:
                base = (state->boundary_initial_bp & 1U) ? 10U : 6U;
                break;
            case 0xca: base = (state->boundary_initial_sp & 1U) ? 32U : 24U; break;
            case 0xcb: base = (state->boundary_initial_sp & 1U) ? 29U : 21U; break;
            case 0xcc: case 0xcd:
                base = (state->boundary_initial_sp & 1U) ? 50U : 38U;
                break;
            case 0xce:
                base = state->bcu.boundary_prefetch_flushed ?
                       ((state->boundary_initial_sp & 1U) ? 52U : 40U) : 3U;
                break;
            case 0xcf:
                base = (state->boundary_initial_sp & 1U) ? 39U : 27U;
                break;
            case 0xd4: base = 15U; break; /* CVTBD/AAM. */
            case 0xd5: base = 7U; break;  /* CVTDB/AAD. */
            case 0xd7: base = 9U; break;  /* TRANS/XLAT. */
            case 0xd8: case 0xd9: case 0xda: case 0xdb:
            case 0xdc: case 0xdd: case 0xde: case 0xdf:
                known = rm_execution_clocks(state, 1, 2U, 0U, 11U, 15U,
                                            &base);
                break;
            case 0xe0: case 0xe1: case 0xe2:
                base = state->bcu.boundary_prefetch_flushed ? 13U : 5U;
                break;
            case 0xe3:
                base = state->bcu.boundary_prefetch_flushed ? 13U : 5U;
                break;
            case 0xe4: base = 9U; break;
            case 0xe5: {
                uint8_t port = 0U;
                if (instruction_byte(state, state->last_prefix_count + 1U,
                                     &port))
                    base = (port & 1U) ? 13U : 9U;
                else
                    known = 0;
                break;
            }
            case 0xe6: base = 8U; break;
            case 0xe7: {
                uint8_t port = 0U;
                if (instruction_byte(state, state->last_prefix_count + 1U,
                                     &port))
                    base = (port & 1U) ? 12U : 8U;
                else
                    known = 0;
                break;
            }
            case 0xe8:
                base = (state->boundary_initial_sp & 1U) ? 20U : 16U;
                break;
            case 0xe9: base = 13U; break;
            case 0xea: base = 15U; break;
            case 0xeb: base = 12U; break;
            case 0xec: base = 8U; break;
            case 0xed:
                base = (state->registers[REG_DX] & 1U) ? 12U : 8U;
                break;
            case 0xee: base = 8U; break;
            case 0xef:
                base = (state->registers[REG_DX] & 1U) ? 12U : 8U;
                break;
            case 0xf6: case 0xf7: {
                uint8_t modrm = 0U;
                unsigned int operation;
                int word = opcode == 0xf7U;
                if (!instruction_byte(state, state->last_prefix_count + 1U,
                                      &modrm)) {
                    known = 0;
                    break;
                }
                operation = (modrm >> 3U) & 7U;
                if (((operation == 6U) || (operation == 7U)) &&
                    state->interrupt_entered) {
                    known = 0;
                    break;
                }
                if (operation <= 1U)
                    known = rm_execution_clocks(state, word, 4U, 11U, 11U,
                                                15U, &base);
                else if ((operation == 2U) || (operation == 3U))
                    known = rm_execution_clocks(state, word, 2U, 16U, 16U,
                                                24U, &base);
                else if (operation == 6U) {
                    if (!state->boundary_rm_valid)
                        known = 0;
                    else if (!word)
                        base = state->boundary_rm_memory ? 25U : 19U;
                    else if (!state->boundary_rm_memory)
                        base = 25U;
                    else
                        base = (state->boundary_rm_offset & 1U) ? 34U : 30U;
                } else if ((operation == 4U) || (operation == 5U) ||
                           (operation == 7U)) {
                    uint32_t range_min = 0U;
                    uint32_t range_max = 0U;
                    if (operation == 4U) {
                        if (word)
                            known = rm_execution_clock_range(
                                state, 1, 29U, 30U, 0U, 0U, 35U, 36U,
                                39U, 40U, &range_min, &range_max);
                        else
                            known = rm_execution_clock_range(
                                state, 0, 21U, 22U, 27U, 28U, 0U, 0U,
                                0U, 0U, &range_min, &range_max);
                        /* NEC documents the one-clock range as data-dependent.
                         * The inherited V30 microcode spends the additional
                         * clock when the unsigned product has no high half. */
                        if (known) {
                            int high_half_is_zero = word ?
                                state->registers[REG_DX] == 0U :
                                (state->registers[REG_AX] & 0xff00U) == 0U;
                            base = range_min +
                                   (high_half_is_zero ? 1U : 0U);
                            kind = BM_808X_EXECUTION_CLOCKS_EXACT;
                        }
                    } else if (operation == 5U) {
                        if (word)
                            known = rm_execution_clock_range(
                                state, 1, 41U, 47U, 0U, 0U, 47U, 53U,
                                51U, 57U, &range_min, &range_max);
                        else
                            known = rm_execution_clock_range(
                                state, 0, 33U, 39U, 39U, 45U, 0U, 0U,
                                0U, 0U, &range_min, &range_max);
                    } else if (word) {
                        known = rm_execution_clock_range(
                            state, 1, 38U, 43U, 0U, 0U, 43U, 48U,
                            47U, 52U, &range_min, &range_max);
                    } else {
                        known = rm_execution_clock_range(
                            state, 0, 29U, 34U, 34U, 39U, 0U, 0U,
                            0U, 0U, &range_min, &range_max);
                    }
                    if (known && (operation != 4U)) {
                        *minimum = range_min +
                                   (uint32_t) state->last_prefix_count * 2U;
                        *maximum = range_max +
                                   (uint32_t) state->last_prefix_count * 2U;
                        return BM_808X_EXECUTION_CLOCKS_RANGE;
                    }
                } else {
                    known = 0;
                }
                break;
            }
            case 0xfe:
                known = rm_execution_clocks(state, 0, 2U, 16U, 0U, 0U,
                                            &base);
                break;
            case 0xff: {
                uint8_t modrm = 0U;
                unsigned int operation;
                if (!instruction_byte(state, state->last_prefix_count + 1U,
                                      &modrm)) {
                    known = 0;
                    break;
                }
                operation = (modrm >> 3U) & 7U;
                if ((operation == 0U) || (operation == 1U))
                    known = rm_execution_clocks(state, 1, 2U, 0U, 16U, 24U,
                                                &base);
                else if ((operation == 2U) && state->boundary_rm_valid) {
                    if (!state->boundary_rm_memory) {
                        base = (state->boundary_initial_sp & 1U) ? 18U : 14U;
                    } else {
                        base = 23U;
                        if ((state->boundary_rm_offset & 1U) != 0U)
                            base += 4U;
                        if ((state->boundary_initial_sp & 1U) != 0U)
                            base += 4U;
                    }
                }
                else if ((operation == 3U) && state->boundary_rm_valid &&
                         state->boundary_rm_memory) {
                    base = 31U;
                    if ((state->boundary_rm_offset & 1U) != 0U)
                        base += 8U;
                    if ((state->boundary_initial_sp & 1U) != 0U)
                        base += 8U;
                }
                else if (operation == 4U)
                    known = rm_execution_clocks(state, 1, 11U, 0U, 20U, 24U,
                                                &base);
                else if (operation == 5U)
                    known = rm_execution_clocks(state, 1, 0U, 0U, 27U, 35U,
                                                &base);
                else if ((operation == 6U) && state->boundary_rm_valid) {
                    if (!state->boundary_rm_memory) {
                        base = (state->boundary_initial_sp & 1U) ? 12U : 8U;
                    } else {
                        base = 18U;
                        if ((state->boundary_rm_offset & 1U) != 0U)
                            base += 4U;
                        if ((state->boundary_initial_sp & 1U) != 0U)
                            base += 4U;
                    }
                }
                else
                    known = 0;
                break;
            }
            case 0xf4: case 0xf5: case 0xf8: case 0xf9:
            case 0xfa: case 0xfb: case 0xfc: case 0xfd:
                base = 2U;
                break;
            default:
                known = 0;
                break;
        }
    }
    if (!known)
        kind = BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    if (kind == BM_808X_EXECUTION_CLOCKS_EXACT) {
        *minimum = base + (uint32_t) state->last_prefix_count * 2U;
        *maximum = *minimum;
    }
    return kind;
}

static bm_status_t
advance_uncontended_prefetch(bm_808x_state_t *state, int native_mode)
{
    uint32_t minimum = 0U;
    uint32_t maximum = 0U;
    bm_808x_execution_clock_kind_t kind;

    if (!native_mode)
        return BM_STATUS_OK;
    if (state->boundary_execution_timeline_active) {
        bm_status_t status;

        if (state->bcu.boundary_prefetch_flushed &&
            !state->boundary_flush_timeline_supported)
            return BM_STATUS_OK;
        if ((state->bcu.boundary_bus_transactions !=
             state->bcu.boundary_prefetch_transactions) &&
            !state->boundary_operand_timeline_supported)
            return BM_STATUS_OK;
        kind = documented_native_execution_clocks(state, &minimum, &maximum);
        if ((kind != BM_808X_EXECUTION_CLOCKS_EXACT) ||
            (minimum != maximum) ||
            (state->boundary_execution_clocks_placed > minimum))
            return BM_STATUS_OK;
        status = place_execution_clocks(
            state, minimum - state->boundary_execution_clocks_placed);
        if (status == BM_STATUS_OK)
            state->boundary_execution_timeline_complete = 1;
        return status;
    }
    if (state->bcu.boundary_prefetch_flushed)
        return BM_STATUS_OK;
    /* Operand and I/O cycles need their position inside the instruction before
     * the BCU may compete with them. Demand and already-running prefetches are
     * the only bus work admitted by this first overlap cut. */
    if (state->bcu.boundary_bus_transactions !=
        state->bcu.boundary_prefetch_transactions)
        return BM_STATUS_OK;
    kind = documented_native_execution_clocks(state, &minimum, &maximum);
    if ((kind != BM_808X_EXECUTION_CLOCKS_EXACT) || (minimum != maximum))
        return BM_STATUS_OK;
    return bm_v30_bcu_advance_prefetch(
        &state->bcu, state->bus, state->segments[1],
        state->bus_lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U,
        minimum);
}

static bm_808x_prefetch_phase_t
observed_prefetch_phase(const bm_v30_bcu_t *bcu)
{
    switch (bcu->prefetch_phase) {
        case BM_V30_BCU_PHASE_IDLE: return BM_808X_PREFETCH_IDLE;
        case BM_V30_BCU_PHASE_T1: return BM_808X_PREFETCH_T1;
        case BM_V30_BCU_PHASE_T2: return BM_808X_PREFETCH_T2;
        case BM_V30_BCU_PHASE_T3: return BM_808X_PREFETCH_T3;
        case BM_V30_BCU_PHASE_TW: return BM_808X_PREFETCH_TW;
        case BM_V30_BCU_PHASE_T4: return BM_808X_PREFETCH_T4;
        default: return BM_808X_PREFETCH_IDLE;
    }
}

static void
compose_boundary_clocks(const bm_808x_state_t *state, int native_mode,
                        bm_808x_timing_observation_t *observation)
{
    uint64_t fixed_clocks;

    observation->boundary_clock_kind = BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    if (((observation->kind != BM_808X_BOUNDARY_INSTRUCTION) &&
         (observation->kind != BM_808X_BOUNDARY_INTERRUPT)) || !native_mode ||
        (observation->execution_clock_kind ==
         BM_808X_EXECUTION_CLOCKS_UNKNOWN))
        return;

    if (state->bcu.boundary_bus_transactions !=
        state->bcu.boundary_prefetch_transactions) {
        uint64_t operand_base_clocks;
        uint64_t operand_wait_clocks;

        if (!state->boundary_execution_timeline_complete ||
            (state->bcu.boundary_operand_transactions > UINT64_MAX / 4U))
            return;
        operand_base_clocks =
            state->bcu.boundary_operand_transactions * UINT64_C(4);
        if (state->bcu.boundary_operand_bus_clocks < operand_base_clocks)
            return;
        operand_wait_clocks =
            state->bcu.boundary_operand_bus_clocks - operand_base_clocks;
        if (state->bcu.boundary_demand_prefetch_bus_clocks >
            UINT64_MAX - state->bcu.boundary_instruction_queue_reads)
            return;
        fixed_clocks = state->bcu.boundary_demand_prefetch_bus_clocks +
                       state->bcu.boundary_instruction_queue_reads;
        if (fixed_clocks >
            UINT64_MAX - state->bcu.boundary_prefetch_handoff_clocks)
            return;
        fixed_clocks += state->bcu.boundary_prefetch_handoff_clocks;
        if (fixed_clocks > UINT64_MAX - operand_wait_clocks)
            return;
        fixed_clocks += operand_wait_clocks;
    } else {
        /* Demand prefetch is a real stall and already includes its wait
         * states. Later prefetch phases run inside the documented EXU
         * interval, while every consumed queue byte contributes its
         * documented pre-decode clock. */
        fixed_clocks = state->bcu.boundary_demand_prefetch_bus_clocks +
                       state->bcu.boundary_instruction_queue_reads;
    }

    if ((fixed_clocks > UINT64_MAX - observation->execution_clocks_min) ||
        (fixed_clocks > UINT64_MAX - observation->execution_clocks_max))
        return;
    observation->boundary_clocks_min =
        fixed_clocks + observation->execution_clocks_min;
    observation->boundary_clocks_max =
        fixed_clocks + observation->execution_clocks_max;
    observation->boundary_clock_kind = observation->execution_clock_kind;
}

static void
begin_boundary_observation(bm_808x_state_t *state)
{
    bm_v30_bcu_begin_boundary(&state->bcu);
    state->boundary_rm_valid = 0;
    state->boundary_rm_memory = 0;
    state->boundary_rm_offset = 0U;
    state->boundary_initial_sp = state->registers[REG_SP];
    state->boundary_initial_bp = state->registers[REG_BP];
    state->boundary_initial_si = state->registers[REG_SI];
    state->boundary_initial_di = state->registers[REG_DI];
    state->boundary_initial_dx = state->registers[REG_DX];
    state->boundary_string_iterations = 0U;
    state->boundary_repeat_mode = 0U;
    state->boundary_string_valid = 0;
    state->boundary_shift_count = 0U;
    state->boundary_shift_count_valid = 0;
    state->boundary_execution_clocks_placed = 0U;
    state->boundary_execution_timeline_active = 0;
    state->boundary_execution_timeline_complete = 0;
    state->boundary_operand_timeline_supported = 0;
    state->boundary_flush_timeline_supported = 0;
    state->boundary_interrupt_execution_clocks = 0U;
    state->boundary_poll_ready = 0;
    state->last_boundary_observed = 0;
}

static void
emit_boundary_observation(bm_808x_state_t *state,
                          bm_808x_boundary_kind_t kind,
                          int native_mode)
{
    uint64_t operand_base_clocks = UINT64_MAX;
    uint64_t operand_wait_states = 0U;

    if (state->bcu.boundary_operand_transactions <= UINT64_MAX / 4U) {
        operand_base_clocks =
            state->bcu.boundary_operand_transactions * UINT64_C(4);
        if (state->bcu.boundary_operand_bus_clocks >= operand_base_clocks)
            operand_wait_states =
                state->bcu.boundary_operand_bus_clocks - operand_base_clocks;
    }
    bm_808x_timing_observation_t observation = {
        .size = sizeof(observation),
        .version = BM_808X_TIMING_OBSERVATION_VERSION,
        .kind = kind,
        .opcode = kind == BM_808X_BOUNDARY_INSTRUCTION ?
                  state->last_opcode : 0U,
        .effective_opcode = kind == BM_808X_BOUNDARY_INSTRUCTION ?
                            state->last_effective_opcode : 0U,
        .prefix_count = kind == BM_808X_BOUNDARY_INSTRUCTION ?
                        state->last_prefix_count : 0U,
        .prefetch_queue_flushed =
            (uint8_t) !!state->bcu.boundary_prefetch_flushed,
        .prefetch_pointer_known = 1U,
        .prefetch_queue_capacity = BM_808X_V30_PREFETCH_QUEUE_CAPACITY,
        .prefetch_queue_count = bm_v30_bcu_queue_count(&state->bcu),
        .prefetch_pointer = bm_v30_bcu_prefetch_pointer(&state->bcu),
        .logical_bus_transactions = state->bcu.boundary_bus_transactions,
        .reported_wait_states = state->bcu.boundary_wait_states,
        .bus_active_clocks = state->bcu.boundary_bus_active_clocks,
        .demand_prefetch_transactions =
            state->bcu.boundary_demand_prefetch_transactions,
        .demand_prefetch_bus_clocks =
            state->bcu.boundary_demand_prefetch_bus_clocks,
        .instruction_queue_reads = kind == BM_808X_BOUNDARY_INSTRUCTION ?
            state->bcu.boundary_instruction_queue_reads : 0U,
        .prefetch_phase = observed_prefetch_phase(&state->bcu),
        .prefetch_transactions = state->bcu.boundary_prefetch_transactions,
        .prefetch_phase_clocks = state->bcu.boundary_prefetch_phase_clocks,
        .operand_transactions = state->bcu.boundary_operand_transactions,
        .operand_bus_clocks = state->bcu.boundary_operand_bus_clocks,
        .prefetch_handoff_clocks =
            state->bcu.boundary_prefetch_handoff_clocks,
        .execution_timeline_complete =
            (uint8_t) !!state->boundary_execution_timeline_complete,
        .execution_clocks_placed =
            state->boundary_execution_clocks_placed,
        .operand_wait_states = operand_wait_states
    };

    if ((kind == BM_808X_BOUNDARY_INSTRUCTION) && native_mode)
        observation.execution_clock_kind =
            documented_native_execution_clocks(
                state, &observation.execution_clocks_min,
                &observation.execution_clocks_max);
    else if ((kind == BM_808X_BOUNDARY_INTERRUPT) && native_mode &&
             (state->boundary_interrupt_execution_clocks != 0U)) {
        observation.execution_clock_kind = BM_808X_EXECUTION_CLOCKS_EXACT;
        observation.execution_clocks_min =
            state->boundary_interrupt_execution_clocks;
        observation.execution_clocks_max =
            state->boundary_interrupt_execution_clocks;
    }
    compose_boundary_clocks(state, native_mode, &observation);
    state->last_boundary_clock_kind = observation.boundary_clock_kind;
    state->last_boundary_clocks_min = observation.boundary_clocks_min;
    state->last_boundary_clocks_max = observation.boundary_clocks_max;
    state->last_boundary_observed = 1;
    if (state->timing != NULL)
        state->timing(state->timing_context, &observation);
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
        int trap_was_enabled;
        int native_mode;

        if (boundary_interrupt_ready(state)) {
            begin_boundary_observation(state);
            status = service_boundary_interrupt(state);
            if (status != BM_STATUS_OK)
                return status;
            emit_boundary_observation(state, BM_808X_BOUNDARY_INTERRUPT, 1);
            ++*consumed;
            continue;
        }
        if (state->halted)
            return BM_STATUS_IDLE;
        trap_was_enabled = (state->flags & FLAG_TF) != 0U;
        native_mode = (state->flags & FLAG_MD) != 0U;
        state->interrupt_entered = 0;
        begin_boundary_observation(state);
        status = execute_one(state);
        state->bus_lock_active = 0;
        if (status != BM_STATUS_OK)
            return status;
        if (state->interrupt_inhibit != 0U)
            --state->interrupt_inhibit;
        if (state->boundary_inhibit != 0U)
            --state->boundary_inhibit;
        if (trap_was_enabled && !state->interrupt_entered &&
            (state->boundary_inhibit == 0U))
            state->trap_pending = 1;
        status = advance_uncontended_prefetch(state, native_mode);
        if (status != BM_STATUS_OK)
            return status;
        emit_boundary_observation(state, BM_808X_BOUNDARY_INSTRUCTION,
                                  native_mode);
        ++*consumed;
        if (state->halted) {
            if (boundary_interrupt_ready(state)) {
                state->halted = 0;
                continue;
            }
            /* In emulation mode an asserted INT also releases HLT when IE is
             * clear; execution resumes without entering an interrupt. */
            if (((state->flags & FLAG_MD) == 0U) &&
                state->interrupt_asserted) {
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
    if (line == BM_808X_SIGNAL_INT) {
        state->interrupt_asserted = !!asserted;
        if (asserted && (maskable_interrupt_ready(state) ||
                         (state->halted &&
                          ((state->flags & FLAG_MD) == 0U))))
            state->halted = 0;
        return BM_STATUS_OK;
    }
    if (line == BM_808X_SIGNAL_NMI) {
        if (asserted && !state->nmi_line_asserted)
            state->nmi_pending = 1;
        state->nmi_line_asserted = !!asserted;
        if (nmi_ready(state))
            state->halted = 0;
        return BM_STATUS_OK;
    }
    return BM_STATUS_UNSUPPORTED;
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
    else if (strcmp(name, "prefetch_pointer") == 0)
        *value = bm_v30_bcu_prefetch_pointer(&state->bcu);
    else if (strcmp(name, "prefetch_queue_count") == 0)
        *value = bm_v30_bcu_queue_count(&state->bcu);
    else if (strcmp(name, "flags") == 0)
        *value = psw_image(state->flags);
    else if (strcmp(name, "md_write_enabled") == 0)
        *value = (uint64_t) state->md_write_enabled;
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
    state->fpo = config->fpo;
    state->poll = config->poll;
    state->coprocessor_context = config->coprocessor_context;
    state->timing = config->timing;
    state->timing_context = config->timing_context;
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
        .size = sizeof(*out_state),
        .version = BM_808X_ARCH_STATE_VERSION,
        .model = state->model,
        .ax = state->registers[REG_AX],
        .cx = state->registers[REG_CX],
        .dx = state->registers[REG_DX],
        .bx = state->registers[REG_BX],
        .sp = state->registers[REG_SP],
        .bp = state->registers[REG_BP],
        .si = state->registers[REG_SI],
        .di = state->registers[REG_DI],
        .es = state->segments[0],
        .cs = state->segments[1],
        .ss = state->segments[2],
        .ds = state->segments[3],
        .ip = state->ip,
        .flags = psw_image(state->flags),
        .halted = (uint8_t) !!state->halted,
        .interrupt_inhibit = state->interrupt_inhibit,
        .boundary_inhibit = state->boundary_inhibit,
        .nmi_pending = (uint8_t) !!state->nmi_pending,
        .trap_pending = (uint8_t) !!state->trap_pending,
        .md_write_enabled = (uint8_t) !!state->md_write_enabled
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
        (state_image->interrupt_inhibit > 1U) ||
        (state_image->boundary_inhibit > 1U) ||
        (state_image->nmi_pending > 1U) ||
        (state_image->trap_pending > 1U) ||
        (state_image->md_write_enabled > 1U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (((state_image->flags & FLAG_MD) == 0U) &&
        !state_image->md_write_enabled)
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
    bm_v30_bcu_reset(&state->bcu, state_image->ip);
    state->flags = psw_image(state_image->flags);
    state->halted = state_image->halted;
    state->interrupt_inhibit = state_image->interrupt_inhibit;
    state->boundary_inhibit = state_image->boundary_inhibit;
    state->nmi_pending = state_image->nmi_pending;
    state->trap_pending = state_image->trap_pending;
    state->md_write_enabled = state_image->md_write_enabled;
    state->last_fetch = physical_address(state_image->cs, state_image->ip);
    state->last_opcode = 0U;
    state->last_effective_opcode = 0U;
    state->last_instruction_bytes = 0U;
    state->last_instruction_length = 0U;
    state->last_prefix_count = 0U;
    state->interrupt_entered = 0;
    state->bus_lock_active = 0;
    state->boundary_rm_valid = 0;
    state->boundary_rm_memory = 0;
    state->boundary_rm_offset = 0U;
    state->boundary_initial_sp = 0U;
    state->boundary_initial_bp = 0U;
    state->boundary_initial_si = 0U;
    state->boundary_initial_di = 0U;
    state->boundary_initial_dx = 0U;
    state->boundary_string_iterations = 0U;
    state->boundary_repeat_mode = 0U;
    state->boundary_string_valid = 0;
    state->boundary_shift_count = 0U;
    state->boundary_shift_count_valid = 0;
    state->boundary_execution_clocks_placed = 0U;
    state->boundary_execution_timeline_active = 0;
    state->boundary_execution_timeline_complete = 0;
    state->boundary_operand_timeline_supported = 0;
    state->boundary_flush_timeline_supported = 0;
    state->boundary_interrupt_execution_clocks = 0U;
    state->last_boundary_clock_kind = BM_808X_EXECUTION_CLOCKS_UNKNOWN;
    state->last_boundary_clocks_min = 0U;
    state->last_boundary_clocks_max = 0U;
    state->last_boundary_observed = 0;
    return BM_STATUS_OK;
}

bm_status_t
bm_808x_step(bm_cpu_t *cpu, bm_tick_t *consumed)
{
    if (!is_808x_cpu(cpu))
        return BM_STATUS_INVALID_ARGUMENT;
    return cpu_run(cpu->context, 1U, consumed);
}

bm_status_t
bm_808x_step_clocked(void *context, bm_tick_t start_ns, uint64_t *cycles)
{
    bm_808x_state_t *state = context;
    bm_tick_t consumed = 0U;
    bm_status_t status;

    (void) start_ns;
    if ((state == NULL) || (cycles == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *cycles = 0U;
    state->last_boundary_observed = 0;
    status = cpu_run(state, 1U, &consumed);
    if ((status == BM_STATUS_IDLE) && (consumed == 0U))
        return BM_STATUS_IDLE;
    if ((status != BM_STATUS_OK) && (status != BM_STATUS_IDLE))
        return status;
    if ((consumed != 1U) || !state->last_boundary_observed)
        return BM_STATUS_DEVICE_ERROR;
    if ((state->last_boundary_clock_kind != BM_808X_EXECUTION_CLOCKS_EXACT) ||
        (state->last_boundary_clocks_min == 0U) ||
        (state->last_boundary_clocks_min != state->last_boundary_clocks_max))
        return BM_STATUS_UNSUPPORTED;
    *cycles = state->last_boundary_clocks_min;
    return BM_STATUS_OK;
}
