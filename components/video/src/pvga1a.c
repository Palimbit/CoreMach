/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2008-2019 Sarah Walker
 * Copyright 2016-2019 Miran Grca
 * Copyright 2026 BluMach contributors
 *
 * Selective portable rewrite of the VGA register and planar-memory behaviour
 * used by the inherited Paradise PVGA1A implementation. This component owns
 * all state and depends only on the BluMach host and bus contracts.
 */
#include <blumach/components/pvga1a.h>

#include <string.h>

#define VGA_MEMORY_FIRST 0x000a0000U
#define VGA_MEMORY_LAST  0x000bffffU
#define VGA_IO_FIRST     0x03b0U
#define VGA_IO_LAST      0x03dfU
#define VGA_PLANE_SIZE   65536U

struct bm_pvga1a {
    bm_host_services_t host;
    uint8_t *vram;
    size_t vram_size;
    uint8_t sequencer[32];
    uint8_t graphics[16];
    uint8_t crtc[64];
    uint8_t attribute[32];
    uint8_t palette[256][3];
    uint8_t latch[4];
    uint8_t sequencer_index;
    uint8_t graphics_index;
    uint8_t crtc_index;
    uint8_t attribute_index;
    uint8_t attribute_flip_flop;
    uint8_t attribute_palette_enable;
    uint8_t misc_output;
    uint8_t feature_control;
    uint8_t dac_mask;
    uint8_t dac_state;
    uint8_t dac_index;
    uint8_t dac_component;
    uint8_t status_phase;
};

static uint8_t
rotate_right(uint8_t value, unsigned int count)
{
    count &= 7U;
    if (count == 0U)
        return value;
    return (uint8_t) ((value >> count) | (value << (8U - count)));
}

static uint8_t
logic_result(uint8_t source, uint8_t latch, uint8_t function)
{
    switch (function & 0x18U) {
        case 0x08U:
            return source & latch;
        case 0x10U:
            return source | latch;
        case 0x18U:
            return source ^ latch;
        default:
            return source;
    }
}

static int
decode_memory(const bm_pvga1a_t *video, uint64_t address, uint16_t *offset)
{
    uint64_t base;
    uint64_t size;

    switch (video->graphics[6] & 0x0cU) {
        case 0x04U:
            base = 0x000a0000U;
            size = 0x00010000U;
            break;
        case 0x08U:
            base = 0x000b0000U;
            size = 0x00008000U;
            break;
        case 0x0cU:
            base = 0x000b8000U;
            size = 0x00008000U;
            break;
        default:
            base = 0x000a0000U;
            size = 0x00020000U;
            break;
    }
    if ((address < base) || ((address - base) >= size))
        return 0;
    *offset = (uint16_t) (address - base);
    return 1;
}

static size_t
plane_address(unsigned int plane, uint16_t offset)
{
    return (size_t) plane * VGA_PLANE_SIZE + offset;
}

static void
load_latches(bm_pvga1a_t *video, uint16_t offset)
{
    unsigned int plane;
    for (plane = 0; plane < 4U; ++plane)
        video->latch[plane] = video->vram[plane_address(plane, offset)];
}

static uint8_t
read_vram_byte(bm_pvga1a_t *video, uint64_t address)
{
    uint16_t offset;
    unsigned int plane;
    uint8_t result;

    if (!decode_memory(video, address, &offset))
        return 0xffU;
    if (((video->sequencer[4] & 0x08U) != 0U)) {
        plane = offset & 3U;
        offset = (uint16_t) (offset >> 2U);
    } else if (((video->sequencer[4] & 0x04U) == 0U) &&
               ((video->graphics[5] & 0x10U) != 0U)) {
        plane = (video->graphics[4] & 2U) | (offset & 1U);
        offset = (uint16_t) (offset >> 1U);
    } else {
        plane = video->graphics[4] & 3U;
    }
    load_latches(video, offset);
    if ((video->graphics[5] & 0x08U) == 0U)
        return video->latch[plane];

    result = 0xffU;
    for (plane = 0; plane < 4U; ++plane) {
        if ((video->graphics[7] & (1U << plane)) != 0U) {
            uint8_t expected = (video->graphics[2] & (1U << plane)) ? 0xffU : 0U;
            result &= (uint8_t) ~(video->latch[plane] ^ expected);
        }
    }
    return result;
}

static void
write_vram_byte(bm_pvga1a_t *video, uint64_t address, uint8_t value)
{
    uint16_t offset;
    uint8_t plane_mask = video->sequencer[2] & 0x0fU;
    uint8_t write_mode = video->graphics[5] & 3U;
    uint8_t rotated = rotate_right(value, video->graphics[3] & 7U);
    uint8_t bit_mask = video->graphics[8];
    unsigned int plane;

    if (!decode_memory(video, address, &offset))
        return;
    if ((video->sequencer[4] & 0x08U) != 0U) {
        plane_mask &= (uint8_t) (1U << (offset & 3U));
        offset = (uint16_t) (offset >> 2U);
    } else if (((video->sequencer[4] & 0x04U) == 0U) &&
               ((video->graphics[5] & 0x10U) != 0U)) {
        plane_mask &= (uint8_t) (0x05U << (offset & 1U));
        offset = (uint16_t) (offset >> 1U);
    }
    for (plane = 0; plane < 4U; ++plane) {
        uint8_t source;
        uint8_t result;
        if ((plane_mask & (1U << plane)) == 0U)
            continue;
        switch (write_mode) {
            case 1U:
                video->vram[plane_address(plane, offset)] = video->latch[plane];
                continue;
            case 2U:
                source = (value & (1U << plane)) ? 0xffU : 0U;
                break;
            case 3U:
                source = (video->graphics[0] & (1U << plane)) ? 0xffU : 0U;
                bit_mask &= rotated;
                break;
            default:
                if ((video->graphics[1] & (1U << plane)) != 0U)
                    source = (video->graphics[0] & (1U << plane)) ? 0xffU : 0U;
                else
                    source = rotated;
                break;
        }
        result = logic_result(source, video->latch[plane], video->graphics[3]);
        result = (uint8_t) ((result & bit_mask) | (video->latch[plane] & (uint8_t) ~bit_mask));
        video->vram[plane_address(plane, offset)] = result;
    }
}

static bm_status_t
memory_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pvga1a_t *video = context;
    uint32_t index;

    if ((transaction == NULL) || (transaction->size == 0U) ||
        (transaction->size > sizeof(transaction->value)) ||
        (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ) {
        transaction->value = 0;
        for (index = 0; index < transaction->size; ++index) {
            uint32_t shift = transaction->endianness == BM_ENDIAN_LITTLE
                                 ? index * 8U
                                 : (transaction->size - index - 1U) * 8U;
            transaction->value |= (uint64_t) read_vram_byte(video, transaction->address + index)
                                  << shift;
        }
        return BM_STATUS_OK;
    }
    for (index = 0; index < transaction->size; ++index) {
        uint32_t shift = transaction->endianness == BM_ENDIAN_LITTLE
                             ? index * 8U
                             : (transaction->size - index - 1U) * 8U;
        write_vram_byte(video, transaction->address + index,
                        (uint8_t) (transaction->value >> shift));
    }
    return BM_STATUS_OK;
}

static uint16_t
normalize_crtc_port(const bm_pvga1a_t *video, uint16_t port)
{
    if (((port & 0xfff0U) == 0x03b0U) || ((port & 0xfff0U) == 0x03d0U)) {
        if ((video->misc_output & 1U) == 0U)
            return port ^ 0x0060U;
    }
    return port;
}

static uint8_t
read_port(bm_pvga1a_t *video, uint16_t raw_port)
{
    uint16_t port = normalize_crtc_port(video, raw_port);

    switch (port) {
        case 0x03c0U:
            return video->attribute_index | video->attribute_palette_enable;
        case 0x03c1U:
            return video->attribute[video->attribute_index & 0x1fU];
        case 0x03c2U:
            return 0x10U;
        case 0x03c4U:
            return video->sequencer_index;
        case 0x03c5U:
            return video->sequencer[video->sequencer_index & 0x1fU];
        case 0x03c6U:
            return video->dac_mask;
        case 0x03c7U:
            return video->dac_state;
        case 0x03c8U:
            return video->dac_index;
        case 0x03c9U: {
            uint8_t value = video->palette[video->dac_index][video->dac_component] & 0x3fU;
            if (++video->dac_component == 3U) {
                video->dac_component = 0;
                ++video->dac_index;
            }
            return value;
        }
        case 0x03caU:
            return video->feature_control;
        case 0x03ccU:
            return video->misc_output;
        case 0x03ceU:
            return video->graphics_index;
        case 0x03cfU:
            if ((video->graphics_index & 0x0fU) == 0x0fU)
                return (video->graphics[0x0f] & 0x17U) | 0x80U;
            return video->graphics[video->graphics_index & 0x0fU];
        case 0x03d4U:
            return video->crtc_index;
        case 0x03d5U:
            return video->crtc[video->crtc_index & 0x3fU];
        case 0x03daU:
            video->attribute_flip_flop = 0;
            video->status_phase ^= 1U;
            return video->status_phase ? 0x09U : 0U;
        default:
            return 0xffU;
    }
}

static void
write_port(bm_pvga1a_t *video, uint16_t raw_port, uint8_t value)
{
    uint16_t port = normalize_crtc_port(video, raw_port);

    switch (port) {
        case 0x03c0U:
            if (video->attribute_flip_flop == 0U) {
                video->attribute_index = value & 0x1fU;
                video->attribute_palette_enable = value & 0x20U;
            } else {
                video->attribute[video->attribute_index & 0x1fU] = value;
            }
            video->attribute_flip_flop ^= 1U;
            break;
        case 0x03c2U:
            video->misc_output = value;
            break;
        case 0x03c4U:
            video->sequencer_index = value;
            break;
        case 0x03c5U:
            if (video->sequencer_index <= 7U)
                video->sequencer[video->sequencer_index] = value;
            break;
        case 0x03c6U:
            video->dac_mask = value;
            break;
        case 0x03c7U:
            video->dac_state = 3U;
            video->dac_index = value;
            video->dac_component = 0;
            break;
        case 0x03c8U:
            video->dac_state = 0U;
            video->dac_index = value;
            video->dac_component = 0;
            break;
        case 0x03c9U:
            video->palette[video->dac_index][video->dac_component] = value & 0x3fU;
            if (++video->dac_component == 3U) {
                video->dac_component = 0;
                ++video->dac_index;
            }
            break;
        case 0x03ceU:
            video->graphics_index = value;
            break;
        case 0x03cfU: {
            uint8_t index = video->graphics_index & 0x0fU;
            if ((index < 9U) || (index == 0x0fU) ||
                ((video->graphics[0x0f] & 7U) == 5U))
                video->graphics[index] = value;
            break;
        }
        case 0x03d4U:
            video->crtc_index = value;
            break;
        case 0x03d5U:
            if ((video->crtc_index <= 0x29U) &&
                (((video->crtc[0x11] & 0x80U) == 0U) || (video->crtc_index >= 7U)))
                video->crtc[video->crtc_index] = value;
            break;
        case 0x03daU:
            video->feature_control = value;
            break;
        default:
            break;
    }
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pvga1a_t *video = context;

    if ((transaction == NULL) || (transaction->size != 1U) ||
        (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ)
        transaction->value = read_port(video, (uint16_t) transaction->address);
    else
        write_port(video, (uint16_t) transaction->address,
                   (uint8_t) transaction->value);
    return BM_STATUS_OK;
}

bm_status_t
bm_pvga1a_create(const bm_host_services_t *host,
                 bm_bus_t *bus,
                 const bm_pvga1a_config_t *config,
                 bm_pvga1a_t **out_video)
{
    bm_pvga1a_t *video;
    bm_status_t status;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (bus == NULL) ||
        (config == NULL) || (out_video == NULL) ||
        (config->vram_size != BM_PVGA1A_VRAM_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_video = NULL;
    video = host->allocate(host->context, sizeof(*video));
    if (video == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(video, 0, sizeof(*video));
    video->host = *host;
    video->vram_size = config->vram_size;
    video->vram = host->allocate(host->context, video->vram_size);
    if (video->vram == NULL) {
        bm_pvga1a_destroy(video);
        return BM_STATUS_OUT_OF_MEMORY;
    }
    memset(video->vram, 0, video->vram_size);
    bm_pvga1a_reset(video);
    status = bm_bus_map(bus, BM_ADDRESS_MEMORY, VGA_MEMORY_FIRST,
                        VGA_MEMORY_LAST, memory_access, video);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(bus, BM_ADDRESS_IO, VGA_IO_FIRST,
                            VGA_IO_LAST, io_access, video);
    if (status != BM_STATUS_OK) {
        bm_pvga1a_destroy(video);
        return status;
    }
    *out_video = video;
    return BM_STATUS_OK;
}

void
bm_pvga1a_destroy(bm_pvga1a_t *video)
{
    if (video == NULL)
        return;
    if (video->vram != NULL)
        video->host.release(video->host.context, video->vram);
    video->host.release(video->host.context, video);
}

void
bm_pvga1a_reset(bm_pvga1a_t *video)
{
    if (video == NULL)
        return;
    memset(video->sequencer, 0, sizeof(video->sequencer));
    memset(video->graphics, 0, sizeof(video->graphics));
    memset(video->crtc, 0, sizeof(video->crtc));
    memset(video->attribute, 0, sizeof(video->attribute));
    memset(video->palette, 0, sizeof(video->palette));
    memset(video->latch, 0, sizeof(video->latch));
    video->crtc[0] = 63U;
    video->crtc[6] = 255U;
    video->misc_output = 1U;
    video->dac_mask = 0xffU;
    video->sequencer[2] = 0x0fU;
    video->graphics[8] = 0xffU;
    video->sequencer_index = 0;
    video->graphics_index = 0;
    video->crtc_index = 0;
    video->attribute_index = 0;
    video->attribute_flip_flop = 0;
    video->attribute_palette_enable = 0;
    video->feature_control = 0;
    video->dac_state = 0;
    video->dac_index = 0;
    video->dac_component = 0;
    video->status_phase = 0;
}

bm_status_t
bm_pvga1a_inspect_register(const bm_pvga1a_t *video,
                           bm_pvga1a_register_set_t set,
                           uint8_t index,
                           uint8_t *value)
{
    if ((video == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    switch (set) {
        case BM_PVGA1A_SEQUENCER:
            if (index >= sizeof(video->sequencer))
                return BM_STATUS_INVALID_ARGUMENT;
            *value = video->sequencer[index];
            break;
        case BM_PVGA1A_GRAPHICS:
            if (index >= sizeof(video->graphics))
                return BM_STATUS_INVALID_ARGUMENT;
            *value = video->graphics[index];
            break;
        case BM_PVGA1A_CRTC:
            if (index >= sizeof(video->crtc))
                return BM_STATUS_INVALID_ARGUMENT;
            *value = video->crtc[index];
            break;
        case BM_PVGA1A_ATTRIBUTE:
            if (index >= sizeof(video->attribute))
                return BM_STATUS_INVALID_ARGUMENT;
            *value = video->attribute[index];
            break;
        default:
            return BM_STATUS_INVALID_ARGUMENT;
    }
    return BM_STATUS_OK;
}

bm_status_t
bm_pvga1a_inspect_vram(const bm_pvga1a_t *video,
                       unsigned int plane,
                       uint16_t offset,
                       uint8_t *value)
{
    if ((video == NULL) || (value == NULL) || (plane >= 4U))
        return BM_STATUS_INVALID_ARGUMENT;
    *value = video->vram[plane_address(plane, offset)];
    return BM_STATUS_OK;
}

static uint32_t
text_character_width(const bm_pvga1a_t *video)
{
    uint32_t width = (video->sequencer[1] & 1U) ? 8U : 9U;
    if ((video->sequencer[1] & 8U) != 0U)
        width *= 2U;
    return width;
}

static uint16_t
vertical_display_lines(const bm_pvga1a_t *video)
{
    uint16_t lines = video->crtc[0x12];
    if ((video->crtc[7] & 0x02U) != 0U)
        lines |= 0x0100U;
    if ((video->crtc[7] & 0x40U) != 0U)
        lines |= 0x0200U;
    return (uint16_t) (lines + 1U);
}

static uint16_t
vertical_total_lines(const bm_pvga1a_t *video)
{
    uint16_t lines = video->crtc[6];
    if ((video->crtc[7] & 0x01U) != 0U)
        lines |= 0x0100U;
    if ((video->crtc[7] & 0x20U) != 0U)
        lines |= 0x0200U;
    return (uint16_t) (lines + 2U);
}

typedef enum pvga1a_display_mode {
    PVGA1A_DISPLAY_TEXT = 0,
    PVGA1A_DISPLAY_PLANAR_4,
    PVGA1A_DISPLAY_CHAIN4_8,
    PVGA1A_DISPLAY_UNSUPPORTED
} pvga1a_display_mode_t;

static pvga1a_display_mode_t
display_mode(const bm_pvga1a_t *video)
{
    if (((video->graphics[6] | video->attribute[0x10]) & 1U) == 0U)
        return PVGA1A_DISPLAY_TEXT;
    if ((video->graphics[5] & 0x60U) == 0U)
        return PVGA1A_DISPLAY_PLANAR_4;
    if (((video->graphics[5] & 0x60U) == 0x40U) &&
        ((video->sequencer[4] & 0x08U) != 0U))
        return PVGA1A_DISPLAY_CHAIN4_8;
    return PVGA1A_DISPLAY_UNSUPPORTED;
}

static uint32_t
graphics_height(const bm_pvga1a_t *video)
{
    uint32_t height = vertical_display_lines(video);
    uint32_t repeats = (video->crtc[9] & 0x1fU) + 1U;
    if ((video->crtc[9] & 0x80U) != 0U)
        repeats *= 2U;
    return (height + repeats - 1U) / repeats;
}

static uint64_t
pixel_clock_hz(const bm_pvga1a_t *video)
{
    switch ((video->misc_output >> 2U) & 3U) {
        case 0U: return UINT64_C(25175000);
        case 1U: return UINT64_C(28322000);
        default: return 0U;
    }
}

static uint64_t
frame_dot_count(const bm_pvga1a_t *video)
{
    return ((uint64_t) video->crtc[0] + 5U) *
           text_character_width(video) * vertical_total_lines(video);
}

static uint64_t
default_cursor_blink_half_period(uint64_t ticks_per_second)
{
    return (ticks_per_second / 70U) * 8U +
           (((ticks_per_second % 70U) * 8U + 69U) / 70U);
}

static uint64_t
cursor_blink_half_period(const bm_pvga1a_t *video, uint64_t ticks_per_second)
{
    uint64_t pixel_clock;
    uint64_t frame_dots;
    uint64_t numerator;

    pixel_clock = pixel_clock_hz(video);
    if (pixel_clock == 0U) {
        /* VCLK2/VCLK3 are board-defined inputs. The portable PCS 86 path does
         * not select them, so retain a deterministic 70 Hz fallback rather
         * than inventing a board clock. */
        return default_cursor_blink_half_period(ticks_per_second);
    }
    frame_dots = frame_dot_count(video);
    if ((frame_dots == 0U) ||
        (ticks_per_second > UINT64_MAX / frame_dots / 8U))
        return default_cursor_blink_half_period(ticks_per_second);
    numerator = ticks_per_second * frame_dots * 8U;
    return numerator / pixel_clock + ((numerator % pixel_clock) != 0U);
}

static int
cursor_blink_visible(const bm_pvga1a_t *video, bm_tick_t emulated_time,
                     uint64_t ticks_per_second)
{
    uint64_t half_period = cursor_blink_half_period(video, ticks_per_second);
    return (half_period != 0U) && (((emulated_time / half_period) & 1U) == 0U);
}

bm_status_t
bm_pvga1a_video_geometry(const bm_pvga1a_t *video, bm_video_geometry_t *geometry)
{
    pvga1a_display_mode_t mode;
    uint32_t columns;
    uint32_t width;
    uint32_t height;

    if ((video == NULL) || (geometry == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    mode = display_mode(video);
    if (mode == PVGA1A_DISPLAY_UNSUPPORTED)
        return BM_STATUS_UNSUPPORTED;
    columns = (uint32_t) video->crtc[1] + 1U;
    if (mode == PVGA1A_DISPLAY_TEXT) {
        width = columns * text_character_width(video);
        height = vertical_display_lines(video);
    } else {
        width = columns * 8U;
        if (mode == PVGA1A_DISPLAY_CHAIN4_8) {
            /* Paradise PR4 bit 0 selects high-resolution 256-colour output;
             * otherwise one source pixel occupies two display dots. */
            if ((video->sequencer[1] & 8U) != 0U)
                width *= 2U;
            if ((video->graphics[0x0e] & 1U) == 0U)
                width /= 2U;
        }
        height = graphics_height(video);
    }
    if ((columns > 160U) || (width == 0U) || (width > 2880U) ||
        (height == 0U) || (height > 1024U))
        return BM_STATUS_DEVICE_ERROR;
    geometry->width = width;
    geometry->height = height;
    geometry->format = BM_PIXEL_XRGB8888;
    geometry->refresh_numerator = pixel_clock_hz(video);
    geometry->refresh_denominator = geometry->refresh_numerator != 0U ?
                                    frame_dot_count(video) : 0U;
    return BM_STATUS_OK;
}

static uint8_t
attribute_palette_index(const bm_pvga1a_t *video, uint8_t color)
{
    uint8_t index;
    if ((video->attribute[0x10] & 0x80U) != 0U)
        index = (video->attribute[color & 0x0fU] & 0x0fU) |
                (uint8_t) ((video->attribute[0x14] & 0x0fU) << 4U);
    else
        index = (video->attribute[color & 0x0fU] & 0x3fU) |
                (uint8_t) ((video->attribute[0x14] & 0x0cU) << 4U);
    return index & video->dac_mask;
}

static uint32_t
dac_color(const bm_pvga1a_t *video, uint8_t index)
{
    index &= video->dac_mask;
    uint32_t red = (uint32_t) ((video->palette[index][0] << 2U) |
                               (video->palette[index][0] >> 4U));
    uint32_t green = (uint32_t) ((video->palette[index][1] << 2U) |
                                 (video->palette[index][1] >> 4U));
    uint32_t blue = (uint32_t) ((video->palette[index][2] << 2U) |
                                (video->palette[index][2] >> 4U));
    return (red << 16U) | (green << 8U) | blue;
}

static uint32_t
attribute_color(const bm_pvga1a_t *video, uint8_t color)
{
    return dac_color(video, attribute_palette_index(video, color));
}

static uint16_t
font_base(const bm_pvga1a_t *video, int use_map_b)
{
    uint8_t select = video->sequencer[3];
    if (use_map_b)
        return (uint16_t) ((((select >> 2U) & 3U) << 14U) |
                           ((select & 0x20U) ? 0x2000U : 0U));
    return (uint16_t) (((select & 3U) << 14U) |
                       ((select & 0x10U) ? 0x2000U : 0U));
}

static int
display_blanked(const bm_pvga1a_t *video)
{
    return ((video->sequencer[1] & 0x20U) != 0U) ||
           ((video->crtc[0x17] & 0x80U) == 0U) ||
           (video->attribute_palette_enable == 0U);
}

static void
render_planar_graphics(const bm_pvga1a_t *video,
                       const bm_video_geometry_t *geometry,
                       bm_video_framebuffer_t *framebuffer)
{
    uint16_t start = (uint16_t) (((uint16_t) video->crtc[0x0c] << 8U) |
                                 video->crtc[0x0d]);
    uint32_t row_stride = (uint32_t) video->crtc[0x13] * 2U;
    uint32_t y;

    for (y = 0U; y < geometry->height; ++y) {
        uint32_t *line = framebuffer->pixels + (size_t) y * framebuffer->stride;
        uint32_t x;
        uint16_t row = (uint16_t) (start + y * row_stride);
        if (display_blanked(video)) {
            memset(line, 0, (size_t) geometry->width * sizeof(*line));
            continue;
        }
        for (x = 0U; x < geometry->width; ++x) {
            uint16_t offset = (uint16_t) (row + x / 8U);
            uint8_t mask = (uint8_t) (0x80U >> (x & 7U));
            uint8_t color = 0U;
            unsigned int plane;
            for (plane = 0U; plane < 4U; ++plane) {
                if ((video->vram[plane_address(plane, offset)] & mask) != 0U)
                    color |= (uint8_t) (1U << plane);
            }
            color &= video->attribute[0x12] & 0x0fU;
            line[x] = attribute_color(video, color);
        }
    }
}

static void
render_chain4_graphics(const bm_pvga1a_t *video,
                       const bm_video_geometry_t *geometry,
                       bm_video_framebuffer_t *framebuffer)
{
    uint16_t start = (uint16_t) (((uint16_t) video->crtc[0x0c] << 8U) |
                                 video->crtc[0x0d]);
    uint32_t row_stride = (uint32_t) video->crtc[0x13] * 2U;
    uint32_t y;

    for (y = 0U; y < geometry->height; ++y) {
        uint32_t *line = framebuffer->pixels + (size_t) y * framebuffer->stride;
        uint32_t x;
        uint16_t row = (uint16_t) (start + y * row_stride);
        if (display_blanked(video)) {
            memset(line, 0, (size_t) geometry->width * sizeof(*line));
            continue;
        }
        for (x = 0U; x < geometry->width; ++x) {
            unsigned int plane = x & 3U;
            uint16_t offset = (uint16_t) (row + x / 4U);
            line[x] = dac_color(
                video, video->vram[plane_address(plane, offset)]);
        }
    }
}

bm_status_t
bm_pvga1a_render(const bm_pvga1a_t *video, bm_tick_t emulated_time,
                 uint64_t ticks_per_second,
                 bm_video_framebuffer_t *framebuffer)
{
    bm_video_geometry_t geometry;
    pvga1a_display_mode_t mode;
    uint32_t columns;
    uint32_t character_width;
    uint32_t character_height;
    uint32_t row_stride;
    uint16_t start;
    uint16_t cursor;
    uint32_t cursor_start;
    uint32_t cursor_end;
    int cursor_enabled;
    uint32_t y;
    bm_status_t status;

    if ((video == NULL) || (ticks_per_second == 0U) ||
        (framebuffer == NULL) || (framebuffer->pixels == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    status = bm_pvga1a_video_geometry(video, &geometry);
    if (status != BM_STATUS_OK)
        return status;
    if ((framebuffer->stride < geometry.width) ||
        (framebuffer->pixel_capacity / framebuffer->stride < geometry.height))
        return BM_STATUS_CAPACITY_EXCEEDED;
    framebuffer->geometry = geometry;
    mode = display_mode(video);
    if (mode == PVGA1A_DISPLAY_PLANAR_4) {
        render_planar_graphics(video, &geometry, framebuffer);
        return BM_STATUS_OK;
    }
    if (mode == PVGA1A_DISPLAY_CHAIN4_8) {
        render_chain4_graphics(video, &geometry, framebuffer);
        return BM_STATUS_OK;
    }
    columns = (uint32_t) video->crtc[1] + 1U;
    character_width = text_character_width(video);
    character_height = (uint32_t) (video->crtc[9] & 0x1fU) + 1U;
    row_stride = (uint32_t) video->crtc[0x13] * 2U;
    if (row_stride == 0U)
        row_stride = columns;
    start = (uint16_t) (((uint16_t) video->crtc[0x0c] << 8U) | video->crtc[0x0d]);
    cursor = (uint16_t) (((uint16_t) video->crtc[0x0e] << 8U) |
                         video->crtc[0x0f]);
    cursor = (uint16_t) (cursor + ((video->crtc[0x0b] >> 5U) & 3U));
    cursor_start = video->crtc[0x0a] & 0x1fU;
    cursor_end = video->crtc[0x0b] & 0x1fU;
    cursor_enabled = ((video->crtc[0x0a] & 0x20U) == 0U) &&
                     (cursor_start <= cursor_end) &&
                     (cursor_start < character_height) &&
                     cursor_blink_visible(video, emulated_time,
                                          ticks_per_second);
    if (cursor_end >= character_height)
        cursor_end = character_height - 1U;

    for (y = 0; y < geometry.height; ++y) {
        uint32_t *line = framebuffer->pixels + (size_t) y * framebuffer->stride;
        uint32_t column;
        if (display_blanked(video)) {
            memset(line, 0, (size_t) geometry.width * sizeof(*line));
            continue;
        }
        for (column = 0; column < columns; ++column) {
            uint32_t row = y / character_height;
            uint32_t scanline = y % character_height;
            uint16_t cell = (uint16_t) (start + row * row_stride + column);
            uint8_t character = video->vram[plane_address(0, cell)];
            uint8_t attribute = video->vram[plane_address(1, cell)];
            uint8_t foreground = attribute & 0x0fU;
            uint8_t background = attribute >> 4U;
            uint16_t glyph_address;
            uint8_t glyph;
            uint32_t x;
            int draw_cursor = cursor_enabled && (cell == cursor) &&
                              (scanline >= cursor_start) &&
                              (scanline <= cursor_end);
            if (((video->attribute[0x10] & 8U) != 0U) &&
                ((attribute & 0x80U) != 0U))
                background &= 7U;
            glyph_address = (uint16_t) (font_base(video, (attribute & 8U) != 0U) +
                                        (uint16_t) character * 32U + scanline);
            glyph = video->vram[plane_address(2, glyph_address)];
            for (x = 0; x < character_width; ++x) {
                uint32_t source_x = ((video->sequencer[1] & 8U) != 0U) ? x / 2U : x;
                int set;
                if (source_x < 8U)
                    set = (glyph & (0x80U >> source_x)) != 0U;
                else
                    set = ((character & 0xe0U) == 0xc0U) &&
                          ((video->attribute[0x10] & 4U) != 0U) &&
                          ((glyph & 1U) != 0U);
                line[column * character_width + x] = attribute_color(
                    video, draw_cursor ? foreground :
                           (set ? foreground : background));
            }
        }
    }
    return BM_STATUS_OK;
}
