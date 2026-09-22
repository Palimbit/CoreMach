/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_window.h"

#include <blumach/engine/version.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QHash>
#include <QMessageBox>
#include <QTimer>

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BluMach Portable"));
    QApplication::setApplicationVersion(QStringLiteral(BM_ENGINE_VERSION));
    QCommandLineParser parser;
    const QCommandLineOption machineOption(
        QStringList { QStringLiteral("machine") }, QStringLiteral("Machine ID"),
        QStringLiteral("id"));
    const QCommandLineOption productOption(
        QStringList { QStringLiteral("product") },
        QStringLiteral("Catalogue product ID"), QStringLiteral("id"));
    const QCommandLineOption assetOption(
        QStringList { QStringLiteral("asset") },
        QStringLiteral("Asset binding in role=path form; repeatable"),
        QStringLiteral("role=path"));
    parser.setApplicationDescription(QStringLiteral("BluMach portable Qt frontend"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(machineOption);
    parser.addOption(productOption);
    parser.addOption(assetOption);
    parser.process(application);

    PortableWindow window;
    window.show();
    const QString machine = parser.value(machineOption);
    const QString product = parser.value(productOption);
    if (!machine.isEmpty() && !product.isEmpty()) {
        QMessageBox::critical(&window, QStringLiteral("Invalid selection"),
                              QStringLiteral("Use either --machine or --product."));
        return 2;
    }
    if (!machine.isEmpty() || !product.isEmpty()) {
        QHash<QString, QString> paths;
        const QStringList values = parser.values(assetOption);
        for (const QString &value : values) {
            const qsizetype separator = value.indexOf(QLatin1Char('='));
            if (separator <= 0) {
                QMessageBox::critical(&window, QStringLiteral("Invalid asset"),
                                      QStringLiteral("Use --asset role=path."));
                return 2;
            }
            paths.insert(value.left(separator), value.mid(separator + 1));
        }
        QTimer::singleShot(0, &window, [&window, machine, product, paths] {
            if (!product.isEmpty())
                (void) window.openInitialProduct(product, paths);
            else
                (void) window.openInitial(machine, paths);
        });
    }
    return application.exec();
}
