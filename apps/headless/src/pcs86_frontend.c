/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pcs86_frontend.h"
#include "file_inputs.h"
#include "text_input.h"

#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct headless_trace {
    bm_808x_trace_t last;
    uint64_t instructions;
    uint64_t io_operations;
} headless_trace_t;

static void
capture_instruction(void *context, const bm_808x_trace_t *trace)
{
    headless_trace_t *state = context;
    state->last = *trace;
    ++state->instructions;
}

static void
capture_io(void *context, const bm_pcs86_io_trace_t *trace)
{
    headless_trace_t *state = context;
    (void) trace;
    ++state->io_operations;
}

static const bm_pcs86_firmware_identity_t *
find_firmware_identity(const bm_pcs86_firmware_identity_t *identities,
                       size_t count, const char *role)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        if ((identities[index].role != NULL) &&
            (strcmp(identities[index].role, role) == 0))
            return &identities[index];
    }
    return NULL;
}

static uint32_t
crc32_pixels(const uint32_t *pixels, size_t count)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    for (index = 0U; index < count; ++index) {
        unsigned int byte_index;
        for (byte_index = 0U; byte_index < 4U; ++byte_index) {
            unsigned int bit;
            crc ^= (pixels[index] >> (byte_index * 8U)) & UINT32_C(0xff);
            for (bit = 0U; bit < 8U; ++bit)
                crc = (crc >> 1U) ^
                      ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return ~crc;
}

static FILE *
open_output(const char *path)
{
    FILE *file = NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "wb") != 0)
        file = NULL;
#else
    file = fopen(path, "wb");
#endif
    return file;
}

static int
write_ppm(const char *path, const bm_video_framebuffer_t *framebuffer)
{
    FILE *file = open_output(path);
    uint32_t y;
    if (file == NULL)
        return 0;
    if (fprintf(file, "P6\n%" PRIu32 " %" PRIu32 "\n255\n",
                framebuffer->geometry.width,
                framebuffer->geometry.height) < 0) {
        fclose(file);
        return 0;
    }
    for (y = 0U; y < framebuffer->geometry.height; ++y) {
        uint32_t x;
        const uint32_t *line = framebuffer->pixels +
                               (size_t) y * framebuffer->stride;
        for (x = 0U; x < framebuffer->geometry.width; ++x) {
            const uint8_t rgb[3] = {
                (uint8_t) (line[x] >> 16U),
                (uint8_t) (line[x] >> 8U),
                (uint8_t) line[x]
            };
            if (fwrite(rgb, 1U, sizeof(rgb), file) != sizeof(rgb)) {
                fclose(file);
                return 0;
            }
        }
    }
    return fclose(file) == 0;
}

int
headless_run_pcs86(const headless_run_options_t *options)
{
    bm_host_services_t host = bm_null_host_services();
    headless_blob_t even = { NULL, 0U };
    headless_blob_t odd = { NULL, 0U };
    headless_readonly_media_t floppy = { 0 };
    headless_trace_t trace = { 0 };
    bm_pcs86_config_t config = { 0 };
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    bm_status_t status = BM_STATUS_INVALID_STATE;
    bm_status_t video_status = BM_STATUS_INVALID_STATE;
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888 };
    uint32_t *pixels = NULL;
    size_t pixel_count = 0U;
    size_t nonblack = 0U;
    uint32_t frame_crc = 0U;
    size_t identity_count = 0U;
    const bm_pcs86_firmware_identity_t *identities;
    const bm_pcs86_firmware_identity_t *even_identity;
    const bm_pcs86_firmware_identity_t *odd_identity;
    int frame_written = options->frame_path == NULL;
    int result = 3;
    uint64_t typing_duration = 0U;

    if (options->type_text != NULL) {
        status = headless_text_duration(options->type_text, options->key_ticks,
                                        &typing_duration);
        if ((status != BM_STATUS_OK) || (options->type_at >= options->ticks) ||
            (typing_duration > options->ticks - options->type_at)) {
            fputs("typed text must be valid ASCII and fit within --ticks\n",
                  stderr);
            result = 2;
            goto cleanup;
        }
    }

    if (!headless_blob_read_exact(options->firmware_even_path,
                                  BM_PCS86_FIRMWARE_HALF_SIZE, &even) ||
        !headless_blob_read_exact(options->firmware_odd_path,
                                  BM_PCS86_FIRMWARE_HALF_SIZE, &odd)) {
        fputs("firmware halves must each be exactly 32768 bytes\n", stderr);
        result = 2;
        goto cleanup;
    }
    if (options->floppy_path != NULL) {
        if (!headless_readonly_media_open(options->floppy_path, 512U, &floppy) ||
            ((floppy.size != 737280U) && (floppy.size != 1474560U))) {
            fputs("floppy image must be a raw 720 KiB or 1.44 MiB image\n",
                  stderr);
            result = 2;
            goto cleanup;
        }
    }
    identities = bm_pcs86_expected_firmware(&identity_count);
    even_identity = find_firmware_identity(identities, identity_count, "even");
    odd_identity = find_firmware_identity(identities, identity_count, "odd");
    if ((even_identity == NULL) || (odd_identity == NULL)) {
        fputs("machine firmware contract is unavailable\n", stderr);
        result = 3;
        goto cleanup;
    }
    config.firmware_even = (bm_blob_view_t) {
        even_identity->asset_id, even.data, even.size, NULL
    };
    config.firmware_odd = (bm_blob_view_t) {
        odd_identity->asset_id, odd.data, odd.size, NULL
    };
    config.trace = capture_instruction;
    config.trace_context = &trace;
    config.io_trace = capture_io;
    config.io_trace_context = &trace;
    if (floppy.file != NULL) {
        config.floppy[0] = (bm_floppy_drive_config_t) {
            1, 1, 1,
            { 80U, 2U, (uint8_t) (floppy.size == 737280U ? 9U : 18U), 512U },
            floppy.media
        };
    }
    machine = bm_pcs86_machine_config(&config);
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session, &machine);
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    if ((status == BM_STATUS_OK) && (options->type_text != NULL))
        status = bm_session_run_for(session, options->type_at);
    if ((status == BM_STATUS_OK) && (options->type_text != NULL))
        status = headless_type_text(session, options->type_text,
                                    options->key_ticks);
    if ((status == BM_STATUS_OK) &&
        (bm_session_time(session) < options->ticks))
        status = bm_session_run_for(session,
                                    options->ticks - bm_session_time(session));

    if (session != NULL) {
        video_status = bm_session_video_geometry(session, &geometry);
        if ((video_status == BM_STATUS_OK) && (geometry.height != 0U) &&
            ((size_t) geometry.width <= SIZE_MAX / geometry.height)) {
            bm_video_framebuffer_t framebuffer;
            size_t index;
            pixel_count = (size_t) geometry.width * geometry.height;
            pixels = calloc(pixel_count, sizeof(*pixels));
            if (pixels == NULL) {
                video_status = BM_STATUS_OUT_OF_MEMORY;
            } else {
                framebuffer = (bm_video_framebuffer_t) {
                    pixels, pixel_count, geometry.width, geometry
                };
                video_status = bm_session_render_video(session, &framebuffer);
                if (video_status == BM_STATUS_OK) {
                    for (index = 0U; index < pixel_count; ++index) {
                        if (pixels[index] != 0U)
                            ++nonblack;
                    }
                    frame_crc = crc32_pixels(pixels, pixel_count);
                    if (options->frame_path != NULL)
                        frame_written = write_ppm(options->frame_path, &framebuffer);
                }
            }
        }
    }

    printf("machine=%s status=%d requested_ticks=%" PRIu64
           " elapsed_ticks=%" PRIu64 " instructions=%" PRIu64
           " io=%" PRIu64 "\n",
           options->machine_id, (int) status, options->ticks,
           session != NULL ? bm_session_time(session) : 0U,
           trace.instructions, trace.io_operations);
    printf("last=%04x:%04x physical=%05" PRIx32
           " opcode=%02x effective=%02x prefixes=%u\n",
           trace.last.cs, trace.last.ip, trace.last.physical_address,
           trace.last.opcode, trace.last.effective_opcode,
           trace.last.prefix_count);
    printf("video_status=%d width=%" PRIu32 " height=%" PRIu32
           " nonblack=%zu crc32=%08" PRIx32 " frame=%s\n",
           (int) video_status, geometry.width, geometry.height, nonblack,
           frame_crc, (frame_written && options->frame_path != NULL) ?
                      options->frame_path : "");
    printf("firmware_hash=unchecked floppy_bytes=%zu floppy_read_only=%d\n",
           floppy.size, floppy.file != NULL ? 1 : 0);
    if (options->type_text != NULL)
        printf("input=scheduled type_at=%" PRIu64 " key_ticks=%" PRIu64
               " duration=%" PRIu64 "\n",
               options->type_at, options->key_ticks, typing_duration);
    if (!frame_written)
        fputs("could not write framebuffer capture\n", stderr);
    if ((status == BM_STATUS_OK) && (video_status == BM_STATUS_OK) &&
        frame_written)
        result = 0;

cleanup:
    bm_session_destroy(session);
    free(pixels);
    headless_readonly_media_close(&floppy);
    headless_blob_release(&even);
    headless_blob_release(&odd);
    return result;
}
