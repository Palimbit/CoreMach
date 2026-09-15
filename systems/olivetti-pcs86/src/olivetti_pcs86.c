/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 rtzor
 *
 * Derived rewrite of BluMach's original PCS 86 machine support. This first
 * stage contains only the documented V30, conventional RAM and system ROM map.
 */
#include <blumach/systems/olivetti_pcs86.h>

#include <blumach/components/bus.h>
#include <blumach/components/linear_memory.h>

#include <ctype.h>
#include <string.h>

typedef struct bm_pcs86_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_linear_memory_t *rom;
} bm_pcs86_machine_t;

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

    status = bm_bus_create(host, 2, &machine->bus);
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
