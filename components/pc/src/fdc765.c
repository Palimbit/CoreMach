/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2018-2020 Fred N. van Kempen
 * Copyright 2025 Toni Riikonen
 * Copyright 2026 BluMach contributors
 *
 * Selective port of the inherited uPD765-compatible command path. The device
 * owns no global drive table, host files, timers or UI state. It intentionally
 * implements the AT register front and commands needed for controller
 * diagnostics and raw-sector boot through an explicit 8237 and block media.
 */
#include <blumach/components/fdc765.h>

#include <string.h>

#define FDC765_MAX_PARAMS 8U
#define FDC765_MAX_RESULTS 10U
struct bm_fdc765 {
    bm_host_services_t host;
    bm_dma8237_t *dma;
    bm_floppy_drive_t *drives[4];
    bm_fdc765_irq_fn irq;
    void *irq_context;
    uint16_t io_base;
    unsigned int dma_channel;
    int disk_change_active_low;
    uint8_t dor;
    uint8_t dsr;
    uint8_t ccr;
    uint8_t command;
    uint8_t params[FDC765_MAX_PARAMS];
    uint8_t param_count;
    uint8_t param_expected;
    uint8_t results[FDC765_MAX_RESULTS];
    uint8_t result_index;
    uint8_t result_count;
    uint8_t specify[2];
    uint8_t pcn[4];
    uint8_t pending_st0;
    uint8_t pending_pcn;
    uint8_t reset_sense_remaining;
    uint8_t transfer_buffer[BM_FDC765_MAX_SECTOR_SIZE];
    int interrupt_pending;
    int interrupt_asserted;
};

static void
update_irq(bm_fdc765_t *fdc)
{
    int asserted = fdc->interrupt_pending && ((fdc->dor & 0x08U) != 0U);
    if (asserted != fdc->interrupt_asserted) {
        fdc->interrupt_asserted = asserted;
        if (fdc->irq != NULL)
            fdc->irq(fdc->irq_context, asserted);
    }
}

static void
set_interrupt(bm_fdc765_t *fdc, int pending)
{
    fdc->interrupt_pending = !!pending;
    update_irq(fdc);
}

static uint8_t
main_status(const bm_fdc765_t *fdc)
{
    if (fdc->result_count != 0U)
        return 0xd0U;
    if (fdc->param_expected != 0U)
        return 0x90U;
    return 0x80U;
}

static void
clear_command(bm_fdc765_t *fdc)
{
    fdc->command = 0U;
    fdc->param_count = 0U;
    fdc->param_expected = 0U;
}

static void
set_results(bm_fdc765_t *fdc, const uint8_t *results, uint8_t count)
{
    if (count > FDC765_MAX_RESULTS)
        count = FDC765_MAX_RESULTS;
    memcpy(fdc->results, results, count);
    fdc->result_index = 0U;
    fdc->result_count = count;
    clear_command(fdc);
}

static void
set_rw_results(bm_fdc765_t *fdc,
               uint8_t st0,
               uint8_t st1,
               uint8_t st2,
               uint8_t cylinder,
               uint8_t head,
               uint8_t sector,
               uint8_t size_code)
{
    const uint8_t results[7] = {
        st0, st1, st2, cylinder, head, sector, size_code
    };
    set_results(fdc, results, 7U);
    set_interrupt(fdc, 1);
}

static bm_floppy_drive_t *
selected_drive(const bm_fdc765_t *fdc, unsigned int drive)
{
    return drive < 4U ? fdc->drives[drive] : NULL;
}

static int
motor_on(const bm_fdc765_t *fdc, unsigned int drive)
{
    return (drive < 4U) && ((fdc->dor & (uint8_t) (0x10U << drive)) != 0U);
}

static uint8_t
size_code_for(const bm_floppy_geometry_t *geometry)
{
    uint32_t bytes;
    uint8_t code = 0U;
    if (geometry == NULL)
        return 0U;
    bytes = 128U;
    while ((bytes < geometry->bytes_per_sector) && (code < 7U)) {
        bytes <<= 1U;
        ++code;
    }
    return bytes == geometry->bytes_per_sector ? code : 0xffU;
}

static bm_status_t
transfer_sector_to_memory(bm_fdc765_t *fdc,
                          bm_floppy_drive_t *drive,
                          uint8_t cylinder,
                          uint8_t head,
                          uint8_t sector,
                          int *terminal)
{
    const bm_floppy_geometry_t *geometry = bm_floppy_drive_geometry(drive);
    size_t index;
    bm_status_t status;

    if ((geometry == NULL) ||
        (geometry->bytes_per_sector > sizeof(fdc->transfer_buffer)))
        return BM_STATUS_CAPACITY_EXCEEDED;

    status = bm_floppy_drive_read_sector(drive, cylinder, head, sector,
                                         fdc->transfer_buffer,
                                         geometry->bytes_per_sector);
    if (status != BM_STATUS_OK)
        return status;
    for (index = 0U; index < geometry->bytes_per_sector; ++index) {
        status = bm_dma8237_device_write(fdc->dma, fdc->dma_channel,
                                         fdc->transfer_buffer[index], terminal);
        if ((status != BM_STATUS_OK) || *terminal)
            return status;
    }
    return BM_STATUS_OK;
}

static bm_status_t
transfer_sector_from_memory(bm_fdc765_t *fdc,
                            bm_floppy_drive_t *drive,
                            uint8_t cylinder,
                            uint8_t head,
                            uint8_t sector,
                            int *terminal)
{
    const bm_floppy_geometry_t *geometry = bm_floppy_drive_geometry(drive);
    size_t index;
    bm_status_t status;

    if ((geometry == NULL) ||
        (geometry->bytes_per_sector > sizeof(fdc->transfer_buffer)))
        return BM_STATUS_CAPACITY_EXCEEDED;

    for (index = 0U; index < geometry->bytes_per_sector; ++index) {
        status = bm_dma8237_device_read(fdc->dma, fdc->dma_channel,
                                        &fdc->transfer_buffer[index], terminal);
        if (status != BM_STATUS_OK)
            return status;
        if (*terminal && (index + 1U != geometry->bytes_per_sector))
            return BM_STATUS_DEVICE_ERROR;
    }
    return bm_floppy_drive_write_sector(drive, cylinder, head, sector,
                                        fdc->transfer_buffer,
                                        geometry->bytes_per_sector);
}

static int
advance_chs(const bm_floppy_geometry_t *geometry,
            int multi_track,
            uint8_t eot,
            uint8_t *cylinder,
            uint8_t *head,
            uint8_t *sector)
{
    if (*sector < eot) {
        ++*sector;
        return 1;
    }
    if (!multi_track)
        return 0;
    if (multi_track && (*head == 0U) && (geometry->heads > 1U)) {
        *head = 1U;
        *sector = 1U;
        return 1;
    }
    if ((uint16_t) *cylinder + 1U >= geometry->cylinders)
        return 0;
    ++*cylinder;
    *head = 0U;
    *sector = 1U;
    return 1;
}

static void
execute_data_command(bm_fdc765_t *fdc, int write)
{
    unsigned int drive_index = fdc->params[0] & 3U;
    bm_floppy_drive_t *drive = selected_drive(fdc, drive_index);
    const bm_floppy_geometry_t *geometry = bm_floppy_drive_geometry(drive);
    uint8_t cylinder = fdc->params[1];
    uint8_t head = fdc->params[2];
    uint8_t sector = fdc->params[3];
    uint8_t size_code = fdc->params[4];
    uint8_t eot = fdc->params[5];
    uint8_t expected_size_code = size_code_for(geometry);
    uint8_t st0 = (uint8_t) (drive_index | ((head & 1U) << 2U));
    int terminal = 0;
    bm_status_t status = BM_STATUS_OK;

    if ((drive == NULL) || !bm_floppy_drive_installed(drive) ||
        !bm_floppy_drive_media_present(drive) || !motor_on(fdc, drive_index)) {
        set_rw_results(fdc, (uint8_t) (st0 | 0x48U), 0x04U, 0U,
                       cylinder, head, sector, size_code);
        return;
    }
    if ((expected_size_code == 0xffU) || (size_code != expected_size_code) ||
        (geometry->bytes_per_sector > sizeof(fdc->transfer_buffer)) ||
        (head >= geometry->heads) || (sector == 0U) ||
        (sector > geometry->sectors_per_track) || (eot == 0U) ||
        (eot > geometry->sectors_per_track)) {
        set_rw_results(fdc, (uint8_t) (st0 | 0x40U), 0x04U, 0U,
                       cylinder, head, sector, size_code);
        return;
    }
    if (write && bm_floppy_drive_write_protected(drive)) {
        set_rw_results(fdc, (uint8_t) (st0 | 0x40U), 0x02U, 0U,
                       cylinder, head, sector, size_code);
        return;
    }

    (void) bm_dma8237_set_dreq(fdc->dma, fdc->dma_channel, 1);
    while (!terminal) {
        status = write ?
            transfer_sector_from_memory(fdc, drive, cylinder, head, sector,
                                        &terminal) :
            transfer_sector_to_memory(fdc, drive, cylinder, head, sector,
                                      &terminal);
        if (status != BM_STATUS_OK)
            break;
        if (!terminal &&
            !advance_chs(geometry, (fdc->command & 0x80U) != 0U, eot,
                         &cylinder, &head, &sector))
            break;
    }
    (void) bm_dma8237_set_dreq(fdc->dma, fdc->dma_channel, 0);
    if (status != BM_STATUS_OK) {
        uint8_t st1 = (status == BM_STATUS_READ_ONLY) ? 0x02U : 0x10U;
        set_rw_results(fdc, (uint8_t) (st0 | 0x40U), st1, 0U,
                       cylinder, head, sector, size_code);
        return;
    }
    set_rw_results(fdc, st0, 0U, 0U, cylinder, head, sector, size_code);
}

static void
execute_command(bm_fdc765_t *fdc)
{
    uint8_t operation = fdc->command & 0x1fU;
    unsigned int drive_index;
    bm_floppy_drive_t *drive;
    const bm_floppy_geometry_t *geometry;
    uint8_t result[2];

    switch (operation) {
        case 0x03U: /* Specify */
            fdc->specify[0] = fdc->params[0];
            fdc->specify[1] = fdc->params[1];
            clear_command(fdc);
            break;
        case 0x04U: /* Sense drive status */
            drive_index = fdc->params[0] & 3U;
            drive = selected_drive(fdc, drive_index);
            result[0] = fdc->params[0] & 7U;
            if ((drive != NULL) && bm_floppy_drive_installed(drive)) {
                result[0] |= 0x20U;
                geometry = bm_floppy_drive_geometry(drive);
                if (geometry->heads > 1U)
                    result[0] |= 0x08U;
                if (bm_floppy_drive_cylinder(drive) == 0U)
                    result[0] |= 0x10U;
                if (bm_floppy_drive_write_protected(drive))
                    result[0] |= 0x40U;
            }
            set_results(fdc, result, 1U);
            break;
        case 0x05U: /* Write data */
            execute_data_command(fdc, 1);
            break;
        case 0x06U: /* Read data */
            execute_data_command(fdc, 0);
            break;
        case 0x07U: /* Recalibrate */
            drive_index = fdc->params[0] & 3U;
            drive = selected_drive(fdc, drive_index);
            fdc->pending_st0 = (uint8_t) (0x20U | drive_index);
            fdc->pending_pcn = 0U;
            fdc->pcn[drive_index] = 0U;
            if ((drive == NULL) ||
                (bm_floppy_drive_seek(drive, 0U) != BM_STATUS_OK))
                fdc->pending_st0 = (uint8_t) (0x70U | drive_index);
            clear_command(fdc);
            set_interrupt(fdc, 1);
            break;
        case 0x08U: /* Sense interrupt status */
            if (fdc->reset_sense_remaining != 0U) {
                drive_index = 4U - fdc->reset_sense_remaining;
                result[0] = (uint8_t) (0xc0U | drive_index);
                result[1] = fdc->pcn[drive_index];
                --fdc->reset_sense_remaining;
            } else if (fdc->interrupt_pending) {
                result[0] = fdc->pending_st0;
                result[1] = fdc->pending_pcn;
            } else {
                result[0] = 0x80U;
                set_results(fdc, result, 1U);
                break;
            }
            set_interrupt(fdc, 0);
            set_results(fdc, result, 2U);
            break;
        case 0x0aU: /* Read sector ID */
            drive_index = fdc->params[0] & 3U;
            drive = selected_drive(fdc, drive_index);
            geometry = bm_floppy_drive_geometry(drive);
            if ((drive == NULL) || !bm_floppy_drive_media_present(drive) ||
                !motor_on(fdc, drive_index) || (geometry == NULL)) {
                set_rw_results(fdc, (uint8_t) (0x48U | drive_index), 0x04U,
                               0U, fdc->pcn[drive_index], 0U, 1U, 2U);
            } else {
                set_rw_results(fdc, (uint8_t) drive_index, 0U, 0U,
                               fdc->pcn[drive_index],
                               (uint8_t) ((fdc->params[0] >> 2U) & 1U), 1U,
                               size_code_for(geometry));
            }
            break;
        case 0x0fU: /* Seek */
            drive_index = fdc->params[0] & 3U;
            drive = selected_drive(fdc, drive_index);
            fdc->pending_st0 = (uint8_t) (0x20U | drive_index |
                                           (fdc->params[0] & 4U));
            fdc->pending_pcn = fdc->params[1];
            fdc->pcn[drive_index] = fdc->params[1];
            if ((drive == NULL) ||
                (bm_floppy_drive_seek(drive, fdc->params[1]) != BM_STATUS_OK))
                fdc->pending_st0 = (uint8_t) (0x40U | drive_index);
            clear_command(fdc);
            set_interrupt(fdc, 1);
            break;
        case 0x10U: /* Version */
            result[0] = 0x90U;
            set_results(fdc, result, 1U);
            break;
        default:
            result[0] = 0x80U;
            set_results(fdc, result, 1U);
            break;
    }
}

static uint8_t
parameter_count(uint8_t command)
{
    switch (command & 0x1fU) {
        case 0x03U: return 2U;
        case 0x04U: return 1U;
        case 0x05U:
        case 0x06U: return 8U;
        case 0x07U:
        case 0x0aU: return 1U;
        case 0x0fU: return 2U;
        default: return 0U;
    }
}

static uint8_t
read_data(bm_fdc765_t *fdc)
{
    uint8_t value = 0xffU;
    if (fdc->result_count != 0U) {
        if (fdc->result_index == 0U)
            set_interrupt(fdc, 0);
        value = fdc->results[fdc->result_index++];
        --fdc->result_count;
        if (fdc->result_count == 0U)
            fdc->result_index = 0U;
    }
    return value;
}

static void
write_data(bm_fdc765_t *fdc, uint8_t value)
{
    if (fdc->result_count != 0U)
        return;
    if (fdc->param_expected == 0U) {
        fdc->command = value;
        fdc->param_count = 0U;
        fdc->param_expected = parameter_count(value);
        if (fdc->param_expected == 0U)
            execute_command(fdc);
        return;
    }
    fdc->params[fdc->param_count++] = value;
    if (fdc->param_count == fdc->param_expected)
        execute_command(fdc);
}

static void
write_dor(bm_fdc765_t *fdc, uint8_t value)
{
    uint8_t previous = fdc->dor;
    fdc->dor = value;
    if ((value & 0x04U) == 0U) {
        clear_command(fdc);
        fdc->result_index = 0U;
        fdc->result_count = 0U;
        fdc->reset_sense_remaining = 0U;
        set_interrupt(fdc, 0);
    } else if ((previous & 0x04U) == 0U) {
        memset(fdc->pcn, 0, sizeof(fdc->pcn));
        fdc->pending_st0 = 0xc0U;
        fdc->pending_pcn = 0U;
        fdc->reset_sense_remaining = 4U;
        clear_command(fdc);
        fdc->result_index = 0U;
        fdc->result_count = 0U;
        set_interrupt(fdc, 1);
    } else {
        update_irq(fdc);
    }
}

static bm_status_t
fdc_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_fdc765_t *fdc = context;
    uint8_t offset;
    uint8_t value;
    unsigned int drive_index;
    bm_floppy_drive_t *drive;
    int changed;

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    offset = (uint8_t) (transaction->address - fdc->io_base);
    if (transaction->operation == BM_BUS_READ) {
        switch (offset) {
            case 1U:
                drive_index = fdc->dor & 3U;
                transaction->value = (uint8_t) (0x70U &
                    (uint8_t) ~(drive_index ? 0x40U : 0x20U));
                break;
            case 2U: transaction->value = fdc->dor; break;
            case 3U: transaction->value = 0x20U; break;
            case 4U: transaction->value = main_status(fdc); break;
            case 5U: transaction->value = read_data(fdc); break;
            case 7U:
                drive_index = fdc->dor & 3U;
                drive = selected_drive(fdc, drive_index);
                changed = (drive == NULL) ||
                          !bm_floppy_drive_media_present(drive) ||
                          bm_floppy_drive_changed(drive);
                value = changed ? 0x80U : 0U;
                if (fdc->disk_change_active_low)
                    value ^= 0x80U;
                transaction->value = value | 0x7fU;
                break;
            default: transaction->value = 0xffU; break;
        }
        return BM_STATUS_OK;
    }

    value = (uint8_t) transaction->value;
    switch (offset) {
        case 2U: write_dor(fdc, value); break;
        case 4U:
            fdc->dsr = value;
            if ((value & 0x80U) != 0U) {
                write_dor(fdc, (uint8_t) (fdc->dor & 0xfbU));
                write_dor(fdc, (uint8_t) (fdc->dor | 0x04U));
            }
            break;
        case 5U: write_data(fdc, value); break;
        case 7U: fdc->ccr = value & 3U; break;
        default: break;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_fdc765_create(const bm_host_services_t *host,
                 bm_bus_t *bus,
                 const bm_fdc765_config_t *config,
                 bm_fdc765_t **out_fdc)
{
    bm_fdc765_t *fdc;
    bm_status_t status;
    size_t index;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_fdc == NULL) || (config->dma == NULL) ||
        (config->dma_channel >= 4U) ||
        ((uint32_t) config->io_base + 7U > UINT16_MAX))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_fdc = NULL;
    fdc = host->allocate(host->context, sizeof(*fdc));
    if (fdc == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(fdc, 0, sizeof(*fdc));
    fdc->host = *host;
    fdc->dma = config->dma;
    fdc->irq = config->irq;
    fdc->irq_context = config->irq_context;
    fdc->io_base = config->io_base;
    fdc->dma_channel = config->dma_channel;
    fdc->disk_change_active_low = !!config->disk_change_active_low;
    for (index = 0U; index < 4U; ++index)
        fdc->drives[index] = config->drives[index];
    bm_fdc765_reset(fdc);
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base,
                        (uint32_t) config->io_base + 7U, fdc_access, fdc);
    if (status != BM_STATUS_OK) {
        bm_fdc765_destroy(fdc);
        return status;
    }
    *out_fdc = fdc;
    return BM_STATUS_OK;
}

void
bm_fdc765_destroy(bm_fdc765_t *fdc)
{
    if (fdc == NULL)
        return;
    set_interrupt(fdc, 0);
    fdc->host.release(fdc->host.context, fdc);
}

void
bm_fdc765_reset(bm_fdc765_t *fdc)
{
    size_t index;
    int was_asserted;
    if (fdc == NULL)
        return;
    was_asserted = fdc->interrupt_asserted;
    fdc->dor = 0U;
    fdc->dsr = 0U;
    fdc->ccr = 0U;
    fdc->command = 0U;
    memset(fdc->params, 0, sizeof(fdc->params));
    fdc->param_count = 0U;
    fdc->param_expected = 0U;
    memset(fdc->results, 0, sizeof(fdc->results));
    fdc->result_index = 0U;
    fdc->result_count = 0U;
    memset(fdc->specify, 0, sizeof(fdc->specify));
    memset(fdc->pcn, 0, sizeof(fdc->pcn));
    fdc->pending_st0 = 0U;
    fdc->pending_pcn = 0U;
    fdc->reset_sense_remaining = 0U;
    fdc->interrupt_pending = 0;
    fdc->interrupt_asserted = 0;
    if (was_asserted && (fdc->irq != NULL))
        fdc->irq(fdc->irq_context, 0);
    for (index = 0U; index < 4U; ++index)
        bm_floppy_drive_reset(fdc->drives[index]);
    (void) bm_dma8237_set_dreq(fdc->dma, fdc->dma_channel, 0);
}

bm_status_t
bm_fdc765_state(const bm_fdc765_t *fdc, bm_fdc765_state_t *state)
{
    if ((fdc == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->digital_output = fdc->dor;
    state->main_status = main_status(fdc);
    state->data_rate = fdc->ccr;
    state->command = fdc->command;
    state->selected_drive = fdc->dor & 3U;
    state->reset_sense_remaining = fdc->reset_sense_remaining;
    state->result_count = fdc->result_count;
    state->interrupt_pending = fdc->interrupt_pending;
    state->interrupt_asserted = fdc->interrupt_asserted;
    return BM_STATUS_OK;
}
