/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_HD44102_H
#define BLUMACH_COMPONENTS_HD44102_H

#include <stdint.h>
#include <blumach/engine/host.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hitachi HD44102CH dot-matrix LCD column driver, one chip per instance.
 * Primary source: Hitachi 1983 MOS LSI Data Book, LCD Driver LSI,
 * HD44102CH (PDF pages 166-175 of the bitsavers scan). The chip is bus
 * agnostic: the board decodes chip select and D/I and passes its own time
 * base, in any monotonic tick unit, to every access.
 *
 * Not modelled: the 4-bit bus mode (BS high), LCD drive waveforms, the
 * Z address counter timing and the HD44103 common driver. */

#define BM_HD44102_COLUMNS 50U
#define BM_HD44102_PAGES 4U
#define BM_HD44102_ROWS 32U

/* Status Read, PDF page 175. Bits 3-0 always read 0. */
#define BM_HD44102_STATUS_BUSY 0x80U
#define BM_HD44102_STATUS_UP 0x40U
#define BM_HD44102_STATUS_OFF 0x20U
#define BM_HD44102_STATUS_RESET 0x10U

typedef struct bm_hd44102 bm_hd44102_t;

/* Accepted means the chip executed the access. Rejected accesses arrive
 * while BUSY or RESET is set ("none of instructions (1)-(5) are accepted",
 * PDF page 175) or carry an instruction code the data book does not
 * define; they change nothing. */
typedef enum bm_hd44102_result {
    BM_HD44102_ACCEPTED = 0,
    BM_HD44102_REJECTED_BUSY = 1,
    BM_HD44102_REJECTED_RESET = 2,
    BM_HD44102_REJECTED_UNDEFINED = 3
} bm_hd44102_result_t;

typedef struct bm_hd44102_state {
    uint8_t ram[BM_HD44102_PAGES][BM_HD44102_COLUMNS];
    uint8_t x;              /* page, 0-3 */
    uint8_t y;              /* column, 0-49 */
    uint8_t up;
    uint8_t display_on;
    uint8_t start_page;
    uint8_t output_register;
    uint8_t reset_asserted;
} bm_hd44102_state_t;

/* busy_ticks is the BUSY period after each accepted instruction, display
 * data read or write, in the owner's tick unit. The data book gives
 * 1/F_phi <= T_BUSY <= 2/F_phi, F_phi being half of the HD44103
 * oscillation frequency (PDF page 171). Zero disables BUSY. */
bm_status_t bm_hd44102_create(const bm_host_services_t *host,
                              uint64_t busy_ticks, bm_hd44102_t **out_chip);
void bm_hd44102_destroy(bm_hd44102_t *chip);

/* RST pin, active low on the chip; asserted = 1 means held in reset. While
 * held, the display is off, the Y counter is in UP mode and status reports
 * BUSY and RESET; after release the chip keeps display OFF and UP mode
 * until instructed (PDF pages 166-167, 175). X, Y, the start page and the
 * display RAM are not specified by the data book and are left unchanged. */
void bm_hd44102_set_reset(bm_hd44102_t *chip, int asserted);

uint8_t bm_hd44102_read_status(const bm_hd44102_t *chip, uint64_t now);
bm_hd44102_result_t bm_hd44102_write_instruction(bm_hd44102_t *chip,
                                                 uint8_t value, uint64_t now);
bm_hd44102_result_t bm_hd44102_write_data(bm_hd44102_t *chip, uint8_t value,
                                          uint64_t now);
/* Returns the output register, then loads it from RAM at the current
 * address and steps Y. The first read after setting an address is
 * therefore a dummy read (PDF page 169). A rejected read leaves the chip
 * unchanged and stores 0 in *value. */
bm_hd44102_result_t bm_hd44102_read_data(bm_hd44102_t *chip, uint64_t now,
                                         uint8_t *value);

/* One byte per dot, 1 = dot on, rows from the top of the chip's 32 lines
 * starting at the display start page; all zero while the display is OFF
 * (PDF pages 170 and 174). */
void bm_hd44102_render(const bm_hd44102_t *chip,
                       uint8_t dots[BM_HD44102_ROWS][BM_HD44102_COLUMNS]);
void bm_hd44102_get_state(const bm_hd44102_t *chip,
                          bm_hd44102_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif
