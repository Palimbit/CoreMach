/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qt_key_map.h"

#include <Qt>

#include <cassert>

int
main()
{
    assert(bmQtKeyCode(Qt::Key_A, 0U, 0U) == BM_KEY_A);
    assert(bmQtKeyCode(Qt::Key_Z, 0U, 0U) == BM_KEY_Z);
    assert(bmQtKeyCode(Qt::Key_0, 0U, 0U) == BM_KEY_0);

    /* Qt reports the produced shifted symbol for these combinations on some
     * host layouts. They must retain the matching guest physical key. */
    assert(bmQtKeyCode(Qt::Key_Underscore, 0U, 0U) == BM_KEY_MINUS);
    assert(bmQtKeyCode(Qt::Key_Plus, 0U, 0U) == BM_KEY_EQUAL);
    assert(bmQtKeyCode(Qt::Key_QuoteDbl, 0U, 0U) == BM_KEY_APOSTROPHE);
    assert(bmQtKeyCode(Qt::Key_Question, 0U, 0U) == BM_KEY_SLASH);
    assert(bmQtKeyCode(Qt::Key_ParenRight, 0U, 0U) == BM_KEY_0);

    assert(bmQtKeyCode(Qt::Key_Shift, 0U, 0xa0U) == BM_KEY_LEFT_SHIFT);
    assert(bmQtKeyCode(Qt::Key_Shift, 0U, 0xa1U) == BM_KEY_RIGHT_SHIFT);
    assert(bmQtKeyCode(Qt::Key_Control, 0U, 0xa3U) == BM_KEY_RIGHT_CONTROL);
    assert(bmQtKeyCode(Qt::Key_AltGr, 0U, 0U) == BM_KEY_RIGHT_ALT);
    assert(bmQtKeyCode(Qt::Key_unknown, 0U, 0U) ==
           static_cast<bm_key_code_t>(0));

    /* The legacy frontend delivered physical Set 1 positions. Preserve that
     * behavior at the Qt boundary so host layouts do not rewrite guest keys. */
    assert(bmWindowsScanCodeToKey(0x010U) == BM_KEY_Q);
    assert(bmWindowsScanCodeToKey(0x01eU) == BM_KEY_A);
    assert(bmWindowsScanCodeToKey(0x00cU) == BM_KEY_MINUS);
    assert(bmWindowsScanCodeToKey(0x056U) == BM_KEY_NON_US_BACKSLASH);
    assert(bmWindowsScanCodeToKey(0x11dU) == BM_KEY_RIGHT_CONTROL);
    assert(bmWindowsScanCodeToKey(0x14bU) == BM_KEY_LEFT);
    assert(bmWindowsScanCodeToKey(0xffffU) ==
           static_cast<bm_key_code_t>(0));

    assert(bmQtShouldForwardKey(true, false));
    assert(bmQtShouldForwardKey(false, false));
    assert(bmQtShouldForwardKey(true, true));
    assert(!bmQtShouldForwardKey(false, true));
    return 0;
}
