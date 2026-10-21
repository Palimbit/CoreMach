/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Manual Olivetti M10 firmware probe. The 32 KiB system ROM is supplied by
 * path and verified by SHA-256; it is never bundled. The probe models no
 * peripheral: memory follows the M10 service manual map and every I/O read
 * returns a fixed value. It reports what the firmware touches and why it
 * stopped. Output contains addresses, values and registers, never ROM bytes.
 * --self-test runs authored synthetic programs instead of firmware. */
#include <blumach/components/bus.h>
#include <blumach/components/cpu_i8085.h>
#include <blumach/components/hd44102.h>
#include <blumach/platforms/null_host.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLK_HZ 2457600U
#define ROM_SIZE 0x8000U
#define IO_LOG_CAPACITY 2000U
#define SOD_LOG_CAPACITY 64U
#define ANOMALY_LOG_CAPACITY 32U
#define PC_RING 64U

static const char m10_rom_sha256[] =
    "43F85E3AC5CC264BE3AD4C3B36DEA82381409A66381146CE06A78C22704B0DBC";

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-4).                                               */
/* ------------------------------------------------------------------ */
static uint32_t
rotr(uint32_t x, unsigned int n)
{
    return (x >> n) | (x << (32U - n));
}

static void
sha256_hex(const uint8_t *data, size_t size, char out[65])
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint64_t bits = (uint64_t) size * 8U;
    size_t padded = ((size + 9U + 63U) / 64U) * 64U;
    size_t block;
    unsigned int i;

    for (block = 0U; block < padded; block += 64U) {
        uint32_t w[64];
        uint32_t a, b, c, d, e, f, g, hh;
        for (i = 0U; i < 64U; ++i) {
            size_t index = block + i;
            uint8_t byte;
            if (index < size)
                byte = data[index];
            else if (index == size)
                byte = 0x80U;
            else if (index >= padded - 8U)
                byte = (uint8_t) (bits >> (8U * (unsigned int) (padded - 1U - index)));
            else
                byte = 0U;
            if ((i & 3U) == 0U)
                w[i / 4U] = 0U;
            w[i / 4U] |= (uint32_t) byte << (24U - 8U * (i & 3U));
        }
        for (i = 16U; i < 64U; ++i) {
            uint32_t s0 = rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
            uint32_t s1 = rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
            w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3];
        e = h[4]; f = h[5]; g = h[6]; hh = h[7];
        for (i = 0U; i < 64U; ++i) {
            uint32_t t1 = hh + (rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U)) +
                          ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U)) +
                          ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (i = 0U; i < 8U; ++i)
        snprintf(out + i * 8U, 9U, "%08" PRIX32, h[i]);
}

/* ------------------------------------------------------------------ */
/* Probe state.                                                        */
/* ------------------------------------------------------------------ */
typedef enum stop_reason {
    STOP_NONE = 0,
    STOP_TSTATE_LIMIT,
    STOP_HALTED_NO_SOURCE,
    STOP_CPU_STATUS,
    STOP_PC_UNPOPULATED,
    STOP_DEVICE_POLL,
    STOP_BREAKPOINT
} stop_reason_t;

static const char *const stop_names[] = {
    "none", "t-state limit", "halted without interrupt source",
    "CPU status (undocumented opcode, INTR or bus error)",
    "PC outside ROM and installed RAM", "device polling loop", "stop address"
};

typedef struct io_entry {
    uint16_t pc;
    uint8_t port;
    uint8_t write;
    uint8_t value;
    uint64_t t_state;
} io_entry_t;

typedef struct port_summary {
    uint64_t reads;
    uint64_t writes;
    uint16_t first_pc;
    uint16_t last_pc;
    uint8_t last_written;
    uint8_t seen;
} port_summary_t;

typedef struct sod_entry {
    uint16_t pc;
    uint8_t level;
    uint64_t t_state;
} sod_entry_t;

typedef struct anomaly {
    uint16_t pc;
    uint16_t address;
    uint8_t value;
} anomaly_t;

/* The M10 LCD: ten HD44102CH column drivers (components/video). Chip
 * select lines CS20-CS27 are 81C55 PA0-PA7 and CS28-CS29 PB0-PB1, Y7
 * enables F0h-FFh and A8 (port bit 0) is D/I, data when high (M10 service
 * manual 6.14, pp. 6-16/6-17). Active-high selection is inferred from the
 * firmware and matches MAME kyocera.cpp. Chip i covers columns
 * (i % 5) * 50 and rows (i / 5) * 32 after MAME kyocera_v.cpp; the
 * manual does not give the panel placement. Reads with several chips
 * selected are combined with OR as in MAME (secondary). */
#define LCD_CHIPS 10U
#define LCD_WIDTH 240U
#define LCD_HEIGHT 64U
#define LCD_MAX_CAPTURES 8U
/* T_BUSY <= 2/F_phi with F_phi half of the HD44103 oscillation (Hitachi
 * PDF page 171); about 430 kHz in the M10 (service manual 6.18), so at most
 * about 9.3 us = 22.9 CPU clocks. Approximate; --lcd-busy-t overrides. */
#define LCD_DEFAULT_BUSY_T 23U

typedef struct probe {
    /* Configuration. */
    const char *lcd_prefix;
    double lcd_capture_seconds[LCD_MAX_CAPTURES];
    unsigned int lcd_capture_count;
    unsigned int lcd_captures_done;
    const uint8_t *rom;
    unsigned int ram_kib;
    uint16_t ram_base;
    uint8_t ram_fill;
    uint8_t open_bus;
    int16_t io_value[256];
    uint8_t io_log_filtered;
    uint8_t io_log_port[256];
    uint64_t io_logged;
    uint64_t max_t_states;
    int32_t stop_at;
    uint32_t rst75_period_t;
    uint64_t poll_limit;
    /* Machine. */
    uint8_t memory[0x10000];
    bm_bus_t *bus;
    bm_cpu_t cpu;
    /* Observation. */
    uint16_t pc;
    uint64_t t_states;
    uint64_t instructions;
    uint64_t io_count;
    io_entry_t io_log[IO_LOG_CAPACITY];
    port_summary_t ports[256];
    uint64_t sod_count;
    sod_entry_t sod_log[SOD_LOG_CAPACITY];
    uint64_t rom_writes;
    anomaly_t rom_write_log[ANOMALY_LOG_CAPACITY];
    uint64_t unpopulated_accesses;
    anomaly_t unpopulated_log[ANOMALY_LOG_CAPACITY];
    uint32_t ram_write_low;
    uint32_t ram_write_high;
    uint64_t rst75_pulses;
    uint16_t pc_ring[PC_RING];
    uint64_t pc_ring_count;
    uint16_t poll_pc;
    uint8_t poll_port;
    uint64_t poll_count;
    stop_reason_t stop;
    bm_status_t cpu_status;
    /* LCD: 81C55 PA/PB latches select chips CS20-CS29. */
    uint64_t lcd_busy_t;
    uint8_t port_a;
    uint8_t port_b;
    bm_hd44102_t *lcd[LCD_CHIPS];
    uint64_t lcd_unselected;
    uint64_t lcd_rejected[4];
    uint64_t lcd_multi_reads;
    bm_hd44102_state_t lcd_final[LCD_CHIPS];
    uint8_t lcd_image[LCD_HEIGHT][LCD_WIDTH];
} probe_t;

static int
lcd_selected(const probe_t *probe, unsigned int chip)
{
    return chip < 8U ? (probe->port_a >> chip) & 1U :
                       (probe->port_b >> (chip - 8U)) & 1U;
}

/* Device access for ports F0h-FFh. Returns the value the bus carries for
 * a read; writes return 0. The time is the T-state of the I/O machine
 * cycle of IN/OUT, which starts seven clocks into the instruction. */
static uint8_t
lcd_access(probe_t *probe, uint8_t port, int write, uint8_t value)
{
    uint64_t now = probe->t_states + 7U;
    unsigned int index;
    unsigned int selected = 0U;
    uint8_t bus = 0U;

    if (write && port == 0xb9U)
        probe->port_a = value;
    if (write && port == 0xbaU)
        probe->port_b = value;
    if (port < 0xf0U)
        return 0U;
    for (index = 0U; index < LCD_CHIPS; ++index) {
        bm_hd44102_t *chip = probe->lcd[index];
        bm_hd44102_result_t result = BM_HD44102_ACCEPTED;
        uint8_t data = 0U;
        if (!lcd_selected(probe, index))
            continue;
        ++selected;
        if ((port & 1U) == 0U) {
            if (write)
                result = bm_hd44102_write_instruction(chip, value, now);
            else
                data = bm_hd44102_read_status(chip, now);
        } else {
            if (write)
                result = bm_hd44102_write_data(chip, value, now);
            else
                result = bm_hd44102_read_data(chip, now, &data);
        }
        ++probe->lcd_rejected[result];
        bus = (uint8_t) (bus | data);
    }
    if (selected == 0U) {
        ++probe->lcd_unselected;
        return probe->open_bus;
    }
    if (!write && selected > 1U)
        ++probe->lcd_multi_reads;
    return bus;
}

static void
lcd_render(const probe_t *probe, uint8_t image[LCD_HEIGHT][LCD_WIDTH])
{
    static uint8_t dots[BM_HD44102_ROWS][BM_HD44102_COLUMNS];
    unsigned int index;
    unsigned int column;
    unsigned int row;

    memset(image, 0, LCD_HEIGHT * LCD_WIDTH);
    for (index = 0U; index < LCD_CHIPS; ++index) {
        bm_hd44102_render(probe->lcd[index], dots);
        for (column = 0U; column < BM_HD44102_COLUMNS; ++column) {
            unsigned int x = (index % 5U) * BM_HD44102_COLUMNS + column;
            if (x >= LCD_WIDTH)
                continue;
            for (row = 0U; row < BM_HD44102_ROWS; ++row)
                image[(index / 5U) * BM_HD44102_ROWS + row][x] = dots[row][column];
        }
    }
}

/* Keep the final LCD state and image in the probe so they survive
 * teardown. */
static void
lcd_finish(probe_t *probe)
{
    unsigned int index;
    for (index = 0U; index < LCD_CHIPS; ++index)
        bm_hd44102_get_state(probe->lcd[index], &probe->lcd_final[index]);
    lcd_render(probe, probe->lcd_image);
}

static int
lcd_write_image(const probe_t *probe, const char *path)
{
    static uint8_t image[LCD_HEIGHT][LCD_WIDTH];
    FILE *file = fopen(path, "wb");
    unsigned int row;
    unsigned int column;

    if (file == NULL)
        return 0;
    lcd_render(probe, image);
    fprintf(file, "P5\n%u %u\n255\n", LCD_WIDTH, LCD_HEIGHT);
    for (row = 0U; row < LCD_HEIGHT; ++row)
        for (column = 0U; column < LCD_WIDTH; ++column)
            fputc(image[row][column] ? 0x20 : 0xc8, file);
    fclose(file);
    return 1;
}

static void
lcd_capture(probe_t *probe, const char *label)
{
    char path[512];

    if (probe->lcd_prefix == NULL)
        return;
    snprintf(path, sizeof(path), "%s-%s.pgm", probe->lcd_prefix, label);
    if (!lcd_write_image(probe, path))
        fprintf(stderr, "cannot write %s\n", path);
}

static void
record_anomaly(anomaly_t *log, uint64_t *count, uint16_t pc,
               uint16_t address, uint8_t value)
{
    if (*count < ANOMALY_LOG_CAPACITY)
        log[*count] = (anomaly_t) { pc, address, value };
    ++*count;
}

static void
reset_poll(probe_t *probe)
{
    probe->poll_count = 0U;
}

static bm_status_t
memory_access(void *context, bm_bus_transaction_t *txn)
{
    probe_t *probe = context;
    uint16_t address = (uint16_t) txn->address;

    if (address < ROM_SIZE) {
        if (txn->operation == BM_BUS_WRITE)
            record_anomaly(probe->rom_write_log, &probe->rom_writes, probe->pc,
                           address, (uint8_t) txn->value);
        else
            txn->value = probe->rom[address];
    } else if (address >= probe->ram_base) {
        if (txn->operation == BM_BUS_WRITE) {
            probe->memory[address] = (uint8_t) txn->value;
            if (address < probe->ram_write_low)
                probe->ram_write_low = address;
            if (address > probe->ram_write_high || probe->ram_write_high > 0xffffU)
                probe->ram_write_high = address;
            reset_poll(probe);
        } else {
            txn->value = probe->memory[address];
        }
    } else {
        /* Unpopulated: reads float to the configured value (assumption). */
        record_anomaly(probe->unpopulated_log, &probe->unpopulated_accesses,
                       probe->pc, address,
                       txn->operation == BM_BUS_WRITE ? (uint8_t) txn->value :
                                                        probe->open_bus);
        if (txn->operation != BM_BUS_WRITE)
            txn->value = probe->open_bus;
    }
    return BM_STATUS_OK;
}

static bm_status_t
io_access(void *context, bm_bus_transaction_t *txn)
{
    probe_t *probe = context;
    uint8_t port = (uint8_t) txn->address;
    port_summary_t *summary = &probe->ports[port];
    int write = txn->operation == BM_BUS_WRITE;

    {
        uint8_t device = lcd_access(probe, port, write, (uint8_t) txn->value);
        if (!write)
            txn->value = probe->io_value[port] >= 0 ? (uint8_t) probe->io_value[port] :
                         port >= 0xf0U ? device : probe->open_bus;
    }
    if (!probe->io_log_filtered || probe->io_log_port[port]) {
        if (probe->io_logged < IO_LOG_CAPACITY)
            probe->io_log[probe->io_logged] = (io_entry_t) {
                probe->pc, port, (uint8_t) write, (uint8_t) txn->value,
                probe->t_states
            };
        ++probe->io_logged;
    }
    ++probe->io_count;
    if (!summary->seen)
        summary->first_pc = probe->pc;
    summary->seen = 1U;
    summary->last_pc = probe->pc;
    if (write) {
        ++summary->writes;
        summary->last_written = (uint8_t) txn->value;
        reset_poll(probe);
    } else {
        ++summary->reads;
        if ((probe->poll_count != 0U) && (probe->poll_pc == probe->pc) &&
            (probe->poll_port == port)) {
            ++probe->poll_count;
        } else {
            probe->poll_pc = probe->pc;
            probe->poll_port = port;
            probe->poll_count = 1U;
        }
    }
    return BM_STATUS_OK;
}

static void
sod_changed(void *context, int level, uint32_t half_clocks)
{
    probe_t *probe = context;
    (void) half_clocks;
    if (probe->sod_count < SOD_LOG_CAPACITY)
        probe->sod_log[probe->sod_count] = (sod_entry_t) {
            probe->pc, (uint8_t) (level != 0), probe->t_states
        };
    ++probe->sod_count;
}

static int
pc_populated(const probe_t *probe, uint16_t pc)
{
    return (pc < ROM_SIZE) || (pc >= probe->ram_base);
}

static void
pulse_rst75(probe_t *probe)
{
    probe->cpu.ops.signal(probe->cpu.context, BM_I8085_SIGNAL_RST75, 1);
    probe->cpu.ops.signal(probe->cpu.context, BM_I8085_SIGNAL_RST75, 0);
    ++probe->rst75_pulses;
}

static int
probe_setup(probe_t *probe, const bm_host_services_t *host)
{
    bm_i8085_config_t config;

    probe->ram_base = (uint16_t) (0x10000U - probe->ram_kib * 1024U);
    memset(probe->memory + probe->ram_base, probe->ram_fill,
           0x10000U - probe->ram_base);
    probe->ram_write_low = 0x10000U;
    probe->ram_write_high = 0x10000U;
    if ((bm_bus_create(host, 2U, &probe->bus) != BM_STATUS_OK) ||
        (bm_bus_map(probe->bus, BM_ADDRESS_MEMORY, 0U, 0xffffU, memory_access,
                    probe) != BM_STATUS_OK) ||
        (bm_bus_map(probe->bus, BM_ADDRESS_IO, 0U, 0xffU, io_access, probe) !=
         BM_STATUS_OK))
        return 0;
    {
        unsigned int index;
        for (index = 0U; index < LCD_CHIPS; ++index) {
            if (bm_hd44102_create(host, probe->lcd_busy_t, &probe->lcd[index]) !=
                BM_STATUS_OK)
                return 0;
            bm_hd44102_set_reset(probe->lcd[index], 1);
            bm_hd44102_set_reset(probe->lcd[index], 0);
        }
    }
    config = (bm_i8085_config_t) {
        .model = BM_I8085_OKI_MSM80C85A,
        .frequency_hz = CLK_HZ,
        .bus = probe->bus,
        .sod = sod_changed,
        .sod_context = probe
    };
    return bm_i8085_create(host, &config, &probe->cpu) == BM_STATUS_OK;
}

static void
probe_teardown(probe_t *probe)
{
    if (probe->cpu.ops.destroy != NULL)
        probe->cpu.ops.destroy(probe->cpu.context);
    bm_bus_destroy(probe->bus);
    {
        unsigned int index;
        for (index = 0U; index < LCD_CHIPS; ++index)
            bm_hd44102_destroy(probe->lcd[index]);
    }
}

/* Run until a stop condition. RST 7.5 pulses, when enabled, are injected
 * at boundaries once the accumulated T-states reach each period. */
static void
probe_run(probe_t *probe)
{
    uint64_t next_pulse = probe->rst75_period_t;

    for (;;) {
        bm_i8085_arch_state_t state;
        uint64_t cycles = 0U;
        bm_status_t status;

        while ((probe->lcd_captures_done < probe->lcd_capture_count) &&
               ((double) probe->t_states >=
                probe->lcd_capture_seconds[probe->lcd_captures_done] * CLK_HZ)) {
            char label[32];
            snprintf(label, sizeof(label), "%.3fs",
                     probe->lcd_capture_seconds[probe->lcd_captures_done]);
            lcd_capture(probe, label);
            ++probe->lcd_captures_done;
        }
        if (probe->t_states >= probe->max_t_states) {
            probe->stop = STOP_TSTATE_LIMIT;
            return;
        }
        if (probe->rst75_period_t != 0U && probe->t_states >= next_pulse) {
            pulse_rst75(probe);
            next_pulse += probe->rst75_period_t;
        }
        bm_i8085_get_arch_state(&probe->cpu, &state);
        probe->pc = state.pc;
        if (!state.halted) {
            if ((probe->stop_at >= 0) && (state.pc == (uint16_t) probe->stop_at)) {
                probe->stop = STOP_BREAKPOINT;
                return;
            }
            if (!pc_populated(probe, state.pc)) {
                probe->stop = STOP_PC_UNPOPULATED;
                return;
            }
            probe->pc_ring[probe->pc_ring_count % PC_RING] = state.pc;
            ++probe->pc_ring_count;
        }
        status = bm_i8085_step_clocked(probe->cpu.context, 0U, &cycles);
        if (status == BM_STATUS_IDLE) {
            if (probe->rst75_period_t == 0U) {
                probe->stop = STOP_HALTED_NO_SOURCE;
                return;
            }
            probe->t_states = next_pulse;
            continue;
        }
        if (status != BM_STATUS_OK) {
            probe->cpu_status = status;
            probe->stop = STOP_CPU_STATUS;
            return;
        }
        probe->t_states += cycles;
        ++probe->instructions;
        if (probe->poll_count >= probe->poll_limit) {
            probe->stop = STOP_DEVICE_POLL;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Report.                                                             */
/* ------------------------------------------------------------------ */
static void
print_report(FILE *out, const probe_t *probe, const char *rom_hash)
{
    bm_i8085_arch_state_t s;
    unsigned int index;
    uint8_t enable;
    uint8_t rim;
    uint64_t shown;

    bm_i8085_get_arch_state(&probe->cpu, &s);
    enable = s.trap_saved_valid ? s.trap_saved_interrupt_enable : s.interrupt_enable;
    /* SID, RST 6.5 and RST 5.5 are not driven by this probe. */
    rim = (uint8_t) ((s.rst75_pending << 6U) | (enable << 3U) | s.interrupt_mask);

    fprintf(out, "Olivetti M10 firmware probe\n");
    fprintf(out, "rom sha256: %s\n", rom_hash);
    fprintf(out, "cpu: OKI MSM80C85A model, %u Hz CLK, CoreMach i8085 component\n", CLK_HZ);
    fprintf(out, "ram: %u KiB at %04X-FFFF, initial fill %02Xh\n",
            probe->ram_kib, probe->ram_base, probe->ram_fill);
    fprintf(out, "ASSUMPTION: unpopulated memory and unmapped I/O reads return %02Xh "
                 "(floating bus value not documented)\n", probe->open_bus);
    for (index = 0U; index < 256U; ++index)
        if (probe->io_value[index] >= 0)
            fprintf(out, "io override: port %02Xh reads %02Xh\n", index,
                    (unsigned int) probe->io_value[index]);
    fprintf(out, "interrupts: %s\n", probe->rst75_period_t ?
            "RST 7.5 pulse enabled" : "none driven (no RST 7.5, TRAP, INTR)");
    fprintf(out, "no peripheral is modelled; I/O values are fixed\n\n");

    fprintf(out, "stop: %s\n", stop_names[probe->stop]);
    if (probe->stop == STOP_CPU_STATUS)
        fprintf(out, "cpu status: %d at PC %04X\n", (int) probe->cpu_status, probe->pc);
    if (probe->stop == STOP_DEVICE_POLL)
        fprintf(out, "polling: PC %04X read port %02Xh %" PRIu64
                " times with no I/O or RAM write\n",
                probe->poll_pc, probe->poll_port, probe->poll_count);
    fprintf(out, "instructions: %" PRIu64 ", T-states: %" PRIu64
            ", virtual time: %.6f s\n", probe->instructions, probe->t_states,
            (double) probe->t_states / CLK_HZ);
    if (probe->rst75_period_t)
        fprintf(out, "RST 7.5 pulses: %" PRIu64 "\n", probe->rst75_pulses);

    fprintf(out, "\nregisters: A=%02X F=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X "
            "SP=%04X PC=%04X halted=%u\n", s.a, s.f, s.b, s.c, s.d, s.e, s.h, s.l,
            s.sp, s.pc, s.halted);
    fprintf(out, "interrupts: IE=%u (EI delay %u) masks M7.5=%u M6.5=%u M5.5=%u "
            "RST7.5 pending=%u TRAP pending=%u SOD=%u\n",
            s.interrupt_enable, s.interrupt_enable_delay,
            (s.interrupt_mask >> 2U) & 1U, (s.interrupt_mask >> 1U) & 1U,
            s.interrupt_mask & 1U, s.rst75_pending, s.trap_pending, s.sod);
    fprintf(out, "RIM would read: %02Xh (SID, I6.5 and I5.5 not driven)%s\n", rim,
            s.trap_saved_valid ? ", IE bit from before TRAP" : "");
    fprintf(out, "stack words from SP:");
    for (index = 0U; index < 4U; ++index) {
        uint16_t at = (uint16_t) (s.sp + 2U * index);
        if (at >= probe->ram_base && (uint16_t) (at + 1U) >= probe->ram_base)
            fprintf(out, " %04X", (unsigned int) (probe->memory[at] |
                    (probe->memory[(uint16_t) (at + 1U)] << 8U)));
        else
            fprintf(out, " ----");
    }
    fprintf(out, "\n");

    fprintf(out, "\nSOD changes: %" PRIu64 "\n", probe->sod_count);
    shown = probe->sod_count < SOD_LOG_CAPACITY ? probe->sod_count : SOD_LOG_CAPACITY;
    for (index = 0U; index < shown; ++index)
        fprintf(out, "  PC %04X level %u at T %" PRIu64 "\n", probe->sod_log[index].pc,
                probe->sod_log[index].level, probe->sod_log[index].t_state);

    fprintf(out, "\nRAM writes: ");
    if (probe->ram_write_low > 0xffffU)
        fprintf(out, "none\n");
    else
        fprintf(out, "%04X-%04X\n", probe->ram_write_low, probe->ram_write_high);
    fprintf(out, "ROM writes: %" PRIu64 "\n", probe->rom_writes);
    shown = probe->rom_writes < ANOMALY_LOG_CAPACITY ? probe->rom_writes : ANOMALY_LOG_CAPACITY;
    for (index = 0U; index < shown; ++index)
        fprintf(out, "  PC %04X wrote %02X to %04X\n", probe->rom_write_log[index].pc,
                probe->rom_write_log[index].value, probe->rom_write_log[index].address);
    fprintf(out, "unpopulated memory accesses: %" PRIu64 "\n", probe->unpopulated_accesses);
    shown = probe->unpopulated_accesses < ANOMALY_LOG_CAPACITY ?
            probe->unpopulated_accesses : ANOMALY_LOG_CAPACITY;
    for (index = 0U; index < shown; ++index)
        fprintf(out, "  PC %04X address %04X value %02X\n", probe->unpopulated_log[index].pc,
                probe->unpopulated_log[index].address, probe->unpopulated_log[index].value);

    fprintf(out, "\nLCD: ten HD44102CH (Hitachi 1983 data book), BUSY %" PRIu64
            " T-states, layout per MAME:\n", probe->lcd_busy_t);
    for (index = 0U; index < LCD_CHIPS; ++index) {
        const bm_hd44102_state_t *c = &probe->lcd_final[index];
        fprintf(out, "  chip %u (CS2%u) %s start page %u, %s, X=%u Y=%u\n", index, index,
                c->display_on ? "ON " : "OFF", c->start_page, c->up ? "up" : "down",
                c->x, c->y);
    }
    fprintf(out, "  accepted %" PRIu64 ", rejected busy %" PRIu64 ", reset %" PRIu64
            ", undefined code %" PRIu64 "\n", probe->lcd_rejected[0], probe->lcd_rejected[1],
            probe->lcd_rejected[2], probe->lcd_rejected[3]);
    fprintf(out, "  accesses with no chip selected: %" PRIu64 ", reads with several chips: %"
            PRIu64 "\n", probe->lcd_unselected, probe->lcd_multi_reads);
    fprintf(out, "\nI/O ports (reads, writes, first PC, last PC, last written):\n");
    for (index = 0U; index < 256U; ++index) {
        const port_summary_t *p = &probe->ports[index];
        if (!p->seen)
            continue;
        fprintf(out, "  %02Xh  r=%" PRIu64 " w=%" PRIu64 " first=%04X last=%04X",
                index, p->reads, p->writes, p->first_pc, p->last_pc);
        if (p->writes)
            fprintf(out, " lastw=%02X", p->last_written);
        fprintf(out, "\n");
    }

    fprintf(out, "\nI/O accesses: %" PRIu64 "; logged %s: %" PRIu64 " (first %u in order)\n",
            probe->io_count, probe->io_log_filtered ? "for selected ports" : "all ports",
            probe->io_logged, IO_LOG_CAPACITY);
    shown = probe->io_logged < IO_LOG_CAPACITY ? probe->io_logged : IO_LOG_CAPACITY;
    for (index = 0U; index < shown; ++index) {
        const io_entry_t *e = &probe->io_log[index];
        fprintf(out, "  T %10" PRIu64 "  PC %04X  %s %02Xh  %02X\n", e->t_state, e->pc,
                e->write ? "OUT" : "IN ", e->port, e->value);
    }

    fprintf(out, "\nlast PCs (oldest first):\n");
    shown = probe->pc_ring_count < PC_RING ? probe->pc_ring_count : PC_RING;
    for (index = 0U; index < shown; ++index) {
        uint64_t slot = (probe->pc_ring_count - shown + index) % PC_RING;
        fprintf(out, "%s%04X", (index % 16U) == 0U ? "  " : " ", probe->pc_ring[slot]);
        if ((index % 16U) == 15U || index + 1U == shown)
            fprintf(out, "\n");
    }
}

/* ------------------------------------------------------------------ */
/* Self-test with authored programs.                                   */
/* ------------------------------------------------------------------ */
static int
self_case(const uint8_t *program, size_t size, stop_reason_t expected,
          probe_t *result)
{
    static uint8_t rom[ROM_SIZE];
    bm_host_services_t host = bm_null_host_services();
    probe_t *probe = calloc(1U, sizeof(*probe));
    unsigned int index;
    int ok;

    if (probe == NULL)
        return 0;
    memset(rom, 0, sizeof(rom));
    memcpy(rom, program, size);
    probe->rom = rom;
    probe->ram_kib = 32U;
    probe->open_bus = 0xffU;
    probe->max_t_states = 100000U;
    probe->stop_at = -1;
    probe->poll_limit = 1000U;
    probe->lcd_busy_t = LCD_DEFAULT_BUSY_T;
    for (index = 0U; index < 256U; ++index)
        probe->io_value[index] = -1;
    ok = probe_setup(probe, &host);
    if (ok) {
        probe_run(probe);
        ok = probe->stop == expected;
        lcd_finish(probe);
        if (result != NULL)
            *result = *probe;
        probe_teardown(probe);
    }
    free(probe);
    return ok;
}

static int
self_test(void)
{
    static const uint8_t poll[] = {
        0x31, 0xc0, 0xfc,       /* LXI SP,FCC0h */
        0xdb, 0xf0,             /* 0003 IN F0h */
        0x07,                   /* RLC */
        0xda, 0x03, 0x00,       /* JC 0003h: wait while bit 7 is set */
        0x76
    };
    static const uint8_t sod_halt[] = {
        0x3e, 0xc0, 0x30,       /* MVI A,C0h; SIM: SOD=1 */
        0x3e, 0x40, 0x30,       /* MVI A,40h; SIM: SOD=0 */
        0x76                    /* HLT */
    };
    static const uint8_t wander[] = { 0xc3, 0x00, 0x40 }; /* JMP 4000h: NOPs */
    static const uint8_t jump_hole[] = { 0xc3, 0x00, 0x90 };
    static const uint8_t undocumented[] = { 0x00, 0x08 };
    static const uint8_t rom_write[] = { 0x32, 0x00, 0x10, 0x76 };
    probe_t *result = calloc(1U, sizeof(*result));
    int ok = result != NULL;

    ok = ok && self_case(poll, sizeof(poll), STOP_DEVICE_POLL, result) &&
         result->poll_port == 0xf0U && result->poll_pc == 0x0003U;
    if (!ok)
        puts("self-test: polling case failed");
    ok = ok && self_case(sod_halt, sizeof(sod_halt), STOP_HALTED_NO_SOURCE, result) &&
         result->sod_count == 2U && result->sod_log[0].pc == 0x0002U &&
         result->sod_log[1].level == 0U;
    if (!ok)
        puts("self-test: SOD/HALT case failed");
    ok = ok && self_case(wander, sizeof(wander), STOP_TSTATE_LIMIT, NULL) &&
         self_case(jump_hole, sizeof(jump_hole), STOP_TSTATE_LIMIT, NULL);
    if (!ok)
        puts("self-test: budget case failed");
    ok = ok && self_case(undocumented, sizeof(undocumented), STOP_CPU_STATUS, result) &&
         result->pc == 0x0001U;
    if (!ok)
        puts("self-test: CPU status case failed");
    ok = ok && self_case(rom_write, sizeof(rom_write), STOP_HALTED_NO_SOURCE, result) &&
         result->rom_writes == 1U && result->rom_write_log[0].address == 0x1000U;
    if (!ok)
        puts("self-test: ROM write case failed");
    {
        static const uint8_t lcd[] = {
            0x31, 0xc0, 0xfc,       /* LXI SP,FCC0h */
            0x3e, 0x01, 0xd3, 0xb9, /* PA=01h: CS20 */
            0x3e, 0xfc, 0xd3, 0xba, /* PB=FCh */
            0xcd, 0x40, 0x00, 0x3e, 0x39, 0xd3, 0xfe, /* display ON */
            0xcd, 0x40, 0x00, 0x3e, 0x7e, 0xd3, 0xfe, /* start page 1 */
            0xcd, 0x40, 0x00, 0x3e, 0x45, 0xd3, 0xfe, /* X page 1, Y 5 */
            0xcd, 0x40, 0x00, 0x3e, 0x81, 0xd3, 0xff, /* data 81h */
            0xcd, 0x40, 0x00, 0x3e, 0x45, 0xd3, 0xfe, /* X page 1, Y 5 */
            0xcd, 0x40, 0x00, 0xdb, 0xff,             /* dummy read */
            0xcd, 0x40, 0x00, 0xdb, 0xff, 0x47,       /* read 81h; MOV B,A */
            0xcd, 0x40, 0x00, 0x76,                   /* HLT */
            0x00, 0x00, 0x00,
            0xdb, 0xfe, 0x17, 0xda, 0x40, 0x00, 0xc9  /* 0040h: wait while BUSY */
        };
        ok = ok && self_case(lcd, sizeof(lcd), STOP_HALTED_NO_SOURCE, result) &&
             result->lcd_final[0].display_on && result->lcd_final[0].start_page == 1U &&
             result->lcd_final[0].ram[1][5] == 0x81U && result->lcd_final[0].y == 7U &&
             !result->lcd_final[1].display_on && result->lcd_unselected == 0U &&
             result->lcd_rejected[1] == 0U;
        /* With start page 1, RAM page 1 is shown in the top eight rows. */
        ok = ok && result->lcd_image[0][5] == 1U && result->lcd_image[7][5] == 1U &&
             result->lcd_image[1][5] == 0U && result->lcd_image[8][5] == 0U;
        if (!ok)
            puts("self-test: LCD case failed");
    }
    free(result);
    /* A 32 KiB map has RAM from 8000h, so 9000h is populated; check a hole
     * with 8 KiB instead. */
    {
        static uint8_t rom[ROM_SIZE];
        bm_host_services_t host = bm_null_host_services();
        probe_t *probe = calloc(1U, sizeof(*probe));
        unsigned int index;
        ok = ok && probe != NULL;
        if (probe != NULL) {
            memcpy(rom, jump_hole, sizeof(jump_hole));
            probe->rom = rom;
            probe->ram_kib = 8U;
            probe->open_bus = 0xffU;
            probe->max_t_states = 100000U;
            probe->stop_at = -1;
            probe->poll_limit = 1000U;
            probe->lcd_busy_t = LCD_DEFAULT_BUSY_T;
            for (index = 0U; index < 256U; ++index)
                probe->io_value[index] = -1;
            ok = ok && probe_setup(probe, &host);
            probe_run(probe);
            ok = ok && probe->stop == STOP_PC_UNPOPULATED && probe->pc == 0x9000U;
            probe_teardown(probe);
            free(probe);
        }
    }
    {
        char hex[65];
        sha256_hex((const uint8_t *) "abc", 3U, hex);
        ok = ok && strcmp(hex, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") == 0;
    }
    puts(ok ? "self-test passed" : "self-test FAILED");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* Command line.                                                       */
/* ------------------------------------------------------------------ */
static int
parse_hex(const char *text, unsigned long limit, unsigned long *value)
{
    char *end = NULL;
    *value = strtoul(text, &end, 16);
    return (end != text) && (*end == '\0') && (*value <= limit);
}

static void
usage(const char *name)
{
    fprintf(stderr,
            "usage: %s <m10-system-first32k.bin> [options]\n"
            "       %s --self-test\n"
            "  --ram-kb 8|16|24|32     installed RAM (default 32)\n"
            "  --ram-fill HH           initial RAM contents (default 00)\n"
            "  --open-bus HH           unpopulated/unmapped read value (default FF)\n"
            "  --io PP=HH              fixed read value for one port\n"
            "  --max-tstates N         T-state budget (default 50000000)\n"
            "  --stop-at AAAA          stop before executing this address\n"
            "  --rst75-ms N            pulse RST 7.5 every N ms\n"
            "  --poll-limit N          device polling threshold (default 100000)\n"
            "  --io-log-ports P,P,...   log only these hex ports in order\n"
            "  --report FILE           also write the report to FILE\n"
            "  --lcd PREFIX            write LCD captures as PREFIX-<label>.pgm\n"
            "  --lcd-at SECONDS        add an increasing capture time (up to 8)\n"
            "  --lcd-busy-t N          HD44102 BUSY period in CPU T-states (default 23)\n",
            name, name);
}

int
main(int argc, char **argv)
{
    bm_host_services_t host = bm_null_host_services();
    probe_t *probe;
    uint8_t *rom;
    const char *report_path = NULL;
    char hash[65];
    FILE *file;
    size_t count;
    int index;

    if ((argc == 2) && (strcmp(argv[1], "--self-test") == 0))
        return self_test();
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    probe = calloc(1U, sizeof(*probe));
    rom = malloc(ROM_SIZE + 1U);
    if ((probe == NULL) || (rom == NULL))
        return 2;
    probe->ram_kib = 32U;
    probe->ram_fill = 0x00U;
    probe->open_bus = 0xffU;
    probe->max_t_states = 50000000U;
    probe->stop_at = -1;
    probe->poll_limit = 100000U;
    probe->lcd_busy_t = LCD_DEFAULT_BUSY_T;
    for (index = 0; index < 256; ++index)
        probe->io_value[index] = -1;
    for (index = 2; index < argc; ++index) {
        unsigned long value = 0U;
        const char *arg = argv[index];
        const char *next = index + 1 < argc ? argv[index + 1] : NULL;
        if ((strcmp(arg, "--ram-kb") == 0) && next) {
            value = strtoul(next, NULL, 10);
            if (value != 8U && value != 16U && value != 24U && value != 32U) {
                usage(argv[0]);
                return 2;
            }
            probe->ram_kib = (unsigned int) value;
        } else if ((strcmp(arg, "--ram-fill") == 0) && next && parse_hex(next, 0xffU, &value)) {
            probe->ram_fill = (uint8_t) value;
        } else if ((strcmp(arg, "--open-bus") == 0) && next && parse_hex(next, 0xffU, &value)) {
            probe->open_bus = (uint8_t) value;
        } else if ((strcmp(arg, "--io") == 0) && next && strchr(next, '=')) {
            char port_text[8] = { 0 };
            unsigned long port = 0U;
            size_t length = (size_t) (strchr(next, '=') - next);
            if (length >= sizeof(port_text) ||
                !parse_hex(memcpy(port_text, next, length), 0xffU, &port) ||
                !parse_hex(strchr(next, '=') + 1, 0xffU, &value)) {
                usage(argv[0]);
                return 2;
            }
            probe->io_value[port] = (int16_t) value;
        } else if ((strcmp(arg, "--max-tstates") == 0) && next) {
            probe->max_t_states = strtoull(next, NULL, 10);
        } else if ((strcmp(arg, "--stop-at") == 0) && next && parse_hex(next, 0xffffU, &value)) {
            probe->stop_at = (int32_t) value;
        } else if ((strcmp(arg, "--rst75-ms") == 0) && next) {
            probe->rst75_period_t = (uint32_t) (strtoul(next, NULL, 10) * (CLK_HZ / 1000U));
        } else if ((strcmp(arg, "--poll-limit") == 0) && next) {
            probe->poll_limit = strtoull(next, NULL, 10);
        } else if ((strcmp(arg, "--io-log-ports") == 0) && next) {
            /* Comma-separated hexadecimal ports, for example FE,FF,B9,BA. */
            const char *cursor = next;
            probe->io_log_filtered = 1U;
            while (*cursor != '\0') {
                char *end = NULL;
                unsigned long port = strtoul(cursor, &end, 16);
                if ((end == cursor) || (port > 0xffU) || ((*end != ',') && (*end != '\0'))) {
                    usage(argv[0]);
                    return 2;
                }
                probe->io_log_port[port] = 1U;
                cursor = *end == ',' ? end + 1 : end;
            }
        } else if ((strcmp(arg, "--report") == 0) && next) {
            report_path = next;
        } else if ((strcmp(arg, "--lcd-busy-t") == 0) && next) {
            probe->lcd_busy_t = strtoull(next, NULL, 10);
        } else if ((strcmp(arg, "--lcd") == 0) && next) {
            probe->lcd_prefix = next;
        } else if ((strcmp(arg, "--lcd-at") == 0) && next &&
                   (probe->lcd_capture_count < LCD_MAX_CAPTURES)) {
            double seconds = strtod(next, NULL);
            if ((probe->lcd_capture_count != 0U) &&
                (seconds <= probe->lcd_capture_seconds[probe->lcd_capture_count - 1U])) {
                usage(argv[0]);
                return 2;
            }
            probe->lcd_capture_seconds[probe->lcd_capture_count++] = seconds;
        } else {
            usage(argv[0]);
            return 2;
        }
        ++index;
    }

    file = fopen(argv[1], "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    count = fread(rom, 1U, ROM_SIZE + 1U, file);
    fclose(file);
    if (count != ROM_SIZE) {
        fprintf(stderr, "expected exactly %u bytes, read %zu\n", ROM_SIZE, count);
        return 2;
    }
    sha256_hex(rom, ROM_SIZE, hash);
    if (strcmp(hash, m10_rom_sha256) != 0) {
        fprintf(stderr, "unexpected ROM SHA-256 %s\n", hash);
        return 2;
    }
    probe->rom = rom;
    if (!probe_setup(probe, &host)) {
        fprintf(stderr, "probe setup failed\n");
        return 2;
    }
    probe_run(probe);
    lcd_capture(probe, "final");
    lcd_finish(probe);
    print_report(stdout, probe, hash);
    if (report_path != NULL) {
        FILE *report = fopen(report_path, "w");
        if (report == NULL) {
            fprintf(stderr, "cannot write %s\n", report_path);
        } else {
            print_report(report, probe, hash);
            fclose(report);
        }
    }
    probe_teardown(probe);
    free(rom);
    free(probe);
    return 0;
}
