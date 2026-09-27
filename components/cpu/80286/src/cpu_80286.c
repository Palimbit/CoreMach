/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 *
 * Instance-owned partial 80286 interpreter. Derived
 * rewrite references at BluMach 87c3fb4876eaad086921bc3444569da026286c36:
 * src/cpu/x86.c, src/cpu/386.c, src/cpu/386_ops.h, src/cpu/x86seg.c,
 * src/cpu/x86_ops_pmode.h, src/cpu/x86_ops_rep_286_2386.h,
 * src/cpu/x86_ops_mov.h, src/cpu/x86_ops_mov_seg.h, src/cpu/x86_ops_xchg.h,
 * src/cpu/x86_ops_arith.h, src/cpu/x86_ops_inc_dec.h, src/cpu/x86_ops_shift.h,
 * src/cpu/x86_ops_misc.h, src/cpu/x86_ops_mul.h, src/cpu/x86_ops_bcd.h,
 * src/cpu/x86_flags.h, src/cpu/x86_ops_stack.h, src/cpu/x86_ops_string.h,
 * src/cpu/x86_ops_jump.h, src/cpu/x86_ops_call.h and
 * src/cpu/x86_ops_ret_2386.h, src/cpu/x86_ops_flag.h,
 * src/cpu/x86_ops_flag_2386.h, src/cpu/x86_ops_int.h and src/cpu/x86_ops_io.h. The inherited
 * sources retain these notices:
 *
 * src/cpu/x86.c — Authors: Andrew Jenner, Miran Grca.
 * Copyright 2015-2020 Andrew Jenner.
 * Copyright 2016-2020 Miran Grca.
 * BluMach modifications: rtzor, Project BluMach, 2026.
 *
 * src/cpu/x86seg.c — Authors: Sarah Walker, Miran Grca.
 * Copyright 2008-2018 Sarah Walker.
 * Copyright 2016-2018 Miran Grca.
 *
 * src/cpu/386_ops.h — Authors: Fred N. van Kempen, Sarah Walker,
 * leilei, Miran Grca.
 * Copyright 2018 Fred N. van Kempen.
 * Copyright 2008-2018 Sarah Walker.
 * Copyright 2016-2018 leilei.
 * Copyright 2016-2018 Miran Grca.
 * These inherited works are GPL-2.0-or-later. No global core is embedded.
 */
#include <blumach/components/cpu_80286.h>
#include "access_286.h"
#include "execution_286.h"

#include <string.h>

enum {
    FLAG_FIXED_ONE = 0x0002U,
    FLAG_CF = 0x0001U,
    FLAG_PF = 0x0004U,
    FLAG_AF = 0x0010U,
    FLAG_ZF = 0x0040U,
    FLAG_SF = 0x0080U,
    FLAG_TF = 0x0100U,
    FLAG_IF = 0x0200U,
    FLAG_DF = 0x0400U,
    FLAG_OF = 0x0800U,
    FLAG_STATUS = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF |
                  FLAG_OF,
    MSW_PE = 0x0001U,
    MSW_MP = 0x0002U,
    MSW_EM = 0x0004U,
    MSW_TS = 0x0008U,
    ADDRESS_MASK = 0x00ffffffU
};

typedef struct bm_286_private {
    bm_host_services_t host;
    bm_286_config_t config;
    bm_286_arch_state_t arch;
    uint8_t intr_line;
    uint8_t nmi_line;
    uint8_t hold_line;
    uint8_t hold_acknowledged;
    uint8_t stopped;
    uint8_t lock_active;
    /* Decoded repetition survives only uninterrupted execution/HOLD. Public
     * arch import and interrupt entry discard it; architectural IP points
     * at the first prefix while incomplete. Not a cycle-exact prefetch. */
    uint8_t rep_active, rep_opcode, rep_prefix, rep_lock;
    int rep_segment;
    uint16_t rep_end_ip;
} bm_286_private_t;

static bm_status_t cpu_run(void *context, bm_tick_t budget,
                           bm_tick_t *consumed);
static void end_bus_lock(bm_286_private_t *state);

static void reset_architecture(bm_286_private_t *state)
{
    memset(&state->arch, 0, sizeof(state->arch));
    state->arch.size = sizeof(state->arch);
    state->arch.version = BM_286_STATE_VERSION;
    state->arch.flags = FLAG_FIXED_ONE;
    state->arch.msw = 0xfff0U;
    state->arch.ip = 0xfff0U;
    state->arch.cs.selector = 0xf000U;
    state->arch.cs.base = 0xff0000U;
    state->arch.cs.limit = 0xffffU;
    state->arch.cs.valid = 1U;
    state->arch.cs.access = 0x82U;
    state->arch.ds.limit = 0xffffU;
    state->arch.ds.valid = 1U;
    state->arch.ds.access = 0x82U;
    state->arch.es.limit = 0xffffU;
    state->arch.es.valid = 1U;
    state->arch.es.access = 0x82U;
    state->arch.ss.limit = 0xffffU;
    state->arch.ss.valid = 1U;
    state->arch.ss.access = 0x82U;
    state->arch.idtr.limit = 0x03ffU;
    state->intr_line = 0U;
    state->nmi_line = 0U;
    state->hold_line = 0U;
    state->hold_acknowledged = 0U;
    state->stopped = 0U;
    state->rep_active = 0U;
}

static bm_status_t cpu_reset(void *context)
{
    bm_286_private_t *state = context;
    if (state == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (state->hold_acknowledged && state->config.hold_ack != NULL)
        state->config.hold_ack(state->config.pin_context, 0);
    if (state->arch.shutdown && state->config.shutdown != NULL)
        state->config.shutdown(state->config.pin_context, 0);
    reset_architecture(state);
    /* Release after reset so a pending arbiter request can reassert HOLD
     * through the callback without that input being cleared afterwards. */
    end_bus_lock(state);
    return BM_STATUS_OK;
}

static bm_status_t cpu_signal(void *context, uint32_t line, int asserted)
{
    bm_286_private_t *state = context;
    if (state == NULL || (asserted != 0 && asserted != 1))
        return BM_STATUS_INVALID_ARGUMENT;
    switch (line) {
    case BM_286_SIGNAL_INTR:
        state->intr_line = (uint8_t) asserted;
        return BM_STATUS_OK;
    case BM_286_SIGNAL_NMI:
        if (asserted && !state->nmi_line)
            state->arch.nmi_pending = 1U;
        state->nmi_line = (uint8_t) asserted;
        return BM_STATUS_OK;
    case BM_286_SIGNAL_HOLD:
        state->hold_line = (uint8_t) asserted;
        if (!asserted && state->hold_acknowledged) {
            state->hold_acknowledged = 0U;
            if (state->config.hold_ack != NULL)
                state->config.hold_ack(state->config.pin_context, 0);
        }
        return BM_STATUS_OK;
    default:
        return BM_STATUS_INVALID_ARGUMENT;
    }
}

static bm_status_t cpu_inspect(const void *context, const char *name,
                               uint64_t *value)
{
    const bm_286_private_t *state = context;
    if (state == NULL || name == NULL || value == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "ip") == 0)
        *value = state->arch.ip;
    else if (strcmp(name, "cs") == 0)
        *value = state->arch.cs.selector;
    else if (strcmp(name, "msw") == 0)
        *value = state->arch.msw;
    else if (strcmp(name, "flags") == 0)
        *value = state->arch.flags;
    else if (strcmp(name, "halted") == 0)
        *value = state->arch.halted;
    else if (strcmp(name, "shutdown") == 0)
        *value = state->arch.shutdown;
    else
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static void cpu_destroy(void *context)
{
    bm_286_private_t *state = context;
    if (state != NULL) {
        end_bus_lock(state);
        state->host.release(state->host.context, state);
    }
}

static int is_286_cpu(const bm_cpu_t *cpu)
{
    return cpu != NULL && cpu->context != NULL &&
        cpu->ops.reset == cpu_reset && cpu->ops.run == cpu_run &&
        cpu->ops.signal == cpu_signal &&
        cpu->ops.inspect == cpu_inspect && cpu->ops.destroy == cpu_destroy;
}

static int valid_segment(const bm_286_segment_state_t *segment)
{
    return segment->base <= ADDRESS_MASK && segment->valid <= 1U;
}

static int valid_architecture(const bm_286_arch_state_t *arch)
{
    return arch->size == sizeof(*arch) &&
        arch->version == BM_286_STATE_VERSION &&
        (arch->flags & FLAG_FIXED_ONE) != 0U &&
        (arch->msw & 0xfff0U) == 0xfff0U &&
        arch->gdtr.base <= ADDRESS_MASK && arch->idtr.base <= ADDRESS_MASK &&
        arch->cpl <= 3U &&
        ((arch->msw & MSW_PE) != 0U || arch->cpl == 0U) &&
        arch->halted <= 1U && arch->shutdown <= 1U &&
        !(arch->halted && arch->shutdown) &&
        arch->interrupt_shadow <= BM_286_SHADOW_SS_LOAD && arch->nmi_blocked <= 1U &&
        arch->nmi_pending <= 1U && arch->trap_pending <= 1U &&
        valid_segment(&arch->es) && valid_segment(&arch->cs) &&
        valid_segment(&arch->ss) && valid_segment(&arch->ds) &&
        valid_segment(&arch->ldtr) && valid_segment(&arch->tr);
}

bm_status_t bm_286_create(const bm_host_services_t *host,
                          const bm_286_config_t *config, bm_cpu_t *out_cpu)
{
    bm_286_private_t *state;
    if (out_cpu == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(out_cpu, 0, sizeof(*out_cpu));
    if (host == NULL || host->allocate == NULL || host->release == NULL ||
        config == NULL || config->size != sizeof(*config) ||
        config->version != BM_286_CONTRACT_VERSION || config->access == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    state = host->allocate(host->context, sizeof(*state));
    if (state == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(state, 0, sizeof(*state));
    state->host = *host;
    state->config = *config;
    reset_architecture(state);
    *out_cpu = (bm_cpu_t) {
        "intel-80286", state,
        { cpu_reset, cpu_run, cpu_signal, cpu_inspect, cpu_destroy }
    };
    return BM_STATUS_OK;
}

bm_status_t bm_286_get_arch_state(const bm_cpu_t *cpu,
                                  bm_286_arch_state_t *out_state)
{
    if (!is_286_cpu(cpu) || out_state == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *out_state = ((const bm_286_private_t *) cpu->context)->arch;
    return BM_STATUS_OK;
}

bm_status_t bm_286_set_arch_state(bm_cpu_t *cpu,
                                  const bm_286_arch_state_t *arch)
{
    bm_286_private_t *state;
    if (!is_286_cpu(cpu) || arch == NULL || !valid_architecture(arch))
        return BM_STATUS_INVALID_ARGUMENT;
    state = cpu->context;
    if (state->stopped)
        return BM_STATUS_INVALID_STATE;
    state->arch = *arch;
    state->rep_active = 0U; /* Conformance import starts a new decode interval. */
    end_bus_lock(state); /* A valid import abandons the private continuation. */
    return BM_STATUS_OK;
}

static bm_status_t fetch_byte(bm_286_private_t *state, uint16_t ip,
                              uint8_t *out_value, uint32_t *out_waits)
{
    bm_bus_transaction_t transaction = {0};
    bm_status_t status;
    if (!state->arch.cs.valid || ip > state->arch.cs.limit)
        return BM_STATUS_UNSUPPORTED; /* Caller classifies guest limit faults. */
    transaction.space = BM_ADDRESS_PROGRAM;
    transaction.operation = BM_BUS_FETCH;
    transaction.address = (state->arch.cs.base + ip) & ADDRESS_MASK;
    transaction.size = 1U;
    transaction.alignment = 1U;
    transaction.endianness = BM_ENDIAN_LITTLE;
    status = state->config.access(state->config.access_context, &transaction);
    if (status != BM_STATUS_OK)
        return status;
    *out_value = (uint8_t) transaction.value;
    *out_waits = transaction.wait_states;
    return BM_STATUS_OK;
}

/* The Intel 286 instruction dictionary, not the later 386 prefix map, bounds
 * this shared decoder. Failed decode never commits IP or a register. */
typedef struct decoded_286 {
    bm_286_private_t *state;
    uint32_t cursor;
    uint8_t length;
    uint64_t waits;
    int override_segment; /* -1 or ES/CS/SS/DS in architectural order. */
    uint8_t next_shadow; /* Published only after a successful instruction. */
    uint8_t software_interrupt; /* Completed INT/INT3/taken INTO, not INTA. */
    uint8_t software_vector;
    uint8_t synchronous_fault; /* Completed fault entry, not software INT. */
    uint8_t lock_prefix; /* Bounded memory forms, not the full 286 LOCK space. */
    uint8_t operand_limit_fault; /* Real-mode unwind to #13. */
    uint8_t protected_subset, pm_fault_vector, task_context;
    uint16_t pm_fault_error; /* Explicit guest metadata, never host status. */
    uint8_t repeat_prefix, string_fault_adjust; /* SI=1, DI=2, CX decrement<<2. */
    uint16_t string_delta;
} decoded_286_t;

static bm_status_t deliver_fault(decoded_286_t *decode, uint8_t vector);

/* A segment-load helper borrows an instruction-owned exclusion window.
 * Its descriptor transactions remain locked; its nested pin edges must not
 * release the outer owner's lock. No generic bus/CPU ABI extension. */
static bm_status_t locked_segment_access(void *context, bm_bus_transaction_t *transfer)
{
    bm_286_private_t *state = context;
    transfer->attributes |= BM_BUS_TRANSACTION_LOCKED;
    return state->config.access(state->config.access_context, transfer);
}
static void borrowed_segment_lock(void *context, int asserted)
{
    (void)context;
    (void)asserted;
}

static bm_status_t load_data_segment(decoded_286_t *decode, unsigned reg,
                                      uint16_t selector)
{
    bm_286_segment_load_result_t result;
    bm_status_t status;
    bm_286_config_t config = decode->state->config;
    if ((decode->state->arch.msw & MSW_PE) && decode->state->lock_active) {
        config.access = locked_segment_access;
        config.access_context = decode->state;
        config.bus_lock = borrowed_segment_lock;
    }
    status = bm_286_load_segment_state(&decode->state->arch,
        &config, reg, selector, &result);
    decode->waits += result.waits;
    if (status != BM_STATUS_OK)
        return status;
    if (result.fault_vector != 0) {
        if (decode->state->arch.msw & MSW_PE) {
            decode->pm_fault_vector = result.fault_vector;
            decode->pm_fault_error = result.fault_error;
            return BM_STATUS_UNSUPPORTED; /* Unwind before protected delivery. */
        }
        return deliver_fault(decode, result.fault_vector);
    }
    return result.loaded ? BM_STATUS_OK : BM_STATUS_INVALID_STATE;
}

typedef struct operand_286 {
    uint8_t reg_field;
    uint8_t reg_number;
    uint8_t memory;
    uint16_t offset;
    const bm_286_segment_state_t *segment;
} operand_286_t;

static uint16_t *word_register(bm_286_arch_state_t *arch, unsigned number)
{
    switch (number & 7U) {
    case 0U: return &arch->ax;
    case 1U: return &arch->cx;
    case 2U: return &arch->dx;
    case 3U: return &arch->bx;
    case 4U: return &arch->sp;
    case 5U: return &arch->bp;
    case 6U: return &arch->si;
    default: return &arch->di;
    }
}

static uint8_t byte_register(bm_286_arch_state_t *arch, unsigned number)
{
    const uint16_t *word = word_register(arch, number & 3U);
    return (uint8_t) (*word >> ((number & 4U) ? 8U : 0U));
}

static void set_byte_register(bm_286_arch_state_t *arch, unsigned number,
                              uint8_t value)
{
    uint16_t *word = word_register(arch, number & 3U);
    unsigned shift = (number & 4U) ? 8U : 0U;
    *word = (uint16_t) ((*word & (shift ? 0x00ffU : 0xff00U)) |
                        ((uint16_t) value << shift));
}

static bm_286_segment_state_t *segment_register(bm_286_arch_state_t *arch,
                                                 unsigned number)
{
    switch (number) {
    case 0U: return &arch->es;
    case 1U: return &arch->cs;
    case 2U: return &arch->ss;
    case 3U: return &arch->ds;
    default: return NULL;
    }
}

static bm_status_t next_byte(decoded_286_t *decode, uint8_t *value)
{
    uint32_t waits;
    bm_status_t status;
    if (decode->protected_subset) {
        bm_286_pm_access_state_t access_state = {0};
        bm_286_pm_access_result_t result;
        if (decode->length >= 10U) {
            decode->pm_fault_vector = 13;
            return BM_STATUS_UNSUPPORTED;
        }
        status = bm_286_pm_access(&decode->state->arch, 1, BM_286_PM_FETCH,
            decode->cursor, 1, false, NULL, decode->state->config.access,
            decode->state->config.access_context, &access_state, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        if (result.fault_vector) {
            decode->pm_fault_vector = result.fault_vector;
            return BM_STATUS_UNSUPPORTED;
        }
        *value = result.bytes[0]; ++decode->cursor; ++decode->length;
        return BM_STATUS_OK;
    }
    if (!decode->state->arch.cs.valid)
        return BM_STATUS_UNSUPPORTED; /* Invalid imported cache is not modeled. */
    if (decode->length >= 10U || decode->cursor > decode->state->arch.cs.limit) {
        decode->operand_limit_fault = 1U;
        return BM_STATUS_UNSUPPORTED;
    }
    status = fetch_byte(decode->state, (uint16_t) decode->cursor,
                        value, &waits);
    if (status != BM_STATUS_OK)
        return status;
    ++decode->cursor;
    ++decode->length;
    decode->waits += waits;
    return BM_STATUS_OK;
}

static bm_status_t next_word(decoded_286_t *decode, uint16_t *value)
{
    uint8_t low, high;
    bm_status_t status = next_byte(decode, &low);
    if (status != BM_STATUS_OK)
        return status;
    status = next_byte(decode, &high);
    if (status != BM_STATUS_OK)
        return status;
    *value = (uint16_t) ((uint16_t) low | ((uint16_t) high << 8));
    return BM_STATUS_OK;
}

static bm_status_t decode_operand(decoded_286_t *decode,
                                  operand_286_t *operand)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint8_t modrm, displacement8;
    uint16_t displacement16 = 0U;
    uint32_t address = 0U;
    unsigned mode, rm;
    int uses_bp = 0;
    bm_status_t status = next_byte(decode, &modrm);
    if (status != BM_STATUS_OK)
        return status;
    mode = modrm >> 6;
    rm = modrm & 7U;
    operand->reg_field = (modrm >> 3) & 7U;
    operand->reg_number = (uint8_t) rm;
    operand->memory = (uint8_t) (mode != 3U);
    operand->offset = 0U;
    operand->segment = NULL;
    if (mode == 3U)
        return BM_STATUS_OK;
    switch (rm) {
    case 0U: address = (uint32_t) arch->bx + arch->si; break;
    case 1U: address = (uint32_t) arch->bx + arch->di; break;
    case 2U: address = (uint32_t) arch->bp + arch->si; uses_bp = 1; break;
    case 3U: address = (uint32_t) arch->bp + arch->di; uses_bp = 1; break;
    case 4U: address = arch->si; break;
    case 5U: address = arch->di; break;
    case 6U:
        if (mode != 0U) { address = arch->bp; uses_bp = 1; }
        break;
    default: address = arch->bx; break;
    }
    if (mode == 0U && rm == 6U) {
        status = next_word(decode, &displacement16);
        if (status != BM_STATUS_OK)
            return status;
        address = displacement16;
    } else if (mode == 1U) {
        status = next_byte(decode, &displacement8);
        if (status != BM_STATUS_OK)
            return status;
        address += (uint16_t) (int16_t) (int8_t) displacement8;
    } else if (mode == 2U) {
        status = next_word(decode, &displacement16);
        if (status != BM_STATUS_OK)
            return status;
        address += displacement16;
    }
    operand->offset = (uint16_t) address;
    operand->segment = segment_register(arch,
        decode->override_segment >= 0 ?
        (unsigned) decode->override_segment : (uses_bp ? 2U : 3U));
    return BM_STATUS_OK;
}

static void end_bus_lock(bm_286_private_t *state)
{
    if (state->lock_active) {
        state->lock_active = 0U;
        state->config.bus_lock(state->config.pin_context, 0);
    }
}

static bm_status_t begin_bus_lock(bm_286_private_t *state)
{
    if (state->config.bus_lock == NULL)
        return BM_STATUS_UNSUPPORTED;
    if (!state->lock_active) {
        state->lock_active = 1U;
        state->config.bus_lock(state->config.pin_context, 1);
    }
    return BM_STATUS_OK;
}

static bm_status_t data_access(decoded_286_t *decode,
                               const bm_286_segment_state_t *segment,
                               uint16_t offset, unsigned size, int write,
                               uint16_t *value)
{
    bm_bus_transaction_t transfer = {0};
    uint32_t address;
    unsigned fragment;
    bm_status_t status;
    if (segment == NULL || !segment->valid ||
        (uint32_t) offset + size - 1U > segment->limit)
        return BM_STATUS_UNSUPPORTED; /* Segment fault delivery is pending. */
    if (decode->lock_prefix && !decode->state->lock_active) {
        bm_286_private_t *state = decode->state;
        /* Bracket writes as well as reads (MOV need not read its destination).
         * All instruction bytes and operand validation precede acquisition. */
        status = begin_bus_lock(state);
        if (status != BM_STATUS_OK)
            return status;
    }
    address = (segment->base + offset) & ADDRESS_MASK;
    transfer.space = BM_ADDRESS_DATA;
    transfer.operation = write ? BM_BUS_WRITE : BM_BUS_READ;
    transfer.endianness = BM_ENDIAN_LITTLE;
    transfer.attributes = decode->state->lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U;
    if (size == 1U || (address & 1U) == 0U) {
        transfer.address = address;
        transfer.size = size;
        transfer.alignment = size;
        transfer.value = write ? *value : 0U;
        status = decode->state->config.access(
            decode->state->config.access_context, &transfer);
        if (status != BM_STATUS_OK)
            return status;
        decode->waits += transfer.wait_states;
        if (!write)
            *value = (uint16_t) transfer.value;
        return BM_STATUS_OK;
    }
    /* An odd physical word uses two byte transactions. A failed second
     * fragment leaves the first completed write intact and stops the CPU. */
    for (fragment = 0U; fragment < 2U; ++fragment) {
        transfer.address = (address + fragment) & ADDRESS_MASK;
        transfer.size = 1U;
        transfer.alignment = 1U;
        transfer.wait_states = 0U;
        transfer.value = write ? (uint8_t) (*value >> (8U * fragment)) : 0U;
        status = decode->state->config.access(
            decode->state->config.access_context, &transfer);
        if (status != BM_STATUS_OK)
            return status;
        decode->waits += transfer.wait_states;
        if (!write) {
            if (fragment == 0U)
                *value = (uint8_t) transfer.value;
            else
                *value |= (uint16_t) ((uint8_t) transfer.value << 8);
        }
    }
    return BM_STATUS_OK;
}

/* Private complete operand check; callers distinguish guest metadata from
 * endpoint errors. No table reload and no bus effects. */
static bm_status_t protected_operand_check(decoded_286_t *decode,
    const bm_286_segment_state_t *segment, uint16_t offset, unsigned length, int write)
{
    bm_286_pm_access_check_t check;
    unsigned reg;
    bm_status_t status;
    for (reg = 0; reg < 4; ++reg)
        if (segment == segment_register(&decode->state->arch, reg)) break;
    status = bm_286_pm_check_access(&decode->state->arch, reg,
        write ? BM_286_PM_WRITE : BM_286_PM_READ, offset, length, &check);
    if (status != BM_STATUS_OK) return status;
    if (!check.allowed) {
        decode->pm_fault_vector = check.fault_vector;
        return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

/* Keep operand faults separate from stack/IVT transport and host failures.
 * Returning a non-OK status unwinds the caller before it commits registers;
 * the private marker, never a host status alone, requests guest delivery. */
static bm_status_t operand_access(decoded_286_t *decode,
                                  const bm_286_segment_state_t *segment,
                                  uint16_t offset, unsigned size, int write,
                                  uint16_t *value)
{
    if (decode->protected_subset) {
        bm_286_pm_access_state_t access_state = {0};
        bm_286_pm_access_result_t result;
        uint8_t bytes[2];
        unsigned reg;
        bm_status_t status;
        for (reg = 0; reg < 4; ++reg)
            if (segment == segment_register(&decode->state->arch, reg)) break;
        if (reg == 4 || (size != 1 && size != 2)) return BM_STATUS_INVALID_STATE;
        bytes[0] = write ? (uint8_t)*value : 0;
        bytes[1] = write ? (uint8_t)(*value >> 8) : 0;
        status = protected_operand_check(decode, segment, offset, size, write);
        if (status != BM_STATUS_OK) return status;
        if (decode->lock_prefix && !decode->state->lock_active) {
            status = begin_bus_lock(decode->state);
            if (status != BM_STATUS_OK) return status;
        }
        status = bm_286_pm_access(&decode->state->arch, reg,
            write ? BM_286_PM_WRITE : BM_286_PM_READ, offset, size,
            decode->state->lock_active != 0,
            write ? bytes : NULL, decode->state->config.access,
            decode->state->config.access_context, &access_state, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        if (result.fault_vector) {
            decode->pm_fault_vector = result.fault_vector;
            return BM_STATUS_UNSUPPORTED;
        }
        if (!write) *value = (uint16_t)(result.bytes[0] | ((uint16_t)result.bytes[1] << 8));
        return BM_STATUS_OK;
    }
    if (segment != NULL && segment->valid &&
        (uint32_t) offset + size - 1U > segment->limit) {
        decode->operand_limit_fault = 1U;
        return BM_STATUS_UNSUPPORTED;
    }
    return data_access(decode, segment, offset, size, write, value);
}

/* Far pointers keep independently wrapped word offsets (including FFFE ->
 * 0000), but neither individual word may straddle a valid segment limit.
 * Check both before any pointer read or destination/stack commit. This is
 * functional preflight policy, not a physical bus/fault precedence claim. */
static bm_status_t pointer_preflight(decoded_286_t *decode,
                                    const operand_286_t *operand)
{
    if (decode->protected_subset)
        return protected_operand_check(decode, operand->segment, operand->offset, 4U, 0);
    if (!operand->memory || !operand->segment->valid)
        return BM_STATUS_UNSUPPORTED;
    if ((uint32_t) operand->offset + 1U > operand->segment->limit ||
        (uint32_t) (uint16_t) (operand->offset + 2U) + 1U > operand->segment->limit) {
        decode->operand_limit_fault = 1U;
        return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static bm_status_t read_operand(decoded_286_t *decode,
                                const operand_286_t *operand,
                                unsigned size, uint16_t *value)
{
    if (decode->lock_prefix && !operand->memory)
        return BM_STATUS_UNSUPPORTED; /* Register-only LOCK is not modeled. */
    if (operand->memory)
        return operand_access(decode, operand->segment, operand->offset,
                           size, 0, value);
    *value = size == 1U ? byte_register(&decode->state->arch,
                                         operand->reg_number) :
        *word_register(&decode->state->arch, operand->reg_number);
    return BM_STATUS_OK;
}

/* Complete destination permission check before a scalar RMW read. */
static bm_status_t read_rmw_operand(decoded_286_t *decode, const operand_286_t *operand,
    unsigned size, uint16_t *value)
{
    if (decode->protected_subset && operand->memory) {
        bm_status_t status = protected_operand_check(decode, operand->segment, operand->offset, size, 1);
        /* PRM specifies #GP even where restart is not guaranteed. Keeping
         * pre-operation registers is our deterministic policy for unspecified
         * fault microstate, not certification that silicon can safely retry. */
        if (status != BM_STATUS_OK) return status;
    }
    return read_operand(decode, operand, size, value);
}

static bm_status_t write_operand(decoded_286_t *decode,
                                 const operand_286_t *operand,
                                 unsigned size, uint16_t value)
{
    if (decode->lock_prefix && !operand->memory)
        return BM_STATUS_UNSUPPORTED;
    if (operand->memory)
        return operand_access(decode, operand->segment, operand->offset,
                           size, 1, &value);
    if (size == 1U)
        set_byte_register(&decode->state->arch, operand->reg_number,
                          (uint8_t) value);
    else
        *word_register(&decode->state->arch, operand->reg_number) = value;
    return BM_STATUS_OK;
}

/* Stack access always uses SS. operand_access supplies real #13 or private
 * protected #SS checks; completed endpoint writes are never undone. */
static bm_status_t push_word(decoded_286_t *decode, uint16_t value)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t sp = (uint16_t) (arch->sp - 2U);
    bm_status_t status = operand_access(decode, &arch->ss, sp, 2U, 1, &value);
    if (status == BM_STATUS_OK)
        arch->sp = sp;
    return status;
}

static int stack_word_valid(const bm_286_arch_state_t *arch, uint16_t offset)
{
    return arch->ss.valid && (uint32_t) offset + 1U <= arch->ss.limit;
}

/* Invalid imported caches are not hardware segment faults. */
static bm_status_t stack_limit_failure(decoded_286_t *decode)
{
    if (decode->state->arch.ss.valid)
        decode->operand_limit_fault = 1U;
    return BM_STATUS_UNSUPPORTED;
}

static bm_status_t protected_stack_range(decoded_286_t *decode,
    uint16_t start, uint32_t length, int write)
{
    bm_286_pm_access_check_t check;
    bm_status_t status = bm_286_pm_check_access(&decode->state->arch, 2U,
        write ? BM_286_PM_WRITE : BM_286_PM_READ, start, length, &check);
    if (status != BM_STATUS_OK)
        return status;
    if (!check.allowed) {
        decode->pm_fault_vector = check.fault_vector;
        decode->pm_fault_error = 0U;
        return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

/* Multiword operations stage register changes, not external bus writes.
 * Whole-operation preflight is a functional policy, not silicon fault
 * precedence or physical bus-cycle ordering. */
static bm_status_t aggregate_stack(decoded_286_t *decode, int pop)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t values[8], offsets[8];
    unsigned i;
    bm_status_t status;
    if (decode->protected_subset) {
        uint16_t start = pop ? arch->sp : (uint16_t) (arch->sp - 16U);
        status = protected_stack_range(decode, start, 16U, !pop);
        if (status != BM_STATUS_OK)
            return status;
    }
    for (i = 0U; i < 8U; ++i) {
        offsets[i] = (uint16_t) (pop ? arch->sp + 2U * i :
                                      arch->sp - 2U * (i + 1U));
        if (!decode->protected_subset && !stack_word_valid(arch, offsets[i]))
            return stack_limit_failure(decode);
        values[i] = *word_register(arch, i);
    }
    for (i = 0U; i < 8U; ++i) {
        if (pop && i == 3U)
            continue; /* Discard the saved SP slot; never load it into SP. */
        status = operand_access(decode, &arch->ss, offsets[i], 2U, !pop, &values[i]);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (pop)
        for (i = 0U; i < 8U; ++i)
            if (i != 3U)
                *word_register(arch, 7U - i) = values[i];
    arch->sp = (uint16_t) (pop ? arch->sp + 16U : arch->sp - 16U);
    return BM_STATUS_OK;
}

static bm_status_t enter_frame(decoded_286_t *decode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t allocation, frame = (uint16_t) (arch->sp - 2U), value;
    uint16_t sp = arch->sp, bp = arch->bp, final_sp;
    uint8_t nesting;
    unsigned i, words;
    bm_status_t status = next_word(decode, &allocation);
    if (status != BM_STATUS_OK)
        return status;
    status = next_byte(decode, &nesting);
    if (status != BM_STATUS_OK)
        return status;
    nesting &= 31U; /* Intel 286 Appendix B: LEVEL modulo 32. */
    words = nesting == 0U ? 1U : (unsigned) nesting + 1U;
    final_sp = (uint16_t) (sp - 2U * words - allocation);
    if (decode->protected_subset) {
        /* Include reserved locals in the range without accessing them.
         * Display words are checked individually, but read interleaved with
         * pushes so overlapping frames observe earlier completed writes. */
        status = protected_stack_range(decode, final_sp,
            2U * words + (uint32_t) allocation, 1);
        if (status != BM_STATUS_OK)
            return status;
        for (i = 1U; i < nesting; ++i) {
            status = protected_stack_range(decode, (uint16_t) (bp - 2U * i), 2U, 0);
            if (status != BM_STATUS_OK)
                return status;
        }
    } else {
        if (!arch->ss.valid || final_sp > arch->ss.limit)
            return stack_limit_failure(decode);
        for (i = 0U; i < words; ++i)
            if (!stack_word_valid(arch, (uint16_t) (sp - 2U * (i + 1U))))
                return stack_limit_failure(decode);
        for (i = 1U; i < nesting; ++i)
            if (!stack_word_valid(arch, (uint16_t) (bp - 2U * i)))
                return stack_limit_failure(decode);
    }
    for (i = 0U; i < words; ++i) {
        if (i == 0U)
            value = arch->bp;
        else if (i == nesting)
            value = frame;
        else {
            bp = (uint16_t) (bp - 2U);
            status = operand_access(decode, &arch->ss, bp, 2U, 0, &value);
            if (status != BM_STATUS_OK)
                return status;
        }
        sp = (uint16_t) (sp - 2U);
        status = operand_access(decode, &arch->ss, sp, 2U, 1, &value);
        if (status != BM_STATUS_OK)
            return status;
    }
    arch->bp = frame;
    arch->sp = final_sp;
    return BM_STATUS_OK;
}

static bm_status_t check_near_target(decoded_286_t *decode, uint16_t ip)
{
    const bm_286_arch_state_t *arch = &decode->state->arch;
    if (decode->protected_subset) {
        bm_286_pm_access_check_t check;
        bm_status_t status = bm_286_pm_check_access(arch, 1, BM_286_PM_FETCH, ip, 1, &check);
        if (status != BM_STATUS_OK) return status;
        if (check.fault_vector) {
            decode->pm_fault_vector = check.fault_vector;
            return BM_STATUS_UNSUPPORTED;
        }
        return BM_STATUS_OK;
    }
    if (!arch->cs.valid)
        return BM_STATUS_UNSUPPORTED;
    if (ip > arch->cs.limit) {
        decode->operand_limit_fault = 1U;
        return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static bm_status_t near_branch(decoded_286_t *decode, uint16_t target, int call)
{
    bm_status_t status = check_near_target(decode, target);
    if (status != BM_STATUS_OK)
        return status;
    if (call) {
        status = push_word(decode, (uint16_t) decode->cursor);
        if (status != BM_STATUS_OK)
            return status;
    }
    decode->cursor = target;
    return BM_STATUS_OK;
}

/* Both pointer words are already fetched. Real CS reload drops the reset
 * base; the private PE path validates direct code, call gates or tasks.
 * A20 remains board-owned. */
static bm_status_t far_jump(decoded_286_t *decode, uint16_t ip, uint16_t cs)
{
    bm_286_segment_state_t *segment = &decode->state->arch.cs;
    if (decode->protected_subset) {
        bm_286_segment_load_result_t result;
        bm_status_t status = bm_286_pm_jump(&decode->state->arch,
            &decode->state->config, cs, ip, (uint16_t)decode->cursor, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        decode->task_context=result.task_context;
        if (result.fault_vector) {
            decode->pm_fault_vector = result.fault_vector;
            decode->pm_fault_error = result.fault_error;
            return BM_STATUS_UNSUPPORTED;
        }
        decode->cursor = decode->state->arch.ip; /* A call gate redirects the offset. */
        return result.loaded ? BM_STATUS_OK : BM_STATUS_INVALID_STATE;
    }
    segment->selector = cs;
    segment->base = (uint32_t) cs << 4;
    segment->limit = 0xffffU;
    segment->access = 0x82U;
    segment->valid = 1U;
    decode->cursor = ip;
    return BM_STATUS_OK;
}

/* Functional real-mode boundary only, not a pin-cycle/fault-order model.
 * Stage registers, never roll back completed endpoint writes. */
static bm_status_t far_call(decoded_286_t *decode, uint16_t ip, uint16_t cs)
{
    if (decode->protected_subset) {
        bm_286_segment_load_result_t result;
        bm_status_t status = bm_286_pm_call(&decode->state->arch,
            &decode->state->config, cs, ip, (uint16_t)decode->cursor, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        decode->task_context=result.task_context;
        if (result.fault_vector) {
            decode->pm_fault_vector = result.fault_vector;
            decode->pm_fault_error = result.fault_error;
            return BM_STATUS_UNSUPPORTED;
        }
        decode->cursor = decode->state->arch.ip;
        return result.loaded ? BM_STATUS_OK : BM_STATUS_INVALID_STATE;
    }
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t words[2] = {arch->cs.selector, (uint16_t) decode->cursor};
    bm_status_t status;
    for (unsigned i = 0; i < 2U; ++i)
        if (!stack_word_valid(arch, (uint16_t) (arch->sp - 2U * (i + 1U))))
            return stack_limit_failure(decode);
    for (unsigned i = 0; i < 2U; ++i) {
        status = data_access(decode, &arch->ss,
            (uint16_t) (arch->sp - 2U * (i + 1U)), 2U, 1, &words[i]);
        if (status != BM_STATUS_OK)
            return status;
    }
    arch->sp = (uint16_t) (arch->sp - 4U);
    return far_jump(decode, ip, cs);
}

static bm_status_t far_return(decoded_286_t *decode, uint16_t discard)
{
    if (decode->protected_subset) {
        bm_286_segment_load_result_t result;
        bm_status_t status = bm_286_pm_retf(&decode->state->arch,
            &decode->state->config, discard, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        if (result.fault_vector) {
            decode->pm_fault_vector = result.fault_vector;
            decode->pm_fault_error = result.fault_error;
            return BM_STATUS_UNSUPPORTED;
        }
        if (!result.loaded) return BM_STATUS_INVALID_STATE;
        decode->cursor = decode->state->arch.ip;
        return BM_STATUS_OK;
    }
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t words[2];
    bm_status_t status;
    for (unsigned i = 0; i < 2U; ++i)
        if (!stack_word_valid(arch, (uint16_t) (arch->sp + 2U * i)))
            return stack_limit_failure(decode);
    for (unsigned i = 0; i < 2U; ++i) {
        status = data_access(decode, &arch->ss,
            (uint16_t) (arch->sp + 2U * i), 2U, 0, &words[i]);
        if (status != BM_STATUS_OK)
            return status;
    }
    arch->sp = (uint16_t) (arch->sp + 4U + discard);
    /* Unlike IRET, RETF changes neither FLAGS nor NMI blocking. */
    return far_jump(decode, words[0], words[1]);
}

static bm_status_t enter_shutdown(decoded_286_t *decode)
{
    bm_286_private_t *state = decode->state;
    uint8_t was_shutdown = state->arch.shutdown;
    state->arch.shutdown = 1U;
    state->arch.halted = 0U;
    state->arch.trap_pending = 0U;
    state->arch.interrupt_shadow = BM_286_SHADOW_NONE;
    decode->cursor = state->arch.ip;
    decode->synchronous_fault = 1U;
    end_bus_lock(state);
    if (!was_shutdown && state->config.shutdown != NULL)
        state->config.shutdown(state->config.pin_context, 1);
    return BM_STATUS_OK; /* Guest shutdown is not a failed host endpoint. */
}

static bm_status_t interrupt_frame(decoded_286_t *decode, uint8_t vector,
                                   uint16_t return_ip)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    bm_286_segment_state_t table = {0};
    uint16_t words[3] = {arch->flags, arch->cs.selector, return_ip};
    uint16_t ip, cs;
    uint16_t offset = (uint16_t) ((unsigned) vector * 4U);
    bm_status_t status;
    if ((uint32_t) offset + 3U > arch->idtr.limit) {
        /* Intel real-mode #8: restart at the first instruction byte, even
         * for INT. No protected-mode error word or general #DF matrix. */
        if (arch->shutdown || arch->idtr.limit < 35U)
            return enter_shutdown(decode);
        vector = 8U;
        offset = 32U;
        words[2] = arch->ip;
        decode->synchronous_fault = 1U;
    }
    if (!arch->ss.valid)
        return BM_STATUS_UNSUPPORTED; /* Invalid imported state, not #SS. */
    for (unsigned i = 0; i < 3U; ++i)
        if (!stack_word_valid(arch, (uint16_t) (arch->sp - 2U * (i + 1U))))
            /* A real-mode exception needs the same six-byte stack frame.
             * Do not recurse into fault delivery or synthesize partial pushes. */
            return enter_shutdown(decode);
    for (unsigned i = 0; i < 3U; ++i) {
        status = data_access(decode, &arch->ss,
            (uint16_t) (arch->sp - 2U * (i + 1U)), 2U, 1, &words[i]);
        if (status != BM_STATUS_OK)
            return status;
        /* B-2/later INTA exclusion reaches the first stack push, not the
         * complete frame/IVT. The word's odd fragments stay indivisible in
         * this logical-transfer model; physical pin edges are not timed. */
        if (i == 0U && decode->state->lock_active)
            end_bus_lock(decode->state);
    }
    table.base = arch->idtr.base;
    table.limit = arch->idtr.limit;
    table.valid = 1U;
    status = data_access(decode, &table, offset, 2U, 0, &ip);
    if (status != BM_STATUS_OK)
        return status;
    status = data_access(decode, &table, (uint16_t) (offset + 2U), 2U, 0, &cs);
    if (status != BM_STATUS_OK)
        return status;
    arch->sp = (uint16_t) (arch->sp - 6U);
    arch->flags &= (uint16_t) ~(FLAG_IF | FLAG_TF);
    arch->halted = 0U;
    arch->shutdown = 0U;
    arch->interrupt_shadow = BM_286_SHADOW_NONE;
    decode->software_vector = vector;
    return far_jump(decode, ip, cs);
}

static bm_status_t interrupt_return(decoded_286_t *decode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t words[3];
    bm_status_t status;
    for (unsigned i = 0; i < 3U; ++i)
        if (!stack_word_valid(arch, (uint16_t) (arch->sp + 2U * i)))
            return stack_limit_failure(decode);
    for (unsigned i = 0; i < 3U; ++i) {
        status = data_access(decode, &arch->ss,
            (uint16_t) (arch->sp + 2U * i), 2U, 0, &words[i]);
        if (status != BM_STATUS_OK)
            return status;
    }
    arch->sp = (uint16_t) (arch->sp + 6U);
    /* 286 real-mode IRET cannot write IOPL/NT; not 386 FLAGS semantics. */
    arch->flags = (uint16_t) ((arch->flags & 0x7000U) |
        (words[2] & 0x0fd5U) | FLAG_FIXED_ONE);
    arch->nmi_blocked = 0U;
    return far_jump(decode, words[0], words[1]);
}

static bm_status_t accept_interrupt(decoded_286_t *decode, unsigned event,
                                     bm_286_boundary_t *boundary)
{
    bm_286_private_t *state = decode->state;
    uint8_t vector = (uint8_t) event; /* #1, NMI=2, INTR=0 */
    uint8_t was_shutdown = state->arch.shutdown;
    bm_status_t status;
    if ((state->arch.msw & MSW_PE) || (was_shutdown && event != 2U))
        return BM_STATUS_UNSUPPORTED; /* Protected gates/recovery pending. */
    if (event == 0U) {
        if (state->config.interrupt_ack == NULL || state->config.bus_lock == NULL)
            return BM_STATUS_UNSUPPORTED;
        /* Intel B-2/B-3 Information Sheet: both INTA cycles are joined to
         * the first stack push. Do not silently run an unlocked acknowledge. */
        state->lock_active = 1U;
        state->config.bus_lock(state->config.pin_context, 1);
        for (unsigned phase = 0; phase < 2U; ++phase) {
            uint32_t waits = 0U;
            status = state->config.interrupt_ack(state->config.interrupt_context,
                                                 phase, &vector, &waits);
            if (status != BM_STATUS_OK)
                return status;
            decode->waits += waits;
        }
    }
    /* Consume the accepted edge BEFORE endpoint callbacks, so a new edge
     * signalled from an access remains pending until a later IRET. */
    if (event == 2U)
        state->arch.nmi_pending = 0U;
    status = interrupt_frame(decode, vector, state->arch.ip);
    if (status != BM_STATUS_OK) {
        if (event == 2U)
            state->arch.nmi_pending = 1U;
        return status;
    }
    if (event == 1U)
        state->arch.trap_pending = 0U;
    if (event == 2U)
        state->arch.nmi_blocked = 1U;
    state->arch.ip = (uint16_t) decode->cursor;
    if (was_shutdown && !state->arch.shutdown && state->config.shutdown != NULL)
        state->config.shutdown(state->config.pin_context, 0);
    boundary->kind = state->arch.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
        (event == 1U || decode->synchronous_fault) ? BM_286_BOUNDARY_EXCEPTION : BM_286_BOUNDARY_INTERRUPT;
    boundary->has_vector = (uint8_t) !state->arch.shutdown;
    boundary->vector = state->arch.shutdown ? 0U : decode->software_vector;
    boundary->bus_wait_cycles = decode->waits;
    boundary->cpu_cycles = decode->waits;
    return BM_STATUS_OK;
}

static bm_status_t software_interrupt(decoded_286_t *decode, uint8_t opcode)
{
    uint8_t vector = opcode == 0xccU ? 3U : 4U;
    bm_status_t status;
    if (opcode == 0xceU && !(decode->state->arch.flags & FLAG_OF))
        return BM_STATUS_OK;
    if (opcode == 0xcdU) {
        status = next_byte(decode, &vector);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (decode->protected_subset) {
        decode->software_interrupt = 1U;
        decode->software_vector = vector;
        return BM_STATUS_OK; /* Private caller supplies SOFTWARE source and next IP. */
    }
    status = interrupt_frame(decode, vector, (uint16_t) decode->cursor);
    if (status == BM_STATUS_OK) {
        decode->software_interrupt = 1U;
    }
    return status;
}

static bm_status_t execute_flags(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t value;
    bm_status_t status;
    switch (opcode) {
    case 0x9cU: /* Canonical 286 FLAGS image, not 8086 high bits. */
        return push_word(decode, (uint16_t) ((arch->flags & 0x7fd5U) | FLAG_FIXED_ONE));
    case 0x9dU:
        status = operand_access(decode, &arch->ss, arch->sp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        if (decode->protected_subset) {
            uint16_t mask = 0x4fd5U; /* NT writable at all CPLs, IOPL only CPL0. */
            if (!arch->cpl) mask |= 0x3000U;
            if (arch->cpl > ((arch->flags >> 12) & 3U)) mask &= 0xfdffU;
            /* IF permission uses incoming IOPL, not the candidate stack word. */
            arch->flags = (uint16_t)((((arch->flags & (uint16_t)~mask) |
                (value & mask)) & 0x7fd5U) | FLAG_FIXED_ONE);
        } else
            arch->flags = (uint16_t) ((arch->flags & 0x7000U) | (value & 0x0fd5U) | FLAG_FIXED_ONE);
        arch->sp = (uint16_t) (arch->sp + 2U);
        return BM_STATUS_OK; /* Unlike STI, POPF creates no INTR shadow. */
    case 0x9eU:
        arch->flags = (uint16_t) ((arch->flags & 0xff00U) |
            ((arch->ax >> 8) & 0xd5U) | FLAG_FIXED_ONE);
        return BM_STATUS_OK;
    case 0x9fU:
        arch->ax = (uint16_t) ((arch->ax & 0xffU) |
            (((arch->flags & 0xd5U) | FLAG_FIXED_ONE) << 8));
        return BM_STATUS_OK;
    case 0xf5U: arch->flags ^= FLAG_CF; return BM_STATUS_OK;
    case 0xf8U: arch->flags &= (uint16_t) ~FLAG_CF; return BM_STATUS_OK;
    case 0xf9U: arch->flags |= FLAG_CF; return BM_STATUS_OK;
    case 0xfcU: arch->flags &= (uint16_t) ~FLAG_DF; return BM_STATUS_OK;
    case 0xfdU: arch->flags |= FLAG_DF; return BM_STATUS_OK;
    default: return BM_STATUS_UNSUPPORTED;
    }
}

static bm_status_t execute_ff_control(decoded_286_t *decode,
                                      const operand_286_t *operand)
{
    uint16_t value, selector;
    bm_status_t status;
    if (operand->reg_field == 3U || operand->reg_field == 5U) {
        if (!operand->memory)
            return decode->lock_prefix ? BM_STATUS_UNSUPPORTED :
                deliver_fault(decode, 6U); /* Combined LOCK precedence pending. */
        status = pointer_preflight(decode, operand);
        if (status != BM_STATUS_OK)
            return status;
        /* Protected preflight above forbids doubleword wrap. In real mode
         * the two word offsets wrap independently. SST Harris captures
         * FF.5 cases 2914/3652/4550 read the selector at 0000 after FFFE. */
        status = read_operand(decode, operand, 2U, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = operand_access(decode, operand->segment,
                             (uint16_t) (operand->offset + 2U), 2U, 0, &selector);
        if (status != BM_STATUS_OK)
            return status;
        return operand->reg_field == 3U ? far_call(decode, value, selector) :
                                          far_jump(decode, value, selector);
    }
    if (operand->reg_field != 2U && operand->reg_field != 4U &&
        operand->reg_field != 6U)
        return BM_STATUS_UNSUPPORTED; /* Invalid /7: guest #6 pending. */
    status = read_operand(decode, operand, 2U, &value);
    if (status != BM_STATUS_OK)
        return status;
    if (operand->reg_field == 6U)
        return push_word(decode, value);
    return near_branch(decode, value, operand->reg_field == 2U);
}

static int branch_condition(uint16_t flags, unsigned condition)
{
    int selected;
    int signed_less = ((flags & FLAG_SF) != 0U) != ((flags & FLAG_OF) != 0U);
    switch (condition >> 1) {
    case 0U: selected = (flags & FLAG_OF) != 0U; break;
    case 1U: selected = (flags & FLAG_CF) != 0U; break;
    case 2U: selected = (flags & FLAG_ZF) != 0U; break;
    case 3U: selected = (flags & (FLAG_CF | FLAG_ZF)) != 0U; break;
    case 4U: selected = (flags & FLAG_SF) != 0U; break;
    case 5U: selected = (flags & FLAG_PF) != 0U; break;
    case 6U: selected = signed_less; break;
    default: selected = signed_less || (flags & FLAG_ZF) != 0U; break;
    }
    return selected != ((condition & 1U) != 0U);
}

static bm_status_t execute_stack_control(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand;
    uint16_t value = 0U, extra = 0U, target, next_cx;
    uint8_t byte;
    int take;
    bm_status_t status;
    if (opcode == 0x9aU || opcode == 0xeaU) {
        status = next_word(decode, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = next_word(decode, &extra);
        if (status != BM_STATUS_OK)
            return status;
        return opcode == 0x9aU ? far_call(decode, value, extra) :
                                far_jump(decode, value, extra);
    }
    if (opcode == 0xcaU || opcode == 0xcbU) {
        if (opcode == 0xcaU) {
            status = next_word(decode, &extra);
            if (status != BM_STATUS_OK)
                return status;
        }
        return far_return(decode, extra);
    }
    if (opcode == 0x60U || opcode == 0x61U)
        return aggregate_stack(decode, opcode == 0x61U);
    if (opcode == 0xc8U)
        return enter_frame(decode);
    if (opcode == 0xc9U) {
        status = operand_access(decode, &arch->ss, arch->bp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        arch->sp = (uint16_t) (arch->bp + 2U);
        arch->bp = value;
        return BM_STATUS_OK;
    }
    if (opcode >= 0x50U && opcode <= 0x57U)
        /* Including SP: the 286 pushes the pre-decrement value. */
        return push_word(decode, *word_register(arch, opcode & 7U));
    if (opcode == 0x06U || opcode == 0x0eU || opcode == 0x16U || opcode == 0x1eU)
        return push_word(decode, segment_register(arch, opcode >> 3)->selector);
    if (opcode == 0x68U || opcode == 0x6aU) {
        if (opcode == 0x68U)
            status = next_word(decode, &value);
        else {
            status = next_byte(decode, &byte);
            if (status == BM_STATUS_OK)
                value = (uint16_t) (int16_t) (int8_t) byte;
        }
        return status == BM_STATUS_OK ? push_word(decode, value) : status;
    }
    if ((opcode >= 0x58U && opcode <= 0x5fU) || opcode == 0x8fU ||
        opcode == 0x07U || opcode == 0x17U || opcode == 0x1fU) {
        if (opcode == 0x8fU) {
            status = decode_operand(decode, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (operand.reg_field != 0U)
                return BM_STATUS_UNSUPPORTED;
        }
        status = operand_access(decode, &arch->ss, arch->sp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        if (opcode == 0x8fU) {
            status = write_operand(decode, &operand, 2U, value);
            if (status != BM_STATUS_OK)
                return status;
            /* Mod=3 SP is loaded last, not incremented after replacement. */
            if (!operand.memory && operand.reg_number == 4U)
                return BM_STATUS_OK;
        } else if (opcode == 0x07U || opcode == 0x17U || opcode == 0x1fU) {
            status = load_data_segment(decode, opcode >> 3, value);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0x17U)
                decode->next_shadow = BM_286_SHADOW_SS_LOAD;
        } else {
            if (opcode == 0x5cU) {
                arch->sp = value;
                return BM_STATUS_OK;
            }
            *word_register(arch, opcode & 7U) = value;
        }
        arch->sp = (uint16_t) (arch->sp + 2U);
        return BM_STATUS_OK;
    }
    if (opcode == 0xc2U || opcode == 0xc3U) {
        if (opcode == 0xc2U) {
            status = next_word(decode, &extra);
            if (status != BM_STATUS_OK)
                return status;
        }
        status = operand_access(decode, &arch->ss, arch->sp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = check_near_target(decode, value);
        if (status != BM_STATUS_OK)
            return status;
        arch->sp = (uint16_t) (arch->sp + 2U + extra);
        decode->cursor = value;
        return BM_STATUS_OK;
    }
    if (opcode == 0xe8U || opcode == 0xe9U) {
        status = next_word(decode, &value);
        if (status != BM_STATUS_OK)
            return status;
        target = (uint16_t) (decode->cursor + value);
        return near_branch(decode, target, opcode == 0xe8U);
    }
    if (opcode == 0xebU || (opcode >= 0x70U && opcode <= 0x7fU) ||
        (opcode >= 0xe0U && opcode <= 0xe3U)) {
        status = next_byte(decode, &byte);
        if (status != BM_STATUS_OK)
            return status;
        target = (uint16_t) (decode->cursor + (uint16_t) (int16_t) (int8_t) byte);
        next_cx = arch->cx;
        if (opcode == 0xebU)
            take = 1;
        else if (opcode <= 0x7fU)
            take = branch_condition(arch->flags, opcode & 15U);
        else if (opcode == 0xe3U)
            take = arch->cx == 0U;
        else {
            next_cx = (uint16_t) (arch->cx - 1U);
            take = next_cx != 0U && (opcode == 0xe2U ||
                ((arch->flags & FLAG_ZF) != 0U) == (opcode == 0xe1U));
        }
        if (take) {
            status = near_branch(decode, target, 0);
            if (status != BM_STATUS_OK)
                return status;
        }
        arch->cx = next_cx;
        return BM_STATUS_OK;
    }
    return BM_STATUS_UNSUPPORTED; /* Remaining instruction families. */
}

/* ALU kinds follow the three-bit opcode/ModR/M operation field. TEST uses
 * the AND calculation but does not write the operand. All result/flag values
 * are private until the destination write has completed successfully. */
static int even_parity(uint8_t value)
{
    unsigned i, ones = 0U;
    for (i = 0U; i < 8U; ++i)
        ones += (value >> i) & 1U;
    return (ones & 1U) == 0U;
}

/* Group 2: Intel 210498-005, 3.4.2 and Appendix B. Count masking is 286,
 * not 8086. Execute individual unsigned bit steps (at most 31), avoiding
 * host signed-shift rules and shifts by the host word width. */
static bm_status_t execute_shift(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand = {0};
    unsigned size = (opcode & 1U) ? 2U : 1U;
    uint32_t mask = size == 1U ? 0xffU : 0xffffU;
    uint32_t sign = size == 1U ? 0x80U : 0x8000U;
    uint8_t count = opcode < 0xd0U ? 0U :
        opcode < 0xd2U ? 1U : (uint8_t) arch->cx;
    uint16_t original, flags = arch->flags;
    uint32_t value, carry = (flags & FLAG_CF) ? 1U : 0U;
    unsigned i;
    bm_status_t status = decode_operand(decode, &operand);
    if (status != BM_STATUS_OK)
        return status;
    /* Undocumented /6 is not silently accepted as SAL or a guest fault. */
    if (operand.reg_field == 6U)
        return BM_STATUS_UNSUPPORTED;
    if (opcode < 0xd0U) {
        status = next_byte(decode, &count);
        if (status != BM_STATUS_OK)
            return status;
    }
    count &= 31U;
    status = count ? read_rmw_operand(decode, &operand, size, &original) :
        read_operand(decode, &operand, size, &original);
    if (status != BM_STATUS_OK || count == 0U)
        return status;
    /* Zero-count memory reads, but no write, match the inherited functional
     * access policy; no claim of measured bus sequencing/timing is made. */
    value = original;
    for (i = 0U; i < count; ++i) {
        uint32_t previous_carry = carry;
        switch (operand.reg_field) {
        case 0U: /* ROL */
        case 2U: /* RCL */
        case 4U: /* SHL/SAL */
            carry = (value & sign) ? 1U : 0U;
            value = ((value << 1U) & mask) |
                (operand.reg_field == 0U ? carry :
                 operand.reg_field == 2U ? previous_carry : 0U);
            break;
        default: /* ROR, RCR, SHR, SAR */
            carry = value & 1U;
            value = (value >> 1U) |
                (operand.reg_field == 1U ? carry * sign :
                 operand.reg_field == 3U ? previous_carry * sign :
                 operand.reg_field == 7U ? value & sign : 0U);
            break;
        }
    }
    flags = (uint16_t) ((flags & ~FLAG_CF) | (carry ? FLAG_CF : 0U));
    if (operand.reg_field >= 4U) {
        /* AF undefined: deterministic clear policy, not hardware evidence. */
        flags &= (uint16_t) ~(FLAG_SF | FLAG_ZF | FLAG_PF | FLAG_AF);
        if (value & sign) flags |= FLAG_SF;
        if (value == 0U) flags |= FLAG_ZF;
        if (even_parity((uint8_t) value)) flags |= FLAG_PF;
    }
    /* OF is defined only for masked count 1. Preserve it otherwise as an
     * explicit emulator policy, including full rotations of the bit ring. */
    if (count == 1U) {
        uint32_t overflow;
        if (operand.reg_field == 0U || operand.reg_field == 2U ||
            operand.reg_field == 4U)
            overflow = ((value & sign) != 0U) ^ carry;
        else if (operand.reg_field == 1U || operand.reg_field == 3U)
            overflow = ((value & sign) != 0U) ^ ((value & (sign >> 1U)) != 0U);
        else
            overflow = operand.reg_field == 5U && (original & sign);
        flags = (uint16_t) ((flags & ~FLAG_OF) | (overflow ? FLAG_OF : 0U));
    }
    status = write_operand(decode, &operand, size, (uint16_t) value);
    if (status == BM_STATUS_OK)
        arch->flags = flags;
    return status;
}

static void alu_calculate(unsigned operation, unsigned size,
                          uint16_t destination, uint16_t source,
                          uint16_t old_flags, int preserve_carry,
                          uint16_t *out_result, uint16_t *out_flags)
{
    uint32_t mask = size == 1U ? 0xffU : 0xffffU;
    uint32_t sign = size == 1U ? 0x80U : 0x8000U;
    uint32_t a = destination & mask, b = source & mask;
    uint32_t carry = ((operation == 2U || operation == 3U) &&
                      (old_flags & FLAG_CF)) ? 1U : 0U;
    uint32_t wide = 0U, result;
    uint16_t flags = (uint16_t) (old_flags & ~FLAG_STATUS);
    if (operation == 0U || operation == 2U) { /* ADD, ADC */
        wide = a + b + carry;
        result = wide & mask;
        if (wide > mask)
            flags |= FLAG_CF;
        if ((a ^ b ^ result) & 0x10U)
            flags |= FLAG_AF;
        if ((~(a ^ b) & (a ^ result) & sign) != 0U)
            flags |= FLAG_OF;
    } else if (operation == 3U || operation == 5U || operation == 7U) {
        /* SBB, SUB, CMP; NEG and DEC reuse subtraction with a=0 or b=1. */
        wide = b + carry;
        result = (a - wide) & mask;
        if (a < wide)
            flags |= FLAG_CF;
        if ((a ^ b ^ result) & 0x10U)
            flags |= FLAG_AF;
        if (((a ^ b) & (a ^ result) & sign) != 0U)
            flags |= FLAG_OF;
    } else {
        switch (operation) {
        case 1U: result = a | b; break;
        case 4U: result = a & b; break;
        default: result = a ^ b; break; /* XOR */
        }
        /* Logical-AF-clear policy: Intel leaves AF undefined. Clearing it
         * makes this emulator deterministic, not silicon-fidelity evidence. */
    }
    if (result == 0U)
        flags |= FLAG_ZF;
    if (result & sign)
        flags |= FLAG_SF;
    if (even_parity((uint8_t) result))
        flags |= FLAG_PF;
    if (preserve_carry)
        flags = (uint16_t) ((flags & ~FLAG_CF) | (old_flags & FLAG_CF));
    *out_result = (uint16_t) result;
    *out_flags = flags;
}

/* Convert guest two's-complement values mathematically, without depending
 * on implementation-defined unsigned-to-signed narrowing or signed shifts. */
static int32_t signed_operand(uint16_t value, unsigned size)
{
    uint32_t sign = size == 1U ? 0x80U : 0x8000U;
    uint32_t bits = value & (size == 1U ? 0xffU : 0xffffU);
    return (int32_t) bits - ((bits & sign) ? (int32_t) (sign * 2U) : 0);
}

static bm_status_t execute_multiply(decoded_286_t *decode, uint8_t opcode,
                                    const operand_286_t *operand)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    unsigned size = opcode == 0xf6U ? 1U : 2U;
    int immediate_form = opcode == 0x69U || opcode == 0x6bU;
    int is_signed = immediate_form || operand->reg_field == 5U;
    uint16_t source, multiplier = arch->ax;
    uint32_t product;
    int overflow;
    bm_status_t status;
    if (immediate_form) {
        if (opcode == 0x69U)
            status = next_word(decode, &multiplier);
        else {
            uint8_t immediate;
            status = next_byte(decode, &immediate);
            if (status == BM_STATUS_OK)
                multiplier = (uint16_t) signed_operand(immediate, 1U);
        }
        if (status != BM_STATUS_OK)
            return status;
    }
    status = read_operand(decode, operand, size, &source);
    if (status != BM_STATUS_OK)
        return status;
    if (is_signed) {
        /* Every signed 16x16 product fits int32_t, including -32768 squared. */
        int32_t full = signed_operand(multiplier, size) * signed_operand(source, size);
        int32_t lower = size == 1U ? -128 : -32768;
        int32_t upper = size == 1U ? 127 : 32767;
        product = (uint32_t) full;
        overflow = full < lower || full > upper;
    } else {
        uint32_t mask = size == 1U ? 0xffU : 0xffffU;
        product = ((uint32_t) multiplier & mask) * (uint32_t) source;
        overflow = product > mask;
    }
    /* No externally failing access remains. Capture aliased sources before
     * replacing AX/DX or the immediate form's selected destination. */
    if (immediate_form)
        *word_register(arch, operand->reg_field) = (uint16_t) product;
    else {
        arch->ax = (uint16_t) product;
        if (size == 2U)
            arch->dx = (uint16_t) (product >> 16U);
    }
    /* Intel leaves S/Z/A/P undefined: preserve them as emulator policy,
     * not a claim about measured chip flags. Only CF/OF are defined here. */
    arch->flags = (uint16_t) ((arch->flags & ~(FLAG_CF | FLAG_OF)) |
                              (overflow ? FLAG_CF | FLAG_OF : 0U));
    return BM_STATUS_OK;
}

static bm_status_t deliver_fault(decoded_286_t *decode, uint8_t vector)
{
    if (decode->protected_subset) {
        decode->pm_fault_vector = vector;
        return BM_STATUS_UNSUPPORTED; /* Caller unwinds, then uses protected IDT. */
    }
    /* Implemented real-mode faults save initial IP, including prefixes,
     * without error code or INTA. Mark complete only after frame/IVT success. */
    bm_status_t status = interrupt_frame(decode, vector, decode->state->arch.ip);
    if (status == BM_STATUS_OK) {
        decode->synchronous_fault = 1U;
    }
    return status;
}

static bm_status_t execute_system(decoded_286_t *decode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand;
    uint8_t opcode;
    uint16_t value;
    bm_status_t status = next_byte(decode, &opcode);
    if (status != BM_STATUS_OK) return status;
    if (opcode == 0x06U) { /* CLTS */
        if ((arch->msw & MSW_PE) && arch->cpl) return deliver_fault(decode, 13);
        arch->msw &= 0xfff7U;
        return BM_STATUS_OK;
    }
    if (opcode > 0x03U) return BM_STATUS_UNSUPPORTED;
    status = decode_operand(decode, &operand);
    if (status != BM_STATUS_OK) return status;
    if (opcode == 2U || opcode == 3U ||
        (opcode == 0U && (operand.reg_field == 4U || operand.reg_field == 5U))) {
        bm_286_pm_query_result_t result;
        bm_286_pm_query_kind_t kind = opcode == 2U ? BM_286_PM_LAR :
            opcode == 3U ? BM_286_PM_LSL : operand.reg_field == 4U ? BM_286_PM_VERR : BM_286_PM_VERW;
        if (!(arch->msw & MSW_PE)) return deliver_fault(decode, 6);
        status = read_operand(decode, &operand, 2, &value);
        if (status != BM_STATUS_OK) return status;
        status = bm_286_pm_query(arch, &decode->state->config, kind, value, &result);
        decode->waits += result.waits;
        if (status != BM_STATUS_OK) return status;
        if (result.accepted && opcode != 0U)
            *word_register(arch, operand.reg_field) = result.value;
        arch->flags = (uint16_t)((arch->flags & ~FLAG_ZF) | (result.accepted ? FLAG_ZF : 0));
        return BM_STATUS_OK;
    }
    if (opcode == 0x00U) {
        if (operand.reg_field < 2U) {
            if (!(arch->msw & MSW_PE)) return deliver_fault(decode, 6U);
            return write_operand(decode, &operand, 2U,
                operand.reg_field ? arch->tr.selector : arch->ldtr.selector);
        }
        if (operand.reg_field != 2U && operand.reg_field != 3U) return BM_STATUS_UNSUPPORTED;
        if (!(arch->msw & MSW_PE)) return deliver_fault(decode, 6); /* LLDT/LTR are PE-only. */
        if (arch->cpl) return deliver_fault(decode, 13);
        status = read_operand(decode, &operand, 2, &value);
        if (status == BM_STATUS_OK && operand.reg_field == 3U) {
            bm_286_segment_load_result_t result;
            status = bm_286_pm_ltr(arch, &decode->state->config, value, &result);
            decode->waits += result.waits;
            if (status != BM_STATUS_OK) return status;
            if (result.fault_vector) {
                decode->pm_fault_vector = result.fault_vector;
                decode->pm_fault_error = result.fault_error;
                return BM_STATUS_UNSUPPORTED;
            }
            return result.loaded ? BM_STATUS_OK : BM_STATUS_INVALID_STATE;
        }
        return status == BM_STATUS_OK ? load_data_segment(decode, 4, value) : status;
    }
    if (operand.reg_field < 4U) {
        bm_286_table_state_t *table = (operand.reg_field & 1U) ? &arch->idtr : &arch->gdtr;
        uint16_t words[3];
        int load = operand.reg_field >= 2U;
        if (!operand.memory) return deliver_fault(decode, 6);
        if (load && (arch->msw & MSW_PE) && arch->cpl) return deliver_fault(decode, 13);
        if (decode->protected_subset) {
            bm_286_pm_access_check_t check;
            unsigned reg;
            for (reg = 0; reg < 4; ++reg)
                if (operand.segment == segment_register(arch, reg)) break;
            status = bm_286_pm_check_access(arch, reg, load ? BM_286_PM_READ : BM_286_PM_WRITE,
                operand.offset, 6, &check);
            if (status != BM_STATUS_OK) return status;
            if (check.fault_vector) {
                decode->pm_fault_vector = check.fault_vector;
                return BM_STATUS_UNSUPPORTED;
            }
        } else {
            if (!operand.segment->valid) return BM_STATUS_UNSUPPORTED;
            if ((uint32_t)operand.offset + 5U > operand.segment->limit)
                return deliver_fault(decode, 13);
        }
        /* Whole-operand preflight before effects. 286 store byte 5 is undefined;
         * preserve the inherited FF policy, not a new physical measurement. */
        words[0] = table->limit; words[1] = (uint16_t)table->base;
        words[2] = (uint16_t)(0xff00U | (table->base >> 16U));
        for (unsigned i = 0; i < 3; ++i) {
            status = operand_access(decode, operand.segment,
                (uint16_t)(operand.offset + 2U * i), 2, !load, &words[i]);
            if (status != BM_STATUS_OK) return status;
        }
        if (load) {
            table->limit = words[0];
            table->base = (uint32_t)words[1] | ((uint32_t)(words[2] & 0xffU) << 16U);
        }
        return BM_STATUS_OK;
    }
    if (operand.reg_field != 4U && operand.reg_field != 6U) return BM_STATUS_UNSUPPORTED;
    if (operand.reg_field == 4U) return write_operand(decode, &operand, 2, arch->msw);
    if ((arch->msw & MSW_PE) && arch->cpl) return deliver_fault(decode, 13);
    status = read_operand(decode, &operand, 2, &value);
    if (status == BM_STATUS_OK) {
        /* Sticky PE, lower four bits only. LMSW never reloads a segment cache.
         * Public step still stops at the next protected boundary. */
        arch->msw = (uint16_t)((arch->msw & 0xfff1U) | (value & 0x000fU));
    }
    return status;
}

static bm_status_t execute_decimal(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t ax = arch->ax, flags = arch->flags;
    unsigned al = ax & 0xffU;
    int low_adjust = (al & 0xfU) > 9U || (flags & FLAG_AF);
    if (opcode == 0x37U || opcode == 0x3fU) { /* AAA/AAS: 286 full AX. */
        flags &= (uint16_t) ~(FLAG_AF | FLAG_CF);
        if (low_adjust) {
            ax = (uint16_t) (opcode == 0x37U ? ax + 0x106U : ax - 0x106U);
            flags |= FLAG_AF | FLAG_CF;
        }
        ax &= 0xff0fU;
        /* S/Z/P/O undefined: preserved, not a silicon behavior claim. */
    } else {
        if (opcode == 0x27U || opcode == 0x2fU) {
            unsigned old_al = al;
            int high_adjust = old_al > 0x99U || (flags & FLAG_CF);
            flags &= (uint16_t) ~(FLAG_AF | FLAG_CF);
            if (low_adjust) {
                al = (opcode == 0x27U ? al + 6U : al - 6U) & 0xffU;
                flags |= FLAG_AF;
                /* DAS retains the low-stage borrow even without a high
                 * adjustment (e.g. AL=0, AF=1, CF=0). DAA does not. */
                if (opcode == 0x2fU && old_al < 6U)
                    flags |= FLAG_CF;
            }
            if (high_adjust) {
                al = (opcode == 0x27U ? al + 0x60U : al - 0x60U) & 0xffU;
                flags |= FLAG_CF;
            }
            ax = (uint16_t) ((ax & 0xff00U) | al);
        } else { /* AAM/AAD: fetch the radix before changing any state. */
            uint8_t radix;
            bm_status_t status = next_byte(decode, &radix);
            if (status != BM_STATUS_OK)
                return status;
            if (opcode == 0xd4U) {
                if (radix == 0U)
                    return deliver_fault(decode, 0U);
                ax = (uint16_t) (((al / radix) << 8U) | (al % radix));
            } else
                ax = (uint16_t) ((al + (ax >> 8U) * radix) & 0xffU);
            al = ax & 0xffU;
            /* AAM/AAD A/C/O undefined: preserve them explicitly. */
        }
        flags &= (uint16_t) ~(FLAG_SF | FLAG_ZF | FLAG_PF);
        if (al == 0U) flags |= FLAG_ZF;
        if (al & 0x80U) flags |= FLAG_SF;
        if (even_parity((uint8_t) al)) flags |= FLAG_PF;
    }
    arch->ax = ax;
    arch->flags = flags;
    return BM_STATUS_OK;
}

static bm_status_t execute_divide(decoded_286_t *decode, unsigned size,
                                  const operand_286_t *operand)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t source;
    uint32_t dividend = size == 1U ? arch->ax :
        ((uint32_t) arch->dx << 16U) | arch->ax;
    uint32_t quotient = 0U, remainder = 0U;
    int fault;
    bm_status_t status = read_operand(decode, operand, size, &source);
    if (status != BM_STATUS_OK)
        return status;
    fault = source == 0U;
    if (!fault && operand->reg_field == 7U) {
        /* int64_t also safely represents INT32_MIN / -1 before checking
         * the guest quotient range. No host overflow or signed narrowing. */
        int64_t numerator = size == 1U ? signed_operand(arch->ax, 2U) :
            (int64_t) dividend - ((dividend & UINT32_C(0x80000000)) ?
                                  INT64_C(4294967296) : 0);
        int64_t divisor = signed_operand(source, size);
        int64_t q = numerator / divisor;
        int64_t r = numerator % divisor; /* C11: truncation toward zero. */
        fault = q < (size == 1U ? -128 : -32768) ||
                q > (size == 1U ? 127 : 32767);
        quotient = (uint32_t) q;
        remainder = (uint32_t) r;
    } else if (!fault) {
        quotient = dividend / source;
        remainder = dividend % source;
        fault = quotient > (size == 1U ? 0xffU : 0xffffU);
    }
    if (fault)
        return deliver_fault(decode, 0U);
    if (size == 1U)
        arch->ax = (uint16_t) ((quotient & 0xffU) | ((remainder & 0xffU) << 8U));
    else {
        arch->ax = (uint16_t) quotient;
        arch->dx = (uint16_t) remainder;
    }
    /* All arithmetic flags are undefined after division. Preserve them as
     * a deterministic emulator policy, not measured silicon behavior. */
    return BM_STATUS_OK;
}

static bm_status_t execute_arithmetic(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand = {0};
    uint16_t destination, source = 0U, result, flags;
    uint8_t immediate;
    unsigned operation, form, size;
    bm_status_t status;
    if (opcode == 0x27U || opcode == 0x2fU || opcode == 0x37U ||
        opcode == 0x3fU || opcode == 0xd4U || opcode == 0xd5U)
        return execute_decimal(decode, opcode);
    if (opcode == 0x98U || opcode == 0x99U) { /* CBW, CWD; FLAGS unchanged. */
        if (opcode == 0x98U)
            arch->ax = (uint16_t) signed_operand(arch->ax, 1U);
        else
            arch->dx = (arch->ax & 0x8000U) ? 0xffffU : 0U;
        return BM_STATUS_OK;
    }
    if (opcode == 0x69U || opcode == 0x6bU) {
        status = decode_operand(decode, &operand);
        return status == BM_STATUS_OK ? execute_multiply(decode, opcode, &operand) : status;
    }
    if (opcode == 0xc0U || opcode == 0xc1U ||
        (opcode >= 0xd0U && opcode <= 0xd3U))
        return execute_shift(decode, opcode);
    if (opcode <= 0x3dU && (opcode & 7U) <= 5U) {
        operation = opcode >> 3;
        form = opcode & 7U;
        size = (form & 1U) ? 2U : 1U;
        if (form < 4U) {
            status = decode_operand(decode, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (form < 2U) {
                status = operation == 7U ? read_operand(decode, &operand, size, &destination) :
                    read_rmw_operand(decode, &operand, size, &destination);
                source = size == 1U ? byte_register(arch, operand.reg_field) :
                    *word_register(arch, operand.reg_field);
            } else {
                destination = size == 1U ?
                    byte_register(arch, operand.reg_field) :
                    *word_register(arch, operand.reg_field);
                status = read_operand(decode, &operand, size, &source);
            }
            if (status != BM_STATUS_OK)
                return status;
            alu_calculate(operation, size, destination, source, arch->flags,
                          0, &result, &flags);
            if (operation != 7U) {
                if (form < 2U) {
                    status = write_operand(decode, &operand, size, result);
                    if (status != BM_STATUS_OK)
                        return status;
                } else if (size == 1U)
                    set_byte_register(arch, operand.reg_field, (uint8_t) result);
                else
                    *word_register(arch, operand.reg_field) = result;
            }
            arch->flags = flags;
            return BM_STATUS_OK;
        }
        if (size == 1U) {
            status = next_byte(decode, &immediate);
            if (status != BM_STATUS_OK)
                return status;
            source = immediate;
            destination = byte_register(arch, 0U);
        } else {
            status = next_word(decode, &source);
            if (status != BM_STATUS_OK)
                return status;
            destination = arch->ax;
        }
        alu_calculate(operation, size, destination, source, arch->flags,
                      0, &result, &flags);
        if (operation != 7U) {
            if (size == 1U)
                set_byte_register(arch, 0U, (uint8_t) result);
            else
                arch->ax = result;
        }
        arch->flags = flags;
        return BM_STATUS_OK;
    }
    if (opcode >= 0x40U && opcode <= 0x4fU) {
        operation = opcode < 0x48U ? 0U : 5U;
        operand.reg_number = opcode & 7U;
        destination = *word_register(arch, operand.reg_number);
        alu_calculate(operation, 2U, destination, 1U, arch->flags,
                      1, &result, &flags);
        *word_register(arch, operand.reg_number) = result;
        arch->flags = flags;
        return BM_STATUS_OK;
    }
    if (opcode == 0x80U || opcode == 0x81U || opcode == 0x83U ||
        opcode == 0x84U || opcode == 0x85U || opcode == 0xf6U ||
        opcode == 0xf7U || opcode == 0xfeU || opcode == 0xffU) {
        status = decode_operand(decode, &operand);
        if (status != BM_STATUS_OK)
            return status;
        if (decode->protected_subset && opcode == 0xffU &&
            operand.reg_field == 7U)
            return BM_STATUS_UNSUPPORTED; /* Reserved form remains separate. */
        if (decode->lock_prefix && (!operand.memory ||
            ((opcode == 0x80U || opcode == 0x81U || opcode == 0x83U) && operand.reg_field == 7U) ||
            ((opcode == 0xf6U || opcode == 0xf7U) && operand.reg_field != 2U && operand.reg_field != 3U) ||
            ((opcode == 0xfeU || opcode == 0xffU) && operand.reg_field > 1U)))
            return BM_STATUS_UNSUPPORTED; /* Not yet a supported LOCK form; not guest #UD. */
        if (opcode == 0xffU && operand.reg_field > 1U)
            return execute_ff_control(decode, &operand);
        size = (opcode == 0x80U || opcode == 0x84U ||
                opcode == 0xf6U || opcode == 0xfeU) ? 1U : 2U;
        if (opcode == 0x80U || opcode == 0x81U || opcode == 0x83U) {
            operation = operand.reg_field;
            if (opcode == 0x81U) {
                status = next_word(decode, &source);
            } else {
                status = next_byte(decode, &immediate);
                if (status == BM_STATUS_OK)
                    source = opcode == 0x83U ?
                        (uint16_t) (int16_t) (int8_t) immediate : immediate;
            }
            if (status != BM_STATUS_OK)
                return status;
        } else if (opcode == 0x84U || opcode == 0x85U) {
            operation = 4U;
            source = size == 1U ? byte_register(arch, operand.reg_field) :
                *word_register(arch, operand.reg_field);
        } else if (opcode == 0xf6U || opcode == 0xf7U) {
            if (operand.reg_field == 4U || operand.reg_field == 5U)
                return execute_multiply(decode, opcode, &operand);
            if (operand.reg_field >= 6U)
                return execute_divide(decode, size, &operand);
            if (operand.reg_field == 0U) {
                operation = 4U; /* TEST */
                if (size == 1U) {
                    status = next_byte(decode, &immediate);
                    if (status == BM_STATUS_OK)
                        source = immediate;
                } else
                    status = next_word(decode, &source);
                if (status != BM_STATUS_OK)
                    return status;
            } else if (operand.reg_field == 2U || operand.reg_field == 3U) {
                operation = operand.reg_field == 2U ? 8U : 5U;
            } else
                return BM_STATUS_UNSUPPORTED; /* /1 invalid; #6 pending. */
        } else {
            if (operand.reg_field > 1U)
                return BM_STATUS_UNSUPPORTED; /* FE /2+ invalid; #6 pending. */
            operation = operand.reg_field == 0U ? 0U : 5U;
        }
        if (opcode == 0x84U || opcode == 0x85U ||
            ((opcode == 0xf6U || opcode == 0xf7U) && operand.reg_field == 0U) ||
            ((opcode == 0x80U || opcode == 0x81U || opcode == 0x83U) && operation == 7U))
            status = read_operand(decode, &operand, size, &destination);
        else status = read_rmw_operand(decode, &operand, size, &destination);
        if (status != BM_STATUS_OK)
            return status;
        if (operation == 8U) { /* NOT, including no FLAGS change. */
            result = (uint16_t) (destination ^ (size == 1U ? 0xffU : 0xffffU));
            return write_operand(decode, &operand, size, result);
        }
        if ((opcode == 0xf6U || opcode == 0xf7U) &&
            operand.reg_field == 3U) {
            source = destination;
            destination = 0U; /* NEG = 0 - operand. */
        } else if (opcode == 0xfeU || opcode == 0xffU)
            source = 1U;
        alu_calculate(operation, size, destination, source, arch->flags,
                      opcode == 0xfeU || opcode == 0xffU,
                      &result, &flags);
        if (opcode != 0x84U && opcode != 0x85U &&
            !(opcode == 0xf6U && operand.reg_field == 0U) &&
            !(opcode == 0xf7U && operand.reg_field == 0U) &&
            !(opcode == 0x80U && operation == 7U) &&
            !(opcode == 0x81U && operation == 7U) &&
            !(opcode == 0x83U && operation == 7U)) {
            status = write_operand(decode, &operand, size, result);
            if (status != BM_STATUS_OK)
                return status;
        }
        arch->flags = flags;
        return BM_STATUS_OK;
    }
    if (opcode == 0xa8U || opcode == 0xa9U) {
        size = opcode == 0xa8U ? 1U : 2U;
        if (size == 1U) {
            status = next_byte(decode, &immediate);
            if (status != BM_STATUS_OK)
                return status;
            source = immediate;
            destination = byte_register(arch, 0U);
        } else {
            status = next_word(decode, &source);
            if (status != BM_STATUS_OK)
                return status;
            destination = arch->ax;
        }
        alu_calculate(4U, size, destination, source, arch->flags,
                      0, &result, &flags);
        arch->flags = flags;
        return BM_STATUS_OK;
    }
    return execute_stack_control(decode, opcode);
}

/* Shared logical port transfer: odd words split, including FFFF -> 0000.
 * Completed endpoint effects cannot be undone if a later fragment fails. */
static bm_status_t io_access(decoded_286_t *decode, uint16_t port,
                             unsigned size, int output, uint16_t *value)
{
    bm_bus_transaction_t t = {0};
    uint16_t input = 0;
    unsigned fragment_size, count;
    bm_status_t status;
    fragment_size = size == 2U && (port & 1U) ? 1U : size;
    count = size / fragment_size;
    for (unsigned i = 0; i < count; ++i) {
        t.space = BM_ADDRESS_IO;
        t.operation = output ? BM_BUS_WRITE : BM_BUS_READ;
        t.address = (uint16_t) (port + i); /* 16-bit I/O address space. */
        t.size = fragment_size;
        t.alignment = fragment_size;
        t.endianness = BM_ENDIAN_LITTLE;
        t.attributes = decode->state->lock_active ? BM_BUS_TRANSACTION_LOCKED : 0U;
        t.wait_states = 0;
        t.value = output ? (fragment_size == 1U ?
            (uint8_t) (*value >> (i * 8U)) : *value) : 0U;
        status = decode->state->config.access(decode->state->config.access_context, &t);
        if (status != BM_STATUS_OK)
            return status; /* Completed endpoint effects are never retried. */
        decode->waits += t.wait_states;
        if (!output)
            input |= fragment_size == 1U ?
                (uint16_t) ((uint8_t) t.value << (i * 8U)) : (uint16_t) t.value;
    }
    if (!output)
        *value = input;
    return BM_STATUS_OK;
}

static bm_status_t execute_io(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t port = arch->dx, value = arch->ax;
    unsigned size = (opcode & 1U) ? 2U : 1U;
    int output = (opcode & 2U) != 0U;
    bm_status_t status;
    if (opcode < 0xe8U) {
        uint8_t immediate;
        status = next_byte(decode, &immediate);
        if (status != BM_STATUS_OK)
            return status;
        port = immediate;
    }
    if (decode->protected_subset && arch->cpl > ((arch->flags >> 12) & 3U))
        return deliver_fault(decode, 13U);
    status = io_access(decode, port, size, output, &value);
    if (status != BM_STATUS_OK)
        return status;
    if (!output) {
        if (size == 1U)
            set_byte_register(arch, 0U, (uint8_t) value);
        else
            arch->ax = value;
    }
    return BM_STATUS_OK;
}

/* Stage the register adjustments described by Intel's string restart memo.
 * The corrected-REP contract is a functional model, not B2/B3/C2 emulation.
 * SCAS uses its architectural DI (PRM B-100), not the memo's anomalous SI;
 * LODS uses pre-operation state where the memo supplies no adjustment.
 * Publish only after successful guest delivery, never on a host failure. */
static bm_status_t string_fault(decoded_286_t *decode, bm_status_t status,
    unsigned indices, unsigned count, uint16_t delta)
{
    if (status != BM_STATUS_OK && decode->pm_fault_vector != UINT8_MAX) {
        decode->string_fault_adjust = (uint8_t)(indices | (decode->repeat_prefix ? count << 2 : 0));
        decode->string_delta = delta;
    }
    return status;
}

static bm_status_t execute_string_io(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    unsigned size = (opcode & 1U) ? 2U : 1U;
    int output = (opcode & 2U) != 0U;
    const bm_286_segment_state_t *segment = output ? segment_register(arch,
        decode->override_segment >= 0 ? (unsigned) decode->override_segment : 3U) : &arch->es;
    uint16_t offset = output ? arch->si : arch->di, value = 0;
    uint16_t delta = (uint16_t) ((arch->flags & FLAG_DF) ? 0U - size : size);
    bm_status_t status;
    /* Preflight guest segment faults before consuming an input.
     * This is functional policy, not certified fault precedence.
     * Host callback failures may still consume input or commit partial output. */
    if (decode->protected_subset) {
        if (arch->cpl > ((arch->flags >> 12) & 3U))
            return string_fault(decode, deliver_fault(decode, 13), output ? 1 : 2,
                output ? 2 : 1, delta);
        status = protected_operand_check(decode, segment, offset, size, !output);
        if (status != BM_STATUS_OK) return string_fault(decode, status, output ? 1 : 2, 2, delta);
    } else if (!segment->valid || (uint32_t) offset + size - 1U > segment->limit)
        return BM_STATUS_UNSUPPORTED;
    if (decode->lock_prefix) {
        status = begin_bus_lock(decode->state);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (output) {
        status = (decode->protected_subset ? operand_access : data_access)(decode, segment, offset, size, 0, &value);
        if (status == BM_STATUS_OK)
            status = io_access(decode, arch->dx, size, 1, &value);
    } else {
        status = io_access(decode, arch->dx, size, 0, &value);
        if (status == BM_STATUS_OK)
            status = (decode->protected_subset ? operand_access : data_access)(decode, segment, offset, size, 1, &value);
    }
    if (status != BM_STATUS_OK)
        return status;
    if (output)
        arch->si = (uint16_t) (offset + delta);
    else
        arch->di = (uint16_t) (offset + delta);
    return BM_STATUS_OK;
}

/* One string element shared by real and private protected execution.
 * CMPS retains the inherited destination-before-source read order, but this
 * functional bus sequence is not a claim of physical 286 fault precedence. */
static bm_status_t execute_string(decoded_286_t *decode, uint8_t opcode)
{
    if (opcode >= 0x6cU && opcode <= 0x6fU)
        return execute_string_io(decode, opcode);
    bm_286_arch_state_t *arch = &decode->state->arch;
    const bm_286_segment_state_t *source_segment = segment_register(arch,
        decode->override_segment >= 0 ? (unsigned) decode->override_segment : 3U);
    unsigned kind = opcode & 0xfeU, size = (opcode & 1U) ? 2U : 1U;
    uint16_t source = size == 1U ? (uint8_t) arch->ax : arch->ax;
    uint16_t destination = 0U, flags = arch->flags, discarded;
    uint16_t delta = (uint16_t) ((arch->flags & FLAG_DF) ? 0U - size : size);
    int uses_source = kind == 0xa4U || kind == 0xa6U || kind == 0xacU;
    int uses_destination = kind != 0xacU;
    bm_status_t status;
    if (decode->protected_subset) {
        /* CMPS validates destination first, matching its documented restart
         * stages. Complete element preflight before endpoint effects remains
         * our functional bus policy (not measured silicon bus sequencing). */
        if (kind == 0xa6U) {
            status = protected_operand_check(decode, &arch->es, arch->di, size, 0);
            if (status != BM_STATUS_OK) return string_fault(decode, status, 2, 1, delta);
        }
        if (uses_source) {
            status = protected_operand_check(decode, source_segment, arch->si, size, 0);
            if (status != BM_STATUS_OK) return string_fault(decode, status,
                kind == 0xa6U ? 3 : kind == 0xa4U ? 1 : 0,
                kind == 0xa6U ? 2 : kind == 0xa4U ? 1 : 0, delta);
        }
        if (uses_destination && kind != 0xa6U) {
            status = protected_operand_check(decode, &arch->es, arch->di, size,
                kind == 0xa4U || kind == 0xaaU);
            if (status != BM_STATUS_OK) return string_fault(decode, status,
                kind == 0xa4U ? 3 : 2, 2, delta);
        }
    }
    if (kind == 0xa6U || kind == 0xaeU) {
        status = (decode->protected_subset ? operand_access : data_access)(decode, &arch->es, arch->di, size, 0, &destination);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (uses_source) {
        status = (decode->protected_subset ? operand_access : data_access)(decode, source_segment, arch->si, size, 0, &source);
        if (status != BM_STATUS_OK)
            return status;
    }
    if (kind == 0xa4U || kind == 0xaaU) {
        status = (decode->protected_subset ? operand_access : data_access)(decode, &arch->es, arch->di, size, 1, &source);
        if (status != BM_STATUS_OK)
            return status;
    } else if (kind == 0xa6U || kind == 0xaeU)
        alu_calculate(7U, size, source, destination, flags, 0, &discarded, &flags);
    /* No potentially failing access remains. CX is never consumed without
     * REP, even when zero. Index arithmetic wraps, individual words do not
     * bypass segment limits. Completed endpoint writes are not rolled back. */
    if (kind == 0xacU) {
        if (size == 1U)
            set_byte_register(arch, 0U, (uint8_t) source);
        else
            arch->ax = source;
    }
    if (uses_source)
        arch->si = (uint16_t) (arch->si + delta);
    if (uses_destination)
        arch->di = (uint16_t) (arch->di + delta);
    arch->flags = flags;
    return BM_STATUS_OK;
}

static bm_status_t execute_data(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand;
    bm_286_segment_state_t *segment;
    uint16_t value, other, offset;
    uint8_t immediate;
    unsigned size;
    bm_status_t status;
    if (arch->msw & MSW_PE) {
        if (!decode->protected_subset)
            return BM_STATUS_UNSUPPORTED; /* Public/internal default PE gate. */
        /* Positive list: unreviewed real-mode handlers cannot leak into PE. */
        if (!((opcode <= 0x3dU && (opcode & 7U) <= 5U) ||
              opcode == 0x27U || opcode == 0x2fU || opcode == 0x37U || opcode == 0x3fU ||
              (opcode >= 0x40U && opcode <= 0x63U) ||
              opcode == 0x69U || opcode == 0x6bU ||
              opcode == 0x80U || opcode == 0x81U || opcode == 0x83U ||
              (opcode >= 0x84U && opcode <= 0x87U) ||
              opcode == 0x98U || opcode == 0x99U || opcode == 0x9bU ||
              opcode == 0xa8U || opcode == 0xa9U ||
              (opcode >= 0xc0U && opcode <= 0xc5U) ||
              (opcode >= 0xccU && opcode <= 0xceU) ||
              (opcode >= 0xd0U && opcode <= 0xd5U) || (opcode >= 0xd8U && opcode <= 0xdfU) ||
              opcode == 0xf6U || opcode == 0xf7U || opcode == 0xfeU ||
              (opcode >= 0x6cU && opcode <= 0x7fU) ||
              (opcode >= 0x88U && opcode <= 0x8fU) ||
              (opcode >= 0x90U && opcode <= 0x97U) ||
              (opcode >= 0x9cU && opcode <= 0x9fU) ||
              (opcode >= 0xa0U && opcode <= 0xafU) ||
              (opcode >= 0xb0U && opcode <= 0xbfU) ||
              (opcode >= 0xe0U && opcode <= 0xe8U) ||
              (opcode >= 0xecU && opcode <= 0xefU) ||
              (opcode >= 0xf8U && opcode <= 0xfdU) || opcode == 0xf4U || opcode == 0xf5U ||
              opcode == 0xc6U || opcode == 0xc7U || opcode == 0xd7U ||
              opcode == 0x06U || opcode == 0x0eU || opcode == 0x16U || opcode == 0x1eU ||
              opcode == 0x07U || opcode == 0x17U || opcode == 0x1fU ||
              opcode == 0x68U || opcode == 0x6aU || opcode == 0xffU ||
              opcode == 0xc2U || opcode == 0xc3U || opcode == 0xc8U || opcode == 0xc9U ||
              opcode == 0xcaU || opcode == 0xcbU || opcode == 0x9aU ||
              opcode == 0x0fU || opcode == 0xe9U || opcode == 0xebU || opcode == 0xeaU))
            return BM_STATUS_UNSUPPORTED;
    }
    if (opcode == 0x90U)
        return BM_STATUS_OK;
    if (opcode == 0x0fU)
        return execute_system(decode);
    if (opcode == 0x9bU) {
        if ((arch->msw & (MSW_MP | MSW_TS)) == (MSW_MP | MSW_TS))
            return deliver_fault(decode, 7U);
        /* This core currently models an unpopulated extension interface:
         * BUSY/ERROR inactive. This is WAIT completion, not x87 execution. */
        return BM_STATUS_OK;
    }
    if (opcode >= 0xd8U && opcode <= 0xdfU) {
        if (arch->msw & (MSW_EM | MSW_TS))
            return deliver_fault(decode, 7U);
        /* The selected machine profile has no populated 80287. The 80286
         * still consumes ModR/M and displacement and calculates the effective
         * address, but no PEREQ operand transfer follows. Consequently a
         * memory destination is untouched and a memory source is not read.
         * This is the architectural absence path, not an x87 no-op model: a
         * populated extension remains outside this contract. */
        return decode_operand(decode, &operand);
    }
    if (opcode == 0x63U) { /* ARPL: Intel B-21; flags other than ZF preserved. */
        if (!(arch->msw & MSW_PE)) return deliver_fault(decode, 6U);
        status = decode_operand(decode, &operand);
        if (status != BM_STATUS_OK) return status;
        if (operand.memory) {
            status = protected_operand_check(decode, operand.segment, operand.offset, 2U, 1);
            if (status != BM_STATUS_OK) return status;
        }
        status = read_operand(decode, &operand, 2U, &value);
        if (status != BM_STATUS_OK) return status;
        other = *word_register(arch, operand.reg_field) & 3U;
        if ((value & 3U) < other) {
            status = write_operand(decode, &operand, 2U, (uint16_t)((value & 0xfffcU) | other));
            if (status != BM_STATUS_OK) return status;
            arch->flags |= FLAG_ZF;
        } else arch->flags &= (uint16_t) ~FLAG_ZF;
        return BM_STATUS_OK;
    }
    if (opcode == 0x62U) { /* BOUND r16, m16:16 (Intel PRM B-22). */
        status = decode_operand(decode, &operand);
        if (status != BM_STATUS_OK)
            return status;
        if (!operand.memory)
            return deliver_fault(decode, 6U);
        /* Preflight ordering is not physical bus evidence. Invalid imported
         * caches remain a gap, not a fabricated guest protection fault. */
        if (decode->protected_subset) {
            status = protected_operand_check(decode, operand.segment, operand.offset, 4U, 0);
            if (status != BM_STATUS_OK) return status;
        } else {
            if (!operand.segment->valid) return BM_STATUS_UNSUPPORTED;
            if ((uint32_t) operand.offset + 3U > operand.segment->limit)
                return deliver_fault(decode, 13U);
        }
        status = read_operand(decode, &operand, 2U, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = operand_access(decode, operand.segment,
            (uint16_t) (operand.offset + 2U), 2U, 0, &other);
        if (status != BM_STATUS_OK)
            return status;
        if (signed_operand(*word_register(arch, operand.reg_field), 2U) <
                signed_operand(value, 2U) ||
            signed_operand(*word_register(arch, operand.reg_field), 2U) >
                signed_operand(other, 2U))
            return deliver_fault(decode, 5U);
        return BM_STATUS_OK;
    }
    if ((opcode >= 0x6cU && opcode <= 0x6fU) ||
        (opcode >= 0xa4U && opcode <= 0xa7U) ||
        (opcode >= 0xaaU && opcode <= 0xafU))
        return execute_string(decode, opcode);
    if (opcode == 0x8dU || opcode == 0xc4U || opcode == 0xc5U) {
        status = decode_operand(decode, &operand);
        if (status != BM_STATUS_OK)
            return status;
        if (!operand.memory)
            return deliver_fault(decode, 6U); /* LEA/LDS/LES require memory. */
        if (opcode == 0x8dU) {
            /* LEA computes only an offset: no data access or segment check. */
            *word_register(arch, operand.reg_field) = operand.offset;
            return BM_STATUS_OK;
        }
        status = pointer_preflight(decode, &operand);
        if (status != BM_STATUS_OK)
            return status;
        status = read_operand(decode, &operand, 2U, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = operand_access(decode, operand.segment,
            (uint16_t) (operand.offset + 2U), 2U, 0, &other);
        if (status != BM_STATUS_OK)
            return status;
        /* Source may itself use DS/ES or the destination register. Read it
         * completely before replacing either part of the destination. */
        status = load_data_segment(decode, opcode == 0xc4U ? 0U : 3U, other);
        if (status != BM_STATUS_OK)
            return status;
        *word_register(arch, operand.reg_field) = value;
        return BM_STATUS_OK;
    }
    if (opcode == 0xd7U) {
        segment = segment_register(arch, decode->override_segment >= 0 ?
            (unsigned) decode->override_segment : 3U);
        offset = (uint16_t) (arch->bx + (arch->ax & 0xffU));
        status = operand_access(decode, segment, offset, 1U, 0, &value);
        if (status == BM_STATUS_OK)
            set_byte_register(arch, 0U, (uint8_t) value);
        return status;
    }
    if (opcode >= 0xccU && opcode <= 0xceU)
        return software_interrupt(decode, opcode);
    if ((opcode >= 0x9cU && opcode <= 0x9fU) || opcode == 0xf5U ||
        opcode == 0xf8U || opcode == 0xf9U || opcode == 0xfcU || opcode == 0xfdU)
        return execute_flags(decode, opcode);
    if (opcode == 0xcfU)
        return interrupt_return(decode);
    if (opcode == 0xfaU || opcode == 0xfbU) {
        if (decode->protected_subset && arch->cpl > ((arch->flags >> 12) & 3U))
            return deliver_fault(decode, 13U);
        if (opcode == 0xfaU)
            arch->flags &= (uint16_t) ~FLAG_IF;
        else {
            arch->flags |= FLAG_IF;
            decode->next_shadow = BM_286_SHADOW_INTR_ONLY;
        }
        return BM_STATUS_OK;
    }
    if (opcode == 0xf4U) {
        if (decode->protected_subset && arch->cpl)
            return deliver_fault(decode, 13U);
        arch->halted = 1U;
        return BM_STATUS_OK;
    }
    if ((opcode >= 0xe4U && opcode <= 0xe7U) ||
        (opcode >= 0xecU && opcode <= 0xefU))
        return execute_io(decode, opcode);
    if (opcode >= 0x91U && opcode <= 0x97U) {
        uint16_t *reg = word_register(arch, opcode & 7U);
        value = arch->ax;
        arch->ax = *reg;
        *reg = value;
        return BM_STATUS_OK;
    }
    if (opcode >= 0xb0U && opcode <= 0xb7U) {
        status = next_byte(decode, &immediate);
        if (status == BM_STATUS_OK)
            set_byte_register(arch, opcode & 7U, immediate);
        return status;
    }
    if (opcode >= 0xb8U && opcode <= 0xbfU) {
        status = next_word(decode, &value);
        if (status == BM_STATUS_OK)
            *word_register(arch, opcode & 7U) = value;
        return status;
    }
    if (opcode >= 0xa0U && opcode <= 0xa3U) {
        status = next_word(decode, &offset);
        if (status != BM_STATUS_OK)
            return status;
        segment = segment_register(arch, decode->override_segment >= 0 ?
            (unsigned) decode->override_segment : 3U);
        size = (opcode & 1U) ? 2U : 1U;
        value = size == 1U ? byte_register(arch, 0U) : arch->ax;
        status = operand_access(decode, segment, offset, size,
                             (opcode & 2U) != 0U, &value);
        if (status == BM_STATUS_OK && (opcode & 2U) == 0U) {
            if (size == 1U)
                set_byte_register(arch, 0U, (uint8_t) value);
            else
                arch->ax = value;
        }
        return status;
    }
    if ((opcode >= 0x88U && opcode <= 0x8bU) ||
        opcode == 0x8cU || opcode == 0x8eU ||
        opcode == 0xc6U || opcode == 0xc7U ||
        opcode == 0x86U || opcode == 0x87U) {
        status = decode_operand(decode, &operand);
        if (status != BM_STATUS_OK)
            return status;
        /* Documented MOV/XCHG register forms have no external data window.
         * MOV-to-segment still needs descriptor exclusion after its source. */
        if (decode->protected_subset && !operand.memory && opcode != 0x8eU)
            decode->lock_prefix = 0;
        size = ((opcode & 1U) || opcode == 0x8cU || opcode == 0x8eU)
            ? 2U : 1U;
        if (opcode == 0x86U || opcode == 0x87U) {
            if (operand.memory)
                decode->lock_prefix = 1U; /* XCHG asserts LOCK without F0. */
            status = read_rmw_operand(decode, &operand, size, &value);
            if (status != BM_STATUS_OK)
                return status;
            other = size == 1U ? byte_register(arch, operand.reg_field) :
                *word_register(arch, operand.reg_field);
            status = write_operand(decode, &operand, size, other);
            if (status != BM_STATUS_OK)
                return status;
            if (size == 1U)
                set_byte_register(arch, operand.reg_field, (uint8_t) value);
            else
                *word_register(arch, operand.reg_field) = value;
            return BM_STATUS_OK;
        }
        if (opcode == 0x8cU) {
            segment = segment_register(arch, operand.reg_field);
            if (segment == NULL)
                return BM_STATUS_UNSUPPORTED; /* Invalid field: #6 pending. */
            return write_operand(decode, &operand, 2U, segment->selector);
        }
        if (opcode == 0x8eU) {
            segment = segment_register(arch, operand.reg_field);
            if (operand.reg_field == 1U)
                return decode->lock_prefix && !decode->protected_subset ? BM_STATUS_UNSUPPORTED :
                    deliver_fault(decode, 6U); /* Real LOCK precedence remains outside its subset. */
            if (segment == NULL)
                return BM_STATUS_UNSUPPORTED; /* Reserved-field aliases unverified. */
            if (decode->protected_subset && !operand.memory) {
                value = *word_register(arch, operand.reg_number);
                status = BM_STATUS_OK;
            } else status = read_operand(decode, &operand, 2U, &value);
            if (status != BM_STATUS_OK)
                return status;
            if (decode->protected_subset && decode->lock_prefix) {
                status = begin_bus_lock(decode->state);
                if (status != BM_STATUS_OK) return status;
            }
            status = load_data_segment(decode, operand.reg_field, value);
            if (status != BM_STATUS_OK)
                return status;
            if (operand.reg_field == 2U)
                decode->next_shadow = BM_286_SHADOW_SS_LOAD;
            return BM_STATUS_OK;
        }
        if (opcode == 0xc6U || opcode == 0xc7U) {
            if (operand.reg_field != 0U)
                return BM_STATUS_UNSUPPORTED; /* Invalid group encoding. */
            if (size == 1U) {
                status = next_byte(decode, &immediate);
                if (status != BM_STATUS_OK)
                    return status;
                value = immediate;
            } else {
                status = next_word(decode, &value);
            }
            if (status != BM_STATUS_OK)
                return status;
            return write_operand(decode, &operand, size, value);
        }
        if ((opcode & 2U) != 0U) {
            status = read_operand(decode, &operand, size, &value);
            if (status == BM_STATUS_OK) {
                if (size == 1U)
                    set_byte_register(arch, operand.reg_field, (uint8_t) value);
                else
                    *word_register(arch, operand.reg_field) = value;
            }
            return status;
        }
        value = size == 1U ? byte_register(arch, operand.reg_field) :
            *word_register(arch, operand.reg_field);
        return write_operand(decode, &operand, size, value);
    }
    return execute_arithmetic(decode, opcode);
}

static bm_status_t step_protected(bm_cpu_t *cpu, bm_286_boundary_t *out_boundary)
{
    bm_286_private_t *state;
    bm_286_boundary_t boundary = {0};
    decoded_286_t decode = {0};
    bm_286_pm_delivery_state_t delivery_state = {0};
    bm_286_pm_delivery_result_t delivered;
    bm_286_pm_request_t request = {0};
    bm_status_t status;
    uint8_t opcode = 0, sampled_tf, repeat = 0, repeat_more = 0;
    bool returned = false, boundary_trap;
    if (!is_286_cpu(cpu) || !out_boundary) return BM_STATUS_INVALID_ARGUMENT;
    memset(out_boundary, 0, sizeof(*out_boundary));
    state = cpu->context;
    if (state->stopped || (state->lock_active && !(state->rep_active && state->rep_lock)))
        return BM_STATUS_INVALID_STATE;
    boundary.instruction_ip = state->arch.ip;
    boundary.instruction_address = (state->arch.cs.base + state->arch.ip) & ADDRESS_MASK;
    boundary.timing = BM_286_TIMING_UNKNOWN;
    if (state->hold_line && !state->lock_active) {
        if (!state->hold_acknowledged) {
            state->hold_acknowledged = 1;
            if (state->config.hold_ack) state->config.hold_ack(state->config.pin_context, 1);
        }
        boundary.kind = BM_286_BOUNDARY_HOLD;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    request.source = BM_286_PM_BOUNDARY;
    request.restart_ip = request.next_ip = state->arch.ip;
    request.intr_line = state->intr_line != 0;
    boundary_trap = !state->arch.shutdown && state->arch.trap_pending &&
        state->arch.interrupt_shadow != BM_286_SHADOW_SS_LOAD;
    /* A pending accepted event ends the diagnostic REP exclusion window. */
    if (state->arch.interrupt_shadow != BM_286_SHADOW_SS_LOAD &&
        (state->arch.trap_pending ||
         (state->arch.nmi_pending && !state->arch.nmi_blocked) ||
         (state->intr_line && (state->arch.flags & FLAG_IF) && !state->arch.interrupt_shadow)))
        end_bus_lock(state);
    status = bm_286_pm_deliver(&state->arch, &state->config, &delivery_state,
        &request, &delivered);
    if (status != BM_STATUS_OK &&
        (status != BM_STATUS_IDLE || delivered.accepted)) goto stop;
    if (status == BM_STATUS_OK) {
        state->rep_active = 0;
        /* Classify by accepted origin, never by the controller's vector.
         * INTR vector 1 is not a sampled trap. A delivery rejection that
         * escalates to a protection fault/#DF is an exception boundary. */
        boundary.kind = delivered.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
            (boundary_trap || delivered.attempts > 1 ?
                BM_286_BOUNDARY_EXCEPTION : BM_286_BOUNDARY_INTERRUPT);
        boundary.has_vector = (uint8_t)delivered.entered;
        boundary.vector = delivered.entered ? delivered.vector : 0;
        boundary.bus_wait_cycles = delivered.waits;
        goto complete;
    }
    if (state->arch.shutdown || state->arch.halted) {
        boundary.kind = state->arch.shutdown ? BM_286_BOUNDARY_SHUTDOWN : BM_286_BOUNDARY_HALT;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    decode.state = state; decode.cursor = state->arch.ip;
    decode.override_segment = -1; decode.protected_subset = 1;
    decode.pm_fault_vector = UINT8_MAX; /* #DE is vector zero, not absence. */
    sampled_tf = (uint8_t)((state->arch.flags & FLAG_TF) != 0);
    if (state->rep_active) {
        opcode = state->rep_opcode; repeat = state->rep_prefix;
        decode.override_segment = state->rep_segment; decode.lock_prefix = state->rep_lock;
        decode.cursor = state->rep_end_ip; status = BM_STATUS_OK;
    } else for (;;) {
        status = next_byte(&decode, &opcode);
        if (status != BM_STATUS_OK) break;
        if (opcode == 0x26 || opcode == 0x2e || opcode == 0x36 || opcode == 0x3e) {
            decode.override_segment = (opcode >> 3) & 3;
            continue;
        }
        if (opcode == 0xf2 || opcode == 0xf3) { repeat = opcode; continue; }
        if (opcode == 0xf0) { decode.lock_prefix = 1; continue; }
        break;
    }
    decode.repeat_prefix = repeat;
    if (status == BM_STATUS_OK && decode.lock_prefix &&
        state->arch.cpl > ((state->arch.flags >> 12) & 3U))
        status = deliver_fault(&decode, 13U);
    if (status == BM_STATUS_OK) {
        int locked_string = (opcode >= 0x6cU && opcode <= 0x6fU) ||
            opcode == 0xa4U || opcode == 0xa5U;
        if (decode.lock_prefix && ((repeat && !locked_string) ||
            !(locked_string || (opcode <= 0x31U && (opcode & 7U) <= 1U) ||
              opcode == 0x80U || opcode == 0x81U || opcode == 0x83U ||
              (opcode >= 0x88U && opcode <= 0x8cU) || opcode == 0x8eU ||
              (opcode >= 0x90U && opcode <= 0x97U) || (opcode >= 0xb0U && opcode <= 0xbfU) ||
              (opcode >= 0xa0U && opcode <= 0xa3U) ||
              opcode == 0xc6U || opcode == 0xc7U ||
              opcode == 0xc0U || opcode == 0xc1U ||
              (opcode >= 0xd0U && opcode <= 0xd3U) ||
              opcode == 0x86U || opcode == 0x87U || opcode == 0xf6U ||
              opcode == 0xf7U || opcode == 0xfeU || opcode == 0xffU)))
            status = BM_STATUS_UNSUPPORTED; /* Outside the documented prefix contract. */

        else if (repeat) {
            if (!((opcode >= 0x6c && opcode <= 0x6f) ||
                (opcode >= 0xa4 && opcode <= 0xa7) || (opcode >= 0xaa && opcode <= 0xaf)))
                status = BM_STATUS_UNSUPPORTED;
            else if (state->arch.cx) {
                status = execute_string(&decode, opcode);
                if (status == BM_STATUS_OK) {
                    --state->arch.cx;
                    repeat_more = state->arch.cx != 0;
                    if ((opcode & 0xfeU) == 0xa6 || (opcode & 0xfeU) == 0xae)
                        repeat_more &= ((state->arch.flags & FLAG_ZF) != 0) == (repeat == 0xf3);
                }
            }
        } else if (opcode == 0xcf) {
            bm_286_segment_load_result_t result;
            status = bm_286_pm_iret(&state->arch, &state->config, (uint16_t)decode.cursor, &result);
            decode.waits += result.waits;
            decode.task_context=result.task_context;
            if (status == BM_STATUS_OK && result.fault_vector) {
                decode.pm_fault_vector = result.fault_vector;
                decode.pm_fault_error = result.fault_error;
                status = BM_STATUS_UNSUPPORTED;
            } else if (status == BM_STATUS_OK) {
                if (!result.loaded) status = BM_STATUS_INVALID_STATE;
                else returned = true;
            }
        } else status = execute_data(&decode, opcode);
    }
    if (decode.pm_fault_vector != UINT8_MAX || decode.software_interrupt) {
        end_bus_lock(state);
        state->rep_active = 0;
        request.source = decode.software_interrupt ? BM_286_PM_SOFTWARE : BM_286_PM_EXCEPTION;
        request.vector = decode.software_interrupt ? decode.software_vector : decode.pm_fault_vector;
        request.next_ip = (uint16_t) decode.cursor;
        request.error_code = decode.pm_fault_error;
        if(decode.task_context) request.restart_ip=state->arch.ip;
        request.task_fault=decode.task_context!=0;
        request.string_fault_adjust=decode.string_fault_adjust;
        request.string_delta=decode.string_delta;
        /* Ordinary faults restart at the first prefix; a selected task owns
         * its incoming IP. Delivery stages string corrections in the outgoing
         * image, before a possible task save, never in the handler's task. */
        status = bm_286_pm_deliver(&state->arch, &state->config, &delivery_state,
            &request, &delivered);
        decode.waits += delivered.waits;
        if (status != BM_STATUS_OK) goto stop;
        boundary.kind = delivered.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
            decode.software_interrupt && delivered.attempts == 1 ? BM_286_BOUNDARY_INSTRUCTION : BM_286_BOUNDARY_EXCEPTION;
        boundary.has_vector = (uint8_t)delivered.entered;
        boundary.vector = delivered.entered ? delivered.vector : 0;
    } else {
        if (status != BM_STATUS_OK) goto stop;
        state->rep_active = repeat_more;
        if (repeat_more) {
            state->rep_opcode = opcode; state->rep_prefix = repeat;
            state->rep_lock = decode.lock_prefix; state->rep_segment = decode.override_segment;
            state->rep_end_ip = (uint16_t)decode.cursor;
        } else if (!returned) state->arch.ip = (uint16_t)decode.cursor;
        state->arch.interrupt_shadow = decode.next_shadow;
        state->arch.trap_pending = (uint8_t)(state->arch.trap_pending ||
            (sampled_tf && !decode.task_context && decode.next_shadow != BM_286_SHADOW_SS_LOAD));
        boundary.kind = repeat_more ? BM_286_BOUNDARY_REP_ITERATION : BM_286_BOUNDARY_INSTRUCTION;
    }
    if (!repeat_more) end_bus_lock(state);
    boundary.bus_wait_cycles = decode.waits;
complete:
    boundary.cpu_cycles = boundary.bus_wait_cycles; /* Known wait lower bound only. */
    *out_boundary = boundary;
    if (state->config.trace) state->config.trace(state->config.trace_context, &boundary);
    return BM_STATUS_OK;
stop:
    state->stopped = 1; /* Includes unsupported gaps; never automatic replay. */
    end_bus_lock(state);
    return status;
}

bm_status_t bm_286_step(bm_cpu_t *cpu, bm_286_boundary_t *out_boundary)
{
    bm_286_private_t *state;
    bm_286_boundary_t boundary = {0};
    decoded_286_t decode;
    bm_status_t status;
    uint8_t opcode;
    uint8_t trap_was_enabled;
    uint8_t repeat = 0U, repeat_more = 0U;
    unsigned event = 3U; /* No accepted event. */
    if (!is_286_cpu(cpu) || out_boundary == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(out_boundary, 0, sizeof(*out_boundary));
    state = cpu->context;
    if (state->stopped)
        return BM_STATUS_INVALID_STATE;
    /* Select the mode before real-mode event arbitration, HOLD or idle paths.
     * The reviewed C/D/E/F functional profile owns every protected boundary. */
    if (state->arch.msw & MSW_PE)
        return step_protected(cpu, out_boundary);
    boundary.instruction_ip = state->arch.ip;
    boundary.instruction_address =
        (state->arch.cs.base + state->arch.ip) & ADDRESS_MASK;
    boundary.timing = BM_286_TIMING_UNKNOWN;
    if (state->hold_line && !state->lock_active) {
        if (!state->hold_acknowledged) {
            state->hold_acknowledged = 1U;
            if (state->config.hold_ack != NULL)
                state->config.hold_ack(state->config.pin_context, 1);
        }
        boundary.kind = BM_286_BOUNDARY_HOLD;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    decode = (decoded_286_t) {0};
    decode.state = state;
    decode.cursor = state->arch.ip;
    decode.override_segment = -1;
    if (!state->arch.shutdown && state->arch.trap_pending &&
        state->arch.interrupt_shadow != BM_286_SHADOW_SS_LOAD)
        event = 1U;
    else if (state->arch.nmi_pending && !state->arch.nmi_blocked &&
        state->arch.interrupt_shadow != BM_286_SHADOW_SS_LOAD)
        event = 2U;
    else if (!state->arch.shutdown && state->intr_line &&
        (state->arch.flags & FLAG_IF) && !state->arch.interrupt_shadow)
        event = 0U;
    if (event != 3U) {
        /* Suspend the REP window before handler entry. INTR establishes its
         * own window; IRET resumes at the prefix using committed CX/SI/DI.
         * Combined event/LOCK pin edges remain a functional policy, not
         * a measured silicon trace. */
        end_bus_lock(state);
        status = accept_interrupt(&decode, event, &boundary);
        if (status != BM_STATUS_OK) {
            state->stopped = 1U;
            end_bus_lock(state);
            return status;
        }
        state->rep_active = 0U;
        *out_boundary = boundary;
        if (state->config.trace != NULL)
            state->config.trace(state->config.trace_context, &boundary);
        return BM_STATUS_OK;
    }
    if (state->arch.shutdown || state->arch.halted) {
        boundary.kind = state->arch.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
            BM_286_BOUNDARY_HALT;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    trap_was_enabled = (uint8_t) ((state->arch.flags & FLAG_TF) != 0U);
    if (state->rep_active) {
        opcode = state->rep_opcode;
        repeat = state->rep_prefix;
        decode.override_segment = state->rep_segment;
        decode.lock_prefix = state->rep_lock;
        decode.cursor = state->rep_end_ip;
        status = BM_STATUS_OK;
    } else for (;;) {
        status = next_byte(&decode, &opcode);
        if (status != BM_STATUS_OK)
            break;
        if (opcode == 0x26U || opcode == 0x2eU ||
            opcode == 0x36U || opcode == 0x3eU) {
            decode.override_segment = (opcode >> 3) & 3U;
            continue;
        }
        if (opcode == 0xf2U || opcode == 0xf3U) {
            repeat = opcode; /* Last repeat prefix wins, within length bound. */
            continue;
        }
        if (opcode == 0xf0U) {
            if (state->config.bus_lock == NULL) {
                status = BM_STATUS_UNSUPPORTED;
                break;
            }
            decode.lock_prefix = 1U;
            continue;
        }
        break;
    }
    if (status == BM_STATUS_OK) {
        int locked_string = (opcode >= 0x6cU && opcode <= 0x6fU) ||
            opcode == 0xa4U || opcode == 0xa5U;
        if (decode.lock_prefix && ((repeat && !locked_string) ||
            !(locked_string || (opcode <= 0x31U && (opcode & 7U) <= 1U) ||
              opcode == 0x80U || opcode == 0x81U || opcode == 0x83U ||
              (opcode >= 0x88U && opcode <= 0x8cU) || opcode == 0x8eU ||
              (opcode >= 0xa0U && opcode <= 0xa3U) ||
              opcode == 0xc6U || opcode == 0xc7U ||
              opcode == 0xc0U || opcode == 0xc1U ||
              (opcode >= 0xd0U && opcode <= 0xd3U) ||
              opcode == 0x86U || opcode == 0x87U || opcode == 0xf6U ||
              opcode == 0xf7U || opcode == 0xfeU || opcode == 0xffU)))
            status = BM_STATUS_UNSUPPORTED; /* Other 286 LOCK forms remain pending. */
        else if (repeat) {
            if (!((opcode >= 0x6cU && opcode <= 0x6fU) ||
                  (opcode >= 0xa4U && opcode <= 0xa7U) ||
                  (opcode >= 0xaaU && opcode <= 0xafU)))
                status = BM_STATUS_UNSUPPORTED; /* No ignored REP/nonstring policy yet. */
            else if (state->arch.cx != 0U) {
                status = execute_string(&decode, opcode);
                if (status == BM_STATUS_OK) {
                    --state->arch.cx;
                    repeat_more = (uint8_t) (state->arch.cx != 0U);
                    if ((opcode & 0xfeU) == 0xa6U || (opcode & 0xfeU) == 0xaeU)
                        repeat_more &= (uint8_t) (((state->arch.flags & FLAG_ZF) != 0U) ==
                                                  (repeat == 0xf3U));
                }
            } /* CX=0: no segment checks or data accesses; FLAGS unchanged. */
        } else
            status = execute_data(&decode, opcode);
    }
    if (decode.operand_limit_fault) {
        /* Do not carry an instruction's explicit/implicit LOCK into its
         * fault frame. No operand access occurred at the rejected address. */
        end_bus_lock(state);
        decode.lock_prefix = 0U;
        status = deliver_fault(&decode, 13U);
    }
    if (status != BM_STATUS_OK) {
        state->stopped = 1U;
        end_bus_lock(state);
        return status;
    }
    state->rep_active = repeat_more;
    if (repeat_more) {
        state->rep_opcode = opcode;
        state->rep_prefix = repeat;
        state->rep_lock = decode.lock_prefix;
        state->rep_segment = decode.override_segment;
        state->rep_end_ip = (uint16_t) decode.cursor;
    } else
        state->arch.ip = (uint16_t) decode.cursor;
    state->arch.interrupt_shadow = decode.next_shadow;
    /* MOV/POP SS and taken software interrupts suppress their own sampled
     * trap (documented B-2/later INT behavior, not an early-stepping model).
     * The following instruction samples its incoming TF normally.
     * A previously deferred trap is kept,
     * not lost when TF is clear or when SS is loaded again. */
    state->arch.trap_pending = (uint8_t) (!decode.synchronous_fault &&
        (state->arch.trap_pending ||
        (trap_was_enabled && decode.next_shadow != BM_286_SHADOW_SS_LOAD &&
         !decode.software_interrupt)));
    /* A fault is not a completed instruction to single-step. Restoring TF
     * through IRET allows the retried instruction to be sampled normally.
     * Discarding a prior SS-deferred trap on a fault is an explicit functional
     * policy awaiting hardware coverage of that combined boundary case. */
    boundary.kind = state->arch.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
        decode.synchronous_fault ? BM_286_BOUNDARY_EXCEPTION :
        repeat_more ? BM_286_BOUNDARY_REP_ITERATION : BM_286_BOUNDARY_INSTRUCTION;
    boundary.has_vector = (uint8_t) (!state->arch.shutdown &&
        (decode.software_interrupt || decode.synchronous_fault));
    boundary.vector = state->arch.shutdown ? 0U : decode.software_vector;
    boundary.bus_wait_cycles = decode.waits;
    /* UNKNOWN timing uses a known wait lower bound, not an elapsed-clock
     * claim. The strict clocked entry point never schedules this boundary. */
    boundary.cpu_cycles = decode.waits;
    if (!repeat_more)
        end_bus_lock(state); /* REP keeps exclusion between diagnostic steps. */
    *out_boundary = boundary;
    if (state->config.trace != NULL)
        state->config.trace(state->config.trace_context, &boundary);
    return BM_STATUS_OK;
}

static bm_status_t cpu_run(void *context, bm_tick_t budget,
                           bm_tick_t *consumed)
{
    bm_286_private_t *state = context;
    bm_cpu_t cpu;
    bm_tick_t completed = 0U;
    bm_status_t status;
    if (state == NULL || consumed == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *consumed = 0U;
    cpu = (bm_cpu_t) {
        "intel-80286", state,
        { cpu_reset, cpu_run, cpu_signal, cpu_inspect, cpu_destroy }
    };
    while (completed < budget) {
        bm_286_boundary_t boundary;
        status = bm_286_step(&cpu, &boundary);
        if (status != BM_STATUS_OK) {
            *consumed = completed;
            return status;
        }
        ++completed;
    }
    *consumed = completed;
    return BM_STATUS_OK;
}

bm_status_t bm_286_step_clocked(void *context, bm_tick_t start_ns,
                                uint64_t *cycles)
{
    bm_286_private_t *state = context;
    (void) start_ns;
    if (state == NULL || cycles == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    *cycles = 0U;
    if (state->stopped)
        return BM_STATUS_INVALID_STATE;
    /* No exact prefetch + execution duration is certified in this tranche.
     * Latch before any fetch so retry cannot repeat guest-visible activity. */
    state->stopped = 1U;
    end_bus_lock(state);
    return BM_STATUS_UNSUPPORTED;
}
