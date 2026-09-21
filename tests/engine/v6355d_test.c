/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/v6355d.h>
#include <blumach/platforms/null_host.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static bm_status_t
access_byte(bm_bus_t *bus, bm_address_space_t space,
            bm_bus_operation_t operation, uint32_t address, uint8_t *value)
{
    bm_bus_transaction_t tx = {
        space, operation, address, *value, 1U, 1U, 0U, BM_ENDIAN_LITTLE, 0U
    };
    bm_status_t status = bm_bus_transact(bus, &tx);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) tx.value;
    return status;
}

static void
write_byte(bm_bus_t *bus, bm_address_space_t space,
           uint32_t address, uint8_t value)
{
    assert(access_byte(bus, space, BM_BUS_WRITE, address, &value) == BM_STATUS_OK);
}

static uint8_t
read_byte(bm_bus_t *bus, bm_address_space_t space, uint32_t address)
{
    uint8_t value = 0U;
    assert(access_byte(bus, space, BM_BUS_READ, address, &value) == BM_STATUS_OK);
    return value;
}

static void
crtc(bm_bus_t *bus, uint8_t index, uint8_t value)
{
    write_byte(bus, BM_ADDRESS_IO, 0x03d4U, index);
    write_byte(bus, BM_ADDRESS_IO, 0x03d5U, value);
}

static bm_tick_t
test_time(void *context)
{
    return *(const bm_tick_t *) context;
}

int
main(void)
{
    static uint8_t font[BM_V6355D_FONT_SIZE];
    static uint8_t pixels[640U * 204U];
    bm_tick_t time = 0U;
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_bus_t *other_bus = NULL;
    bm_v6355d_t *video = NULL;
    bm_v6355d_t *other_video = NULL;
    bm_video_geometry_t geometry;
    bm_v6355d_config_t config = { font, sizeof(font), test_time, &time };

    font[8U * 'A'] = 0x80U;
    font[8U * 'R' + 1U] = 0x80U;
    assert(bm_bus_create(&host, 3U, &bus) == BM_STATUS_OK);
    assert(bm_v6355d_create(&host, bus, &config, &video) == BM_STATUS_OK);
    assert(bm_v6355d_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 640U && geometry.height == 200U);

    /* One physical 16 KiB bank mirrors across B0000-BFFFF, including B8000. */
    write_byte(bus, BM_ADDRESS_MEMORY, 0x000b8000U, 'A');
    write_byte(bus, BM_ADDRESS_MEMORY, 0x000b8001U, 0x1fU);
    assert(read_byte(bus, BM_ADDRESS_MEMORY, 0x000b0000U) == 'A');
    assert(read_byte(bus, BM_ADDRESS_MEMORY, 0x000bc001U) == 0x1fU);
    assert(bm_bus_create(&host, 3U, &other_bus) == BM_STATUS_OK);
    assert(bm_v6355d_create(&host, other_bus, &config, &other_video) == BM_STATUS_OK);
    assert(read_byte(other_bus, BM_ADDRESS_MEMORY, 0x000b8000U) == 0U);
    bm_v6355d_destroy(other_video);
    bm_bus_destroy(other_bus);

    /* Monochrome compatibility ports address the same controller. */
    write_byte(bus, BM_ADDRESS_IO, 0x03b4U, 9U);
    write_byte(bus, BM_ADDRESS_IO, 0x03b5U, 7U);
    assert(read_byte(bus, BM_ADDRESS_IO, 0x03d4U) == 9U);
    assert(read_byte(bus, BM_ADDRESS_IO, 0x03d5U) == 7U);
    crtc(bus, 10U, 0x20U); /* No cursor for glyph test. */
    write_byte(bus, BM_ADDRESS_IO, 0x03d8U, 0x09U);
    assert(bm_v6355d_render_indices(video, 0U, pixels,
                                    sizeof(pixels), 640U) == BM_STATUS_OK);
    assert(pixels[0] == 15U && pixels[1] == 1U);
    assert(pixels[640U] == 1U);

    /* The M15 BIOS writes POST text but leaves CRTC 9 at zero. Its glyphs
     * often have a blank first scanline, so text must still use eight rows. */
    write_byte(bus, BM_ADDRESS_MEMORY, 0x000b8002U, 'R');
    write_byte(bus, BM_ADDRESS_MEMORY, 0x000b8003U, 0x07U);
    crtc(bus, 9U, 0U);
    write_byte(bus, BM_ADDRESS_IO, 0x03d8U, 0x29U);
    assert(read_byte(bus, BM_ADDRESS_IO, 0x03d5U) == 0U);
    assert(bm_v6355d_render_indices(video, 0U, pixels,
                                    sizeof(pixels), 640U) == BM_STATUS_OK);
    assert(pixels[8U] == 0U);
    assert(pixels[640U + 8U] == 7U);
    assert(pixels[8U * 640U + 8U] == 0U);

    /* Indexed extended registers auto-increment; 65h selects 512 x 204. */
    write_byte(bus, BM_ADDRESS_IO, 0x03ddU, 0x65U);
    write_byte(bus, BM_ADDRESS_IO, 0x03deU, 0x06U);
    write_byte(bus, BM_ADDRESS_IO, 0x03deU, 0x80U);
    assert(bm_v6355d_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 512U && geometry.height == 204U);
    assert(bm_v6355d_render_indices(video, 0U, pixels, 511U,
                                    512U) == BM_STATUS_INVALID_ARGUMENT);

    write_byte(bus, BM_ADDRESS_IO, 0x03ddU, 0x65U);
    write_byte(bus, BM_ADDRESS_IO, 0x03deU, 1U);
    crtc(bus, 12U, 0U);
    crtc(bus, 13U, 0U);
    write_byte(bus, BM_ADDRESS_IO, 0x03d8U, 0x1aU); /* 640-pixel graphics. */
    write_byte(bus, BM_ADDRESS_IO, 0x03d9U, 0x0fU);
    write_byte(bus, BM_ADDRESS_MEMORY, 0x000b8000U, 0x80U);
    assert(bm_v6355d_render_indices(video, 0U, pixels,
                                    sizeof(pixels), 640U) == BM_STATUS_OK);
    assert(pixels[0] == 15U && pixels[1] == 0U);
    assert(pixels[640U] == 0U); /* Odd scanline is the other CGA bank. */

    assert((bm_v6355d_status_at(0U) & 8U) == 0U);
    assert((bm_v6355d_status_at(16000000U) & 8U) != 0U);
    assert(read_byte(bus, BM_ADDRESS_IO, 0x03daU) == 0U);
    time = 16000000U;
    assert((read_byte(bus, BM_ADDRESS_IO, 0x03baU) & 8U) != 0U);
    bm_v6355d_reset(video);
    assert(bm_v6355d_geometry(video, &geometry) == BM_STATUS_OK);
    assert(geometry.width == 640U && geometry.height == 200U);
    assert(read_byte(bus, BM_ADDRESS_MEMORY, 0x000b0001U) == 0x1fU);
    bm_v6355d_destroy(video);
    bm_bus_destroy(bus);
    return 0;
}
