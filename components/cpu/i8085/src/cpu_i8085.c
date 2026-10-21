/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/cpu_i8085.h>

#include <string.h>

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

    uint8_t a;
    uint8_t f;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
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
    (void) budget;
    if ((context == NULL) || (consumed == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *consumed = 0U;
    return BM_STATUS_UNSUPPORTED;
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
    const bm_i8085_state_t *state = context;

    if ((state == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "a") == 0)
        *value = state->a;
    else if (strcmp(name, "f") == 0)
        *value = state->f;
    else if (strcmp(name, "b") == 0)
        *value = state->b;
    else if (strcmp(name, "c") == 0)
        *value = state->c;
    else if (strcmp(name, "d") == 0)
        *value = state->d;
    else if (strcmp(name, "e") == 0)
        *value = state->e;
    else if (strcmp(name, "h") == 0)
        *value = state->h;
    else if (strcmp(name, "l") == 0)
        *value = state->l;
    else if (strcmp(name, "bc") == 0)
        *value = ((unsigned int) state->b << 8U) | state->c;
    else if (strcmp(name, "de") == 0)
        *value = ((unsigned int) state->d << 8U) | state->e;
    else if (strcmp(name, "hl") == 0)
        *value = ((unsigned int) state->h << 8U) | state->l;
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
    out_state->a = state->a;
    out_state->f = state->f;
    out_state->b = state->b;
    out_state->c = state->c;
    out_state->d = state->d;
    out_state->e = state->e;
    out_state->h = state->h;
    out_state->l = state->l;
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
    state->a = image->a;
    state->f = image->f;
    state->b = image->b;
    state->c = image->c;
    state->d = image->d;
    state->e = image->e;
    state->h = image->h;
    state->l = image->l;
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
    (void) start_ns;
    if ((context == NULL) || (cycles == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *cycles = 0U;
    return BM_STATUS_UNSUPPORTED;
}
