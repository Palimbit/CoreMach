/*
 * BluMach form-factor icon renderer.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_FORMFACTORICON_HPP
#define QT_BLUMACH_FORMFACTORICON_HPP

class QPainter;
class QPalette;
class QRect;
class QString;

namespace BluMachFormFactorIcon {
void paint(QPainter *painter, const QRect &bounds, const QString &formFactor,
           const QPalette &palette);
}

#endif
