/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fdc765_test_harness.h"

#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

void
fdc765_test_machine_create(fdc765_test_machine_t *machine,
                           const fdc765_test_config_t *config)
{
    bm_linear_memory_config_t ram_config = {
        BM_ADDRESS_MEMORY, 0U, 0x10000U, 0, NULL, 0U
    };
    bm_dma8237_config_t dma_config = { 0U };
    bm_fdc765_config_t fdc_config;
    size_t index;

    assert(machine != NULL);
    assert(config != NULL);
    memset(machine, 0, sizeof(*machine));
    machine->host = bm_null_host_services();
    assert(bm_bus_create(&machine->host, 8U, &machine->bus) == BM_STATUS_OK);
    assert(bm_linear_memory_create(&machine->host, machine->bus, &ram_config,
                                   &machine->ram) == BM_STATUS_OK);
    assert(bm_dma8237_create(&machine->host, machine->bus, &dma_config,
                             &machine->dma) == BM_STATUS_OK);
    for (index = 0U; index < 4U; ++index) {
        if (config->drive_configs[index] != NULL) {
            assert(bm_floppy_drive_create(&machine->host,
                                          config->drive_configs[index],
                                          &machine->drives[index]) ==
                   BM_STATUS_OK);
        }
    }

    memset(&fdc_config, 0, sizeof(fdc_config));
    fdc_config.io_base = 0x03f0U;
    fdc_config.dma_channel = 2U;
    fdc_config.disk_change_active_low = config->disk_change_active_low;
    fdc_config.dma = machine->dma;
    for (index = 0U; index < 4U; ++index)
        fdc_config.drives[index] = machine->drives[index];
    fdc_config.irq = config->irq;
    fdc_config.irq_context = config->irq_context;
    assert(bm_fdc765_create(&machine->host, machine->bus, &fdc_config,
                            &machine->fdc) == BM_STATUS_OK);
}

void
fdc765_test_machine_destroy(fdc765_test_machine_t *machine)
{
    size_t index;

    if (machine == NULL)
        return;
    bm_fdc765_destroy(machine->fdc);
    for (index = 4U; index != 0U; --index)
        bm_floppy_drive_destroy(machine->drives[index - 1U]);
    bm_dma8237_destroy(machine->dma);
    bm_linear_memory_destroy(machine->ram);
    bm_bus_destroy(machine->bus);
    memset(machine, 0, sizeof(*machine));
}

void
fdc765_test_io_write(fdc765_test_machine_t *machine,
                     uint16_t port,
                     uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(machine != NULL);
    assert(bm_bus_transact(machine->bus, &transaction) == BM_STATUS_OK);
}

uint8_t
fdc765_test_io_read(fdc765_test_machine_t *machine, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(machine != NULL);
    assert(bm_bus_transact(machine->bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

void
fdc765_test_memory_write(fdc765_test_machine_t *machine,
                         uint64_t address,
                         uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, address, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(machine != NULL);
    assert(bm_bus_transact(machine->bus, &transaction) == BM_STATUS_OK);
}

uint8_t
fdc765_test_memory_read(fdc765_test_machine_t *machine, uint64_t address)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_READ, address, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0
    };
    assert(machine != NULL);
    assert(bm_bus_transact(machine->bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

void
fdc765_test_program_dma(fdc765_test_machine_t *machine,
                        uint16_t address,
                        uint16_t count,
                        uint8_t mode)
{
    fdc765_test_io_write(machine, 0x0cU, 0U);
    fdc765_test_io_write(machine, 0x04U, (uint8_t) address);
    fdc765_test_io_write(machine, 0x04U, (uint8_t) (address >> 8U));
    fdc765_test_io_write(machine, 0x05U, (uint8_t) count);
    fdc765_test_io_write(machine, 0x05U, (uint8_t) (count >> 8U));
    fdc765_test_io_write(machine, 0x0bU, mode);
    fdc765_test_io_write(machine, 0x0aU, 0x02U);
}

void
fdc765_test_send_command(fdc765_test_machine_t *machine,
                         uint8_t command,
                         const uint8_t *params,
                         size_t count)
{
    size_t index;

    assert(machine != NULL);
    assert(params != NULL || count == 0U);
    fdc765_test_io_write(machine, 0x03f5U, command);
    for (index = 0U; index < count; ++index)
        fdc765_test_io_write(machine, 0x03f5U, params[index]);
}

void
fdc765_test_read_results(fdc765_test_machine_t *machine,
                         uint8_t *results,
                         size_t count)
{
    size_t index;

    assert(machine != NULL);
    assert(results != NULL || count == 0U);
    for (index = 0U; index < count; ++index)
        results[index] = fdc765_test_io_read(machine, 0x03f5U);
}

uint8_t
fdc765_test_drive_dor(unsigned int drive)
{
    assert(drive < 4U);
    return (uint8_t) (0x0cU | drive | (0x10U << drive));
}
