/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/rtc_mm58167.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    return bm_bus_transact(bus, &transaction);
}

static bm_status_t
read_port(bm_bus_t *bus, uint16_t port, uint8_t *value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_READ, port, 0, 1, 1, 0, BM_ENDIAN_LITTLE, 0
    };
    bm_status_t status = bm_bus_transact(bus, &transaction);
    if (status == BM_STATUS_OK)
        *value = (uint8_t) transaction.value;
    return status;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_mm58167_t *rtc = NULL;
    bm_mm58167_config_t config = { 0x00b0U };
    uint8_t value = 0xffU;

    assert(bm_bus_create(&host, 1, &bus) == BM_STATUS_OK);
    assert(bm_mm58167_create(&host, bus, &config, &rtc) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b0U, &value) == BM_STATUS_OK && value == 0);
    assert(read_port(bus, 0x00b1U, &value) == BM_STATUS_OK && value == 0);

    assert(bm_mm58167_set_interrupt_status(rtc, 0xa5U) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b0U, &value) == BM_STATUS_OK && value == 0xa5U);
    assert(read_port(bus, 0x00b0U, &value) == BM_STATUS_OK && value == 0);

    assert(bm_mm58167_set_interrupt_status(rtc, 0x3cU) == BM_STATUS_OK);
    assert(write_port(bus, 0x00b1U, 0x81U) == BM_STATUS_OK);
    assert(bm_mm58167_interrupt_control(rtc) == 0x81U);
    assert(read_port(bus, 0x00b0U, &value) == BM_STATUS_OK && value == 0);
    assert(read_port(bus, 0x00b1U, &value) == BM_STATUS_OK && value == 0x81U);

    /* Interrupt status is read-only and unsupported RTC registers stay strict. */
    assert(write_port(bus, 0x00b0U, 0xffU) == BM_STATUS_OK);
    assert(read_port(bus, 0x00b0U, &value) == BM_STATUS_OK && value == 0);
    assert(read_port(bus, 0x00b2U, &value) == BM_STATUS_UNMAPPED);

    bm_mm58167_reset(rtc);
    assert(bm_mm58167_interrupt_control(rtc) == 0);
    assert(bm_mm58167_set_interrupt_status(NULL, 1) == BM_STATUS_INVALID_ARGUMENT);

    bm_mm58167_destroy(rtc);
    bm_bus_destroy(bus);
    return 0;
}
