/*
 * Private wiring shared by the discrete Headland chipset used on the
 * Olivetti PCS 386SX and Triumph-Adler Dario 386SX/P45.
 *
 * This is deliberately not a register definition.  The exact division of
 * labour between HT101SX and HT113 is not publicly documented, so the board
 * context only carries links and signals which are visible at package level.
 */
#ifndef EMU_HEADLAND_PCS386SX_H
#define EMU_HEADLAND_PCS386SX_H

#include <stdint.h>

typedef struct headland_pcs386sx_t {
    void *ht101sx;
    void *ht113;
    void *gc102pc;

    uint8_t cold_start;
    uint8_t parity_error;
    uint8_t parity_nmi_connected;
    uint8_t references;
} headland_pcs386sx_t;

void headland_pcs386sx_retain(headland_pcs386sx_t *board);
void headland_pcs386sx_release(headland_pcs386sx_t *board);

#endif
