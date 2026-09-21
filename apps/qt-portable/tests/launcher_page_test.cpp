/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_page.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <cassert>

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QByteArray json = R"({
      "schema":"blumach-catalog-v3",
      "manufacturers":[{"id":"olivetti","name":"Olivetti",
        "history_key":"olivetti.history"}],
      "families":[{"id":"pcs","manufacturer_id":"olivetti",
        "name":"PCS","description_key":"pcs.family"},
        {"id":"m15","manufacturer_id":"olivetti","name":"M15"}],
      "platforms":[
        {"id":"pcs","portable_adapter_id":"olivetti-pcs86"},
        {"id":"m15","portable_adapter_id":"olivetti-m15"}],
      "products":[
        {"id":"olivetti-pcs86","name":"Olivetti PCS 86",
         "manufacturer_id":"olivetti","family_id":"pcs",
         "status":"validated","platform_id":"pcs",
         "summary_key":"pcs.summary","history_key":"pcs.history"},
        {"id":"olivetti-m15","name":"Olivetti M15",
         "manufacturer_id":"olivetti","family_id":"m15",
         "status":"experimental","platform_id":"m15",
         "summary_key":"m15.summary","history_key":"m15.history"},
        {"id":"pending","name":"Future PC","manufacturer_id":"olivetti",
         "family_id":"pcs","status":"research","platform_id":"none"}]
    })";
    const QByteArray locale = R"({
      "pcs.summary":"PCS summary", "pcs.history":"PCS history",
      "m15.summary":"M15 summary", "m15.history":"M15 history",
      "olivetti.history":"Manufacturer history", "pcs.family":"Family history"
    })";
    PortableCatalog catalog;
    QString launched;
    QString openedSaved;
    assert(catalog.parse(json, locale));
    LauncherPage page(catalog, QString(),
                      [&launched](const QString &id) { launched = id; },
                      [&openedSaved](const QString &id) { openedSaved = id; });
    PortableMachineProfile saved;
    saved.id = QStringLiteral("profile-one");
    saved.name = QStringLiteral("My M15");
    saved.productId = QStringLiteral("olivetti-m15");
    saved.adapterId = QStringLiteral("olivetti-m15");
    page.setProfiles({ saved });
    auto *savedList = page.findChild<QListWidget *>(QStringLiteral("saved-machines"));
    assert(savedList != nullptr && savedList->count() == 1);
    savedList->setCurrentRow(0);
    auto *openSaved = page.findChild<QPushButton *>(
        QStringLiteral("open-saved-machine"));
    assert(openSaved != nullptr && openSaved->isEnabled());
    openSaved->click();
    assert(openedSaved == QStringLiteral("profile-one"));
    auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("catalog-tree"));
    assert(tree != nullptr && tree->topLevelItemCount() == 1);
    const auto findProduct = [tree](const QString &id) {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->data(0, Qt::UserRole).toString() == id)
                return *it;
        }
        return static_cast<QTreeWidgetItem *>(nullptr);
    };
    assert(findProduct(QStringLiteral("olivetti-pcs86")) != nullptr);
    assert(findProduct(QStringLiteral("olivetti-m15")) != nullptr);
    assert(findProduct(QStringLiteral("pending")) != nullptr);
    auto *details = page.findChild<QTextBrowser *>(
        QStringLiteral("machine-details"));
    assert(details != nullptr);
    assert(details->toPlainText().contains(QStringLiteral("PCS history")));
    tree->setCurrentItem(findProduct(QStringLiteral("olivetti-m15")));
    assert(details->toPlainText().contains(QStringLiteral("M15 history")));
    auto *context = page.findChild<QTextBrowser *>(
        QStringLiteral("machine-context"));
    assert(context->toPlainText().contains(QStringLiteral("Manufacturer history")));
    auto *launch = page.findChild<QPushButton *>(
        QStringLiteral("launch-selected-machine"));
    assert(launch != nullptr && launch->isEnabled());
    launch->click();
    assert(launched == QStringLiteral("olivetti-m15"));
    tree->setCurrentItem(tree->topLevelItem(0));
    assert(!launch->isEnabled());
    assert(details->toPlainText().contains(QStringLiteral("Manufacturer history")));
    tree->setCurrentItem(findProduct(QStringLiteral("pending")));
    assert(!launch->isEnabled());
    auto *search = page.findChild<QLineEdit *>(QStringLiteral("catalog-search"));
    assert(search != nullptr);
    search->setText(QStringLiteral("M15"));
    assert(findProduct(QStringLiteral("olivetti-m15"))->isHidden() == false);
    assert(findProduct(QStringLiteral("pending"))->isHidden());
    search->setText(QStringLiteral("no matching machine"));
    assert(!launch->isEnabled());
    search->clear();
    assert(findProduct(QStringLiteral("pending"))->isHidden() == false);

    PortableCatalog bundled;
    QString error;
    assert(bundled.load(&error));
    assert(error.isEmpty());
    assert(bundled.product(QStringLiteral("olivetti-pcs86")) != nullptr);
    const auto *m15 = bundled.product(QStringLiteral("olivetti-m15"));
    assert(m15 != nullptr && !m15->history.isEmpty());
    LauncherPage realPage(bundled, QString(), [](const QString &) {},
                          [](const QString &) {});
    auto *realTree = realPage.findChild<QTreeWidget *>(QStringLiteral("catalog-tree"));
    assert(realTree != nullptr && bundled.machines().size() >= 30);
    return 0;
}
