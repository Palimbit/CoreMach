/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_runner.h"
#include "text_input.h"

#include <blumach/platforms/null_host.h>
#include <blumach/frontend/file_inputs.h>
#include <blumach/runtime/runtime.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG_INTERRUPT_HISTORY_CAPACITY 32U

typedef struct debug_interrupt_boundary {
    bm_frontend_debug_event_t interrupt;
    bm_frontend_debug_event_t source;
    bm_frontend_debug_event_t target;
    int has_source;
    int has_target;
} debug_interrupt_boundary_t;

typedef struct debug_tail {
    bm_frontend_debug_event_t *events;
    size_t capacity;
    size_t count;
    size_t next;
    bm_frontend_debug_event_t last_interrupt;
    bm_frontend_debug_event_t interrupt_source;
    bm_frontend_debug_event_t interrupt_target;
    bm_frontend_debug_event_t last_instruction;
    int has_last_interrupt;
    int has_interrupt_source;
    int has_interrupt_target;
    int has_last_instruction;
    int awaiting_interrupt_target;
    debug_interrupt_boundary_t interrupt_history[DEBUG_INTERRUPT_HISTORY_CAPACITY];
    size_t interrupt_history_count;
    size_t interrupt_history_next;
    size_t current_interrupt;
} debug_tail_t;

static void
capture_debug_event(void *context, const bm_frontend_debug_event_t *event)
{
    debug_tail_t *tail = context;
    if ((tail == NULL) || (event == NULL) || (tail->capacity == 0U))
        return;
    tail->events[tail->next] = *event;
    tail->next = (tail->next + 1U) % tail->capacity;
    if (tail->count < tail->capacity)
        ++tail->count;
    if (event->kind == BM_FRONTEND_DEBUG_INTERRUPT) {
        debug_interrupt_boundary_t *boundary =
            &tail->interrupt_history[tail->interrupt_history_next];
        *boundary = (debug_interrupt_boundary_t) { 0 };
        boundary->interrupt = *event;
        boundary->has_source = tail->has_last_instruction;
        if (tail->has_last_instruction)
            boundary->source = tail->last_instruction;
        tail->current_interrupt = tail->interrupt_history_next;
        tail->interrupt_history_next = (tail->interrupt_history_next + 1U) %
                                       DEBUG_INTERRUPT_HISTORY_CAPACITY;
        if (tail->interrupt_history_count < DEBUG_INTERRUPT_HISTORY_CAPACITY)
            ++tail->interrupt_history_count;
        tail->last_interrupt = *event;
        tail->has_last_interrupt = 1;
        tail->has_interrupt_source = tail->has_last_instruction;
        if (tail->has_last_instruction)
            tail->interrupt_source = tail->last_instruction;
        tail->has_interrupt_target = 0;
        tail->awaiting_interrupt_target = 1;
    } else if (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) {
        if (tail->awaiting_interrupt_target) {
            debug_interrupt_boundary_t *boundary =
                &tail->interrupt_history[tail->current_interrupt];
            tail->interrupt_target = *event;
            tail->has_interrupt_target = 1;
            boundary->target = *event;
            boundary->has_target = 1;
            tail->awaiting_interrupt_target = 0;
        }
        tail->last_instruction = *event;
        tail->has_last_instruction = 1;
    }
}

static void
print_debug_tail(const debug_tail_t *tail)
{
    size_t index;
    const size_t first = tail->count == tail->capacity ? tail->next : 0U;
    printf("debug_tail=%zu\n", tail->count);
    if (tail->has_last_interrupt) {
        printf("debug_last_interrupt_sequence=%" PRIu64 " vector=%02x\n",
               tail->last_interrupt.sequence,
               (unsigned int) tail->last_interrupt.value.interrupt.vector);
        if (tail->has_interrupt_source) {
            printf("debug_interrupt_source=%04x:%04x opcode=%02x\n",
                   tail->interrupt_source.value.instruction.cs,
                   tail->interrupt_source.value.instruction.ip,
                   tail->interrupt_source.value.instruction.opcode);
        }
        if (tail->has_interrupt_target) {
            printf("debug_interrupt_target=%04x:%04x opcode=%02x\n",
                   tail->interrupt_target.value.instruction.cs,
                   tail->interrupt_target.value.instruction.ip,
                   tail->interrupt_target.value.instruction.opcode);
        }
    } else {
        puts("debug_last_interrupt=none");
    }
    printf("debug_interrupt_history=%zu\n", tail->interrupt_history_count);
    for (index = 0U; index < tail->interrupt_history_count; ++index) {
        const size_t first_interrupt =
            tail->interrupt_history_count == DEBUG_INTERRUPT_HISTORY_CAPACITY ?
                tail->interrupt_history_next : 0U;
        const debug_interrupt_boundary_t *boundary =
            &tail->interrupt_history[(first_interrupt + index) %
                                     DEBUG_INTERRUPT_HISTORY_CAPACITY];
        printf("debug interrupt_sequence=%" PRIu64 " vector=%02x",
               boundary->interrupt.sequence,
               (unsigned int) boundary->interrupt.value.interrupt.vector);
        if (boundary->has_source) {
            printf(" source=%04x:%04x/%02x",
                   boundary->source.value.instruction.cs,
                   boundary->source.value.instruction.ip,
                   boundary->source.value.instruction.opcode);
        }
        if (boundary->has_target) {
            printf(" target=%04x:%04x/%02x",
                   boundary->target.value.instruction.cs,
                   boundary->target.value.instruction.ip,
                   boundary->target.value.instruction.opcode);
        }
        putchar('\n');
    }
    for (index = 0U; index < tail->count; ++index) {
        const bm_frontend_debug_event_t *event =
            &tail->events[(first + index) % tail->capacity];
        if (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) {
            printf("debug sequence=%" PRIu64
                   " kind=instruction cs=%04x ip=%04x physical=%05" PRIx32
                   " opcode=%02x effective=%02x prefixes=%u\n",
                   event->sequence, event->value.instruction.cs,
                   event->value.instruction.ip,
                   event->value.instruction.physical_address,
                   event->value.instruction.opcode,
                   event->value.instruction.effective_opcode,
                   (unsigned int) event->value.instruction.prefix_count);
        } else if (event->kind == BM_FRONTEND_DEBUG_IO) {
            printf("debug sequence=%" PRIu64
                   " kind=io direction=%s address=%04" PRIx64
                   " width=%u value=%02" PRIx64 "\n",
                   event->sequence, event->value.io.write ? "write" : "read",
                   event->value.io.address,
                   (unsigned int) event->value.io.width,
                   event->value.io.value);
        } else if (event->kind == BM_FRONTEND_DEBUG_INTERRUPT) {
            printf("debug sequence=%" PRIu64
                   " kind=interrupt vector=%02x\n",
                   event->sequence,
                   (unsigned int) event->value.interrupt.vector);
        }
    }
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
headless_run_machine(const bm_frontend_adapter_t *adapter,
                     const headless_run_options_t *options)
{
    bm_frontend_asset_binding_t bindings[4];
    size_t binding_count = 2U;
    bm_host_services_t host = bm_null_host_services();
    bm_frontend_blob_t even = { NULL, 0U };
    bm_frontend_blob_t odd = { NULL, 0U };
    bm_frontend_readonly_media_t floppy = { 0 };
    bm_frontend_readonly_media_t hard_disk = { 0 };
    bm_frontend_machine_t *machine = NULL;
    bm_frontend_diagnostics_t diagnostics = { 0 };
    bm_session_t *session = NULL;
    bm_status_t status = BM_STATUS_INVALID_STATE;
    bm_status_t video_status = BM_STATUS_INVALID_STATE;
    debug_tail_t debug_tail = { 0 };
    bm_video_geometry_t geometry = { 0U, 0U, BM_PIXEL_XRGB8888, 0U, 0U };
    uint32_t *pixels = NULL;
    size_t pixel_count = 0U;
    size_t nonblack = 0U;
    size_t storage_count = 0U;
    uint32_t frame_crc = 0U;
    int frame_matches = !options->expect_frame_crc32;
    int frame_written = options->frame_path == NULL;
    int result = 3;

    status = headless_text_schedule_validate(
        options->text_actions, options->text_action_count, options->key_ticks,
        options->ticks);
    if (status != BM_STATUS_OK) {
        fputs("typed actions must be valid, ordered, and fit within --ticks\n",
              stderr);
        return 2;
    }
    if (!bm_frontend_blob_read_exact(options->firmware_even_path, 32768U,
                                     &even) ||
        !bm_frontend_blob_read_exact(options->firmware_odd_path, 32768U,
                                     &odd)) {
        fputs("firmware halves must each be exactly 32768 bytes\n", stderr);
        result = 2;
        goto cleanup;
    }
    bindings[0] = (bm_frontend_asset_binding_t) {
        "firmware-even", BM_FRONTEND_ASSET_BLOB,
        { .blob = { NULL, even.data, even.size, NULL } }
    };
    bindings[1] = (bm_frontend_asset_binding_t) {
        "firmware-odd", BM_FRONTEND_ASSET_BLOB,
        { .blob = { NULL, odd.data, odd.size, NULL } }
    };
    if (options->floppy_path != NULL) {
        if (!bm_frontend_readonly_media_open(options->floppy_path, 512U,
                                             &floppy) ||
            ((floppy.size != 737280U) && (floppy.size != 1474560U))) {
            fputs("floppy image must be a raw 720 KiB or 1.44 MiB image\n",
                  stderr);
            result = 2;
            goto cleanup;
        }
        bindings[2] = (bm_frontend_asset_binding_t) {
            "floppy-0", BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
            { .media = floppy.media }
        };
        binding_count = 3U;
    }
    if (options->hard_disk_path != NULL) {
        const int opened = options->hard_disk_writable ?
            bm_frontend_working_media_open(options->hard_disk_path, 512U,
                                           &hard_disk) :
            bm_frontend_readonly_media_open(options->hard_disk_path, 512U,
                                            &hard_disk);
        if (!opened ||
            (hard_disk.size != 21411840U)) {
            fputs("hard disk image must be a raw 615/4/17 CP3026 image\n",
                  stderr);
            result = 2;
            goto cleanup;
        }
        bindings[binding_count++] = (bm_frontend_asset_binding_t) {
            "hard-disk-0", options->hard_disk_writable ?
                BM_FRONTEND_ASSET_BLOCK_MEDIA :
                BM_FRONTEND_ASSET_READ_ONLY_MEDIA,
            { .media = hard_disk.media }
        };
    }
    status = bm_frontend_machine_open(adapter, bindings, binding_count, &machine);
    if (status != BM_STATUS_OK) {
        fputs("machine assets are missing, unreadable, or invalid\n", stderr);
        result = 2;
        goto cleanup;
    }
    if (options->trace_tail != 0U) {
        debug_tail.events = calloc(options->trace_tail,
                                   sizeof(*debug_tail.events));
        if (debug_tail.events == NULL) {
            status = BM_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        debug_tail.capacity = options->trace_tail;
        status = bm_frontend_machine_set_debug_observer(
            machine, capture_debug_event, &debug_tail);
        if (status != BM_STATUS_OK)
            goto cleanup;
    }
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session,
                                      bm_frontend_machine_config(machine));
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    if (status == BM_STATUS_OK)
        status = headless_run_text_schedule(
            session, options->text_actions, options->text_action_count,
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
                    frame_matches = !options->expect_frame_crc32 ||
                                    (frame_crc == options->expected_frame_crc32);
                    if (options->frame_path != NULL)
                        frame_written = write_ppm(options->frame_path,
                                                  &framebuffer);
                }
            }
        }
    }
    (void) bm_frontend_machine_diagnostics(machine, &diagnostics);
    if ((status != BM_STATUS_OK) && (session != NULL)) {
        uint64_t bytes = 0U, length = 0U, dx = 0U;
        if (bm_session_inspect_cpu(session, 0, "last_instruction_bytes", &bytes) == BM_STATUS_OK &&
            bm_session_inspect_cpu(session, 0, "last_instruction_length", &length) == BM_STATUS_OK) {
            (void) bm_session_inspect_cpu(session, 0, "dx", &dx);
            fprintf(stderr, "failure_instruction_bytes_le=%016" PRIx64
                    " length=%" PRIu64 " dx=%04" PRIx64 "\n", bytes, length, dx);
        }
        {
            static const char *const names[] = {
                "ax", "bx", "cx", "dx", "sp", "bp", "si", "di",
                "cs", "ds", "es", "ss", "ip", "flags"
            };
            size_t index;
            fputs("failure_cpu", stderr);
            for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
                uint64_t value;
                if (bm_session_inspect_cpu(session, 0, names[index], &value) ==
                    BM_STATUS_OK)
                    fprintf(stderr, " %s=%04" PRIx64, names[index], value);
            }
            fputc('\n', stderr);
        }
    }
    if (session != NULL)
        (void) bm_session_storage_device_count(session, &storage_count);
    printf("machine=%s status=%d requested_ticks=%" PRIu64
           " elapsed_ticks=%" PRIu64 " instructions=%" PRIu64
           " io=%" PRIu64 "\n",
           options->machine_id, (int) status, options->ticks,
           session != NULL ? bm_session_time(session) : 0U,
           diagnostics.instructions, diagnostics.io_operations);
    printf("last=%04x:%04x physical=%05" PRIx32
           " opcode=%02x effective=%02x prefixes=%u\n",
           diagnostics.last_cs, diagnostics.last_ip,
           diagnostics.last_physical_address, diagnostics.last_opcode,
           diagnostics.last_effective_opcode,
           (unsigned int) diagnostics.last_prefix_count);
    printf("video_status=%d width=%" PRIu32 " height=%" PRIu32
           " refresh=%" PRIu64 "/%" PRIu64
           " nonblack=%zu crc32=%08" PRIx32 " frame=%s\n",
           (int) video_status, geometry.width, geometry.height,
           geometry.refresh_numerator, geometry.refresh_denominator, nonblack,
           frame_crc, (frame_written && options->frame_path != NULL) ?
                      options->frame_path : "");
    printf("firmware_hash=unchecked read_only_media_bytes=%" PRIu64
           " media_read_only=%d\n",
           diagnostics.read_only_media_bytes,
           diagnostics.read_only_media_bytes != 0U ? 1 : 0);
    {
        size_t index;
        for (index = 0U; index < storage_count; ++index) {
            bm_storage_device_status_t device;
            if (bm_session_storage_device_status(session, index, &device) ==
                BM_STATUS_OK) {
                printf("storage=%u unit=%" PRIu32 " installed=%d media=%d"
                       " read_only=%d motor=%d reads=%" PRIu64
                       " writes=%" PRIu64 "\n",
                       (unsigned int) device.kind, device.unit,
                       device.installed, device.media_present,
                       device.write_protected, device.motor_active,
                       device.read_operations, device.write_operations);
            }
        }
    }
    if (options->text_action_count != 0U)
        printf("input_actions=%zu key_ticks=%" PRIu64 "\n",
               options->text_action_count, options->key_ticks);
    if (debug_tail.capacity != 0U)
        print_debug_tail(&debug_tail);
    if (options->expect_frame_crc32)
        printf("expected_frame_crc32=%08" PRIx32 " matched=%d\n",
               options->expected_frame_crc32, frame_matches);
    if (!frame_written)
        fputs("could not write framebuffer capture\n", stderr);
    if ((status == BM_STATUS_OK) && (video_status == BM_STATUS_OK) &&
        frame_written && frame_matches)
        result = 0;

cleanup:
    bm_session_destroy(session);
    free(pixels);
    free(debug_tail.events);
    bm_frontend_machine_close(machine);
    bm_frontend_readonly_media_close(&floppy);
    bm_frontend_readonly_media_close(&hard_disk);
    bm_frontend_blob_release(&even);
    bm_frontend_blob_release(&odd);
    return result;
}
