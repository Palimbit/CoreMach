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
