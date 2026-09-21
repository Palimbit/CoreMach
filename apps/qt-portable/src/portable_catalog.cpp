/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_catalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
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
    const QByteArray catalog = file.readAll();
    QString language = QLocale::system().name().left(2).toLower();
    if (language != QStringLiteral("es") &&
        language != QStringLiteral("en") &&
        language != QStringLiteral("fr") &&
        language != QStringLiteral("it") &&
        language != QStringLiteral("pt"))
        language = QStringLiteral("en");
    QFile locale(QStringLiteral(":/blumach/catalog/locales/%1.json")
                     .arg(language));
    if (!locale.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = locale.errorString();
        return false;
    }
    return parse(catalog, locale.readAll(), error);
}

bool
PortableCatalog::parse(const QByteArray &json, QString *error)
{
    return parse(json, QByteArrayLiteral("{}"), error);
}

bool
PortableCatalog::parse(const QByteArray &json, const QByteArray &translations,
                       QString *error)
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

    const QJsonDocument locale = QJsonDocument::fromJson(translations,
                                                        &parseError);
    if (!locale.isObject()) {
        if (error != nullptr)
            *error = QStringLiteral("Invalid catalogue translations");
        return false;
    }
    const QJsonObject words = locale.object();
    const auto translated = [&words](const QJsonValue &key) {
        return words.value(key.toString()).toString();
    };

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
        PortableCatalogMachine item;
        item.productId = product.value(QStringLiteral("id")).toString();
        item.name = product.value(QStringLiteral("name")).toString();
        item.status = product.value(QStringLiteral("status")).toString();
        item.adapterId = adapters.value(
            product.value(QStringLiteral("platform_id")).toString());
        if (item.adapterId.isEmpty())
            continue;
        if (item.productId.isEmpty() || item.name.isEmpty() ||
            item.status.isEmpty() || productIds.contains(item.productId)) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid portable catalogue product");
            return false;
        }
        item.period = product.value(QStringLiteral("period")).toString();
        item.summary = translated(product.value(QStringLiteral("summary_key")));
        item.history = translated(product.value(QStringLiteral("history_key")));
        item.warning = translated(product.value(QStringLiteral("warning_key")));
        item.mediaResource = product.value(QStringLiteral("media"))
                                 .toObject().value(QStringLiteral("resource"))
                                 .toString();
        for (const QJsonValue &sectionValue :
             product.value(QStringLiteral("technical")).toArray()) {
            const QJsonObject source = sectionValue.toObject();
            PortableCatalogSection section;
            section.title = translated(source.value(QStringLiteral("title_key")));
            for (const QJsonValue &factValue :
                 source.value(QStringLiteral("entries")).toArray()) {
                const QJsonObject fact = factValue.toObject();
                const QString label = translated(fact.value(QStringLiteral("label_key")));
                const QString value = translated(fact.value(QStringLiteral("value_key")));
                if (!label.isEmpty() && !value.isEmpty())
                    section.facts.append({ label, value,
                        fact.value(QStringLiteral("url")).toString() });
            }
            if (!section.facts.isEmpty())
                item.sections.append(std::move(section));
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
