/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/pvga1a.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static bm_status_t
io_write(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
io_read(bm_bus_t *bus, uint16_t port, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

static bm_status_t
io_write_word(bm_bus_t *bus, uint16_t port, uint16_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 2, 2, 0,
        BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
io_read_word(bm_bus_t *bus, uint16_t port, uint16_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 2, 2, 0,
        BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint16_t) transaction.value;
    return status;
}

static bm_status_t
memory_write(bm_bus_t *bus, uint32_t address, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_MEMORY, BM_BUS_WRITE, address, value, 1, 1, 0,
        BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
crtc_write(bm_bus_t *bus, uint8_t index, uint8_t value)
{
    bm_status_t status = io_write(bus, 0x03d4U, index);
    return status == BM_STATUS_OK ? io_write(bus, 0x03d5U, value) : status;
}

static bm_status_t
sequencer_write(bm_bus_t *bus, uint8_t index, uint8_t value)
{
    bm_status_t status = io_write(bus, 0x03c4U, index);
    return status == BM_STATUS_OK ? io_write(bus, 0x03c5U, value) : status;
}

static bm_status_t
graphics_write(bm_bus_t *bus, uint8_t index, uint8_t value)
{
    bm_status_t status = io_write(bus, 0x03ceU, index);
    return status == BM_STATUS_OK ? io_write(bus, 0x03cfU, value) : status;
}

static bm_status_t
attribute_write(bm_bus_t *bus, uint8_t index, uint8_t value)
{
    uint8_t ignored;
    bm_status_t status = io_read(bus, 0x03daU, &ignored);
    if (status == BM_STATUS_OK)
        status = io_write(bus, 0x03c0U, index);
    return status == BM_STATUS_OK ? io_write(bus, 0x03c0U, value) : status;
}

static bm_status_t
attribute_enable(bm_bus_t *bus)
{
    uint8_t ignored;
    bm_status_t status = io_read(bus, 0x03daU, &ignored);
    return status == BM_STATUS_OK ? io_write(bus, 0x03c0U, 0x20U) : status;
}

static void
dac_write(bm_bus_t *bus, uint8_t index, uint8_t red, uint8_t green,
          uint8_t blue)
{
    assert(io_write(bus, 0x03c8U, index) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, red) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, green) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, blue) == BM_STATUS_OK);
}

static void
diagnostic_status_test(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pvga1a_t *video = NULL;
    bm_pvga1a_config_t config = { BM_PVGA1A_VRAM_SIZE };
    uint8_t value;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pvga1a_create(&host, bus, &config, &video) == BM_STATUS_OK);
    /* A small authored planar raster makes the source of the diagnostic
     * outputs explicit: the first eight active pixels are colour 0fh and the
     * following blanking interval has no attribute-palette output. */
    assert(sequencer_write(bus, 1U, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0U, 0U) == BM_STATUS_OK);     /* 40 dots/line. */
    assert(crtc_write(bus, 1U, 1U) == BM_STATUS_OK);     /* 16 active dots. */
    assert(crtc_write(bus, 6U, 2U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x12U, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x13U, 1U) == BM_STATUS_OK);
    assert(memory_write(bus, 0xa0000U, 0xffU) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x10U, 1U) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x12U, 0x0fU) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x0fU, 0x05U) == BM_STATUS_OK);
    assert(attribute_enable(bus) == BM_STATUS_OK);
    assert(bm_pvga1a_advance_ns(video, 0U) == BM_STATUS_OK);

    /* Attribute Controller register 12h selects two of the eight palette
     * outputs for Input Status 1 bits 4 and 5. */
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK &&
           (value & 0x30U) == 0x30U);                   /* P0 and P2. */
    assert(attribute_write(bus, 0x12U, 0x1fU) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x0fU, 0x30U) == BM_STATUS_OK);
    assert(attribute_enable(bus) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK &&
           (value & 0x30U) == 0x30U);                   /* P4 and P5. */
    assert(attribute_write(bus, 0x12U, 0x2fU) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x0fU, 0x0aU) == BM_STATUS_OK);
    assert(attribute_enable(bus) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK &&
           (value & 0x30U) == 0x30U);                   /* P1 and P3. */
    assert(attribute_write(bus, 0x12U, 0x3fU) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x0fU, 0U) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x14U, 0x0cU) == BM_STATUS_OK);
    assert(attribute_enable(bus) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK &&
           (value & 0x30U) == 0x30U);                   /* P6 and P7. */

    assert(bm_pvga1a_advance_ns(video, 636U) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK &&
           (value & 0x31U) == 0x01U);                   /* Horizontal blank. */
    bm_bus_destroy(bus);
    bm_pvga1a_destroy(video);
}

static void
dac_post_roundtrip_test(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pvga1a_t *video = NULL;
    bm_pvga1a_config_t config = { BM_PVGA1A_VRAM_SIZE };
    const uint8_t patterns[] = { 0x2aU, 0x15U, 0U };
    uint8_t value;

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pvga1a_create(&host, bus, &config, &video) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c6U, 0U) == BM_STATUS_OK);
    /* The VGA monitor-sense comparator observes DAC entry zero even while the
     * pixel mask is zero. The PCS 286 VGA BIOS qualifies the dark level and
     * then each primary at 10h before accepting the attached display. */
    assert(io_read(bus, 0x03c2U, &value) == BM_STATUS_OK &&
           (value & 0x10U) == 0x10U);
    dac_write(bus, 0U, 0x04U, 0x04U, 0x04U);
    assert(io_read(bus, 0x03c2U, &value) == BM_STATUS_OK &&
           (value & 0x10U) == 0x10U);
    dac_write(bus, 0U, 0x10U, 0x04U, 0x04U);
    assert(io_read(bus, 0x03c2U, &value) == BM_STATUS_OK &&
           (value & 0x10U) == 0U);
    dac_write(bus, 0U, 0x04U, 0x10U, 0x04U);
    assert(io_read(bus, 0x03c2U, &value) == BM_STATUS_OK &&
           (value & 0x10U) == 0U);
    dac_write(bus, 0U, 0x04U, 0x04U, 0x10U);
    assert(io_read(bus, 0x03c2U, &value) == BM_STATUS_OK &&
           (value & 0x10U) == 0U);
    for (unsigned int pattern = 0U;
         pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern) {
        assert(io_write(bus, 0x03c8U, 0U) == BM_STATUS_OK);
        for (unsigned int component = 0U; component < 768U; ++component)
            assert(io_write(bus, 0x03c9U, patterns[pattern]) == BM_STATUS_OK);
        if (patterns[pattern] == 0U)
            continue;
        assert(io_write(bus, 0x03c7U, 0U) == BM_STATUS_OK);
        for (unsigned int component = 0U; component < 768U; ++component)
            assert(io_read(bus, 0x03c9U, &value) == BM_STATUS_OK &&
                   value == patterns[pattern]);
    }
    bm_bus_destroy(bus);
    bm_pvga1a_destroy(video);
}

static void clocked_status_test(void)
{
    bm_host_services_t host=bm_null_host_services();
    bm_bus_t *bus[2]={NULL,NULL};
    bm_pvga1a_t *v[2]={NULL,NULL};
    bm_pvga1a_config_t config={BM_PVGA1A_VRAM_SIZE};
    uint8_t a,b;
    bm_bus_transaction_t t={BM_ADDRESS_IO,BM_BUS_READ,0x3da,0,1,1,0,
                            BM_ENDIAN_LITTLE,BM_BUS_TRANSACTION_DEBUG};
    for (unsigned i=0;i<2;++i) {
        assert(bm_bus_create(&host,2,&bus[i])==BM_STATUS_OK);
        assert(bm_pvga1a_create(&host,bus[i],&config,&v[i])==BM_STATUS_OK);
        /* 40 dots per line, four lines, 16 active dots/two active lines.
         * Vertical retrace spans only line two. */
        assert(sequencer_write(bus[i],1,1)==BM_STATUS_OK);
        assert(crtc_write(bus[i],0,0)==BM_STATUS_OK);
        assert(crtc_write(bus[i],1,1)==BM_STATUS_OK);
        assert(crtc_write(bus[i],6,2)==BM_STATUS_OK);
        assert(crtc_write(bus[i],0x12,1)==BM_STATUS_OK);
        assert(crtc_write(bus[i],0x10,2)==BM_STATUS_OK);
        assert(crtc_write(bus[i],0x11,3)==BM_STATUS_OK);
        assert(bm_pvga1a_advance_ns(v[i],0)==BM_STATUS_OK);
        for (unsigned n=0;n<100;++n)
            assert(io_read(bus[i],0x3da,&a)==BM_STATUS_OK && a==0);
        /* Sequencer screen-off blanks pixels, but the raster/display-enable
         * diagnostic signal continues. PCS 286 POST samples it while blanked;
         * this matches the inherited SVGA timer rather than a BIOS special. */
        assert(sequencer_write(bus[i],1,0x21)==BM_STATUS_OK);
        assert(io_read(bus[i],0x3da,&a)==BM_STATUS_OK && a==0);
        assert(sequencer_write(bus[i],1,1)==BM_STATUS_OK);
    }
    assert(bm_pvga1a_advance_ns(NULL,0)==BM_STATUS_INVALID_ARGUMENT);
    /* ceil(16 dots / 25.175 MHz): horizontal inactive, no vertical retrace. */
    assert(bm_pvga1a_advance_ns(v[0],636)==BM_STATUS_OK);
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==1);
    /* ceil(80 dots / clock): vertical retrace. Partitioning and polling have
     * no effect on phase, including sub-dot calls. */
    assert(bm_pvga1a_advance_ns(v[0],2542)==BM_STATUS_OK);
    for (unsigned n=0;n<3178;++n) {
        assert(bm_pvga1a_advance_ns(v[1],1)==BM_STATUS_OK);
        assert(io_read(bus[1],0x3da,&b)==BM_STATUS_OK);
    }
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==9 && a==b);
    assert(io_read(bus[0],0x3ba,&a)==BM_STATUS_OK && a==0xff);
    assert(io_write(bus[0],0x3c2,0)==BM_STATUS_OK);
    assert(io_read(bus[0],0x3ba,&a)==BM_STATUS_OK && a==9);
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==0xff);
    assert(io_write(bus[0],0x3c2,9)==BM_STATUS_OK);
    assert(bm_pvga1a_advance_ns(v[0],UINT64_MAX)==BM_STATUS_UNSUPPORTED);
    assert(io_write(bus[0],0x3c2,1)==BM_STATUS_OK);
    assert(bm_pvga1a_advance_ns(v[0],UINT64_MAX)==BM_STATUS_OK);
    assert(bm_pvga1a_advance_ns(v[1],UINT64_MAX/2)==BM_STATUS_OK);
    assert(bm_pvga1a_advance_ns(v[1],UINT64_MAX-UINT64_MAX/2)==BM_STATUS_OK);
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK);
    assert(io_read(bus[1],0x3da,&b)==BM_STATUS_OK && a==b);
    for (unsigned n=0;n<200;++n) {
        assert(bm_pvga1a_advance_ns(v[0],39)==BM_STATUS_OK);
        assert(bm_pvga1a_advance_ns(v[1],39)==BM_STATUS_OK);
        assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK);
        assert(io_read(bus[1],0x3da,&b)==BM_STATUS_OK && a==b);
    }
    /* DEBUG status cannot reset the attribute data/index flip-flop. */
    assert(io_write(bus[0],0x3c0,0x12)==BM_STATUS_OK);
    assert(bm_bus_transact(bus[0],&t)==BM_STATUS_OK);
    assert(io_write(bus[0],0x3c0,0x34)==BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(v[0],BM_PVGA1A_ATTRIBUTE,0x12,&a)==BM_STATUS_OK && a==0x34);
    dac_write(bus[0],2,11,22,33);
    assert(io_write(bus[0],0x3c7,2)==BM_STATUS_OK);
    t.address=0x3c9;
    for (unsigned n=0;n<3;++n)
        assert(bm_bus_transact(bus[0],&t)==BM_STATUS_OK && t.value==11);
    assert(io_read(bus[0],0x3c9,&a)==BM_STATUS_OK && a==11);
    t.operation=BM_BUS_WRITE;
    assert(bm_bus_transact(bus[0],&t)==BM_STATUS_UNSUPPORTED);
    assert(io_read(bus[0],0x3c9,&a)==BM_STATUS_OK && a==22);
    /* DEBUG VRAM reads must not change the four write-mode-1 latches. */
    assert(memory_write(bus[0],0xa0000,0x55)==BM_STATUS_OK);
    assert(memory_write(bus[0],0xa0001,0xaa)==BM_STATUS_OK);
    t=(bm_bus_transaction_t){BM_ADDRESS_MEMORY,BM_BUS_READ,0xa0000,0,1,1,0,BM_ENDIAN_LITTLE,0};
    assert(bm_bus_transact(bus[0],&t)==BM_STATUS_OK && t.value==0x55);
    t.address=0xa0001; t.attributes=BM_BUS_TRANSACTION_DEBUG;
    assert(bm_bus_transact(bus[0],&t)==BM_STATUS_OK && t.value==0xaa);
    t.operation=BM_BUS_WRITE; t.value=0xff;
    assert(bm_bus_transact(bus[0],&t)==BM_STATUS_UNSUPPORTED);
    assert(graphics_write(bus[0],5,1)==BM_STATUS_OK);
    assert(memory_write(bus[0],0xa0002,0)==BM_STATUS_OK);
    for (unsigned plane=0;plane<4;++plane) {
        assert(bm_pvga1a_inspect_vram(v[0],plane,2,&a)==BM_STATUS_OK && a==0x55);
        assert(bm_pvga1a_inspect_vram(v[0],plane,1,&a)==BM_STATUS_OK && a==0xaa);
    }
    /* Both internal clocks, nine-dot and doubled characters, and overflow
     * bits in VT/VDE/VRS use the same geometry as the existing renderer. */
    for (unsigned clock=0;clock<2;++clock) {
        uint64_t rate=clock ? 28322000U : 25175000U;
        uint64_t edge=(UINT64_C(90)*514U*1000000000U+rate-1U)/rate;
        bm_pvga1a_reset(v[0]);
        assert(io_write(bus[0],0x3c2,(uint8_t)(1U+4U*clock))==BM_STATUS_OK);
        assert(sequencer_write(bus[0],1,8)==BM_STATUS_OK);
        assert(crtc_write(bus[0],0,0)==BM_STATUS_OK);
        assert(crtc_write(bus[0],1,4)==BM_STATUS_OK);
        assert(crtc_write(bus[0],6,4)==BM_STATUS_OK);
        assert(crtc_write(bus[0],7,0xe0)==BM_STATUS_OK); /* VT=516+2,VDE=513+1,VRS=514. */
        assert(crtc_write(bus[0],0x12,1)==BM_STATUS_OK);
        assert(crtc_write(bus[0],0x10,2)==BM_STATUS_OK);
        assert(crtc_write(bus[0],0x11,3)==BM_STATUS_OK);
        assert(bm_pvga1a_advance_ns(v[0],edge-1U)==BM_STATUS_OK);
        assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==0);
        assert(bm_pvga1a_advance_ns(v[0],1)==BM_STATUS_OK);
        assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==9);
    }
    /* Reset keeps the pre-existing PCS86 status contract until explicit opt-in. */
    bm_pvga1a_reset(v[0]);
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==9);
    assert(io_read(bus[0],0x3da,&a)==BM_STATUS_OK && a==0);
    for (unsigned i=0;i<2;++i) { bm_bus_destroy(bus[i]); bm_pvga1a_destroy(v[i]); }
}

int
main(void)
{
    clocked_status_test();
    diagnostic_status_test();
    dac_post_roundtrip_test();
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pvga1a_t *video = NULL;
    bm_pvga1a_config_t config = { BM_PVGA1A_VRAM_SIZE };
    uint8_t value = 0;
    uint16_t word = 0;
    unsigned int plane;
    bm_video_geometry_t geometry;
    uint32_t pixels[64];
    bm_video_framebuffer_t framebuffer = {
        pixels, 64U, 16U, { 0, 0, BM_PIXEL_XRGB8888, 0U, 0U }
    };
    uint32_t graphics_pixels[16];
    bm_video_framebuffer_t graphics_framebuffer = {
        graphics_pixels, 16U, 8U,
        { 0, 0, BM_PIXEL_XRGB8888, 0U, 0U }
    };

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pvga1a_create(&host, bus, &config, &video) == BM_STATUS_OK);

    /* V30 word I/O to an indexed VGA pair writes the low byte to the index
     * port and the high byte to the adjacent data port. Reads preserve the
     * same ascending-port, little-endian order. */
    assert(io_write_word(bus, 0x03d4U, 0x5a0eU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_CRTC, 0x0eU,
                                      &value) == BM_STATUS_OK && value == 0x5aU);
    assert(io_read_word(bus, 0x03d4U, &word) == BM_STATUS_OK);
    assert(word == 0x5a0eU);

    /* Paradise extended graphics registers remain locked until 0Fh = 05h. */
    assert(io_write(bus, 0x03ceU, 0x09U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x5aU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_GRAPHICS, 9,
                                      &value) == BM_STATUS_OK && value == 0);
    assert(io_write(bus, 0x03ceU, 0x0fU) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x05U) == BM_STATUS_OK);
    assert(io_read(bus, 0x03cfU, &value) == BM_STATUS_OK && value == 0x85U);
    assert(io_write(bus, 0x03ceU, 0x09U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0x5aU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_GRAPHICS, 9,
                                      &value) == BM_STATUS_OK && value == 0x5aU);

    /* Planar write mode 0 writes every enabled plane through the bit mask. */
    assert(io_write(bus, 0x03c4U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 0x0fU) == BM_STATUS_OK);
    assert(io_write(bus, 0x03ceU, 8U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03cfU, 0xffU) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0123U, 0xa5U) == BM_STATUS_OK);
    for (plane = 0; plane < 4U; ++plane)
        assert(bm_pvga1a_inspect_vram(video, plane, 0x0123U,
                                      &value) == BM_STATUS_OK && value == 0xa5U);

    /* Sequencer map-mask selection keeps the remaining planes unchanged. */
    assert(io_write(bus, 0x03c5U, 0x04U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0123U, 0x3cU) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_vram(video, 2, 0x0123U,
                                  &value) == BM_STATUS_OK && value == 0x3cU);
    assert(bm_pvga1a_inspect_vram(video, 1, 0x0123U,
                                  &value) == BM_STATUS_OK && value == 0xa5U);

    /* Input status is deterministic, changes phase and resets attribute FF. */
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK && value == 0x09U);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK && value == 0U);
    assert(io_write(bus, 0x03c0U, 0x12U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0x34U) == BM_STATUS_OK);
    assert(bm_pvga1a_inspect_register(video, BM_PVGA1A_ATTRIBUTE, 0x12U,
                                      &value) == BM_STATUS_OK && value == 0x34U);

    /* A real text cell is rasterized from planes 0/1 and its plane-2 glyph. */
    assert(io_write(bus, 0x03c4U, 1U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 1U) == BM_STATUS_OK); /* Eight-dot characters. */
    assert(io_write(bus, 0x03c4U, 3U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 0U) == BM_STATUS_OK); /* Font map A at zero. */
    assert(io_write(bus, 0x03d4U, 1U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d5U, 0U) == BM_STATUS_OK); /* One column. */
    assert(io_write(bus, 0x03d4U, 9U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d5U, 1U) == BM_STATUS_OK); /* Two scanlines. */
    assert(io_write(bus, 0x03d4U, 0x12U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d5U, 1U) == BM_STATUS_OK); /* Two visible lines. */
    assert(io_write(bus, 0x03d4U, 0x13U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d5U, 1U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d4U, 0x17U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03d5U, 0x80U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0aU, 0x20U) == BM_STATUS_OK); /* Cursor disabled. */

    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0U) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 2U) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0x20U) == BM_STATUS_OK); /* Display enabled. */

    assert(io_write(bus, 0x03c8U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c8U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 63U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 0U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, 0U) == BM_STATUS_OK);

    assert(io_write(bus, 0x03c4U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 1U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0000U, 1U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 2U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0000U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 4U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0020U, 0x80U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0021U, 0x40U) == BM_STATUS_OK);

    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 8U && geometry.height == 2U &&
           geometry.format == BM_PIXEL_XRGB8888);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(framebuffer.geometry.width == 8U && framebuffer.geometry.height == 2U);
    assert(pixels[0] == 0x00ff0000U && pixels[1] == 0U);
    assert(pixels[16] == 0U && pixels[17] == 0x00ff0000U);

    /* Cursor shape uses 0Ah/0Bh, and a second render can change solely from
     * deterministic emulated time: VGA cursor blink is VSYNC/16. */
    assert(crtc_write(bus, 0U, 0x5fU) == BM_STATUS_OK); /* 100 character clocks. */
    assert(crtc_write(bus, 6U, 0xbfU) == BM_STATUS_OK);
    assert(crtc_write(bus, 7U, 0x01U) == BM_STATUS_OK); /* 449 total lines. */
    assert(io_write(bus, 0x03c2U, 0x05U) == BM_STATUS_OK); /* 28.322 MHz. */
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.refresh_numerator == UINT64_C(28322000));
    assert(geometry.refresh_denominator == UINT64_C(359200));
    assert(io_write(bus, 0x03c2U, 0x0dU) == BM_STATUS_OK); /* Board VCLK3. */
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.refresh_numerator == 0U &&
           geometry.refresh_denominator == 0U);
    assert(io_write(bus, 0x03c2U, 0x05U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0aU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0bU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0eU, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0fU, 0U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[16] == 0x00ff0000U && pixels[23] == 0x00ff0000U);
    assert(bm_pvga1a_render(video, UINT64_C(120000), UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[16] == 0U && pixels[17] == 0x00ff0000U && pixels[23] == 0U);

    /* Disable and invalid (start > end) forms do not fabricate a cursor. */
    assert(crtc_write(bus, 0x0aU, 0x21U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[16] == 0U && pixels[17] == 0x00ff0000U);
    assert(crtc_write(bus, 0x0aU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0bU, 0U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[16] == 0U && pixels[17] == 0x00ff0000U);

    /* Position, CRTC skew, display start and offset all use the same character
     * address space as the text fetcher. */
    assert(io_write(bus, 0x03c4U, 2U) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c5U, 2U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0001U, 2U) == BM_STATUS_OK);
    assert(crtc_write(bus, 1U, 1U) == BM_STATUS_OK); /* Two columns. */
    assert(crtc_write(bus, 0x0aU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0bU, 0x21U) == BM_STATUS_OK); /* End 1, skew 1. */
    assert(crtc_write(bus, 0x0eU, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0fU, 0U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(framebuffer.geometry.width == 16U);
    assert(pixels[24] == 0x00ff0000U && pixels[31] == 0x00ff0000U);
    assert(crtc_write(bus, 0x0bU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0fU, 2U) == BM_STATUS_OK); /* Outside visible cells. */
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[24] == 0U && pixels[31] == 0U);
    assert(crtc_write(bus, 0x0cU, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0dU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0fU, 1U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(pixels[16] == 0x00ff0000U && pixels[23] == 0x00ff0000U);

    /* Taller cells clip the inclusive cursor shape to their visible scanlines. */
    assert(crtc_write(bus, 1U, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 9U, 3U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x12U, 3U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0aU, 2U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0bU, 3U) == BM_STATUS_OK);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &framebuffer) == BM_STATUS_OK);
    assert(framebuffer.geometry.height == 4U);
    assert(pixels[0] == 0U && pixels[16] == 0U);
    assert(pixels[32] == 0x00ff0000U && pixels[39] == 0x00ff0000U);
    assert(pixels[48] == 0x00ff0000U && pixels[55] == 0x00ff0000U);

    /* Standard four-plane VGA graphics combine one bit from each plane and
     * pass the resulting attribute index through the DAC. The CRTC start and
     * offset retain their display-address meaning instead of being replaced
     * by a mode-number special case. */
    assert(sequencer_write(bus, 1U, 1U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 4U, 4U) == BM_STATUS_OK);
    assert(graphics_write(bus, 5U, 0U) == BM_STATUS_OK);
    assert(graphics_write(bus, 6U, 1U) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x10U, 1U) == BM_STATUS_OK);
    assert(attribute_write(bus, 0x12U, 0x0fU) == BM_STATUS_OK);
    for (value = 0U; value < 16U; ++value)
        assert(attribute_write(bus, value, value) == BM_STATUS_OK);
    assert(io_read(bus, 0x03daU, &value) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c0U, 0x20U) == BM_STATUS_OK);
    dac_write(bus, 0U, 0U, 0U, 0U);
    dac_write(bus, 1U, 63U, 0U, 0U);
    dac_write(bus, 2U, 0U, 63U, 0U);
    dac_write(bus, 4U, 0U, 0U, 63U);
    dac_write(bus, 8U, 63U, 63U, 63U);
    dac_write(bus, 15U, 63U, 63U, 0U);
    assert(crtc_write(bus, 1U, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 9U, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0cU, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0dU, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x12U, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x13U, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x17U, 0x80U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 2U, 1U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0100U, 0x80U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0102U, 0x80U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 2U, 2U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0100U, 0x40U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0102U, 0x80U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 2U, 4U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0100U, 0x20U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0102U, 0x80U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 2U, 8U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0100U, 0x10U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0102U, 0x80U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 8U && geometry.height == 2U);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &graphics_framebuffer) == BM_STATUS_OK);
    assert(graphics_pixels[0] == 0x00ff0000U);
    assert(graphics_pixels[1] == 0x0000ff00U);
    assert(graphics_pixels[2] == 0x000000ffU);
    assert(graphics_pixels[3] == 0x00ffffffU);
    assert(graphics_pixels[4] == 0U);
    assert(graphics_pixels[8] == 0x00ffff00U);

    /* Chain-4 256-colour mode fetches one byte per pixel from the interleaved
     * planes and collapses CRTC double-scan into logical output lines. */
    assert(sequencer_write(bus, 2U, 0x0fU) == BM_STATUS_OK);
    assert(sequencer_write(bus, 4U, 0x08U) == BM_STATUS_OK);
    assert(graphics_write(bus, 5U, 0x40U) == BM_STATUS_OK);
    assert(crtc_write(bus, 1U, 1U) == BM_STATUS_OK);
    assert(crtc_write(bus, 9U, 0x80U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0cU, 2U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x0dU, 0U) == BM_STATUS_OK);
    assert(crtc_write(bus, 0x12U, 3U) == BM_STATUS_OK);
    dac_write(bus, 3U, 0U, 0U, 63U);
    assert(memory_write(bus, 0x000a0800U, 1U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0801U, 2U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0802U, 3U) == BM_STATUS_OK);
    assert(memory_write(bus, 0x000a0808U, 2U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 8U && geometry.height == 2U);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &graphics_framebuffer) == BM_STATUS_OK);
    assert(graphics_pixels[0] == 0x00ff0000U);
    assert(graphics_pixels[1] == 0x0000ff00U);
    assert(graphics_pixels[2] == 0x000000ffU);
    assert(graphics_pixels[8] == 0x0000ff00U);

    /* Mode-13-style scan repetition uses maximum scan line = 1, without
     * the separate double-scan bit. Do not consume a new VRAM row twice. */
    assert(crtc_write(bus, 9U, 1U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 8U && geometry.height == 2U);
    assert(bm_pvga1a_render(video, 0U, UINT64_C(1000000),
                            &graphics_framebuffer) == BM_STATUS_OK);
    assert(graphics_pixels[8] == 0x0000ff00U);
    assert(crtc_write(bus, 9U, 0x81U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.height == 1U);
    assert(graphics_write(bus, 0x0fU, 5U) == BM_STATUS_OK);
    assert(graphics_write(bus, 0x0eU, 1U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 16U); /* Paradise PR4 high-resolution override. */
    assert(graphics_write(bus, 0x0eU, 0U) == BM_STATUS_OK);
    assert(sequencer_write(bus, 1U, 9U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 16U); /* Dot-clock division is independent. */
    assert(sequencer_write(bus, 1U, 1U) == BM_STATUS_OK);

    /* CGA-compatible packed shift modes remain explicit until their address
     * and palette contracts are implemented; do not render them as planar. */
    assert(sequencer_write(bus, 4U, 4U) == BM_STATUS_OK);
    assert(graphics_write(bus, 5U, 0x20U) == BM_STATUS_OK);
    assert(bm_pvga1a_video_geometry(video, &geometry) ==
           BM_STATUS_UNSUPPORTED);

    bm_pvga1a_destroy(video);
    bm_bus_destroy(bus);
    return 0;
}
