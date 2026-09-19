/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/rtc_mm58167.h>
#include <blumach/components/rtc_mm58167_clock.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct irq_reentry {
    bm_bus_t *bus;
    unsigned int assertions;
} irq_reentry_t;

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };

    return bm_bus_transact(bus, &transaction);
}

static uint8_t
read_port(bm_bus_t *bus, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0U, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };

    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
reset_subsecond_from_irq(void *context, int asserted)
{
    irq_reentry_t *reentry = context;

    if (!asserted)
        return;
    ++reentry->assertions;
    assert(write_port(reentry->bus, 0x00b5U, 0U) == BM_STATUS_OK);
}

static void
create_clocked_rtc(const bm_host_services_t *host,
                   bm_engine_t **engine,
                   bm_bus_t **bus,
                   bm_mm58167_t **rtc)
{
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_mm58167_config_t rtc_config = {
        0x00b0U, 0x00e0U, NULL, NULL, NULL, 0U
    };

    assert(bm_engine_create_clocked(host, &engine_config, engine) ==
           BM_STATUS_OK);
    assert(bm_bus_create(host, 2U, bus) == BM_STATUS_OK);
    assert(bm_mm58167_create(host, *bus, &rtc_config, rtc) == BM_STATUS_OK);
    assert(bm_mm58167_attach_clock(*engine, *rtc, NULL) == BM_STATUS_OK);
}

static void
destroy_clocked_rtc(bm_engine_t *engine, bm_bus_t *bus,
                    bm_mm58167_t *rtc)
{
    /* The clock-adapter contract requires this order. */
    bm_engine_destroy(engine);
    bm_mm58167_destroy(rtc);
    bm_bus_destroy(bus);
}

static void
test_lazy_millisecond_and_rollover_boundaries(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;

    create_clocked_rtc(&host, &engine, &bus, &rtc);

    assert(bm_engine_run_for(engine, UINT64_C(999000)) == BM_STATUS_OK);
    /* An observation before the scheduled millisecond catches the component
     * up to the source cursor without inventing an early counter edge. */
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);
    assert(read_port(bus, 0x00b4U) == 1U);
    assert(read_port(bus, 0x00b4U) == 0U);

    /* Calendar reads during the documented 150 us rollover window restore
     * the status flag. Exactly at its end they no longer do so. */
    assert(bm_engine_run_for(engine, UINT64_C(149000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);
    assert(read_port(bus, 0x00b4U) == 1U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);
    assert(read_port(bus, 0x00b4U) == 0U);

    destroy_clocked_rtc(engine, bus, rtc);
}

static void
test_programming_rearms_from_exact_bus_boundary(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;

    create_clocked_rtc(&host, &engine, &bus, &rtc);
    assert(bm_engine_run_for(engine, UINT64_C(500000)) == BM_STATUS_OK);
    /* GO clears the subsecond counters at 500 us, moving the next millisecond
     * edge from absolute 1 ms to absolute 1.5 ms. */
    assert(write_port(bus, 0x00b5U, 0U) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(bm_engine_run_for(engine, UINT64_C(499000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);

    destroy_clocked_rtc(engine, bus, rtc);
}

static void
test_engine_reset_starts_a_new_clock_epoch(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;

    create_clocked_rtc(&host, &engine, &bus, &rtc);
    assert(bm_engine_run_for(engine, UINT64_C(1200000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);

    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    bm_mm58167_reset(rtc);
    assert(bm_engine_run_for(engine, UINT64_C(799000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x20U);

    destroy_clocked_rtc(engine, bus, rtc);
}

static void
test_state_restore_rearms_from_restore_boundary(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;
    uint8_t initial_state[BM_MM58167_STATE_SIZE];

    create_clocked_rtc(&host, &engine, &bus, &rtc);
    assert(bm_mm58167_save_state(rtc, initial_state,
                                 sizeof(initial_state)) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(500000)) == BM_STATUS_OK);
    /* Loading persistent state first catches the old state up to 500 us, then
     * starts the restored subsecond phase at that exact virtual boundary. */
    assert(bm_mm58167_load_state(rtc, initial_state,
                                 sizeof(initial_state)) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, UINT64_C(999000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(bm_engine_run_for(engine, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);

    destroy_clocked_rtc(engine, bus, rtc);
}

static void
test_irq_callback_can_change_clock_state_reentrantly(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;
    irq_reentry_t reentry = { NULL, 0U };
    bm_mm58167_config_t rtc_config = {
        0x00b0U, 0x00e0U, reset_subsecond_from_irq, &reentry, NULL, 0U
    };

    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    assert(bm_bus_create(&host, 2U, &bus) == BM_STATUS_OK);
    reentry.bus = bus;
    assert(bm_mm58167_create(&host, bus, &rtc_config, &rtc) == BM_STATUS_OK);
    assert(bm_mm58167_attach_clock(engine, rtc, NULL) == BM_STATUS_OK);
    assert(write_port(bus, 0x00b1U, 0x02U) == BM_STATUS_OK);

    /* The tenth-second IRQ callback issues GO while the source is firing. The
     * adapter must defer self-rearming and use the callback's final state. */
    assert(bm_engine_run_for(engine, UINT64_C(100000000)) == BM_STATUS_OK);
    assert(reentry.assertions == 1U);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(read_port(bus, 0x00b0U) == 0x02U);
    assert(bm_engine_run_for(engine, UINT64_C(1000000)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);

    destroy_clocked_rtc(engine, bus, rtc);
}

static void
test_adapter_rejects_wrong_engine_mode_and_capacity(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t config = { 1U, 1U, 0U };
    bm_engine_t *legacy = NULL;
    bm_engine_t *clocked = NULL;
    bm_engine_t *working = NULL;
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;
    bm_mm58167_config_t rtc_config = {
        0x00b0U, 0x00e0U, NULL, NULL, NULL, 0U
    };
    bm_timed_source_id_t source_id = UINT32_MAX;

    assert(bm_bus_create(&host, 2U, &bus) == BM_STATUS_OK);
    assert(bm_mm58167_create(&host, bus, &rtc_config, &rtc) == BM_STATUS_OK);
    assert(bm_engine_create(&host, &config, &legacy) == BM_STATUS_OK);
    assert(bm_mm58167_attach_clock(legacy, rtc, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create_clocked(&host, &config, &clocked) == BM_STATUS_OK);
    assert(bm_mm58167_attach_clock(clocked, rtc, NULL) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_mm58167_attach_clock(NULL, rtc, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_mm58167_attach_clock(clocked, NULL, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);

    bm_engine_destroy(clocked);
    config.max_timed_sources = 1U;
    assert(bm_engine_create_clocked(&host, &config, &working) == BM_STATUS_OK);
    assert(bm_mm58167_attach_clock(working, rtc, &source_id) == BM_STATUS_OK);
    assert(source_id == 0U);
    assert(bm_mm58167_attach_clock(working, rtc, NULL) ==
           BM_STATUS_INVALID_STATE);

    bm_engine_destroy(working);
    bm_engine_destroy(legacy);
    bm_mm58167_destroy(rtc);
    bm_bus_destroy(bus);
}

int
main(void)
{
    test_lazy_millisecond_and_rollover_boundaries();
    test_programming_rearms_from_exact_bus_boundary();
    test_engine_reset_starts_a_new_clock_epoch();
    test_state_restore_rearms_from_restore_boundary();
    test_irq_callback_can_change_clock_state_reentrantly();
    test_adapter_rejects_wrong_engine_mode_and_capacity();
    return 0;
}
