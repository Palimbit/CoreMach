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
            unsigned int register_index = (opcode - 0xb0U) & 3U;
            if (opcode < 0xb4U)
                state->registers[register_index] =
                    (uint16_t) ((state->registers[register_index] & 0xff00U) | immediate);
            else
                state->registers[register_index] =
                    (uint16_t) ((state->registers[register_index] & 0x00ffU) |
                                ((uint16_t) immediate << 8U));
        }
        return status;
    }

    switch (opcode) {
        case 0x90: /* NOP */
            return BM_STATUS_OK;
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
        case 0xa2: { /* MOV moffs8,AL */
            uint16_t offset;
            status = fetch_word(state, &offset);
            if (status != BM_STATUS_OK)
                return status;
            return write_byte(state, state->segments[3], offset,
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
            state->flags &= 0xfdffU;
            return BM_STATUS_OK;
        case 0xfb: /* STI */
            state->flags |= 0x0200U;
            return BM_STATUS_OK;
        case 0xfc: /* CLD */
            state->flags &= 0xfbffU;
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
    if (asserted)
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
    else if (strcmp(name, "cs") == 0)
        *value = state->segments[1];
    else if (strcmp(name, "ds") == 0)
        *value = state->segments[3];
    else if (strcmp(name, "dx") == 0)
        *value = state->registers[REG_DX];
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
    *out_cpu = (bm_cpu_t) {
        "nec-v30-bring-up",
        state,
        { cpu_reset, cpu_run, cpu_signal, cpu_inspect, cpu_destroy }
    };
    return BM_STATUS_OK;
}
