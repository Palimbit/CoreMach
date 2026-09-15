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

    if (argc != 3) {
        fprintf(stderr, "usage: %s <even-rom> <odd-rom>\n", argv[0]);
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
        status = bm_session_run_for(session, 200000U);

    printf("status=%d instructions=%" PRIu64 " io=%" PRIu64
           " last=%04x:%04x physical=%05" PRIx32 " opcode=%02x\n",
           (int) status, probe.instructions, probe.io_operations,
           probe.last.cs, probe.last.ip, probe.last.physical_address, probe.last.opcode);

    bm_session_destroy(session);
    free(even);
    free(odd);
    return (status == BM_STATUS_OK) ? 0 : 3;
}
