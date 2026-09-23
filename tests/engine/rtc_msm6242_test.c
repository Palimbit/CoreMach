/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/rtc_msm6242.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

static void
write_reg(bm_bus_t *bus, uint8_t reg, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        .space = BM_ADDRESS_IO, .operation = BM_BUS_WRITE,
        .address = 0x0100U + reg, .value = value,
        .size = 1U, .alignment = 1U
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
}

static uint8_t
read_reg(bm_bus_t *bus, uint8_t reg)
{
    bm_bus_transaction_t transaction = {
        .space = BM_ADDRESS_IO, .operation = BM_BUS_READ,
        .address = 0x0100U + reg, .size = 1U, .alignment = 1U
    };
    assert(bm_bus_transact(bus, &transaction) == BM_STATUS_OK);
    return (uint8_t) transaction.value;
}

int
main(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_bus_t *bus = NULL;
    bm_bus_t *second_bus = NULL;
    bm_msm6242_t *rtc = NULL;
    bm_msm6242_t *second_rtc = NULL;
    bm_msm6242_config_t config = { .io_base = 0x0100U };
    uint8_t state[BM_MSM6242_STATE_SIZE];

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_msm6242_create(&host, bus, &config, &rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x0fU) == 0x04U); /* 24-hour mode. */
    assert(read_reg(bus, 0x06U) == 1U);    /* 1980-01-01 Tuesday. */
    assert(read_reg(bus, 0x08U) == 1U);
    assert(read_reg(bus, 0x0bU) == 8U);
    assert(read_reg(bus, 0x0cU) == 2U);
    assert(bm_bus_create(&host, 1U, &second_bus) == BM_STATUS_OK);
    assert(bm_msm6242_create(&host, second_bus, &config, &second_rtc) ==
           BM_STATUS_OK);
    assert(bm_msm6242_advance_second(second_rtc) == BM_STATUS_OK);
    assert(read_reg(second_bus, 0x00U) == 1U);
    assert(read_reg(bus, 0x00U) == 0U);
    bm_msm6242_destroy(second_rtc);
    bm_bus_destroy(second_bus);

    /* 1984-02-28 23:59:59 -> leap day, then March 1. */
    write_reg(bus, 0x00U, 9U); write_reg(bus, 0x01U, 5U);
    write_reg(bus, 0x02U, 9U); write_reg(bus, 0x03U, 5U);
    write_reg(bus, 0x04U, 3U); write_reg(bus, 0x05U, 2U);
    write_reg(bus, 0x06U, 8U); write_reg(bus, 0x07U, 2U);
    write_reg(bus, 0x08U, 2U); write_reg(bus, 0x09U, 0U);
    write_reg(bus, 0x0aU, 4U); write_reg(bus, 0x0bU, 8U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x06U) == 9U);
    assert(read_reg(bus, 0x07U) == 2U);
    assert(read_reg(bus, 0x08U) == 2U);
    assert(read_reg(bus, 0x00U) == 0U);
    write_reg(bus, 0x00U, 9U); write_reg(bus, 0x01U, 5U);
    write_reg(bus, 0x02U, 9U); write_reg(bus, 0x03U, 5U);
    write_reg(bus, 0x04U, 3U); write_reg(bus, 0x05U, 2U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x06U) == 1U);
    assert(read_reg(bus, 0x07U) == 0U);
    assert(read_reg(bus, 0x08U) == 3U);

    /* HOLD and STOP suspend the calendar; BUSY stays clear for atomic I/O. */
    write_reg(bus, 0x0dU, 1U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x00U) == 0U);
    assert(read_reg(bus, 0x0dU) == 1U);
    write_reg(bus, 0x0dU, 0U);
    assert(read_reg(bus, 0x00U) == 1U); /* Deferred HOLD carry. */
    write_reg(bus, 0x0fU, 6U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x00U) == 1U);
    write_reg(bus, 0x0fU, 4U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x00U) == 2U);

    /* 30-second adjustment rounds the minute without host time. */
    write_reg(bus, 0x00U, 5U); write_reg(bus, 0x01U, 3U);
    write_reg(bus, 0x0dU, 8U);
    assert(read_reg(bus, 0x00U) == 0U);
    assert(read_reg(bus, 0x01U) == 0U);
    assert(read_reg(bus, 0x02U) == 1U);
    assert(read_reg(bus, 0x0dU) == 0U);

    assert(bm_msm6242_save_state(rtc, state, sizeof(state)) == BM_STATUS_OK);
    assert(state[0x0fU] == 4U);
    assert(bm_msm6242_save_state(rtc, state, sizeof(state) - 1U) ==
           BM_STATUS_INVALID_ARGUMENT);
    bm_msm6242_destroy(rtc);
    bm_bus_destroy(bus);

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    config.initial_state = state;
    config.initial_state_size = sizeof(state);
    rtc = NULL;
    assert(bm_msm6242_create(&host, bus, &config, &rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x08U) == 3U);
    write_reg(bus, 0x0fU, 5U); /* Assert REST to select 12-hour mode. */
    write_reg(bus, 0x0fU, 1U);
    write_reg(bus, 0x0fU, 0U);
    write_reg(bus, 0x04U, 1U); write_reg(bus, 0x05U, 1U); /* 11 AM. */
    write_reg(bus, 0x02U, 9U); write_reg(bus, 0x03U, 5U);
    write_reg(bus, 0x00U, 9U); write_reg(bus, 0x01U, 5U);
    assert(bm_msm6242_advance_second(rtc) == BM_STATUS_OK);
    assert(read_reg(bus, 0x04U) == 2U);
    assert(read_reg(bus, 0x05U) == 5U); /* 12 PM. */
    bm_msm6242_destroy(rtc);
    bm_bus_destroy(bus);

    state[0] = 0x1fU;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    rtc = NULL;
    assert(bm_msm6242_create(&host, bus, &config, &rtc) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(rtc == NULL);
    bm_bus_destroy(bus);
    return 0;
}
