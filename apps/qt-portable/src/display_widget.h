/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_DISPLAY_WIDGET_H
#define BLUMACH_PORTABLE_DISPLAY_WIDGET_H

#include <QImage>
#include <QRect>
#include <QWidget>

#include <cstdint>
#include <functional>

class QKeyEvent;
class QPainter;
class QStackedLayout;
class OpenGLDisplayCanvas;
class SoftwareDisplayCanvas;

class DisplayWidget final : public QWidget {
public:
    enum class ScaleMode {
        Fit,
        Integer,
        CorrectedFourThree,
        Stretch
    };
    enum class Renderer {
        Software,
        OpenGL
    };
    enum class Effect {
        None,
        Crt
    };

    using KeyHandler = std::function<void(QKeyEvent *, bool)>;

    explicit DisplayWidget(QWidget *parent = nullptr);
    void setFrame(const QImage &frame, uint64_t traceFrame = 0U);
    QImage frame() const;
    bool hasFrame() const;
    QSize outputPixelSize() const;
    void setScaleMode(ScaleMode mode);
    ScaleMode scaleMode() const;
    void setSmoothScaling(bool enabled);
    bool smoothScaling() const;
    void setRenderer(Renderer renderer);
    Renderer renderer() const;
    void setEffect(Effect effect);
    Effect effect() const;
    void setKeyHandler(KeyHandler handler);

    static QRect targetRect(const QSize &frameSize, const QSize &viewportSize,
                            ScaleMode mode);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    friend class OpenGLDisplayCanvas;
    friend class SoftwareDisplayCanvas;
    void paintPresentation(QPainter &painter, const QRect &viewport) const;
    QImage frame_;
    uint64_t traceFrame_ = 0U;
    KeyHandler keyHandler_;
    QStackedLayout *surfaces_;
    QWidget *canvas_;
    ScaleMode scaleMode_ = ScaleMode::Fit;
    bool smoothScaling_ = false;
    Renderer renderer_ = Renderer::Software;
    Effect effect_ = Effect::None;
};

#endif
