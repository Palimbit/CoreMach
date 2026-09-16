/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_TEXT_INPUT_H
#define BLUMACH_HEADLESS_TEXT_INPUT_H

#include <blumach/engine/input.h>
#include <blumach/engine/types.h>
#include <blumach/runtime/runtime.h>

#include <stdint.h>

typedef struct headless_text_key {
    bm_key_code_t key;
    int shifted;
} headless_text_key_t;

/* Decode one US-layout ASCII key from a command-line string. Backslash
 * escapes are accepted for newline, carriage return, tab, backspace and a
 * literal backslash. Returns one for a key, zero at the end, or -1 on error. */
int headless_text_decode_next(const char **cursor, headless_text_key_t *key);

bm_status_t headless_text_duration(const char *text, uint64_t key_ticks,
                                   uint64_t *duration);
bm_status_t headless_type_text(bm_session_t *session, const char *text,
                               uint64_t key_ticks);

#endif
