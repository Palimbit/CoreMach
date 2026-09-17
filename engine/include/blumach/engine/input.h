/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_ENGINE_INPUT_H
#define BLUMACH_ENGINE_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable physical-key identifiers. Values follow the USB HID keyboard usage
 * page so frontends do not expose guest scan-code sets to the runtime. */
typedef enum bm_key_code {
    BM_KEY_A = 0x04,
    BM_KEY_B, BM_KEY_C, BM_KEY_D, BM_KEY_E, BM_KEY_F, BM_KEY_G, BM_KEY_H,
    BM_KEY_I, BM_KEY_J, BM_KEY_K, BM_KEY_L, BM_KEY_M, BM_KEY_N, BM_KEY_O,
    BM_KEY_P, BM_KEY_Q, BM_KEY_R, BM_KEY_S, BM_KEY_T, BM_KEY_U, BM_KEY_V,
    BM_KEY_W, BM_KEY_X, BM_KEY_Y, BM_KEY_Z,
    BM_KEY_1 = 0x1e,
    BM_KEY_2, BM_KEY_3, BM_KEY_4, BM_KEY_5,
    BM_KEY_6, BM_KEY_7, BM_KEY_8, BM_KEY_9, BM_KEY_0,
    BM_KEY_ENTER = 0x28,
    BM_KEY_ESCAPE,
    BM_KEY_BACKSPACE,
    BM_KEY_TAB,
    BM_KEY_SPACE,
    BM_KEY_MINUS,
    BM_KEY_EQUAL,
    BM_KEY_LEFT_BRACKET,
    BM_KEY_RIGHT_BRACKET,
    BM_KEY_BACKSLASH,
    BM_KEY_SEMICOLON = 0x33,
    BM_KEY_APOSTROPHE,
    BM_KEY_GRAVE,
    BM_KEY_COMMA,
    BM_KEY_PERIOD,
    BM_KEY_SLASH,
    BM_KEY_CAPS_LOCK,
    BM_KEY_F1,
    BM_KEY_F2, BM_KEY_F3, BM_KEY_F4, BM_KEY_F5, BM_KEY_F6,
    BM_KEY_F7, BM_KEY_F8, BM_KEY_F9, BM_KEY_F10,
    BM_KEY_PRINT_SCREEN,
    BM_KEY_SCROLL_LOCK,
    BM_KEY_PAUSE,
    BM_KEY_INSERT,
    BM_KEY_HOME,
    BM_KEY_PAGE_UP,
    BM_KEY_DELETE,
    BM_KEY_END,
    BM_KEY_PAGE_DOWN,
    BM_KEY_RIGHT,
    BM_KEY_LEFT,
    BM_KEY_DOWN,
    BM_KEY_UP,
    BM_KEY_NON_US_BACKSLASH = 0x64,
    BM_KEY_LEFT_CONTROL = 0xe0,
    BM_KEY_LEFT_SHIFT,
    BM_KEY_LEFT_ALT,
    BM_KEY_RIGHT_CONTROL = 0xe4,
    BM_KEY_RIGHT_SHIFT,
    BM_KEY_RIGHT_ALT
} bm_key_code_t;

typedef enum bm_input_event_kind {
    BM_INPUT_KEY = 1
} bm_input_event_kind_t;

typedef struct bm_input_event {
    bm_input_event_kind_t kind;
    bm_key_code_t key;
    int pressed;
    int repeat;
} bm_input_event_t;

#ifdef __cplusplus
}
#endif

#endif
