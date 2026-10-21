/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NEC uPD1990AC contracts from NEC 1983 "Integrated Circuits for Consumer
 * Use", book pages 792-799. */
#include <blumach/components/rtc_upd1990ac.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <string.h>

typedef struct tp_log {
    unsigned int changes;
    unsigned int rising;
    int level;
} tp_log_t;

static void
tp_changed(void *context, int level)
{
    tp_log_t *log = context;
    ++log->changes;
    if (level && !log->level)
        ++log->rising;
    log->level = level;
}

static bm_upd1990ac_t *
make(const bm_upd1990ac_time_t *time, tp_log_t *log)
{
    bm_host_services_t host = bm_null_host_services();
    bm_upd1990ac_config_t config;
    bm_upd1990ac_t *chip = NULL;

    memset(&config, 0, sizeof(config));
    config.initial_time = *time;
    config.tp_changed = log != NULL ? tp_changed : NULL;
    config.tp_context = log;
    assert(bm_upd1990ac_create(&host, &config, &chip) == BM_STATUS_OK);
    if (log != NULL)
        log->level = bm_upd1990ac_tp(chip);
    return chip;
}

static void
command(bm_upd1990ac_t *chip, uint8_t code)
{
    bm_upd1990ac_set_command_pins(chip, code);
    bm_upd1990ac_set_stb(chip, 1);
    bm_upd1990ac_set_stb(chip, 0);
}

static int
shift_bit(bm_upd1990ac_t *chip, int in)
{
    int out = bm_upd1990ac_data_out(chip);
    bm_upd1990ac_set_data_in(chip, in);
    bm_upd1990ac_set_clk(chip, 1);
    bm_upd1990ac_set_clk(chip, 0);
    return out;
}

/* Time Read loads the register; Shift outputs it LSB first: units and tens
 * of seconds, minutes, hours, date, day of week, month (Fig. 1, p. 795). */
static void
test_time_read_order(void)
{
    const bm_upd1990ac_time_t t = { 49, 23, 2, 25, 6, 11 };
    bm_upd1990ac_t *chip = make(&t, NULL);
    unsigned int nibble[10] = { 0 };
    unsigned int bit;

    command(chip, BM_UPD1990AC_TIME_READ);
    command(chip, BM_UPD1990AC_REGISTER_SHIFT);
    for (bit = 0U; bit < 40U; ++bit)
        nibble[bit / 4U] |= (unsigned int) shift_bit(chip, 0) << (bit % 4U);
    assert(nibble[0] == 9U && nibble[1] == 4U);     /* 49 s */
    assert(nibble[2] == 3U && nibble[3] == 2U);     /* 23 min */
    assert(nibble[4] == 2U && nibble[5] == 0U);     /* 2 h */
    assert(nibble[6] == 5U && nibble[7] == 2U);     /* date 25 */
    assert(nibble[8] == 6U && nibble[9] == 0x0bU);  /* day 6, month 0Bh */
    bm_upd1990ac_destroy(chip);
}

/* Shift in a new time, Time Set presets the counter and holds it until
 * another group-0 command (p. 797); the data sheet example is July 16
 * (Sun.) 2:24:35 (p. 798, weekday code assumed 0). */
static void
test_time_set_and_hold(void)
{
    const bm_upd1990ac_time_t zero = { 0, 0, 0, 1, 0, 1 };
    const unsigned int digits[10] = { 5, 3, 4, 2, 2, 0, 6, 1, 0, 7 };
    bm_upd1990ac_t *chip = make(&zero, NULL);
    bm_upd1990ac_time_t now;
    unsigned int bit;

    command(chip, BM_UPD1990AC_REGISTER_SHIFT);
    for (bit = 0U; bit < 40U; ++bit)
        (void) shift_bit(chip, (int) ((digits[bit / 4U] >> (bit % 4U)) & 1U));
    command(chip, BM_UPD1990AC_TIME_SET);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.second == 35U && now.minute == 24U && now.hour == 2U &&
           now.day == 16U && now.weekday == 0U && now.month == 7U);
    bm_upd1990ac_advance(chip, 3U * BM_UPD1990AC_OSC_HZ);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.second == 35U);                      /* held */
    command(chip, BM_UPD1990AC_REGISTER_HOLD);
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.second == 36U);
    bm_upd1990ac_destroy(chip);
}

/* Month lengths are corrected, leap years are not; Feb. 29 can be set and
 * is followed by Mar. 1 (p. 795). No year is kept. */
static void
test_calendar_rollover(void)
{
    const bm_upd1990ac_time_t feb28 = { 59, 59, 23, 28, 3, 2 };
    const bm_upd1990ac_time_t feb29 = { 59, 59, 23, 29, 4, 2 };
    const bm_upd1990ac_time_t dec31 = { 59, 59, 23, 31, 6, 12 };
    bm_upd1990ac_time_t now;
    bm_upd1990ac_t *chip;

    chip = make(&feb28, NULL);
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.day == 1U && now.month == 3U && now.weekday == 4U && now.hour == 0U);
    bm_upd1990ac_destroy(chip);
    chip = make(&feb29, NULL);
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.day == 1U && now.month == 3U && now.weekday == 5U);
    bm_upd1990ac_destroy(chip);
    chip = make(&dec31, NULL);
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    bm_upd1990ac_get_time(chip, &now);
    assert(now.day == 1U && now.month == 1U && now.weekday == 0U);
    bm_upd1990ac_destroy(chip);
}

/* TP is a 50 % square wave of 64, 256 or 2048 Hz selected by group-1
 * commands (pp. 795, 797); DATA OUT is 1 Hz in Register Hold (p. 796). */
static void
test_tp_and_hold_output(void)
{
    const bm_upd1990ac_time_t zero = { 0, 0, 0, 1, 0, 1 };
    tp_log_t log = { 0, 0, 0 };
    bm_upd1990ac_t *chip = make(&zero, &log);
    unsigned int highs = 0U;
    unsigned int index;

    assert(bm_upd1990ac_tp_hz(chip) == 64U);
    command(chip, BM_UPD1990AC_TP_256HZ);
    assert(bm_upd1990ac_tp_hz(chip) == 256U);
    log.rising = 0U;
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    assert(log.rising == 256U);
    command(chip, BM_UPD1990AC_TP_2048HZ);
    log.rising = 0U;
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    assert(log.rising == 2048U);
    command(chip, BM_UPD1990AC_TP_64HZ);
    log.rising = 0U;
    bm_upd1990ac_advance(chip, BM_UPD1990AC_OSC_HZ);
    assert(log.rising == 64U);
    for (index = 0U; index < 32768U; ++index) {
        bm_upd1990ac_advance(chip, 1U);
        highs += (unsigned int) bm_upd1990ac_data_out(chip);
    }
    assert(highs == 16384U);
    bm_upd1990ac_set_output_enable(chip, 0);
    command(chip, BM_UPD1990AC_TIME_READ);
    assert(bm_upd1990ac_data_out(chip) == 1);       /* high impedance */
    bm_upd1990ac_destroy(chip);
}

/* CS low prohibits CLK and STB (p. 795). */
static void
test_chip_select(void)
{
    const bm_upd1990ac_time_t zero = { 0, 0, 0, 1, 0, 1 };
    bm_upd1990ac_t *chip = make(&zero, NULL);

    bm_upd1990ac_set_cs(chip, 0);
    command(chip, BM_UPD1990AC_TP_2048HZ);
    assert(bm_upd1990ac_tp_hz(chip) == 64U);
    assert(bm_upd1990ac_mode(chip) == BM_UPD1990AC_REGISTER_HOLD);
    bm_upd1990ac_set_cs(chip, 1);
    command(chip, BM_UPD1990AC_TEST_MODE);
    assert(bm_upd1990ac_mode(chip) == BM_UPD1990AC_TEST_MODE);
    command(chip, BM_UPD1990AC_REGISTER_HOLD);
    assert(bm_upd1990ac_mode(chip) == BM_UPD1990AC_REGISTER_HOLD);
    bm_upd1990ac_destroy(chip);
}

/* The TP deadline matches the advance needed to change TP. */
static void
test_tp_deadline(void)
{
    const bm_upd1990ac_time_t zero = { 0, 0, 0, 1, 0, 1 };
    tp_log_t log = { 0U, 0U, 0 };
    bm_upd1990ac_t *chip = make(&zero, &log);
    unsigned int step;

    command(chip, BM_UPD1990AC_TP_256HZ);
    for (step = 0U; step < 6U; ++step) {
        uint64_t wait = bm_upd1990ac_cycles_until_tp_change(chip);
        unsigned int before = log.changes;
        assert(wait >= 1U && wait <= 64U);
        bm_upd1990ac_advance(chip, wait - 1U);
        assert(log.changes == before);
        bm_upd1990ac_advance(chip, 1U);
        assert(log.changes == before + 1U);
    }
    bm_upd1990ac_advance(chip, 5U);
    assert(bm_upd1990ac_cycles_until_tp_change(chip) == 59U);
    bm_upd1990ac_destroy(chip);
}

/* Saved state restores counter, divider phase and latched commands; the
 * seconds advance carries the calendar but not a held counter. */
static void
test_state_round_trip(void)
{
    const bm_upd1990ac_time_t start = { 58, 59, 23, 28, 6, 2 };
    const bm_upd1990ac_time_t zero = { 0, 0, 0, 1, 0, 1 };
    uint8_t state[BM_UPD1990AC_STATE_SIZE];
    bm_upd1990ac_t *chip = make(&start, NULL);
    bm_upd1990ac_t *copy = make(&zero, NULL);
    bm_upd1990ac_time_t t;

    command(chip, BM_UPD1990AC_TP_256HZ);
    command(chip, BM_UPD1990AC_TIME_READ);
    bm_upd1990ac_advance(chip, 1234U);
    assert(bm_upd1990ac_save_state(chip, state, sizeof(state)) == BM_STATUS_OK);
    assert(bm_upd1990ac_load_state(copy, state, sizeof(state)) == BM_STATUS_OK);
    assert(bm_upd1990ac_tp_hz(copy) == 256U);
    assert(bm_upd1990ac_mode(copy) == BM_UPD1990AC_TIME_READ);
    assert(bm_upd1990ac_cycles_until_tp_change(copy) ==
           bm_upd1990ac_cycles_until_tp_change(chip));
    assert(bm_upd1990ac_tp(copy) == bm_upd1990ac_tp(chip));
    bm_upd1990ac_advance(chip, 32768U);
    bm_upd1990ac_advance(copy, 32768U);
    bm_upd1990ac_get_time(copy, &t);
    assert(t.second == 59U && t.minute == 59U && t.hour == 23U && t.day == 28U);
    bm_upd1990ac_advance_seconds(copy, 1U);
    bm_upd1990ac_get_time(copy, &t);
    /* Feb. 28 is followed by Mar. 1; weekday 6 wraps to 0. */
    assert(t.second == 0U && t.minute == 0U && t.hour == 0U && t.day == 1U &&
           t.month == 3U && t.weekday == 0U);
    bm_upd1990ac_set_time(copy, &start);
    bm_upd1990ac_get_time(copy, &t);
    assert(memcmp(&t, &start, sizeof(t)) == 0);
    state[0] = 0U;
    assert(bm_upd1990ac_load_state(copy, state, sizeof(state)) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_upd1990ac_load_state(copy, state, sizeof(state) - 1U) == BM_STATUS_INVALID_ARGUMENT);
    command(copy, BM_UPD1990AC_TIME_SET);
    bm_upd1990ac_get_time(copy, &t);
    bm_upd1990ac_advance_seconds(copy, 10U);
    {
        bm_upd1990ac_time_t held;
        bm_upd1990ac_get_time(copy, &held);
        assert(memcmp(&held, &t, sizeof(t)) == 0);
    }
    bm_upd1990ac_destroy(chip);
    bm_upd1990ac_destroy(copy);
}

int
main(void)
{
    test_time_read_order();
    test_time_set_and_hold();
    test_calendar_rollover();
    test_tp_and_hold_output();
    test_chip_select();
    test_tp_deadline();
    test_state_round_trip();
    return 0;
}
