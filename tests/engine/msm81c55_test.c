/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OKI MSM81C55 contracts from the OKI 1986 Microprocessor Data Book,
 * MSM81C55RS/GS, book pages 102-114. */
#include <blumach/components/msm81c55.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

typedef struct board {
    uint8_t pins[3];
    unsigned int changes[3];
} board_t;

static uint8_t
read_pins(void *context, unsigned int port)
{
    return ((board_t *) context)->pins[port];
}

static void
port_changed(void *context, unsigned int port)
{
    ++((board_t *) context)->changes[port];
}

static bm_msm81c55_t *
make(board_t *board)
{
    bm_host_services_t host = bm_null_host_services();
    bm_msm81c55_config_t config = { read_pins, port_changed, board };
    bm_msm81c55_t *chip = NULL;
    assert(bm_msm81c55_create(&host, &config, &chip) == BM_STATUS_OK);
    return chip;
}

static void
load_timer(bm_msm81c55_t *chip, uint16_t length, unsigned int mode, uint64_t now)
{
    bm_msm81c55_write(chip, BM_MSM81C55_TIMER_LOW, (uint8_t) length, now);
    bm_msm81c55_write(chip, BM_MSM81C55_TIMER_HIGH,
                      (uint8_t) ((length >> 8U) | (mode << 6U)), now);
}

/* Command bits 0-3 set the port directions (page 112); input bits read the
 * pins and output bits the latch. */
static void
test_ports(void)
{
    board_t board = { { 0x5aU, 0xa5U, 0xeaU }, { 0U, 0U, 0U } };
    bm_msm81c55_t *chip = make(&board);

    bm_msm81c55_write(chip, BM_MSM81C55_REGISTER_A, 0x0fU, 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_A, 0U) == 0x5aU);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0x01U, 0U);
    assert(board.changes[0] == 2U && board.changes[1] == 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_A, 0U) == 0x0fU);
    assert(bm_msm81c55_port_levels(chip, BM_MSM81C55_PORT_A, 0xffU) == 0x0fU);
    assert(bm_msm81c55_port_levels(chip, BM_MSM81C55_PORT_B, 0x33U) == 0x33U);
    /* ALT1: port C is an input, bits 6-7 come from the owner. */
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_C, 0U) == 0xeaU);
    bm_msm81c55_write(chip, BM_MSM81C55_REGISTER_C, 0xffU, 0U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0x0fU, 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_C, 0U) == 0xffU);
    assert(bm_msm81c55_port_levels(chip, BM_MSM81C55_PORT_C, 0x00U) == 0x3fU);
    bm_msm81c55_reset(chip, 0U);
    assert(bm_msm81c55_command(chip) == 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_B, 0U) == 0xa5U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0x02U, 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_REGISTER_B, 0U) == 0x00U);
    bm_msm81c55_destroy(chip);
}

/* Continuous square wave: high for the longer half, low for the rest, and
 * the timer flag set at TC and cleared by a status read (page 113). */
static void
test_square_wave(void)
{
    board_t board;
    bm_msm81c55_t *chip;
    bm_msm81c55_timer_mode_t mode;
    uint64_t t;
    unsigned int high = 0U;

    memset(&board, 0, sizeof(board));
    chip = make(&board);
    assert(bm_msm81c55_timer_out(chip, 0U) == 1);
    load_timer(chip, 9U, BM_MSM81C55_CONTINUOUS_SQUARE, 0U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0xc0U, 100U);
    for (t = 100U; t < 109U; ++t)
        high += (unsigned int) bm_msm81c55_timer_out(chip, t);
    assert(high == 5U);
    assert(bm_msm81c55_timer_out(chip, 104U) == 1 &&
           bm_msm81c55_timer_out(chip, 105U) == 0);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_COMMAND_STATUS, 108U) == 0U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_COMMAND_STATUS, 109U) ==
           BM_MSM81C55_STATUS_TIMER);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_COMMAND_STATUS, 110U) == 0U);
    assert(bm_msm81c55_timer_period(chip, 1000009U, &mode) == 9U &&
           mode == BM_MSM81C55_CONTINUOUS_SQUARE);
    assert(bm_msm81c55_timer_out(chip, 1000009U) == 1);
    /* Remaining count read back with the mode bits. */
    assert(bm_msm81c55_read(chip, BM_MSM81C55_TIMER_LOW, 1000012U) == 6U);
    assert(bm_msm81c55_read(chip, BM_MSM81C55_TIMER_HIGH, 1000012U) == 0x40U);
    /* STOP: output back high, no period. */
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0x40U, 1000014U);
    assert(bm_msm81c55_timer_period(chip, 1000014U, NULL) == 0U);
    assert(bm_msm81c55_timer_out(chip, 1000015U) == 1);
    bm_msm81c55_destroy(chip);
}

/* Single pulse: low for the last cycle, then the timer stops. START while
 * running takes effect at TC; STOP AFTER TC ends at TC. */
static void
test_single_and_pending(void)
{
    board_t board;
    bm_msm81c55_t *chip;

    memset(&board, 0, sizeof(board));
    chip = make(&board);
    load_timer(chip, 4U, BM_MSM81C55_SINGLE_PULSE, 0U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0xc0U, 0U);
    assert(bm_msm81c55_timer_out(chip, 2U) == 1);
    assert(bm_msm81c55_timer_out(chip, 3U) == 0);
    assert(bm_msm81c55_timer_out(chip, 4U) == 1);
    assert(bm_msm81c55_timer_out(chip, 7U) == 1);
    assert((bm_msm81c55_read(chip, 0U, 8U) & BM_MSM81C55_STATUS_TIMER) != 0U);

    load_timer(chip, 10U, BM_MSM81C55_CONTINUOUS_SQUARE, 20U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0xc0U, 20U);
    load_timer(chip, 4U, BM_MSM81C55_CONTINUOUS_SQUARE, 22U);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0xc0U, 22U);
    assert(bm_msm81c55_timer_period(chip, 29U, NULL) == 10U);
    assert(bm_msm81c55_timer_period(chip, 30U, NULL) == 4U);
    assert(bm_msm81c55_timer_out(chip, 31U) == 1 &&
           bm_msm81c55_timer_out(chip, 32U) == 0 &&
           bm_msm81c55_timer_out(chip, 34U) == 1);
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0x80U, 35U);
    assert(bm_msm81c55_timer_period(chip, 37U, NULL) == 4U);
    assert(bm_msm81c55_timer_period(chip, 38U, NULL) == 0U);
    /* RESET stops the timer and clears the flag. */
    bm_msm81c55_write(chip, BM_MSM81C55_COMMAND_STATUS, 0xc0U, 40U);
    bm_msm81c55_reset(chip, 100U);
    assert(bm_msm81c55_timer_period(chip, 100U, NULL) == 0U);
    assert(bm_msm81c55_read(chip, 0U, 100U) == 0U);
    bm_msm81c55_destroy(chip);
}

int
main(void)
{
    test_ports();
    test_square_wave();
    test_single_and_pending();
    return 0;
}
