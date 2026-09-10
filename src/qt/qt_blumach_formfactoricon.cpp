/*
 * BluMach form-factor icon renderer.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_formfactoricon.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {
QColor
blendColor(const QColor &background, const QColor &foreground, const qreal amount)
{
    const qreal inverse = 1.0 - amount;
    return QColor::fromRgbF(background.redF() * inverse + foreground.redF() * amount,
                            background.greenF() * inverse + foreground.greenF() * amount,
                            background.blueF() * inverse + foreground.blueF() * amount);
}

void
drawKeyboard(QPainter *painter, const QRectF &keyboard)
{
    painter->drawRoundedRect(keyboard, 0.9, 0.9);
    const qreal firstRow = keyboard.top() + keyboard.height() * 0.32;
    const qreal secondRow = keyboard.top() + keyboard.height() * 0.62;
    painter->drawLine(QPointF(keyboard.left() + keyboard.width() * 0.12, firstRow),
                      QPointF(keyboard.right() - keyboard.width() * 0.12, firstRow));
    painter->drawLine(QPointF(keyboard.left() + keyboard.width() * 0.12, secondRow),
                      QPointF(keyboard.right() - keyboard.width() * 0.12, secondRow));
    painter->drawLine(QPointF(keyboard.center().x(), secondRow),
                      QPointF(keyboard.center().x(), keyboard.bottom() - keyboard.height() * 0.14));
}

void
drawCrtScreen(QPainter *painter, const QRectF &screen)
{
    painter->drawRoundedRect(screen, 1.3, 1.3);
    const QRectF inner = screen.adjusted(screen.width() * 0.15, screen.height() * 0.15,
                                         -screen.width() * 0.15, -screen.height() * 0.22);
    painter->drawRoundedRect(inner, 0.7, 0.7);
}
}

namespace BluMachFormFactorIcon {
void
paint(QPainter *painter, const QRect &bounds, const QString &formFactor,
      const QPalette &palette)
{
    // "Archivo vivo": the catalogue's technical blue makes the machine
    // silhouette a first-class part of its preservation record, while still
    // adapting to the active Qt palette.
    const QColor outline = blendColor(palette.color(QPalette::Base),
                                      palette.color(QPalette::Highlight), 0.68);
    QPen pen(outline, qMax<qreal>(1.1, bounds.width() * 0.055));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const QRectF glyph = bounds.adjusted(3, 3, -3, -3);

    if (formFactor == QStringLiteral("laptop")) {
        const QRectF screen(glyph.left() + glyph.width() * 0.17, glyph.top() + 1,
                            glyph.width() * 0.66, glyph.height() * 0.47);
        drawCrtScreen(painter, screen);
        QPainterPath base;
        base.moveTo(glyph.left() + glyph.width() * 0.06, screen.bottom() + 2);
        base.lineTo(glyph.right() - glyph.width() * 0.06, screen.bottom() + 2);
        base.lineTo(glyph.right() - glyph.width() * 0.15, glyph.bottom() - 1);
        base.lineTo(glyph.left() + glyph.width() * 0.15, glyph.bottom() - 1);
        base.closeSubpath();
        painter->drawPath(base);
        const QRectF keyboard(glyph.left() + glyph.width() * 0.28, screen.bottom() + 4,
                              glyph.width() * 0.44, glyph.height() * 0.16);
        drawKeyboard(painter, keyboard);
    } else if (formFactor == QStringLiteral("luggable")) {
        const QRectF caseRect(glyph.left() + 1, glyph.top() + glyph.height() * 0.17,
                              glyph.width() - 2, glyph.height() * 0.75);
        painter->drawRoundedRect(caseRect, 2, 2);
        painter->drawArc(QRectF(glyph.center().x() - glyph.width() * 0.18, glyph.top(),
                                glyph.width() * 0.36, glyph.height() * 0.3), 0, 180 * 16);
        const QRectF screen(caseRect.left() + caseRect.width() * 0.11, caseRect.top() + caseRect.height() * 0.17,
                            caseRect.width() * 0.42, caseRect.height() * 0.38);
        drawCrtScreen(painter, screen);
        const QRectF bays(caseRect.right() - caseRect.width() * 0.25, caseRect.top() + caseRect.height() * 0.22,
                          caseRect.width() * 0.14, caseRect.height() * 0.14);
        painter->drawRect(bays);
        painter->drawRect(bays.translated(0, bays.height() * 1.55));
        painter->drawLine(QPointF(caseRect.left() + caseRect.width() * 0.12, caseRect.bottom() - caseRect.height() * 0.15),
                          QPointF(caseRect.right() - caseRect.width() * 0.12, caseRect.bottom() - caseRect.height() * 0.15));
    } else if (formFactor == QStringLiteral("palmtop")) {
        const QRectF device(glyph.left() + glyph.width() * 0.18, glyph.top() + 1,
                            glyph.width() * 0.64, glyph.height() - 2);
        painter->drawRoundedRect(device, 2, 2);
        const QRectF screen(device.left() + device.width() * 0.15, device.top() + device.height() * 0.12,
                            device.width() * 0.7, device.height() * 0.32);
        painter->drawRect(screen);
        const QRectF keyboard(device.left() + device.width() * 0.12, device.top() + device.height() * 0.53,
                              device.width() * 0.76, device.height() * 0.26);
        drawKeyboard(painter, keyboard);
        painter->drawEllipse(QPointF(device.center().x(), device.bottom() - device.height() * 0.1), 0.7, 0.7);
    } else if (formFactor == QStringLiteral("all_in_one")) {
        const QRectF body(glyph.left() + glyph.width() * 0.18, glyph.top() + 1,
                          glyph.width() * 0.64, glyph.height() * 0.64);
        drawCrtScreen(painter, body);
        painter->drawLine(QPointF(body.left() + body.width() * 0.2, body.bottom() - body.height() * 0.1),
                          QPointF(body.right() - body.width() * 0.2, body.bottom() - body.height() * 0.1));
        painter->drawEllipse(QPointF(body.center().x(), body.bottom() - body.height() * 0.04), 0.6, 0.6);
        const QRectF keyboard(glyph.left() + glyph.width() * 0.1, glyph.bottom() - glyph.height() * 0.2,
                              glyph.width() * 0.8, glyph.height() * 0.16);
        drawKeyboard(painter, keyboard);
    } else if (formFactor == QStringLiteral("tower")) {
        const QRectF tower(glyph.left() + glyph.width() * 0.25, glyph.top() + 1,
                           glyph.width() * 0.5, glyph.height() - 2);
        painter->drawRoundedRect(tower, 2, 2);
        const QRectF bay(tower.left() + tower.width() * 0.18, tower.top() + tower.height() * 0.14,
                         tower.width() * 0.64, tower.height() * 0.14);
        painter->drawRect(bay);
        painter->drawRect(bay.translated(0, bay.height() * 1.45));
        painter->drawLine(QPointF(tower.left() + tower.width() * 0.2, tower.top() + tower.height() * 0.62),
                          QPointF(tower.right() - tower.width() * 0.2, tower.top() + tower.height() * 0.62));
        painter->drawEllipse(QPointF(tower.center().x(), tower.bottom() - tower.height() * 0.15), 0.8, 0.8);
    } else {
        const QRectF screen(glyph.left() + glyph.width() * 0.14, glyph.top() + 1,
                            glyph.width() * 0.56, glyph.height() * 0.51);
        drawCrtScreen(painter, screen);
        painter->drawLine(QPointF(screen.center().x(), screen.bottom()),
                          QPointF(screen.center().x(), glyph.bottom() - glyph.height() * 0.27));
        painter->drawLine(QPointF(screen.left() + screen.width() * 0.2, glyph.bottom() - glyph.height() * 0.27),
                          QPointF(screen.right() - screen.width() * 0.2, glyph.bottom() - glyph.height() * 0.27));
        const QRectF keyboard(glyph.left() + glyph.width() * 0.42, glyph.bottom() - glyph.height() * 0.19,
                              glyph.width() * 0.48, glyph.height() * 0.16);
        drawKeyboard(painter, keyboard);
    }
}
} // namespace BluMachFormFactorIcon
