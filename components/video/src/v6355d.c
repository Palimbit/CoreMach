/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2025 Sarah Walker
 * Copyright 2016-2025 Miran Grca
 * Copyright 2023-2025 W. M. Martinez
 * Copyright 2025 John Elliott
 * Copyright 2026 BluMach contributors
 *
 * Instance-owned, host-neutral selective rewrite of the inherited V6355D
 * register, VRAM and CGA-compatible raster paths. Physical LCD timing and
 * undocumented 16-colour modes are not modeled.
 */
#include <blumach/components/v6355d.h>

#include <string.h>

#define VRAM_FIRST UINT64_C(0x000b0000)
#define VRAM_LAST  UINT64_C(0x000bffff)
#define FRAME_NS UINT64_C(16666667)

struct bm_v6355d {
    bm_host_services_t host;
    uint8_t vram[BM_V6355D_VRAM_SIZE];
    uint8_t font[BM_V6355D_FONT_SIZE];
    uint8_t crtc[32];
    uint8_t ext[106];
    uint8_t crtc_index;
    uint8_t ext_index;
    uint8_t mode;
    uint8_t color;
    bm_tick_t (*time_now)(void *context);
    void *time_context;
};

static const uint8_t crtc_mask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

uint8_t
bm_v6355d_status_at(bm_tick_t emulated_time)
{
    /* Deterministic approximation, not a measured V6355D dot clock. */
    uint64_t phase = (uint64_t) emulated_time % FRAME_NS;
    uint64_t line = (phase * 262U) / FRAME_NS;
    uint8_t status = line >= 240U ? 8U : 0U;

    if (((phase * 262U) % FRAME_NS) > (FRAME_NS * 4U / 5U))
        status |= 1U;
    return status;
}

static uint8_t
read_port(const bm_v6355d_t *video, uint16_t port, bm_tick_t time)
{
    if ((port >= 0x03b0U) && (port <= 0x03bfU))
        port = (uint16_t) (port + 0x20U);
    switch (port) {
        case 0x03d4U: return video->crtc_index;
        case 0x03d5U: return video->crtc[video->crtc_index];
        case 0x03daU: return bm_v6355d_status_at(time);
        default: return 0xffU;
    }
}

static void
write_port(bm_v6355d_t *video, uint16_t port, uint8_t value)
{
    if ((port >= 0x03b0U) && (port <= 0x03bfU))
        port = (uint16_t) (port + 0x20U);
    switch (port) {
        case 0x03d0U: case 0x03d2U: case 0x03d4U: case 0x03d6U:
            video->crtc_index = value & 31U; break;
        case 0x03d1U: case 0x03d3U: case 0x03d5U: case 0x03d7U:
            video->crtc[video->crtc_index] = value & crtc_mask[video->crtc_index];
            break;
        case 0x03d8U: video->mode = value; break;
        case 0x03d9U: video->color = value; break;
        case 0x03ddU: video->ext_index = value; break;
        case 0x03deU:
            if (video->ext_index < sizeof(video->ext))
                video->ext[video->ext_index] = value;
            video->ext_index = (uint8_t) ((video->ext_index + 1U) % sizeof(video->ext));
            break;
        default: break;
    }
}

static bm_status_t
memory_access(void *context, bm_bus_transaction_t *tx)
{
    bm_v6355d_t *video = context;
    uint32_t byte;

    if ((tx == NULL) || (tx->size == 0U) ||
        (tx->size > sizeof(tx->value)) || (tx->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (tx->operation == BM_BUS_READ)
        tx->value = 0U;
    for (byte = 0U; byte < tx->size; ++byte) {
        uint32_t shift = (tx->endianness == BM_ENDIAN_LITTLE ? byte :
                          tx->size - byte - 1U) * 8U;
        size_t offset = (size_t) ((tx->address + byte) & 0x3fffU);
        if (tx->operation == BM_BUS_READ)
            tx->value |= (uint64_t) video->vram[offset] << shift;
        else
            video->vram[offset] = (uint8_t) (tx->value >> shift);
    }
    return BM_STATUS_OK;
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *tx)
{
    bm_v6355d_t *video = context;
    uint32_t byte;

    if ((tx == NULL) || (tx->size == 0U) ||
        (tx->size > sizeof(tx->value)) || (tx->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (tx->operation == BM_BUS_READ)
        tx->value = 0U;
    for (byte = 0U; byte < tx->size; ++byte) {
        uint32_t shift = (tx->endianness == BM_ENDIAN_LITTLE ? byte :
                          tx->size - byte - 1U) * 8U;
        uint16_t port = (uint16_t) (tx->address + byte);
        if (tx->operation == BM_BUS_READ)
            tx->value |= (uint64_t) read_port(video, port,
                video->time_now != NULL ?
                video->time_now(video->time_context) : 0U) << shift;
        else
            write_port(video, port, (uint8_t) (tx->value >> shift));
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_v6355d_create(const bm_host_services_t *host, bm_bus_t *bus,
                 const bm_v6355d_config_t *config, bm_v6355d_t **out_video)
{
    bm_v6355d_t *video;
    bm_status_t status;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_video == NULL) || (config->font == NULL) ||
        (config->font_size != BM_V6355D_FONT_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_video = NULL;
    video = host->allocate(host->context, sizeof(*video));
    if (video == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(video, 0, sizeof(*video));
    video->host = *host;
    video->time_now = config->time_now;
    video->time_context = config->time_context;
    memcpy(video->font, config->font, sizeof(video->font));
    bm_v6355d_reset(video);
    status = bm_bus_map(bus, BM_ADDRESS_MEMORY, VRAM_FIRST, VRAM_LAST,
                        memory_access, video);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(bus, BM_ADDRESS_IO, 0x03b0U, 0x03bfU,
                            io_access, video);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(bus, BM_ADDRESS_IO, 0x03d0U, 0x03dfU,
                            io_access, video);
    if (status != BM_STATUS_OK) {
        bm_v6355d_destroy(video);
        return status;
    }
    *out_video = video;
    return BM_STATUS_OK;
}

void
bm_v6355d_destroy(bm_v6355d_t *video)
{
    if (video != NULL)
        video->host.release(video->host.context, video);
}

void
bm_v6355d_reset(bm_v6355d_t *video)
{
    if (video == NULL)
        return;
    memset(video->crtc, 0, sizeof(video->crtc));
    memset(video->ext, 0, sizeof(video->ext));
    video->ext[0x65] = 1U;
    video->crtc_index = video->ext_index = video->mode = video->color = 0U;
}

bm_status_t
bm_v6355d_geometry(const bm_v6355d_t *video, bm_video_geometry_t *geometry)
{
    static const uint32_t heights[4] = { 192U, 200U, 204U, 64U };

    if ((video == NULL) || (geometry == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *geometry = (bm_video_geometry_t) {
        (video->ext[0x65] & 4U) != 0U ? 512U : 640U,
        heights[video->ext[0x65] & 3U], BM_PIXEL_XRGB8888, 0U, 0U
    };
    return BM_STATUS_OK;
}

static uint8_t
text_pixel(const bm_v6355d_t *video, uint32_t x, uint32_t y,
           uint32_t width, bm_tick_t time)
{
    uint32_t cell_width = (video->mode & 1U) != 0U ? 8U : 16U;
    uint32_t row_height = (video->crtc[9] & 31U) + 1U;
    uint32_t row = y / row_height;
    uint32_t scan = y % row_height;
    uint32_t columns = width / cell_width;
    uint16_t start = (uint16_t) ((video->crtc[12] << 8U) | video->crtc[13]);
    uint16_t cell = (uint16_t) (start + row * columns + x / cell_width);
    uint8_t ch = video->vram[(2U * cell) & 0x3fffU];
    uint8_t attr = video->vram[(2U * cell + 1U) & 0x3fffU];
    uint8_t glyph = video->font[8U * ch + (scan & 7U)];
    uint32_t bit = (x % cell_width) / (cell_width / 8U);
    uint8_t fg = attr & 15U;
    uint8_t bg = (video->mode & 0x20U) != 0U ? (attr >> 4U) & 7U : attr >> 4U;
    uint16_t cursor = (uint16_t) ((video->crtc[14] << 8U) | video->crtc[15]);
    int cursor_on = (video->crtc[10] & 0x20U) == 0U &&
                    (uint16_t) (cell & 0x3fffU) == (uint16_t) (cursor & 0x3fffU) &&
                    scan >= (video->crtc[10] & 31U) &&
                    scan <= (video->crtc[11] & 31U) &&
                    ((uint64_t) time / (FRAME_NS * 16U) & 1U) == 0U;

    if ((video->mode & 0x20U) != 0U && (attr & 0x80U) != 0U &&
        ((uint64_t) time / (FRAME_NS * 16U) & 1U) != 0U)
        fg = bg;
    if ((video->ext[0x66] & 0x80U) != 0U && (attr & 7U) == 1U && scan == 7U)
        glyph = 0xffU;
    return cursor_on ? (uint8_t) (fg ^ 15U) :
           (glyph & (0x80U >> bit)) != 0U ? fg : bg;
}

static uint8_t
graphics_pixel(const bm_v6355d_t *video, uint32_t x, uint32_t y,
               uint32_t width)
{
    uint32_t byte = ((y & 1U) << 13U) + (y >> 1U) * (width / 8U);
    uint16_t start = (uint16_t) ((video->crtc[12] << 8U) | video->crtc[13]);
    uint8_t data = video->vram[(byte + 2U * start +
                                (video->mode & 0x10U ? x / 8U : x / 16U)) & 0x3fffU];
    uint8_t intensity = (video->color & 0x10U) != 0U ? 8U : 0U;

    if ((video->mode & 0x10U) != 0U)
        return (data & (0x80U >> (x & 7U))) != 0U ? video->color & 15U : 0U;
    {
        uint8_t palette[4];
        unsigned int sample = (data >> (6U - 2U * ((x / 2U) & 3U))) & 3U;

        palette[0] = video->color & 15U;
        palette[1] = (uint8_t) (intensity | ((video->mode & 4U) != 0U ||
                                (video->color & 0x20U) != 0U ? 3U : 2U));
        palette[2] = (uint8_t) (intensity | ((video->color & 0x20U) != 0U &&
                                (video->mode & 4U) == 0U ? 5U : 4U));
        palette[3] = (uint8_t) (intensity | ((video->mode & 4U) != 0U ||
                                (video->color & 0x20U) != 0U ? 7U : 6U));
        return palette[sample];
    }
}

bm_status_t
bm_v6355d_render_indices(const bm_v6355d_t *video, bm_tick_t emulated_time,
                         uint8_t *pixels, size_t capacity, uint32_t stride)
{
    bm_video_geometry_t geometry;
    uint32_t y, x;

    if ((video == NULL) || (pixels == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    (void) bm_v6355d_geometry(video, &geometry);
    if ((stride < geometry.width) ||
        (capacity / stride < geometry.height))
        return BM_STATUS_INVALID_ARGUMENT;
    for (y = 0U; y < geometry.height; ++y) {
        for (x = 0U; x < geometry.width; ++x) {
            uint8_t index = 0U;
            if ((video->mode & 8U) != 0U) {
                index = (video->mode & 2U) != 0U ?
                    graphics_pixel(video, x, y, geometry.width) :
                    text_pixel(video, x, y, geometry.width, emulated_time);
            }
            pixels[(size_t) y * stride + x] = index;
        }
    }
    return BM_STATUS_OK;
}
