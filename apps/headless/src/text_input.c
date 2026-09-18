/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_input.h"

#include <stddef.h>
#include <string.h>

typedef struct named_key {
    const char *name;
    bm_key_code_t key;
} named_key_t;

int
headless_key_code_from_name(const char *name, bm_key_code_t *key)
{
    static const named_key_t named_keys[] = {
        { "enter", BM_KEY_ENTER }, { "escape", BM_KEY_ESCAPE },
        { "backspace", BM_KEY_BACKSPACE }, { "tab", BM_KEY_TAB },
        { "space", BM_KEY_SPACE }, { "minus", BM_KEY_MINUS },
        { "equal", BM_KEY_EQUAL }, { "left-bracket", BM_KEY_LEFT_BRACKET },
        { "right-bracket", BM_KEY_RIGHT_BRACKET },
        { "backslash", BM_KEY_BACKSLASH },
        { "semicolon", BM_KEY_SEMICOLON },
        { "apostrophe", BM_KEY_APOSTROPHE }, { "grave", BM_KEY_GRAVE },
        { "comma", BM_KEY_COMMA }, { "period", BM_KEY_PERIOD },
        { "slash", BM_KEY_SLASH }, { "caps-lock", BM_KEY_CAPS_LOCK },
        { "f1", BM_KEY_F1 }, { "f2", BM_KEY_F2 }, { "f3", BM_KEY_F3 },
        { "f4", BM_KEY_F4 }, { "f5", BM_KEY_F5 }, { "f6", BM_KEY_F6 },
        { "f7", BM_KEY_F7 }, { "f8", BM_KEY_F8 }, { "f9", BM_KEY_F9 },
        { "f10", BM_KEY_F10 }, { "print-screen", BM_KEY_PRINT_SCREEN },
        { "scroll-lock", BM_KEY_SCROLL_LOCK }, { "pause", BM_KEY_PAUSE },
        { "insert", BM_KEY_INSERT }, { "home", BM_KEY_HOME },
        { "page-up", BM_KEY_PAGE_UP }, { "delete", BM_KEY_DELETE },
        { "end", BM_KEY_END }, { "page-down", BM_KEY_PAGE_DOWN },
        { "right", BM_KEY_RIGHT }, { "left", BM_KEY_LEFT },
        { "down", BM_KEY_DOWN }, { "up", BM_KEY_UP },
        { "non-us-backslash", BM_KEY_NON_US_BACKSLASH },
        { "left-control", BM_KEY_LEFT_CONTROL },
        { "left-shift", BM_KEY_LEFT_SHIFT }, { "left-alt", BM_KEY_LEFT_ALT },
        { "right-control", BM_KEY_RIGHT_CONTROL },
        { "right-shift", BM_KEY_RIGHT_SHIFT },
        { "right-alt", BM_KEY_RIGHT_ALT }
    };
    size_t index;
    if ((name == NULL) || (key == NULL) || (name[0] == '\0'))
        return 0;
    if ((name[1] == '\0') && (name[0] >= 'a') && (name[0] <= 'z')) {
        *key = (bm_key_code_t) (BM_KEY_A + name[0] - 'a');
        return 1;
    }
    if ((name[1] == '\0') && (name[0] >= '0') && (name[0] <= '9')) {
        *key = name[0] == '0' ? BM_KEY_0 :
               (bm_key_code_t) (BM_KEY_1 + name[0] - '1');
        return 1;
    }
    for (index = 0U; index < sizeof(named_keys) / sizeof(named_keys[0]);
         ++index) {
        if (strcmp(name, named_keys[index].name) == 0) {
            *key = named_keys[index].key;
            return 1;
        }
    }
    return 0;
}

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
    const bm_input_event_t event = {
        .kind = BM_INPUT_KEY, .key = key, .pressed = pressed
    };
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

bm_status_t
headless_input_schedule_validate(
    const headless_text_action_t *text_actions, size_t text_action_count,
    const headless_key_action_t *key_actions, size_t key_action_count,
    uint64_t key_ticks, uint64_t total_ticks)
{
    size_t text_index = 0U;
    size_t key_index = 0U;
    uint64_t previous_end = 0U;
    if (((text_action_count != 0U) && (text_actions == NULL)) ||
        ((key_action_count != 0U) && (key_actions == NULL)) ||
        (key_ticks == 0U))
        return BM_STATUS_INVALID_ARGUMENT;
    while ((text_index < text_action_count) || (key_index < key_action_count)) {
        const int use_text = (text_index < text_action_count) &&
            ((key_index >= key_action_count) ||
             (text_actions[text_index].at <= key_actions[key_index].at));
        uint64_t at;
        uint64_t duration;
        bm_status_t status = BM_STATUS_OK;
        if (use_text) {
            at = text_actions[text_index].at;
            status = headless_text_duration(text_actions[text_index].text,
                                            key_ticks, &duration);
            ++text_index;
        } else {
            at = key_actions[key_index].at;
            duration = key_ticks;
            if ((key_actions[key_index].pressed != 0) &&
                (key_actions[key_index].pressed != 1))
                status = BM_STATUS_INVALID_ARGUMENT;
            ++key_index;
        }
        if ((status != BM_STATUS_OK) || (at < previous_end) ||
            (at >= total_ticks) || (duration > total_ticks - at))
            return BM_STATUS_INVALID_ARGUMENT;
        previous_end = at + duration;
    }
    return BM_STATUS_OK;
}

bm_status_t
headless_run_input_schedule(
    bm_session_t *session, const headless_text_action_t *text_actions,
    size_t text_action_count, const headless_key_action_t *key_actions,
    size_t key_action_count, uint64_t key_ticks)
{
    return headless_run_input_schedule_with_action(
        session, text_actions, text_action_count, key_actions,
        key_action_count, key_ticks, 0U, NULL, NULL);
}

bm_status_t
headless_run_input_schedule_with_action(
    bm_session_t *session, const headless_text_action_t *text_actions,
    size_t text_action_count, const headless_key_action_t *key_actions,
    size_t key_action_count, uint64_t key_ticks, uint64_t action_at,
    headless_timed_action_fn action, void *action_context)
{
    size_t text_index = 0U;
    size_t key_index = 0U;
    int action_pending = action != NULL;
    if ((session == NULL) ||
        ((text_action_count != 0U) && (text_actions == NULL)) ||
        ((key_action_count != 0U) && (key_actions == NULL)))
        return BM_STATUS_INVALID_ARGUMENT;
    while ((text_index < text_action_count) || (key_index < key_action_count) ||
           action_pending) {
        const int use_text = (text_index < text_action_count) &&
            ((key_index >= key_action_count) ||
             (text_actions[text_index].at <= key_actions[key_index].at)) &&
            (!action_pending || (text_actions[text_index].at <= action_at));
        const int use_key = !use_text && (key_index < key_action_count) &&
            (!action_pending || (key_actions[key_index].at <= action_at));
        const uint64_t at = use_text ? text_actions[text_index].at :
                            use_key ? key_actions[key_index].at : action_at;
        const uint64_t now = bm_session_time(session);
        bm_status_t status;
        if (now > at)
            return BM_STATUS_INVALID_ARGUMENT;
        status = now < at ? bm_session_run_for(session, at - now) :
                            BM_STATUS_OK;
        if (status != BM_STATUS_OK)
            return status;
        if (use_text) {
            status = headless_type_text(session, text_actions[text_index].text,
                                        key_ticks);
            ++text_index;
        } else if (use_key) {
            status = send_transition(session, key_actions[key_index].key,
                                     key_actions[key_index].pressed, key_ticks);
            ++key_index;
        } else {
            status = action(action_context);
            action_pending = 0;
        }
        if (status != BM_STATUS_OK)
            return status;
    }
    return BM_STATUS_OK;
}
