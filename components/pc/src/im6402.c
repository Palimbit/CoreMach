/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Intersil IM6402 CMOS UART, character-level ([IM6402]). */
#include <blumach/components/im6402.h>

#include <string.h>

#define CLOCKS_PER_BIT 16U

struct bm_im6402 {
    bm_host_services_t host;
    bm_im6402_config_t config;
    bm_im6402_control_t control;
    uint8_t transmitter_buffer;
    uint8_t transmitter_register;
    uint8_t buffer_full;      /* TBRE low */
    uint8_t register_busy;    /* TRE low */
    uint64_t clocks_left;     /* of the character in the register */
    uint8_t receiver_buffer;
    uint8_t dr;
    uint8_t oe;
    uint8_t fe;
    uint8_t pe;
};

static uint8_t
character_mask(const bm_im6402_t *uart)
{
    return (uint8_t) ((1U << uart->control.character_bits) - 1U);
}

static void
set_data_ready(bm_im6402_t *uart, int level)
{
    level = level != 0;
    if (uart->dr == (uint8_t) level)
        return;
    uart->dr = (uint8_t) level;
    if (uart->config.data_ready != NULL)
        uart->config.data_ready(uart->config.context, level);
}

/* Moves the buffer into the empty transmitter register ([IM6402] Figure 2:
 * data transfers 0 to 1 clock after TBRL; modelled as at once). */
static void
start_next(bm_im6402_t *uart)
{
    if (uart->register_busy || !uart->buffer_full)
        return;
    uart->transmitter_register = (uint8_t) (uart->transmitter_buffer &
                                            character_mask(uart));
    uart->buffer_full = 0U;
    uart->register_busy = 1U;
    uart->clocks_left = bm_im6402_frame_clocks(uart);
}

bm_status_t
bm_im6402_create(const bm_host_services_t *host,
                 const bm_im6402_config_t *config, bm_im6402_t **out_uart)
{
    bm_im6402_t *uart;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) ||
        (config == NULL) || (out_uart == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    uart = host->allocate(host->context, sizeof(*uart));
    if (uart == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(uart, 0, sizeof(*uart));
    uart->host = *host;
    uart->config = *config;
    uart->control.character_bits = 8U;
    uart->control.parity_inhibit = 1U;
    bm_im6402_master_reset(uart);
    *out_uart = uart;
    return BM_STATUS_OK;
}

void
bm_im6402_destroy(bm_im6402_t *uart)
{
    if (uart != NULL)
        uart->host.release(uart->host.context, uart);
}

void
bm_im6402_master_reset(bm_im6402_t *uart)
{
    uart->pe = 0U;
    uart->fe = 0U;
    uart->oe = 0U;
    set_data_ready(uart, 0);
    uart->buffer_full = 0U;
    uart->register_busy = 0U;
    uart->clocks_left = 0U;
}

void
bm_im6402_load_control(bm_im6402_t *uart, const bm_im6402_control_t *control)
{
    uart->control = *control;
    if (uart->control.character_bits < 5U)
        uart->control.character_bits = 5U;
    else if (uart->control.character_bits > 8U)
        uart->control.character_bits = 8U;
    uart->control.parity_inhibit = control->parity_inhibit != 0U;
    uart->control.even_parity = control->even_parity != 0U;
    uart->control.stop_select = control->stop_select != 0U;
}

bm_im6402_control_t
bm_im6402_control(const bm_im6402_t *uart)
{
    return uart->control;
}

uint32_t
bm_im6402_frame_clocks(const bm_im6402_t *uart)
{
    /* Start bit, data bits and parity, then the stop bits: one, or with
     * SBS 1.5 for five-bit characters and 2 otherwise ([IM6402] pin 36). */
    uint32_t half_bits = 2U * (1U + uart->control.character_bits +
                               (uart->control.parity_inhibit ? 0U : 1U));

    if (!uart->control.stop_select)
        half_bits += 2U;
    else if (uart->control.character_bits == 5U)
        half_bits += 3U;
    else
        half_bits += 4U;
    return half_bits * (CLOCKS_PER_BIT / 2U);
}

void
bm_im6402_load_transmitter(bm_im6402_t *uart, uint8_t data)
{
    uart->transmitter_buffer = data;
    uart->buffer_full = 1U;
    start_next(uart);
}

void
bm_im6402_advance(bm_im6402_t *uart, uint64_t clocks)
{
    while ((clocks > 0U) && uart->register_busy) {
        if (clocks < uart->clocks_left) {
            uart->clocks_left -= clocks;
            return;
        }
        clocks -= uart->clocks_left;
        uart->clocks_left = 0U;
        uart->register_busy = 0U;
        if (uart->config.transmit != NULL)
            uart->config.transmit(uart->config.context,
                                  uart->transmitter_register);
        start_next(uart);
    }
}

uint64_t
bm_im6402_clocks_until_transmitted(const bm_im6402_t *uart)
{
    return uart->register_busy ? uart->clocks_left : UINT64_MAX;
}

void
bm_im6402_receive(bm_im6402_t *uart, uint8_t data, int framing_error,
                  int parity_error)
{
    /* Overrun: DR was not cleared before this character was transferred
     * ([IM6402] pin 15). */
    uart->oe = uart->dr;
    uart->fe = framing_error != 0;
    uart->pe = (uart->control.parity_inhibit || !parity_error) ? 0U : 1U;
    uart->receiver_buffer = (uint8_t) (data & character_mask(uart));
    set_data_ready(uart, 1);
}

uint8_t
bm_im6402_receiver_buffer(const bm_im6402_t *uart)
{
    return uart->receiver_buffer;
}

void
bm_im6402_reset_data_received(bm_im6402_t *uart)
{
    set_data_ready(uart, 0);
}

bm_im6402_status_t
bm_im6402_status(const bm_im6402_t *uart)
{
    bm_im6402_status_t status;

    status.tbre = uart->buffer_full ? 0U : 1U;
    status.tre = uart->register_busy ? 0U : 1U;
    status.dr = uart->dr;
    status.oe = uart->oe;
    status.fe = uart->fe;
    status.pe = uart->pe;
    return status;
}
