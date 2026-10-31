/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Intersil IM6402 CMOS UART (Olivetti M10 serial controller).
 *
 * Source: Intersil IM6402/IM6403 Universal Asynchronous Receiver
 * Transmitter data sheet ([IM6402]): functional pin definitions and the
 * transmitter and receiver operation notes.
 *
 * The component is character-level. Time is counted in register clocks:
 * TRC and RRC run at 16 times the data rate ([IM6402] pins 17 and 40), so
 * one bit is 16 clocks and a frame is start bit + 5-8 data bits + optional
 * parity + 1, 1.5 or 2 stop bits. The transmitter completes a character
 * after its frame; a received character is delivered whole by the board
 * when its frame has elapsed on the line. */
#ifndef BLUMACH_COMPONENTS_IM6402_H
#define BLUMACH_COMPONENTS_IM6402_H

#include <blumach/engine/host.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bm_im6402 bm_im6402_t;

/* Called when a character has left TRO, stop bits included. data holds the
 * character bits only, right-justified. */
typedef void (*bm_im6402_transmit_fn)(void *context, uint8_t data);
/* Called when DR (data received) changes level. */
typedef void (*bm_im6402_data_ready_fn)(void *context, int level);

typedef struct bm_im6402_config {
    bm_im6402_transmit_fn transmit;
    bm_im6402_data_ready_fn data_ready;
    void *context;
} bm_im6402_config_t;

/* Control register inputs, loaded by CRL ([IM6402] pins 34-39). */
typedef struct bm_im6402_control {
    uint8_t character_bits; /* 5..8 (CLS1, CLS2) */
    uint8_t parity_inhibit; /* PI */
    uint8_t even_parity;    /* EPE, used when PI is low */
    uint8_t stop_select;    /* SBS: 1.5 stop bits for 5 bits, else 2 */
} bm_im6402_control_t;

typedef struct bm_im6402_status {
    uint8_t tbre; /* transmitter buffer register empty */
    uint8_t tre;  /* transmitter register empty */
    uint8_t dr;   /* data received */
    uint8_t oe;   /* overrun error */
    uint8_t fe;   /* framing error */
    uint8_t pe;   /* parity error */
} bm_im6402_status_t;

bm_status_t bm_im6402_create(const bm_host_services_t *host,
                             const bm_im6402_config_t *config,
                             bm_im6402_t **out_uart);
void bm_im6402_destroy(bm_im6402_t *uart);

/* MR: clears PE, FE, OE, DR and TRE, sets TBRE and TRO high; TRE returns
 * high afterwards. The receiver buffer register is not cleared. A
 * character being transmitted is abandoned. */
void bm_im6402_master_reset(bm_im6402_t *uart);
/* CRL. 5..8 character bits; other values are clamped. */
void bm_im6402_load_control(bm_im6402_t *uart,
                            const bm_im6402_control_t *control);
bm_im6402_control_t bm_im6402_control(const bm_im6402_t *uart);

/* Register clocks (16 per bit) of one frame with the current control. */
uint32_t bm_im6402_frame_clocks(const bm_im6402_t *uart);

/* TBRL: loads TBR1-TBR8 and clears TBRE. With an empty transmitter
 * register the character moves on at once and TBRE is set again;
 * otherwise it follows the current one end to end. */
void bm_im6402_load_transmitter(bm_im6402_t *uart, uint8_t data);
/* Advances the transmitter by register clocks. */
void bm_im6402_advance(bm_im6402_t *uart, uint64_t clocks);
/* Register clocks until the character in the transmitter register has
 * been sent; UINT64_MAX when the transmitter is empty. */
uint64_t bm_im6402_clocks_until_transmitted(const bm_im6402_t *uart);

/* A whole character has arrived on RRI. It is transferred to the receiver
 * buffer register, right-justified with unused high bits low, and DR is
 * set; OE is set if DR was still high. framing_error and parity_error
 * report the line conditions of the frame (PE is forced low by PI). */
void bm_im6402_receive(bm_im6402_t *uart, uint8_t data, int framing_error,
                       int parity_error);
/* RBR1-RBR8. */
uint8_t bm_im6402_receiver_buffer(const bm_im6402_t *uart);
/* DRR: clears DR. */
void bm_im6402_reset_data_received(bm_im6402_t *uart);

bm_im6402_status_t bm_im6402_status(const bm_im6402_t *uart);

#ifdef __cplusplus
}
#endif

#endif
