/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qt_key_map.h"

#include <Qt>

namespace {
/* Win32 virtual-key values are used only to retain the side of modifier keys.
 * Other Qt platforms pass zero or their native value and use the left-key
 * fallback until a platform-specific physical-key adapter is added. */
constexpr quint32 winVkRightShift = 0xa1U;
constexpr quint32 winVkRightControl = 0xa3U;
constexpr quint32 winVkRightAlt = 0xa5U;
}

bm_key_code_t
bmQtKeyCode(int key, quint32 nativeVirtualKey)
{
    if ((key >= Qt::Key_A) && (key <= Qt::Key_Z))
        return static_cast<bm_key_code_t>(BM_KEY_A + key - Qt::Key_A);
    if ((key >= Qt::Key_1) && (key <= Qt::Key_9))
        return static_cast<bm_key_code_t>(BM_KEY_1 + key - Qt::Key_1);
    if (key == Qt::Key_0)
        return BM_KEY_0;
    switch (key) {
        case Qt::Key_Return: case Qt::Key_Enter: return BM_KEY_ENTER;
        case Qt::Key_Escape: return BM_KEY_ESCAPE;
        case Qt::Key_Backspace: return BM_KEY_BACKSPACE;
        case Qt::Key_Tab: return BM_KEY_TAB;
        case Qt::Key_Space: return BM_KEY_SPACE;
        case Qt::Key_Minus: case Qt::Key_Underscore: return BM_KEY_MINUS;
        case Qt::Key_Equal: case Qt::Key_Plus: return BM_KEY_EQUAL;
        case Qt::Key_BracketLeft: case Qt::Key_BraceLeft:
            return BM_KEY_LEFT_BRACKET;
        case Qt::Key_BracketRight: case Qt::Key_BraceRight:
            return BM_KEY_RIGHT_BRACKET;
        case Qt::Key_Backslash: case Qt::Key_Bar: return BM_KEY_BACKSLASH;
        case Qt::Key_Semicolon: case Qt::Key_Colon: return BM_KEY_SEMICOLON;
        case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl:
            return BM_KEY_APOSTROPHE;
        case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return BM_KEY_GRAVE;
        case Qt::Key_Comma: case Qt::Key_Less: return BM_KEY_COMMA;
        case Qt::Key_Period: case Qt::Key_Greater: return BM_KEY_PERIOD;
        case Qt::Key_Slash: case Qt::Key_Question: return BM_KEY_SLASH;
        case Qt::Key_Exclam: return BM_KEY_1;
        case Qt::Key_At: return BM_KEY_2;
        case Qt::Key_NumberSign: return BM_KEY_3;
        case Qt::Key_Dollar: return BM_KEY_4;
        case Qt::Key_Percent: return BM_KEY_5;
        case Qt::Key_AsciiCircum: return BM_KEY_6;
        case Qt::Key_Ampersand: return BM_KEY_7;
        case Qt::Key_Asterisk: return BM_KEY_8;
        case Qt::Key_ParenLeft: return BM_KEY_9;
        case Qt::Key_ParenRight: return BM_KEY_0;
        case Qt::Key_CapsLock: return BM_KEY_CAPS_LOCK;
        case Qt::Key_F1: return BM_KEY_F1;
        case Qt::Key_F2: return BM_KEY_F2;
        case Qt::Key_F3: return BM_KEY_F3;
        case Qt::Key_F4: return BM_KEY_F4;
        case Qt::Key_F5: return BM_KEY_F5;
        case Qt::Key_F6: return BM_KEY_F6;
        case Qt::Key_F7: return BM_KEY_F7;
        case Qt::Key_F8: return BM_KEY_F8;
        case Qt::Key_F9: return BM_KEY_F9;
        case Qt::Key_F10: return BM_KEY_F10;
        case Qt::Key_Insert: return BM_KEY_INSERT;
        case Qt::Key_Home: return BM_KEY_HOME;
        case Qt::Key_PageUp: return BM_KEY_PAGE_UP;
        case Qt::Key_Delete: return BM_KEY_DELETE;
        case Qt::Key_End: return BM_KEY_END;
        case Qt::Key_PageDown: return BM_KEY_PAGE_DOWN;
        case Qt::Key_Right: return BM_KEY_RIGHT;
        case Qt::Key_Left: return BM_KEY_LEFT;
        case Qt::Key_Down: return BM_KEY_DOWN;
        case Qt::Key_Up: return BM_KEY_UP;
        case Qt::Key_Control:
            return nativeVirtualKey == winVkRightControl ?
                BM_KEY_RIGHT_CONTROL : BM_KEY_LEFT_CONTROL;
        case Qt::Key_Shift:
            return nativeVirtualKey == winVkRightShift ?
                BM_KEY_RIGHT_SHIFT : BM_KEY_LEFT_SHIFT;
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
            return nativeVirtualKey == winVkRightAlt || key == Qt::Key_AltGr ?
                BM_KEY_RIGHT_ALT : BM_KEY_LEFT_ALT;
        default: return static_cast<bm_key_code_t>(0);
    }
}
