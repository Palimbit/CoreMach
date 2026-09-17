/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/systems/olivetti_pcs86.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void
put_combined_byte(uint8_t *even, uint8_t *odd, size_t offset, uint8_t value)
{
    if ((offset & 1U) == 0U)
        even[offset / 2U] = value;
    else
        odd[offset / 2U] = value;
}

static uint64_t
inspect_cpu(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_cpu(session, 0U, name, &value) == BM_STATUS_OK);
    return value;
}

static uint64_t
inspect_machine(bm_session_t *session, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_session_inspect_machine(session, name, &value) == BM_STATUS_OK);
    return value;
}

static void
run_ems_variant(const bm_host_services_t *host,
                uint32_t ems_kib,
                uint16_t ems_pages,
                uint8_t simm_code,
                uint16_t expected_bx,
                uint16_t expected_cx,
                uint16_t expected_dx)
{
    static const uint8_t reset_jump[] = { 0xea, 0x00, 0x01, 0x00, 0xf0 };
    static const uint8_t program[] = {
        0xb8, 0x00, 0x80,             /* MOV AX,8000h. */
        0x8e, 0xd8,                   /* MOV DS,AX. */
        0xc6, 0x06, 0x00, 0x00, 0x11, /* Conventional RAM under window 0. */
        0xba, 0x00, 0x84,             /* MOV DX,8400h. */
        0xb0, 0x80, 0xee,             /* Enable window 0 on EMS page 0. */
        0xc6, 0x06, 0x00, 0x00, 0x22,
        0xb0, 0x81, 0xee,             /* Select page 1 and write it. */
        0xc6, 0x06, 0x00, 0x00, 0x33,
        0xb0, 0x80, 0xee,
        0x8a, 0x1e, 0x00, 0x00,       /* BL=page 0. */
        0xb0, 0x81, 0xee,
        0x8a, 0x3e, 0x00, 0x00,       /* BH=page 1. */
        0xb0, 0x00, 0xee,
        0x8a, 0x0e, 0x00, 0x00,       /* CL=underlying conventional RAM. */
        0xb0, 0xff, 0xee,
        0x8a, 0x2e, 0x00, 0x00,       /* CH=RAM for invalid page 127. */
        0xb8, 0x00, 0x84,
        0x8e, 0xd8,                   /* DS=8400h, window 1. */
        0xba, 0x01, 0x84,
        0xb0, 0x80, 0xee,
        0xc6, 0x06, 0x00, 0x00, 0x44, /* Window 1 aliases page 0. */
        0xb8, 0x00, 0x80,
        0x8e, 0xd8,
        0xba, 0x00, 0x84,
        0xb0, 0x80, 0xee,
        0xec,                         /* Selector registers are readable. */
        0x88, 0xc6,                   /* DH=80h. */
        0x8a, 0x16, 0x00, 0x00,       /* DL=page 0 byte written via window 1. */
        0xe4, 0x64,                   /* Read installed-SIMM code. */
        0x88, 0xc4,                   /* AH=AL. */
        0xf4
    };
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    bm_pcs86_config_t config;
    bm_machine_config_t machine;
    bm_session_t *session = NULL;
    size_t index;

    for (index = 0U; index < sizeof(reset_jump); ++index)
        put_combined_byte(even, odd, 0xfff0U + index, reset_jump[index]);
    for (index = 0U; index < sizeof(program); ++index)
        put_combined_byte(even, odd, 0x0100U + index, program[index]);
    config = (bm_pcs86_config_t) {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .ems_kib = ems_kib
    };
    machine = bm_pcs86_machine_config(&config);

    assert(bm_session_create(host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_OK);
    assert(bm_session_start(session) == BM_STATUS_OK);
    assert(inspect_machine(session, "ems_kib") == ems_kib);
    assert(inspect_machine(session, "ems_pages") == ems_pages);
    assert(inspect_machine(session, "ems_selector0") == 0U);
    assert(bm_session_run_for(session, 80U) == BM_STATUS_OK);
    assert(inspect_cpu(session, "halted") == 1U);
    assert(inspect_cpu(session, "bx") == expected_bx);
    assert(inspect_cpu(session, "cx") == expected_cx);
    assert(inspect_cpu(session, "dx") == expected_dx);
    assert(inspect_cpu(session, "ax") == (uint16_t) (simm_code * 0x0101U));
    assert(inspect_machine(session, "ems_selector0") == 0x80U);
    assert(inspect_machine(session, "ems_selector1") == 0x80U);
    assert(bm_session_reset(session) == BM_STATUS_OK);
    assert(inspect_machine(session, "ems_selector0") == 0U);
    assert(inspect_machine(session, "ems_selector1") == 0U);
    assert(bm_session_stop(session) == BM_STATUS_OK);
    bm_session_destroy(session);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    uint8_t even[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    uint8_t odd[BM_PCS86_FIRMWARE_HALF_SIZE] = { 0 };
    bm_pcs86_config_t invalid = {
        .firmware_even = { "synthetic-even", even, sizeof(even), NULL },
        .firmware_odd = { "synthetic-odd", odd, sizeof(odd), NULL },
        .ems_kib = 1U
    };
    bm_machine_config_t machine = bm_pcs86_machine_config(&invalid);
    bm_session_t *session = NULL;

    run_ems_variant(&host, BM_PCS86_EMS_NONE_KIB, 0U, 0x00U,
                    0x3333U, 0x3333U, 0x8033U);
    run_ems_variant(&host, BM_PCS86_EMS_384_KIB, 24U, 0x20U,
                    0x3322U, 0x1111U, 0x8044U);
    run_ems_variant(&host, BM_PCS86_EMS_1920_KIB, 120U, 0x40U,
                    0x3322U, 0x1111U, 0x8044U);

    assert(bm_session_create(&host, &session) == BM_STATUS_OK);
    assert(bm_session_configure(session, &machine) == BM_STATUS_INVALID_ARGUMENT);
    bm_session_destroy(session);
    return 0;
}
