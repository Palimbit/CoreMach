/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct probe_media {
    uint8_t *bytes;
    size_t size;
} probe_media_t;

typedef struct probe_trace {
    bm_808x_trace_t last;
    uint64_t instructions;
    uint64_t io_operations;
    uint32_t diagnostic_paths;
    bm_session_t *session;
    uint16_t timer0_failure_cx;
    uint64_t timer_irq_handler_instruction;
    uint64_t timer0_program_instruction;
    uint64_t timer0_irq_instruction;
    uint16_t timer0_failure_count;
    uint8_t timer0_failure_output;
    uint8_t timer0_failure_pic_pending;
    uint8_t timer0_failure_pic_mask;
    uint8_t timer0_failure_pic_requests;
    uint8_t timer0_failure_pic_in_service;
    uint8_t timer0_failure_pic_lines;
    uint64_t timing_boundaries;
    uint64_t timing_complete;
    uint64_t timing_boundary_exact;
    uint64_t timing_boundary_range;
    uint64_t timing_boundary_unknown;
    uint64_t timing_unknown_by_opcode[256];
    bm_808x_timing_observation_t first_timing_unknown;
    bm_808x_trace_t first_timing_unknown_trace;
    int has_first_timing_unknown;
} probe_trace_t;

static void
capture_instruction(void *context, const bm_808x_trace_t *trace)
{
    probe_trace_t *probe = context;
    probe->last = *trace;
    ++probe->instructions;
    if ((trace->physical_address == 0xff100U) &&
        (probe->timer_irq_handler_instruction == 0U))
        probe->timer_irq_handler_instruction = probe->instructions;
    if (trace->physical_address == 0xf03e0U)
        probe->timer0_program_instruction = probe->instructions;
    if ((trace->physical_address == 0xff100U) &&
        (probe->timer0_program_instruction != 0U) &&
        (probe->timer0_irq_instruction == 0U))
        probe->timer0_irq_instruction = probe->instructions;
    if (trace->physical_address == 0xf03f0U) {
        uint64_t cx = 0U;
        probe->diagnostic_paths |= 0x01U; /* Timer 0 timeout/range failure. */
        if ((probe->session != NULL) &&
            (bm_session_inspect_cpu(probe->session, 0, "cx", &cx) == BM_STATUS_OK))
            probe->timer0_failure_cx = (uint16_t) cx;
        if (probe->session != NULL) {
            uint64_t value = 0U;
            if (bm_session_inspect_machine(probe->session, "pit0_count", &value) == BM_STATUS_OK)
                probe->timer0_failure_count = (uint16_t) value;
            if (bm_session_inspect_machine(probe->session, "pit0_output", &value) == BM_STATUS_OK)
                probe->timer0_failure_output = (uint8_t) value;
            if (bm_session_inspect_machine(probe->session, "pic_pending", &value) == BM_STATUS_OK)
                probe->timer0_failure_pic_pending = (uint8_t) value;
            if (bm_session_inspect_machine(probe->session, "pic_mask", &value) == BM_STATUS_OK)
                probe->timer0_failure_pic_mask = (uint8_t) value;
            if (bm_session_inspect_machine(probe->session, "pic_requests", &value) == BM_STATUS_OK)
                probe->timer0_failure_pic_requests = (uint8_t) value;
            if (bm_session_inspect_machine(probe->session, "pic_in_service", &value) == BM_STATUS_OK)
                probe->timer0_failure_pic_in_service = (uint8_t) value;
            if (bm_session_inspect_machine(probe->session, "pic_lines", &value) == BM_STATUS_OK)
                probe->timer0_failure_pic_lines = (uint8_t) value;
        }
    } else if (trace->physical_address == 0xf043bU)
        probe->diagnostic_paths |= 0x02U; /* PIT channel 1 readback failure. */
    else if (trace->physical_address == 0xf047bU)
        probe->diagnostic_paths |= 0x04U; /* PIT channel 2 readback failure. */
    else if (trace->physical_address == 0xf048cU)
        probe->diagnostic_paths |= 0x08U; /* PIT diagnostic success path. */
}

static void
capture_io(void *context, const bm_pcs86_io_trace_t *trace)
{
    probe_trace_t *probe = context;
    (void) trace;
    ++probe->io_operations;
}

static void
capture_timing(void *context,
               const bm_808x_timing_observation_t *observation)
{
    probe_trace_t *probe = context;

    if (observation->kind != BM_808X_BOUNDARY_INSTRUCTION)
        return;
    ++probe->timing_boundaries;
    if (observation->execution_timeline_complete)
        ++probe->timing_complete;
    if (observation->boundary_clock_kind == BM_808X_EXECUTION_CLOCKS_EXACT) {
        ++probe->timing_boundary_exact;
    } else if (observation->boundary_clock_kind ==
               BM_808X_EXECUTION_CLOCKS_RANGE) {
        ++probe->timing_boundary_range;
    } else {
        ++probe->timing_boundary_unknown;
        ++probe->timing_unknown_by_opcode[observation->effective_opcode];
        if (!probe->has_first_timing_unknown) {
            probe->first_timing_unknown = *observation;
            probe->first_timing_unknown_trace = probe->last;
            probe->has_first_timing_unknown = 1;
        }
    }
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

static uint8_t *
read_disk_image(const char *path, size_t *size)
{
    FILE *file;
    long length;
    uint8_t *data;

    *size = 0U;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0)
        file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL)
        return NULL;
    if ((fseek(file, 0L, SEEK_END) != 0) || ((length = ftell(file)) <= 0L) ||
        (fseek(file, 0L, SEEK_SET) != 0)) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t) length);
    if ((data == NULL) ||
        (fread(data, 1U, (size_t) length, file) != (size_t) length)) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t) length;
    return data;
}

static bm_status_t
probe_media_read(void *context, uint64_t first_block, uint32_t block_count,
                 uint8_t *destination)
{
    probe_media_t *media = context;
    size_t offset = (size_t) first_block * 512U;
    size_t count = (size_t) block_count * 512U;

    if ((destination == NULL) || (offset > media->size) ||
        (count > media->size - offset))
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(destination, media->bytes + offset, count);
    return BM_STATUS_OK;
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
    probe_trace_t probe = { 0 };
    uint8_t *even;
    uint8_t *odd;
    probe_media_t disk = { NULL, 0U };
    bm_status_t status;
    uint64_t ax = 0;
    uint64_t dx = 0;
    uint64_t instruction_bytes = 0;
    uint64_t instruction_length = 0;
    bm_video_geometry_t geometry = { 0, 0, BM_PIXEL_XRGB8888, 0U, 0U };
    bm_status_t video_status = BM_STATUS_INVALID_STATE;
    uint32_t *pixels = NULL;
    size_t pixel_count = 0;
    size_t nonblack = 0;
    uint32_t frame_crc = 0;
    uint64_t run_nanoseconds = UINT64_C(5000000000);
    uint64_t fdc_dor = 0U;
    uint64_t fdc_msr = 0U;
    uint64_t fdc_irq = 0U;
    uint64_t crtc_cursor_start = 0U;
    uint64_t crtc_cursor_end = 0U;
    uint64_t crtc_cursor_high = 0U;
    uint64_t crtc_cursor_low = 0U;
    uint64_t crtc_start_high = 0U;
    uint64_t crtc_start_low = 0U;
    uint64_t crtc_max_scan_line = 0U;
    uint64_t crtc_offset = 0U;
    uint64_t pvga_pr3 = 0U;

    if ((argc < 3) || (argc > 6)) {
        fprintf(stderr, "usage: %s <even-rom> <odd-rom>"
                        " [frame.ppm [nanoseconds [floppy.img]]]\n", argv[0]);
        return 2;
    }
    if (argc >= 5) {
        char *end = NULL;
        unsigned long long parsed = strtoull(argv[4], &end, 10);
        if ((argv[4][0] == '\0') || (end == NULL) || (*end != '\0') || (parsed == 0U)) {
            fputs("nanoseconds must be a positive decimal integer\n", stderr);
            return 2;
        }
        run_nanoseconds = (uint64_t) parsed;
    }
    even = read_firmware(argv[1]);
    odd = read_firmware(argv[2]);
    if ((even == NULL) || (odd == NULL)) {
        fputs("firmware halves must each be exactly 32768 bytes\n", stderr);
        free(even);
        free(odd);
        return 2;
    }
    if (argc == 6) {
        disk.bytes = read_disk_image(argv[5], &disk.size);
        if ((disk.bytes == NULL) ||
            ((disk.size != 737280U) && (disk.size != 1474560U))) {
            fputs("floppy image must be a raw 720 KiB or 1.44 MiB image\n",
                  stderr);
            free(disk.bytes);
            free(even);
            free(odd);
            return 2;
        }
    }
    config = (bm_pcs86_config_t) {
        .firmware_even = {
            "bios-even-109", even, BM_PCS86_FIRMWARE_HALF_SIZE, NULL
        },
        .firmware_odd = {
            "bios-odd-109", odd, BM_PCS86_FIRMWARE_HALF_SIZE, NULL
        },
        .trace = capture_instruction,
        .trace_context = &probe,
        .timing = capture_timing,
        .timing_context = &probe,
        .io_trace = capture_io,
        .io_trace_context = &probe
    };
    if (disk.bytes != NULL) {
        config.floppy[0] = (bm_floppy_drive_config_t) {
            1, 1, 1,
            { 80U, 2U, (uint8_t) (disk.size == 737280U ? 9U : 18U), 512U },
            { &disk, disk.size / 512U, 512U, 1, probe_media_read, NULL }
        };
    }
    machine = bm_pcs86_machine_config(&config);
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session, &machine);
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    probe.session = session;
    if (status == BM_STATUS_OK)
        status = bm_session_run_for(session, run_nanoseconds);

    if (session != NULL) {
        (void) bm_session_inspect_cpu(session, 0, "ax", &ax);
        (void) bm_session_inspect_cpu(session, 0, "dx", &dx);
        (void) bm_session_inspect_cpu(session, 0, "last_instruction_bytes",
                                      &instruction_bytes);
        (void) bm_session_inspect_cpu(session, 0, "last_instruction_length",
                                      &instruction_length);
        (void) bm_session_inspect_machine(session, "fdc_dor", &fdc_dor);
        (void) bm_session_inspect_machine(session, "fdc_msr", &fdc_msr);
        (void) bm_session_inspect_machine(session, "fdc_irq", &fdc_irq);
        (void) bm_session_inspect_machine(session, "video_crtc_cursor_start",
                                          &crtc_cursor_start);
        (void) bm_session_inspect_machine(session, "video_crtc_cursor_end",
                                          &crtc_cursor_end);
        (void) bm_session_inspect_machine(session, "video_crtc_cursor_high",
                                          &crtc_cursor_high);
        (void) bm_session_inspect_machine(session, "video_crtc_cursor_low",
                                          &crtc_cursor_low);
        (void) bm_session_inspect_machine(session, "video_crtc_start_high",
                                          &crtc_start_high);
        (void) bm_session_inspect_machine(session, "video_crtc_start_low",
                                          &crtc_start_low);
        (void) bm_session_inspect_machine(session, "video_crtc_max_scan_line",
                                          &crtc_max_scan_line);
        (void) bm_session_inspect_machine(session, "video_crtc_offset",
                                          &crtc_offset);
        (void) bm_session_inspect_machine(session, "video_pvga_pr3",
                                          &pvga_pr3);
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
                    if ((argc >= 4) && !write_ppm(argv[3], &framebuffer)) {
                        fputs("could not write framebuffer capture\n", stderr);
                        video_status = BM_STATUS_DEVICE_ERROR;
                    }
                }
            }
        }
    }

    printf("status=%d instructions=%" PRIu64 " io=%" PRIu64
           " time_ns=%" PRIu64
           " last=%04x:%04x physical=%05" PRIx32
           " opcode=%02x effective=%02x prefixes=%u bytes=",
           (int) status, probe.instructions, probe.io_operations,
           bm_session_time(session),
           probe.last.cs, probe.last.ip, probe.last.physical_address,
           probe.last.opcode, probe.last.effective_opcode, probe.last.prefix_count);
    {
        uint64_t index;
        uint64_t captured = instruction_length < 8U ? instruction_length : 8U;
        for (index = 0U; index < captured; ++index)
            printf("%s%02" PRIx64, index == 0U ? "" : ",",
                   (instruction_bytes >> (index * 8U)) & 0xffU);
        if (instruction_length > captured)
            printf(",...(%" PRIu64 " bytes)", instruction_length);
    }
    printf(" ax=%04" PRIx64 " dx=%04" PRIx64 "\n", ax, dx);
    printf("diagnostic_paths=%02" PRIx32 " timer0_failure_cx=%04x"
           " timer_program_at=%" PRIu64 " timer_irq_at=%" PRIu64
           " timer0_irq_at=%" PRIu64 "\n",
           probe.diagnostic_paths, probe.timer0_failure_cx,
           probe.timer0_program_instruction,
           probe.timer_irq_handler_instruction, probe.timer0_irq_instruction);
    printf("timer0_failure_count=%04x output=%u pic_pending=%u"
           " mask=%02x requests=%02x in_service=%02x lines=%02x\n",
           probe.timer0_failure_count, probe.timer0_failure_output,
           probe.timer0_failure_pic_pending, probe.timer0_failure_pic_mask,
           probe.timer0_failure_pic_requests, probe.timer0_failure_pic_in_service,
           probe.timer0_failure_pic_lines);
    printf("timing_boundaries=%" PRIu64 " complete=%" PRIu64
           " exact=%" PRIu64 " range=%" PRIu64 " unknown=%" PRIu64 "\n",
           probe.timing_boundaries, probe.timing_complete,
           probe.timing_boundary_exact, probe.timing_boundary_range,
           probe.timing_boundary_unknown);
    fputs("timing_unknown_opcodes=", stdout);
    {
        unsigned int opcode;
        int first = 1;
        for (opcode = 0U; opcode < 256U; ++opcode) {
            if (probe.timing_unknown_by_opcode[opcode] == 0U)
                continue;
            printf("%s%02x:%" PRIu64, first ? "" : ",", opcode,
                   probe.timing_unknown_by_opcode[opcode]);
            first = 0;
        }
    }
    fputc('\n', stdout);
    if (probe.has_first_timing_unknown) {
        const bm_808x_timing_observation_t *unknown =
            &probe.first_timing_unknown;
        const bm_808x_trace_t *trace = &probe.first_timing_unknown_trace;
        printf("timing_first_unknown=%04x:%04x opcode=%02x effective=%02x"
               " execution_kind=%u execution=%" PRIu32 "..%" PRIu32
               " timeline=%u placed=%" PRIu32 " operand=%" PRIu64
               " prefetch=%" PRIu64 " total_bus=%" PRIu64
               " flushed=%u\n",
               trace->cs, trace->ip, trace->opcode, trace->effective_opcode,
               (unsigned int) unknown->execution_clock_kind,
               unknown->execution_clocks_min,
               unknown->execution_clocks_max,
               unknown->execution_timeline_complete,
               unknown->execution_clocks_placed,
               unknown->operand_transactions,
               unknown->prefetch_transactions,
               unknown->logical_bus_transactions,
               unknown->prefetch_queue_flushed);
    }
    printf("video_status=%d width=%" PRIu32 " height=%" PRIu32
           " nonblack=%zu crc32=%08" PRIx32 " capture=%s\n",
           (int) video_status, geometry.width, geometry.height,
           nonblack, frame_crc, (argc >= 4 && video_status == BM_STATUS_OK) ? argv[3] : "");
    printf("floppy_bytes=%zu fdc_dor=%02" PRIx64 " fdc_msr=%02" PRIx64
           " fdc_irq=%" PRIu64 "\n",
           disk.size, fdc_dor, fdc_msr, fdc_irq);
    printf("crtc_0a=%02" PRIx64 " crtc_0b=%02" PRIx64
           " crtc_0e=%02" PRIx64 " crtc_0f=%02" PRIx64
           " crtc_0c=%02" PRIx64 " crtc_0d=%02" PRIx64
           " crtc_09=%02" PRIx64 " crtc_13=%02" PRIx64
           " pvga_pr3=%02" PRIx64 "\n",
           crtc_cursor_start, crtc_cursor_end,
           crtc_cursor_high, crtc_cursor_low,
           crtc_start_high, crtc_start_low,
           crtc_max_scan_line, crtc_offset, pvga_pr3);

    bm_session_destroy(session);
    free(pixels);
    free(even);
    free(odd);
    free(disk.bytes);
    return (status == BM_STATUS_OK) ? 0 : 3;
}
