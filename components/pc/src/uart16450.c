/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2016-2025 Miran Grca
 * Copyright 2017-2020 Fred N. van Kempen
 * Copyright 2026 BluMach contributors
 *
 * Derived rewrite of BluMach's inherited NS8250/16450 register model. This
 * component has explicit ownership and callbacks and intentionally omits the
 * 16550 FIFO, host serial backends and real-time baud scheduling.
 */
#include <blumach/components/uart16450.h>

#include <string.h>

struct bm_uart16450 {
    bm_host_services_t host;
    bm_uart16450_config_t configuration;
    uint16_t divisor;
    uint8_t receive_buffer;
    uint8_t transmit_holding;
    uint8_t interrupt_enable;
    uint8_t interrupt_identification;
    uint8_t line_control;
    uint8_t modem_control;
    uint8_t line_status;
    uint8_t modem_status;
    uint8_t scratch;
    int transmit_interrupt_pending;
    int irq_asserted;
};

static void
update_interrupts(bm_uart16450_t *uart)
{
    uint8_t identification = 0x01U;

    if (((uart->interrupt_enable & 0x04U) != 0) &&
        ((uart->line_status & 0x1eU) != 0))
        identification = 0x06U;
    else if (((uart->interrupt_enable & 0x01U) != 0) &&
             ((uart->line_status & 0x01U) != 0))
        identification = 0x04U;
    else if (((uart->interrupt_enable & 0x02U) != 0) &&
             uart->transmit_interrupt_pending)
        identification = 0x02U;
    else if (((uart->interrupt_enable & 0x08U) != 0) &&
             ((uart->modem_status & 0x0fU) != 0))
        identification = 0x00U;

    uart->interrupt_identification = identification;
    if (uart->irq_asserted != ((identification & 0x01U) == 0)) {
        uart->irq_asserted = (identification & 0x01U) == 0;
        if (uart->configuration.irq != NULL)
            uart->configuration.irq(uart->configuration.context,
                                    uart->irq_asserted);
    }
}

static uint8_t
loopback_modem_status(uint8_t modem_control)
{
    uint8_t result = 0;
    if ((modem_control & 0x02U) != 0)
        result |= 0x10U; /* RTS -> CTS. */
    if ((modem_control & 0x01U) != 0)
        result |= 0x20U; /* DTR -> DSR. */
    if ((modem_control & 0x04U) != 0)
        result |= 0x40U; /* OUT1 -> RI. */
    if ((modem_control & 0x08U) != 0)
        result |= 0x80U; /* OUT2 -> DCD. */
    return result;
}

bm_status_t
bm_uart16450_create(const bm_host_services_t *host,
                    const bm_uart16450_config_t *configuration,
                    bm_uart16450_t **out_uart)
{
    bm_uart16450_t *uart;
    if ((bm_host_services_validate(host) != BM_STATUS_OK) ||
        (configuration == NULL) || (out_uart == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_uart = NULL;
    uart = host->allocate(host->context, sizeof(*uart));
    if (uart == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(uart, 0, sizeof(*uart));
    uart->host = *host;
    uart->configuration = *configuration;
    bm_uart16450_reset(uart);
    *out_uart = uart;
    return BM_STATUS_OK;
}

void
bm_uart16450_destroy(bm_uart16450_t *uart)
{
    if (uart != NULL)
        uart->host.release(uart->host.context, uart);
}

void
bm_uart16450_reset(bm_uart16450_t *uart)
{
    if (uart == NULL)
        return;
    if (uart->irq_asserted && (uart->configuration.irq != NULL))
        uart->configuration.irq(uart->configuration.context, 0);
    uart->divisor = 0;
    uart->receive_buffer = 0;
    uart->transmit_holding = 0;
    uart->interrupt_enable = 0;
    uart->interrupt_identification = 0x01U;
    uart->line_control = 0;
    uart->modem_control = 0;
    uart->line_status = 0x60U;
    uart->modem_status = 0;
    uart->scratch = 0;
    uart->transmit_interrupt_pending = 0;
    uart->irq_asserted = 0;
}

bm_status_t
bm_uart16450_write(bm_uart16450_t *uart,
                   unsigned int register_index,
                   uint8_t value)
{
    uint8_t old_modem;
    if ((uart == NULL) || (register_index > 7U))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((uart->line_control & 0x80U) != 0) {
        if (register_index == 0U) {
            uart->divisor = (uart->divisor & 0xff00U) | value;
            return BM_STATUS_OK;
        }
        if (register_index == 1U) {
            uart->divisor = (uart->divisor & 0x00ffU) | ((uint16_t) value << 8U);
            return BM_STATUS_OK;
        }
    }
    switch (register_index) {
        case 0:
            uart->transmit_holding = value;
            uart->transmit_interrupt_pending = 1;
            if ((uart->modem_control & 0x10U) != 0) {
                if ((uart->line_status & 0x01U) != 0)
                    uart->line_status |= 0x02U;
                uart->receive_buffer = value;
                uart->line_status |= 0x01U;
            } else if (uart->configuration.transmit != NULL)
                uart->configuration.transmit(uart->configuration.context, value);
            break;
        case 1:
            uart->interrupt_enable = value & 0x0fU;
            if (((value & 0x02U) != 0) && ((uart->line_status & 0x20U) != 0))
                uart->transmit_interrupt_pending = 1;
            break;
        case 2:
            /* 16450 has no FIFO control register. */
            break;
        case 3:
            uart->line_control = value;
            break;
        case 4:
            old_modem = uart->modem_status & 0xf0U;
            uart->modem_control = value & 0x1fU;
            {
                uint8_t new_modem = (uart->modem_control & 0x10U) != 0 ?
                                    loopback_modem_status(uart->modem_control) : 0U;
                uart->modem_status = new_modem;
                if (((old_modem ^ new_modem) & 0x10U) != 0)
                    uart->modem_status |= 0x01U;
                if (((old_modem ^ new_modem) & 0x20U) != 0)
                    uart->modem_status |= 0x02U;
                if (((old_modem & 0x40U) != 0) && ((new_modem & 0x40U) == 0))
                    uart->modem_status |= 0x04U;
                if (((old_modem ^ new_modem) & 0x80U) != 0)
                    uart->modem_status |= 0x08U;
            }
            break;
        case 7:
            uart->scratch = value;
            break;
        default:
            break;
    }
    update_interrupts(uart);
    return BM_STATUS_OK;
}

bm_status_t
bm_uart16450_read(bm_uart16450_t *uart,
                  unsigned int register_index,
                  uint8_t *value)
{
    if ((uart == NULL) || (value == NULL) || (register_index > 7U))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((uart->line_control & 0x80U) != 0) {
        if (register_index == 0U) {
            *value = (uint8_t) uart->divisor;
            return BM_STATUS_OK;
        }
        if (register_index == 1U) {
            *value = (uint8_t) (uart->divisor >> 8U);
            return BM_STATUS_OK;
        }
    }
    switch (register_index) {
        case 0:
            *value = uart->receive_buffer;
            uart->line_status &= 0xfeU;
            break;
        case 1:
            *value = uart->interrupt_enable;
            break;
        case 2:
            *value = uart->interrupt_identification;
            if ((*value & 0x0fU) == 0x02U)
                uart->transmit_interrupt_pending = 0;
            break;
        case 3:
            *value = uart->line_control;
            break;
        case 4:
            *value = uart->modem_control;
            break;
        case 5:
            *value = uart->line_status;
            uart->line_status &= 0xe1U;
            break;
        case 6:
            *value = uart->modem_status;
            uart->modem_status &= 0xf0U;
            break;
        case 7:
            *value = uart->scratch;
            break;
        default:
            return BM_STATUS_INVALID_ARGUMENT;
    }
    update_interrupts(uart);
    return BM_STATUS_OK;
}

bm_status_t
bm_uart16450_receive(bm_uart16450_t *uart, uint8_t value)
{
    if (uart == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    if ((uart->line_status & 0x01U) != 0)
        uart->line_status |= 0x02U;
    uart->receive_buffer = value;
    uart->line_status |= 0x01U;
    update_interrupts(uart);
    return BM_STATUS_OK;
}

bm_status_t
bm_uart16450_state(const bm_uart16450_t *uart, bm_uart16450_state_t *state)
{
    if ((uart == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *state = (bm_uart16450_state_t) {
        uart->divisor,
        uart->interrupt_enable,
        uart->interrupt_identification,
        uart->line_control,
        uart->modem_control,
        uart->line_status,
        uart->modem_status,
        uart->scratch
    };
    return BM_STATUS_OK;
}
