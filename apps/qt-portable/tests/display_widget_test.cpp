/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <QApplication>

#include <cassert>

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);
    (void) application;
    using ScaleMode = DisplayWidget::ScaleMode;

    assert(DisplayWidget::targetRect({}, QSize(1024, 768), ScaleMode::Fit)
               .isEmpty());
    assert(DisplayWidget::targetRect(QSize(720, 400), {}, ScaleMode::Fit)
               .isEmpty());

    assert(DisplayWidget::targetRect(QSize(720, 400), QSize(1200, 800),
                                     ScaleMode::Fit) ==
           QRect(0, 67, 1200, 666));
    assert(DisplayWidget::targetRect(QSize(720, 400), QSize(1200, 800),
                                     ScaleMode::Integer) ==
           QRect(240, 200, 720, 400));
    assert(DisplayWidget::targetRect(QSize(720, 400), QSize(1024, 768),
                                     ScaleMode::CorrectedFourThree) ==
           QRect(0, 0, 1024, 768));
    assert(DisplayWidget::targetRect(QSize(720, 400), QSize(1200, 800),
                                     ScaleMode::Stretch) ==
           QRect(0, 0, 1200, 800));

    assert(DisplayWidget::targetRect(QSize(720, 400), QSize(360, 200),
                                     ScaleMode::Integer) ==
           QRect(0, 0, 360, 200));

    DisplayWidget widget;
    QImage frame(720, 400, QImage::Format_RGB32);
    widget.resize(1200, 800);
    widget.setFrame(frame);
    const qreal ratio = widget.devicePixelRatioF();
    assert(widget.outputPixelSize() ==
           QSize(qRound(1200 * ratio), qRound(666 * ratio)));
    widget.setScaleMode(ScaleMode::Integer);
    assert(widget.outputPixelSize() ==
           QSize(qRound(720 * ratio), qRound(400 * ratio)));
    return 0;
}
