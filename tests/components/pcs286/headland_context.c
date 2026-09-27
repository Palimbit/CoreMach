/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored GC103 context tests from Headland 07-89 (01), pp2/3/5/7.
 * No classic mapping functions are used as the expected-value oracle.
 * RAM populations remain inherited fixtures, not PCS286 strap evidence.
 */
#include "legacy_gc103_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned long checks;

static void write_port(bm_gc103_memory_t *m, uint16_t port, unsigned width, uint16_t value)
{
    assert(bm_gc103_memory_io(m, port, width, BM_BUS_WRITE, 0, &value) == BM_STATUS_OK);
}

static void program(bm_gc103_memory_t *m, unsigned context, unsigned page, uint16_t word)
{
    write_port(m, 0x1eeU, 1, (uint16_t)(context * 32U + page));
    write_port(m, 0x1ecU, 2, word);
}

static uint32_t page_base(unsigned page)
{
    return page < 24U ? 0x40000U + page * 0x4000U : 0xc0000U + (page - 24U) * 0x4000U;
}

static void check_route(bm_gc103_memory_t *m, unsigned mib, unsigned page,
                        uint16_t word, int enabled, unsigned who, unsigned op, uint32_t within)
{
    bm_gc10x_route_t got;
    uint32_t base = page_base(page), offset = base + within;
    bm_gc10x_memory_target_t target = page < 24U ? BM_GC10X_RAM : BM_GC10X_EXTERNAL;
    int writable = 1;
    /* MR bank/page wiring p5. Fixture CR7 is 1 only at 4MiB; this does not
     * qualify the old population/strap table against a physical PCS286. */
    if (enabled && (word & 0x200U)) {
        unsigned pages_per_bank = mib == 4U ? 128U : 32U;
        offset = (((word >> 7) & 3U) * pages_per_bank + (word & (pages_per_bank - 1U))) * 16384U + within;
        target = offset < mib * 0x100000U ? BM_GC10X_RAM : BM_GC10X_OPEN_BUS;
        writable = target == BM_GC10X_RAM;
        if (target == BM_GC10X_OPEN_BUS) offset = 0;
    }
    assert(bm_gc103_memory_resolve(m, (bm_gc10x_requester_t)who, 0, base + within,
                                  (bm_bus_operation_t)op, &got) == BM_STATUS_OK);
    assert(got.target == target && got.offset == offset && got.writable == writable);
    assert(got.contiguous_bytes == 0x4000U - within);
    assert(got.wait_quality == BM_GC10X_WAIT_UNKNOWN && got.extra_memory_clocks == 0);
    ++checks;
}

static void check_page(bm_gc103_memory_t *m, unsigned mib, unsigned page, uint16_t word, int enabled)
{
    static const uint32_t offsets[] = {0, 0x1234U, 0x3fffU};
    unsigned who, op, i;
    for (who = 0; who < 3U; ++who)
        for (op = 0; op < 3U; ++op)
            for (i = 0; i < 3U; ++i)
                check_route(m, mib, page, word, enabled, who, op, offsets[i]);
}

static void context_matrix(void)
{
    bm_gc103_memory_t m;
    unsigned mib, context, page, word;
    for (mib = 1; mib <= 4U; ++mib) {
        assert(bm_gc103_memory_initialize(&m, mib * 0x100000U) == BM_STATUS_OK);
        for (context = 0; context < 2U; ++context) {
            write_port(&m, 0x1efU, 1, (uint16_t)(2U | context));
            for (page = 0; page < 32U; ++page) {
                for (word = 0; word < 1024U; ++word) {
                    uint16_t active = (uint16_t)(0x85U + page + ((word & 1U) ? 0x200U : 0U));
                    program(&m, context, page, active);
                    program(&m, context ^ 1U, page, (uint16_t)word);
                    /* Programming the other context never changes this route. */
                    check_page(&m, mib, page, active, 1);
                    write_port(&m, 0x1efU, 1, (uint16_t)(2U | (context ^ 1U)));
                    check_page(&m, mib, page, (uint16_t)word, 1);
                    write_port(&m, 0x1efU, 1, (uint16_t)(2U | context));
                    check_page(&m, mib, page, active, 1);
                }
            }
        }
    }
}

static void auto_increment_and_reads(void)
{
    bm_gc103_memory_t m, before;
    uint16_t words[64] = {0};
    unsigned context, step, page, mar;
    uint16_t value;
    for (context = 0; context < 2U; ++context) {
        assert(bm_gc103_memory_initialize(&m, 0x100000U) == BM_STATUS_OK);
        write_port(&m, 0x1efU, 1, (uint16_t)(2U | context));
        memset(words, 0, sizeof(words));
        write_port(&m, 0x1eeU, 1, 0x80U);
        for (step = 0; step < 128U; ++step) {
            unsigned slot = step % 64U;
            words[slot] = (uint16_t)(0x200U | ((step * 7U) & 0xffU));
            write_port(&m, 0x1ecU, 2, words[slot]);
            value = 0;
            assert(bm_gc103_memory_io(&m, 0x1eeU, 1, BM_BUS_READ, 0, &value) == BM_STATUS_OK);
            assert(value == ((0x81U + step) & 0xffU));
            for (page = 0; page < 32U; ++page)
                check_page(&m, 1, page, words[context * 32U + page], 1);
        }
        /* DEBUG reads don't increment, normal MR reads do; neither remaps. */
        write_port(&m, 0x1eeU, 1, 0x80U);
        for (mar = 0x80U; mar <= 0xffU; ++mar) {
            memcpy(&before, &m, sizeof(m));
            value = 0;
            assert(bm_gc103_memory_io(&m, 0x1ecU, 2, BM_BUS_READ, 1, &value) == BM_STATUS_OK);
            assert((value & 0x3ffU) == words[mar % 64U]);
            assert(memcmp(&before, &m, sizeof(m)) == 0);
            assert(bm_gc103_memory_io(&m, 0x1ecU, 2, BM_BUS_READ, 0, &value) == BM_STATUS_OK);
            assert(m.registers.mar == (uint8_t)(mar + 1U));
            for (page = 0; page < 32U; ++page)
                check_page(&m, 1, page, words[context * 32U + page], 1);
        }
        /* Legacy byte-MR policy remains unqualified; it too must not leak
         * into memory-context selection. Invalid/debug writes remain pure. */
        write_port(&m, 0x1eeU, 1, (uint16_t)((context ^ 1U) * 32U + 24U));
        write_port(&m, 0x1ecU, 1, 0);
        check_page(&m, 1, 24, words[context * 32U + 24U], 1);
        memcpy(&before, &m, sizeof(m));
        value = 0;
        assert(bm_gc103_memory_io(&m, 0x1ecU, 2, BM_BUS_WRITE, 1, &value) == BM_STATUS_READ_ONLY);
        assert(bm_gc103_memory_io(&m, 0x1ecU, 4, BM_BUS_WRITE, 0, &value) == BM_STATUS_INVALID_ARGUMENT);
        assert(memcmp(&before, &m, sizeof(m)) == 0);
        /* Disabled global EMS restores ordinary/external memory independently
         * of MAR, followed by re-enable selecting the retained current bank. */
        write_port(&m, 0x1efU, 1, (uint16_t)context);
        program(&m, context ^ 1U, 31, 0x3ffU);
        for (page = 0; page < 32U; ++page) check_page(&m, 1, page, 0, 0);
        write_port(&m, 0x1efU, 1, (uint16_t)(2U | context));
        for (page = 0; page < 32U; ++page)
            check_page(&m, 1, page, words[context * 32U + page], 1);
    }
}

int main(void)
{
    context_matrix();
    auto_increment_and_reads();
    printf("GC103 documented contexts: %lu route checks; independent banks, switching, MAR traversal and pure reads.\n", checks);
    return 0;
}
