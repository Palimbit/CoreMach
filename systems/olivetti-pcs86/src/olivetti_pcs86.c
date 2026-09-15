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
#include <blumach/components/linear_memory.h>
#include <blumach/components/pic8259.h>
#include <blumach/components/pit8253.h>

#include <ctype.h>
#include <string.h>

typedef struct bm_pcs86_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_linear_memory_t *rom;
    bm_pic8259_t *pic;
    bm_pit8253_t *pit;
    uint8_t port61;
    uint8_t control;
    uint8_t memory_blocks;
    uint8_t glue[16];
    uint8_t ps2[5];
    uint8_t jumpers;
    bm_pcs86_io_trace_fn io_trace;
    void *io_trace_context;
} bm_pcs86_machine_t;

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
    bm_pit8253_destroy(machine->pit);
    bm_pic8259_destroy(machine->pic);
    bm_linear_memory_destroy(machine->rom);
    bm_linear_memory_destroy(machine->ram);
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
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
    machine->control = 0x80U;
    machine->ps2[0] = 0x04U; /* The front-panel key lock is open. */
    machine->jumpers = 0xffU; /* No HDD and both floppy banks open. */
    machine->io_trace = config->io_trace;
    machine->io_trace_context = config->io_trace_context;

    status = bm_bus_create(host, 6, &machine->bus);
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
    if (status == BM_STATUS_OK) {
        bm_pic8259_config_t pic_config = { 0x0020U };
        status = bm_pic8259_create(host, machine->bus, &pic_config, &machine->pic);
    }
    if (status == BM_STATUS_OK) {
        bm_pit8253_config_t pit_config = { 0x0040U, pcs86_pit_output, machine };
        status = bm_pit8253_create(host, machine->bus, &pit_config, &machine->pit);
    }
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0060U, 0x006fU,
                            pcs86_board_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0100U, 0x0100U,
                            pcs86_jumpers_access, machine);
    if (status == BM_STATUS_OK) {
        bm_808x_config_t cpu_config = {
            BM_808X_NEC_V30, 10000000U, machine->bus, config->trace, config->trace_context
        };
        status = bm_808x_create(host, &cpu_config, &cpu);
    }
    if (status == BM_STATUS_OK) {
        status = bm_engine_add_cpu(engine, &cpu, NULL);
        if (status != BM_STATUS_OK)
            cpu.ops.destroy(cpu.context);
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
        { pcs86_validate, pcs86_create, pcs86_destroy },
        { 1, 8 }
    };
    return result;
}
