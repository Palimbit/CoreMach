/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_runner.h"
#include "text_input.h"

#include <blumach/platforms/null_host.h>
#include <blumach/frontend/file_inputs.h>
#include <blumach/runtime/runtime.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_INTERRUPT_HISTORY_CAPACITY 32U

typedef struct debug_interrupt_boundary {
    bm_frontend_debug_event_t interrupt;
    bm_frontend_debug_event_t source;
    bm_frontend_debug_event_t target;
    int has_source;
    int has_target;
} debug_interrupt_boundary_t;

typedef struct debug_tail {
    bm_session_t *session;
    bm_frontend_debug_event_t *events;
    size_t capacity;
    size_t count;
    size_t next;
    bm_frontend_debug_event_t last_interrupt;
    bm_frontend_debug_event_t interrupt_source;
    bm_frontend_debug_event_t interrupt_target;
    bm_frontend_debug_event_t last_instruction;
    bm_frontend_debug_event_t last_memory_write;
    bm_frontend_debug_event_t last_memory_write_source;
    int has_last_interrupt;
    int has_interrupt_source;
    int has_interrupt_target;
    int has_last_instruction;
    int has_last_memory_write;
    int has_last_memory_write_source;
    int awaiting_interrupt_target;
    debug_interrupt_boundary_t interrupt_history[DEBUG_INTERRUPT_HISTORY_CAPACITY];
    size_t interrupt_history_count;
    size_t interrupt_history_next;
    size_t current_interrupt;
    int include_memory;
    uint64_t memory_reads;
    uint64_t memory_writes;
    uint32_t memory_first;
    uint32_t memory_last;
    uint8_t *memory_image;
    size_t memory_image_size;
    uint16_t freeze_cs;
    uint16_t freeze_ip;
    int freeze_at;
    uint32_t freeze_physical;
    int freeze_at_physical;
    uint64_t freeze_sequence;
    int freeze_after_sequence;
    int frozen;
    int memory_only;
    int memory_writes_only;
    int io_only;
    uint64_t frozen_cpu[14];
    int has_frozen_cpu;
} debug_tail_t;

typedef struct floppy_swap_context {
    bm_session_t *session;
    bm_frontend_readonly_media_t *media;
} floppy_swap_context_t;

static bm_status_t
replace_floppy(void *context)
{
    const floppy_swap_context_t *swap = context;
    const bm_storage_media_change_t change = {
        1, 1, swap->media->media
    };
    return bm_session_replace_storage_media(
        swap->session, BM_STORAGE_DEVICE_FLOPPY, 0U, &change);
}

static void
freeze_debug_tail(debug_tail_t *tail)
{
    static const char *const names[] = {
        "ax", "bx", "cx", "dx", "sp", "bp", "si", "di",
        "cs", "ds", "es", "ss", "ip", "flags"
    };
    size_t index;
    tail->frozen = 1;
    if (tail->session == NULL)
        return;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (bm_session_inspect_cpu(tail->session, 0U, names[index],
                                   &tail->frozen_cpu[index]) != BM_STATUS_OK)
            return;
    }
    tail->has_frozen_cpu = 1;
}

static void
capture_debug_event(void *context, const bm_frontend_debug_event_t *event)
{
    debug_tail_t *tail = context;
    if ((tail == NULL) || (event == NULL) || (tail->capacity == 0U))
        return;
    if (tail->frozen)
        return;
    if (tail->io_only && (event->kind != BM_FRONTEND_DEBUG_IO)) {
        if (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) {
            tail->last_instruction = *event;
            tail->has_last_instruction = 1;
        }
        return;
    }
    if (tail->memory_only &&
        (event->kind != BM_FRONTEND_DEBUG_MEMORY)) {
        if (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) {
            tail->last_instruction = *event;
            tail->has_last_instruction = 1;
            if (tail->freeze_at &&
                (event->value.instruction.cs == tail->freeze_cs) &&
                (event->value.instruction.ip == tail->freeze_ip))
                freeze_debug_tail(tail);
            if (tail->freeze_at_physical &&
                (event->value.instruction.physical_address ==
                 tail->freeze_physical))
                freeze_debug_tail(tail);
        }
        return;
    }
    if (event->kind == BM_FRONTEND_DEBUG_MEMORY) {
        const uint64_t event_last = event->value.memory.address +
                                    event->value.memory.width - 1U;
        if (tail->include_memory &&
            ((event_last < tail->memory_first) ||
             (event->value.memory.address > tail->memory_last)))
            return;
        if (event->value.memory.write) {
            if (tail->memory_image != NULL) {
                uint64_t byte_index;
                for (byte_index = 0U;
                     byte_index < event->value.memory.width; ++byte_index) {
                    const uint64_t address = event->value.memory.address +
                                             byte_index;
                    if ((address >= tail->memory_first) &&
                        (address <= tail->memory_last))
                        tail->memory_image[address - tail->memory_first] =
                            (uint8_t) (event->value.memory.value >>
                                       (byte_index * 8U));
                }
            }
            ++tail->memory_writes;
            tail->last_memory_write = *event;
            tail->has_last_memory_write = 1;
            tail->has_last_memory_write_source = tail->has_last_instruction;
            if (tail->has_last_instruction)
                tail->last_memory_write_source = tail->last_instruction;
        } else {
            ++tail->memory_reads;
            if (tail->memory_writes_only)
                return;
        }
        if (!tail->include_memory)
            return;
    }
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
    if (tail->freeze_at &&
        (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) &&
        (event->value.instruction.cs == tail->freeze_cs) &&
        (event->value.instruction.ip == tail->freeze_ip))
        freeze_debug_tail(tail);
    if (tail->freeze_at_physical &&
        (event->kind == BM_FRONTEND_DEBUG_INSTRUCTION) &&
        (event->value.instruction.physical_address == tail->freeze_physical))
        freeze_debug_tail(tail);
    if (tail->freeze_after_sequence &&
        (event->sequence >= tail->freeze_sequence))
        freeze_debug_tail(tail);
}

static void
print_debug_tail(const debug_tail_t *tail)
{
    size_t index;
    const size_t first = tail->count == tail->capacity ? tail->next : 0U;
    printf("debug_tail=%zu\n", tail->count);
    printf("debug_trace_frozen=%d\n", tail->frozen);
    if (tail->has_frozen_cpu) {
        printf("debug_frozen_cpu ax=%04" PRIx64 " bx=%04" PRIx64
               " cx=%04" PRIx64 " dx=%04" PRIx64 " sp=%04" PRIx64
               " bp=%04" PRIx64 " si=%04" PRIx64 " di=%04" PRIx64
               " cs=%04" PRIx64 " ds=%04" PRIx64 " es=%04" PRIx64
               " ss=%04" PRIx64 " ip=%04" PRIx64 " flags=%04" PRIx64
               "\n",
               tail->frozen_cpu[0], tail->frozen_cpu[1],
               tail->frozen_cpu[2], tail->frozen_cpu[3],
               tail->frozen_cpu[4], tail->frozen_cpu[5],
               tail->frozen_cpu[6], tail->frozen_cpu[7],
               tail->frozen_cpu[8], tail->frozen_cpu[9],
               tail->frozen_cpu[10], tail->frozen_cpu[11],
               tail->frozen_cpu[12], tail->frozen_cpu[13]);
    }
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
    printf("debug_memory_reads=%" PRIu64 " writes=%" PRIu64 "\n",
           tail->memory_reads, tail->memory_writes);
    if (tail->has_last_memory_write) {
        printf("debug_last_memory_write_sequence=%" PRIu64
               " address=%05" PRIx64 " width=%u value=%" PRIx64,
               tail->last_memory_write.sequence,
               tail->last_memory_write.value.memory.address,
               (unsigned int) tail->last_memory_write.value.memory.width,
               tail->last_memory_write.value.memory.value);
        if (tail->has_last_memory_write_source) {
            printf(" source=%04x:%04x/%02x",
                   tail->last_memory_write_source.value.instruction.cs,
                   tail->last_memory_write_source.value.instruction.ip,
                   tail->last_memory_write_source.value.instruction.opcode);
        }
        putchar('\n');
    } else {
        puts("debug_last_memory_write=none");
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
                   " opcode=%02x effective=%02x prefixes=%u"
                   " ax=%04x bx=%04x cx=%04x dx=%04x"
                   " sp=%04x bp=%04x si=%04x di=%04x"
                   " ds=%04x es=%04x ss=%04x flags=%04x\n",
                   event->sequence, event->value.instruction.cs,
                   event->value.instruction.ip,
                   event->value.instruction.physical_address,
                   event->value.instruction.opcode,
                   event->value.instruction.effective_opcode,
                   (unsigned int) event->value.instruction.prefix_count,
                   event->value.instruction.ax,
                   event->value.instruction.bx,
                   event->value.instruction.cx,
                   event->value.instruction.dx,
                   event->value.instruction.sp,
                   event->value.instruction.bp,
                   event->value.instruction.si,
                   event->value.instruction.di,
                   event->value.instruction.ds,
                   event->value.instruction.es,
                   event->value.instruction.ss,
                   event->value.instruction.flags);
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
        } else if (event->kind == BM_FRONTEND_DEBUG_MEMORY) {
            printf("debug sequence=%" PRIu64
                   " kind=memory direction=%s address=%05" PRIx64
                   " width=%u value=%" PRIx64 "\n",
                   event->sequence,
                   event->value.memory.write ? "write" : "read",
                   event->value.memory.address,
                   (unsigned int) event->value.memory.width,
                   event->value.memory.value);
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

static int
write_bytes(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = open_output(path);
    if (file == NULL)
        return 0;
    if (fwrite(data, 1U, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int
input_file_exists(const char *path, int *exists)
{
    FILE *file = NULL;
    if ((path == NULL) || (exists == NULL))
        return 0;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0)
        file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (file != NULL) {
        *exists = 1;
        return fclose(file) == 0;
    }
    if (errno == ENOENT) {
        *exists = 0;
        return 1;
    }
    return 0;
}

int
headless_run_machine(const bm_frontend_adapter_t *adapter,
                     const headless_run_options_t *options)
{
    bm_frontend_asset_binding_t bindings[4];
    bm_frontend_persistent_state_binding_t state_binding;
    size_t binding_count = 2U;
    size_t state_binding_count = 0U;
    bm_host_services_t host = bm_null_host_services();
    bm_frontend_blob_t even = { NULL, 0U };
    bm_frontend_blob_t odd = { NULL, 0U };
    bm_frontend_blob_t persistent_state_blob = { NULL, 0U };
    bm_frontend_readonly_media_t floppy = { 0 };
    bm_frontend_readonly_media_t swap_floppy = { 0 };
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
    uint8_t *depleted_state = NULL;
    int frame_matches = !options->expect_frame_crc32;
    int frame_written = options->frame_path == NULL;
    int persistent_state_saved = 1;
    int memory_image_written = options->trace_memory_image_path == NULL;
    int result = 3;
    floppy_swap_context_t swap_context = { 0 };

    status = headless_input_schedule_validate(
        options->text_actions, options->text_action_count,
        options->key_actions, options->key_action_count, options->key_ticks,
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
    if ((options->persistent_state_path != NULL) ||
        (options->depleted_state_role[0] != '\0')) {
        const char *role = options->persistent_state_path != NULL ?
            options->persistent_state_role : options->depleted_state_role;
        const bm_frontend_persistent_state_requirement_t *requirements;
        const bm_frontend_persistent_state_requirement_t *requirement = NULL;
        size_t requirement_count = 0U;
        size_t index;
        requirements = bm_frontend_adapter_persistent_states(
            adapter, &requirement_count);
        for (index = 0U; index < requirement_count; ++index) {
            if (strcmp(requirements[index].role, role) == 0)
                requirement = &requirements[index];
        }
        if (requirement == NULL) {
            fprintf(stderr, "unknown persistent-state role: %s\n", role);
            result = 2;
            goto cleanup;
        }
        if (options->persistent_state_path != NULL) {
            int exists = 0;
            if (!input_file_exists(options->persistent_state_path, &exists)) {
                fputs("persistent-state file is not readable\n", stderr);
                result = 2;
                goto cleanup;
            }
            if (exists) {
                if (!bm_frontend_blob_read_exact(
                        options->persistent_state_path, requirement->size,
                        &persistent_state_blob)) {
                    fputs("persistent-state file has the wrong size\n", stderr);
                    result = 2;
                    goto cleanup;
                }
                state_binding = (bm_frontend_persistent_state_binding_t) {
                    requirement->role, persistent_state_blob.data,
                    persistent_state_blob.size
                };
                state_binding_count = 1U;
            }
        } else {
            depleted_state = calloc(requirement->size, 1U);
            if (depleted_state == NULL) {
                result = 2;
                goto cleanup;
            }
            state_binding = (bm_frontend_persistent_state_binding_t) {
                requirement->role, depleted_state, requirement->size
            };
            state_binding_count = 1U;
        }
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
    if (options->swap_floppy_path != NULL) {
        if (!bm_frontend_readonly_media_open(options->swap_floppy_path, 512U,
                                             &swap_floppy) ||
            ((swap_floppy.size != 737280U) &&
             (swap_floppy.size != 1474560U))) {
            fputs("replacement floppy image must be raw 720 KiB or 1.44 MiB\n",
                  stderr);
            result = 2;
            goto cleanup;
        }
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
    status = bm_frontend_machine_open_with_persistent_state(
        adapter, bindings, binding_count,
        state_binding_count != 0U ? &state_binding : NULL,
        state_binding_count, &machine);
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
        debug_tail.include_memory = options->trace_memory;
        debug_tail.memory_first = options->trace_memory_first;
        debug_tail.memory_last = options->trace_memory_last;
        if (options->trace_memory_image_path != NULL) {
            debug_tail.memory_image_size =
                (size_t) ((uint64_t) options->trace_memory_last -
                          options->trace_memory_first + 1U);
            debug_tail.memory_image = calloc(debug_tail.memory_image_size, 1U);
            if (debug_tail.memory_image == NULL) {
                status = BM_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
        }
        debug_tail.freeze_at = options->freeze_trace_at;
        debug_tail.freeze_cs = options->freeze_trace_cs;
        debug_tail.freeze_ip = options->freeze_trace_ip;
        debug_tail.freeze_at_physical = options->freeze_trace_at_physical;
        debug_tail.freeze_physical = options->freeze_trace_physical;
        debug_tail.freeze_after_sequence = options->freeze_trace_after_sequence;
        debug_tail.freeze_sequence = options->freeze_trace_sequence;
        debug_tail.memory_only = options->trace_only_memory;
        debug_tail.memory_writes_only = options->trace_only_memory_writes;
        debug_tail.io_only = options->trace_only_io;
        if (debug_tail.memory_writes_only)
            debug_tail.memory_only = 1;
        status = bm_frontend_machine_set_debug_observer(
            machine, capture_debug_event, &debug_tail);
        if (status != BM_STATUS_OK)
            goto cleanup;
    }
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session,
                                      bm_frontend_machine_config(machine));
    debug_tail.session = session;
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    if (status == BM_STATUS_OK)
        swap_context = (floppy_swap_context_t) { session, &swap_floppy };
    if (status == BM_STATUS_OK)
        status = headless_run_input_schedule_with_action(
            session, options->text_actions, options->text_action_count,
            options->key_actions, options->key_action_count,
            options->key_ticks, options->swap_floppy_at,
            options->swap_floppy_path != NULL ? replace_floppy : NULL,
            &swap_context);
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
    if ((session != NULL) &&
        ((status != BM_STATUS_OK) || (options->trace_tail != 0U))) {
        uint64_t bytes = 0U, length = 0U, dx = 0U;
        if (bm_session_inspect_cpu(session, 0, "last_instruction_bytes", &bytes) == BM_STATUS_OK &&
            bm_session_inspect_cpu(session, 0, "last_instruction_length", &length) == BM_STATUS_OK) {
            (void) bm_session_inspect_cpu(session, 0, "dx", &dx);
            fprintf(stderr, "%s_instruction_bytes_le=%016" PRIx64
                    " length=%" PRIu64 " dx=%04" PRIx64 "\n",
                    status != BM_STATUS_OK ? "failure" : "debug",
                    bytes, length, dx);
        }
        {
            static const char *const names[] = {
                "ax", "bx", "cx", "dx", "sp", "bp", "si", "di",
                "cs", "ds", "es", "ss", "ip", "flags"
            };
            size_t index;
            fputs(status != BM_STATUS_OK ? "failure_cpu" : "debug_cpu", stderr);
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
    if (session != NULL) {
        uint64_t seconds = 0U, minutes = 0U, hours = 0U;
        uint64_t day_of_week = 0U, day_of_month = 0U, month = 0U;
        if ((bm_session_inspect_machine(session, "rtc_seconds", &seconds) ==
             BM_STATUS_OK) &&
            (bm_session_inspect_machine(session, "rtc_minutes", &minutes) ==
             BM_STATUS_OK) &&
            (bm_session_inspect_machine(session, "rtc_hours", &hours) ==
             BM_STATUS_OK) &&
            (bm_session_inspect_machine(session, "rtc_day_of_week", &day_of_week) ==
             BM_STATUS_OK) &&
            (bm_session_inspect_machine(session, "rtc_day_of_month", &day_of_month) ==
             BM_STATUS_OK) &&
            (bm_session_inspect_machine(session, "rtc_month", &month) ==
             BM_STATUS_OK))
            printf("rtc_bcd=%02" PRIx64 ":%02" PRIx64 ":%02" PRIx64
                   " dow=%02" PRIx64 " date=%02" PRIx64 "-%02" PRIx64 "\n",
                   hours, minutes, seconds, day_of_week, month, day_of_month);
    }
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
    if ((options->text_action_count != 0U) ||
        (options->key_action_count != 0U))
        printf("text_actions=%zu key_actions=%zu key_ticks=%" PRIu64 "\n",
               options->text_action_count, options->key_action_count,
               options->key_ticks);
    if (debug_tail.capacity != 0U)
        print_debug_tail(&debug_tail);
    if (options->trace_memory_image_path != NULL) {
        FILE *memory_image = open_output(options->trace_memory_image_path);
        if (memory_image != NULL) {
            const int complete =
                fwrite(debug_tail.memory_image, 1U,
                       debug_tail.memory_image_size, memory_image) ==
                debug_tail.memory_image_size;
            memory_image_written = complete && (fclose(memory_image) == 0);
            if (!complete)
                (void) fclose(memory_image);
        }
        if (!memory_image_written)
            fputs("could not write memory trace image\n", stderr);
    }
    if (options->expect_frame_crc32)
        printf("expected_frame_crc32=%08" PRIx32 " matched=%d\n",
               options->expected_frame_crc32, frame_matches);
    if (!frame_written)
        fputs("could not write framebuffer capture\n", stderr);
    if ((options->persistent_state_path != NULL) && (session != NULL)) {
        size_t state_size = 0U;
        uint8_t *saved_state = NULL;
        bm_status_t state_status = bm_session_persistent_state_size(
            session, options->persistent_state_role, &state_size);
        if (state_status == BM_STATUS_OK)
            saved_state = malloc(state_size);
        if ((saved_state == NULL) ||
            (bm_session_save_persistent_state(
                 session, options->persistent_state_role,
                 saved_state, state_size) != BM_STATUS_OK) ||
            !write_bytes(options->persistent_state_path,
                         saved_state, state_size)) {
            fputs("could not save persistent state\n", stderr);
            persistent_state_saved = 0;
        }
        free(saved_state);
    }
    if ((status == BM_STATUS_OK) && (video_status == BM_STATUS_OK) &&
        frame_written && memory_image_written && frame_matches &&
        persistent_state_saved)
        result = 0;

cleanup:
    bm_session_destroy(session);
    free(pixels);
    free(debug_tail.events);
    free(debug_tail.memory_image);
    bm_frontend_machine_close(machine);
    bm_frontend_readonly_media_close(&floppy);
    bm_frontend_readonly_media_close(&swap_floppy);
    bm_frontend_readonly_media_close(&hard_disk);
    bm_frontend_blob_release(&even);
    bm_frontend_blob_release(&odd);
    bm_frontend_blob_release(&persistent_state_blob);
    free(depleted_state);
    return result;
}
