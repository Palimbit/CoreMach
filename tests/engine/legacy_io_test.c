/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/lpt_spp.h>
#include <blumach/components/uart16450.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct io_sink {
    uint8_t transmitted;
    unsigned int transmit_count;
    int lpt_irq;
    int uart_irq;
} io_sink_t;

static void capture_transmit(void *context, uint8_t value)
{
    io_sink_t *sink = context;
    sink->transmitted = value;
    ++sink->transmit_count;
}

static void capture_lpt_irq(void *context, int asserted)
{
    ((io_sink_t *) context)->lpt_irq = asserted;
}

static void capture_uart_irq(void *context, int asserted)
{
    ((io_sink_t *) context)->uart_irq = asserted;
}

static void test_lpt(const bm_host_services_t *host, io_sink_t *sink)
{
    bm_lpt_spp_t *lpt = NULL;
    bm_lpt_spp_config_t configuration = { NULL, capture_lpt_irq, sink };
    bm_lpt_spp_state_t state;
    uint8_t value;

    assert(bm_lpt_spp_create(host, &configuration, &lpt) == BM_STATUS_OK);
    assert(bm_lpt_spp_read(lpt, 1U, &value) == BM_STATUS_OK && value == 0xdfU);
    assert(bm_lpt_spp_read(lpt, 2U, &value) == BM_STATUS_OK && value == 0xe0U);
    assert(bm_lpt_spp_write(lpt, 0U, 0xaaU) == BM_STATUS_OK);
    assert(bm_lpt_spp_read(lpt, 0U, &value) == BM_STATUS_OK && value == 0xaaU);
    assert(bm_lpt_spp_write(lpt, 2U, 0x30U) == BM_STATUS_OK);
    assert(bm_lpt_spp_read(lpt, 2U, &value) == BM_STATUS_OK && value == 0xf0U);
    assert(bm_lpt_spp_set_status(lpt, 0x9fU) == BM_STATUS_OK && sink->lpt_irq);
    assert(bm_lpt_spp_set_status(lpt, 0xdfU) == BM_STATUS_OK && !sink->lpt_irq);
    assert(bm_lpt_spp_set_status(lpt, 0x9fU) == BM_STATUS_OK && sink->lpt_irq);
    assert(bm_lpt_spp_write(lpt, 2U, 0U) == BM_STATUS_OK && !sink->lpt_irq);
    assert(bm_lpt_spp_state(lpt, &state) == BM_STATUS_OK);
    assert(state.data == 0xaaU && state.status == 0x9fU && state.control == 0U);
    bm_lpt_spp_destroy(lpt);
}

static void test_uart(const bm_host_services_t *host, io_sink_t *sink)
{
    bm_uart16450_t *uart = NULL;
    bm_uart16450_config_t configuration = {
        capture_transmit, capture_uart_irq, sink
    };
    bm_uart16450_state_t state;
    uint8_t value;

    assert(bm_uart16450_create(host, &configuration, &uart) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 5U, &value) == BM_STATUS_OK);
    assert((value & 0x60U) == 0x60U);
    assert(bm_uart16450_write(uart, 3U, 0x80U) == BM_STATUS_OK);
    assert(bm_uart16450_write(uart, 0U, 0x0cU) == BM_STATUS_OK);
    assert(bm_uart16450_write(uart, 1U, 0x01U) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 0U, &value) == BM_STATUS_OK && value == 0x0cU);
    assert(bm_uart16450_read(uart, 1U, &value) == BM_STATUS_OK && value == 0x01U);
    assert(bm_uart16450_write(uart, 3U, 0x03U) == BM_STATUS_OK);
    assert(bm_uart16450_write(uart, 4U, 0x1aU) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 6U, &value) == BM_STATUS_OK);
    assert((value >> 4U) == 0x09U);
    assert(bm_uart16450_write(uart, 4U, 0x10U) == BM_STATUS_OK);
    assert(bm_uart16450_write(uart, 0U, 0x5aU) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 0U, &value) == BM_STATUS_OK && value == 0x5aU);
    assert(bm_uart16450_write(uart, 4U, 0U) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 6U, &value) == BM_STATUS_OK);
    assert((value & 0xf0U) == 0U);
    assert(bm_uart16450_write(uart, 0U, 0xa5U) == BM_STATUS_OK);
    assert(sink->transmit_count == 1U && sink->transmitted == 0xa5U);
    assert(bm_uart16450_write(uart, 1U, 0x02U) == BM_STATUS_OK && sink->uart_irq);
    assert(bm_uart16450_read(uart, 2U, &value) == BM_STATUS_OK);
    assert((value & 0x0fU) == 0x02U && !sink->uart_irq);
    assert(bm_uart16450_receive(uart, 0x33U) == BM_STATUS_OK);
    assert(bm_uart16450_write(uart, 1U, 0x01U) == BM_STATUS_OK && sink->uart_irq);
    assert(bm_uart16450_read(uart, 0U, &value) == BM_STATUS_OK && value == 0x33U);
    assert(!sink->uart_irq);
    assert(bm_uart16450_write(uart, 7U, 0x96U) == BM_STATUS_OK);
    assert(bm_uart16450_read(uart, 7U, &value) == BM_STATUS_OK && value == 0x96U);
    assert(bm_uart16450_state(uart, &state) == BM_STATUS_OK);
    assert(state.scratch == 0x96U && state.divisor == 0x010cU);
    bm_uart16450_destroy(uart);
}

int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    io_sink_t sink = { 0 };
    test_lpt(&host, &sink);
    test_uart(&host, &sink);
    return 0;
}
