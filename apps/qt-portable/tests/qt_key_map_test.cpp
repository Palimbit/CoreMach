/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qt_key_map.h"

#include <Qt>

#include <cassert>

int
main()
{
    assert(bmQtKeyCode(Qt::Key_A, 0U) == BM_KEY_A);
    assert(bmQtKeyCode(Qt::Key_Z, 0U) == BM_KEY_Z);
    assert(bmQtKeyCode(Qt::Key_0, 0U) == BM_KEY_0);

    /* Qt reports the produced shifted symbol for these combinations on some
     * host layouts. They must retain the matching guest physical key. */
    assert(bmQtKeyCode(Qt::Key_Underscore, 0U) == BM_KEY_MINUS);
    assert(bmQtKeyCode(Qt::Key_Plus, 0U) == BM_KEY_EQUAL);
    assert(bmQtKeyCode(Qt::Key_QuoteDbl, 0U) == BM_KEY_APOSTROPHE);
    assert(bmQtKeyCode(Qt::Key_Question, 0U) == BM_KEY_SLASH);
    assert(bmQtKeyCode(Qt::Key_ParenRight, 0U) == BM_KEY_0);

    assert(bmQtKeyCode(Qt::Key_Shift, 0xa0U) == BM_KEY_LEFT_SHIFT);
    assert(bmQtKeyCode(Qt::Key_Shift, 0xa1U) == BM_KEY_RIGHT_SHIFT);
    assert(bmQtKeyCode(Qt::Key_Control, 0xa3U) == BM_KEY_RIGHT_CONTROL);
    assert(bmQtKeyCode(Qt::Key_AltGr, 0U) == BM_KEY_RIGHT_ALT);
    assert(bmQtKeyCode(Qt::Key_unknown, 0U) ==
           static_cast<bm_key_code_t>(0));
    return 0;
}
