/*
 * BluMach, a preservation-focused fork of 86Box.
 *
 * Initial NEC V40 integrated-peripheral model.
 *
 * Author: rtzor
 * Project: BluMach
 */

#ifndef EMU_NEC_V40_H
#define EMU_NEC_V40_H

#include <stdint.h>

#include <86box/device.h>

extern const device_t nec_v40_device;

extern uint8_t nec_v40_reg_read(uint8_t reg);

#endif /* EMU_NEC_V40_H */
