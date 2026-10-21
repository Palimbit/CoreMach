/* SPDX-License-Identifier: GPL-2.0-or-later */
/* HD44102CH contracts from the Hitachi 1983 MOS LSI Data Book, LCD Driver
 * LSI (bitsavers PDF pages 166-175). */
#include <blumach/components/hd44102.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

#define BUSY_TICKS 20U

static bm_hd44102_t *
make(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_hd44102_t *chip = NULL;
    assert(bm_hd44102_create(&host, BUSY_TICKS, &chip) == BM_STATUS_OK);
    return chip;
}

/* Display OFF and UP mode at power up and with RST; RST reads BUSY and
 * RESET and rejects instructions (pages 166-167, 175). */
static void
test_reset_and_status(void)
{
    bm_hd44102_t *chip = make();

    assert(bm_hd44102_read_status(chip, 0U) ==
           (BM_HD44102_STATUS_UP | BM_HD44102_STATUS_OFF));
    assert(bm_hd44102_write_instruction(chip, 0x39U, 0U) == BM_HD44102_ACCEPTED);
    assert(bm_hd44102_write_instruction(chip, 0x3aU, 100U) == BM_HD44102_ACCEPTED);
    assert(bm_hd44102_read_status(chip, 200U) == 0x00U);
    bm_hd44102_set_reset(chip, 1);
    assert(bm_hd44102_read_status(chip, 300U) ==
           (BM_HD44102_STATUS_BUSY | BM_HD44102_STATUS_UP |
            BM_HD44102_STATUS_OFF | BM_HD44102_STATUS_RESET));
    assert(bm_hd44102_write_instruction(chip, 0x39U, 300U) ==
           BM_HD44102_REJECTED_RESET);
    bm_hd44102_set_reset(chip, 0);
    assert(bm_hd44102_read_status(chip, 300U) ==
           (BM_HD44102_STATUS_UP | BM_HD44102_STATUS_OFF));
    bm_hd44102_destroy(chip);
}

/* BUSY lasts the configured period after an accepted access and rejects
 * instructions and data meanwhile; Status Read is always available
 * (pages 170-171, 175). */
static void
test_busy_period(void)
{
    bm_hd44102_t *chip = make();
    uint8_t value = 0xaaU;

    assert(bm_hd44102_write_instruction(chip, 0x39U, 1000U) == BM_HD44102_ACCEPTED);
    assert(bm_hd44102_read_status(chip, 1000U) & BM_HD44102_STATUS_BUSY);
    assert(bm_hd44102_read_status(chip, 1000U + BUSY_TICKS - 1U) & BM_HD44102_STATUS_BUSY);
    assert(bm_hd44102_write_data(chip, 0x55U, 1010U) == BM_HD44102_REJECTED_BUSY);
    assert(bm_hd44102_read_data(chip, 1010U, &value) == BM_HD44102_REJECTED_BUSY);
    assert(value == 0U);
    assert((bm_hd44102_read_status(chip, 1000U + BUSY_TICKS) & BM_HD44102_STATUS_BUSY) == 0U);
    assert(bm_hd44102_write_data(chip, 0x55U, 1000U + BUSY_TICKS) == BM_HD44102_ACCEPTED);
    bm_hd44102_destroy(chip);
}

/* Set X/Y, data writes step Y with wrap in UP and DOWN modes (pages 170,
 * 173, 175); undefined Y codes are rejected. */
static void
test_addressing_and_counter(void)
{
    bm_hd44102_t *chip = make();
    bm_hd44102_state_t state;
    uint64_t t = 0U;

    assert(bm_hd44102_write_instruction(chip, 0x80U | 48U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_data(chip, 0x11U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_data(chip, 0x22U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_data(chip, 0x33U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    bm_hd44102_get_state(chip, &state);
    assert(state.x == 2U && state.y == 1U);
    assert(state.ram[2][48] == 0x11U && state.ram[2][49] == 0x22U &&
           state.ram[2][0] == 0x33U);
    assert(bm_hd44102_write_instruction(chip, 0x3aU, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_instruction(chip, 0xc0U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_data(chip, 0x44U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    bm_hd44102_get_state(chip, &state);
    assert(state.x == 3U && state.y == 49U && state.ram[3][0] == 0x44U && !state.up);
    assert(bm_hd44102_write_instruction(chip, 0x32U, t) == BM_HD44102_REJECTED_UNDEFINED);
    assert(bm_hd44102_write_instruction(chip, 0x3fU, t) == BM_HD44102_REJECTED_UNDEFINED);
    bm_hd44102_get_state(chip, &state);
    assert(state.x == 3U && state.y == 49U);
    bm_hd44102_destroy(chip);
}

/* Reads go through the output register: after setting an address the
 * first read is a dummy and the second returns the data at that address
 * (page 169, figure). */
static void
test_dummy_read_pipeline(void)
{
    bm_hd44102_t *chip = make();
    uint8_t value = 0U;
    uint64_t t = 0U;
    unsigned int column;

    assert(bm_hd44102_write_instruction(chip, 0x40U | 10U, t) == BM_HD44102_ACCEPTED);
    for (column = 0U; column < 3U; ++column) {
        t += BUSY_TICKS;
        assert(bm_hd44102_write_data(chip, (uint8_t) (0xa0U + column), t) ==
               BM_HD44102_ACCEPTED);
    }
    t += BUSY_TICKS;
    assert(bm_hd44102_write_instruction(chip, 0x40U | 10U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_read_data(chip, t, &value) == BM_HD44102_ACCEPTED);
    assert(value == 0x00U);                 /* dummy */
    t += BUSY_TICKS;
    assert(bm_hd44102_read_data(chip, t, &value) == BM_HD44102_ACCEPTED);
    assert(value == 0xa0U);                 /* address N */
    t += BUSY_TICKS;
    assert(bm_hd44102_read_data(chip, t, &value) == BM_HD44102_ACCEPTED);
    assert(value == 0xa1U);                 /* address N+1 */
    bm_hd44102_destroy(chip);
}

/* Display ON/OFF and start page select what reaches the panel (pages 170,
 * 173-174); bit 0 is the top dot. */
static void
test_render(void)
{
    static uint8_t dots[BM_HD44102_ROWS][BM_HD44102_COLUMNS];
    bm_hd44102_t *chip = make();
    uint64_t t = 0U;

    assert(bm_hd44102_write_instruction(chip, 0x00U | 7U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    assert(bm_hd44102_write_data(chip, 0x81U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    bm_hd44102_render(chip, dots);
    assert(dots[0][7] == 0U);               /* display OFF */
    assert(bm_hd44102_write_instruction(chip, 0x39U, t) == BM_HD44102_ACCEPTED);
    t += BUSY_TICKS;
    bm_hd44102_render(chip, dots);
    assert(dots[0][7] == 1U && dots[7][7] == 1U && dots[1][7] == 0U);
    assert(bm_hd44102_write_instruction(chip, 0xfeU, t) == BM_HD44102_ACCEPTED);
    bm_hd44102_render(chip, dots);
    /* Start page 3: page 0 appears in rows 8-15. */
    assert(dots[0][7] == 0U && dots[8][7] == 1U && dots[15][7] == 1U);
    bm_hd44102_destroy(chip);
}

int
main(void)
{
    test_reset_and_status();
    test_busy_period();
    test_addressing_and_counter();
    test_dummy_read_pipeline();
    test_render();
    return 0;
}
