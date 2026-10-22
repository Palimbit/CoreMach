/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "m10_keyboard.h"

#include <string.h>

/* Host keys follow the physical positions of the USA-ASCII M10 keyboard
 * (operations guide, Fig. 1-3, p. 1-5) on a PC keyboard; matrix codes and
 * their English legends come from the service manual Fig. 6-7. National
 * M10 layouts move legends, not positions, and are selected by the
 * firmware from the language jumpers.
 *
 *   M10 number row  [ 1 2 3 4 5 6 7 8 9 0 - ^ ]  -> ` 1 ... 0 - = and ]
 *   M10 Q row       Q W E R T Y U I O P @         -> [ for @
 *   M10 A row       A S D F G H J K L ; :         -> ' for :
 *   M10 Z row       \ Z X C V B N M , . /         -> ISO key or \ for \
 * Keys with no PC counterpart: PASTE on F9, LABEL on F10, PRINT on Print
 * Screen, PAUSE/BREAK on Pause, GRPH on Alt and NUM on Insert. */
static const struct {
    bm_key_code_t key;
    uint8_t code;
} key_map[] = {
    { BM_KEY_Q, 0 }, { BM_KEY_W, 1 }, { BM_KEY_GRAVE, 2 },
    { BM_KEY_1, 3 }, { BM_KEY_2, 4 }, { BM_KEY_3, 5 }, { BM_KEY_4, 6 },
    { BM_KEY_5, 7 }, { BM_KEY_Y, 8 }, { BM_KEY_6, 9 }, { BM_KEY_7, 10 },
    { BM_KEY_8, 11 }, { BM_KEY_9, 12 }, { BM_KEY_0, 13 },
    { BM_KEY_MINUS, 14 }, { BM_KEY_EQUAL, 15 },
    { BM_KEY_RIGHT_BRACKET, 16 }, { BM_KEY_LEFT_BRACKET, 17 },
    { BM_KEY_SEMICOLON, 18 }, { BM_KEY_APOSTROPHE, 19 },
    { BM_KEY_SLASH, 20 }, { BM_KEY_PERIOD, 21 }, { BM_KEY_COMMA, 22 },
    { BM_KEY_M, 23 }, { BM_KEY_A, 24 },
    { BM_KEY_BACKSLASH, 25 }, { BM_KEY_NON_US_BACKSLASH, 25 },
    { BM_KEY_Z, 26 }, { BM_KEY_X, 27 }, { BM_KEY_C, 28 }, { BM_KEY_V, 29 },
    { BM_KEY_B, 30 }, { BM_KEY_N, 31 }, { BM_KEY_S, 32 }, { BM_KEY_D, 33 },
    { BM_KEY_F, 34 }, { BM_KEY_G, 35 }, { BM_KEY_H, 36 }, { BM_KEY_J, 37 },
    { BM_KEY_K, 38 }, { BM_KEY_L, 39 }, { BM_KEY_E, 40 }, { BM_KEY_R, 41 },
    { BM_KEY_T, 42 }, { BM_KEY_U, 43 }, { BM_KEY_I, 44 }, { BM_KEY_O, 45 },
    { BM_KEY_P, 46 }, { BM_KEY_SPACE, 47 },
    { BM_KEY_LEFT, 48 }, { BM_KEY_RIGHT, 49 }, { BM_KEY_UP, 50 },
    { BM_KEY_DOWN, 51 }, { BM_KEY_BACKSPACE, 52 }, { BM_KEY_DELETE, 52 },
    { BM_KEY_TAB, 53 }, { BM_KEY_ENTER, 54 }, { BM_KEY_F9, 55 },
    { BM_KEY_F1, 56 }, { BM_KEY_F2, 57 }, { BM_KEY_F3, 58 }, { BM_KEY_F4, 59 },
    { BM_KEY_F5, 60 }, { BM_KEY_F6, 61 }, { BM_KEY_F7, 62 }, { BM_KEY_F8, 63 },
    { BM_KEY_F10, 64 }, { BM_KEY_PRINT_SCREEN, 65 },
    { BM_KEY_LEFT_SHIFT, 66 }, { BM_KEY_RIGHT_SHIFT, 66 },
    { BM_KEY_LEFT_CONTROL, 67 }, { BM_KEY_RIGHT_CONTROL, 67 },
    { BM_KEY_LEFT_ALT, 68 }, { BM_KEY_RIGHT_ALT, 68 },
    { BM_KEY_INSERT, 69 }, { BM_KEY_CAPS_LOCK, M10_KEY_CAPS_LOCK },
    { BM_KEY_PAUSE, 71 }
};

void
m10_keyboard_reset(m10_keyboard_t *keyboard)
{
    /* Releasing the host keys; a latched CAPS LOCK stays down. */
    memset(keyboard->rows, 0, sizeof(keyboard->rows));
    if (keyboard->caps_lock)
        keyboard->rows[M10_KEY_CAPS_LOCK >> 3U] |=
            (uint8_t) (1U << (M10_KEY_CAPS_LOCK & 7U));
}

int
m10_keyboard_code(bm_key_code_t key)
{
    size_t index;
    for (index = 0U; index < sizeof(key_map) / sizeof(key_map[0]); ++index)
        if (key_map[index].key == key)
            return key_map[index].code;
    return -1;
}

bm_status_t
m10_keyboard_input(m10_keyboard_t *keyboard, const bm_input_event_t *event)
{
    int code;
    uint8_t bit;

    if ((keyboard == NULL) || (event == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (event->kind != BM_INPUT_KEY)
        return BM_STATUS_UNSUPPORTED;
    code = m10_keyboard_code(event->key);
    if (code < 0)
        return BM_STATUS_UNSUPPORTED;
    bit = (uint8_t) (1U << ((unsigned int) code & 7U));
    if (code == (int) M10_KEY_CAPS_LOCK) {
        /* CAPS/SHIFT LOCK stays down until pressed again (operations
         * guide, p. 1-7); each host press toggles it. */
        if (event->pressed && !event->repeat)
            keyboard->caps_lock = !keyboard->caps_lock;
        if (keyboard->caps_lock)
            keyboard->rows[code >> 3] |= bit;
        else
            keyboard->rows[code >> 3] &= (uint8_t) ~bit;
        return BM_STATUS_OK;
    }
    if (event->pressed)
        keyboard->rows[code >> 3] |= bit;
    else
        keyboard->rows[code >> 3] &= (uint8_t) ~bit;
    return BM_STATUS_OK;
}

uint8_t
m10_keyboard_scan(const m10_keyboard_t *keyboard, uint8_t port_a,
                  uint8_t port_b)
{
    uint8_t closed = 0U;
    unsigned int row;

    for (row = 0U; row < 8U; ++row)
        if (((port_a >> row) & 1U) == 0U)
            closed |= keyboard->rows[row];
    if ((port_b & 1U) == 0U)
        closed |= keyboard->rows[8];
    return (uint8_t) ~closed;
}
