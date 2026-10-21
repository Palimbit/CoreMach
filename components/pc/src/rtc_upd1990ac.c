/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NEC uPD1990AC serial I/O calendar and clock. Written from NEC 1983
 * "Integrated Circuits for Consumer Use", book pages 792-799. */
#include <blumach/components/rtc_upd1990ac.h>

#include <string.h>

#define REGISTER_BITS 40U
#define DIVIDER_MASK 0x7fffU

struct bm_upd1990ac {
    bm_host_services_t host;
    bm_upd1990ac_tp_fn tp_changed;
    void *tp_context;
    bm_upd1990ac_time_t time;
    uint64_t shift_register;
    uint16_t divider;           /* 15-stage binary divider, page 793 */
    uint8_t mode;               /* latched group-0 command */
    uint8_t test_mode;
    unsigned int tp_hz;
    uint8_t counter_held;       /* Time Set holds the counter */
    uint8_t command_pins;
    uint8_t data_in;
    uint8_t clk;
    uint8_t stb;
    uint8_t cs;
    uint8_t output_enable;
    uint8_t tp_level;
};

static unsigned int
bcd_value(unsigned int tens, unsigned int units)
{
    /* Invalid BCD digits are not specified by the data book; they are
     * taken arithmetically without validation. */
    return tens * 10U + units;
}

/* Fig. 1, page 795: from the LSB, units and tens of seconds, minutes,
 * hours and date, then day of week and month, four bits each. */
static uint64_t
pack(const bm_upd1990ac_time_t *t)
{
    const unsigned int digits[10] = {
        t->second % 10U, t->second / 10U, t->minute % 10U, t->minute / 10U,
        t->hour % 10U, t->hour / 10U, t->day % 10U, t->day / 10U,
        t->weekday & 0x0fU, t->month & 0x0fU
    };
    uint64_t value = 0U;
    unsigned int index;

    for (index = 0U; index < 10U; ++index)
        value |= (uint64_t) (digits[index] & 0x0fU) << (4U * index);
    return value;
}

static void
unpack(uint64_t value, bm_upd1990ac_time_t *t)
{
    unsigned int digit[10];
    unsigned int index;

    for (index = 0U; index < 10U; ++index)
        digit[index] = (unsigned int) ((value >> (4U * index)) & 0x0fU);
    t->second = (uint8_t) bcd_value(digit[1], digit[0]);
    t->minute = (uint8_t) bcd_value(digit[3], digit[2]);
    t->hour = (uint8_t) bcd_value(digit[5], digit[4]);
    t->day = (uint8_t) bcd_value(digit[7], digit[6]);
    t->weekday = (uint8_t) digit[8];
    t->month = (uint8_t) digit[9];
}

/* Month lengths are corrected automatically; leap years are not, but a
 * date set to Feb. 29 is followed by Mar. 1 (page 795). Months outside
 * 1-12 are not specified and use 31 days. */
static unsigned int
days_in_month(unsigned int month)
{
    static const uint8_t length[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (month >= 1U && month <= 12U) ? length[month - 1U] : 31U;
}

static void
tick_second(bm_upd1990ac_time_t *t)
{
    if (++t->second < 60U)
        return;
    t->second = 0U;
    if (++t->minute < 60U)
        return;
    t->minute = 0U;
    if (++t->hour < 24U)
        return;
    t->hour = 0U;
    t->weekday = (uint8_t) ((t->weekday + 1U) % 7U);
    if (++t->day <= days_in_month(t->month))
        return;
    t->day = 1U;
    t->month = (uint8_t) (t->month >= 12U ? 1U : t->month + 1U);
}

/* 64, 256 and 2048 Hz are divider stages 8, 6 and 3 of the 32.768 kHz
 * chain (page 793); duty 50 % (page 797). */
static uint8_t
tp_from_divider(const bm_upd1990ac_t *chip)
{
    unsigned int stage = chip->tp_hz == 2048U ? 3U : chip->tp_hz == 256U ? 6U : 8U;
    return (uint8_t) ((chip->divider >> stage) & 1U);
}

static void
update_tp(bm_upd1990ac_t *chip)
{
    uint8_t level = tp_from_divider(chip);

    if (level != chip->tp_level) {
        chip->tp_level = level;
        if (chip->tp_changed != NULL)
            chip->tp_changed(chip->tp_context, level);
    }
}

bm_status_t
bm_upd1990ac_create(const bm_host_services_t *host,
                    const bm_upd1990ac_config_t *config,
                    bm_upd1990ac_t **out_chip)
{
    bm_upd1990ac_t *chip;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (config == NULL) ||
        (out_chip == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    chip = host->allocate(host->context, sizeof(*chip));
    if (chip == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(chip, 0, sizeof(*chip));
    chip->host = *host;
    chip->tp_changed = config->tp_changed;
    chip->tp_context = config->tp_context;
    chip->time = config->initial_time;
    /* The power-on TP rate and mode are not specified; 64 Hz is the rate
     * the data book gives after leaving TEST MODE (page 797). */
    chip->tp_hz = 64U;
    chip->mode = BM_UPD1990AC_REGISTER_HOLD;
    chip->output_enable = 1U;
    chip->cs = 1U;
    chip->tp_level = tp_from_divider(chip);
    *out_chip = chip;
    return BM_STATUS_OK;
}

void
bm_upd1990ac_destroy(bm_upd1990ac_t *chip)
{
    if (chip != NULL)
        chip->host.release(chip->host.context, chip);
}

void
bm_upd1990ac_set_command_pins(bm_upd1990ac_t *chip, uint8_t c2_c1_c0)
{
    chip->command_pins = (uint8_t) (c2_c1_c0 & 7U);
}

void
bm_upd1990ac_set_data_in(bm_upd1990ac_t *chip, int level)
{
    chip->data_in = (uint8_t) (level != 0);
}

/* Register Shift: DATA IN enters at the month end and the LSB of seconds
 * leaves at DATA OUT, one bit per CLK (page 798). */
void
bm_upd1990ac_set_clk(bm_upd1990ac_t *chip, int level)
{
    uint8_t rising = (uint8_t) (level && !chip->clk);

    chip->clk = (uint8_t) (level != 0);
    if (rising && chip->cs && chip->mode == BM_UPD1990AC_REGISTER_SHIFT)
        chip->shift_register = (chip->shift_register >> 1U) |
                               ((uint64_t) chip->data_in << (REGISTER_BITS - 1U));
}

/* Commands are latched by STB; group 0 and group 1 are independent
 * (pages 796-797). */
void
bm_upd1990ac_set_stb(bm_upd1990ac_t *chip, int level)
{
    uint8_t rising = (uint8_t) (level && !chip->stb);
    uint8_t command = chip->command_pins;

    chip->stb = (uint8_t) (level != 0);
    if (!rising || !chip->cs)
        return;
    if (command < 4U) {
        chip->mode = command;
        chip->counter_held = 0U;
        if (command == BM_UPD1990AC_REGISTER_HOLD)
            chip->test_mode = 0U;
        if (command == BM_UPD1990AC_TIME_SET) {
            /* Preset the counter, reset divider stages 11-15 and hold. */
            unpack(chip->shift_register, &chip->time);
            chip->divider = (uint16_t) (chip->divider & 0x03ffU);
            chip->counter_held = 1U;
        } else if (command == BM_UPD1990AC_TIME_READ) {
            chip->shift_register = pack(&chip->time);
        }
    } else if (command == BM_UPD1990AC_TEST_MODE) {
        chip->test_mode = 1U;
    } else {
        chip->test_mode = 0U;
        chip->tp_hz = command == BM_UPD1990AC_TP_64HZ ? 64U :
                      command == BM_UPD1990AC_TP_256HZ ? 256U : 2048U;
    }
    update_tp(chip);
}

void
bm_upd1990ac_set_cs(bm_upd1990ac_t *chip, int level)
{
    chip->cs = (uint8_t) (level != 0);
}

void
bm_upd1990ac_set_output_enable(bm_upd1990ac_t *chip, int level)
{
    chip->output_enable = (uint8_t) (level != 0);
}

/* DATA OUT per command (pages 796-797): 1 Hz in Register Hold, otherwise
 * the register LSB, which follows the counter in Time Read. A low OUT ENBL
 * makes it high impedance (page 795). */
int
bm_upd1990ac_data_out(const bm_upd1990ac_t *chip)
{
    if (!chip->output_enable)
        return 1;
    if (chip->mode == BM_UPD1990AC_REGISTER_HOLD)
        return (chip->divider >> 14U) & 1U;
    return (int) (chip->shift_register & 1U);
}

int
bm_upd1990ac_tp(const bm_upd1990ac_t *chip)
{
    return chip->tp_level;
}

void
bm_upd1990ac_advance(bm_upd1990ac_t *chip, uint64_t oscillator_cycles)
{
    while (oscillator_cycles-- != 0U) {
        if (chip->counter_held) {
            chip->divider = (uint16_t) ((chip->divider + 1U) & 0x03ffU);
        } else {
            chip->divider = (uint16_t) ((chip->divider + 1U) & DIVIDER_MASK);
            if (chip->divider == 0U) {
                tick_second(&chip->time);
                if (chip->mode == BM_UPD1990AC_TIME_READ)
                    chip->shift_register = pack(&chip->time);
            }
        }
        update_tp(chip);
    }
}

void
bm_upd1990ac_get_time(const bm_upd1990ac_t *chip, bm_upd1990ac_time_t *out_time)
{
    *out_time = chip->time;
}

bm_upd1990ac_command_t
bm_upd1990ac_mode(const bm_upd1990ac_t *chip)
{
    return chip->test_mode ? BM_UPD1990AC_TEST_MODE : (bm_upd1990ac_command_t) chip->mode;
}

unsigned int
bm_upd1990ac_tp_hz(const bm_upd1990ac_t *chip)
{
    return chip->tp_hz;
}
