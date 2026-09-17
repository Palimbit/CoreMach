/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <cassert>

int
main()
{
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
    return 0;
}
