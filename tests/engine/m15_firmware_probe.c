/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_m15.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct probe_trace {
    bm_808x_trace_t last;
    uint64_t instructions;
    uint64_t bios_error_path;
    uint16_t pit_count[3];
    uint8_t pit_seen[3];
    uint8_t pit_failure_channel;
    uint8_t pit_passed;
    uint32_t diagnostic_source;
    uint16_t diagnostic_si;
    uint16_t diagnostic_ax;
    uint64_t diagnostic_instruction;
} probe_trace_t;

typedef struct probe_media {
    uint8_t *bytes;
    size_t size;
    uint64_t read_operations;
} probe_media_t;

static void
capture_instruction(void *context, const bm_808x_trace_t *trace)
{
    probe_trace_t *probe = context;
    if ((trace->physical_address == 0xfcce0U) &&
        (probe->diagnostic_instruction == 0U)) {
        probe->diagnostic_source = probe->last.physical_address;
        probe->diagnostic_si = trace->si;
        probe->diagnostic_ax = trace->ax;
        probe->diagnostic_instruction = probe->instructions + 1U;
    }
    probe->last = *trace;
    ++probe->instructions;
    /* Addresses are diagnostic landmarks in the local BIOS 1.08, not a
     * portable machine contract. Keep the trace bounded in memory. */
    if ((trace->physical_address == 0xfcdb0U) &&
        (probe->bios_error_path == 0U))
        probe->bios_error_path = probe->instructions;
    if ((trace->physical_address == 0xfc293U) &&
        (trace->dx >= 0x40U) && (trace->dx <= 0x42U)) {
        unsigned int channel = trace->dx - 0x40U;
        probe->pit_count[channel] = trace->ax;
        probe->pit_seen[channel] = 1U;
    }
    if (trace->physical_address == 0xfc2b1U)
        probe->pit_failure_channel = (uint8_t) (trace->dx - 0x40U + 1U);
    if (trace->physical_address == 0xfc2aeU)
        probe->pit_passed = 1U;
}

static uint8_t *
read_exact_file(const char *path, size_t size)
{
    uint8_t *bytes = malloc(size);
    FILE *file;
    size_t count;
    int extra;
    if (bytes == NULL)
        return NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0)
        file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        free(bytes);
        return NULL;
    }
    count = fread(bytes, 1U, size, file);
    extra = fgetc(file);
    fclose(file);
    if ((count != size) || (extra != EOF)) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static bm_status_t
media_read(void *context, uint64_t first_block, uint32_t block_count,
           uint8_t *destination)
{
    probe_media_t *media = context;
    uint64_t offset;
    uint64_t count;
    if ((destination == NULL) || (first_block > media->size / 512U) ||
        (block_count > media->size / 512U - first_block))
        return BM_STATUS_INVALID_ARGUMENT;
    offset = first_block * 512U;
    count = (uint64_t) block_count * 512U;
    memcpy(destination, media->bytes + (size_t) offset, (size_t) count);
    ++media->read_operations;
    return BM_STATUS_OK;
}

static uint64_t
inspect(const bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    (void) bm_session_inspect_cpu(session, 0U, name, &value);
    return value;
}

static int
write_ppm(const char *path, const bm_video_framebuffer_t *frame)
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
                frame->geometry.width, frame->geometry.height) < 0) {
        fclose(file);
        return 0;
    }
    for (y = 0U; y < frame->geometry.height; ++y) {
        uint32_t x;
        const uint32_t *line = frame->pixels + (size_t) y * frame->stride;
        for (x = 0U; x < frame->geometry.width; ++x) {
            uint8_t rgb[3] = {
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
main(int argc, char **argv)
{
    uint8_t *firmware;
    probe_media_t disk = { NULL, 0U, 0U };
    bm_host_services_t host = bm_null_host_services();
    bm_m15_config_t config = { 0 };
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    bm_status_t status;
    probe_trace_t probe = { 0 };
    const char *no_trace = getenv("BLUMACH_M15_PROBE_NO_TRACE");
    int trace_enabled = (no_trace == NULL) || (strcmp(no_trace, "1") != 0);
    uint64_t duration = UINT64_C(100000000);
    size_t index;

    if ((argc < 2) || (argc > 7)) {
        fprintf(stderr, "usage: %s <local-firmware-64k>"
                        " [nanoseconds [ram-kib:256|512"
                        " [local-floppy-720k [frame.ppm [f1]]]]]\n",
                argv[0]);
        return 2;
    }
    if ((argc == 7) && (strcmp(argv[6], "f1") != 0)) {
        fputs("post-run key must be f1\n", stderr);
        return 2;
    }
    if ((argc >= 4) && (strcmp(argv[3], "256") != 0) &&
        (strcmp(argv[3], "512") != 0)) {
        fputs("ram-kib must be 256 or 512\n", stderr);
        return 2;
    }
    if (argc >= 3) {
        char *end = NULL;
        unsigned long long parsed = strtoull(argv[2], &end, 10);
        if ((argv[2][0] == '\0') || (*end != '\0') || (parsed == 0U)) {
            fputs("nanoseconds must be positive decimal\n", stderr);
            return 2;
        }
        duration = (uint64_t) parsed;
    }
    firmware = read_exact_file(argv[1], BM_M15_FIRMWARE_SIZE);
    if (firmware == NULL) {
        fputs("firmware must be a readable, exact 65536-byte file\n", stderr);
        return 2;
    }
    config.firmware = (bm_blob_view_t) {
        "local-m15-firmware", firmware, BM_M15_FIRMWARE_SIZE, NULL
    };
    if (argc >= 5) {
        disk.size = 737280U;
        disk.bytes = read_exact_file(argv[4], disk.size);
        if (disk.bytes == NULL) {
            fputs("floppy must be a readable, exact 737280-byte file\n", stderr);
            free(firmware);
            return 2;
        }
    }
    config.ram_kib = (argc >= 4 && strcmp(argv[3], "256") == 0) ? 256U : 512U;
    config.startup_display_switches = 0x20U;
    /* The traced probe is for BIOS landmarks. Opt out when measuring raw
     * engine throughput so an instruction callback does not skew the result. */
    config.trace = trace_enabled ? capture_instruction : NULL;
    config.trace_context = trace_enabled ? &probe : NULL;
    for (index = 0U; index < 2U; ++index) {
        config.floppy[index] = (bm_floppy_drive_config_t) {
            .installed = 1, .geometry = { 80U, 2U, 9U, 512U }
        };
    }
    if (disk.bytes != NULL) {
        config.floppy[0].media_present = 1;
        config.floppy[0].write_protected = 1;
        config.floppy[0].media = (bm_block_media_t) {
            &disk, disk.size / 512U, 512U, 1, media_read, NULL
        };
    }
    machine = bm_m15_machine_config(&config);
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session, &machine);
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    if (status == BM_STATUS_OK)
        status = bm_session_run_for(session, duration);
    if ((status == BM_STATUS_OK) && (argc == 7)) {
        bm_input_event_t key = { 0 };
        key.kind = BM_INPUT_KEY;
        key.key = BM_KEY_F1;
        key.pressed = 1;
        status = bm_session_send_input(session, &key);
        if (status == BM_STATUS_OK) {
            key.pressed = 0;
            status = bm_session_send_input(session, &key);
        }
        if (status == BM_STATUS_OK)
            status = bm_session_run_for(session, UINT64_C(20000000000));
        printf("f1_after_initial_run_status=%d\n", (int) status);
    }
    if ((status == BM_STATUS_OK) && (probe.bios_error_path != 0U)) {
        bm_input_event_t escape = { 0 };
        escape.kind = BM_INPUT_KEY;
        escape.key = BM_KEY_ESCAPE;
        escape.pressed = 1;
        status = bm_session_send_input(session, &escape);
        if (status == BM_STATUS_OK)
            status = bm_session_run_for(session, UINT64_C(5000000));
        printf("escape_after_error_status=%d cs=%04" PRIx64
               " ip=%04" PRIx64 " halted=%" PRIu64 "\n",
               (int) status, inspect(session, "cs"), inspect(session, "ip"),
               inspect(session, "halted"));
    }
    if (session != NULL) {
        uint64_t keyboard_latch = 0U;
        uint64_t keyboard_full = 0U;
        uint64_t pic_requests = 0U;
        bm_video_geometry_t geometry = { 0 };
        bm_status_t video_status = bm_session_video_geometry(session, &geometry);
        size_t nonblack = 0U;
        if (video_status == BM_STATUS_OK) {
            size_t pixel_count = (size_t) geometry.width * geometry.height;
            uint32_t *pixels = calloc(pixel_count, sizeof(*pixels));
            if (pixels == NULL) {
                video_status = BM_STATUS_OUT_OF_MEMORY;
            } else {
                bm_video_framebuffer_t frame = {
                    pixels, pixel_count, geometry.width, geometry
                };
                video_status = bm_session_render_video(session, &frame);
                if (video_status == BM_STATUS_OK) {
                    for (index = 0U; index < pixel_count; ++index) {
                        if (pixels[index] != UINT32_C(0xff000000))
                            ++nonblack;
                    }
                    if ((argc >= 6) && !write_ppm(argv[5], &frame))
                        video_status = BM_STATUS_DEVICE_ERROR;
                }
                free(pixels);
            }
        }
        (void) bm_session_inspect_machine(session, "keyboard_latch", &keyboard_latch);
        (void) bm_session_inspect_machine(session, "keyboard_latch_full", &keyboard_full);
        (void) bm_session_inspect_machine(session, "pic_irq_requests", &pic_requests);
        printf("status=%d time_ns=%" PRIu64 " instructions=%" PRIu64
               " last=%04x:%04x physical=%05" PRIx32 " opcode=%02x"
               " cs=%04" PRIx64 " ip=%04" PRIx64 " ax=%04" PRIx64
               " halted=%" PRIu64 " error_path_at=%" PRIu64
               " keyboard_latch=%02" PRIx64 " full=%" PRIu64
               " pic_requests=%02" PRIx64 " video_status=%d"
               " width=%" PRIu32 " height=%" PRIu32
               " nonblack=%zu\n",
               (int) status, bm_session_time(session), probe.instructions,
               probe.last.cs, probe.last.ip, probe.last.physical_address,
               probe.last.opcode, inspect(session, "cs"), inspect(session, "ip"),
               inspect(session, "ax"), inspect(session, "halted"),
               probe.bios_error_path, keyboard_latch, keyboard_full,
               pic_requests, (int) video_status, geometry.width,
               geometry.height, nonblack);
        printf("pit_ch0=%s%04x pit_ch1=%s%04x pit_ch2=%s%04x"
               " pit_failure_channel=%u pit_passed=%u\n",
               probe.pit_seen[0] ? "" : "unseen:", probe.pit_count[0],
               probe.pit_seen[1] ? "" : "unseen:", probe.pit_count[1],
               probe.pit_seen[2] ? "" : "unseen:", probe.pit_count[2],
               probe.pit_failure_channel, probe.pit_passed);
        printf("diagnostic_at=%" PRIu64 " source=%05" PRIx32
               " si=%04x ax=%04x\n",
               probe.diagnostic_instruction, probe.diagnostic_source,
               probe.diagnostic_si, probe.diagnostic_ax);
        printf("floppy_bytes=%zu read_only=%u reads=%" PRIu64 "\n",
               disk.size, disk.bytes != NULL ? 1U : 0U,
               disk.read_operations);
        printf("trace_enabled=%d\n", trace_enabled);
        if ((argc >= 6) && (video_status != BM_STATUS_OK) &&
            (status == BM_STATUS_OK))
            status = video_status;
    }
    bm_session_destroy(session);
    free(disk.bytes);
    free(firmware);
    return status == BM_STATUS_OK ? 0 : 1;
}
