/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

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

QImage
DisplayWidget::frame() const
{
    return frame_;
}

bool
DisplayWidget::hasFrame() const
{
    return !frame_.isNull();
}

void
DisplayWidget::setScaleMode(ScaleMode mode)
{
    if (scaleMode_ == mode)
        return;
    scaleMode_ = mode;
    update();
}

DisplayWidget::ScaleMode
DisplayWidget::scaleMode() const
{
    return scaleMode_;
}

void
DisplayWidget::setSmoothScaling(bool enabled)
{
    if (smoothScaling_ == enabled)
        return;
    smoothScaling_ = enabled;
    update();
}

bool
DisplayWidget::smoothScaling() const
{
    return smoothScaling_;
}

void
DisplayWidget::setKeyHandler(KeyHandler handler)
{
    keyHandler_ = std::move(handler);
}

QRect
DisplayWidget::targetRect(const QSize &frameSize, const QSize &viewportSize,
                          ScaleMode mode)
{
    if (frameSize.isEmpty() || viewportSize.isEmpty())
        return {};
    if (mode == ScaleMode::Stretch)
        return { QPoint(0, 0), viewportSize };

    QSize scaled;
    if (mode == ScaleMode::CorrectedFourThree) {
        scaled = QSize(4, 3);
        scaled.scale(viewportSize, Qt::KeepAspectRatio);
    } else if (mode == ScaleMode::Integer) {
        const int factor = std::min(viewportSize.width() / frameSize.width(),
                                    viewportSize.height() / frameSize.height());
        if (factor > 0)
            scaled = frameSize * factor;
        else {
            scaled = frameSize;
            scaled.scale(viewportSize, Qt::KeepAspectRatio);
        }
    } else {
        scaled = frameSize;
        scaled.scale(viewportSize, Qt::KeepAspectRatio);
    }

    QRect target(QPoint(0, 0), scaled);
    target.moveCenter(QRect(QPoint(0, 0), viewportSize).center());
    return target;
}

void
DisplayWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    (void) event;
    painter.fillRect(rect(), Qt::black);
    if (frame_.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling_);
    painter.drawImage(targetRect(frame_.size(), size(), scaleMode_), frame_);
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
