/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_input.h"

#include <stddef.h>

static int
decode_character(unsigned char value, headless_text_key_t *key)
{
    if ((value >= 'a') && (value <= 'z')) {
        key->key = (bm_key_code_t) (BM_KEY_A + value - 'a');
        key->shifted = 0;
        return 1;
    }
    if ((value >= 'A') && (value <= 'Z')) {
        key->key = (bm_key_code_t) (BM_KEY_A + value - 'A');
        key->shifted = 1;
        return 1;
    }
    if ((value >= '1') && (value <= '9')) {
        key->key = (bm_key_code_t) (BM_KEY_1 + value - '1');
        key->shifted = 0;
        return 1;
    }
    switch (value) {
        case '0': key->key = BM_KEY_0; key->shifted = 0; break;
        case '\n':
        case '\r': key->key = BM_KEY_ENTER; key->shifted = 0; break;
        case '\b': key->key = BM_KEY_BACKSPACE; key->shifted = 0; break;
        case '\t': key->key = BM_KEY_TAB; key->shifted = 0; break;
        case ' ': key->key = BM_KEY_SPACE; key->shifted = 0; break;
        case '-': key->key = BM_KEY_MINUS; key->shifted = 0; break;
        case '_': key->key = BM_KEY_MINUS; key->shifted = 1; break;
        case '=': key->key = BM_KEY_EQUAL; key->shifted = 0; break;
        case '+': key->key = BM_KEY_EQUAL; key->shifted = 1; break;
        case '[': key->key = BM_KEY_LEFT_BRACKET; key->shifted = 0; break;
        case '{': key->key = BM_KEY_LEFT_BRACKET; key->shifted = 1; break;
        case ']': key->key = BM_KEY_RIGHT_BRACKET; key->shifted = 0; break;
        case '}': key->key = BM_KEY_RIGHT_BRACKET; key->shifted = 1; break;
        case '\\': key->key = BM_KEY_BACKSLASH; key->shifted = 0; break;
        case '|': key->key = BM_KEY_BACKSLASH; key->shifted = 1; break;
        case ';': key->key = BM_KEY_SEMICOLON; key->shifted = 0; break;
        case ':': key->key = BM_KEY_SEMICOLON; key->shifted = 1; break;
        case '\'': key->key = BM_KEY_APOSTROPHE; key->shifted = 0; break;
        case '"': key->key = BM_KEY_APOSTROPHE; key->shifted = 1; break;
        case '`': key->key = BM_KEY_GRAVE; key->shifted = 0; break;
        case '~': key->key = BM_KEY_GRAVE; key->shifted = 1; break;
        case ',': key->key = BM_KEY_COMMA; key->shifted = 0; break;
        case '<': key->key = BM_KEY_COMMA; key->shifted = 1; break;
        case '.': key->key = BM_KEY_PERIOD; key->shifted = 0; break;
        case '>': key->key = BM_KEY_PERIOD; key->shifted = 1; break;
        case '/': key->key = BM_KEY_SLASH; key->shifted = 0; break;
        case '?': key->key = BM_KEY_SLASH; key->shifted = 1; break;
        case '!': key->key = BM_KEY_1; key->shifted = 1; break;
        case '@': key->key = BM_KEY_2; key->shifted = 1; break;
        case '#': key->key = BM_KEY_3; key->shifted = 1; break;
        case '$': key->key = BM_KEY_4; key->shifted = 1; break;
        case '%': key->key = BM_KEY_5; key->shifted = 1; break;
        case '^': key->key = BM_KEY_6; key->shifted = 1; break;
        case '&': key->key = BM_KEY_7; key->shifted = 1; break;
        case '*': key->key = BM_KEY_8; key->shifted = 1; break;
        case '(': key->key = BM_KEY_9; key->shifted = 1; break;
        case ')': key->key = BM_KEY_0; key->shifted = 1; break;
        default: return 0;
    }
    return 1;
}

int
headless_text_decode_next(const char **cursor, headless_text_key_t *key)
{
    const unsigned char *current;
    unsigned char value;
    if ((cursor == NULL) || (*cursor == NULL) || (key == NULL))
        return -1;
    current = (const unsigned char *) *cursor;
    if (*current == '\0')
        return 0;
    value = *current++;
    if (value == '\\') {
        value = *current++;
        switch (value) {
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case 'b': value = '\b'; break;
            case '\\': value = '\\'; break;
            default: return -1;
        }
    }
    if (!decode_character(value, key))
        return -1;
    *cursor = (const char *) current;
    return 1;
}

bm_status_t
headless_text_duration(const char *text, uint64_t key_ticks,
                       uint64_t *duration)
{
    const char *cursor = text;
    uint64_t transitions = 0U;
    headless_text_key_t key;
    int decoded;
    if ((text == NULL) || (text[0] == '\0') || (key_ticks == 0U) ||
        (duration == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    while ((decoded = headless_text_decode_next(&cursor, &key)) > 0) {
        const uint64_t key_transitions = key.shifted ? 4U : 2U;
        if (transitions > UINT64_MAX - key_transitions)
            return BM_STATUS_CAPACITY_EXCEEDED;
        transitions += key_transitions;
    }
    if (decoded < 0)
        return BM_STATUS_INVALID_ARGUMENT;
    if (transitions > UINT64_MAX / key_ticks)
        return BM_STATUS_CAPACITY_EXCEEDED;
    *duration = transitions * key_ticks;
    return BM_STATUS_OK;
}

static bm_status_t
send_transition(bm_session_t *session, bm_key_code_t key, int pressed,
                uint64_t key_ticks)
{
    const bm_input_event_t event = { BM_INPUT_KEY, key, pressed, 0 };
    bm_status_t status = bm_session_send_input(session, &event);
    if (status == BM_STATUS_OK)
        status = bm_session_run_for(session, key_ticks);
    return status;
}

bm_status_t
headless_type_text(bm_session_t *session, const char *text, uint64_t key_ticks)
{
    const char *cursor = text;
    uint64_t ignored_duration;
    headless_text_key_t key;
    int decoded;
    bm_status_t status;
    if (session == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    status = headless_text_duration(text, key_ticks, &ignored_duration);
    if (status != BM_STATUS_OK)
        return status;
    while ((decoded = headless_text_decode_next(&cursor, &key)) > 0) {
        if (key.shifted) {
            status = send_transition(session, BM_KEY_LEFT_SHIFT, 1, key_ticks);
            if (status != BM_STATUS_OK)
                return status;
        }
        status = send_transition(session, key.key, 1, key_ticks);
        if (status == BM_STATUS_OK)
            status = send_transition(session, key.key, 0, key_ticks);
        if ((status == BM_STATUS_OK) && key.shifted)
            status = send_transition(session, BM_KEY_LEFT_SHIFT, 0, key_ticks);
        if (status != BM_STATUS_OK)
            return status;
    }
    return decoded < 0 ? BM_STATUS_INVALID_ARGUMENT : BM_STATUS_OK;
}

bm_status_t
headless_text_schedule_validate(const headless_text_action_t *actions,
                                size_t action_count, uint64_t key_ticks,
                                uint64_t total_ticks)
{
    uint64_t previous_end = 0U;
    size_t index;
    if ((action_count != 0U) && (actions == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < action_count; ++index) {
        uint64_t duration;
        bm_status_t status = headless_text_duration(
            actions[index].text, key_ticks, &duration);
        if (status != BM_STATUS_OK)
            return status;
        if ((actions[index].at < previous_end) ||
            (actions[index].at >= total_ticks) ||
            (duration > total_ticks - actions[index].at))
            return BM_STATUS_INVALID_ARGUMENT;
        previous_end = actions[index].at + duration;
    }
    return BM_STATUS_OK;
}

bm_status_t
headless_run_text_schedule(bm_session_t *session,
                           const headless_text_action_t *actions,
                           size_t action_count, uint64_t key_ticks)
{
    size_t index;
    if ((session == NULL) || ((action_count != 0U) && (actions == NULL)))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < action_count; ++index) {
        bm_status_t status;
        const uint64_t now = bm_session_time(session);
        if (now > actions[index].at)
            return BM_STATUS_INVALID_ARGUMENT;
        status = now < actions[index].at ?
                 bm_session_run_for(session, actions[index].at - now) :
                 BM_STATUS_OK;
        if (status == BM_STATUS_OK)
            status = headless_type_text(session, actions[index].text,
                                        key_ticks);
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}
