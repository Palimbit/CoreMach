/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct probe_trace {
    bm_808x_trace_t last;
    uint64_t instructions;
    uint64_t io_operations;
} probe_trace_t;

static void
capture_instruction(void *context, const bm_808x_trace_t *trace)
{
    probe_trace_t *probe = context;
    probe->last = *trace;
    ++probe->instructions;
}

static void
capture_io(void *context, const bm_pcs86_io_trace_t *trace)
{
    probe_trace_t *probe = context;
    (void) trace;
    ++probe->io_operations;
}

static uint8_t *
read_firmware(const char *path)
{
    FILE *file;
    uint8_t *data = malloc(BM_PCS86_FIRMWARE_HALF_SIZE);
    size_t count;
    int extra;
    if (data == NULL)
        return NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0)
        file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        free(data);
        return NULL;
    }
    count = fread(data, 1, BM_PCS86_FIRMWARE_HALF_SIZE, file);
    extra = fgetc(file);
    fclose(file);
    if ((count != BM_PCS86_FIRMWARE_HALF_SIZE) || (extra != EOF)) {
        free(data);
        return NULL;
    }
    return data;
}

static uint32_t
crc32_pixels(const uint32_t *pixels, size_t count)
{
    uint32_t crc = 0xffffffffU;
    size_t index;
    for (index = 0; index < count; ++index) {
        unsigned int byte_index;
        for (byte_index = 0; byte_index < 4U; ++byte_index) {
            unsigned int bit;
            crc ^= (pixels[index] >> (byte_index * 8U)) & 0xffU;
            for (bit = 0; bit < 8U; ++bit)
                crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
        }
    }
    return ~crc;
}

static int
write_ppm(const char *path, const bm_video_framebuffer_t *framebuffer)
{
    FILE *file;
    uint32_t y;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "wb") != 0)
        file = NULL;
#else
    file = fopen(path, "wb");
#endif
    if (file == NULL)
        return 0;
    if (fprintf(file, "P6\n%" PRIu32 " %" PRIu32 "\n255\n",
                framebuffer->geometry.width, framebuffer->geometry.height) < 0) {
        fclose(file);
        return 0;
    }
    for (y = 0; y < framebuffer->geometry.height; ++y) {
        uint32_t x;
        const uint32_t *line = framebuffer->pixels + (size_t) y * framebuffer->stride;
        for (x = 0; x < framebuffer->geometry.width; ++x) {
            uint8_t rgb[3] = {
                (uint8_t) (line[x] >> 16U),
                (uint8_t) (line[x] >> 8U),
                (uint8_t) line[x]
            };
            if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
                fclose(file);
                return 0;
            }
        }
    }
    return fclose(file) == 0;
}

int
main(int argc, char **argv)
{
    bm_host_services_t host = bm_null_host_services();
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    probe_trace_t probe = { { 0, 0, 0, 0 }, 0, 0 };
    uint8_t *even;
    uint8_t *odd;
    bm_status_t status;
    uint64_t ax = 0;
    uint64_t dx = 0;
    bm_video_geometry_t geometry = { 0, 0, BM_PIXEL_XRGB8888 };
    bm_status_t video_status = BM_STATUS_INVALID_STATE;
    uint32_t *pixels = NULL;
    size_t pixel_count = 0;
    size_t nonblack = 0;
    uint32_t frame_crc = 0;

    if ((argc != 3) && (argc != 4)) {
        fprintf(stderr, "usage: %s <even-rom> <odd-rom> [frame.ppm]\n", argv[0]);
        return 2;
    }
    even = read_firmware(argv[1]);
    odd = read_firmware(argv[2]);
    if ((even == NULL) || (odd == NULL)) {
        fputs("firmware halves must each be exactly 32768 bytes\n", stderr);
        free(even);
        free(odd);
        return 2;
    }
    config = (bm_pcs86_config_t) {
        { "bios-even-109", even, BM_PCS86_FIRMWARE_HALF_SIZE, NULL },
        { "bios-odd-109", odd, BM_PCS86_FIRMWARE_HALF_SIZE, NULL },
        capture_instruction, &probe, capture_io, &probe
    };
    machine = bm_pcs86_machine_config(&config);
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session, &machine);
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    if (status == BM_STATUS_OK)
        status = bm_session_run_for(session, 10000000U);

    if (session != NULL) {
        (void) bm_session_inspect_cpu(session, 0, "ax", &ax);
        (void) bm_session_inspect_cpu(session, 0, "dx", &dx);
        video_status = bm_session_video_geometry(session, &geometry);
        if (video_status == BM_STATUS_OK) {
            pixel_count = (size_t) geometry.width * geometry.height;
            pixels = calloc(pixel_count, sizeof(*pixels));
            if (pixels == NULL)
                video_status = BM_STATUS_OUT_OF_MEMORY;
            else {
                bm_video_framebuffer_t framebuffer = {
                    pixels, pixel_count, geometry.width, geometry
                };
                size_t index;
                video_status = bm_session_render_video(session, &framebuffer);
                if (video_status == BM_STATUS_OK) {
                    for (index = 0; index < pixel_count; ++index) {
                        if (pixels[index] != 0U)
                            ++nonblack;
                    }
                    frame_crc = crc32_pixels(pixels, pixel_count);
                    if ((argc == 4) && !write_ppm(argv[3], &framebuffer)) {
                        fputs("could not write framebuffer capture\n", stderr);
                        video_status = BM_STATUS_DEVICE_ERROR;
                    }
                }
            }
        }
    }

    printf("status=%d instructions=%" PRIu64 " io=%" PRIu64
           " last=%04x:%04x physical=%05" PRIx32 " opcode=%02x"
           " ax=%04" PRIx64 " dx=%04" PRIx64 "\n",
           (int) status, probe.instructions, probe.io_operations,
           probe.last.cs, probe.last.ip, probe.last.physical_address, probe.last.opcode,
           ax, dx);
    printf("video_status=%d width=%" PRIu32 " height=%" PRIu32
           " nonblack=%zu crc32=%08" PRIx32 " capture=%s\n",
           (int) video_status, geometry.width, geometry.height,
           nonblack, frame_crc, (argc == 4 && video_status == BM_STATUS_OK) ? argv[3] : "");

    bm_session_destroy(session);
    free(pixels);
    free(even);
    free(odd);
    return (status == BM_STATUS_OK) ? 0 : 3;
}
