/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 *
 * Instance-owned partial 80286 interpreter. Derived
 * rewrite references at BluMach 87c3fb4876eaad086921bc3444569da026286c36:
 * src/cpu/x86.c, src/cpu/386.c, src/cpu/386_ops.h, src/cpu/x86seg.c,
 * src/cpu/x86_ops_pmode.h, src/cpu/x86_ops_rep_286_2386.h,
 * src/cpu/x86_ops_mov.h, src/cpu/x86_ops_mov_seg.h,
 * src/cpu/x86_ops_arith.h, src/cpu/x86_ops_inc_dec.h,
 * src/cpu/x86_ops_misc.h, src/cpu/x86_flags.h, src/cpu/x86_ops_stack.h,
 * src/cpu/x86_ops_jump.h, src/cpu/x86_ops_call.h and
 * src/cpu/x86_ops_ret_2386.h. The inherited
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
    FLAG_CF = 0x0001U,
    FLAG_PF = 0x0004U,
    FLAG_AF = 0x0010U,
    FLAG_ZF = 0x0040U,
    FLAG_SF = 0x0080U,
    FLAG_TF = 0x0100U,
    FLAG_IF = 0x0200U,
    FLAG_OF = 0x0800U,
    FLAG_STATUS = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF |
                  FLAG_OF,
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
    if (!state->arch.cs.valid || ip > state->arch.cs.limit)
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

/* The Intel 286 instruction dictionary, not the later 386 prefix map, bounds
 * this private real-mode decoder. Failed decode never commits IP or a register. */
typedef struct decoded_286 {
    bm_286_private_t *state;
    uint32_t cursor;
    uint8_t length;
    uint64_t waits;
    int override_segment; /* -1 or ES/CS/SS/DS in architectural order. */
} decoded_286_t;

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
    if (decode->length >= 10U || decode->cursor > 0xffffU)
        return BM_STATUS_UNSUPPORTED; /* #6/#13 delivery is not available. */
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
    address = (segment->base + offset) & ADDRESS_MASK;
    transfer.space = BM_ADDRESS_DATA;
    transfer.operation = write ? BM_BUS_WRITE : BM_BUS_READ;
    transfer.endianness = BM_ENDIAN_LITTLE;
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

static bm_status_t read_operand(decoded_286_t *decode,
                                const operand_286_t *operand,
                                unsigned size, uint16_t *value)
{
    if (operand->memory)
        return data_access(decode, operand->segment, operand->offset,
                           size, 0, value);
    *value = size == 1U ? byte_register(&decode->state->arch,
                                         operand->reg_number) :
        *word_register(&decode->state->arch, operand->reg_number);
    return BM_STATUS_OK;
}

static bm_status_t write_operand(decoded_286_t *decode,
                                 const operand_286_t *operand,
                                 unsigned size, uint16_t value)
{
    if (operand->memory)
        return data_access(decode, operand->segment, operand->offset,
                           size, 1, &value);
    if (size == 1U)
        set_byte_register(&decode->state->arch, operand->reg_number,
                          (uint8_t) value);
    else
        *word_register(&decode->state->arch, operand->reg_number) = value;
    return BM_STATUS_OK;
}

/* Real-mode stack operations never use a segment override for the stack
 * itself. A limit/shutdown condition remains an explicit unsupported gap
 * until guest exception delivery exists. Completed bus writes are not undone. */
static bm_status_t push_word(decoded_286_t *decode, uint16_t value)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t sp = (uint16_t) (arch->sp - 2U);
    bm_status_t status = data_access(decode, &arch->ss, sp, 2U, 1, &value);
    if (status == BM_STATUS_OK)
        arch->sp = sp;
    return status;
}

static int stack_word_valid(const bm_286_arch_state_t *arch, uint16_t offset)
{
    return arch->ss.valid && (uint32_t) offset + 1U <= arch->ss.limit;
}

/* Multiword operations stage register changes, not external bus writes.
 * Preflight rejects unsupported exception/shutdown paths without claiming
 * silicon fault precedence or bus-cycle ordering on those paths. */
static bm_status_t aggregate_stack(decoded_286_t *decode, int pop)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    uint16_t values[8], offsets[8];
    unsigned i;
    bm_status_t status;
    for (i = 0U; i < 8U; ++i) {
        offsets[i] = (uint16_t) (pop ? arch->sp + 2U * i :
                                      arch->sp - 2U * (i + 1U));
        if (!stack_word_valid(arch, offsets[i]))
            return BM_STATUS_UNSUPPORTED;
        values[i] = *word_register(arch, i);
    }
    for (i = 0U; i < 8U; ++i) {
        if (pop && i == 3U)
            continue; /* Discard the saved SP slot; never load it into SP. */
        status = data_access(decode, &arch->ss, offsets[i], 2U, !pop, &values[i]);
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
    if (!arch->ss.valid || final_sp > arch->ss.limit)
        return BM_STATUS_UNSUPPORTED;
    for (i = 0U; i < words; ++i)
        if (!stack_word_valid(arch, (uint16_t) (sp - 2U * (i + 1U))))
            return BM_STATUS_UNSUPPORTED;
    for (i = 1U; i < nesting; ++i)
        if (!stack_word_valid(arch, (uint16_t) (bp - 2U * i)))
            return BM_STATUS_UNSUPPORTED;
    for (i = 0U; i < words; ++i) {
        if (i == 0U)
            value = arch->bp;
        else if (i == nesting)
            value = frame;
        else {
            bp = (uint16_t) (bp - 2U);
            status = data_access(decode, &arch->ss, bp, 2U, 0, &value);
            if (status != BM_STATUS_OK)
                return status;
        }
        sp = (uint16_t) (sp - 2U);
        status = data_access(decode, &arch->ss, sp, 2U, 1, &value);
        if (status != BM_STATUS_OK)
            return status;
    }
    arch->bp = frame;
    arch->sp = final_sp;
    return BM_STATUS_OK;
}

static int valid_near_target(const bm_286_arch_state_t *arch, uint16_t ip)
{
    return arch->cs.valid && ip <= arch->cs.limit;
}

static bm_status_t near_branch(decoded_286_t *decode, uint16_t target, int call)
{
    bm_status_t status;
    if (!valid_near_target(&decode->state->arch, target))
        return BM_STATUS_UNSUPPORTED; /* #13, not a successful branch. */
    if (call) {
        status = push_word(decode, (uint16_t) decode->cursor);
        if (status != BM_STATUS_OK)
            return status;
    }
    decode->cursor = target;
    return BM_STATUS_OK;
}

/* Real-mode CS reload drops the special reset base. A20 remains board-owned.
 * Only called after both pointer words were obtained successfully; protected
 * mode and guest exception delivery are rejected by the surrounding decoder. */
static bm_status_t far_jump(decoded_286_t *decode, uint16_t ip, uint16_t cs)
{
    bm_286_segment_state_t *segment = &decode->state->arch.cs;
    segment->selector = cs;
    segment->base = (uint32_t) cs << 4;
    segment->limit = 0xffffU;
    segment->access = 0U;
    segment->valid = 1U;
    decode->cursor = ip;
    return BM_STATUS_OK;
}

static bm_status_t execute_ff_control(decoded_286_t *decode,
                                      const operand_286_t *operand)
{
    uint16_t value, selector;
    bm_status_t status;
    if (operand->reg_field == 5U) {
        if (!operand->memory || !operand->segment->valid ||
            (uint32_t) operand->offset + 1U > operand->segment->limit ||
            (uint32_t) (uint16_t) (operand->offset + 2U) + 1U > operand->segment->limit)
            return BM_STATUS_UNSUPPORTED; /* Invalid form/#13 not yet delivered. */
        /* The two word offsets wrap independently. SST Harris captures
         * FF.5 cases 2914/3652/4550 read the selector at 0000 after FFFE. */
        status = read_operand(decode, operand, 2U, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = data_access(decode, operand->segment,
                             (uint16_t) (operand->offset + 2U), 2U, 0, &selector);
        return status == BM_STATUS_OK ? far_jump(decode, value, selector) : status;
    }
    if (operand->reg_field != 2U && operand->reg_field != 4U &&
        operand->reg_field != 6U)
        return BM_STATUS_UNSUPPORTED; /* Far call and /7 remain gaps. */
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
    bm_286_segment_state_t *segment;
    uint16_t value = 0U, extra = 0U, target, next_cx;
    uint8_t byte;
    int take;
    bm_status_t status;
    if (opcode == 0xeaU) {
        status = next_word(decode, &value);
        if (status != BM_STATUS_OK)
            return status;
        status = next_word(decode, &extra);
        return status == BM_STATUS_OK ? far_jump(decode, value, extra) : status;
    }
    if (opcode == 0x60U || opcode == 0x61U)
        return aggregate_stack(decode, opcode == 0x61U);
    if (opcode == 0xc8U)
        return enter_frame(decode);
    if (opcode == 0xc9U) {
        status = data_access(decode, &arch->ss, arch->bp, 2U, 0, &value);
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
        opcode == 0x07U || opcode == 0x1fU) {
        if (opcode == 0x8fU) {
            status = decode_operand(decode, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (operand.reg_field != 0U)
                return BM_STATUS_UNSUPPORTED;
        }
        status = data_access(decode, &arch->ss, arch->sp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        if (opcode == 0x8fU) {
            status = write_operand(decode, &operand, 2U, value);
            if (status != BM_STATUS_OK)
                return status;
            /* Mod=3 SP is loaded last, not incremented after replacement. */
            if (!operand.memory && operand.reg_number == 4U)
                return BM_STATUS_OK;
        } else if (opcode == 0x07U || opcode == 0x1fU) {
            segment = segment_register(arch, opcode >> 3);
            segment->selector = value;
            segment->base = (uint32_t) value << 4;
            segment->limit = 0xffffU;
            segment->access = 0U;
            segment->valid = 1U;
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
        status = data_access(decode, &arch->ss, arch->sp, 2U, 0, &value);
        if (status != BM_STATUS_OK)
            return status;
        if (!valid_near_target(arch, value))
            return BM_STATUS_UNSUPPORTED;
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
    return BM_STATUS_UNSUPPORTED; /* Flags, SS and far control pending. */
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

static bm_status_t execute_arithmetic(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand = {0};
    uint16_t destination, source = 0U, result, flags;
    uint8_t immediate;
    unsigned operation, form, size;
    bm_status_t status;
    if (opcode <= 0x3dU && (opcode & 7U) <= 5U) {
        operation = opcode >> 3;
        form = opcode & 7U;
        size = (form & 1U) ? 2U : 1U;
        if (form < 4U) {
            status = decode_operand(decode, &operand);
            if (status != BM_STATUS_OK)
                return status;
            if (form < 2U) {
                status = read_operand(decode, &operand, size, &destination);
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
                return BM_STATUS_UNSUPPORTED; /* /1 invalid; /4-7 deferred. */
        } else {
            if (operand.reg_field > 1U)
                return BM_STATUS_UNSUPPORTED; /* FE /2+ invalid; #6 pending. */
            operation = operand.reg_field == 0U ? 0U : 5U;
        }
        status = read_operand(decode, &operand, size, &destination);
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

static bm_status_t execute_data(decoded_286_t *decode, uint8_t opcode)
{
    bm_286_arch_state_t *arch = &decode->state->arch;
    operand_286_t operand;
    bm_286_segment_state_t *segment;
    uint16_t value, other, offset;
    uint8_t immediate;
    unsigned size;
    bm_status_t status;
    if (opcode == 0x90U)
        return BM_STATUS_OK;
    if (arch->msw & MSW_PE)
        return BM_STATUS_UNSUPPORTED; /* No real-mode semantics in PE mode. */
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
        status = data_access(decode, segment, offset, size,
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
        size = ((opcode & 1U) || opcode == 0x8cU || opcode == 0x8eU)
            ? 2U : 1U;
        if (opcode == 0x86U || opcode == 0x87U) {
            if (operand.memory)
                return BM_STATUS_UNSUPPORTED; /* Requires implicit LOCK. */
            status = read_operand(decode, &operand, size, &value);
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
            if (segment == NULL || operand.reg_field == 1U ||
                operand.reg_field == 2U)
                return BM_STATUS_UNSUPPORTED; /* CS invalid; SS shadow pending. */
            status = read_operand(decode, &operand, 2U, &value);
            if (status != BM_STATUS_OK)
                return status;
            segment->selector = value;
            segment->base = (uint32_t) value << 4;
            segment->limit = 0xffffU;
            segment->access = 0U;
            segment->valid = 1U;
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

bm_status_t bm_286_step(bm_cpu_t *cpu, bm_286_boundary_t *out_boundary)
{
    bm_286_private_t *state;
    bm_286_boundary_t boundary = {0};
    decoded_286_t decode;
    bm_status_t status;
    uint8_t opcode;
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
    if (state->arch.msw & MSW_PE) {
        state->stopped = 1U;
        return BM_STATUS_UNSUPPORTED; /* No protected fetch/privilege model. */
    }
    decode.state = state;
    decode.cursor = state->arch.ip;
    decode.length = 0U;
    decode.waits = 0U;
    decode.override_segment = -1;
    for (;;) {
        status = next_byte(&decode, &opcode);
        if (status != BM_STATUS_OK)
            break;
        if (opcode == 0x26U || opcode == 0x2eU ||
            opcode == 0x36U || opcode == 0x3eU) {
            decode.override_segment = (opcode >> 3) & 3U;
            continue;
        }
        /* LOCK and repeat require their own instruction/atomicity rules.
         * Stop before any guest data transaction, even for valid opcodes. */
        if (opcode == 0xf0U || opcode == 0xf2U || opcode == 0xf3U)
            status = BM_STATUS_UNSUPPORTED;
        else
            status = execute_data(&decode, opcode);
        break;
    }
    if (status != BM_STATUS_OK) {
        state->stopped = 1U;
        return status;
    }
    trap_was_enabled = (uint8_t) (((state->arch.flags & FLAG_TF) != 0U) &&
                                  !state->arch.interrupt_shadow);
    state->arch.ip = (uint16_t) decode.cursor;
    state->arch.interrupt_shadow = 0U;
    state->arch.trap_pending = trap_was_enabled;
    boundary.kind = BM_286_BOUNDARY_INSTRUCTION;
    boundary.bus_wait_cycles = decode.waits;
    /* UNKNOWN timing uses a known wait lower bound, not an elapsed-clock
     * claim. The strict clocked entry point never schedules this boundary. */
    boundary.cpu_cycles = decode.waits;
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
