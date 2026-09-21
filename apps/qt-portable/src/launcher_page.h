/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_LAUNCHER_PAGE_H
#define BLUMACH_PORTABLE_LAUNCHER_PAGE_H

#include "portable_catalog.h"

#include <QWidget>

#include <functional>

class QPushButton;
class QTextBrowser;

class LauncherPage final : public QWidget {
public:
    LauncherPage(const PortableCatalog &catalog, const QString &catalogError,
                 std::function<void(const QString &)> launch,
                 QWidget *parent = nullptr);

private:
    void select(const PortableCatalogMachine &machine);

    std::function<void(const QString &)> launch_;
    QTextBrowser *details_;
    QPushButton *launchButton_;
    QString selectedProductId_;
};

#endif
