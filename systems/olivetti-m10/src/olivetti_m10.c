/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 *
 * Olivetti M10 board wiring, written from the M10 service manual
 * (Olivetti 4100230 N, 1983) and the M10 operations guide, and moved from
 * the CoreMach M10 firmware probe. MAME's kc85 driver is a secondary source
 * for the HD44102 chip placement only; no MAME code is used.
 */
#include <blumach/systems/olivetti_m10.h>

#include <blumach/components/bus.h>
#include <blumach/components/cpu_i8085.h>
#include <blumach/components/hd44102.h>
#include <blumach/components/im6402.h>
#include <blumach/components/msm81c55.h>

#include "m10_keyboard.h"

#include <string.h>

#define M10_CPU_HZ 2457600U
#define M10_ROM_END 0x8000U
#define M10_RAM_BASE 0x8000U
#define M10_LCD_CHIPS 10U
#define M10_LCD_WIDTH 240U
#define M10_LCD_HEIGHT 64U
/* T_BUSY <= 2/F_phi with F_phi half of the HD44103 oscillation (Hitachi
 * 1983 LCD Driver LSI, PDF page 171), about 430 kHz in the M10 (service
 * manual 6.18): at most about 9.3 us, 23 CPU clocks. Approximate. */
#define M10_LCD_BUSY_CYCLES 23U
/* IN and OUT start their I/O machine cycle seven clocks into the
 * instruction (Intel MCS-80/85 timing); devices see that time. */
#define M10_IO_CYCLE_OFFSET 7U
/* Unpopulated memory, unmapped I/O and floating inputs read FFh. The
 * floating value is not documented (assumption kept from the probe). */
#define M10_OPEN_BUS 0xffU

/* Schmitt trigger thresholds of the playback path (6.9.2), in 16-bit sample
 * units: SID goes high above +threshold and low below -threshold
 * (assumption: the manual gives neither the levels nor the polarity). */
#define M10_TAPE_THRESHOLD 1024
/* Characters received from the host and not yet on the line. Kept short so
 * that the firmware's XOFF stops a host within a few characters. */
#define M10_SERIAL_QUEUE 16U
/* D0h status bits without a modelled source ([MAME kc85], secondary):
 * bit 5 RP and bit 6 (+5 V) read high, bit 7 low power sensor reads high
 * (battery good) and bit 0 carrier detect reads low (assumption: DCD is
 * not connected on the European machine). */
#define M10_UART_STATUS_FIXED 0xe0U

#define RTC_STATE_VERSION 1U
#define RTC_WALL_CLOCK_UNKNOWN INT64_MIN

typedef struct bm_m10_machine {
    bm_host_services_t host;
    bm_engine_t *engine;
    bm_bus_t *bus;
    bm_cpu_id_t cpu_id;
    int cpu_ready;
    bm_timed_source_id_t cpu_clock_id;
    bm_timed_source_id_t rtc_clock_id;
    const uint8_t *rom;
    uint32_t ram_kib;
    uint32_t ram_base;
    bm_m10_language_t language;
    bm_m10_wall_clock_fn wall_clock;
    void *wall_clock_context;
    uint8_t ram[BM_M10_RAM_STATE_SIZE];
    /* 81C55 at B0h-BFh (Y3), AD2-AD0 select the register. TIMER IN (pin 3)
     * is the 80C85 CLK output (pin 37) on the board schematic, so the timer
     * counts CPU clocks. */
    bm_msm81c55_t *pio;
    uint8_t port_a;
    uint8_t port_b;
    /* E0h-EFh writes latch AD0-AD3 in M14 (40H175): bit 3 is the cassette
     * REMOTE (6.9.3), bit 2 the uPD1990AC STB (6.15) and bit 1 the printer
     * STROBE (6.11). */
    uint8_t control_latch;
    /* uPD1990AC (6.15, pp. 6-18..6-20): C0-C2 on PA0-PA2, CLK on PA3,
     * DATA IN on PA4, DATA OUT on PC0 with a pull-up, CS and OUT ENBL on
     * the system RESET, TP on RST 7.5. */
    bm_upd1990ac_t *rtc;
    uint64_t rtc_accounted;
    int rtc_tp_level;
    bm_hd44102_t *lcd[M10_LCD_CHIPS];
    m10_keyboard_t keyboard;
    /* Power: P.C.S. on PB4 switches the converter off (6.20.2); the
     * switch must be cycled, which a machine reset stands for. */
    int powered_off;
    /* Buzzer (6.12, Fig. 6-13): the transistor follows
     * PB5 AND (PB2 OR TIMER OUT). */
    uint64_t buzzer_edges;
    /* Tolerant hosts only: every fallback below already applies to all
     * hosts (open-bus reads, ignored writes and LCD instructions); a
     * tolerant host also hears of each case once. */
    const bm_host_tolerance_services_t *tolerance;
    /* IM6402 UART (6.16.1): the receiver buffer is read at C0h-CFh (Y4)
     * and the transmitter loaded there, status is read at D0h-DFh (Y5)
     * and the control word written there (VS; [MAME kc85] for the bits).
     * DR interrupts on RST 6.5 and TRC/RRC run from the 81C55 TIMER OUT.
     * Time is accounted in CPU clocks, one register clock per TIMER OUT
     * period. */
    bm_im6402_t *uart;
    bm_timed_source_id_t uart_clock_id;
    uint64_t uart_accounted;
    uint8_t rx_queue[M10_SERIAL_QUEUE];
    size_t rx_head;
    size_t rx_count;
    int rx_line_busy;
    uint64_t rx_clocks_left;
    const bm_host_serial_services_t *serial;
    /* Buzzer changes for an audio host. */
    const bm_host_audio_services_t *audio;
    int audio_reported;
    int audio_level;
    uint16_t audio_period;
    /* Cassette (6.9): SOD, inverted by M34, drives MIC; EAR reaches SID
     * through a comparator and Schmitt trigger; REMOTE (latch bit 3,
     * 6.9.3) runs the motor through RY1. The tape is at tape_origin at CPU
     * clock tape_cycles and advances only while the motor runs. */
    const bm_host_cassette_services_t *cassette;
    /* Printer (6.11): data on PA0-PA7, STROBE on latch bit 1 (a 1 drives
     * it low through T8), BUSY on PC2 and SLCT on PC1 ([MAME kc85] for
     * SLCT). */
    const bm_host_parallel_services_t *printer;
    bm_cassette_tape_t tape;
    int tape_loaded;
    int motor;
    uint64_t tape_cycles;
    size_t tape_origin;
    /* The sample that changes SID next; the search for the following
     * crossing starts there, since an edge may come before its sample. */
    size_t tape_crossing;
    int sid_level;
    bm_timed_source_id_t tape_clock_id;
    uint8_t io_read_reported[32];
    uint8_t io_write_reported[32];
    uint8_t lcd_command_reported[32];
    uint8_t memory_reported; /* bit 0: read, bit 1: write */
} bm_m10_machine_t;

static void audio_update(bm_m10_machine_t *machine);

static uint64_t
cpu_cycles(const bm_m10_machine_t *machine)
{
    uint64_t cycles = 0U;
    (void) bm_engine_timed_source_cycle_count(machine->engine,
                                              machine->cpu_clock_id, &cycles);
    return cycles;
}

static uint64_t
io_time(const bm_m10_machine_t *machine)
{
    return cpu_cycles(machine) + M10_IO_CYCLE_OFFSET;
}

/* Engine time of a CPU clock count. bm_engine_now() only moves between
 * instructions' scheduling boundaries, so host reports use the clock. */
static bm_tick_t
cycle_time(uint64_t cycles)
{
    return (bm_tick_t) ((cycles / M10_CPU_HZ) * BM_MACHINE_CLOCKED_TICKS_PER_SECOND +
                        ((cycles % M10_CPU_HZ) * BM_MACHINE_CLOCKED_TICKS_PER_SECOND) /
                            M10_CPU_HZ);
}

/* value * multiplier / divisor, rounded down or up, without overflow for
 * the clock and sample rates used here. */
static uint64_t
scale_down(uint64_t value, uint64_t multiplier, uint64_t divisor)
{
    return (value / divisor) * multiplier + ((value % divisor) * multiplier) / divisor;
}

static uint64_t
scale_up(uint64_t value, uint64_t multiplier, uint64_t divisor)
{
    const uint64_t rest = (value % divisor) * multiplier;
    return (value / divisor) * multiplier + rest / divisor +
           ((rest % divisor) != 0U ? 1U : 0U);
}

/* The sample the tape has reached. */
static size_t
tape_position(const bm_m10_machine_t *machine)
{
    uint64_t position;
    const uint64_t now = cpu_cycles(machine);
    if (!machine->tape_loaded)
        return 0U;
    /* The motor starts at the I/O cycle of its OUT, after the boundary. */
    if (!machine->motor || (now <= machine->tape_cycles))
        return machine->tape_origin;
    position = machine->tape_origin +
               scale_down(now - machine->tape_cycles,
                          machine->tape.sample_rate, M10_CPU_HZ);
    return position < machine->tape.sample_count ?
               (size_t) position : machine->tape.sample_count;
}

/* CPU clocks from now until the tape crosses the Schmitt trigger's next
 * threshold, or 0 when the tape stands still or never crosses it. The
 * signal is taken as a straight line between samples (assumption), so the
 * crossing falls between them: at 11025 Hz a 2400 Hz half cycle spans two
 * or three samples, and the sample times alone would stretch some halves
 * past the loader's limit. */
static uint64_t
tape_next_event(bm_m10_machine_t *machine)
{
    size_t index;
    size_t start;
    uint64_t target;
    const uint64_t now = cpu_cycles(machine);
    const int threshold = machine->sid_level ? -M10_TAPE_THRESHOLD : M10_TAPE_THRESHOLD;

    if (!machine->tape_loaded || !machine->motor)
        return 0U;
    start = tape_position(machine);
    if (machine->tape_crossing > start)
        start = machine->tape_crossing;
    for (index = start; index < machine->tape.sample_count; ++index) {
        const int16_t sample = machine->tape.samples[index];
        if (machine->sid_level ? (sample < threshold) : (sample > threshold))
            break;
    }
    if (index >= machine->tape.sample_count)
        return 0U;
    machine->tape_crossing = index;
    if (index == start) {
        /* Already past the threshold where the tape stands. */
        target = machine->tape_cycles +
                 scale_up(index - machine->tape_origin, M10_CPU_HZ,
                          machine->tape.sample_rate);
    } else {
        /* Between index - 1 (not past the threshold) and index. */
        const int previous = machine->tape.samples[index - 1U];
        const int sample = machine->tape.samples[index];
        const uint64_t part = (uint64_t) (machine->sid_level ? previous - threshold :
                                                               threshold - previous);
        const uint64_t whole = (uint64_t) (machine->sid_level ? previous - sample :
                                                                sample - previous);
        target = machine->tape_cycles +
                 scale_up(index - 1U - machine->tape_origin, M10_CPU_HZ,
                          machine->tape.sample_rate) +
                 scale_up(part * M10_CPU_HZ, 1U, whole * machine->tape.sample_rate);
    }
    return target > now ? target - now : 1U;
}

static void
tape_rearm(bm_m10_machine_t *machine)
{
    const uint64_t cycles = tape_next_event(machine);
    if (cycles == 0U)
        (void) bm_engine_disarm_timed_source(machine->engine,
                                             machine->tape_clock_id);
    else
        (void) bm_engine_arm_timed_source(machine->engine,
                                          machine->tape_clock_id, cycles);
}

static bm_status_t
tape_fire(bm_engine_t *engine, void *context, const bm_time_point_t *when,
          uint64_t *cycles_until_next)
{
    bm_m10_machine_t *machine = context;
    (void) engine;
    (void) when;
    machine->sid_level = !machine->sid_level;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                    BM_I8085_SIGNAL_SID, machine->sid_level);
    *cycles_until_next = tape_next_event(machine);
    return *cycles_until_next != 0U ? BM_STATUS_OK : BM_STATUS_IDLE;
}

/* REMOTE: the tape stops or starts where it is. */
static void
tape_set_motor(bm_m10_machine_t *machine, int on)
{
    if (machine->motor == on)
        return;
    machine->tape_origin = tape_position(machine);
    machine->tape_cycles = io_time(machine);
    machine->motor = on;
    if (machine->cassette != NULL)
        machine->cassette->motor(machine->cassette->context, 0U, on,
                                 cycle_time(io_time(machine)));
    tape_rearm(machine);
}

/* SOD changes in half CLK periods after the end of SIM (4 clocks); M34
 * inverts it on its way to MIC. */
static void
m10_sod(void *context, int level, uint32_t half_clocks_after_boundary)
{
    bm_m10_machine_t *machine = context;
    const uint64_t half_clocks = 2U * (cpu_cycles(machine) + 4U) +
                                 half_clocks_after_boundary;
    if (machine->cassette != NULL)
        machine->cassette->record(machine->cassette->context, 0U, !level,
                                  (bm_tick_t) scale_down(half_clocks,
                                                         BM_MACHINE_CLOCKED_TICKS_PER_SECOND,
                                                         2U * M10_CPU_HZ));
}

static bm_status_t
m10_cassette_load(void *context, uint32_t port, const bm_cassette_tape_t *tape)
{
    bm_m10_machine_t *machine = context;
    if ((machine == NULL) || (port != 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    machine->tape_loaded = tape != NULL;
    if (tape != NULL) {
        machine->tape = *tape;
        machine->tape_origin = tape->position;
        machine->tape_crossing = tape->position;
        machine->tape_cycles = cpu_cycles(machine);
    } else {
        memset(&machine->tape, 0, sizeof(machine->tape));
        machine->tape_crossing = 0U;
    }
    tape_rearm(machine);
    return BM_STATUS_OK;
}

static bm_status_t
m10_cassette_position(const void *context, uint32_t port, size_t *position)
{
    const bm_m10_machine_t *machine = context;
    if ((machine == NULL) || (port != 0U) || (position == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *position = tape_position(machine);
    return BM_STATUS_OK;
}

/* TIMER OUT period in CPU clocks when the timer runs continuously, else 0:
 * the UART then has no clock. */
static uint16_t
timer_out_period(bm_m10_machine_t *machine)
{
    bm_msm81c55_timer_mode_t mode;
    const uint16_t period = bm_msm81c55_timer_period(machine->pio,
                                                     io_time(machine), &mode);
    if ((mode != BM_MSM81C55_CONTINUOUS_SQUARE) &&
        (mode != BM_MSM81C55_CONTINUOUS_PULSE))
        return 0U;
    return period;
}

/* Brings the UART up to the current CPU clock: whole register clocks are
 * applied to the transmitter and to the character on the receive line. */
static void
uart_sync(bm_m10_machine_t *machine)
{
    const uint64_t now = cpu_cycles(machine);
    const uint16_t period = timer_out_period(machine);
    uint64_t clocks;
    uint64_t remaining;

    if ((period == 0U) || (now <= machine->uart_accounted)) {
        if (period == 0U)
            machine->uart_accounted = now;
        return;
    }
    clocks = (now - machine->uart_accounted) / period;
    machine->uart_accounted += clocks * period;
    remaining = clocks;
    while (remaining > 0U) {
        uint64_t step;
        if (!machine->rx_line_busy) {
            if (machine->rx_count == 0U)
                break;
            machine->rx_line_busy = 1;
            machine->rx_clocks_left = bm_im6402_frame_clocks(machine->uart);
        }
        step = remaining < machine->rx_clocks_left ? remaining :
                                                     machine->rx_clocks_left;
        machine->rx_clocks_left -= step;
        remaining -= step;
        if (machine->rx_clocks_left == 0U) {
            /* Host characters arrive well formed. */
            bm_im6402_receive(machine->uart,
                              machine->rx_queue[machine->rx_head], 0, 0);
            machine->rx_head = (machine->rx_head + 1U) % M10_SERIAL_QUEUE;
            --machine->rx_count;
            machine->rx_line_busy = 0;
        }
    }
    bm_im6402_advance(machine->uart, clocks);
}

/* CPU clocks until the next UART event, or 0 when there is none. */
static uint64_t
uart_next_event(bm_m10_machine_t *machine)
{
    const uint16_t period = timer_out_period(machine);
    uint64_t clocks = bm_im6402_clocks_until_transmitted(machine->uart);
    uint64_t receive = UINT64_MAX;
    uint64_t elapsed;
    uint64_t cycles;

    if (period == 0U)
        return 0U;
    if (machine->rx_line_busy)
        receive = machine->rx_clocks_left;
    else if (machine->rx_count > 0U)
        receive = bm_im6402_frame_clocks(machine->uart);
    if (receive < clocks)
        clocks = receive;
    if (clocks == UINT64_MAX)
        return 0U;
    elapsed = cpu_cycles(machine) - machine->uart_accounted;
    cycles = clocks * period;
    return cycles > elapsed ? cycles - elapsed : 1U;
}

static void
uart_rearm(bm_m10_machine_t *machine)
{
    const uint64_t cycles = uart_next_event(machine);
    if (cycles == 0U)
        (void) bm_engine_disarm_timed_source(machine->engine,
                                             machine->uart_clock_id);
    else
        (void) bm_engine_arm_timed_source(machine->engine,
                                          machine->uart_clock_id, cycles);
}

static bm_status_t
uart_fire(bm_engine_t *engine, void *context, const bm_time_point_t *when,
          uint64_t *cycles_until_next)
{
    bm_m10_machine_t *machine = context;
    (void) engine;
    (void) when;
    uart_sync(machine);
    *cycles_until_next = uart_next_event(machine);
    return *cycles_until_next != 0U ? BM_STATUS_OK : BM_STATUS_IDLE;
}

static void
uart_transmitted(void *context, uint8_t data)
{
    bm_m10_machine_t *machine = context;
    if ((machine->serial != NULL) && (machine->serial->transmit != NULL))
        machine->serial->transmit(machine->serial->context, 0U, data,
                                  cycle_time(cpu_cycles(machine)));
}

static void
uart_data_ready(void *context, int level)
{
    bm_m10_machine_t *machine = context;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                    BM_I8085_SIGNAL_RST65, level);
}

static uint8_t
uart_status(bm_m10_machine_t *machine)
{
    const bm_im6402_status_t status = bm_im6402_status(machine->uart);
    return (uint8_t) (M10_UART_STATUS_FIXED | (status.oe << 1U) |
                      (status.fe << 2U) | (status.pe << 3U) |
                      (status.tbre << 4U));
}

/* Control word ([MAME kc85], secondary): bit 0 SBS, bit 1 EPE, bit 2 PI,
 * bits 3-4 CLS1/CLS2 (5 + CLS1 + 2 x CLS2 data bits, [IM6402] pin 37). */
static void
uart_control(bm_m10_machine_t *machine, uint8_t value)
{
    bm_im6402_control_t control;
    control.stop_select = (uint8_t) (value & 1U);
    control.even_parity = (uint8_t) ((value >> 1U) & 1U);
    control.parity_inhibit = (uint8_t) ((value >> 2U) & 1U);
    control.character_bits = (uint8_t) (5U + ((value >> 3U) & 1U) +
                                        2U * ((value >> 4U) & 1U));
    bm_im6402_load_control(machine->uart, &control);
}

static void
rtc_catch_up(bm_m10_machine_t *machine)
{
    uint64_t oscillator = 0U;
    if (bm_engine_timed_source_cycle_count(machine->engine,
                                           machine->rtc_clock_id,
                                           &oscillator) != BM_STATUS_OK)
        return;
    if (oscillator > machine->rtc_accounted) {
        bm_upd1990ac_advance(machine->rtc, oscillator - machine->rtc_accounted);
        machine->rtc_accounted = oscillator;
    }
}

static void
rtc_rearm(bm_m10_machine_t *machine)
{
    (void) bm_engine_arm_timed_source(
        machine->engine, machine->rtc_clock_id,
        bm_upd1990ac_cycles_until_tp_change(machine->rtc));
}

static void
rtc_tp_changed(void *context, int level)
{
    bm_m10_machine_t *machine = context;
    machine->rtc_tp_level = level;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                    BM_I8085_SIGNAL_RST75, level);
}

static bm_status_t
rtc_fire(bm_engine_t *engine, void *context, const bm_time_point_t *when,
         uint64_t *cycles_until_next)
{
    bm_m10_machine_t *machine = context;
    (void) engine;
    (void) when;
    rtc_catch_up(machine);
    *cycles_until_next = bm_upd1990ac_cycles_until_tp_change(machine->rtc);
    return BM_STATUS_OK;
}

/* Never armed: the source only measures time in CPU clocks. */
static bm_status_t
cpu_clock_fire(bm_engine_t *engine, void *context, const bm_time_point_t *when,
               uint64_t *cycles_until_next)
{
    (void) engine;
    (void) context;
    (void) when;
    *cycles_until_next = 0U;
    return BM_STATUS_IDLE;
}

static void
power_off(bm_m10_machine_t *machine)
{
    machine->powered_off = 1;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                    BM_I8085_SIGNAL_RESET, 1);
}

/* PA and PB pin levels: outputs from the latches, inputs float high. */
static void
pio_port_changed(void *context, unsigned int port)
{
    bm_m10_machine_t *machine = context;
    const uint8_t levels = bm_msm81c55_port_levels(machine->pio, port, 0xffU);

    if (port == BM_MSM81C55_PORT_A) {
        machine->port_a = levels;
        rtc_catch_up(machine);
        bm_upd1990ac_set_command_pins(machine->rtc, (uint8_t) (levels & 7U));
        bm_upd1990ac_set_data_in(machine->rtc, (levels >> 4U) & 1U);
        bm_upd1990ac_set_clk(machine->rtc, (levels >> 3U) & 1U);
    } else if (port == BM_MSM81C55_PORT_B) {
        const uint8_t before = machine->port_b;
        const int driven = (bm_msm81c55_command(machine->pio) & 0x02U) != 0U;
        machine->port_b = levels;
        /* Only edges the 81C55 drives count; reset leaves the pins
         * floating. */
        if (driven && (((before ^ levels) & 0x20U) != 0U) &&
            ((levels & 0x04U) != 0U))
            ++machine->buzzer_edges;
        if (driven && ((levels & 0x10U) != 0U))
            power_off(machine);
        audio_update(machine);
    }
}

/* Port C inputs (ALT1): PC0 uPD1990AC DATA OUT, PC1 SLCT and PC2 BUSY from
 * a host printer (6.11), PC4/PC5 language jumpers (6.16.3). Without a
 * printer PC1-PC2 float high, as does PC3 (bar-code reader, not modelled);
 * bits 6-7 do not exist and read high. */
static uint8_t
pio_read_pins(void *context, unsigned int port)
{
    bm_m10_machine_t *machine = context;
    uint8_t pins = 0xffU;

    if (port == BM_MSM81C55_PORT_C) {
        rtc_catch_up(machine);
        pins = (uint8_t) ((pins & 0xceU) |
                          (bm_upd1990ac_data_out(machine->rtc) & 1) |
                          ((unsigned int) machine->language << 4U));
        if (machine->printer != NULL) {
            /* Without a printer both lines stay high (BUSY). */
            const uint32_t status = machine->printer->status != NULL ?
                machine->printer->status(machine->printer->context, 0U) :
                BM_PARALLEL_STATUS_SELECTED;
            pins = (uint8_t) ((pins & ~0x06U) |
                              ((status & BM_PARALLEL_STATUS_SELECTED) != 0U ? 0x02U : 0U) |
                              ((status & BM_PARALLEL_STATUS_BUSY) != 0U ? 0x04U : 0U));
        }
    }
    return pins;
}

/* Returns 1 the first time bit index of the set is seen. */
static int
first_report(uint8_t *bits, unsigned int index)
{
    const uint8_t mask = (uint8_t) (1U << (index & 7U));

    if ((bits[index >> 3U] & mask) != 0U)
        return 0;
    bits[index >> 3U] = (uint8_t) (bits[index >> 3U] | mask);
    return 1;
}

static void
report_fallback(bm_m10_machine_t *machine, bm_tolerance_event_kind_t kind,
                const char *device, const char *note, uint64_t address,
                uint64_t value)
{
    bm_tolerance_event_t event;
    uint64_t pc = 0U;
    uint64_t opcode = 0U;

    if (machine->tolerance == NULL)
        return;
    memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.kind = kind;
    event.device = device;
    event.note = note;
    event.address = address;
    event.value = value;
    if (machine->cpu_ready &&
        (bm_engine_inspect_cpu(machine->engine, machine->cpu_id,
                               "instruction_pc", &pc) == BM_STATUS_OK) &&
        (bm_engine_inspect_cpu(machine->engine, machine->cpu_id,
                               "instruction_opcode", &opcode) == BM_STATUS_OK)) {
        event.flags = BM_TOLERANCE_FLAG_HAS_PC;
        event.pc = pc;
        event.opcode = (uint32_t) opcode;
    }
    bm_tolerance_report(machine->tolerance, &event);
}

static int
lcd_selected(const bm_m10_machine_t *machine, unsigned int chip)
{
    /* CS20-CS27 on PA0-PA7 and CS28-CS29 on PB0-PB1, active high (6.14,
     * Fig. 6-14, Table 6-5). */
    return chip < 8U ? (machine->port_a >> chip) & 1U :
                       (machine->port_b >> (chip - 8U)) & 1U;
}

/* F0h-FFh (Y7): A0 high selects data, low the instruction/status register.
 * Several selected chips drive the bus together (OR, as in MAME). */
static uint8_t
lcd_access(bm_m10_machine_t *machine, uint8_t port, int write, uint8_t value)
{
    const uint64_t now = io_time(machine);
    unsigned int index;
    unsigned int selected = 0U;
    uint8_t bus = 0U;

    for (index = 0U; index < M10_LCD_CHIPS; ++index) {
        bm_hd44102_t *chip = machine->lcd[index];
        uint8_t data = 0U;
        if (!lcd_selected(machine, index))
            continue;
        ++selected;
        if ((port & 1U) == 0U) {
            if (write) {
                /* An undefined instruction leaves the chip unchanged. */
                if ((bm_hd44102_write_instruction(chip, value, now) ==
                     BM_HD44102_REJECTED_UNDEFINED) &&
                    (machine->tolerance != NULL) &&
                    first_report(machine->lcd_command_reported, value))
                    report_fallback(machine,
                                    BM_TOLERANCE_UNKNOWN_DEVICE_COMMAND,
                                    "HD44102",
                                    "undefined instruction ignored",
                                    port, value);
            } else
                data = bm_hd44102_read_status(chip, now);
        } else {
            if (write)
                (void) bm_hd44102_write_data(chip, value, now);
            else
                (void) bm_hd44102_read_data(chip, now, &data);
        }
        bus = (uint8_t) (bus | data);
    }
    return selected != 0U ? bus : M10_OPEN_BUS;
}

static bm_status_t
m10_io(void *context, bm_bus_transaction_t *txn)
{
    bm_m10_machine_t *machine = context;
    const uint8_t port = (uint8_t) txn->address;
    const int write = txn->operation == BM_BUS_WRITE;
    const uint8_t value = (uint8_t) txn->value;
    uint8_t result = M10_OPEN_BUS;
    /* 81C55 registers 6 and 7 do not exist; A0h-AFh and the remaining
     * ports are not modelled. */
    const int modelled = (((port & 0xf0U) == 0xb0U) && ((port & 7U) < 6U)) ||
                         ((port & 0xf0U) == 0xc0U) || ((port & 0xf0U) == 0xd0U) ||
                         ((port & 0xf0U) == 0xe0U) || ((port & 0xf0U) == 0xf0U);

    if (!modelled && (machine->tolerance != NULL) &&
        first_report(write ? machine->io_write_reported :
                             machine->io_read_reported, port))
        report_fallback(machine,
                        write ? BM_TOLERANCE_UNMODELLED_IO_WRITE :
                                BM_TOLERANCE_UNMODELLED_IO_READ,
                        "Olivetti M10 I/O",
                        write ? "write to an unmodelled port ignored" :
                                "unmodelled port reads FFh (open bus)",
                        port, write ? value : M10_OPEN_BUS);
    if ((port & 0xf0U) == 0xb0U) {
        if (write) {
            /* The timer is the UART clock and drives the buzzer. */
            uart_sync(machine);
            bm_msm81c55_write(machine->pio, (uint8_t) (port & 7U), value,
                              io_time(machine));
            uart_rearm(machine);
            audio_update(machine);
        } else
            result = bm_msm81c55_read(machine->pio, (uint8_t) (port & 7U),
                                      io_time(machine));
    } else if ((port & 0xf0U) == 0xc0U) {
        uart_sync(machine);
        if (write)
            bm_im6402_load_transmitter(machine->uart, value);
        else {
            result = bm_im6402_receiver_buffer(machine->uart);
            /* Reading the buffer resets DR (inferred, [MAME kc85]). */
            bm_im6402_reset_data_received(machine->uart);
        }
        uart_rearm(machine);
    } else if ((port & 0xf0U) == 0xd0U) {
        uart_sync(machine);
        if (write)
            uart_control(machine, value);
        else
            result = uart_status(machine);
        uart_rearm(machine);
    } else if ((port & 0xf0U) == 0xe0U) {
        if (write) {
            /* The printer takes the data as STROBE goes active. */
            if ((machine->printer != NULL) && ((value & 0x02U) != 0U) &&
                ((machine->control_latch & 0x02U) == 0U))
                machine->printer->write(machine->printer->context, 0U,
                                        machine->port_a,
                                        cycle_time(io_time(machine)));
            machine->control_latch = (uint8_t) (value & 0x0fU);
            rtc_catch_up(machine);
            bm_upd1990ac_set_stb(machine->rtc, (value >> 2U) & 1U);
            rtc_rearm(machine);
            tape_set_motor(machine, (value >> 3U) & 1U);
        } else {
            result = m10_keyboard_scan(&machine->keyboard, machine->port_a,
                                       machine->port_b);
        }
    } else if ((port & 0xf0U) == 0xf0U) {
        result = lcd_access(machine, port, write, value);
    }
    /* A0h-AFh and the rest are not modelled. */
    if (!write)
        txn->value = result;
    return BM_STATUS_OK;
}

static bm_status_t
m10_memory(void *context, bm_bus_transaction_t *txn)
{
    bm_m10_machine_t *machine = context;
    const uint32_t address = (uint32_t) (txn->address & 0xffffU);

    if (address < M10_ROM_END) {
        if (txn->operation != BM_BUS_WRITE)
            txn->value = machine->rom[address];
    } else if (address >= machine->ram_base) {
        if (txn->operation == BM_BUS_WRITE)
            machine->ram[address - M10_RAM_BASE] = (uint8_t) txn->value;
        else
            txn->value = machine->ram[address - M10_RAM_BASE];
    } else {
        /* Between the ROM and installed RAM: no device answers. */
        const int write = txn->operation == BM_BUS_WRITE;
        const uint8_t bit = write ? 2U : 1U;
        if (!write)
            txn->value = M10_OPEN_BUS;
        if ((machine->tolerance != NULL) &&
            ((machine->memory_reported & bit) == 0U)) {
            machine->memory_reported = (uint8_t) (machine->memory_reported | bit);
            report_fallback(machine,
                            write ? BM_TOLERANCE_UNMODELLED_MEMORY_WRITE :
                                    BM_TOLERANCE_UNMODELLED_MEMORY_READ,
                            "Olivetti M10 memory",
                            write ? "no RAM installed here; write ignored" :
                                    "no RAM installed here; reads FFh",
                            address, write ? (uint8_t) txn->value : M10_OPEN_BUS);
        }
    }
    return BM_STATUS_OK;
}

static int
valid_time(const bm_upd1990ac_time_t *t)
{
    return (t->second < 60U) && (t->minute < 60U) && (t->hour < 24U) &&
           (t->day >= 1U) && (t->day <= 31U) && (t->weekday < 7U) &&
           (t->month >= 1U) && (t->month <= 12U);
}

static int
rtc_state_valid(const uint8_t *state, size_t size)
{
    return (state != NULL) && (size == BM_M10_RTC_STATE_SIZE) &&
           (memcmp(state, "M10R", 4U) == 0) && (state[4] == RTC_STATE_VERSION);
}

static int64_t
read_le64(const uint8_t *data)
{
    uint64_t value = 0U;
    unsigned int index;
    for (index = 0U; index < 8U; ++index)
        value |= (uint64_t) data[index] << (8U * index);
    return (int64_t) value;
}

static bm_status_t
m10_validate(const bm_configuration_view_t *view)
{
    const bm_m10_config_t *config;

    if ((view == NULL) || (view->type == NULL) ||
        (strcmp(view->type, BM_M10_CONFIG_TYPE) != 0) ||
        (view->version != BM_M10_CONFIG_VERSION) ||
        (view->size != sizeof(bm_m10_config_t)) || (view->data == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    config = view->data;
    if ((config->firmware.data == NULL) ||
        (config->firmware.size != BM_M10_FIRMWARE_SIZE) ||
        ((config->ram_kib != 8U) && (config->ram_kib != 16U) &&
         (config->ram_kib != 24U) && (config->ram_kib != 32U)) ||
        ((unsigned int) config->language > 3U) ||
        ((config->ram_state == NULL) != (config->ram_state_size == 0U)) ||
        ((config->ram_state != NULL) &&
         (config->ram_state_size != BM_M10_RAM_STATE_SIZE)) ||
        ((config->rtc_state == NULL) != (config->rtc_state_size == 0U)) ||
        ((config->rtc_state != NULL) &&
         (config->rtc_state_size != BM_M10_RTC_STATE_SIZE)) ||
        !valid_time(&config->rtc_initial_time))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static void
m10_destroy(void *context)
{
    bm_m10_machine_t *machine = context;
    unsigned int index;

    if (machine == NULL)
        return;
    for (index = 0U; index < M10_LCD_CHIPS; ++index)
        bm_hd44102_destroy(machine->lcd[index]);
    bm_upd1990ac_destroy(machine->rtc);
    bm_msm81c55_destroy(machine->pio);
    bm_im6402_destroy(machine->uart);
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
}

/* A valid saved clock continues from its state, advanced by the wall-clock
 * time the machine spent switched off. Otherwise the chip starts at the
 * configured time; a cold start of the firmware sets it again. */
static bm_status_t
restore_rtc(bm_m10_machine_t *machine, const bm_m10_config_t *config)
{
    const uint8_t *state = config->rtc_state;

    if (!rtc_state_valid(state, config->rtc_state_size) ||
        (bm_upd1990ac_load_state(machine->rtc, state + 16U,
                                 BM_UPD1990AC_STATE_SIZE) != BM_STATUS_OK)) {
        bm_upd1990ac_set_time(machine->rtc, &config->rtc_initial_time);
        return BM_STATUS_OK;
    }
    if (machine->wall_clock != NULL) {
        const int64_t saved = read_le64(state + 8U);
        const int64_t now = machine->wall_clock(machine->wall_clock_context);
        if ((saved != RTC_WALL_CLOCK_UNKNOWN) && (now > saved))
            bm_upd1990ac_advance_seconds(machine->rtc, (uint64_t) (now - saved));
    }
    return BM_STATUS_OK;
}

static bm_status_t
m10_create(bm_engine_t *engine, const bm_host_services_t *host,
           const bm_configuration_view_t *view, void **out_machine)
{
    static const bm_clock_rate_t cpu_rate = { M10_CPU_HZ, 1U };
    static const bm_clock_rate_t rtc_rate = { BM_UPD1990AC_OSC_HZ, 1U };
    const bm_m10_config_t *config;
    bm_m10_machine_t *machine;
    bm_status_t status;
    bm_cpu_t cpu;
    unsigned int index;

    if ((engine == NULL) || (out_machine == NULL) ||
        (bm_host_services_validate(host) != BM_STATUS_OK) ||
        (m10_validate(view) != BM_STATUS_OK))
        return BM_STATUS_INVALID_ARGUMENT;
    config = view->data;
    machine = host->allocate(host->context, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(machine, 0, sizeof(*machine));
    machine->host = *host;
    machine->engine = engine;
    machine->rom = config->firmware.data;
    machine->ram_kib = config->ram_kib;
    machine->ram_base = 0x10000U - config->ram_kib * 1024U;
    machine->language = config->language;
    machine->wall_clock = config->wall_clock;
    machine->wall_clock_context = config->wall_clock_context;
    machine->tolerance = bm_host_tolerance(host);
    machine->serial = bm_host_serial(host);
    machine->audio = bm_host_audio(host);
    machine->cassette = bm_host_cassette(host);
    machine->printer = bm_host_parallel(host);
    machine->port_a = machine->port_b = 0xffU;
    if (config->ram_state != NULL)
        memcpy(machine->ram + (machine->ram_base - M10_RAM_BASE),
               config->ram_state + (machine->ram_base - M10_RAM_BASE),
               0x10000U - machine->ram_base);
    m10_keyboard_reset(&machine->keyboard);

    status = bm_bus_create(host, 2U, &machine->bus);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_MEMORY, 0U, 0xffffU,
                            m10_memory, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0U, 0xffU, m10_io,
                            machine);
    for (index = 0U; (status == BM_STATUS_OK) && (index < M10_LCD_CHIPS);
         ++index) {
        status = bm_hd44102_create(host, M10_LCD_BUSY_CYCLES,
                                   &machine->lcd[index]);
        if (status == BM_STATUS_OK) {
            bm_hd44102_set_reset(machine->lcd[index], 1);
            bm_hd44102_set_reset(machine->lcd[index], 0);
        }
    }
    if (status == BM_STATUS_OK) {
        bm_upd1990ac_config_t rtc_config;
        memset(&rtc_config, 0, sizeof(rtc_config));
        rtc_config.initial_time = config->rtc_initial_time;
        rtc_config.tp_changed = rtc_tp_changed;
        rtc_config.tp_context = machine;
        status = bm_upd1990ac_create(host, &rtc_config, &machine->rtc);
    }
    if (status == BM_STATUS_OK)
        status = restore_rtc(machine, config);
    if (status == BM_STATUS_OK) {
        const bm_msm81c55_config_t pio_config = {
            pio_read_pins, pio_port_changed, machine
        };
        status = bm_msm81c55_create(host, &pio_config, &machine->pio);
    }
    if (status == BM_STATUS_OK) {
        const bm_im6402_config_t uart_config = {
            uart_transmitted, uart_data_ready, machine
        };
        status = bm_im6402_create(host, &uart_config, &machine->uart);
    }
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, rtc_fire, machine, &rtc_rate,
                                             bm_upd1990ac_cycles_until_tp_change(machine->rtc),
                                             &machine->rtc_clock_id);
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, cpu_clock_fire, machine,
                                             &cpu_rate, 0U,
                                             &machine->cpu_clock_id);
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, uart_fire, machine,
                                             &cpu_rate, 0U,
                                             &machine->uart_clock_id);
    if (status == BM_STATUS_OK)
        status = bm_engine_add_timed_source(engine, tape_fire, machine,
                                             &cpu_rate, 0U,
                                             &machine->tape_clock_id);
    if (status == BM_STATUS_OK) {
        const bm_i8085_config_t cpu_config = {
            .model = BM_I8085_OKI_MSM80C85A,
            .frequency_hz = M10_CPU_HZ,
            .bus = machine->bus,
            .sod = m10_sod,
            .sod_context = machine
        };
        status = bm_i8085_create(host, &cpu_config, &cpu);
    }
    if (status == BM_STATUS_OK) {
        status = bm_engine_add_clocked_cpu(engine, &cpu, bm_i8085_step_clocked,
                                           &cpu_rate, &machine->cpu_id);
        if (status != BM_STATUS_OK)
            cpu.ops.destroy(cpu.context);
        else
            machine->cpu_ready = 1;
    }
    if (status != BM_STATUS_OK) {
        m10_destroy(machine);
        return status;
    }
    machine->rtc_tp_level = bm_upd1990ac_tp(machine->rtc);
    (void) bm_engine_signal_cpu(engine, machine->cpu_id, BM_I8085_SIGNAL_RST75,
                                machine->rtc_tp_level);
    *out_machine = machine;
    return BM_STATUS_OK;
}

/* RESET (button or power switch): the CPU, the 81C55 and the LCD drivers
 * restart; RAM, the clock and the keyboard keep their state. Engine time
 * restarts at zero. */
static bm_status_t
m10_reset(void *context)
{
    bm_m10_machine_t *machine = context;
    unsigned int index;

    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    machine->rtc_accounted = 0U;
    machine->powered_off = 0;
    machine->control_latch = 0U;
    tape_set_motor(machine, 0);
    /* The 81C55 reset reports the buzzer as its ports float. */
    machine->audio_reported = 0;
    bm_msm81c55_reset(machine->pio, 0U);
    /* RESET also resets the UART (assumption: MR on the system RESET);
     * characters not yet delivered are dropped. */
    bm_im6402_master_reset(machine->uart);
    machine->uart_accounted = 0U;
    machine->rx_head = 0U;
    machine->rx_count = 0U;
    machine->rx_line_busy = 0;
    machine->rx_clocks_left = 0U;
    for (index = 0U; index < M10_LCD_CHIPS; ++index) {
        bm_hd44102_set_reset(machine->lcd[index], 1);
        bm_hd44102_set_reset(machine->lcd[index], 0);
    }
    bm_upd1990ac_set_stb(machine->rtc, 0);
    rtc_rearm(machine);
    (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                BM_I8085_SIGNAL_RESET, 0);
    (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id,
                                BM_I8085_SIGNAL_RST75, machine->rtc_tp_level);
    return BM_STATUS_OK;
}

static int
buzzer_level(bm_m10_machine_t *machine)
{
    const int timer_out = bm_msm81c55_timer_out(machine->pio, io_time(machine));
    return ((machine->port_b & 0x20U) != 0U) &&
           (((machine->port_b & 0x04U) != 0U) || timer_out);
}

/* Reports what the buzzer plays when it changes: the timer's square wave
 * while PB5 is high and PB2 low (6.12), otherwise the drive level. */
static void
audio_update(bm_m10_machine_t *machine)
{
    bm_msm81c55_timer_mode_t mode;
    uint16_t period;
    int level;
    bm_audio_change_t change;

    if ((machine->audio == NULL) || (machine->pio == NULL))
        return;
    period = bm_msm81c55_timer_period(machine->pio, io_time(machine), &mode);
    if (((machine->port_b & 0x24U) != 0x20U) ||
        (mode != BM_MSM81C55_CONTINUOUS_SQUARE))
        period = 0U;
    level = period != 0U ? 0 : buzzer_level(machine);
    if (machine->audio_reported && (machine->audio_level == level) &&
        (machine->audio_period == period))
        return;
    machine->audio_reported = 1;
    machine->audio_level = level;
    machine->audio_period = period;
    memset(&change, 0, sizeof(change));
    change.size = sizeof(change);
    change.channel = 0U;
    change.time = cycle_time(io_time(machine));
    change.level = level;
    change.tone_numerator = period != 0U ? M10_CPU_HZ : 0U;
    change.tone_denominator = period;
    machine->audio->change(machine->audio->context, &change);
}

static bm_status_t
m10_serial_receive(void *context, uint32_t port, const uint8_t *data,
                   size_t size, size_t *accepted)
{
    bm_m10_machine_t *machine = context;
    size_t index;

    if ((machine == NULL) || (accepted == NULL) ||
        ((data == NULL) && (size != 0U)))
        return BM_STATUS_INVALID_ARGUMENT;
    *accepted = 0U;
    if (port != 0U)
        return BM_STATUS_INVALID_ARGUMENT;
    uart_sync(machine);
    for (index = 0U; (index < size) && (machine->rx_count < M10_SERIAL_QUEUE);
         ++index) {
        machine->rx_queue[(machine->rx_head + machine->rx_count) %
                          M10_SERIAL_QUEUE] = data[index];
        ++machine->rx_count;
    }
    *accepted = index;
    uart_rearm(machine);
    return BM_STATUS_OK;
}

static uint64_t
buzzer_tone_hz(bm_m10_machine_t *machine)
{
    bm_msm81c55_timer_mode_t mode;
    const uint16_t period = bm_msm81c55_timer_period(machine->pio,
                                                     io_time(machine), &mode);
    /* Timer mode: PB5 high and PB2 low gate the square wave (6.12). */
    if (((machine->port_b & 0x24U) != 0x20U) || (period == 0U) ||
        (mode != BM_MSM81C55_CONTINUOUS_SQUARE))
        return 0U;
    return M10_CPU_HZ / period;
}

static bm_status_t
m10_inspect(const void *context, const char *name, uint64_t *value)
{
    bm_m10_machine_t *machine = (bm_m10_machine_t *) context;
    bm_upd1990ac_time_t time;

    if ((machine == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    rtc_catch_up(machine);
    bm_upd1990ac_get_time(machine->rtc, &time);
    if (strcmp(name, "ram_kib") == 0)
        *value = machine->ram_kib;
    else if (strcmp(name, "language") == 0)
        *value = (uint64_t) machine->language;
    else if (strcmp(name, "port_a") == 0)
        *value = machine->port_a;
    else if (strcmp(name, "port_b") == 0)
        *value = machine->port_b;
    else if (strcmp(name, "control_latch") == 0)
        *value = machine->control_latch;
    else if (strcmp(name, "cassette_motor") == 0)
        *value = (uint64_t) machine->motor;
    else if (strcmp(name, "sid") == 0)
        *value = (uint64_t) machine->sid_level;
    else if (strcmp(name, "powered_off") == 0)
        *value = (uint64_t) machine->powered_off;
    else if (strcmp(name, "caps_lock") == 0)
        *value = (uint64_t) machine->keyboard.caps_lock;
    else if (strcmp(name, "rtc_month") == 0)
        *value = time.month;
    else if (strcmp(name, "rtc_day") == 0)
        *value = time.day;
    else if (strcmp(name, "rtc_weekday") == 0)
        *value = time.weekday;
    else if (strcmp(name, "rtc_hour") == 0)
        *value = time.hour;
    else if (strcmp(name, "rtc_minute") == 0)
        *value = time.minute;
    else if (strcmp(name, "rtc_second") == 0)
        *value = time.second;
    else if (strcmp(name, "rtc_tp_hz") == 0)
        *value = bm_upd1990ac_tp_hz(machine->rtc);
    else if (strcmp(name, "buzzer_level") == 0)
        *value = (uint64_t) buzzer_level(machine);
    else if (strcmp(name, "buzzer_tone_hz") == 0)
        *value = buzzer_tone_hz(machine);
    else if (strcmp(name, "buzzer_edges") == 0)
        *value = machine->buzzer_edges;
    else
        return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t
m10_input(void *context, const bm_input_event_t *event)
{
    bm_m10_machine_t *machine = context;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return m10_keyboard_input(&machine->keyboard, event);
}

static bm_status_t
m10_keyboard_leds(const void *context, bm_keyboard_led_state_t *state)
{
    const bm_m10_machine_t *machine = context;
    if ((machine == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->indicators = machine->keyboard.caps_lock ?
        (uint8_t) BM_KEYBOARD_LED_CAPS_LOCK : 0U;
    return BM_STATUS_OK;
}

static bm_status_t
m10_video_geometry(const void *context, bm_video_geometry_t *geometry)
{
    if ((context == NULL) || (geometry == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    memset(geometry, 0, sizeof(*geometry));
    geometry->width = M10_LCD_WIDTH;
    geometry->height = M10_LCD_HEIGHT;
    geometry->format = BM_PIXEL_XRGB8888;
    return BM_STATUS_OK;
}

/* Chip i drives columns (i % 5) * 50 and rows (i / 5) * 32 (MAME kc85,
 * secondary; consistent with the guide's screens). The reflective panel's
 * colours are approximate. */
static bm_status_t
m10_video_render(const void *context, bm_tick_t emulated_time,
                 bm_video_framebuffer_t *framebuffer)
{
    static const uint32_t panel_off = UINT32_C(0xffb8c0a8);
    static const uint32_t panel_on = UINT32_C(0xff283020);
    const bm_m10_machine_t *machine = context;
    uint8_t dots[BM_HD44102_ROWS][BM_HD44102_COLUMNS];
    unsigned int index;
    unsigned int row;
    unsigned int column;

    (void) emulated_time;
    if ((machine == NULL) || (framebuffer == NULL) ||
        (framebuffer->pixels == NULL) ||
        (framebuffer->stride < M10_LCD_WIDTH) ||
        (framebuffer->pixel_capacity / framebuffer->stride < M10_LCD_HEIGHT))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < M10_LCD_CHIPS; ++index) {
        if (machine->powered_off)
            memset(dots, 0, sizeof(dots));
        else
            bm_hd44102_render(machine->lcd[index], dots);
        for (row = 0U; row < BM_HD44102_ROWS; ++row) {
            uint32_t *line = framebuffer->pixels +
                (size_t) ((index / 5U) * BM_HD44102_ROWS + row) * framebuffer->stride;
            /* Five chips address 250 columns; the panel shows 240. */
            for (column = 0U; column < BM_HD44102_COLUMNS; ++column) {
                const unsigned int x = (index % 5U) * BM_HD44102_COLUMNS + column;
                if (x < M10_LCD_WIDTH)
                    line[x] = dots[row][column] ? panel_on : panel_off;
            }
        }
    }
    (void) m10_video_geometry(machine, &framebuffer->geometry);
    return BM_STATUS_OK;
}

static bm_status_t
m10_persistent_state_size(const void *context, const char *name, size_t *size)
{
    if ((context == NULL) || (name == NULL) || (size == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "ram") == 0)
        *size = BM_M10_RAM_STATE_SIZE;
    else if (strcmp(name, "rtc") == 0)
        *size = BM_M10_RTC_STATE_SIZE;
    else
        return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t
m10_save_persistent_state(const void *context, const char *name,
                          uint8_t *data, size_t size)
{
    bm_m10_machine_t *machine = (bm_m10_machine_t *) context;
    size_t expected;
    bm_status_t status = m10_persistent_state_size(context, name, &expected);

    if (status != BM_STATUS_OK)
        return status;
    if ((data == NULL) || (size != expected))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "ram") == 0) {
        const size_t hole = machine->ram_base - M10_RAM_BASE;
        memset(data, 0, hole);
        memcpy(data + hole, machine->ram + hole, size - hole);
        return BM_STATUS_OK;
    }
    {
        const int64_t now = machine->wall_clock != NULL ?
            machine->wall_clock(machine->wall_clock_context) :
            RTC_WALL_CLOCK_UNKNOWN;
        unsigned int index;
        memset(data, 0, size);
        memcpy(data, "M10R", 4U);
        data[4] = RTC_STATE_VERSION;
        for (index = 0U; index < 8U; ++index)
            data[8U + index] = (uint8_t) ((uint64_t) now >> (8U * index));
        rtc_catch_up(machine);
        return bm_upd1990ac_save_state(machine->rtc, data + 16U,
                                       BM_UPD1990AC_STATE_SIZE);
    }
}

static const bm_machine_definition_t m10_definition = {
    .id = "olivetti-m10",
    .scheduler_ticks_per_second = BM_MACHINE_CLOCKED_TICKS_PER_SECOND,
    .engine_mode = BM_MACHINE_ENGINE_CLOCKED,
    .configuration = {
        BM_M10_CONFIG_TYPE, BM_M10_CONFIG_VERSION, sizeof(bm_m10_config_t)
    },
    .ops = {
        .validate = m10_validate,
        .create = m10_create,
        .destroy = m10_destroy,
        .reset = m10_reset,
        .inspect = m10_inspect,
        .input = m10_input,
        .keyboard_leds = m10_keyboard_leds,
        .video_geometry = m10_video_geometry,
        .video_render = m10_video_render,
        .persistent_state_size = m10_persistent_state_size,
        .save_persistent_state = m10_save_persistent_state,
        .serial_receive = m10_serial_receive,
        .cassette_load = m10_cassette_load,
        .cassette_position = m10_cassette_position
    },
    .engine = { 1U, 4U, 4U }
};

const bm_machine_definition_t *
bm_m10_machine_definition(void)
{
    return &m10_definition;
}

bm_machine_config_t
bm_m10_machine_config(const bm_m10_config_t *configuration)
{
    bm_machine_config_t result = {
        .definition = &m10_definition,
        .configuration = {
            BM_M10_CONFIG_TYPE, BM_M10_CONFIG_VERSION,
            sizeof(*configuration), configuration
        }
    };
    return result;
}
