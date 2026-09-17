/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_DISPLAY_WIDGET_H
#define BLUMACH_PORTABLE_DISPLAY_WIDGET_H

#include <QImage>
#include <QRect>
#include <QWidget>

#include <functional>

class QKeyEvent;

class DisplayWidget final : public QWidget {
public:
    enum class ScaleMode {
        Fit,
        Integer,
        CorrectedFourThree,
        Stretch
    };

    using KeyHandler = std::function<void(QKeyEvent *, bool)>;

    explicit DisplayWidget(QWidget *parent = nullptr);
    void setFrame(const QImage &frame);
    QImage frame() const;
    bool hasFrame() const;
    void setScaleMode(ScaleMode mode);
    ScaleMode scaleMode() const;
    void setSmoothScaling(bool enabled);
    bool smoothScaling() const;
    void setKeyHandler(KeyHandler handler);

    static QRect targetRect(const QSize &frameSize, const QSize &viewportSize,
                            ScaleMode mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QImage frame_;
    KeyHandler keyHandler_;
    ScaleMode scaleMode_ = ScaleMode::Fit;
    bool smoothScaling_ = false;
};

#endif
