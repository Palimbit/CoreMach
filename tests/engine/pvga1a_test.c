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

static void
dac_write(bm_bus_t *bus, uint8_t index, uint8_t red, uint8_t green,
          uint8_t blue)
{
    assert(io_write(bus, 0x03c8U, index) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, red) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, green) == BM_STATUS_OK);
    assert(io_write(bus, 0x03c9U, blue) == BM_STATUS_OK);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_pvga1a_t *video = NULL;
    bm_pvga1a_config_t config = { BM_PVGA1A_VRAM_SIZE };
    uint8_t value = 0;
    unsigned int plane;
    bm_video_geometry_t geometry;
    uint32_t pixels[64];
    bm_video_framebuffer_t framebuffer = { pixels, 64U, 16U, { 0, 0, BM_PIXEL_XRGB8888 } };
    uint32_t graphics_pixels[16];
    bm_video_framebuffer_t graphics_framebuffer = {
        graphics_pixels, 16U, 8U, { 0, 0, BM_PIXEL_XRGB8888 }
    };

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_pvga1a_create(&host, bus, &config, &video) == BM_STATUS_OK);

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
