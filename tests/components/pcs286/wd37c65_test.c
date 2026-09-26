/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/at_dma.h>
#include <blumach/components/wd37c65.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

#define SECTOR 512U

typedef struct fixture {
    bm_host_services_t host;
    bm_wd37c65_t *fdc;
    bm_floppy_drive_t *drive;
    bm_at_dma_t *dma;
    uint8_t media[SECTOR * 2U];
    uint8_t ram[0x10000U];
    bm_status_t read_status, write_status;
    int irq, dreq, hrq;
    unsigned irq_edges, dreq_edges, reads, writes;
} fixture_t;

static bm_status_t media_read(void *context, uint64_t block, uint32_t count,
                              uint8_t *destination)
{
    fixture_t *f = context;
    assert(count == 1U && block < 2U);
    ++f->reads;
    if (f->read_status != BM_STATUS_OK) return f->read_status;
    memcpy(destination, f->media + block * SECTOR, SECTOR);
    return BM_STATUS_OK;
}

static bm_status_t media_write(void *context, uint64_t block, uint32_t count,
                               const uint8_t *source)
{
    fixture_t *f = context;
    assert(count == 1U && block < 2U);
    ++f->writes;
    if (f->write_status != BM_STATUS_OK) return f->write_status;
    memcpy(f->media + block * SECTOR, source, SECTOR);
    return BM_STATUS_OK;
}

static void irq_line(void *context, int level)
{
    fixture_t *f = context;
    if (!!level != f->irq) ++f->irq_edges;
    f->irq = !!level;
}

static void dreq_line(void *context, int level)
{
    fixture_t *f = context;
    if (!!level != f->dreq) ++f->dreq_edges;
    f->dreq = !!level;
    if (f->dma) assert(bm_at_dma_set_dreq(f->dma, 2U, level) == BM_STATUS_OK);
}

static void dma_hrq(void *context, int level)
{
    fixture_t *f = context;
    assert(f->hrq != !!level);
    f->hrq = !!level;
}

static bm_status_t dma_memory(void *context, bm_at_transfer_t *transfer)
{
    fixture_t *f = context;
    if (transfer->bus.address >= sizeof(f->ram) || transfer->bus.size != 1U)
        return BM_STATUS_UNMAPPED;
    if (transfer->bus.operation == BM_BUS_READ)
        transfer->bus.value = f->ram[transfer->bus.address];
    else if (transfer->bus.operation == BM_BUS_WRITE)
        f->ram[transfer->bus.address] = (uint8_t)transfer->bus.value;
    else return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t dma_read(void *context, uint16_t *value)
{
    uint8_t byte;
    bm_status_t status = bm_wd37c65_dma_read(context, &byte);
    if (status == BM_STATUS_OK) *value = byte;
    return status;
}

static bm_status_t dma_write(void *context, uint16_t value)
{
    return value <= 0xffU ? bm_wd37c65_dma_write(context, (uint8_t)value) :
                            BM_STATUS_INVALID_ARGUMENT;
}

static void dma_tc(void *context, int level)
{
    assert(bm_wd37c65_set_terminal_count(context, level) == BM_STATUS_OK);
}

static bm_status_t io(fixture_t *f, bm_bus_operation_t operation,
                      uint16_t port, uint8_t *value, uint32_t attributes)
{
    bm_bus_transaction_t t = {
        BM_ADDRESS_IO, operation, port, *value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, attributes
    };
    bm_status_t status = bm_wd37c65_io(f->fdc, &t);
    if (status == BM_STATUS_OK && operation == BM_BUS_READ) *value = (uint8_t)t.value;
    return status;
}

static void wr(fixture_t *f, uint16_t port, uint8_t value)
{
    assert(io(f, BM_BUS_WRITE, port, &value, 0U) == BM_STATUS_OK);
}

static uint8_t rd(fixture_t *f, uint16_t port)
{
    uint8_t value = 0U;
    assert(io(f, BM_BUS_READ, port, &value, 0U) == BM_STATUS_OK);
    return value;
}

static void command(fixture_t *f, uint8_t command_byte,
                    const uint8_t *params, size_t count)
{
    size_t i;
    wr(f, 0x3f5U, command_byte);
    for (i = 0U; i < count; ++i) wr(f, 0x3f5U, params[i]);
}

static void create(fixture_t *f, int with_drive)
{
    bm_wd37c65_config_t config;
    bm_floppy_drive_config_t drive;
    memset(f, 0, sizeof(*f));
    f->host = bm_null_host_services();
    f->read_status = f->write_status = BM_STATUS_OK;
    memset(&config, 0, sizeof(config));
    config.io_base = 0x3f0U;
    config.clock = (bm_clock_rate_t){ 8000000U, 1U };
    config.irq = irq_line;
    config.dreq = dreq_line;
    config.output_context = f;
    if (with_drive) {
        memset(&drive, 0, sizeof(drive));
        drive.installed = drive.media_present = 1;
        drive.geometry = (bm_floppy_geometry_t){ 1U, 1U, 2U, SECTOR };
        drive.media = (bm_block_media_t){ f, 2U, SECTOR, 0, media_read, media_write };
        assert(bm_floppy_drive_create(&f->host, &drive, &f->drive) == BM_STATUS_OK);
        config.drives[0] = f->drive;
    }
    assert(bm_wd37c65_create(&f->host, &config, &f->fdc) == BM_STATUS_OK);
}

static void destroy(fixture_t *f)
{
    bm_wd37c65_destroy(f->fdc);
    bm_at_dma_destroy(f->dma);
    bm_floppy_drive_destroy(f->drive);
}

static void dma_wr(fixture_t *f, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t t = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };
    assert(bm_at_dma_io(f->dma, &t) == BM_STATUS_OK);
}

static void attach_dma(fixture_t *f)
{
    bm_at_dma_config_t config;
    memset(&config, 0, sizeof(config));
    config.memory = dma_memory;
    config.memory_context = f;
    config.bus_request = dma_hrq;
    config.bus_context = f;
    config.clock = (bm_clock_rate_t){ 4000000U, 1U };
    config.endpoints[2] = (bm_at_dma_endpoint_t){
        f->fdc, dma_read, dma_write, NULL, dma_tc
    };
    assert(bm_at_dma_create(&f->host, &config, &f->dma) == BM_STATUS_OK);
}

static void program_dma_read(fixture_t *f, uint16_t address, uint16_t count)
{
    dma_wr(f, 0x0cU, 0U);
    dma_wr(f, 0x04U, (uint8_t)address);
    dma_wr(f, 0x04U, (uint8_t)(address >> 8U));
    dma_wr(f, 0x05U, (uint8_t)count);
    dma_wr(f, 0x05U, (uint8_t)(count >> 8U));
    dma_wr(f, 0x0bU, 0x86U); /* Channel 2, block, device-to-memory. */
    dma_wr(f, 0x0aU, 0x02U);
    dma_wr(f, 0xd6U, 0xc0U); /* Upper channel 4 cascade. */
    dma_wr(f, 0xd4U, 0U);
    dma_wr(f, 0x81U, 0U);
}

static void reset_and_commands(void)
{
    fixture_t f;
    uint8_t params[8] = { 0U, 0U, 0U, 1U, 2U, 1U, 0x1bU, 0xffU };
    unsigned i;
    create(&f, 0);
    assert(rd(&f, 0x3f4U) == 0x80U);
    wr(&f, 0x3f2U, 0U);
    wr(&f, 0x3f2U, 0x0cU);
    assert(f.irq && f.irq_edges == 1U && rd(&f, 0x3f4U) == 0x80U);
    for (i = 0U; i < 4U; ++i) {
        command(&f, 0x08U, NULL, 0U);
        assert(rd(&f, 0x3f4U) == 0xd0U);
        assert(rd(&f, 0x3f5U) == (uint8_t)(0xc0U | i));
        assert(rd(&f, 0x3f5U) == 0U);
    }
    assert(!f.irq && rd(&f, 0x3f4U) == 0x80U);
    wr(&f, 0x3f5U, 0x04U);
    assert(rd(&f, 0x3f4U) == 0x90U);
    wr(&f, 0x3f5U, 0U);
    assert(rd(&f, 0x3f5U) == 0U);
    command(&f, 0x46U, params, 8U);
    assert(!f.dreq && f.irq);
    assert(rd(&f, 0x3f5U) == 0x48U);
    assert(rd(&f, 0x3f5U) == 0x04U);
    for (i = 0U; i < 5U; ++i) (void)rd(&f, 0x3f5U);
    assert(!f.irq && rd(&f, 0x3f4U) == 0x80U);
    {
        uint8_t value = 0U;
        assert(io(&f, BM_BUS_READ, 0x3f4U, &value,
                  BM_BUS_TRANSACTION_DEBUG) == BM_STATUS_OK && value == 0x80U);
        assert(io(&f, BM_BUS_WRITE, 0x3f4U, &value,
                  BM_BUS_TRANSACTION_DEBUG) == BM_STATUS_UNSUPPORTED);
    }
    destroy(&f);
}

static void dma_media_and_failures(void)
{
    fixture_t f;
    uint8_t params[8] = { 0U, 0U, 0U, 1U, 2U, 1U, 0x1bU, 0xffU };
    uint8_t value;
    size_t i;
    create(&f, 1);
    for (i = 0U; i < SECTOR; ++i) f.media[i] = (uint8_t)(i ^ 0x5aU);
    wr(&f, 0x3f2U, 0x1cU);
    for (i = 0U; i < 4U; ++i) {
        command(&f, 0x08U, NULL, 0U);
        (void)rd(&f, 0x3f5U);
        (void)rd(&f, 0x3f5U);
    }
    command(&f, 0x03U, (const uint8_t[]){ 0xdfU, 0x02U }, 2U);
    command(&f, 0x46U, params, 8U);
    assert(f.dreq && !f.irq && f.reads == 1U && rd(&f, 0x3f4U) == 0x10U);
    for (i = 0U; i < SECTOR; ++i) {
        assert(bm_wd37c65_dma_read(f.fdc, &value) == BM_STATUS_OK);
        assert(value == (uint8_t)(i ^ 0x5aU));
    }
    assert(!f.dreq && f.irq && rd(&f, 0x3f4U) == 0xd0U);
    for (i = 0U; i < 7U; ++i) (void)rd(&f, 0x3f5U);

    params[0] = 0U;
    command(&f, 0x45U, params, 8U);
    assert(f.dreq);
    for (i = 0U; i < SECTOR; ++i)
        assert(bm_wd37c65_dma_write(f.fdc, (uint8_t)(i ^ 0xa5U)) == BM_STATUS_OK);
    assert(f.writes == 1U && !f.dreq && f.irq);
    for (i = 0U; i < SECTOR; ++i) assert(f.media[i] == (uint8_t)(i ^ 0xa5U));
    for (i = 0U; i < 7U; ++i) (void)rd(&f, 0x3f5U);

    command(&f, 0x46U, params, 8U);
    for (i = 0U; i < 10U; ++i)
        assert(bm_wd37c65_dma_read(f.fdc, &value) == BM_STATUS_OK);
    assert(bm_wd37c65_set_terminal_count(f.fdc, 1) == BM_STATUS_OK);
    assert(!f.dreq && f.irq);
    assert(rd(&f, 0x3f5U) == 0x40U);
    assert(rd(&f, 0x3f5U) == 0x10U);
    for (i = 0U; i < 5U; ++i) (void)rd(&f, 0x3f5U);
    assert(bm_wd37c65_set_terminal_count(f.fdc, 0) == BM_STATUS_OK);

    f.read_status = BM_STATUS_DEVICE_ERROR;
    command(&f, 0x46U, params, 8U - 1U);
    value = params[7];
    assert(io(&f, BM_BUS_WRITE, 0x3f5U, &value, 0U) == BM_STATUS_DEVICE_ERROR);
    assert(!f.dreq && !f.irq);
    assert(io(&f, BM_BUS_READ, 0x3f4U, &value, 0U) == BM_STATUS_DEVICE_ERROR);
    bm_wd37c65_reset(f.fdc);
    assert(rd(&f, 0x3f4U) == 0x80U);
    destroy(&f);
}

static void at_dma_endpoint_roundtrip(void)
{
    fixture_t f;
    uint8_t params[8] = { 0U, 0U, 0U, 1U, 2U, 1U, 0x1bU, 0xffU };
    size_t i;
    uint64_t clocks;
    create(&f, 1);
    attach_dma(&f);
    for (i = 0U; i < SECTOR; ++i) f.media[i] = (uint8_t)(i ^ 0x3cU);
    wr(&f, 0x3f2U, 0x1cU);
    for (i = 0U; i < 4U; ++i) {
        command(&f, 0x08U, NULL, 0U);
        (void)rd(&f, 0x3f5U);
        (void)rd(&f, 0x3f5U);
    }
    command(&f, 0x03U, (const uint8_t[]){ 0xdfU, 0x02U }, 2U);
    program_dma_read(&f, 0x2000U, SECTOR - 1U);
    command(&f, 0x46U, params, 8U);
    assert(f.dreq && f.hrq);
    assert(bm_at_dma_set_bus_grant(f.dma, 1) == BM_STATUS_OK);
    for (i = 0U; i < SECTOR; ++i) {
        clocks = 0U;
        assert(bm_at_dma_service(f.dma, &clocks) == BM_STATUS_OK);
        assert(clocks >= 3U);
    }
    assert(!f.dreq && !f.hrq);
    assert(bm_at_dma_set_bus_grant(f.dma, 0) == BM_STATUS_OK);
    for (i = 0U; i < SECTOR; ++i)
        assert(f.ram[0x2000U + i] == (uint8_t)(i ^ 0x3cU));
    assert(f.irq && rd(&f, 0x3f4U) == 0xd0U);
    destroy(&f);
}

int main(void)
{
    reset_and_commands();
    dma_media_and_failures();
    at_dma_endpoint_roundtrip();
    return 0;
}
