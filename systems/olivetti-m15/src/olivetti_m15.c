/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2025 Sarah Walker
 * Copyright 2016-2025 Miran Grca
 * Copyright 2017-2025 Fred N. van Kempen
 * Copyright 2020 EngiNerd
 * Copyright 2025 Jasmine Iwanek
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

#include <string.h>

#define M15_ROM_BASE UINT64_C(0x000f0000)
#define M15_VIDEO_RAM_BASE UINT64_C(0x000b8000)
#define M15_VIDEO_RAM_SIZE 16384U

typedef struct bm_m15_machine {
    bm_host_services_t host;
    bm_engine_t *engine;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_linear_memory_t *video_ram;
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
            machine->port_b = value;
            return bm_pit8253_set_gate(machine->pit, 2U,
                                       (value & 1U) != 0U);
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
                transaction->value = 0U;
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
            transaction->value = machine->keyboard_response_pending ? 1U : 0U;
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
    bm_linear_memory_destroy(machine->video_ram);
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
        const bm_linear_memory_config_t memory = {
            BM_ADDRESS_MEMORY, M15_VIDEO_RAM_BASE, M15_VIDEO_RAM_SIZE,
            BM_LINEAR_MEMORY_WRITABLE, NULL, 0U
        };
        status = bm_linear_memory_create(host, machine->bus, &memory,
                                         &machine->video_ram);
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
    bm_fdc765_reset(machine->fdc);
    bm_floppy_drive_reset(machine->floppy[0]);
    bm_floppy_drive_reset(machine->floppy[1]);
    machine->port_b = 0U;
    machine->keyboard_response = 0U;
    machine->keyboard_response_pending = 0;
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
        .inspect = m15_inspect
    },
    .engine = { 1U, 8U, 2U }
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
