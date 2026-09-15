/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/rtc_mm58167.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct irq_sink {
    unsigned int changes;
    int asserted;
} irq_sink_t;

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static uint8_t
read_port(bm_bus_t *bus, uint16_t port)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

static void
capture_irq(void *context, int asserted)
{
    irq_sink_t *sink = context;
    ++sink->changes;
    sink->asserted = asserted;
}

static void
write_datetime(bm_bus_t *bus,
               uint8_t second,
               uint8_t minute,
               uint8_t hour,
               uint8_t weekday,
               uint8_t day,
               uint8_t month)
{
    assert(write_port(bus, 0x00e2U, second) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e3U, minute) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e4U, hour) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e5U, weekday) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e6U, day) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e7U, month) == BM_STATUS_OK);
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;
    irq_sink_t irq = { 0, 0 };
    bm_mm58167_config_t config = {
        0x00b0U, 0x00e0U, capture_irq, &irq, NULL, 0U
    };
    uint8_t state[BM_MM58167_STATE_SIZE];
    uint8_t restored[BM_MM58167_STATE_SIZE];

    assert(bm_bus_create(&host, 2, &bus) == BM_STATUS_OK);
    assert(bm_mm58167_create(&host, bus, &config, &rtc) == BM_STATUS_OK);

    assert(read_port(bus, 0x00e0U) == 0U);
    assert(read_port(bus, 0x00e2U) == 0U);
    assert(read_port(bus, 0x00e5U) == 1U);
    assert(read_port(bus, 0x00e6U) == 1U);
    assert(read_port(bus, 0x00e7U) == 1U);
    assert(read_port(bus, 0x00b0U) == 0U);
    assert(read_port(bus, 0x00b1U) == 0U);

    assert(bm_mm58167_advance_microseconds(rtc, 999U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(bm_mm58167_advance_microseconds(rtc, 1U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0x10U);
    assert(read_port(bus, 0x00b4U) == 1U);
    assert(read_port(bus, 0x00b4U) == 0U);

    write_datetime(bus, 0x59U, 0x59U, 0x23U, 0x07U, 0x31U, 0x12U);
    assert(bm_mm58167_advance_microseconds(rtc, 999000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e2U) == 0U);
    assert(read_port(bus, 0x00e3U) == 0U);
    assert(read_port(bus, 0x00e4U) == 0U);
    assert(read_port(bus, 0x00e5U) == 1U);
    assert(read_port(bus, 0x00e6U) == 1U);
    assert(read_port(bus, 0x00e7U) == 1U);

    assert(write_port(bus, 0x00b1U, 0x02U) == BM_STATUS_OK);
    assert(bm_mm58167_advance_microseconds(rtc, 100000U) == BM_STATUS_OK);
    assert(irq.asserted == 1);
    assert(read_port(bus, 0x00b0U) == 0x02U);
    assert(irq.asserted == 0);
    assert(irq.changes == 2U);

    assert(write_port(bus, 0x00e8U, 0xc0U) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e9U, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00eaU, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00ebU, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00ecU, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00edU, 0x0cU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00eeU, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00efU, 0xccU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00b1U, 0x01U) == BM_STATUS_OK);
    assert(bm_mm58167_advance_microseconds(rtc, 1000U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b0U) == 0x01U);

    assert(write_port(bus, 0x00b2U, 0xffU) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e0U) == 0U);
    assert(read_port(bus, 0x00e1U) == 0U);
    assert(read_port(bus, 0x00e5U) == 1U);
    assert(write_port(bus, 0x00b3U, 0xffU) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e9U) == 0U);
    assert(write_port(bus, 0x00b5U, 0x5aU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00b6U, 0x7eU) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b6U) == 0x7eU);
    assert(write_port(bus, 0x00b7U, 0xa5U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b7U) == 0xa5U);

    assert(write_port(bus, 0x00e2U, 0x42U) == BM_STATUS_OK);
    assert(bm_mm58167_save_state(rtc, state, sizeof(state)) == BM_STATUS_OK);
    assert(write_port(bus, 0x00e2U, 0x11U) == BM_STATUS_OK);
    assert(bm_mm58167_load_state(rtc, state, sizeof(state)) == BM_STATUS_OK);
    assert(read_port(bus, 0x00e2U) == 0x42U);
    assert(bm_mm58167_save_state(rtc, restored, sizeof(restored)) == BM_STATUS_OK);
    assert(memcmp(state, restored, sizeof(state)) == 0);

    assert(bm_mm58167_set_interrupt_status(rtc, 0xa5U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b0U) == 0xa5U);
    assert(bm_mm58167_interrupt_control(rtc) == 0x01U);
    bm_mm58167_reset(rtc);
    assert(read_port(bus, 0x00b0U) == 0U);
    assert(bm_mm58167_advance_microseconds(NULL, 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_mm58167_save_state(rtc, state, sizeof(state) - 1U) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_mm58167_load_state(rtc, state, sizeof(state) - 1U) == BM_STATUS_INVALID_ARGUMENT);

    bm_mm58167_destroy(rtc);
    bm_bus_destroy(bus);
    return 0;
}
