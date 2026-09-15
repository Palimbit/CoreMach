/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_TYPES_H
#define BLUMACH_ENGINE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t bm_tick_t;

typedef enum bm_status {
    BM_STATUS_OK = 0,
    BM_STATUS_IDLE = 1,
    BM_STATUS_INVALID_ARGUMENT = -1,
    BM_STATUS_OUT_OF_MEMORY = -2,
    BM_STATUS_INVALID_STATE = -3,
    BM_STATUS_CAPACITY_EXCEEDED = -4,
    BM_STATUS_UNMAPPED = -5,
    BM_STATUS_DEVICE_ERROR = -6
} bm_status_t;

#ifdef __cplusplus
}
#endif

#endif
