/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include <blumach/systems/olivetti_pcs286.h>
#include <blumach/platforms/null_host.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "composition.h"
#include <string.h>
#include <ctype.h>

static bm_status_t image_read(void *context, uint64_t first, uint32_t count, uint8_t *out)
{
    if (!out || first>2880 || count>2880-first) return BM_STATUS_INVALID_ARGUMENT;
    memcpy(out,(const uint8_t *)context+(size_t)first*512,(size_t)count*512);
    return BM_STATUS_OK;
}

static uint64_t sample_ns(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

/* Diagnostic wall-time attribution only: timer overhead perturbs these values.
 * This uses the shared board without the outer runtime scheduler. */
static int profile_board(const bm_host_services_t *host, const uint8_t *bios,
                         const bm_floppy_drive_config_t *drive, uint64_t count, int estimate,
                         const uint8_t *cmos, uint64_t warmup)
{
    bm_pcs286_composition_t *board = calloc(1, sizeof(*board));
    uint8_t calendar[128] = {[4]=1,[6]=3,[7]=1,[8]=1,[9]=0x80,[10]=0x60,[11]=0x82};
    uint64_t totals[4] = {0}, done = 0;
    uint64_t sync_totals[3]={0};
    uint64_t kinds[BM_286_BOUNDARY_REP_ITERATION + 1] = {0};
    uint64_t no_cpu = 0, resets = 0, unknown = 0, remainder = 0, manual = 0, fallback = 0;
    uint64_t start_guest=0, start_memory=0, start_io=0;
    bm_status_t status;
    if (!board) return 1;
    status = bm_pcs286_composition_initialize(board, host, bios, 1, 1, 1, 2,
                                             cmos ? cmos : calendar, 1, NULL, 0, drive);
    /* Same fixed peripheral timeline in both modes isolates estimator cost. */
    if (estimate) board->control.provisional_fallback = 12;
    if (estimate==2) board->profile_now=sample_ns;
    if (estimate==2) bm_pcs286_services_profile(board->services,sample_ns,sync_totals);
    for (; status == BM_STATUS_OK && done < count; ++done) {
        bm_pcs286_services_step_t event = {0};
        uint64_t clocks, t[5], ns=1000, elapsed=12;
        if (done==warmup) {
            memset(totals,0,sizeof(totals)); memset(kinds,0,sizeof(kinds));
            memset(sync_totals,0,sizeof(sync_totals));
            no_cpu=resets=unknown=manual=fallback=0;
            board->profile_memory_ns=board->profile_io_ns=0;
            start_guest=board->video_ns;
            start_memory=board->memory_calls; start_io=board->io_count;
            uint64_t cs=0,ip=0;
            (void)board->cpu.ops.inspect(board->cpu.context,"cs",&cs);
            (void)board->cpu.ops.inspect(board->cpu.context,"ip",&ip);
            printf("profile_start cs_ip=%04" PRIx64 ":%04" PRIx64 "\n",cs,ip);
        }
        t[0] = sample_ns();
        status = bm_pcs286_services_step(board->services, &event);
        t[1] = sample_ns();
        if (status != BM_STATUS_OK && status != BM_STATUS_IDLE) break;
        /* IDLE leaves the result unspecified. A successful services step may
         * also have serviced refresh without completing a CPU boundary. */
        if (status == BM_STATUS_IDLE || !event.cpu_completed) ++no_cpu;
        else if (event.cpu.kind == BM_PCS286_CONTROL_RESET) ++resets;
        else {
            if ((unsigned)event.cpu.cpu.kind > BM_286_BOUNDARY_REP_ITERATION) {
                status = BM_STATUS_DEVICE_ERROR; break;
            }
            ++kinds[event.cpu.cpu.kind];
            if (event.cpu.cpu.timing == BM_286_TIMING_UNKNOWN) ++unknown;
        }
        status = bm_pcs286_dma_coordinator_step(&board->dma_coordinator, &clocks);
        t[2] = sample_ns();
        if (status != BM_STATUS_OK && status != BM_STATUS_IDLE) break;
        if (estimate==2) {
            if (event.cpu_completed && event.cpu.estimate.estimated_clocks)
                elapsed=event.cpu.estimate.estimated_clocks;
            if (event.cpu_completed && event.cpu.estimate.source==BM_286_ESTIMATE_MANUAL) ++manual;
            else ++fallback;
            if ((clocks || event.refresh_completed) && elapsed<12) elapsed=12;
            ns=(elapsed*250+remainder)/3;
            remainder=(elapsed*250+remainder)%3;
        }
        status = bm_pcs286_services_advance(board->services, ns);
        t[3] = sample_ns();
        if (status != BM_STATUS_OK) break;
        status = bm_pvga1a_advance_ns(board->video, ns);
        t[4] = sample_ns();
        if (status != BM_STATUS_OK) break;
        for (size_t i = 0; i < 4; ++i) {
            if (t[i+1] < t[i] || !t[i]) { status = BM_STATUS_INVALID_STATE; break; }
            totals[i] += t[i+1] - t[i];
        }
        board->video_ns += ns; ++board->step;
    }
    printf("block_profile status=%d boundaries=%" PRIu64
           " cpu_and_sync_s=%.6f dma_s=%.6f peripherals_s=%.6f video_clock_s=%.6f\n",
           (int)status, done, totals[0]/1e9, totals[1]/1e9, totals[2]/1e9, totals[3]/1e9);
    printf("inclusive_memory_s=%.6f memory_calls=%" PRIu64
           " inclusive_io_s=%.6f io_calls=%" PRIu64
           " guest_ns=%" PRIu64 " manual=%" PRIu64 " fallback=%" PRIu64 "\n",
           board->profile_memory_ns/1e9,board->memory_calls-start_memory,board->profile_io_ns/1e9,
           board->io_count-start_io,board->video_ns-start_guest,manual,fallback);
    printf("warmup_boundaries=%" PRIu64 " measured_boundaries=%" PRIu64
           " measured_start_guest_ns=%" PRIu64 "\n",warmup,
           done>warmup ? done-warmup : 0,start_guest);
    bm_pcs286_composition_destroy(board); free(board);
    printf("inclusive_sync pit_s=%.6f rtc_s=%.6f keyboard_s=%.6f\n",
           sync_totals[0]/1e9,sync_totals[1]/1e9,sync_totals[2]/1e9);
    printf("cpu_boundaries instruction=%" PRIu64 " rep_iteration=%" PRIu64
           " interrupt=%" PRIu64 " exception=%" PRIu64 " halt=%" PRIu64
           " hold=%" PRIu64 " shutdown=%" PRIu64 " reset=%" PRIu64
           " no_cpu=%" PRIu64 " unknown_timing=%" PRIu64 "\n",
           kinds[BM_286_BOUNDARY_INSTRUCTION], kinds[BM_286_BOUNDARY_REP_ITERATION],
           kinds[BM_286_BOUNDARY_INTERRUPT], kinds[BM_286_BOUNDARY_EXCEPTION],
           kinds[BM_286_BOUNDARY_HALT], kinds[BM_286_BOUNDARY_HOLD],
           kinds[BM_286_BOUNDARY_SHUTDOWN], resets, no_cpu, unknown);
    return status == BM_STATUS_OK ? 0 : 1;
}

/* Measures the same provisional runtime as Qt, without rendering or pacing.
 * Firmware is an external read-only input, never bundled with this tool. */
int main(int argc, char **argv)
{
    uint8_t bios[BM_PCS286_FIRMWARE_BYTES];
    bm_host_services_t host = bm_null_host_services();
    bm_pcs286_config_t config = {0};
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    FILE *file;
    char *end;
    uint64_t boundaries, actual = 0, guest_ns = 0;
    uint32_t *pixels = NULL;
    size_t capacity = 0;
    unsigned frames = 0;
    uint64_t render_ns = 0, digest = UINT64_C(14695981039346656037);
    uint8_t *disk=NULL;
    uint8_t saved_cmos[128];
    int dos_profile=(argc>=5 && argc<=7) && !strcmp(argv[3],"--dos-profile");
    int dos_board=argc==7 && !strcmp(argv[3],"--dos-board-profile");
    uint64_t warmup=0;
    uint64_t previous_sample=0, previous_steps=0;
    int render_frames = argc == 4 && !strcmp(argv[3], "--render");
    clock_t started;
    bm_status_t status;
    if (!dos_board && !dos_profile && argc != 3 && !(argc == 4 && (!strcmp(argv[3], "--blocks") || !strcmp(argv[3], "--blocks-estimate") || !strcmp(argv[3], "--board-profile") || render_frames))) {
        fprintf(stderr, "usage: pcs286-runtime-benchmark BIOS DURATION_US [--render]\n"
                        "       pcs286-runtime-benchmark BIOS DURATION_US --dos-profile FLOPPY [OUTPUT.ppm [CMOS_HEX]]\n"
                        "       pcs286-runtime-benchmark BIOS BOUNDARIES --dos-board-profile FLOPPY CMOS_HEX WARMUP_BOUNDARIES\n"
                        "       pcs286-runtime-benchmark BIOS BOUNDARIES --blocks|--blocks-estimate|--board-profile\n");
        return 2;
    }
    boundaries = strtoull(argv[2], &end, 10);
    if (*end || !boundaries || boundaries > 100000000) return 2;
    if (dos_board) {
        warmup=strtoull(argv[6],&end,10);
        if (*end || end==argv[6] || warmup>=boundaries) return 2;
    }
    file = fopen(argv[1], "rb");
    if (!file) return 2;
    if (fread(bios, 1, sizeof(bios), file) != sizeof(bios) || fgetc(file) != EOF) {
        fclose(file); return 2;
    }
    fclose(file);
    config.size = sizeof(config); config.version = BM_PCS286_CONFIG_VERSION;
    config.ram_kib = 1024;
    config.firmware.image[0].data = bios;
    config.firmware.image[0].size = sizeof(bios);
    config.floppy[0].installed = 1;
    config.floppy[0].geometry = (bm_floppy_geometry_t){80,2,18,512};
    if (dos_board || (dos_profile && argc==7)) {
        const char *encoded=argv[dos_board ? 5 : 6];
        if (strlen(encoded)!=256) return 2;
        for (size_t i=0;i<128;++i) {
            char pair[3]={encoded[2*i],encoded[2*i+1],0};
            if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1])) return 2;
            char *tail; unsigned long value=strtoul(pair,&tail,16);
            if (*tail || tail!=pair+2 || value>255) return 2;
            saved_cmos[i]=(uint8_t)value;
        }
        config.initial_cmos=saved_cmos; config.initial_cmos_size=128;
    }
    if (dos_profile || dos_board) {
        disk=malloc(1474560);
        file=fopen(argv[4],"rb");
        if (!disk || !file) { if(file) fclose(file); free(disk); return 2; }
        int valid=fread(disk,1,1474560,file)==1474560 && fgetc(file)==EOF;
        fclose(file);
        if (!valid) { free(disk); return 2; }
        config.floppy[0].media_present=1; config.floppy[0].write_protected=1;
        config.floppy[0].media.context=disk;
        config.floppy[0].media.read=image_read;
        config.floppy[0].media.block_size=512;
        config.floppy[0].media.block_count=2880;
        config.floppy[0].media.read_only=1;
    }
    if (dos_board) {
        int result=profile_board(&host,bios,&config.floppy[0],boundaries,2,saved_cmos,warmup);
        free(disk); return result;
    }
    if (argc == 4 && !render_frames) return profile_board(&host, bios, &config.floppy[0], boundaries,
        !strcmp(argv[3], "--board-profile") ? 2 : !strcmp(argv[3], "--blocks-estimate"),NULL,0);
    machine = bm_pcs286_machine_config(&config);
    status = bm_session_create(&host, &session);
    if (status == BM_STATUS_OK) status = bm_session_configure(session, &machine);
    if (status == BM_STATUS_OK) status = bm_session_start(session);
    started = clock();
    previous_sample=sample_ns();
    /* Same 5 ms maximum chunk as the Qt worker; no real-time throttle. */
    for (uint64_t done = 0; status == BM_STATUS_OK && done < boundaries;) {
        uint64_t chunk = boundaries - done;
        if (chunk > 5000) chunk = 5000;
        status = bm_session_run_for(session, chunk * 1000);
        if (status == BM_STATUS_OK) guest_ns += chunk * 1000;
        done += chunk;
        if (dos_profile && status==BM_STATUS_OK && done%1000000==0) {
            uint64_t t=sample_ns(), steps=0, ip=0, cs=0, halted=0, io=0, memory=0;
            (void)bm_session_inspect_machine(session,"boundaries",&steps);
            (void)bm_session_inspect_machine(session,"ip",&ip);
            (void)bm_session_inspect_machine(session,"cs",&cs);
            (void)bm_session_inspect_machine(session,"halted",&halted);
            (void)bm_session_inspect_machine(session,"io_calls",&io);
            (void)bm_session_inspect_machine(session,"memory_calls",&memory);
            printf("sample guest_s=%" PRIu64 " wall_s=%.6f steps=%" PRIu64
                   " cs_ip=%04" PRIx64 ":%04" PRIx64 " halted=%" PRIu64
                   " io=%" PRIu64 " memory=%" PRIu64 "\n",
                   guest_ns/1000000000,(t-previous_sample)/1e9,steps-previous_steps,
                   cs,ip,halted,io,memory);
            fflush(stdout); previous_sample=t; previous_steps=steps;
        }
        /* 50 emulated frames/s, not Qt's wall-time display cadence. */
        if (status == BM_STATUS_OK && ((render_frames && done % 20000 == 0) || done == boundaries)) {
            bm_video_geometry_t geometry;
            bm_video_framebuffer_t framebuffer;
            size_t count;
            status = bm_session_video_geometry(session, &geometry);
            if (status != BM_STATUS_OK) break;
            if (!geometry.width || !geometry.height) continue;
            if ((size_t)geometry.width > SIZE_MAX / geometry.height) {
                status = BM_STATUS_CAPACITY_EXCEEDED; break;
            }
            count = (size_t)geometry.width * geometry.height;
            if (count > SIZE_MAX / sizeof(*pixels)) { status = BM_STATUS_CAPACITY_EXCEEDED; break; }
            if (count > capacity) {
                uint32_t *replacement = realloc(pixels, count * sizeof(*pixels));
                if (!replacement) { status = BM_STATUS_OUT_OF_MEMORY; break; }
                pixels = replacement; capacity = count;
            }
            framebuffer = (bm_video_framebuffer_t){pixels, capacity, geometry.width, geometry};
            uint64_t before = sample_ns();
            status = bm_session_render_video(session, &framebuffer);
            uint64_t after = sample_ns();
            if (!before || after < before) { status = BM_STATUS_INVALID_STATE; break; }
            render_ns += after - before; ++frames;
            if (done == boundaries) {
                for (size_t pixel = 0; pixel < count; ++pixel) {
                    digest ^= pixels[pixel]; digest *= UINT64_C(1099511628211);
                }
                if (dos_profile && argc>=6) {
                    FILE *picture=fopen(argv[5],"wb");
                    if (!picture) { status=BM_STATUS_DEVICE_ERROR; break; }
                    int ok=fprintf(picture,"P6\n%u %u\n255\n",geometry.width,geometry.height)>0;
                    for (size_t pixel=0;ok && pixel<count;++pixel) {
                        uint8_t rgb[3]={(uint8_t)(pixels[pixel]>>16),(uint8_t)(pixels[pixel]>>8),(uint8_t)pixels[pixel]};
                        ok=fwrite(rgb,1,3,picture)==3;
                    }
                    if (fclose(picture)!=0 || !ok) status=BM_STATUS_DEVICE_ERROR;
                }
            }
        }
    }
    double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
    if (session) (void)bm_session_inspect_machine(session, "boundaries", &actual);
    printf("status=%d boundaries=%" PRIu64 " provisional_guest_s=%.6f clock_s=%.6f\n",
           (int)status, actual, (double)guest_ns / 1000000000, seconds);
    printf("frames=%u render_s=%.6f final_pixel_digest=%016" PRIx64 "\n", frames, render_ns / 1e9, digest);
    free(pixels);
    bm_session_destroy(session);
    free(disk);
    return status == BM_STATUS_OK ? 0 : 1;
}
