/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>

DisplayWidget::DisplayWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(720, 400);
    setAutoFillBackground(false);
}

void
DisplayWidget::setFrame(const QImage &frame)
{
    frame_ = frame;
    update();
}

void
DisplayWidget::setKeyHandler(KeyHandler handler)
{
    keyHandler_ = std::move(handler);
}

void
DisplayWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QRect target = rect();
    (void) event;
    painter.fillRect(target, Qt::black);
    if (frame_.isNull())
        return;
    QSize scaled = frame_.size();
    scaled.scale(target.size(), Qt::KeepAspectRatio);
    target = QRect(QPoint(0, 0), scaled);
    target.moveCenter(rect().center());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, frame_);
}

void
DisplayWidget::keyPressEvent(QKeyEvent *event)
{
    if (keyHandler_)
        keyHandler_(event, true);
    else
        QWidget::keyPressEvent(event);
}

void
DisplayWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (keyHandler_)
        keyHandler_(event, false);
    else
        QWidget::keyReleaseEvent(event);
}
