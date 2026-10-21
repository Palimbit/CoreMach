/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OKI MSM81C55 I/O ports and timer. Written from the OKI 1986
 * Microprocessor Data Book, MSM81C55RS/GS, book pages 102-114. */
#include <blumach/components/msm81c55.h>

#include <string.h>

#define COUNT_MASK 0x3fffU

struct bm_msm81c55 {
    bm_host_services_t host;
    bm_msm81c55_input_fn read_pins;
    bm_msm81c55_port_fn port_changed;
    void *context;
    uint8_t command;
    uint8_t latch[3];
    /* Count length register as written: bits 0-13 length, 14-15 mode. */
    uint16_t length_register;
    /* Running timer: the current period started at period_start with
     * length and mode; a START while running is pending until TC. */
    int running;
    uint16_t length;
    uint8_t mode;
    uint64_t period_start;
    int pending_start;
    uint16_t pending_length;
    uint8_t pending_mode;
    int stop_after_tc;
    int tc_flag;
};

static uint8_t
direction_mask(const bm_msm81c55_t *chip, unsigned int port)
{
    if (port == BM_MSM81C55_PORT_A)
        return (chip->command & 0x01U) != 0U ? 0xffU : 0x00U;
    if (port == BM_MSM81C55_PORT_B)
        return (chip->command & 0x02U) != 0U ? 0xffU : 0x00U;
    /* Port control table, page 112: ALT2 (11) makes PC0-PC5 outputs and
     * ALT3 (01) PC3-PC5; ALT3/ALT4 control lines are not modelled. */
    switch ((chip->command >> 2U) & 3U) {
    case 3U: return 0x3fU;
    case 1U: return 0x38U;
    default: return 0x00U;
    }
}

static void
notify(bm_msm81c55_t *chip, unsigned int port)
{
    if (chip->port_changed != NULL)
        chip->port_changed(chip->context, port);
}

static int
continuous(uint8_t mode)
{
    return (mode & 1U) != 0U;
}

/* Bring the timer to now: every terminal count passed sets the timer flag,
 * applies a pending START or STOP AFTER TC, and ends the single modes. */
static void
settle(bm_msm81c55_t *chip, uint64_t now)
{
    while (chip->running && (now >= chip->period_start) &&
           (now - chip->period_start >= chip->length)) {
        const uint64_t tc = chip->period_start + chip->length;
        chip->tc_flag = 1;
        if (chip->stop_after_tc) {
            chip->running = 0;
            chip->stop_after_tc = 0;
            chip->pending_start = 0;
        } else if (chip->pending_start) {
            chip->length = chip->pending_length;
            chip->mode = chip->pending_mode;
            chip->pending_start = 0;
            chip->period_start = tc;
        } else if (continuous(chip->mode)) {
            const uint64_t periods = (now - chip->period_start) / chip->length;
            chip->period_start += periods * chip->length;
        } else {
            chip->running = 0;
        }
    }
}

static uint16_t
loaded_length(const bm_msm81c55_t *chip)
{
    const uint16_t length = (uint16_t) (chip->length_register & COUNT_MASK);
    /* The data book allows 2-3FFFh; smaller values are not specified and
     * are treated as 2. */
    return length < 2U ? 2U : length;
}

static void
timer_command(bm_msm81c55_t *chip, unsigned int command, uint64_t now)
{
    settle(chip, now);
    switch (command) {
    case 1U:    /* STOP */
        chip->running = 0;
        chip->pending_start = 0;
        chip->stop_after_tc = 0;
        break;
    case 2U:    /* STOP AFTER TC */
        if (chip->running)
            chip->stop_after_tc = 1;
        break;
    case 3U:    /* START */
        if (chip->running) {
            chip->pending_start = 1;
            chip->pending_length = loaded_length(chip);
            chip->pending_mode = (uint8_t) (chip->length_register >> 14U);
            chip->stop_after_tc = 0;
        } else {
            chip->running = 1;
            chip->length = loaded_length(chip);
            chip->mode = (uint8_t) (chip->length_register >> 14U);
            chip->period_start = now;
        }
        break;
    default:
        break;
    }
}

bm_status_t
bm_msm81c55_create(const bm_host_services_t *host,
                   const bm_msm81c55_config_t *config,
                   bm_msm81c55_t **out_chip)
{
    bm_msm81c55_t *chip;

    if ((bm_host_services_validate(host) != BM_STATUS_OK) || (config == NULL) ||
        (out_chip == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    chip = host->allocate(host->context, sizeof(*chip));
    if (chip == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(chip, 0, sizeof(*chip));
    chip->host = *host;
    chip->read_pins = config->read_pins;
    chip->port_changed = config->port_changed;
    chip->context = config->context;
    chip->length = 2U;
    *out_chip = chip;
    return BM_STATUS_OK;
}

void
bm_msm81c55_destroy(bm_msm81c55_t *chip)
{
    if (chip != NULL)
        chip->host.release(chip->host.context, chip);
}

void
bm_msm81c55_reset(bm_msm81c55_t *chip, uint64_t now)
{
    unsigned int port;

    (void) now;
    chip->command = 0U;
    memset(chip->latch, 0, sizeof(chip->latch));
    chip->running = 0;
    chip->pending_start = 0;
    chip->stop_after_tc = 0;
    chip->tc_flag = 0;
    for (port = 0U; port < 3U; ++port)
        notify(chip, port);
}

uint8_t
bm_msm81c55_read(bm_msm81c55_t *chip, uint8_t address, uint64_t now)
{
    switch (address & 7U) {
    case BM_MSM81C55_COMMAND_STATUS: {
        /* Status word, page 113: the interrupt enables follow the command
         * register; the timer flag is cleared by this read. */
        uint8_t status = (uint8_t) (((chip->command >> 4U) & 1U) << 2U |
                                    ((chip->command >> 5U) & 1U) << 5U);
        settle(chip, now);
        if (chip->tc_flag)
            status |= BM_MSM81C55_STATUS_TIMER;
        chip->tc_flag = 0;
        return status;
    }
    case BM_MSM81C55_REGISTER_A:
    case BM_MSM81C55_REGISTER_B:
    case BM_MSM81C55_REGISTER_C: {
        const unsigned int port = (unsigned int) (address & 7U) - 1U;
        const uint8_t outputs = direction_mask(chip, port);
        const uint8_t pins = chip->read_pins != NULL ?
            chip->read_pins(chip->context, port) : 0xffU;
        return (uint8_t) ((chip->latch[port] & outputs) | (pins & (uint8_t) ~outputs));
    }
    case BM_MSM81C55_TIMER_LOW:
    case BM_MSM81C55_TIMER_HIGH: {
        uint16_t remaining = (uint16_t) (chip->length_register & COUNT_MASK);
        uint8_t mode = (uint8_t) (chip->length_register >> 14U);
        uint16_t value;
        settle(chip, now);
        if (chip->running) {
            remaining = (uint16_t) (chip->length - (now - chip->period_start));
            mode = chip->mode;
        }
        value = (uint16_t) ((remaining & COUNT_MASK) | (uint16_t) (mode << 14U));
        return (address & 7U) == BM_MSM81C55_TIMER_LOW ? (uint8_t) value :
                                                          (uint8_t) (value >> 8U);
    }
    default:
        /* Addresses 6 and 7 select no register. */
        return 0xffU;
    }
}

void
bm_msm81c55_write(bm_msm81c55_t *chip, uint8_t address, uint8_t value,
                  uint64_t now)
{
    switch (address & 7U) {
    case BM_MSM81C55_COMMAND_STATUS: {
        const uint8_t before[3] = {
            direction_mask(chip, 0U), direction_mask(chip, 1U),
            direction_mask(chip, 2U)
        };
        unsigned int port;
        chip->command = value;
        for (port = 0U; port < 3U; ++port)
            if (direction_mask(chip, port) != before[port])
                notify(chip, port);
        timer_command(chip, (unsigned int) (value >> 6U), now);
        break;
    }
    case BM_MSM81C55_REGISTER_A:
    case BM_MSM81C55_REGISTER_B:
    case BM_MSM81C55_REGISTER_C: {
        const unsigned int port = (unsigned int) (address & 7U) - 1U;
        chip->latch[port] = port == BM_MSM81C55_PORT_C ?
            (uint8_t) (value & 0x3fU) : value;
        notify(chip, port);
        break;
    }
    case BM_MSM81C55_TIMER_LOW:
        settle(chip, now);
        chip->length_register = (uint16_t) ((chip->length_register & 0xff00U) | value);
        break;
    case BM_MSM81C55_TIMER_HIGH:
        settle(chip, now);
        chip->length_register = (uint16_t) ((chip->length_register & 0x00ffU) |
                                            ((uint16_t) value << 8U));
        break;
    default:
        break;
    }
}

uint8_t
bm_msm81c55_port_levels(const bm_msm81c55_t *chip, unsigned int port,
                        uint8_t input_levels)
{
    const uint8_t outputs = direction_mask(chip, port);
    return (uint8_t) ((chip->latch[port] & outputs) |
                      (input_levels & (uint8_t) ~outputs));
}

uint8_t
bm_msm81c55_command(const bm_msm81c55_t *chip)
{
    return chip->command;
}

/* Waveforms, page 108 and the mode table on page 113: square wave is high
 * for the first half of each period (the longer half for odd lengths, note
 * 1) and low for the rest; pulse mode is low for the last cycle. */
int
bm_msm81c55_timer_out(bm_msm81c55_t *chip, uint64_t now)
{
    uint64_t phase;

    settle(chip, now);
    if (!chip->running || (now < chip->period_start))
        return 1;
    phase = now - chip->period_start;
    if ((chip->mode & 2U) != 0U)
        return phase + 1U < chip->length;
    return phase < (uint64_t) ((chip->length + 1U) / 2U);
}

uint16_t
bm_msm81c55_timer_period(bm_msm81c55_t *chip, uint64_t now,
                         bm_msm81c55_timer_mode_t *out_mode)
{
    settle(chip, now);
    if (out_mode != NULL)
        *out_mode = (bm_msm81c55_timer_mode_t) chip->mode;
    return (chip->running && continuous(chip->mode)) ? chip->length : 0U;
}
