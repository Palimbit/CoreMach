/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QOpenGLWidget>

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
    assert(widget.renderer() == DisplayWidget::Renderer::Software);
    assert(widget.effect() == DisplayWidget::Effect::None);
    QImage frame(720, 400, QImage::Format_RGB32);
    frame.fill(QColor(220, 230, 240));
    widget.resize(1200, 800);
    widget.setFrame(frame);
    const qreal ratio = widget.devicePixelRatioF();
    assert(widget.outputPixelSize() ==
           QSize(qRound(1200 * ratio), qRound(666 * ratio)));
    widget.setScaleMode(ScaleMode::Integer);
    assert(widget.outputPixelSize() ==
           QSize(qRound(720 * ratio), qRound(400 * ratio)));
    widget.setRenderer(DisplayWidget::Renderer::Software);
    assert(widget.renderer() == DisplayWidget::Renderer::Software);
    widget.show();
    application.processEvents();
    QImage clean(widget.size(), QImage::Format_RGB32);
    clean.fill(Qt::magenta);
    widget.render(&clean);

    widget.setEffect(DisplayWidget::Effect::Crt);
    assert(widget.effect() == DisplayWidget::Effect::Crt);
    application.processEvents();
    QImage crt(widget.size(), QImage::Format_RGB32);
    crt.fill(Qt::magenta);
    widget.render(&crt);

    assert(clean != crt);
    assert(widget.frame() == frame);
    assert(clean.pixelColor(0, 0) == QColor(Qt::black));
    assert(crt.pixelColor(0, 0) == QColor(Qt::black));
    widget.setEffect(DisplayWidget::Effect::None);
    QImage restored(widget.size(), QImage::Format_RGB32);
    widget.render(&restored);
    assert(restored == clean);

    // Explicit local GPU check; the default suite also runs without a display.
    if (application.arguments().contains(QStringLiteral("--opengl"))) {
        widget.setRenderer(DisplayWidget::Renderer::OpenGL);
        application.processEvents();
        auto *surface = widget.findChild<QOpenGLWidget *>();
        assert(surface != nullptr);
        assert(surface->isValid());
        const QImage gpuClean = surface->grabFramebuffer();
        assert(!gpuClean.isNull());
        assert(gpuClean.pixelColor(gpuClean.width() / 2,
                                   gpuClean.height() / 2) == frame.pixelColor(0, 0));
        widget.setEffect(DisplayWidget::Effect::Crt);
        application.processEvents();
        const QImage gpuCrt = surface->grabFramebuffer();
        assert(gpuClean != gpuCrt);
        assert(widget.frame() == frame);
        widget.setRenderer(DisplayWidget::Renderer::Software);
        assert(widget.findChild<QOpenGLWidget *>() == nullptr);
    }
    return 0;
}
