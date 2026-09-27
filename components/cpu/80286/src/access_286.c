/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Intel 80286/80287 PRM 1987 sections 7.4 and 9.8: cached access checks.
 * Transfer splitting/commit policy is functional, not physical bus timing.
 */
#include "access_286.h"
#include <string.h>

static const bm_286_segment_state_t *segment(const bm_286_arch_state_t *a,
    unsigned reg)
{
    switch (reg) {
        case 0: return &a->es;
        case 1: return &a->cs;
        case 2: return &a->ss;
        default: return &a->ds; /* Validated reg == 3. */
    }
}

bm_status_t bm_286_pm_check_access(const bm_286_arch_state_t *arch,
    unsigned reg, bm_286_pm_access_kind_t kind, uint32_t offset,
    uint32_t length, bm_286_pm_access_check_t *result)
{
    const bm_286_segment_state_t *s;
    bm_286_pm_descriptor_t d;
    if (!result) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!arch || reg > 3 || kind < BM_286_PM_FETCH || kind > BM_286_PM_WRITE ||
        (kind == BM_286_PM_FETCH && reg != 1) || !length)
        return BM_STATUS_INVALID_ARGUMENT;
    if (!(arch->msw & 1u) || arch->cpl > 3 || arch->shutdown)
        return BM_STATUS_INVALID_STATE;
    s = segment(arch, reg);
    if (s->valid > 1 || s->base > 0xffffffu) return BM_STATUS_INVALID_STATE;
    if (!s->valid) {
        if (reg == 1 || reg == 2) return BM_STATUS_INVALID_STATE;
        result->fault_vector = 13;
        return BM_STATUS_OK;
    }
    d = bm_286_cached_descriptor(s);
    /* These caches cannot result from the supported ordinary segment loads.
     * Presence faults belong to loading the descriptor, not a later access. */
    if (!d.present || (d.kind != BM_286_PM_DATA && d.kind != BM_286_PM_CODE) ||
        (reg == 1 && d.kind != BM_286_PM_CODE && s->access != 0x82) ||
        (reg == 2 && (d.kind != BM_286_PM_DATA || !d.writable)) ||
        ((reg == 0 || reg == 3) && d.kind == BM_286_PM_CODE && !d.readable))
        return BM_STATUS_INVALID_STATE;
    if ((kind == BM_286_PM_READ && !d.readable) ||
        (kind == BM_286_PM_WRITE && !d.writable)) {
        result->fault_vector = 13;
        return BM_STATUS_OK;
    }
    if (!bm_286_pm_segment_contains(&d, offset, length)) {
        result->fault_vector = reg == 2 ? 12 : 13;
        return BM_STATUS_OK;
    }
    result->allowed = true;
    return BM_STATUS_OK;
}

bm_status_t bm_286_pm_access(const bm_286_arch_state_t *arch,
    unsigned reg, bm_286_pm_access_kind_t kind, uint32_t offset,
    unsigned length, bool locked, const uint8_t *write_bytes,
    bm_bus_access_fn access, void *context, bm_286_pm_access_state_t *state,
    bm_286_pm_access_result_t *result)
{
    bm_286_pm_access_check_t check;
    bm_status_t status;
    uint8_t bytes[10] = {0};
    uint32_t address;
    unsigned pos = 0;
    if (!result) return BM_STATUS_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!state || !access || length == 0 || length > sizeof(bytes) ||
        (kind == BM_286_PM_WRITE && !write_bytes) || (kind == BM_286_PM_FETCH && locked))
        return BM_STATUS_INVALID_ARGUMENT;
    if (state->stopped) return BM_STATUS_INVALID_STATE;
    status = bm_286_pm_check_access(arch, reg, kind, offset, length, &check);
    if (status != BM_STATUS_OK) return status;
    result->fault_vector = check.fault_vector;
    if (!check.allowed) return BM_STATUS_OK;
    address = (segment(arch, reg)->base + offset) & 0xffffffu;
    while (pos < length) {
        bm_bus_transaction_t t = {0};
        unsigned i;
        /* Preserve logical word splitting at odd addresses; no re-pairing of
         * the second byte of one word with the first byte of the next. */
        unsigned size = kind != BM_286_PM_FETCH && !(address & 1u) &&
            length - pos >= 2 ? 2u : 1u;
        t.address = (address + pos) & 0xffffffu;
        t.space = kind == BM_286_PM_FETCH ? BM_ADDRESS_PROGRAM : BM_ADDRESS_DATA;
        t.operation = kind == BM_286_PM_FETCH ? BM_BUS_FETCH :
            (kind == BM_286_PM_WRITE ? BM_BUS_WRITE : BM_BUS_READ);
        t.size = t.alignment = size;
        t.endianness = BM_ENDIAN_LITTLE;
        t.attributes = locked ? BM_BUS_TRANSACTION_LOCKED : 0;
        if (kind == BM_286_PM_WRITE)
            for (i = 0; i < size; ++i) t.value |= (uint64_t)write_bytes[pos + i] << (i * 8u);
        status = access(context, &t);
        if (status != BM_STATUS_OK) { state->stopped = true; return status; }
        result->waits += t.wait_states;
        if (kind != BM_286_PM_WRITE)
            for (i = 0; i < size; ++i) bytes[pos + i] = (uint8_t)(t.value >> (i * 8u));
        pos += size;
    }
    if (kind != BM_286_PM_WRITE) memcpy(result->bytes, bytes, length);
    result->completed = true;
    return BM_STATUS_OK;
}
