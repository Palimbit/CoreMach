/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 *
 * Instance-owned 80286 lifecycle and initial execution boundary. Derived
 * rewrite references at BluMach 87c3fb4876eaad086921bc3444569da026286c36:
 * src/cpu/x86.c, src/cpu/386.c, src/cpu/386_ops.h, src/cpu/x86seg.c,
 * src/cpu/x86_ops_pmode.h and src/cpu/x86_ops_rep_286_2386.h. The inherited
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

#include <string.h>

enum {
    FLAG_FIXED_ONE = 0x0002U,
    FLAG_TF = 0x0100U,
    FLAG_IF = 0x0200U,
    MSW_PE = 0x0001U,
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
} bm_286_private_t;

static bm_status_t cpu_run(void *context, bm_tick_t budget,
                           bm_tick_t *consumed);

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
    state->arch.ds.limit = 0xffffU;
    state->arch.ds.valid = 1U;
    state->arch.es.limit = 0xffffU;
    state->arch.es.valid = 1U;
    state->arch.ss.limit = 0xffffU;
    state->arch.ss.valid = 1U;
    state->arch.idtr.limit = 0x03ffU;
    state->intr_line = 0U;
    state->nmi_line = 0U;
    state->hold_line = 0U;
    state->hold_acknowledged = 0U;
    state->stopped = 0U;
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
    if (state != NULL)
        state->host.release(state->host.context, state);
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
        arch->interrupt_shadow <= 1U && arch->nmi_blocked <= 1U &&
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
    /* No prefetch or REP continuation exists in the first functional tranche. */
    return BM_STATUS_OK;
}

static bm_status_t fetch_byte(bm_286_private_t *state, uint16_t ip,
                              uint8_t *out_value, uint32_t *out_waits)
{
    bm_bus_transaction_t transaction = {0};
    bm_status_t status;
    if (ip > state->arch.cs.limit)
        return BM_STATUS_UNSUPPORTED; /* #13 delivery is not implemented yet. */
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

bm_status_t bm_286_step(bm_cpu_t *cpu, bm_286_boundary_t *out_boundary)
{
    bm_286_private_t *state;
    bm_286_boundary_t boundary = {0};
    bm_status_t status;
    uint8_t opcode;
    uint32_t waits;
    uint8_t trap_was_enabled;
    if (!is_286_cpu(cpu) || out_boundary == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    memset(out_boundary, 0, sizeof(*out_boundary));
    state = cpu->context;
    if (state->stopped)
        return BM_STATUS_INVALID_STATE;
    boundary.instruction_ip = state->arch.ip;
    boundary.instruction_address =
        (state->arch.cs.base + state->arch.ip) & ADDRESS_MASK;
    boundary.timing = BM_286_TIMING_UNKNOWN;
    if (state->hold_line) {
        if (!state->hold_acknowledged) {
            state->hold_acknowledged = 1U;
            if (state->config.hold_ack != NULL)
                state->config.hold_ack(state->config.pin_context, 1);
        }
        boundary.kind = BM_286_BOUNDARY_HOLD;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    if (!state->arch.shutdown && state->arch.trap_pending &&
        !state->arch.interrupt_shadow) {
        state->stopped = 1U;
        return BM_STATUS_UNSUPPORTED; /* #1 delivery is not implemented. */
    }
    if (state->arch.nmi_pending && !state->arch.nmi_blocked &&
        !state->arch.interrupt_shadow) {
        state->stopped = 1U;
        return BM_STATUS_UNSUPPORTED; /* NMI delivery/recovery is missing. */
    }
    if (!state->arch.shutdown && state->intr_line &&
        (state->arch.flags & FLAG_IF) && !state->arch.interrupt_shadow) {
        state->stopped = 1U;
        return BM_STATUS_UNSUPPORTED; /* INTR delivery is missing. */
    }
    if (state->arch.shutdown || state->arch.halted) {
        boundary.kind = state->arch.shutdown ? BM_286_BOUNDARY_SHUTDOWN :
            BM_286_BOUNDARY_HALT;
        *out_boundary = boundary;
        return BM_STATUS_IDLE;
    }
    status = fetch_byte(state, state->arch.ip, &opcode, &waits);
    if (status != BM_STATUS_OK) {
        state->stopped = 1U;
        return status;
    }
    if (opcode != 0x90U) {
        state->stopped = 1U;
        return BM_STATUS_UNSUPPORTED;
    }
    trap_was_enabled = (uint8_t) (((state->arch.flags & FLAG_TF) != 0U) &&
                                  !state->arch.interrupt_shadow);
    state->arch.ip = (uint16_t) (state->arch.ip + 1U);
    state->arch.interrupt_shadow = 0U;
    state->arch.trap_pending = trap_was_enabled;
    boundary.kind = BM_286_BOUNDARY_INSTRUCTION;
    boundary.bus_wait_cycles = waits;
    /* UNKNOWN timing uses a known wait lower bound, not an elapsed-clock
     * claim. The strict clocked entry point never schedules this boundary. */
    boundary.cpu_cycles = waits;
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
    return BM_STATUS_UNSUPPORTED;
}
