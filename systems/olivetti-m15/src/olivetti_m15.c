/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2025 Sarah Walker
 * Copyright 2016-2025 Miran Grca
 * Copyright 2017-2025 Fred N. van Kempen
 * Copyright 2020 EngiNerd
 * Copyright 2025 Jasmine Iwanek
 * Copyright 2026 rtzor
 * Copyright 2026 BluMach contributors
 *
 * Selective, instance-owned rewrite of the BluMach M15 board-switch path.
 * The device implementations are linked as separate portable components.
 */
#include <blumach/systems/olivetti_m15.h>

#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/dma_page_registers.h>
#include <blumach/components/fdc765.h>
#include <blumach/components/linear_memory.h>
#include <blumach/components/pic8259.h>
#include <blumach/components/pit8253.h>
#include <blumach/components/pit8253_clock.h>
#include <blumach/components/rtc_msm6242.h>
#include <blumach/components/v6355d.h>

#include <string.h>

#define M15_ROM_BASE UINT64_C(0x000f0000)
#define M15_BIOS_FONT_OFFSET 0xfa6eU
#define M15_LCD_PIXELS (640U * 204U)
#define M15_KEY_QUEUE_SIZE 16U

typedef struct bm_m15_machine {
    bm_host_services_t host;
    bm_engine_t *engine;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_v6355d_t *video;
    uint8_t *video_indices;
    bm_linear_memory_t *rom;
    bm_dma8237_t *dma;
    bm_dma_page_registers_t *dma_pages;
    bm_pic8259_t *pic;
    bm_pit8253_t *pit;
    bm_msm6242_t *rtc;
    bm_floppy_drive_t *floppy[2];
    bm_fdc765_t *fdc;
    bm_cpu_id_t cpu_id;
    uint32_t ram_kib;
    uint8_t startup_display_switches;
    uint8_t port_b;
    uint8_t keyboard_response;
    int keyboard_response_pending;
    uint8_t key_queue[M15_KEY_QUEUE_SIZE];
    uint8_t key_queue_head;
    uint8_t key_queue_tail;
    uint8_t key_latch;
    int key_latch_full;
    bm_timed_source_id_t keyboard_timer_id;
    int keyboard_timer_armed;
    int cpu_ready;
} bm_m15_machine_t;

static bm_status_t
m15_validate(const bm_configuration_view_t *view)
{
    const bm_m15_config_t *config;
    size_t index;

    if ((view == NULL) || (view->type == NULL) ||
        (strcmp(view->type, BM_M15_CONFIG_TYPE) != 0) ||
        (view->version != BM_M15_CONFIG_VERSION) ||
        (view->size != sizeof(bm_m15_config_t)) || (view->data == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    config = view->data;
    if ((config->firmware.data == NULL) ||
        (config->firmware.size != BM_M15_FIRMWARE_SIZE) ||
        ((config->ram_kib != 256U) && (config->ram_kib != 512U)) ||
        ((config->startup_display_switches != 0x10U) &&
         (config->startup_display_switches != 0x20U)) ||
        ((config->rtc_initial_state == NULL) &&
         (config->rtc_initial_state_size != 0U)) ||
        ((config->rtc_initial_state != NULL) &&
         (config->rtc_initial_state_size != BM_MSM6242_STATE_SIZE)))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < 2U; ++index) {
        const bm_floppy_drive_config_t *floppy = &config->floppy[index];

        if ((bm_floppy_drive_config_validate(floppy) != BM_STATUS_OK) ||
            !floppy->installed || (floppy->geometry.cylinders != 80U) ||
            (floppy->geometry.heads != 2U) ||
            (floppy->geometry.sectors_per_track != 9U) ||
            (floppy->geometry.bytes_per_sector != 512U))
            return BM_STATUS_INVALID_ARGUMENT;
    }
    return BM_STATUS_OK;
}

static void
m15_pic_output(void *context, int asserted)
{
    bm_m15_machine_t *machine = context;

    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id, 0U,
                                    asserted);
}

static void
m15_pit_output(void *context, unsigned int channel, int asserted)
{
    bm_m15_machine_t *machine = context;

    if ((channel == 0U) && (machine->pic != NULL))
        (void) bm_pic8259_set_irq(machine->pic, 0U, asserted);
}

static void
m15_fdc_irq(void *context, int asserted)
{
    bm_m15_machine_t *machine = context;

    if (machine->pic != NULL)
        (void) bm_pic8259_set_irq(machine->pic, 6U, asserted);
}

static bm_status_t
m15_interrupt_acknowledge(void *context, uint8_t *vector)
{
    bm_m15_machine_t *machine = context;

    return bm_pic8259_acknowledge(machine->pic, vector);
}

static bm_status_t
m15_rtc_second(bm_engine_t *engine, void *context,
               const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    bm_m15_machine_t *machine = context;

    (void) engine;
    (void) when;
    *cycles_until_next = 1U;
    return bm_msm6242_advance_second(machine->rtc);
}

static bm_tick_t
m15_video_time(void *context)
{
    const bm_m15_machine_t *machine = context;

    return bm_engine_now(machine->engine);
}

static bm_status_t
m15_key_enqueue(bm_m15_machine_t *machine, uint8_t value)
{
    uint8_t next = (uint8_t) ((machine->key_queue_tail + 1U) &
                              (M15_KEY_QUEUE_SIZE - 1U));

    if (next == machine->key_queue_head)
        return BM_STATUS_CAPACITY_EXCEEDED;
    machine->key_queue[machine->key_queue_tail] = value;
    machine->key_queue_tail = next;
    return BM_STATUS_OK;
}

static bm_status_t
m15_keyboard_schedule(bm_m15_machine_t *machine)
{
    bm_status_t status;

    if (machine->keyboard_timer_armed || machine->key_latch_full ||
        (machine->port_b & 0x40U) == 0U ||
        machine->key_queue_head == machine->key_queue_tail)
        return BM_STATUS_OK;
    status = bm_engine_arm_timed_source(machine->engine,
                                        machine->keyboard_timer_id, 1U);
    if (status == BM_STATUS_OK)
        machine->keyboard_timer_armed = 1;
    return status;
}

static bm_status_t
m15_keyboard_clock(bm_engine_t *engine, void *context,
                   const bm_time_point_t *when, uint64_t *cycles_until_next)
{
    bm_m15_machine_t *machine = context;

    (void) engine;
    (void) when;
    *cycles_until_next = 0U;
    machine->keyboard_timer_armed = 0;
    if ((machine->port_b & 0x40U) == 0U || machine->key_latch_full ||
        machine->key_queue_head == machine->key_queue_tail)
        return BM_STATUS_IDLE;
    machine->key_latch = machine->key_queue[machine->key_queue_head];
    machine->key_queue_head = (uint8_t) ((machine->key_queue_head + 1U) &
                                         (M15_KEY_QUEUE_SIZE - 1U));
    machine->key_latch_full = 1;
    if (bm_pic8259_set_irq(machine->pic, 1U, 1) != BM_STATUS_OK)
        return BM_STATUS_DEVICE_ERROR;
    return BM_STATUS_IDLE;
}

/* XT-compatible guest byte stream used by the inherited pilot. The M15 has
 * its own detachable keyboard; this is not a claim about its wire protocol or
 * about nationally-specific and extended key positions. */
static bm_status_t
m15_key_to_guest_byte(bm_key_code_t key, uint8_t *scan)
{
    static const uint8_t letter_scan[26] = {
        0x1eU, 0x30U, 0x2eU, 0x20U, 0x12U, 0x21U, 0x22U,
        0x23U, 0x17U, 0x24U, 0x25U, 0x26U, 0x32U, 0x31U,
        0x18U, 0x19U, 0x10U, 0x13U, 0x1fU, 0x14U, 0x16U,
        0x2fU, 0x11U, 0x2dU, 0x15U, 0x2cU
    };

    if ((key >= BM_KEY_A) && (key <= BM_KEY_Z)) {
        *scan = letter_scan[key - BM_KEY_A];
        return BM_STATUS_OK;
    }
    if ((key >= BM_KEY_1) && (key <= BM_KEY_9)) {
        *scan = (uint8_t) (2U + key - BM_KEY_1);
        return BM_STATUS_OK;
    }
    if ((key >= BM_KEY_F1) && (key <= BM_KEY_F10)) {
        *scan = (uint8_t) (0x3bU + key - BM_KEY_F1);
        return BM_STATUS_OK;
    }
    switch (key) {
        case BM_KEY_0: *scan = 0x0bU; break;
        case BM_KEY_ENTER: *scan = 0x1cU; break;
        case BM_KEY_ESCAPE: *scan = 0x01U; break;
        case BM_KEY_BACKSPACE: *scan = 0x0eU; break;
        case BM_KEY_TAB: *scan = 0x0fU; break;
        case BM_KEY_SPACE: *scan = 0x39U; break;
        case BM_KEY_MINUS: *scan = 0x0cU; break;
        case BM_KEY_EQUAL: *scan = 0x0dU; break;
        case BM_KEY_LEFT_BRACKET: *scan = 0x1aU; break;
        case BM_KEY_RIGHT_BRACKET: *scan = 0x1bU; break;
        case BM_KEY_BACKSLASH: *scan = 0x2bU; break;
        case BM_KEY_SEMICOLON: *scan = 0x27U; break;
        case BM_KEY_APOSTROPHE: *scan = 0x28U; break;
        case BM_KEY_GRAVE: *scan = 0x29U; break;
        case BM_KEY_COMMA: *scan = 0x33U; break;
        case BM_KEY_PERIOD: *scan = 0x34U; break;
        case BM_KEY_SLASH: *scan = 0x35U; break;
        case BM_KEY_CAPS_LOCK: *scan = 0x3aU; break;
        case BM_KEY_SCROLL_LOCK: *scan = 0x46U; break;
        case BM_KEY_LEFT_CONTROL: *scan = 0x1dU; break;
        case BM_KEY_LEFT_SHIFT: *scan = 0x2aU; break;
        case BM_KEY_RIGHT_SHIFT: *scan = 0x36U; break;
        case BM_KEY_LEFT_ALT: *scan = 0x38U; break;
        default: return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static bm_status_t
m15_input(void *context, const bm_input_event_t *event)
{
    bm_m15_machine_t *machine = context;
    uint8_t scan;
    bm_status_t status;

    if ((machine == NULL) || (event == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (event->kind != BM_INPUT_KEY)
        return BM_STATUS_UNSUPPORTED;
    status = m15_key_to_guest_byte(event->key, &scan);
    if (status != BM_STATUS_OK)
        return status;
    status = m15_key_enqueue(machine, event->pressed ? scan :
                             (uint8_t) (scan | 0x80U));
    return status == BM_STATUS_OK ? m15_keyboard_schedule(machine) : status;
}

static uint8_t
m15_memory_switch_x(const bm_m15_machine_t *machine)
{
    return (uint8_t) (((machine->ram_kib / 16U) - 1U) >> 1U);
}

static uint8_t
m15_board_switches(const bm_m15_machine_t *machine)
{
    uint8_t memory_y = (uint8_t) (((machine->ram_kib / 16U) - 1U) & 1U);

    return (uint8_t) (machine->startup_display_switches |
                      (uint8_t) (memory_y << 2U));
}

static bm_status_t
m15_board_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_m15_machine_t *machine = context;
    uint16_t port;

    if ((transaction == NULL) || (transaction->size != 1U))
        return BM_STATUS_INVALID_ARGUMENT;
    port = (uint16_t) transaction->address;
    if (transaction->operation == BM_BUS_WRITE) {
        uint8_t value = (uint8_t) transaction->value;

        if (port == 0x60U) {
            if (value == 0x05U) {
                machine->keyboard_response = 0x82U;
                machine->keyboard_response_pending = 1;
            }
            return BM_STATUS_OK;
        }
        if (port == 0x61U) {
            uint8_t previous = machine->port_b;
            bm_status_t status;

            if ((previous & 0x40U) == 0U && (value & 0x40U) != 0U) {
                /* Inherited XT-compatible keyboard reset handshake. */
                machine->key_queue_head = machine->key_queue_tail = 0U;
                machine->key_latch_full = 0;
                (void) bm_pic8259_set_irq(machine->pic, 1U, 0);
                (void) m15_key_enqueue(machine, 0xaaU);
            }
            if ((value & 0x80U) != 0U) {
                machine->key_latch_full = 0;
                (void) bm_pic8259_set_irq(machine->pic, 1U, 0);
            }
            machine->port_b = value;
            status = bm_pit8253_set_gate(machine->pit, 2U,
                                         (value & 1U) != 0U);
            return status == BM_STATUS_OK ?
                m15_keyboard_schedule(machine) : status;
        }
        return BM_STATUS_OK;
    }
    if (transaction->operation != BM_BUS_READ)
        return BM_STATUS_INVALID_ARGUMENT;
    switch (port) {
        case 0x60U:
            if (machine->keyboard_response_pending) {
                transaction->value = machine->keyboard_response;
                if ((transaction->attributes & BM_BUS_TRANSACTION_DEBUG) == 0U)
                    machine->keyboard_response_pending = 0;
            } else if ((machine->port_b & 0x80U) != 0U) {
                transaction->value = m15_board_switches(machine);
            } else {
                transaction->value = machine->key_latch;
            }
            return BM_STATUS_OK;
        case 0x61U:
            transaction->value = machine->port_b;
            return BM_STATUS_OK;
        case 0x62U:
            transaction->value = (machine->port_b & 0x04U) != 0U ?
                (m15_memory_switch_x(machine) & 0x0fU) :
                (m15_memory_switch_x(machine) >> 4U);
            return BM_STATUS_OK;
        case 0x64U:
            transaction->value = (machine->keyboard_response_pending ||
                                  machine->key_latch_full) ? 1U : 0U;
            return BM_STATUS_OK;
        case 0x63U:
            transaction->value = 0xffU; /* No M15 board register is modeled. */
            return BM_STATUS_OK;
        default:
            return BM_STATUS_UNMAPPED;
    }
}

static void
m15_destroy(void *context)
{
    bm_m15_machine_t *machine = context;
    size_t index;

    if (machine == NULL)
        return;
    bm_fdc765_destroy(machine->fdc);
    for (index = 0U; index < 2U; ++index)
        bm_floppy_drive_destroy(machine->floppy[index]);
    bm_pit8253_destroy(machine->pit);
    bm_msm6242_destroy(machine->rtc);
    bm_pic8259_destroy(machine->pic);
    bm_dma_page_registers_destroy(machine->dma_pages);
    bm_dma8237_destroy(machine->dma);
    bm_linear_memory_destroy(machine->rom);
    bm_v6355d_destroy(machine->video);
    if (machine->video_indices != NULL)
        machine->host.release(machine->host.context, machine->video_indices);
    bm_linear_memory_destroy(machine->ram);
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
}

static bm_status_t
m15_create(bm_engine_t *engine, const bm_host_services_t *host,
           const bm_configuration_view_t *view, void **out_machine)
{
    static const bm_bus_static_response_t open_bus = {
        BM_STATUS_OK, BM_STATUS_OK, BM_STATUS_OK, 0xffU
    };
    static const bm_clock_rate_t cpu_rate = { UINT64_C(14318180), 3U };
    static const bm_clock_rate_t pit_rate = { UINT64_C(14318180), 9U };
    static const bm_clock_rate_t rtc_rate = { 1U, 1U };
    static const bm_clock_rate_t keyboard_rate = { 1000U, 1U };
    const bm_m15_config_t *config;
    bm_m15_machine_t *machine;
    bm_status_t status;
    bm_cpu_t cpu;
    size_t index;

    if ((engine == NULL) || (host == NULL) || (out_machine == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_machine = NULL;
    status = m15_validate(view);
    if (status != BM_STATUS_OK)
        return status;
    config = view->data;
    machine = host->allocate(host->context, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(machine, 0, sizeof(*machine));
    machine->host = *host;
    machine->engine = engine;
    machine->ram_kib = config->ram_kib;
    machine->startup_display_switches = config->startup_display_switches;

    status = bm_bus_create(host, 28U, &machine->bus);
    if (status == BM_STATUS_OK)
        status = bm_bus_set_default_response(machine->bus, BM_ADDRESS_IO,
                                             &open_bus);
    if (status == BM_STATUS_OK)
        status = bm_bus_set_default_response(machine->bus, BM_ADDRESS_MEMORY,
                                             &open_bus);
    if (status == BM_STATUS_OK) {
        const bm_linear_memory_config_t memory = {
            BM_ADDRESS_MEMORY, 0U, (size_t) config->ram_kib * 1024U,
            BM_LINEAR_MEMORY_WRITABLE, NULL, 0U
        };
        status = bm_linear_memory_create(host, machine->bus, &memory,
                                         &machine->ram);
    }
    if (status == BM_STATUS_OK) {
        uint8_t font[BM_V6355D_FONT_SIZE] = { 0 };
        bm_v6355d_config_t video_config = {
            font, sizeof(font), m15_video_time, machine
        };

        /* The inspected 1.08 M15 ROM has 128 eight-byte glyphs at FA6Eh.
         * Upper glyphs remain explicitly unresolved, not borrowed from IBM. */
        memcpy(font, config->firmware.data + M15_BIOS_FONT_OFFSET, 1024U);
        status = bm_v6355d_create(host, machine->bus, &video_config,
                                  &machine->video);
    }
    if (status == BM_STATUS_OK) {
        machine->video_indices = host->allocate(host->context, M15_LCD_PIXELS);
        if (machine->video_indices == NULL)
            status = BM_STATUS_OUT_OF_MEMORY;
    }
    if (status == BM_STATUS_OK) {
        const bm_linear_memory_config_t memory = {
            BM_ADDRESS_MEMORY, M15_ROM_BASE, BM_M15_FIRMWARE_SIZE,
            BM_LINEAR_MEMORY_WRITE_IGNORE, config->firmware.data,
            config->firmware.size
        };
        status = bm_linear_memory_create(host, machine->bus, &memory,
                                         &machine->rom);
    }
    if (status == BM_STATUS_OK) {
        bm_dma8237_config_t dma_config = { 0U };
        status = bm_dma8237_create(host, machine->bus, &dma_config,
                                   &machine->dma);
    }
    if (status == BM_STATUS_OK) {
        bm_dma_page_registers_config_t page_config = {
            .io_base = 0x0080U, .page_mask = 0x0fU,
            .dma = machine->dma, .register_count = 16U
        };
        status = bm_dma_page_registers_create(host, machine->bus,
                                               &page_config,
                                               &machine->dma_pages);
    }
    if (status == BM_STATUS_OK) {
        bm_pic8259_config_t pic_config = {
            0x0020U, m15_pic_output, machine
        };
        status = bm_pic8259_create(host, machine->bus, &pic_config,
                                   &machine->pic);
    }
    if (status == BM_STATUS_OK) {
        bm_pit8253_config_t pit_config = {
            0x0040U, m15_pit_output, machine
        };
        status = bm_pit8253_create(host, machine->bus, &pit_config,
                                   &machine->pit);
    }
    if (status == BM_STATUS_OK) {
        bm_msm6242_config_t rtc_config = {
            .io_base = 0x0100U,
            .initial_state = config->rtc_initial_state,
            .initial_state_size = config->rtc_initial_state_size
        };
        status = bm_msm6242_create(host, machine->bus, &rtc_config,
                                   &machine->rtc);
    }
    for (index = 0U; (status == BM_STATUS_OK) && (index < 2U); ++index)
        status = bm_floppy_drive_create(host, &config->floppy[index],
                                        &machine->floppy[index]);
    if (status == BM_STATUS_OK) {
        bm_fdc765_config_t fdc_config = {
            .io_base = 0x03f0U, .dma_channel = 2U,
            .dma = machine->dma, .drives = {
                machine->floppy[0], machine->floppy[1], NULL, NULL
            },
            .irq = m15_fdc_irq, .irq_context = machine
        };
        status = bm_fdc765_create(host, machine->bus, &fdc_config,
                                  &machine->fdc);
    }
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0060U, 0x0064U,
                            m15_board_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_pit8253_attach_clock(engine, machine->pit, &pit_rate,
                                         NULL);
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, m15_rtc_second, machine,
                                             &rtc_rate, 1U, NULL);
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, m15_keyboard_clock,
                                             machine, &keyboard_rate, 0U,
                                             &machine->keyboard_timer_id);
    if (status == BM_STATUS_OK) {
        bm_808x_config_t cpu_config = {
            .model = BM_808X_INTEL_8088,
            .frequency_hz = 4772727U,
            .bus = machine->bus,
            .trace = config->trace,
            .trace_context = config->trace_context,
            .timing = config->timing,
            .timing_context = config->timing_context,
            .interrupt_ack = m15_interrupt_acknowledge,
            .interrupt_context = machine
        };
        status = bm_808x_create(host, &cpu_config, &cpu);
    }
    if (status == BM_STATUS_OK) {
        status = bm_engine_add_clocked_cpu(
            engine, &cpu, bm_808x_step_clocked_provisional, &cpu_rate,
            &machine->cpu_id);
        if (status != BM_STATUS_OK)
            cpu.ops.destroy(cpu.context);
        else
            machine->cpu_ready = 1;
    }
    if (status != BM_STATUS_OK) {
        m15_destroy(machine);
        return status;
    }
    *out_machine = machine;
    return BM_STATUS_OK;
}

static bm_status_t
m15_reset(void *context)
{
    bm_m15_machine_t *machine = context;

    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    bm_dma8237_reset(machine->dma);
    bm_dma_page_registers_reset(machine->dma_pages);
    bm_pic8259_reset(machine->pic);
    bm_pit8253_reset(machine->pit);
    bm_v6355d_reset(machine->video);
    bm_fdc765_reset(machine->fdc);
    bm_floppy_drive_reset(machine->floppy[0]);
    bm_floppy_drive_reset(machine->floppy[1]);
    machine->port_b = 0U;
    machine->keyboard_response = 0U;
    machine->keyboard_response_pending = 0;
    machine->key_queue_head = machine->key_queue_tail = 0U;
    machine->key_latch = 0U;
    machine->key_latch_full = 0;
    machine->keyboard_timer_armed = 0;
    return BM_STATUS_OK;
}

static bm_status_t
m15_inspect(const void *context, const char *name, uint64_t *value)
{
    const bm_m15_machine_t *machine = context;

    if ((machine == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "ram_kib") == 0)
        *value = machine->ram_kib;
    else if (strcmp(name, "port_b") == 0)
        *value = machine->port_b;
    else if (strcmp(name, "keyboard_response_pending") == 0)
        *value = (uint64_t) machine->keyboard_response_pending;
    else if (strcmp(name, "keyboard_latch") == 0)
        *value = machine->key_latch;
    else if (strcmp(name, "keyboard_latch_full") == 0)
        *value = (uint64_t) machine->key_latch_full;
    else if (strcmp(name, "keyboard_timer_armed") == 0)
        *value = (uint64_t) machine->keyboard_timer_armed;
    else if (strcmp(name, "keyboard_queue_depth") == 0)
        *value = (uint8_t) ((machine->key_queue_tail - machine->key_queue_head) &
                           (M15_KEY_QUEUE_SIZE - 1U));
    else if (strcmp(name, "pic_irq_requests") == 0) {
        bm_pic8259_state_t state;
        if (bm_pic8259_state(machine->pic, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = state.interrupt_requests;
    }
    else if (strcmp(name, "rtc_seconds") == 0) {
        uint8_t state[BM_MSM6242_STATE_SIZE];
        if (bm_msm6242_save_state(machine->rtc, state, sizeof(state)) !=
            BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = state[0] + 10U * state[1];
    }
    else
        return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t
m15_video_geometry(const void *context, bm_video_geometry_t *geometry)
{
    const bm_m15_machine_t *machine = context;

    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_v6355d_geometry(machine->video, geometry);
}

static bm_status_t
m15_video_render(const void *context, bm_tick_t emulated_time,
                 bm_video_framebuffer_t *framebuffer)
{
    static const uint8_t green[4] = { 0x00U, 0x19U, 0x43U, 0x78U };
    bm_m15_machine_t *machine = (bm_m15_machine_t *) context;
    bm_video_geometry_t geometry;
    bm_status_t status;
    uint32_t x, y;

    if ((machine == NULL) || (framebuffer == NULL) ||
        (framebuffer->pixels == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_v6355d_geometry(machine->video, &geometry);
    if (status != BM_STATUS_OK)
        return status;
    if ((framebuffer->stride < geometry.width) ||
        (framebuffer->pixel_capacity / framebuffer->stride < geometry.height))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_v6355d_render_indices(machine->video, emulated_time,
                                      machine->video_indices, M15_LCD_PIXELS,
                                      geometry.width);
    if (status != BM_STATUS_OK)
        return status;
    for (y = 0U; y < geometry.height; ++y) {
        for (x = 0U; x < geometry.width; ++x) {
            uint8_t index = machine->video_indices[(size_t) y * geometry.width + x];
            unsigned int luminance = ((index & 4U) != 0U ? 3U : 0U) +
                                     ((index & 2U) != 0U ? 6U : 0U) +
                                     ((index & 1U) != 0U ? 1U : 0U) +
                                     ((index & 8U) != 0U ? 3U : 0U);
            unsigned int level = (luminance * 3U + 6U) / 13U;

            framebuffer->pixels[(size_t) y * framebuffer->stride + x] =
                UINT32_C(0xff000000) | ((uint32_t) green[level] << 8U);
        }
    }
    framebuffer->geometry = geometry;
    return BM_STATUS_OK;
}

static size_t
m15_storage_count(const void *context)
{
    return context != NULL ? 2U : 0U;
}

static bm_status_t
m15_storage_status(const void *context, size_t index,
                   bm_storage_device_status_t *status)
{
    const bm_m15_machine_t *machine = context;
    bm_floppy_drive_state_t drive_state;
    bm_fdc765_state_t fdc_state;

    if ((machine == NULL) || (status == NULL) || (index >= 2U))
        return BM_STATUS_INVALID_ARGUMENT;
    memset(status, 0, sizeof(*status));
    if (bm_floppy_drive_state(machine->floppy[index], &drive_state) !=
        BM_STATUS_OK)
        return BM_STATUS_DEVICE_ERROR;
    status->kind = BM_STORAGE_DEVICE_FLOPPY;
    status->unit = (uint32_t) index;
    status->installed = drive_state.installed;
    status->media_present = drive_state.media_present;
    status->write_protected = drive_state.write_protected;
    status->read_operations = drive_state.read_operations;
    status->write_operations = drive_state.write_operations;
    if (bm_fdc765_state(machine->fdc, &fdc_state) == BM_STATUS_OK)
        status->motor_active =
            (fdc_state.digital_output & (uint8_t) (0x10U << index)) != 0U;
    return BM_STATUS_OK;
}

static bm_status_t
m15_storage_media(void *context, bm_storage_device_kind_t kind,
                  uint32_t unit, const bm_storage_media_change_t *change)
{
    bm_m15_machine_t *machine = context;
    const bm_floppy_geometry_t geometry = { 80U, 2U, 9U, 512U };

    if ((machine == NULL) || (change == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((kind != BM_STORAGE_DEVICE_FLOPPY) || (unit >= 2U))
        return BM_STATUS_UNSUPPORTED;
    if (!change->media_present)
        return bm_floppy_drive_replace_media(machine->floppy[unit], NULL,
                                             NULL, 0);
    if ((change->media.block_size != 512U) ||
        (change->media.block_count != 1440U) ||
        (change->media.read == NULL) ||
        (change->media.write != NULL) ||
        !change->media.read_only || !change->write_protected)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_floppy_drive_replace_media(machine->floppy[unit], &geometry,
                                         &change->media, 1);
}

static const bm_machine_definition_t m15_definition = {
    .id = "olivetti-m15",
    .scheduler_ticks_per_second = BM_MACHINE_CLOCKED_TICKS_PER_SECOND,
    .engine_mode = BM_MACHINE_ENGINE_CLOCKED,
    .configuration = {
        BM_M15_CONFIG_TYPE, BM_M15_CONFIG_VERSION, sizeof(bm_m15_config_t)
    },
    .ops = {
        .validate = m15_validate,
        .create = m15_create,
        .destroy = m15_destroy,
        .reset = m15_reset,
        .inspect = m15_inspect,
        .input = m15_input,
        .video_geometry = m15_video_geometry,
        .video_render = m15_video_render,
        .storage_count = m15_storage_count,
        .storage_status = m15_storage_status,
        .storage_media = m15_storage_media
    },
    .engine = { 1U, 8U, 3U }
};

const bm_machine_definition_t *
bm_m15_machine_definition(void)
{
    return &m15_definition;
}

bm_machine_config_t
bm_m15_machine_config(const bm_m15_config_t *configuration)
{
    bm_machine_config_t result = {
        .definition = &m15_definition,
        .configuration = {
            BM_M15_CONFIG_TYPE, BM_M15_CONFIG_VERSION,
            sizeof(*configuration), configuration
        }
    };
    return result;
}
