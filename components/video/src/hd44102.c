/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Hitachi HD44102CH LCD column driver. Written from the Hitachi 1983 MOS
 * LSI Data Book, LCD Driver LSI; page references are to the bitsavers PDF. */
#include <blumach/components/hd44102.h>

#include <string.h>

struct bm_hd44102 {
    bm_host_services_t host;
    uint64_t busy_ticks;
    uint64_t busy_until;
    bm_hd44102_state_t state;
};

bm_status_t
bm_hd44102_create(const bm_host_services_t *host, uint64_t busy_ticks,
                  bm_hd44102_t **out_chip)
{
    bm_hd44102_t *chip;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (out_chip == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    chip = host->allocate(host->context, sizeof(*chip));
    if (chip == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(chip, 0, sizeof(*chip));
    chip->host = *host;
    chip->busy_ticks = busy_ticks;
    /* Display OFF at initial power up (Note 16, page 166) and UP mode with
     * reset (page 167). */
    chip->state.up = 1U;
    *out_chip = chip;
    return BM_STATUS_OK;
}

void
bm_hd44102_destroy(bm_hd44102_t *chip)
{
    if (chip != NULL)
        chip->host.release(chip->host.context, chip);
}

void
bm_hd44102_set_reset(bm_hd44102_t *chip, int asserted)
{
    chip->state.reset_asserted = (uint8_t) (asserted != 0);
    if (asserted) {
        chip->state.display_on = 0U;
        chip->state.up = 1U;
        chip->busy_until = 0U;
    }
}

static int
busy(const bm_hd44102_t *chip, uint64_t now)
{
    return now < chip->busy_until;
}

uint8_t
bm_hd44102_read_status(const bm_hd44102_t *chip, uint64_t now)
{
    uint8_t status = 0U;

    /* Busy also reads 1 while RST is held (page 175). */
    if (chip->state.reset_asserted)
        status |= BM_HD44102_STATUS_RESET | BM_HD44102_STATUS_BUSY;
    if (busy(chip, now))
        status |= BM_HD44102_STATUS_BUSY;
    if (chip->state.up)
        status |= BM_HD44102_STATUS_UP;
    if (!chip->state.display_on)
        status |= BM_HD44102_STATUS_OFF;
    return status;
}

static bm_hd44102_result_t
admit(const bm_hd44102_t *chip, uint64_t now)
{
    if (chip->state.reset_asserted)
        return BM_HD44102_REJECTED_RESET;
    if (busy(chip, now))
        return BM_HD44102_REJECTED_BUSY;
    return BM_HD44102_ACCEPTED;
}

static void
start_busy(bm_hd44102_t *chip, uint64_t now)
{
    chip->busy_until = now + chip->busy_ticks;
}

/* The Y register counts 0-49 and wraps both ways (page 170). */
static void
step_y(bm_hd44102_state_t *state)
{
    if (state->up)
        state->y = (uint8_t) (state->y == BM_HD44102_COLUMNS - 1U ? 0U : state->y + 1U);
    else
        state->y = (uint8_t) (state->y == 0U ? BM_HD44102_COLUMNS - 1U : state->y - 1U);
}

/* Instruction codes, pages 173-175: 39h/38h display ON/OFF, 3Bh/3Ah UP/DOWN,
 * xx111110 display start page xx, and xx yyyyyy set X page xx and Y column
 * yyyyyy for Y 0-49. Other Y codes (50-63 apart from the above) are not
 * defined by the data book and are rejected. */
bm_hd44102_result_t
bm_hd44102_write_instruction(bm_hd44102_t *chip, uint8_t value, uint64_t now)
{
    bm_hd44102_state_t *state = &chip->state;
    bm_hd44102_result_t result = admit(chip, now);
    uint8_t low = (uint8_t) (value & 0x3fU);

    if (result != BM_HD44102_ACCEPTED)
        return result;
    if (low == 0x3eU) {
        state->start_page = (uint8_t) (value >> 6U);
    } else if ((value == 0x39U) || (value == 0x38U)) {
        state->display_on = (uint8_t) (value & 1U);
    } else if ((value == 0x3bU) || (value == 0x3aU)) {
        state->up = (uint8_t) (value & 1U);
    } else if (low < BM_HD44102_COLUMNS) {
        state->x = (uint8_t) (value >> 6U);
        state->y = low;
    } else {
        return BM_HD44102_REJECTED_UNDEFINED;
    }
    start_busy(chip, now);
    return BM_HD44102_ACCEPTED;
}

bm_hd44102_result_t
bm_hd44102_write_data(bm_hd44102_t *chip, uint8_t value, uint64_t now)
{
    bm_hd44102_result_t result = admit(chip, now);

    if (result != BM_HD44102_ACCEPTED)
        return result;
    chip->state.ram[chip->state.x][chip->state.y] = value;
    step_y(&chip->state);
    start_busy(chip, now);
    return BM_HD44102_ACCEPTED;
}

bm_hd44102_result_t
bm_hd44102_read_data(bm_hd44102_t *chip, uint64_t now, uint8_t *value)
{
    bm_hd44102_result_t result = admit(chip, now);

    *value = 0U;
    if (result != BM_HD44102_ACCEPTED)
        return result;
    /* Output register pipeline, page 169. */
    *value = chip->state.output_register;
    chip->state.output_register = chip->state.ram[chip->state.x][chip->state.y];
    step_y(&chip->state);
    start_busy(chip, now);
    return BM_HD44102_ACCEPTED;
}

/* Bit 0 of a page byte is its top dot (page 173); the display start page
 * is shown at the top of the screen (page 174). */
void
bm_hd44102_render(const bm_hd44102_t *chip,
                  uint8_t dots[BM_HD44102_ROWS][BM_HD44102_COLUMNS])
{
    unsigned int row;
    unsigned int column;

    memset(dots, 0, BM_HD44102_ROWS * BM_HD44102_COLUMNS);
    if (!chip->state.display_on)
        return;
    for (row = 0U; row < BM_HD44102_ROWS; ++row) {
        unsigned int page = ((row >> 3U) + chip->state.start_page) & 3U;
        for (column = 0U; column < BM_HD44102_COLUMNS; ++column)
            dots[row][column] = (uint8_t) ((chip->state.ram[page][column] >> (row & 7U)) & 1U);
    }
}

void
bm_hd44102_get_state(const bm_hd44102_t *chip, bm_hd44102_state_t *out_state)
{
    *out_state = chip->state;
}
