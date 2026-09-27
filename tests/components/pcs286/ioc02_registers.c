/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored harness; original handlers/notices are in the generated include.
 * Classic equivalence is not independent evidence of physical IOC02 behavior.
 */
#include "legacy_ioc02_registers.h"
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test-only machine identities for the unchanged classic reset function. */
static void classic_pcs286(void) {}
static void machine_at_olivetti_pcs386sx_init(void) {}
static const struct { void (*init)(void); } machines[] = {
    {classic_pcs286}, {machine_at_olivetti_pcs386sx_init}
};
static unsigned machine;
#define olivetti_ioc02_log(...) ((void)0)
#include "ioc02_classic_registers.inc"

static unsigned long comparisons;
static const uint16_t ports[] = {0x68U, 0x6aU, 0x6cU};

static void same(const bm_ioc02_legacy_registers_t *a,
                 const bm_ioc02_legacy_registers_t *b)
{
    assert(a->select == b->select && a->data == b->data && a->control == b->control);
}
static void equivalent(const bm_ioc02_legacy_registers_t *r,
                       const olivetti_ioc02_t *c)
{
    assert(r->select == c->reg_068 && r->data == c->reg_06a && r->control == c->reg_06c);
    ++comparisons;
}
static void start(bm_ioc02_legacy_registers_t *r, olivetti_ioc02_t *c)
{
    assert(bm_ioc02_legacy_initialize(r) == BM_STATUS_OK);
    memset(c, 0, sizeof(*c));
    olivetti_ioc02_reset(c);
    equivalent(r, c);
    /* DELIBERATE differential boundary: compare steady-state classic logic.
     * The rejected first-read override is tested independently below. */
    c->first_read_done = 1U;
}
static uint16_t transfer(bm_ioc02_legacy_registers_t *r, olivetti_ioc02_t *c,
                         uint16_t port, bm_bus_operation_t op,
                         uint16_t value, int debug)
{
    const bm_ioc02_legacy_registers_t before = *r;
    bm_ioc02_legacy_effect_t effect = {0xfeU, 0xfdU};
    uint16_t expected = value;
    unsigned written = 0U, changed = 0U;
    assert(bm_ioc02_legacy_access(r, port, 1U, op, debug, &value, &effect) == BM_STATUS_OK);
    if (op == BM_BUS_READ) {
        expected = olivetti_ioc02_read(port, c);
        same(r, &before);
    } else {
        olivetti_ioc02_write(port, (uint8_t)value, c);
        if (port == 0x68U) written = BM_IOC02_LEGACY_SELECT;
        if (port == 0x6cU) written = BM_IOC02_LEGACY_CONTROL;
        if (port == 0x6aU && (before.select & 31U)) written = BM_IOC02_LEGACY_DATA;
    }
    if (before.select != c->reg_068) changed |= BM_IOC02_LEGACY_SELECT;
    if (before.data != c->reg_06a) changed |= BM_IOC02_LEGACY_DATA;
    if (before.control != c->reg_06c) changed |= BM_IOC02_LEGACY_CONTROL;
    assert(value == expected && effect.written == written && effect.changed == changed);
    equivalent(r, c);
    return value;
}
static void exhaustive(void)
{
    bm_ioc02_legacy_registers_t r;
    olivetti_ioc02_t c;
    unsigned sel, data, old, i;
    start(&r, &c);
    for (sel = 0; sel < 256U; ++sel) {
        for (data = 0; data < 256U; ++data) {
            transfer(&r, &c, 0x68U, BM_BUS_WRITE, (uint16_t)sel, 0);
            transfer(&r, &c, 0x6aU, BM_BUS_WRITE, (uint16_t)data, 0);
            transfer(&r, &c, 0x6cU, BM_BUS_WRITE, (uint16_t)(255U - data), 0);
            for (i = 0; i < 3U; ++i) {
                transfer(&r, &c, ports[i], BM_BUS_READ, 0xbeefU, 0);
                transfer(&r, &c, ports[i], BM_BUS_READ, 0xbeefU, 1);
            }
            /* Same-value writes still report latch enable, not a transition. */
            transfer(&r, &c, 0x6aU, BM_BUS_WRITE, (uint16_t)data, 0);
        }
    }
    /* Every old/new data pair for each of the eight disabled selectors.
     * Seed via genuine accepted writes; high selector bits never enable data. */
    for (sel = 0; sel < 256U; sel += 32U) {
        for (old = 0; old < 256U; ++old) {
            transfer(&r, &c, 0x68U, BM_BUS_WRITE, 1U, 0);
            transfer(&r, &c, 0x6aU, BM_BUS_WRITE, (uint16_t)old, 0);
            transfer(&r, &c, 0x68U, BM_BUS_WRITE, (uint16_t)sel, 0);
            for (data = 0; data < 256U; ++data) {
                transfer(&r, &c, 0x6aU, BM_BUS_WRITE, (uint16_t)data, 0);
                assert(transfer(&r, &c, 0x6aU, BM_BUS_READ, 0U, 0) == (old ^ 0x20U));
            }
        }
    }
}
static void no_first_read_override(void)
{
    bm_ioc02_legacy_registers_t r;
    bm_ioc02_legacy_effect_t effect;
    olivetti_ioc02_t c;
    unsigned model, pattern, i;
    for (model = 0; model < 2U; ++model) {
        machine = model;
        for (pattern = 0; pattern < 3U; ++pattern) {
            start(&r, &c);
            if (pattern == 1U) {
                transfer(&r, &c, 0x6aU, BM_BUS_WRITE, 0x55U, 0);
            } else if (pattern == 2U) {
                transfer(&r, &c, 0x68U, BM_BUS_WRITE, 0xe0U, 0);
                transfer(&r, &c, 0x6aU, BM_BUS_WRITE, 0x55U, 0);
            }
            for (i = 0; i < 4U; ++i) {
                uint16_t v = 0xbeefU;
                assert(bm_ioc02_legacy_access(&r, 0x6aU, 1U, BM_BUS_READ,
                                             (int)(i & 1U), &v, &effect) == BM_STATUS_OK);
                assert(v == (pattern == 1U ? 0x75U : 0x24U));
                assert(effect.written == 0U && effect.changed == 0U);
                equivalent(&r, &c);
            }
            /* Demonstrate why simply disabling the classic flag is NOT the
             * portable implementation: a read-first still returns forced04. */
            olivetti_ioc02_reset(&c);
            assert(c.first_read_hack_enabled == (model == 0U));
            assert(olivetti_ioc02_read(0x6aU, &c) == 0x04U);
            assert(olivetti_ioc02_read(0x6aU, &c) == 0x24U);
            start(&r, &c); /* Reinitialization must not create a one-shot. */
            assert(transfer(&r, &c, 0x6aU, BM_BUS_READ, 0U, 0) == 0x24U);
        }
    }
    machine = 0U;
}
static void sequences(void)
{
    bm_ioc02_legacy_registers_t r[2];
    olivetti_ioc02_t c[2];
    uint32_t rng = 0x6a020068U;
    unsigned i, which;
    start(&r[0], &c[0]); start(&r[1], &c[1]);
    for (i = 0; i < 100000U; ++i) {
        bm_ioc02_legacy_registers_t other;
        rng = rng * 1664525U + 1013904223U;
        which = (rng >> 17) & 1U;
        other = r[1U - which];
        if ((rng & 1023U) == 0U) {
            start(&r[which], &c[which]);
        } else {
            bm_bus_operation_t op = (rng & 2U) ? BM_BUS_READ : BM_BUS_WRITE;
            transfer(&r[which], &c[which], ports[(rng >> 20) % 3U], op,
                     (uint16_t)(rng >> 24), op == BM_BUS_READ && (rng & 8U));
        }
        same(&other, &r[1U - which]);
    }
}
static void rejected(bm_ioc02_legacy_registers_t *r, uint16_t port,
                     uint32_t width, bm_bus_operation_t op, int debug,
                     uint16_t value, bm_status_t expected)
{
    bm_ioc02_legacy_registers_t before = *r;
    bm_ioc02_legacy_effect_t effect = {0xfeU, 0xfdU};
    const uint16_t before_value = value;
    assert(bm_ioc02_legacy_access(r, port, width, op, debug, &value, &effect) == expected);
    same(r, &before);
    assert(value == before_value && effect.written == 0xfeU && effect.changed == 0xfdU);
}
static void bad_inputs(void)
{
    bm_ioc02_legacy_registers_t r = {0x17U, 0x53U, 0xc7U}, before = r;
    bm_ioc02_legacy_effect_t effect = {0xfeU, 0xfdU};
    uint16_t value = 0x83U;
    uint32_t port, width;
    unsigned i;
    assert(bm_ioc02_legacy_initialize(NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_ioc02_legacy_access(NULL, 0x68U, 1U, BM_BUS_READ, 0, &value, &effect) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_ioc02_legacy_access(&r, 0x68U, 1U, BM_BUS_READ, 0, NULL, &effect) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_ioc02_legacy_access(&r, 0x68U, 1U, BM_BUS_READ, 0, &value, NULL) == BM_STATUS_INVALID_ARGUMENT);
    same(&r, &before);
    assert(value == 0x83U && effect.written == 0xfeU && effect.changed == 0xfdU);
    for (port = 0; port < 65536U; ++port) {
        if (port == 0x68U || port == 0x6aU || port == 0x6cU) continue;
        rejected(&r, (uint16_t)port, 1U, BM_BUS_READ, 0, 0xbeefU, BM_STATUS_UNMAPPED);
        rejected(&r, (uint16_t)port, 1U, BM_BUS_WRITE, 0, 0x55U, BM_STATUS_UNMAPPED);
        rejected(&r, (uint16_t)port, 1U, BM_BUS_READ, 1, 0xbeefU, BM_STATUS_UNMAPPED);
        rejected(&r, (uint16_t)port, 1U, BM_BUS_WRITE, 1, 0x55U, BM_STATUS_UNMAPPED);
    }
    for (i = 0; i < 3U; ++i) {
        for (width = 0; width <= 8U; ++width) {
            if (width == 1U) continue;
            rejected(&r, ports[i], width, BM_BUS_READ, 0, 0xbeefU, BM_STATUS_INVALID_ARGUMENT);
            rejected(&r, ports[i], width, BM_BUS_WRITE, 0, 0x55U, BM_STATUS_INVALID_ARGUMENT);
        }
        rejected(&r, ports[i], UINT32_MAX, BM_BUS_READ, 0, 0U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_FETCH, 0, 0U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, (bm_bus_operation_t)99, 0, 0U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_READ, -1, 0U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_READ, 2, 0U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_WRITE, 0, 0x100U, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_WRITE, 0, 0xffffU, BM_STATUS_INVALID_ARGUMENT);
        rejected(&r, ports[i], 1U, BM_BUS_WRITE, 1, 0x55U, BM_STATUS_READ_ONLY);
    }
}

/* Authored router adapter, not production board timing/wiring. It demonstrates
 * raw effects reaching an owner only after a successful transfer; it never
 * interprets a latch as an A20, shadow, remap or device-enable signal. */
typedef struct fixture {
    bm_ioc02_legacy_registers_t registers;
    bm_ioc02_legacy_effect_t last;
    unsigned writes;
} fixture_t;
static bm_status_t endpoint(void *context, bm_bus_transaction_t *tx)
{
    fixture_t *f = context;
    bm_ioc02_legacy_effect_t effect;
    bm_status_t status;
    uint16_t value;
    if (tx->space != BM_ADDRESS_IO || tx->address > 0xffffU)
        return BM_STATUS_UNMAPPED;
    if (tx->operation == BM_BUS_WRITE && tx->value > 0xffU)
        return BM_STATUS_INVALID_ARGUMENT;
    value = tx->operation == BM_BUS_WRITE ? (uint16_t)tx->value : 0U;
    status = bm_ioc02_legacy_access(&f->registers, (uint16_t)tx->address,
        tx->size, tx->operation, (tx->attributes & BM_BUS_TRANSACTION_DEBUG) != 0U,
        &value, &effect);
    if (status != BM_STATUS_OK) return status;
    if (tx->operation == BM_BUS_READ) tx->value = value;
    if (effect.written) { f->last = effect; ++f->writes; }
    return BM_STATUS_OK;
}
static void router(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    fixture_t f = {0};
    bm_bus_transaction_t tx = {0};
    bm_ioc02_legacy_registers_t before;
    unsigned i;
    assert(bm_ioc02_legacy_initialize(&f.registers) == BM_STATUS_OK);
    assert(bm_bus_create(&host, 3U, &bus) == BM_STATUS_OK);
    for (i = 0; i < 3U; ++i)
        assert(bm_bus_map(bus, BM_ADDRESS_IO, ports[i], ports[i], endpoint, &f) == BM_STATUS_OK);
    tx.space = BM_ADDRESS_IO; tx.operation = BM_BUS_READ;
    tx.address = 0x6aU; tx.size = 1U; tx.alignment = 1U;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK && tx.value == 0x24U);
    assert(f.writes == 0U);
    tx.operation = BM_BUS_WRITE; tx.value = 0x55U;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK && tx.value == 0x55U);
    assert(f.writes == 1U && f.last.written == BM_IOC02_LEGACY_DATA && f.last.changed == BM_IOC02_LEGACY_DATA);
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK);
    assert(f.writes == 2U && f.last.changed == 0U);
    tx.address = 0x68U; tx.value = 0xe0U;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK && f.writes == 3U);
    tx.address = 0x6aU; tx.value = 0xaaU;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK && f.writes == 3U);
    before = f.registers;
    tx.attributes = BM_BUS_TRANSACTION_DEBUG;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_READ_ONLY);
    tx.operation = BM_BUS_READ;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_OK && tx.value == 0x75U);
    same(&before, &f.registers);
    tx.attributes = 0U;
    tx.address = 0x6bU; tx.value = 0xdeadU;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_UNMAPPED && tx.value == 0xdeadU);
    tx.address = 0x6aU; tx.size = 2U;
    assert(bm_bus_transact(bus, &tx) == BM_STATUS_UNMAPPED && tx.value == 0xdeadU);
    same(&before, &f.registers);
    assert(f.writes == 3U);
    bm_bus_destroy(bus);
}
int main(void)
{
    exhaustive(); no_first_read_override(); sequences(); bad_inputs(); router();
    printf("IOC02: %lu classic latch comparisons; first-read override excluded; router/effect/error checks passed\n", comparisons);
    return 0;
}
