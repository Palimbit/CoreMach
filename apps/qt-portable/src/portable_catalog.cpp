/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_catalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <utility>

bool
PortableCatalog::load(QString *error)
{
    QFile file(QStringLiteral(":/blumach/catalog/catalog.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    return parse(file.readAll(), error);
}

bool
PortableCatalog::parse(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (error != nullptr)
            *error = parseError.error == QJsonParseError::NoError ?
                     QStringLiteral("Catalogue root is not an object") :
                     parseError.errorString();
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() !=
        QStringLiteral("blumach-catalog-v3")) {
        if (error != nullptr)
            *error = QStringLiteral("Unsupported catalogue schema");
        return false;
    }

    QHash<QString, QString> adapters;
    for (const QJsonValue &value :
         root.value(QStringLiteral("platforms")).toArray()) {
        const QJsonObject platform = value.toObject();
        const QString id = platform.value(QStringLiteral("id")).toString();
        const QString adapter =
            platform.value(QStringLiteral("portable_adapter_id")).toString();
        if (!id.isEmpty() && !adapter.isEmpty())
            adapters.insert(id, adapter);
    }

    QVector<PortableCatalogMachine> parsed;
    QSet<QString> productIds;
    for (const QJsonValue &value :
         root.value(QStringLiteral("products")).toArray()) {
        const QJsonObject product = value.toObject();
        PortableCatalogMachine item {
            product.value(QStringLiteral("id")).toString(),
            product.value(QStringLiteral("name")).toString(),
            product.value(QStringLiteral("status")).toString(),
            adapters.value(
                product.value(QStringLiteral("platform_id")).toString())
        };
        if (item.adapterId.isEmpty())
            continue;
        if (item.productId.isEmpty() || item.name.isEmpty() ||
            item.status.isEmpty() || productIds.contains(item.productId)) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid portable catalogue product");
            return false;
        }
        productIds.insert(item.productId);
        parsed.append(std::move(item));
    }
    machines_ = std::move(parsed);
    return true;
}

const QVector<PortableCatalogMachine> &
PortableCatalog::machines() const
{
    return machines_;
}

const PortableCatalogMachine *
PortableCatalog::product(const QString &productId) const
{
    for (const PortableCatalogMachine &machine : machines_) {
        if (machine.productId == productId)
            return &machine;
    }
    return nullptr;
}
