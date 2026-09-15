/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <assert.h>
#include <stdint.h>

static void
put_combined_byte(uint8_t *even, uint8_t *odd, size_t offset, uint8_t value)
{
    if ((offset & 1U) == 0)
        even[offset / 2U] = value;
    else
        odd[offset / 2U] = value;
}

static uint64_t
inspect_cpu(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0, name, &value) == BM_STATUS_OK);
    return value;
}

static uint64_t
inspect_machine(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(session, name, &value) == BM_STATUS_OK);
    return value;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb0, 0x93, 0xe6, 0x65,       /* Enable LPT1 and COM1. */
        0xba, 0x78, 0x03,             /* DX=378h, SPP data register. */
        0xb0, 0xa5, 0xee, 0xec,       /* Write/read LPT data. */
        0x88, 0xc7,                   /* Save result in BH. */
        0xba, 0xfb, 0x03,             /* DX=3FBh, UART line control. */
        0xb0, 0x03, 0xee,
        0x42,                         /* DX=3FCh, UART modem control. */
        0xb0, 0x10, 0xee,             /* Enable internal loopback. */
        0xba, 0xf8, 0x03,             /* DX=3F8h, UART data. */
        0xb0, 0x5a, 0xee, 0xec,       /* Loop back one byte. */
        0x88, 0xc3,                   /* Save result in BL. */
        0xb0, 0xf2, 0xe6, 0x67,       /* Identify keyboard channel. */
        0xe4, 0x6a, 0x88, 0xc1,       /* Status in CL. */
        0xe4, 0x67, 0x88, 0xc5,       /* ACK in CH. */
        0xe4, 0x67, 0x88, 0xc2,       /* Manufacturer in DL. */
        0xe4, 0x67, 0x88, 0xc6,       /* Device ID in DH. */
        0xf4
    };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    for (index = 0; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);

    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL }
    };
    machine = bm_pcs86_machine_config(&config);

    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(bm_session_run_for(session, 64U) == BM_STATUS_OK);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == 0xa55aU);
    assert(inspect_cpu(session, "cx") == 0xfa20U);
    assert(inspect_cpu(session, "dx") == 0x83abU);
    assert(inspect_machine(session, "lpt_data") == 0xa5U);
    assert((inspect_machine(session, "uart_line_status") & 0x01U) == 0U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
    return 0;
}
