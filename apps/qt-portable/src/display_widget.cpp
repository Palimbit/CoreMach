/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "display_widget.h"

#include <QKeyEvent>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QStackedLayout>

#include <algorithm>
#include <cmath>

class SoftwareDisplayCanvas final : public QWidget {
public:
    explicit SoftwareDisplayCanvas(DisplayWidget *owner)
        : QWidget(owner), owner_(owner)
    {
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        (void) event;
        QPainter painter(this);
        owner_->paintPresentation(painter, rect());
    }

private:
    DisplayWidget *owner_;
};

class OpenGLDisplayCanvas final : public QOpenGLWidget {
public:
    explicit OpenGLDisplayCanvas(DisplayWidget *owner)
        : QOpenGLWidget(owner), owner_(owner)
    {
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    }

protected:
    void paintGL() override
    {
        QPainter painter(this);
        owner_->paintPresentation(painter, rect());
    }

private:
    DisplayWidget *owner_;
};

DisplayWidget::DisplayWidget(QWidget *parent)
    : QWidget(parent), surfaces_(new QStackedLayout(this)),
      canvas_(new SoftwareDisplayCanvas(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(720, 400);
    setAutoFillBackground(false);
    surfaces_->setContentsMargins(0, 0, 0, 0);
    surfaces_->addWidget(canvas_);
}

void
DisplayWidget::setFrame(const QImage &frame)
{
    frame_ = frame;
    canvas_->update();
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

QSize
DisplayWidget::outputPixelSize() const
{
    if (frame_.isNull())
        return {};
    const QSize logical = targetRect(frame_.size(), size(), scaleMode_).size();
    const qreal ratio = devicePixelRatioF();
    return { qRound(logical.width() * ratio),
             qRound(logical.height() * ratio) };
}

void
DisplayWidget::setScaleMode(ScaleMode mode)
{
    if (scaleMode_ == mode)
        return;
    scaleMode_ = mode;
    canvas_->update();
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
    canvas_->update();
}

bool
DisplayWidget::smoothScaling() const
{
    return smoothScaling_;
}

void
DisplayWidget::setRenderer(Renderer renderer)
{
    if (renderer_ == renderer)
        return;
    renderer_ = renderer;
    surfaces_->removeWidget(canvas_);
    delete canvas_;
    canvas_ = renderer == Renderer::OpenGL ?
        static_cast<QWidget *>(new OpenGLDisplayCanvas(this)) :
        static_cast<QWidget *>(new SoftwareDisplayCanvas(this));
    surfaces_->addWidget(canvas_);
    canvas_->update();
}

DisplayWidget::Renderer
DisplayWidget::renderer() const
{
    return renderer_;
}

void
DisplayWidget::setEffect(Effect effect)
{
    if (effect_ == effect)
        return;
    effect_ = effect;
    canvas_->update();
}

DisplayWidget::Effect
DisplayWidget::effect() const
{
    return effect_;
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
DisplayWidget::paintPresentation(QPainter &painter, const QRect &viewport) const
{
    painter.fillRect(viewport, Qt::black);
    if (frame_.isNull())
        return;
    const QRect target = targetRect(frame_.size(), viewport.size(), scaleMode_);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling_);
    painter.drawImage(target, frame_);
    if (effect_ != Effect::Crt)
        return;

    painter.save();
    painter.setClipRect(target);
    const qreal sourceLineHeight = static_cast<qreal>(target.height()) /
                                   static_cast<qreal>(frame_.height());
    const int scanlineStep = std::max(2, qRound(sourceLineHeight * 2.0));
    painter.setPen(QPen(QColor(0, 0, 0, 52), 1));
    for (int y = target.top() + scanlineStep - 1; y <= target.bottom();
         y += scanlineStep)
        painter.drawLine(target.left(), y, target.right(), y);

    const QPointF center = target.center();
    const qreal radius = std::hypot(static_cast<qreal>(target.width()),
                                    static_cast<qreal>(target.height())) * 0.52;
    QRadialGradient vignette(center, radius);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.72, QColor(0, 0, 0, 0));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 105));
    painter.fillRect(target, vignette);
    painter.restore();
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
