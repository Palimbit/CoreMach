/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_QT_KEY_MAP_H
#define BLUMACH_QT_KEY_MAP_H

#include <blumach/engine/input.h>

#include <QtGlobal>

bm_key_code_t bmWindowsScanCodeToKey(quint32 nativeScanCode);
bm_key_code_t bmQtKeyCode(int key, quint32 nativeScanCode,
                          quint32 nativeVirtualKey);
bool bmQtShouldForwardKey(bool pressed, bool autoRepeat);

#endif
