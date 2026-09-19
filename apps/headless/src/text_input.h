/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_HEADLESS_TEXT_INPUT_H
#define BLUMACH_HEADLESS_TEXT_INPUT_H

#include <blumach/engine/input.h>
#include <blumach/engine/types.h>
#include <blumach/runtime/runtime.h>

#include <stddef.h>
#include <stdint.h>

typedef struct headless_text_key {
    bm_key_code_t key;
    int shifted;
} headless_text_key_t;

typedef struct headless_text_action {
    const char *text;
    uint64_t at;
} headless_text_action_t;

typedef struct headless_key_action {
    bm_key_code_t key;
    uint64_t at;
    int pressed;
} headless_key_action_t;

typedef bm_status_t (*headless_timed_action_fn)(void *context);

/* Decode one US-layout ASCII key from a command-line string. Backslash
 * escapes are accepted for newline, carriage return, tab, backspace and a
 * literal backslash. Returns one for a key, zero at the end, or -1 on error. */
int headless_text_decode_next(const char **cursor, headless_text_key_t *key);
int headless_key_code_from_name(const char *name, bm_key_code_t *key);

bm_status_t headless_text_duration(const char *text, uint64_t key_ticks,
                                   uint64_t *duration);
bm_status_t headless_type_text(bm_session_t *session, const char *text,
                               uint64_t key_ticks);
bm_status_t headless_text_schedule_validate(
    const headless_text_action_t *actions, size_t action_count,
    uint64_t key_ticks, uint64_t total_ticks);
bm_status_t headless_run_text_schedule(
    bm_session_t *session, const headless_text_action_t *actions,
    size_t action_count, uint64_t key_ticks);
bm_status_t headless_input_schedule_validate(
    const headless_text_action_t *text_actions, size_t text_action_count,
    const headless_key_action_t *key_actions, size_t key_action_count,
    uint64_t key_ticks, uint64_t total_ticks);
bm_status_t headless_run_input_schedule(
    bm_session_t *session, const headless_text_action_t *text_actions,
    size_t text_action_count, const headless_key_action_t *key_actions,
    size_t key_action_count, uint64_t key_ticks);
bm_status_t headless_run_input_schedule_with_action(
    bm_session_t *session, const headless_text_action_t *text_actions,
    size_t text_action_count, const headless_key_action_t *key_actions,
    size_t key_action_count, uint64_t key_ticks, uint64_t action_at,
    headless_timed_action_fn action, void *action_context);

#endif
