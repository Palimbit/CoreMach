/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 rtzor
 *
 * Derived rewrite of BluMach's original PCS 86 machine support. This first
 * stage contains the documented V30, conventional RAM, system ROM map and
 * minimum motherboard I/O used to establish the platform contract.
 */
#include <blumach/systems/olivetti_pcs86.h>

#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/dma_page_registers.h>
#include <blumach/components/linear_memory.h>
#include <blumach/components/pic8259.h>
#include <blumach/components/pit8253.h>
#include <blumach/components/pvga1a.h>
#include <blumach/components/rtc_mm58167.h>

#include <ctype.h>
#include <string.h>

typedef struct bm_pcs86_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_linear_memory_t *rom;
    bm_dma8237_t *dma;
    bm_dma_page_registers_t *dma_pages;
    bm_pic8259_t *pic;
    bm_pit8253_t *pit;
    bm_pvga1a_t *video;
    bm_mm58167_t *rtc;
    bm_engine_t *engine;
    bm_cpu_id_t cpu_id;
    int cpu_ready;
    uint8_t port61;
    uint8_t control;
    uint8_t memory_blocks;
    uint8_t glue[16];
    uint8_t ps2[5];
    uint8_t jumpers;
    uint8_t nmi_mask;
    uint8_t diagnostic_port;
    uint8_t memory_control_latch;
    uint8_t video_setup_latch;
    uint8_t video_select_latch;
    uint8_t ems_page_selector[4];
    bm_pcs86_io_trace_fn io_trace;
    void *io_trace_context;
} bm_pcs86_machine_t;

static bm_status_t
pcs86_unpopulated_option_rom_access(void *context, bm_bus_transaction_t *transaction)
{
    uint32_t index;

    (void) context;
    if ((transaction == NULL) || (transaction->size == 0) ||
        (transaction->size > sizeof(transaction->value)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->operation == BM_BUS_WRITE)
        return BM_STATUS_OK;
    if ((transaction->operation != BM_BUS_READ) &&
        (transaction->operation != BM_BUS_FETCH))
        return BM_STATUS_INVALID_ARGUMENT;

    transaction->value = 0;
    for (index = 0; index < transaction->size; ++index)
        transaction->value |= UINT64_C(0xff) << (index * 8U);
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_diagnostic_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint8_t *latch;
    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    latch = (transaction->address == 0x00a0U) ?
        &machine->nmi_mask : &machine->diagnostic_port;
    if (transaction->operation == BM_BUS_READ)
        transaction->value = *latch;
    else
        *latch = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_memory_control_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;

    if ((transaction->size != 1) || (transaction->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;

    /*
     * BIOS 1.09 writes 40h here while enabling the upper conventional-memory
     * path, between accesses to the board registers at 6Ch, 6Bh and 6Fh. No
     * surviving PCS 86 documentation currently defines the individual bits.
     * Keep the write explicit and observable without assigning guessed AT
     * CMOS/NMI side effects to this XT-class machine.
     */
    machine->memory_control_latch = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_video_arbitration_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;

    if ((transaction->size != 1) || (transaction->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;

    /*
     * BIOS 1.09 brackets writes to 102h with writes to 46E8h while choosing
     * between internal and secondary video. The values are retained for
     * deterministic inspection, but no undocumented arbitration side effects
     * are assigned until the portable video device is connected.
     */
    if (transaction->address == 0x46e8U)
        machine->video_setup_latch = (uint8_t) transaction->value;
    else
        machine->video_select_latch = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_ems_selector_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    size_t window;

    if ((transaction->size != 1) || (transaction->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;

    window = (size_t) (transaction->address - 0x8400U);
    machine->ems_page_selector[window] = (uint8_t) transaction->value;
    /* The EMS aperture and backing SIMMs deliberately remain unimplemented. */
    return BM_STATUS_OK;
}

static void
pcs86_pic_output(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id, 0, asserted);
}

static bm_status_t
pcs86_interrupt_acknowledge(void *context, uint8_t *vector)
{
    bm_pcs86_machine_t *machine = context;
    return bm_pic8259_acknowledge(machine->pic, vector);
}

static void
pcs86_io_observer(void *context, const bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    if ((machine->io_trace != NULL) && (transaction->space == BM_ADDRESS_IO) &&
        (transaction->size == 1)) {
        bm_pcs86_io_trace_t trace = {
            transaction->operation,
            (uint16_t) transaction->address,
            (uint8_t) transaction->value
        };
        machine->io_trace(machine->io_trace_context, &trace);
    }
}

static void
pcs86_pit_output(void *context, unsigned int channel, int output)
{
    bm_pcs86_machine_t *machine = context;
    if (channel == 0)
        (void) bm_pic8259_set_irq(machine->pic, 0, output);
}

static bm_status_t
pcs86_board_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint16_t port = (uint16_t) transaction->address;
    uint8_t value = (uint8_t) transaction->value;
    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ) {
        switch (port) {
            case 0x0060:
                value = 0;
                (void) bm_pic8259_set_irq(machine->pic, 1, 0);
                break;
            case 0x0061:
                value = machine->port61;
                break;
            case 0x0062:
                value = machine->memory_blocks >= 10 ? 0xc0U : 0;
                break;
            case 0x0063:
                value = 0x08U;
                break;
            case 0x0064:
                value = machine->glue[4] & 0x8fU;
                break;
            case 0x0065:
                value = machine->control;
                break;
            case 0x0066:
            case 0x0067:
            case 0x0068:
            case 0x0069:
                value = machine->ps2[port - 0x0066U];
                break;
            case 0x006a:
                value = 0;
                break;
            case 0x006b:
            case 0x006c:
            case 0x006f:
                value = machine->glue[port & 0x0fU];
                break;
            default:
                value = 0xffU;
                break;
        }
        transaction->value = value;
        return BM_STATUS_OK;
    }
    switch (port) {
        case 0x0061:
            machine->port61 = value;
            return bm_pit8253_set_gate(machine->pit, 2, value & 1U);
        case 0x0064:
        case 0x006c:
        case 0x006f:
            machine->glue[port & 0x0fU] = value;
            break;
        case 0x0065:
            machine->control = value;
            break;
        case 0x0066:
            machine->ps2[0] = (value & 0xfbU) | (machine->ps2[0] & 0x04U);
            break;
        case 0x0067:
        case 0x0068:
        case 0x0069:
        case 0x006a:
            machine->ps2[port - 0x0066U] = value;
            break;
        case 0x006b:
            machine->glue[11] = value & 0xfeU;
            if (((value & 1U) != 0) && (machine->memory_blocks < 10))
                ++machine->memory_blocks;
            break;
        default:
            break;
    }
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_jumpers_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    if ((transaction->size != 1) || (transaction->operation != BM_BUS_READ))
        return BM_STATUS_UNSUPPORTED;
    transaction->value = machine->jumpers;
    return BM_STATUS_OK;
}

static const bm_pcs86_firmware_identity_t expected_firmware[] = {
    {
        "even",
        "bios-even-109",
        BM_PCS86_FIRMWARE_HALF_SIZE,
        "c92a79509def8aee30d76700a5ce1c7b6716d2375a64075c9669ca376fde4eb8"
    },
    {
        "odd",
        "bios-odd-109",
        BM_PCS86_FIRMWARE_HALF_SIZE,
        "82f8363ea7cde1fb8abe50fd76c7381831b7b89539d3dc71cbe4305fb1568d40"
    }
};

static int
ascii_equal_case_insensitive(const char *left, const char *right)
{
    if ((left == NULL) || (right == NULL))
        return 0;
    while ((*left != '\0') && (*right != '\0')) {
        if (tolower((unsigned char) *left) != tolower((unsigned char) *right))
            return 0;
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static bm_status_t
validate_blob(const bm_blob_view_t *blob, const bm_pcs86_firmware_identity_t *identity)
{
    if ((blob->data == NULL) || (blob->size != identity->size))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((blob->sha256 != NULL) &&
        !ascii_equal_case_insensitive(blob->sha256, identity->sha256))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_validate(const void *configuration)
{
    const bm_pcs86_config_t *config = configuration;
    bm_status_t status;

    if (config == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    status = validate_blob(&config->firmware_even, &expected_firmware[0]);
    if (status == BM_STATUS_OK)
        status = validate_blob(&config->firmware_odd, &expected_firmware[1]);
    return status;
}

static void
pcs86_destroy(void *context)
{
    bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return;
    bm_dma_page_registers_destroy(machine->dma_pages);
    bm_dma8237_destroy(machine->dma);
    bm_mm58167_destroy(machine->rtc);
    bm_pvga1a_destroy(machine->video);
    bm_pit8253_destroy(machine->pit);
    bm_pic8259_destroy(machine->pic);
    bm_linear_memory_destroy(machine->rom);
    bm_linear_memory_destroy(machine->ram);
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
}

static bm_status_t
pcs86_video_geometry(const void *context, bm_video_geometry_t *geometry)
{
    const bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_pvga1a_video_geometry(machine->video, geometry);
}

static bm_status_t
pcs86_video_render(const void *context, bm_video_framebuffer_t *framebuffer)
{
    const bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_pvga1a_render(machine->video, framebuffer);
}

static bm_status_t
pcs86_create(bm_engine_t *engine,
             const bm_host_services_t *host,
             const void *configuration,
             void **out_machine)
{
    const bm_pcs86_config_t *config = configuration;
    bm_pcs86_machine_t *machine;
    uint8_t *combined_rom = NULL;
    bm_cpu_t cpu;
    bm_status_t status;
    size_t index;

    if ((engine == NULL) || (out_machine == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_machine = NULL;
    status = pcs86_validate(config);
    if (status != BM_STATUS_OK)
        return status;
    machine = host->allocate(host->context, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(machine, 0, sizeof(*machine));
    machine->host = *host;
    machine->engine = engine;
    machine->control = 0x80U;
    machine->ps2[0] = 0x04U; /* The front-panel key lock is open. */
    machine->jumpers = 0xffU; /* No HDD and both floppy banks open. */
    machine->io_trace = config->io_trace;
    machine->io_trace_context = config->io_trace_context;

    status = bm_bus_create(host, 18, &machine->bus);
    if (status == BM_STATUS_OK)
        bm_bus_set_observer(machine->bus, pcs86_io_observer, machine);
    if (status == BM_STATUS_OK) {
        bm_linear_memory_config_t ram_config = {
            BM_ADDRESS_MEMORY, 0, BM_PCS86_MEMORY_SIZE, 0, NULL, 0
        };
        status = bm_linear_memory_create(host, machine->bus, &ram_config, &machine->ram);
    }
    if (status == BM_STATUS_OK) {
        combined_rom = host->allocate(host->context, BM_PCS86_ROM_SIZE);
        if (combined_rom == NULL)
            status = BM_STATUS_OUT_OF_MEMORY;
    }
    if (status == BM_STATUS_OK) {
        bm_linear_memory_config_t rom_config;
        for (index = 0; index < BM_PCS86_FIRMWARE_HALF_SIZE; ++index) {
            combined_rom[index * 2U] = config->firmware_even.data[index];
            combined_rom[index * 2U + 1U] = config->firmware_odd.data[index];
        }
        rom_config = (bm_linear_memory_config_t) {
            BM_ADDRESS_MEMORY, BM_PCS86_ROM_BASE, BM_PCS86_ROM_SIZE, 1,
            combined_rom, BM_PCS86_ROM_SIZE
        };
        status = bm_linear_memory_create(host, machine->bus, &rom_config, &machine->rom);
    }
    if (combined_rom != NULL)
        host->release(host->context, combined_rom);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_MEMORY,
                            0x000c0000U, 0x000effffU,
                            pcs86_unpopulated_option_rom_access, machine);
    if (status == BM_STATUS_OK) {
        bm_dma8237_config_t dma_config = { 0x0000U };
        status = bm_dma8237_create(host, machine->bus, &dma_config, &machine->dma);
    }
    if (status == BM_STATUS_OK) {
        bm_dma_page_registers_config_t page_config = {
            0x0080U, 0x0fU, machine->dma
        };
        status = bm_dma_page_registers_create(host, machine->bus, &page_config,
                                              &machine->dma_pages);
    }
    if (status == BM_STATUS_OK) {
        bm_pic8259_config_t pic_config = {
            0x0020U, pcs86_pic_output, machine
        };
        status = bm_pic8259_create(host, machine->bus, &pic_config, &machine->pic);
    }
    if (status == BM_STATUS_OK) {
        bm_pit8253_config_t pit_config = { 0x0040U, pcs86_pit_output, machine };
        status = bm_pit8253_create(host, machine->bus, &pit_config, &machine->pit);
    }
    if (status == BM_STATUS_OK) {
        bm_mm58167_config_t rtc_config = { 0x00b0U };
        status = bm_mm58167_create(host, machine->bus, &rtc_config, &machine->rtc);
    }
    if (status == BM_STATUS_OK) {
        bm_pvga1a_config_t video_config = { BM_PVGA1A_VRAM_SIZE };
        status = bm_pvga1a_create(host, machine->bus, &video_config, &machine->video);
    }
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0060U, 0x006fU,
                            pcs86_board_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0100U, 0x0100U,
                            pcs86_jumpers_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x00a0U, 0x00a0U,
                            pcs86_diagnostic_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0070U, 0x0070U,
                            pcs86_memory_control_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0378U, 0x0378U,
                            pcs86_diagnostic_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x8400U, 0x8403U,
                            pcs86_ems_selector_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0102U, 0x0102U,
                            pcs86_video_arbitration_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x46e8U, 0x46e8U,
                            pcs86_video_arbitration_access, machine);
    if (status == BM_STATUS_OK) {
        bm_808x_config_t cpu_config = {
            BM_808X_NEC_V30, 10000000U, machine->bus,
            config->trace, config->trace_context,
            pcs86_interrupt_acknowledge, machine
        };
        status = bm_808x_create(host, &cpu_config, &cpu);
    }
    if (status == BM_STATUS_OK) {
        status = bm_engine_add_cpu(engine, &cpu, &machine->cpu_id);
        if (status != BM_STATUS_OK)
            cpu.ops.destroy(cpu.context);
        else {
            machine->cpu_ready = 1;
            if (bm_pic8259_pending(machine->pic))
                status = bm_engine_signal_cpu(engine, machine->cpu_id, 0, 1);
        }
    }
    if (status != BM_STATUS_OK) {
        pcs86_destroy(machine);
        return status;
    }
    *out_machine = machine;
    return BM_STATUS_OK;
}

const bm_pcs86_firmware_identity_t *
bm_pcs86_expected_firmware(size_t *count)
{
    if (count != NULL)
        *count = sizeof(expected_firmware) / sizeof(expected_firmware[0]);
    return expected_firmware;
}

bm_machine_config_t
bm_pcs86_machine_config(const bm_pcs86_config_t *configuration)
{
    bm_machine_config_t result = {
        "olivetti-pcs86",
        configuration,
        {
            pcs86_validate,
            pcs86_create,
            pcs86_destroy,
            pcs86_video_geometry,
            pcs86_video_render
        },
        { 1, 10 }
    };
    return result;
}
