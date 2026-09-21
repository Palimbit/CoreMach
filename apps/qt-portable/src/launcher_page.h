/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_LAUNCHER_PAGE_H
#define BLUMACH_PORTABLE_LAUNCHER_PAGE_H

#include "portable_catalog.h"

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTreeWidget;

class LauncherPage final : public QWidget {
public:
    LauncherPage(const PortableCatalog &catalog, const QString &catalogError,
                 std::function<void(const QString &)> launch,
                 QWidget *parent = nullptr);

private:
    void filter(const QString &query);
    void select(const PortableCatalogMachine &machine);
    void selectProduct(const QString &productId);

    const PortableCatalog &catalog_;
    std::function<void(const QString &)> launch_;
    QLineEdit *search_;
    QTreeWidget *tree_;
    QLabel *image_;
    QLabel *title_;
    QLabel *availability_;
    QTextBrowser *details_;
    QTextBrowser *technical_;
    QTextBrowser *context_;
    QTextBrowser *sources_;
    QPushButton *launchButton_;
    QString selectedProductId_;
};

#endif
