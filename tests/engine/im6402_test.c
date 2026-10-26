/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Intersil IM6402 UART contracts ([IM6402]): frame lengths, end-to-end
 * transmission, received data, flags and master reset. */
#include <blumach/components/im6402.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

typedef struct log {
    unsigned int sent;
    uint8_t data[8];
    int dr_level;
    unsigned int dr_changes;
} log_t;

static void
sent(void *context, uint8_t data)
{
    log_t *log = context;
    assert(log->sent < 8U);
    log->data[log->sent++] = data;
}

static void
ready(void *context, int level)
{
    log_t *log = context;
    log->dr_level = level;
    ++log->dr_changes;
}

static bm_im6402_t *
create(log_t *log)
{
    const bm_host_services_t host = bm_null_host_services();
    const bm_im6402_config_t config = { sent, ready, log };
    bm_im6402_t *uart = NULL;
    memset(log, 0, sizeof(*log));
    assert(bm_im6402_create(&host, &config, &uart) == BM_STATUS_OK);
    return uart;
}

static void
format(bm_im6402_t *uart, unsigned int bits, int parity, int even, int sbs)
{
    const bm_im6402_control_t control = {
        (uint8_t) bits, (uint8_t) (parity ? 0U : 1U), (uint8_t) even,
        (uint8_t) sbs
    };
    bm_im6402_load_control(uart, &control);
}

static void
test_frames(void)
{
    log_t log;
    bm_im6402_t *uart = create(&log);

    format(uart, 8U, 0, 0, 0);                   /* 8N1: 10 bits */
    assert(bm_im6402_frame_clocks(uart) == 160U);
    format(uart, 7U, 1, 1, 0);                   /* 7E1: 10 bits */
    assert(bm_im6402_frame_clocks(uart) == 160U);
    format(uart, 8U, 0, 0, 1);                   /* 8N2: 11 bits */
    assert(bm_im6402_frame_clocks(uart) == 176U);
    format(uart, 5U, 0, 0, 1);                   /* 5N1.5: 7.5 bits */
    assert(bm_im6402_frame_clocks(uart) == 120U);
    format(uart, 6U, 1, 0, 1);                   /* 6O2: 10 bits */
    assert(bm_im6402_frame_clocks(uart) == 160U);
    format(uart, 3U, 0, 0, 0);                   /* clamped to 5 */
    assert(bm_im6402_control(uart).character_bits == 5U);
    bm_im6402_destroy(uart);
}

static void
test_transmit_end_to_end(void)
{
    log_t log;
    bm_im6402_t *uart = create(&log);
    bm_im6402_status_t status;

    format(uart, 7U, 0, 0, 0);                   /* 7N1: 9 bits, 144 */
    status = bm_im6402_status(uart);
    assert(status.tbre && status.tre);
    assert(bm_im6402_clocks_until_transmitted(uart) == UINT64_MAX);

    bm_im6402_load_transmitter(uart, 0xc1U);     /* moves on at once */
    status = bm_im6402_status(uart);
    assert(status.tbre && !status.tre);
    assert(bm_im6402_clocks_until_transmitted(uart) == 144U);
    bm_im6402_load_transmitter(uart, 0x42U);     /* waits in the buffer */
    assert(!bm_im6402_status(uart).tbre);

    bm_im6402_advance(uart, 143U);
    assert(log.sent == 0U);
    bm_im6402_advance(uart, 1U);
    assert((log.sent == 1U) && (log.data[0] == 0x41U)); /* 7 bits */
    status = bm_im6402_status(uart);
    assert(status.tbre && !status.tre);          /* second follows */
    assert(bm_im6402_clocks_until_transmitted(uart) == 144U);
    bm_im6402_advance(uart, 1000U);
    assert((log.sent == 2U) && (log.data[1] == 0x42U));
    assert(bm_im6402_status(uart).tre);
    bm_im6402_destroy(uart);
}

static void
test_receive_flags(void)
{
    log_t log;
    bm_im6402_t *uart = create(&log);
    bm_im6402_status_t status;

    format(uart, 7U, 1, 1, 0);
    bm_im6402_receive(uart, 0xffU, 0, 1);
    status = bm_im6402_status(uart);
    assert(status.dr && !status.oe && !status.fe && status.pe);
    assert(bm_im6402_receiver_buffer(uart) == 0x7fU); /* unused MSB low */
    assert((log.dr_level == 1) && (log.dr_changes == 1U));

    bm_im6402_receive(uart, 0x31U, 1, 0);        /* DR still high */
    status = bm_im6402_status(uart);
    assert(status.oe && status.fe && !status.pe);
    assert(bm_im6402_receiver_buffer(uart) == 0x31U);
    assert(log.dr_changes == 1U);                /* no new edge */

    bm_im6402_reset_data_received(uart);         /* DRR */
    assert(!bm_im6402_status(uart).dr && (log.dr_level == 0));
    bm_im6402_receive(uart, 0x32U, 0, 0);
    assert(!bm_im6402_status(uart).oe);

    format(uart, 8U, 0, 0, 0);                   /* PI forces PE low */
    bm_im6402_reset_data_received(uart);
    bm_im6402_receive(uart, 0x80U, 0, 1);
    assert(!bm_im6402_status(uart).pe);

    /* MR clears the flags and DR but not the receiver buffer. */
    bm_im6402_load_transmitter(uart, 0x55U);
    bm_im6402_master_reset(uart);
    status = bm_im6402_status(uart);
    assert(!status.dr && !status.oe && !status.fe && !status.pe);
    assert(status.tbre && status.tre);
    assert(bm_im6402_receiver_buffer(uart) == 0x80U);
    bm_im6402_advance(uart, 1000U);
    assert(log.sent == 0U);                      /* abandoned */
    bm_im6402_destroy(uart);
}

int
main(void)
{
    test_frames();
    test_transmit_end_to_end();
    test_receive_flags();
    return 0;
}
