/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2020 Sarah Walker
 * Copyright 2016-2020 Miran Grca
 * Copyright 2018-2020 Fred N. van Kempen
 * Copyright 2025 Toni Riikonen
 * Copyright 2026 BluMach contributors
 *
 * Portable WD37C65 functional core.  The command subset and media rules are
 * derived from BluMach's existing instance-local fdc765 component; transfers
 * are exposed one byte at a time so the AT DMA owner remains authoritative.
 */
#include <blumach/components/wd37c65.h>

#include <string.h>

#define WD37C65_MAX_PARAMS 8U
#define WD37C65_MAX_RESULTS 10U
#define WD37C65_MAX_SECTOR_SIZE 4096U

typedef enum transfer_phase {
    TRANSFER_NONE = 0,
    TRANSFER_READ,
    TRANSFER_WRITE
} transfer_phase_t;

struct bm_wd37c65 {
    bm_host_services_t host;
    bm_wd37c65_config_t config;
    uint8_t dor, dsr, ccr, command;
    uint8_t params[WD37C65_MAX_PARAMS], param_count, param_expected;
    uint8_t results[WD37C65_MAX_RESULTS], result_index, result_count;
    uint8_t specify[2], pcn[4], pending_st0, pending_pcn;
    uint8_t reset_sense_remaining;
    uint8_t buffer[WD37C65_MAX_SECTOR_SIZE];
    size_t transfer_index, transfer_size;
    uint8_t transfer_drive, cylinder, head, sector, size_code, eot;
    transfer_phase_t transfer;
    bm_status_t failure;
    int irq_pending, irq_asserted, dreq_asserted, terminal_count, busy;
};

static void set_line(bm_at_line_fn fn, void *context, int *state, int level)
{
    level = !!level;
    if (*state != level) {
        *state = level;
        if (fn) fn(context, level);
    }
}

static void update_irq(bm_wd37c65_t *fdc)
{
    set_line(fdc->config.irq, fdc->config.output_context,
             &fdc->irq_asserted,
             fdc->irq_pending && ((fdc->dor & 0x08U) != 0U));
}

static void set_irq(bm_wd37c65_t *fdc, int pending)
{
    fdc->irq_pending = !!pending;
    update_irq(fdc);
}

static void set_dreq(bm_wd37c65_t *fdc, int level)
{
    set_line(fdc->config.dreq, fdc->config.output_context,
             &fdc->dreq_asserted, level);
}

static uint8_t main_status(const bm_wd37c65_t *fdc)
{
    if (fdc->result_count) return 0xd0U; /* RQM, DIO, controller busy. */
    if (fdc->transfer != TRANSFER_NONE) return 0x10U; /* DMA execution. */
    if (fdc->param_expected) return 0x90U; /* RQM, command phase. */
    return 0x80U; /* RQM, idle. */
}

static void clear_command(bm_wd37c65_t *fdc)
{
    fdc->command = fdc->param_count = fdc->param_expected = 0U;
}

static void set_results(bm_wd37c65_t *fdc, const uint8_t *bytes, uint8_t count)
{
    if (count > WD37C65_MAX_RESULTS) count = WD37C65_MAX_RESULTS;
    memcpy(fdc->results, bytes, count);
    fdc->result_index = 0U;
    fdc->result_count = count;
    clear_command(fdc);
}

static void finish_rw(bm_wd37c65_t *fdc, uint8_t st0, uint8_t st1, uint8_t st2)
{
    const uint8_t result[7] = {
        st0, st1, st2, fdc->cylinder, fdc->head, fdc->sector, fdc->size_code
    };
    fdc->transfer = TRANSFER_NONE;
    fdc->terminal_count = 0;
    set_dreq(fdc, 0);
    set_results(fdc, result, 7U);
    set_irq(fdc, 1);
}

static bm_floppy_drive_t *drive_at(const bm_wd37c65_t *fdc, unsigned drive)
{
    return drive < 4U ? fdc->config.drives[drive] : NULL;
}

static int motor_on(const bm_wd37c65_t *fdc, unsigned drive)
{
    return drive < 4U && (fdc->dor & (uint8_t)(0x10U << drive));
}

static uint8_t geometry_size_code(const bm_floppy_geometry_t *geometry)
{
    uint32_t bytes = 128U;
    uint8_t code = 0U;
    if (!geometry) return 0xffU;
    while (bytes < geometry->bytes_per_sector && code < 7U) {
        bytes <<= 1U;
        ++code;
    }
    return bytes == geometry->bytes_per_sector ? code : 0xffU;
}

static int advance_chs(bm_wd37c65_t *fdc,
                       const bm_floppy_geometry_t *geometry)
{
    if (fdc->sector < fdc->eot) {
        ++fdc->sector;
        return 1;
    }
    if (!(fdc->command & 0x80U)) return 0;
    if (!fdc->head && geometry->heads > 1U) {
        fdc->head = 1U;
        fdc->sector = 1U;
        return 1;
    }
    if ((uint16_t)fdc->cylinder + 1U >= geometry->cylinders) return 0;
    ++fdc->cylinder;
    fdc->head = 0U;
    fdc->sector = 1U;
    return 1;
}

static int has_next_sector(const bm_wd37c65_t *fdc,
                           const bm_floppy_geometry_t *geometry)
{
    if (fdc->sector < fdc->eot) return 1;
    if (!(fdc->command & 0x80U)) return 0;
    if (!fdc->head && geometry->heads > 1U) return 1;
    return (uint16_t)fdc->cylinder + 1U < geometry->cylinders;
}

static bm_status_t load_sector(bm_wd37c65_t *fdc)
{
    bm_floppy_drive_t *drive = drive_at(fdc, fdc->transfer_drive);
    return bm_floppy_drive_read_sector(drive, fdc->cylinder, fdc->head,
                                       fdc->sector, fdc->buffer,
                                       fdc->transfer_size);
}

static bm_status_t store_sector(bm_wd37c65_t *fdc)
{
    bm_floppy_drive_t *drive = drive_at(fdc, fdc->transfer_drive);
    return bm_floppy_drive_write_sector(drive, fdc->cylinder, fdc->head,
                                        fdc->sector, fdc->buffer,
                                        fdc->transfer_size);
}

static bm_status_t begin_rw(bm_wd37c65_t *fdc, int write)
{
    bm_floppy_drive_t *drive;
    const bm_floppy_geometry_t *geometry;
    uint8_t st0;
    bm_status_t status;

    fdc->transfer_drive = fdc->params[0] & 3U;
    fdc->cylinder = fdc->params[1];
    fdc->head = fdc->params[2];
    fdc->sector = fdc->params[3];
    fdc->size_code = fdc->params[4];
    fdc->eot = fdc->params[5];
    st0 = (uint8_t)(fdc->transfer_drive | ((fdc->head & 1U) << 2U));
    drive = drive_at(fdc, fdc->transfer_drive);
    geometry = bm_floppy_drive_geometry(drive);
    if (!drive || !bm_floppy_drive_installed(drive) ||
        !bm_floppy_drive_media_present(drive) ||
        !motor_on(fdc, fdc->transfer_drive)) {
        finish_rw(fdc, (uint8_t)(st0 | 0x48U), 0x04U, 0U);
        return BM_STATUS_OK;
    }
    if (!geometry || geometry_size_code(geometry) != fdc->size_code ||
        geometry->bytes_per_sector > sizeof(fdc->buffer) ||
        fdc->head >= geometry->heads || !fdc->sector ||
        fdc->sector > geometry->sectors_per_track || !fdc->eot) {
        finish_rw(fdc, (uint8_t)(st0 | 0x40U), 0x04U, 0U);
        return BM_STATUS_OK;
    }
    if (write && bm_floppy_drive_write_protected(drive)) {
        finish_rw(fdc, (uint8_t)(st0 | 0x40U), 0x02U, 0U);
        return BM_STATUS_OK;
    }
    /* ND=1 requests non-DMA execution, which this first portable profile does
     * not implement. Refuse before a media callback or line transition. */
    if (fdc->specify[1] & 1U) return BM_STATUS_UNSUPPORTED;
    fdc->transfer_size = geometry->bytes_per_sector;
    fdc->transfer_index = 0U;
    fdc->terminal_count = 0;
    fdc->transfer = write ? TRANSFER_WRITE : TRANSFER_READ;
    if (!write) {
        status = load_sector(fdc);
        if (status != BM_STATUS_OK) {
            fdc->transfer = TRANSFER_NONE;
            fdc->failure = status;
            return status;
        }
    }
    clear_command(fdc);
    set_dreq(fdc, 1);
    return BM_STATUS_OK;
}

static uint8_t parameter_count(uint8_t command)
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

static bm_status_t execute_command(bm_wd37c65_t *fdc)
{
    uint8_t operation = fdc->command & 0x1fU;
    unsigned drive_index;
    bm_floppy_drive_t *drive;
    const bm_floppy_geometry_t *geometry;
    uint8_t result[2];
    bm_status_t status;
    switch (operation) {
    case 0x03U:
        fdc->specify[0] = fdc->params[0];
        fdc->specify[1] = fdc->params[1];
        clear_command(fdc);
        return BM_STATUS_OK;
    case 0x04U:
        drive_index = fdc->params[0] & 3U;
        drive = drive_at(fdc, drive_index);
        result[0] = fdc->params[0] & 7U;
        if (drive && bm_floppy_drive_installed(drive)) {
            result[0] |= 0x20U;
            geometry = bm_floppy_drive_geometry(drive);
            if (geometry && geometry->heads > 1U) result[0] |= 0x08U;
            if (!bm_floppy_drive_cylinder(drive)) result[0] |= 0x10U;
            if (bm_floppy_drive_write_protected(drive)) result[0] |= 0x40U;
        }
        set_results(fdc, result, 1U);
        return BM_STATUS_OK;
    case 0x05U: return begin_rw(fdc, 1);
    case 0x06U: return begin_rw(fdc, 0);
    case 0x07U:
        drive_index = fdc->params[0] & 3U;
        drive = drive_at(fdc, drive_index);
        fdc->pending_st0 = (uint8_t)(0x20U | drive_index);
        fdc->pending_pcn = fdc->pcn[drive_index] = 0U;
        if (!drive || (status = bm_floppy_drive_seek(drive, 0U)) != BM_STATUS_OK) {
            if (drive && status != BM_STATUS_INVALID_ARGUMENT) {
                fdc->failure = status;
                return status;
            }
            fdc->pending_st0 = (uint8_t)(0x70U | drive_index);
        }
        clear_command(fdc);
        set_irq(fdc, 1);
        return BM_STATUS_OK;
    case 0x08U:
        if (fdc->reset_sense_remaining) {
            drive_index = 4U - fdc->reset_sense_remaining;
            result[0] = (uint8_t)(0xc0U | drive_index);
            result[1] = fdc->pcn[drive_index];
            --fdc->reset_sense_remaining;
        } else if (fdc->irq_pending) {
            result[0] = fdc->pending_st0;
            result[1] = fdc->pending_pcn;
        } else {
            result[0] = 0x80U;
            set_results(fdc, result, 1U);
            return BM_STATUS_OK;
        }
        set_irq(fdc, 0);
        set_results(fdc, result, 2U);
        return BM_STATUS_OK;
    case 0x0aU:
        drive_index = fdc->params[0] & 3U;
        drive = drive_at(fdc, drive_index);
        geometry = bm_floppy_drive_geometry(drive);
        fdc->transfer_drive = (uint8_t)drive_index;
        fdc->cylinder = fdc->pcn[drive_index];
        fdc->head = (uint8_t)((fdc->params[0] >> 2U) & 1U);
        fdc->sector = 1U;
        fdc->size_code = geometry_size_code(geometry);
        if (!drive || !bm_floppy_drive_media_present(drive) ||
            !motor_on(fdc, drive_index) || !geometry)
            finish_rw(fdc, (uint8_t)(0x48U | drive_index), 0x04U, 0U);
        else
            finish_rw(fdc, (uint8_t)drive_index, 0U, 0U);
        return BM_STATUS_OK;
    case 0x0fU:
        drive_index = fdc->params[0] & 3U;
        drive = drive_at(fdc, drive_index);
        fdc->pending_st0 = (uint8_t)(0x20U | drive_index |
                                     (fdc->params[0] & 4U));
        fdc->pending_pcn = fdc->params[1];
        fdc->pcn[drive_index] = fdc->params[1];
        if (!drive || (status = bm_floppy_drive_seek(drive, fdc->params[1])) != BM_STATUS_OK) {
            if (drive && status != BM_STATUS_INVALID_ARGUMENT) {
                fdc->failure = status;
                return status;
            }
            fdc->pending_st0 = (uint8_t)(0x40U | drive_index);
        }
        clear_command(fdc);
        set_irq(fdc, 1);
        return BM_STATUS_OK;
    case 0x10U:
        result[0] = 0x90U;
        set_results(fdc, result, 1U);
        return BM_STATUS_OK;
    default:
        result[0] = 0x80U;
        set_results(fdc, result, 1U);
        return BM_STATUS_OK;
    }
}

static bm_status_t write_data(bm_wd37c65_t *fdc, uint8_t value)
{
    if (fdc->result_count || fdc->transfer != TRANSFER_NONE) return BM_STATUS_OK;
    if (!fdc->param_expected) {
        fdc->command = value;
        fdc->param_count = 0U;
        fdc->param_expected = parameter_count(value);
        return fdc->param_expected ? BM_STATUS_OK : execute_command(fdc);
    }
    fdc->params[fdc->param_count++] = value;
    return fdc->param_count == fdc->param_expected ? execute_command(fdc) : BM_STATUS_OK;
}

static uint8_t read_data(bm_wd37c65_t *fdc)
{
    uint8_t value = 0xffU;
    if (fdc->result_count) {
        if (!fdc->result_index) set_irq(fdc, 0);
        value = fdc->results[fdc->result_index++];
        --fdc->result_count;
        if (!fdc->result_count) fdc->result_index = 0U;
    }
    return value;
}

static void write_dor(bm_wd37c65_t *fdc, uint8_t value)
{
    uint8_t previous = fdc->dor;
    fdc->dor = value;
    if (!(value & 4U)) {
        clear_command(fdc);
        fdc->result_index = fdc->result_count = 0U;
        fdc->reset_sense_remaining = 0U;
        fdc->transfer = TRANSFER_NONE;
        set_dreq(fdc, 0);
        set_irq(fdc, 0);
    } else if (!(previous & 4U)) {
        memset(fdc->pcn, 0, sizeof(fdc->pcn));
        fdc->pending_st0 = 0xc0U;
        fdc->pending_pcn = 0U;
        fdc->reset_sense_remaining = 4U;
        clear_command(fdc);
        fdc->result_index = fdc->result_count = 0U;
        fdc->transfer = TRANSFER_NONE;
        set_dreq(fdc, 0);
        set_irq(fdc, 1);
    } else update_irq(fdc);
}

bm_status_t bm_wd37c65_create(const bm_host_services_t *host,
                              const bm_wd37c65_config_t *config,
                              bm_wd37c65_t **out_fdc)
{
    bm_wd37c65_t *fdc;
    if (!out_fdc) return BM_STATUS_INVALID_ARGUMENT;
    *out_fdc = NULL;
    if (bm_host_services_validate(host) != BM_STATUS_OK || !config ||
        (uint32_t)config->io_base + 7U > UINT16_MAX ||
        !config->clock.cycles_per_second_numerator ||
        !config->clock.cycles_per_second_denominator)
        return BM_STATUS_INVALID_ARGUMENT;
    fdc = host->allocate(host->context, sizeof(*fdc));
    if (!fdc) return BM_STATUS_OUT_OF_MEMORY;
    memset(fdc, 0, sizeof(*fdc));
    fdc->host = *host;
    fdc->config = *config;
    fdc->failure = BM_STATUS_OK;
    *out_fdc = fdc;
    return BM_STATUS_OK;
}

void bm_wd37c65_destroy(bm_wd37c65_t *fdc)
{
    if (!fdc) return;
    set_dreq(fdc, 0);
    set_irq(fdc, 0);
    fdc->host.release(fdc->host.context, fdc);
}

void bm_wd37c65_reset(bm_wd37c65_t *fdc)
{
    size_t i;
    if (!fdc) return;
    set_dreq(fdc, 0);
    set_irq(fdc, 0);
    fdc->dor = fdc->dsr = fdc->ccr = fdc->command = 0U;
    memset(fdc->params, 0, sizeof(fdc->params));
    memset(fdc->results, 0, sizeof(fdc->results));
    memset(fdc->specify, 0, sizeof(fdc->specify));
    memset(fdc->pcn, 0, sizeof(fdc->pcn));
    fdc->param_count = fdc->param_expected = 0U;
    fdc->result_index = fdc->result_count = 0U;
    fdc->pending_st0 = fdc->pending_pcn = fdc->reset_sense_remaining = 0U;
    fdc->transfer = TRANSFER_NONE;
    fdc->transfer_index = fdc->transfer_size = 0U;
    fdc->terminal_count = fdc->busy = 0;
    fdc->failure = BM_STATUS_OK;
    for (i = 0U; i < 4U; ++i) bm_floppy_drive_reset(fdc->config.drives[i]);
}

bm_status_t bm_wd37c65_io(void *context, bm_bus_transaction_t *transaction)
{
    bm_wd37c65_t *fdc = context;
    uint8_t offset, value;
    unsigned drive_index;
    bm_floppy_drive_t *drive;
    bm_status_t status = BM_STATUS_OK;
    if (!fdc || !transaction || transaction->space != BM_ADDRESS_IO ||
        transaction->size != 1U || transaction->operation == BM_BUS_FETCH ||
        (transaction->operation != BM_BUS_READ &&
         transaction->operation != BM_BUS_WRITE) ||
        transaction->address < fdc->config.io_base ||
        transaction->address > (uint32_t)fdc->config.io_base + 7U ||
        transaction->wait_states)
        return BM_STATUS_INVALID_ARGUMENT;
    if (fdc->busy) return BM_STATUS_INVALID_STATE;
    if (fdc->failure != BM_STATUS_OK) return fdc->failure;
    if (transaction->attributes & ~(uint32_t)BM_BUS_TRANSACTION_DEBUG)
        return BM_STATUS_INVALID_ARGUMENT;
    if ((transaction->attributes & BM_BUS_TRANSACTION_DEBUG) &&
        transaction->operation == BM_BUS_WRITE) return BM_STATUS_UNSUPPORTED;
    offset = (uint8_t)(transaction->address - fdc->config.io_base);
    if (transaction->operation == BM_BUS_READ) {
        switch (offset) {
        case 1U:
            drive_index = fdc->dor & 3U;
            transaction->value = (uint8_t)(0x70U &
                (uint8_t)~(drive_index ? 0x40U : 0x20U));
            break;
        case 2U: transaction->value = fdc->dor; break;
        case 3U: transaction->value = 0x20U; break;
        case 4U: transaction->value = main_status(fdc); break;
        case 5U:
            transaction->value = (transaction->attributes & BM_BUS_TRANSACTION_DEBUG) ?
                (fdc->result_count ? fdc->results[fdc->result_index] : 0xffU) :
                read_data(fdc);
            break;
        case 7U:
            drive_index = fdc->dor & 3U;
            drive = drive_at(fdc, drive_index);
            value = (!drive || !bm_floppy_drive_media_present(drive) ||
                     bm_floppy_drive_changed(drive)) ? 0x80U : 0U;
            transaction->value = value | 0x7fU;
            break;
        default: transaction->value = 0xffU; break;
        }
        return BM_STATUS_OK;
    }
    value = (uint8_t)transaction->value;
    fdc->busy = 1;
    switch (offset) {
    case 2U: write_dor(fdc, value); break;
    case 4U:
        fdc->dsr = value;
        if (value & 0x80U) {
            write_dor(fdc, (uint8_t)(fdc->dor & 0xfbU));
            write_dor(fdc, (uint8_t)(fdc->dor | 4U));
        }
        break;
    case 5U: status = write_data(fdc, value); break;
    case 7U: fdc->ccr = value & 3U; break;
    default: break;
    }
    fdc->busy = 0;
    return status;
}

bm_status_t bm_wd37c65_dma_read(bm_wd37c65_t *fdc, uint8_t *value)
{
    const bm_floppy_geometry_t *geometry;
    bm_status_t status;
    if (!fdc || !value) return BM_STATUS_INVALID_ARGUMENT;
    if (fdc->failure != BM_STATUS_OK) return fdc->failure;
    if (fdc->busy || fdc->transfer != TRANSFER_READ) return BM_STATUS_INVALID_STATE;
    fdc->busy = 1;
    if (fdc->transfer_index == fdc->transfer_size) {
        geometry = bm_floppy_drive_geometry(drive_at(fdc, fdc->transfer_drive));
        if (!advance_chs(fdc, geometry)) {
            fdc->busy = 0;
            return BM_STATUS_INVALID_STATE;
        }
        status = load_sector(fdc);
        if (status != BM_STATUS_OK) {
            fdc->failure = status;
            set_dreq(fdc, 0);
            fdc->busy = 0;
            return status;
        }
        fdc->transfer_index = 0U;
    }
    *value = fdc->buffer[fdc->transfer_index++];
    if (fdc->transfer_index == fdc->transfer_size && !fdc->terminal_count) {
        geometry = bm_floppy_drive_geometry(drive_at(fdc, fdc->transfer_drive));
        if (!has_next_sector(fdc, geometry))
            finish_rw(fdc, (uint8_t)(fdc->transfer_drive |
                      ((fdc->head & 1U) << 2U)), 0U, 0U);
    }
    status = BM_STATUS_OK;
    fdc->busy = 0;
    return status;
}

bm_status_t bm_wd37c65_dma_write(bm_wd37c65_t *fdc, uint8_t value)
{
    const bm_floppy_geometry_t *geometry;
    bm_status_t status = BM_STATUS_OK;
    if (!fdc) return BM_STATUS_INVALID_ARGUMENT;
    if (fdc->failure != BM_STATUS_OK) return fdc->failure;
    if (fdc->busy || fdc->transfer != TRANSFER_WRITE) return BM_STATUS_INVALID_STATE;
    fdc->busy = 1;
    if (fdc->transfer_index == fdc->transfer_size) {
        geometry = bm_floppy_drive_geometry(drive_at(fdc, fdc->transfer_drive));
        if (!advance_chs(fdc, geometry)) {
            fdc->busy = 0;
            return BM_STATUS_INVALID_STATE;
        }
        fdc->transfer_index = 0U;
    }
    fdc->buffer[fdc->transfer_index++] = value;
    if (fdc->transfer_index == fdc->transfer_size) {
        status = store_sector(fdc);
        if (status != BM_STATUS_OK) {
            fdc->failure = status;
            set_dreq(fdc, 0);
        }
        if (status == BM_STATUS_OK && !fdc->terminal_count) {
            geometry = bm_floppy_drive_geometry(drive_at(fdc, fdc->transfer_drive));
            if (!has_next_sector(fdc, geometry))
                finish_rw(fdc, (uint8_t)(fdc->transfer_drive |
                          ((fdc->head & 1U) << 2U)), 0U, 0U);
        }
    }
    fdc->busy = 0;
    return status;
}

bm_status_t bm_wd37c65_set_terminal_count(bm_wd37c65_t *fdc, int level)
{
    uint8_t st0;
    if (!fdc || (level != 0 && level != 1)) return BM_STATUS_INVALID_ARGUMENT;
    if (fdc->failure != BM_STATUS_OK) return fdc->failure;
    if (fdc->busy) return BM_STATUS_INVALID_STATE;
    fdc->terminal_count = level;
    if (level && fdc->transfer != TRANSFER_NONE) {
        st0 = (uint8_t)(fdc->transfer_drive | ((fdc->head & 1U) << 2U));
        if (fdc->transfer_index == fdc->transfer_size)
            finish_rw(fdc, st0, 0U, 0U);
        else
            finish_rw(fdc, (uint8_t)(st0 | 0x40U), 0x10U, 0U);
    }
    return BM_STATUS_OK;
}

bm_status_t bm_wd37c65_advance(bm_wd37c65_t *fdc, uint64_t cycles)
{
    (void)cycles;
    if (!fdc) return BM_STATUS_INVALID_ARGUMENT;
    return fdc->failure;
}

bm_status_t bm_wd37c65_next_deadline(const bm_wd37c65_t *fdc, uint64_t *cycles)
{
    if (!fdc || !cycles) return BM_STATUS_INVALID_ARGUMENT;
    if (fdc->failure != BM_STATUS_OK) return fdc->failure;
    *cycles = 0U;
    return BM_STATUS_IDLE;
}
