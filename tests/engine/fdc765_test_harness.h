/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_TESTS_FDC765_TEST_HARNESS_H
#define BLUMACH_TESTS_FDC765_TEST_HARNESS_H

#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/fdc765.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/components/linear_memory.h>

#include <stddef.h>
#include <stdint.h>

typedef struct fdc765_test_config {
    const bm_floppy_drive_config_t *drive_configs[4];
    bm_fdc765_irq_fn irq;
    void *irq_context;
    int disk_change_active_low;
} fdc765_test_config_t;

typedef struct fdc765_test_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    bm_linear_memory_t *ram;
    bm_dma8237_t *dma;
    bm_floppy_drive_t *drives[4];
    bm_fdc765_t *fdc;
} fdc765_test_machine_t;

void fdc765_test_machine_create(fdc765_test_machine_t *machine,
                                const fdc765_test_config_t *config);
void fdc765_test_machine_destroy(fdc765_test_machine_t *machine);
void fdc765_test_io_write(fdc765_test_machine_t *machine,
                          uint16_t port,
                          uint8_t value);
uint8_t fdc765_test_io_read(fdc765_test_machine_t *machine, uint16_t port);
void fdc765_test_memory_write(fdc765_test_machine_t *machine,
                              uint64_t address,
                              uint8_t value);
uint8_t fdc765_test_memory_read(fdc765_test_machine_t *machine,
                                uint64_t address);
void fdc765_test_program_dma(fdc765_test_machine_t *machine,
                             uint16_t address,
                             uint16_t count,
                             uint8_t mode);
void fdc765_test_send_command(fdc765_test_machine_t *machine,
                              uint8_t command,
                              const uint8_t *params,
                              size_t count);
void fdc765_test_read_results(fdc765_test_machine_t *machine,
                              uint8_t *results,
                              size_t count);
uint8_t fdc765_test_drive_dor(unsigned int drive);

#endif
