/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qt_key_map.h"

#include <Qt>

namespace {
/* Win32 virtual-key values retain the side of modifiers when Qt supplies no
 * physical scan code. */
constexpr quint32 winVkRightShift = 0xa1U;
constexpr quint32 winVkRightControl = 0xa3U;
constexpr quint32 winVkRightAlt = 0xa5U;
}

bm_key_code_t
bmWindowsScanCodeToKey(quint32 nativeScanCode)
{
    switch (nativeScanCode) {
        case 0x001U: return BM_KEY_ESCAPE;
        case 0x002U: return BM_KEY_1;
        case 0x003U: return BM_KEY_2;
        case 0x004U: return BM_KEY_3;
        case 0x005U: return BM_KEY_4;
        case 0x006U: return BM_KEY_5;
        case 0x007U: return BM_KEY_6;
        case 0x008U: return BM_KEY_7;
        case 0x009U: return BM_KEY_8;
        case 0x00aU: return BM_KEY_9;
        case 0x00bU: return BM_KEY_0;
        case 0x00cU: return BM_KEY_MINUS;
        case 0x00dU: return BM_KEY_EQUAL;
        case 0x00eU: return BM_KEY_BACKSPACE;
        case 0x00fU: return BM_KEY_TAB;
        case 0x010U: return BM_KEY_Q;
        case 0x011U: return BM_KEY_W;
        case 0x012U: return BM_KEY_E;
        case 0x013U: return BM_KEY_R;
        case 0x014U: return BM_KEY_T;
        case 0x015U: return BM_KEY_Y;
        case 0x016U: return BM_KEY_U;
        case 0x017U: return BM_KEY_I;
        case 0x018U: return BM_KEY_O;
        case 0x019U: return BM_KEY_P;
        case 0x01aU: return BM_KEY_LEFT_BRACKET;
        case 0x01bU: return BM_KEY_RIGHT_BRACKET;
        case 0x01cU: return BM_KEY_ENTER;
        case 0x01dU: return BM_KEY_LEFT_CONTROL;
        case 0x01eU: return BM_KEY_A;
        case 0x01fU: return BM_KEY_S;
        case 0x020U: return BM_KEY_D;
        case 0x021U: return BM_KEY_F;
        case 0x022U: return BM_KEY_G;
        case 0x023U: return BM_KEY_H;
        case 0x024U: return BM_KEY_J;
        case 0x025U: return BM_KEY_K;
        case 0x026U: return BM_KEY_L;
        case 0x027U: return BM_KEY_SEMICOLON;
        case 0x028U: return BM_KEY_APOSTROPHE;
        case 0x029U: return BM_KEY_GRAVE;
        case 0x02aU: return BM_KEY_LEFT_SHIFT;
        case 0x02bU: return BM_KEY_BACKSLASH;
        case 0x02cU: return BM_KEY_Z;
        case 0x02dU: return BM_KEY_X;
        case 0x02eU: return BM_KEY_C;
        case 0x02fU: return BM_KEY_V;
        case 0x030U: return BM_KEY_B;
        case 0x031U: return BM_KEY_N;
        case 0x032U: return BM_KEY_M;
        case 0x033U: return BM_KEY_COMMA;
        case 0x034U: return BM_KEY_PERIOD;
        case 0x035U: return BM_KEY_SLASH;
        case 0x036U: return BM_KEY_RIGHT_SHIFT;
        case 0x038U: return BM_KEY_LEFT_ALT;
        case 0x039U: return BM_KEY_SPACE;
        case 0x03aU: return BM_KEY_CAPS_LOCK;
        case 0x03bU: return BM_KEY_F1;
        case 0x03cU: return BM_KEY_F2;
        case 0x03dU: return BM_KEY_F3;
        case 0x03eU: return BM_KEY_F4;
        case 0x03fU: return BM_KEY_F5;
        case 0x040U: return BM_KEY_F6;
        case 0x041U: return BM_KEY_F7;
        case 0x042U: return BM_KEY_F8;
        case 0x043U: return BM_KEY_F9;
        case 0x044U: return BM_KEY_F10;
        case 0x046U: return BM_KEY_SCROLL_LOCK;
        case 0x056U: return BM_KEY_NON_US_BACKSLASH;
        case 0x11dU: return BM_KEY_RIGHT_CONTROL;
        case 0x138U: return BM_KEY_RIGHT_ALT;
        case 0x147U: return BM_KEY_HOME;
        case 0x148U: return BM_KEY_UP;
        case 0x149U: return BM_KEY_PAGE_UP;
        case 0x14bU: return BM_KEY_LEFT;
        case 0x14dU: return BM_KEY_RIGHT;
        case 0x14fU: return BM_KEY_END;
        case 0x150U: return BM_KEY_DOWN;
        case 0x151U: return BM_KEY_PAGE_DOWN;
        case 0x152U: return BM_KEY_INSERT;
        case 0x153U: return BM_KEY_DELETE;
        default: return static_cast<bm_key_code_t>(0);
    }
}

bm_key_code_t
bmQtKeyCode(int key, quint32 nativeScanCode, quint32 nativeVirtualKey)
{
#if defined(Q_OS_WIN)
    const bm_key_code_t physical = bmWindowsScanCodeToKey(nativeScanCode);
    if (physical != static_cast<bm_key_code_t>(0))
        return physical;
#else
    (void) nativeScanCode;
#endif
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

bool
bmQtShouldForwardKey(bool pressed, bool autoRepeat)
{
    return pressed || !autoRepeat;
}
