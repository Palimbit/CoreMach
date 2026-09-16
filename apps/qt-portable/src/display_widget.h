/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_DISPLAY_WIDGET_H
#define BLUMACH_PORTABLE_DISPLAY_WIDGET_H

#include <QImage>
#include <QWidget>

#include <functional>

class QKeyEvent;

class DisplayWidget final : public QWidget {
public:
    using KeyHandler = std::function<void(QKeyEvent *, bool)>;

    explicit DisplayWidget(QWidget *parent = nullptr);
    void setFrame(const QImage &frame);
    void setKeyHandler(KeyHandler handler);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QImage frame_;
    KeyHandler keyHandler_;
};

#endif
