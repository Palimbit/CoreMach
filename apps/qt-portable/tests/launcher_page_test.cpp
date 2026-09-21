/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"

#include <QApplication>
#include <QPushButton>
#include <QTextBrowser>

#include <cassert>

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QByteArray json = R"({
      "schema":"blumach-catalog-v3",
      "platforms":[
        {"id":"pcs","portable_adapter_id":"olivetti-pcs86"},
        {"id":"m15","portable_adapter_id":"olivetti-m15"}],
      "products":[
        {"id":"olivetti-pcs86","name":"Olivetti PCS 86",
         "status":"validated","platform_id":"pcs",
         "summary_key":"pcs.summary","history_key":"pcs.history"},
        {"id":"olivetti-m15","name":"Olivetti M15",
         "status":"experimental","platform_id":"m15",
         "summary_key":"m15.summary","history_key":"m15.history"}]
    })";
    const QByteArray locale = R"({
      "pcs.summary":"PCS summary", "pcs.history":"PCS history",
      "m15.summary":"M15 summary", "m15.history":"M15 history"
    })";
    PortableCatalog catalog;
    QString launched;
    assert(catalog.parse(json, locale));
    LauncherPage page(catalog, QString(),
                      [&launched](const QString &id) { launched = id; });
    assert(page.findChild<QWidget *>(QStringLiteral("machine-card-olivetti-pcs86")));
    assert(page.findChild<QWidget *>(QStringLiteral("machine-card-olivetti-m15")));
    auto *details = page.findChild<QTextBrowser *>(
        QStringLiteral("machine-details"));
    assert(details != nullptr);
    assert(details->toPlainText().contains(QStringLiteral("PCS history")));
    auto *selectM15 = page.findChild<QPushButton *>(
        QStringLiteral("select-olivetti-m15"));
    assert(selectM15 != nullptr);
    selectM15->click();
    assert(details->toPlainText().contains(QStringLiteral("M15 history")));
    auto *launch = page.findChild<QPushButton *>(
        QStringLiteral("launch-selected-machine"));
    assert(launch != nullptr && launch->isEnabled());
    launch->click();
    assert(launched == QStringLiteral("olivetti-m15"));

    PortableCatalog bundled;
    QString error;
    assert(bundled.load(&error));
    assert(error.isEmpty());
    assert(bundled.product(QStringLiteral("olivetti-pcs86")) != nullptr);
    const auto *m15 = bundled.product(QStringLiteral("olivetti-m15"));
    assert(m15 != nullptr && !m15->history.isEmpty());
    LauncherPage realPage(bundled, QString(), [](const QString &) {});
    assert(realPage.findChild<QWidget *>(
        QStringLiteral("machine-card-olivetti-pcs86")));
    assert(realPage.findChild<QWidget *>(
        QStringLiteral("machine-card-olivetti-m15")));
    return 0;
}
