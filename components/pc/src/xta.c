/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2017-2018 Fred N. van Kempen
 * Copyright 2026 rtzor
 * Copyright 2026 BluMach contributors
 *
 * Portable, host-neutral derived rewrite of BluMach's generic XTA controller.
 */
#include <blumach/components/xta.h>

#include "xta_private.h"

#include <string.h>

enum xta_phase {
    XTA_IDLE = 0,
    XTA_RECEIVE_DCB,
    XTA_RECEIVE_DATA,
    XTA_SEND_DATA,
    XTA_DMA_PENDING,
    XTA_COMPLETION
};

enum {
    XTA_CMD_TEST_READY = 0x00,
    XTA_CMD_RECALIBRATE = 0x01,
    XTA_CMD_READ_SENSE = 0x03,
    XTA_CMD_FORMAT_DRIVE = 0x04,
    XTA_CMD_READ_VERIFY = 0x05,
    XTA_CMD_FORMAT_TRACK = 0x06,
    XTA_CMD_READ_SECTORS = 0x08,
    XTA_CMD_WRITE_SECTORS = 0x0a,
    XTA_CMD_SEEK = 0x0b,
    XTA_CMD_SET_DRIVE_PARAMS = 0x0c,
    XTA_CMD_READ_SECTOR_BUFFER = 0x0e,
    XTA_CMD_WRITE_SECTOR_BUFFER = 0x0f,
    XTA_CMD_RAM_DIAGS = 0xe0,
    XTA_CMD_DRIVE_DIAGS = 0xe3,
    XTA_CMD_CTRL_DIAGS = 0xe4
};

#define XTA_STAT_REQ 0x01U
#define XTA_STAT_IO  0x02U
#define XTA_STAT_CD  0x04U
#define XTA_STAT_BSY 0x08U
#define XTA_STAT_DRQ 0x10U
#define XTA_STAT_IRQ 0x20U
#define XTA_STAT_DCB 0x80U
#define XTA_IRQ_ENABLE 0x02U
#define XTA_DMA_ENABLE 0x01U
#define XTA_COMP_DRIVE 0x20U
#define XTA_COMP_ERROR 0x02U
#define XTA_ERR_NONE 0x00U
#define XTA_ERR_WRITE_FAULT 0x03U
#define XTA_ERR_NOT_READY 0x04U
#define XTA_ERR_DATA 0x11U
#define XTA_ERR_NO_SECTOR 0x14U
#define XTA_ERR_SEEK 0x15U
#define XTA_ERR_ILLEGAL_COMMAND 0x20U
#define XTA_ERR_ILLEGAL_ADDRESS 0x21U

static void
xta_set_irq(bm_xta_t *xta, int asserted)
{
    asserted = !!asserted;
    if (xta->interrupt_asserted == asserted)
        return;
    xta->interrupt_asserted = asserted;
    if (xta->irq != NULL)
        xta->irq(xta->irq_context, asserted);
}

static void
xta_complete(bm_xta_t *xta, uint8_t error)
{
    if (error != XTA_ERR_NONE) {
        xta->sense = error;
        xta->completion |= XTA_COMP_ERROR;
    }
    xta->phase = XTA_COMPLETION;
    xta->status = XTA_STAT_REQ | XTA_STAT_CD | XTA_STAT_IO | XTA_STAT_BSY;
    if ((xta->interrupt_mask & XTA_IRQ_ENABLE) != 0U) {
        xta->status |= XTA_STAT_IRQ;
        xta_set_irq(xta, 1);
    }
}

static int
xta_block(const bm_xta_t *xta, uint16_t cylinder, uint8_t head,
          uint8_t sector, uint64_t *block)
{
    if ((cylinder >= xta->active_geometry.cylinders) ||
        (head >= xta->active_geometry.heads) ||
        (sector >= xta->active_geometry.sectors_per_track))
        return 0;
    *block = ((uint64_t) cylinder * xta->active_geometry.heads + head) *
             xta->active_geometry.sectors_per_track + sector;
    return *block < xta->media.block_count;
}

static int
xta_next_sector(bm_xta_t *xta)
{
    if (++xta->sector >= xta->active_geometry.sectors_per_track) {
        xta->sector = 0U;
        if (++xta->head >= xta->active_geometry.heads) {
            xta->head = 0U;
            if (++xta->cylinder >= xta->active_geometry.cylinders)
                return 0;
        }
    }
    return 1;
}

static bm_status_t
xta_media_read(bm_xta_t *xta)
{
    uint64_t block;
    bm_status_t status;
    if (!xta_block(xta, xta->cylinder, xta->head, xta->sector, &block))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_block_media_read(&xta->media, block, 1U, xta->sector_buffer);
    if (status == BM_STATUS_OK)
        ++xta->read_operations;
    return status;
}

static bm_status_t
xta_media_write(bm_xta_t *xta)
{
    uint64_t block;
    bm_status_t status;
    if (!xta_block(xta, xta->cylinder, xta->head, xta->sector, &block))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_block_media_write(&xta->media, block, 1U, xta->sector_buffer);
    if (status == BM_STATUS_OK)
        ++xta->write_operations;
    return status;
}

static uint8_t
xta_media_error(bm_status_t status, int writing)
{
    if (status == BM_STATUS_READ_ONLY)
        return XTA_ERR_WRITE_FAULT;
    if (status == BM_STATUS_INVALID_ARGUMENT)
        return XTA_ERR_ILLEGAL_ADDRESS;
    return writing ? XTA_ERR_WRITE_FAULT : XTA_ERR_DATA;
}

static bm_status_t
xta_dma_sector(bm_xta_t *xta, int device_to_memory)
{
    size_t index;
    bm_status_t status = BM_STATUS_OK;
    int terminal = 0;
    (void) bm_dma8237_set_dreq(xta->dma, xta->dma_channel, 1);
    for (index = 0U; index < sizeof(xta->sector_buffer); ++index) {
        status = device_to_memory ?
            bm_dma8237_device_write(xta->dma, xta->dma_channel,
                                    xta->sector_buffer[index], &terminal) :
            bm_dma8237_device_read(xta->dma, xta->dma_channel,
                                   &xta->sector_buffer[index], &terminal);
        if (status != BM_STATUS_OK)
            break;
        if (terminal && (index + 1U != sizeof(xta->sector_buffer))) {
            status = BM_STATUS_DEVICE_ERROR;
            break;
        }
    }
    (void) bm_dma8237_set_dreq(xta->dma, xta->dma_channel, 0);
    return status;
}

static void xta_prepare_pio_sector(bm_xta_t *xta, int reading);

static void
xta_finish_sector(bm_xta_t *xta, int writing)
{
    bm_status_t status = BM_STATUS_OK;
    if (writing) {
        memcpy(xta->sector_buffer, xta->buffer, sizeof(xta->sector_buffer));
        status = xta_media_write(xta);
    }
    if (status != BM_STATUS_OK) {
        xta_complete(xta, xta_media_error(status, writing));
        return;
    }
    if (--xta->remaining == 0U) {
        xta_complete(xta, XTA_ERR_NONE);
        return;
    }
    if (!xta_next_sector(xta)) {
        xta_complete(xta, XTA_ERR_ILLEGAL_ADDRESS);
        return;
    }
    xta_prepare_pio_sector(xta, !writing);
}

static void
xta_prepare_pio_sector(bm_xta_t *xta, int reading)
{
    bm_status_t status;
    xta->buffer_index = 0U;
    xta->buffer_length = sizeof(xta->buffer);
    if (reading) {
        status = xta_media_read(xta);
        if (status != BM_STATUS_OK) {
            xta_complete(xta, xta_media_error(status, 0));
            return;
        }
        memcpy(xta->buffer, xta->sector_buffer, sizeof(xta->buffer));
        xta->phase = XTA_SEND_DATA;
        xta->status = XTA_STAT_BSY | XTA_STAT_IO | XTA_STAT_REQ;
    } else {
        xta->phase = XTA_RECEIVE_DATA;
        xta->status = XTA_STAT_BSY | XTA_STAT_REQ;
    }
}

static void
xta_transfer_dma(bm_xta_t *xta, int writing)
{
    bm_status_t status = BM_STATUS_OK;
    while (xta->remaining != 0U) {
        if (!writing)
            status = xta_media_read(xta);
        if (status == BM_STATUS_OK)
            status = xta_dma_sector(xta, !writing);
        if (status == BM_STATUS_OK && writing)
            status = xta_media_write(xta);
        if (status != BM_STATUS_OK) {
            xta_complete(xta, xta_media_error(status, writing));
            return;
        }
        --xta->remaining;
        if ((xta->remaining != 0U) && !xta_next_sector(xta)) {
            xta_complete(xta, XTA_ERR_ILLEGAL_ADDRESS);
            return;
        }
    }
    xta_complete(xta, XTA_ERR_NONE);
}

static bm_status_t
xta_begin_dma(bm_xta_t *xta)
{
    bm_status_t status;

    xta->phase = XTA_DMA_PENDING;
    xta->status = XTA_STAT_BSY;
    (void) bm_dma8237_set_dreq(xta->dma, xta->dma_channel, 1);
    if (xta->service_changed == NULL)
        return BM_STATUS_OK;
    status = xta->service_changed(xta->service_context);
    if (status != BM_STATUS_OK) {
        (void) bm_dma8237_set_dreq(xta->dma, xta->dma_channel, 0);
        xta->phase = XTA_IDLE;
        xta->status = 0U;
    }
    return status;
}

static void
xta_format(bm_xta_t *xta, int whole_drive)
{
    uint16_t first_cylinder = whole_drive ? 0U : xta->cylinder;
    uint16_t last_cylinder = whole_drive ? xta->active_geometry.cylinders :
                                           (uint16_t) (xta->cylinder + 1U);
    uint8_t first_head = whole_drive ? 0U : xta->head;
    uint8_t last_head = whole_drive ? xta->active_geometry.heads :
                                      (uint8_t) (xta->head + 1U);
    uint16_t cylinder;
    for (cylinder = first_cylinder; cylinder < last_cylinder; ++cylinder) {
        uint8_t head;
        for (head = first_head; head < last_head; ++head) {
            uint8_t sector;
            for (sector = 0U; sector < xta->active_geometry.sectors_per_track;
                 ++sector) {
                bm_status_t status;
                xta->cylinder = cylinder;
                xta->head = head;
                xta->sector = sector;
                status = xta_media_write(xta);
                if (status != BM_STATUS_OK) {
                    xta_complete(xta, xta_media_error(status, 1));
                    return;
                }
            }
        }
    }
    xta_complete(xta, XTA_ERR_NONE);
}

static bm_status_t
xta_execute(bm_xta_t *xta)
{
    uint8_t command = xta->dcb[0];
    int drive = (xta->dcb[1] >> 5U) & 1U;
    xta->completion = drive ? XTA_COMP_DRIVE : 0U;
    xta->active_command = command;
    xta->head = xta->dcb[1] & 0x1fU;
    xta->sector = xta->dcb[2] & 0x3fU;
    xta->cylinder = (uint16_t) xta->dcb[3] |
                    (uint16_t) ((xta->dcb[2] & 0xc0U) << 2U);
    xta->remaining = xta->dcb[4] == 0U ? 256U : xta->dcb[4];

    if (((drive != 0) || !xta->drive_present) &&
        (command != XTA_CMD_READ_SENSE) &&
        (command != XTA_CMD_RAM_DIAGS) &&
        (command != XTA_CMD_CTRL_DIAGS)) {
        xta_complete(xta, XTA_ERR_NOT_READY);
        return BM_STATUS_OK;
    }
    switch (command) {
        case XTA_CMD_TEST_READY:
            xta_complete(xta, XTA_ERR_NONE);
            break;
        case XTA_CMD_RECALIBRATE:
            xta->cylinder = 0U;
            xta_complete(xta, XTA_ERR_NONE);
            break;
        case XTA_CMD_READ_SENSE:
            xta->buffer[0] = xta->sense;
            xta->buffer[1] = 0U;
            xta->buffer[2] = (uint8_t) ((xta->cylinder >> 2U) |
                                        (xta->sector & 0x3fU));
            xta->buffer[3] = (uint8_t) xta->cylinder;
            xta->sense = XTA_ERR_NONE;
            xta->buffer_index = 0U;
            xta->buffer_length = 4U;
            xta->phase = XTA_SEND_DATA;
            xta->status = XTA_STAT_BSY | XTA_STAT_IO | XTA_STAT_REQ;
            break;
        case XTA_CMD_READ_SECTORS:
            if ((xta->interrupt_mask & XTA_DMA_ENABLE) != 0U) {
                bm_status_t status = xta_begin_dma(xta);
                if (status != BM_STATUS_OK)
                    return status;
            } else
                xta_prepare_pio_sector(xta, 1);
            break;
        case XTA_CMD_WRITE_SECTORS:
            if ((xta->interrupt_mask & XTA_DMA_ENABLE) != 0U) {
                bm_status_t status = xta_begin_dma(xta);
                if (status != BM_STATUS_OK)
                    return status;
            } else
                xta_prepare_pio_sector(xta, 0);
            break;
        case XTA_CMD_READ_VERIFY:
            while (xta->remaining != 0U) {
                bm_status_t status = xta_media_read(xta);
                if (status != BM_STATUS_OK) {
                    xta_complete(xta, xta_media_error(status, 0));
                    return BM_STATUS_OK;
                }
                --xta->remaining;
                if ((xta->remaining != 0U) && !xta_next_sector(xta)) {
                    xta_complete(xta, XTA_ERR_ILLEGAL_ADDRESS);
                    return BM_STATUS_OK;
                }
            }
            xta_complete(xta, XTA_ERR_NONE);
            break;
        case XTA_CMD_SEEK:
            if (xta->cylinder >= xta->active_geometry.cylinders)
                xta_complete(xta, XTA_ERR_SEEK);
            else
                xta_complete(xta, XTA_ERR_NONE);
            break;
        case XTA_CMD_SET_DRIVE_PARAMS:
            xta->buffer_index = 0U;
            xta->buffer_length = 8U;
            xta->phase = XTA_RECEIVE_DATA;
            xta->status = XTA_STAT_BSY | XTA_STAT_REQ;
            break;
        case XTA_CMD_READ_SECTOR_BUFFER:
            memcpy(xta->buffer, xta->sector_buffer, sizeof(xta->buffer));
            xta->buffer_index = 0U;
            xta->buffer_length = sizeof(xta->buffer);
            xta->phase = XTA_SEND_DATA;
            xta->status = XTA_STAT_BSY | XTA_STAT_IO | XTA_STAT_REQ;
            break;
        case XTA_CMD_WRITE_SECTOR_BUFFER:
            xta->buffer_index = 0U;
            xta->buffer_length = sizeof(xta->buffer);
            xta->phase = XTA_RECEIVE_DATA;
            xta->status = XTA_STAT_BSY | XTA_STAT_REQ;
            break;
        case XTA_CMD_FORMAT_DRIVE:
        case XTA_CMD_FORMAT_TRACK:
            xta_format(xta, command == XTA_CMD_FORMAT_DRIVE);
            break;
        case XTA_CMD_RAM_DIAGS:
        case XTA_CMD_DRIVE_DIAGS:
            xta_complete(xta, XTA_ERR_NONE);
            break;
        case XTA_CMD_CTRL_DIAGS:
            xta_complete(xta, XTA_ERR_NONE);
            break;
        default:
            xta_complete(xta, XTA_ERR_ILLEGAL_COMMAND);
            break;
    }
    return BM_STATUS_OK;
}

static void
xta_finish_receive(bm_xta_t *xta)
{
    if (xta->active_command == XTA_CMD_WRITE_SECTORS) {
        xta_finish_sector(xta, 1);
    } else if (xta->active_command == XTA_CMD_SET_DRIVE_PARAMS) {
        bm_xta_geometry_t geometry = {
            (uint16_t) ((uint16_t) xta->buffer[0] << 8U | xta->buffer[1]),
            xta->buffer[2],
            xta->physical_geometry.sectors_per_track
        };
        uint64_t blocks = (uint64_t) geometry.cylinders * geometry.heads *
                          geometry.sectors_per_track;
        if ((geometry.cylinders == 0U) || (geometry.heads == 0U) ||
            (blocks > xta->media.block_count))
            xta_complete(xta, XTA_ERR_ILLEGAL_ADDRESS);
        else {
            xta->active_geometry = geometry;
            xta_complete(xta, XTA_ERR_NONE);
        }
    } else if (xta->active_command == XTA_CMD_WRITE_SECTOR_BUFFER) {
        memcpy(xta->sector_buffer, xta->buffer, sizeof(xta->sector_buffer));
        xta_complete(xta, XTA_ERR_NONE);
    } else {
        xta_complete(xta, XTA_ERR_ILLEGAL_COMMAND);
    }
}

static bm_status_t
xta_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_xta_t *xta = context;
    uint8_t reg;
    uint8_t value = (uint8_t) transaction->value;
    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    reg = (uint8_t) (transaction->address & 3U);
    if (!xta->enabled) {
        if (transaction->operation == BM_BUS_READ)
            transaction->value = 0xffU;
        return BM_STATUS_OK;
    }
    if (transaction->operation == BM_BUS_READ) {
        value = 0xffU;
        if (reg == 0U) {
            if ((xta->status & XTA_STAT_IRQ) != 0U) {
                xta->status = (uint8_t) (xta->status & ~XTA_STAT_IRQ);
                xta_set_irq(xta, 0);
            }
            if (xta->phase == XTA_SEND_DATA) {
                value = xta->buffer[xta->buffer_index++];
                if (xta->buffer_index == xta->buffer_length) {
                    xta->status = (uint8_t) (xta->status & ~XTA_STAT_REQ);
                    if (xta->active_command == XTA_CMD_READ_SECTORS)
                        xta_finish_sector(xta, 0);
                    else
                        xta_complete(xta, XTA_ERR_NONE);
                }
            } else if (xta->phase == XTA_COMPLETION) {
                value = xta->completion;
                xta->phase = XTA_IDLE;
                xta->status = 0U;
            }
        } else if (reg == 1U) {
            value = (uint8_t) (xta->status & ~XTA_STAT_DCB);
        } else if (reg == 2U) {
            value = xta->option_switches;
        }
        transaction->value = value;
        return BM_STATUS_OK;
    }
    if (reg == 0U) {
        if (((xta->phase != XTA_RECEIVE_DCB) &&
             (xta->phase != XTA_RECEIVE_DATA)) ||
            ((xta->status & XTA_STAT_REQ) == 0U) ||
            (xta->buffer_index >= xta->buffer_length))
            return BM_STATUS_OK;
        if (xta->phase == XTA_RECEIVE_DCB)
            xta->dcb[xta->buffer_index] = value;
        else
            xta->buffer[xta->buffer_index] = value;
        if (++xta->buffer_index == xta->buffer_length) {
            bm_status_t status = BM_STATUS_OK;
            xta->status = (uint8_t) (xta->status & ~(XTA_STAT_REQ | XTA_STAT_CD));
            if (xta->phase == XTA_RECEIVE_DCB)
                status = xta_execute(xta);
            else
                xta_finish_receive(xta);
            if (status != BM_STATUS_OK)
                return status;
        }
    } else if (reg == 1U) {
        bm_xta_reset(xta);
    } else if (reg == 2U) {
        xta_set_irq(xta, 0);
        xta->buffer_index = 0U;
        xta->buffer_length = sizeof(xta->dcb);
        xta->phase = XTA_RECEIVE_DCB;
        xta->status = XTA_STAT_BSY | XTA_STAT_CD | XTA_STAT_REQ | XTA_STAT_DCB;
    } else {
        xta->interrupt_mask = value;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_xta_config_validate(const bm_xta_config_t *config)
{
    uint64_t blocks;
    if ((config == NULL) || (config->dma == NULL) || (config->dma_channel >= 4U) ||
        (config->io_base > UINT16_MAX - 3U))
        return BM_STATUS_INVALID_ARGUMENT;
    if (!config->drive_present)
        return BM_STATUS_OK;
    if ((config->geometry.cylinders == 0U) || (config->geometry.heads == 0U) ||
        (config->geometry.sectors_per_track == 0U) ||
        (bm_block_media_validate(&config->media) != BM_STATUS_OK) ||
        (config->media.block_size != sizeof(((bm_xta_t *) 0)->sector_buffer)))
        return BM_STATUS_INVALID_ARGUMENT;
    blocks = (uint64_t) config->geometry.cylinders * config->geometry.heads *
             config->geometry.sectors_per_track;
    return config->media.block_count < blocks ? BM_STATUS_INVALID_ARGUMENT :
                                                BM_STATUS_OK;
}

bm_status_t
bm_xta_create(const bm_host_services_t *host, bm_bus_t *bus,
              const bm_xta_config_t *config, bm_xta_t **out_xta)
{
    bm_xta_t *xta;
    bm_status_t status;
    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (out_xta == NULL) || (bm_xta_config_validate(config) != BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_xta = NULL;
    xta = host->allocate(host->context, sizeof(*xta));
    if (xta == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(xta, 0, sizeof(*xta));
    xta->host = *host;
    xta->dma = config->dma;
    xta->media = config->media;
    xta->physical_geometry = config->geometry;
    xta->active_geometry = config->geometry;
    xta->irq = config->irq;
    xta->irq_context = config->irq_context;
    xta->dma_channel = config->dma_channel;
    xta->option_switches = config->option_switches;
    xta->drive_present = !!config->drive_present;
    status = bm_bus_map(bus, BM_ADDRESS_IO, config->io_base,
                        (uint16_t) (config->io_base + 3U), xta_access, xta);
    if (status != BM_STATUS_OK) {
        bm_xta_destroy(xta);
        return status;
    }
    *out_xta = xta;
    return BM_STATUS_OK;
}

void
bm_xta_destroy(bm_xta_t *xta)
{
    if (xta != NULL) {
        if (xta->service_binding != NULL)
            xta->host.release(xta->host.context, xta->service_binding);
        xta->host.release(xta->host.context, xta);
    }
}

void
bm_xta_reset(bm_xta_t *xta)
{
    if (xta == NULL)
        return;
    xta_set_irq(xta, 0);
    (void) bm_dma8237_set_dreq(xta->dma, xta->dma_channel, 0);
    xta->phase = XTA_IDLE;
    xta->status = 0U;
    xta->sense = XTA_ERR_NONE;
    xta->completion = 0U;
    xta->interrupt_mask = 0U;
    xta->buffer_index = 0U;
    xta->buffer_length = 0U;
    xta->cylinder = 0U;
    xta->head = 0U;
    xta->sector = 0U;
    xta->remaining = 0U;
    xta->active_command = 0U;
    xta->active_geometry = xta->physical_geometry;
    xta->read_operations = 0U;
    xta->write_operations = 0U;
    if (xta->service_changed != NULL)
        (void) xta->service_changed(xta->service_context);
}

void
bm_xta_set_enabled(bm_xta_t *xta, int enabled)
{
    if (xta == NULL)
        return;
    xta->enabled = !!enabled;
    if (!xta->enabled)
        bm_xta_reset(xta);
}

void
bm_xta_service(bm_xta_t *xta)
{
    bm_dma8237_channel_state_t state;
    uint8_t transfer_type;
    int writing;

    if ((xta == NULL) || (xta->phase != XTA_DMA_PENDING))
        return;
    if (bm_dma8237_channel_state(xta->dma, xta->dma_channel, &state) !=
        BM_STATUS_OK)
        return;
    writing = xta->active_command == XTA_CMD_WRITE_SECTORS;
    transfer_type = state.mode & 0x0cU;
    if (state.masked || ((bm_dma8237_command(xta->dma) & 0x04U) != 0U) ||
        ((transfer_type != 0U) &&
         (transfer_type != (writing ? 0x08U : 0x04U))))
        return;
    xta_transfer_dma(xta, writing);
}

int
bm_xta_service_pending(const bm_xta_t *xta)
{
    return (xta != NULL) && (xta->phase == XTA_DMA_PENDING);
}

bm_status_t
bm_xta_state(const bm_xta_t *xta, bm_xta_state_t *state)
{
    if ((xta == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->status = xta->status;
    state->sense = xta->sense;
    state->completion = xta->completion;
    state->interrupt_mask = xta->interrupt_mask;
    state->cylinder = xta->cylinder;
    state->read_operations = xta->read_operations;
    state->write_operations = xta->write_operations;
    state->drive_present = xta->drive_present;
    state->write_protected = xta->media.read_only;
    state->enabled = xta->enabled;
    state->interrupt_asserted = xta->interrupt_asserted;
    return BM_STATUS_OK;
}
