/*
 * BluMach historical machine catalog.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_catalog.hpp"
#include "qt_preferences.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

extern "C" {
extern int lang_id;
}

namespace {
QJsonDocument
readJson(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Unable to open %1: %2").arg(path, file.errorString());
        return {};
    }

    QJsonParseError parseError;
    const auto      document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError && errorMessage)
        *errorMessage = QStringLiteral("Invalid JSON in %1: %2").arg(path, parseError.errorString());
    return document;
}

QStringList
stringArray(const QJsonValue &value)
{
    QStringList result;
    for (const auto &item : value.toArray())
        result.append(item.toString());
    return result;
}
} // namespace

bool
BluMachCatalog::load(QString *errorMessage)
{
    const auto document = readJson(QStringLiteral(":/blumach/catalog/catalog.json"), errorMessage);
    if (!document.isObject())
        return false;

    m_manufacturers.clear();
    m_families.clear();
    m_platforms.clear();
    m_products.clear();
    m_filterFacets.clear();

    const auto root = document.object();
    for (const auto &value : root.value(QStringLiteral("manufacturers")).toArray()) {
        const auto object = value.toObject();
        m_manufacturers.append({ object.value(QStringLiteral("id")).toString(),
                                 object.value(QStringLiteral("name")).toString(),
                                 object.value(QStringLiteral("description_key")).toString(),
                                 object.value(QStringLiteral("history_key")).toString(),
                                 object.value(QStringLiteral("history_source_url")).toString() });
        for (const auto &entry : object.value(QStringLiteral("history_references")).toArray()) {
            const auto reference = entry.toObject();
            m_manufacturers.last().historyReferences.append({
                reference.value(QStringLiteral("title")).toString(),
                reference.value(QStringLiteral("publisher")).toString(),
                reference.value(QStringLiteral("url")).toString() });
        }
    }
    for (const auto &value : root.value(QStringLiteral("families")).toArray()) {
        const auto object = value.toObject();
        m_families.append({ object.value(QStringLiteral("id")).toString(),
                            object.value(QStringLiteral("manufacturer_id")).toString(),
                            object.value(QStringLiteral("parent_family_id")).toString(),
                            object.value(QStringLiteral("name")).toString(),
                            object.value(QStringLiteral("description_key")).toString() });
    }
    for (const auto &value : root.value(QStringLiteral("platforms")).toArray()) {
        const auto object = value.toObject();
        m_platforms.append({ object.value(QStringLiteral("id")).toString(),
                             object.value(QStringLiteral("emulator_machine_id")).toString(),
                             object.value(QStringLiteral("architecture")).toString() });
    }
    for (const auto &value : root.value(QStringLiteral("filter_facets")).toArray()) {
        const auto object = value.toObject();
        BluMachFilterFacet facet;
        facet.id       = object.value(QStringLiteral("id")).toString();
        facet.labelKey = object.value(QStringLiteral("label_key")).toString();
        for (const auto &entry : object.value(QStringLiteral("values")).toArray()) {
            const auto option = entry.toObject();
            facet.values.append({ option.value(QStringLiteral("id")).toString(),
                                  option.value(QStringLiteral("label_key")).toString() });
        }
        m_filterFacets.append(facet);
    }
    for (const auto &value : root.value(QStringLiteral("products")).toArray()) {
        const auto object = value.toObject();
        BluMachProduct product;
        product.id                 = object.value(QStringLiteral("id")).toString();
        product.manufacturerId     = object.value(QStringLiteral("manufacturer_id")).toString();
        product.familyId           = object.value(QStringLiteral("family_id")).toString();
        product.platformId         = object.value(QStringLiteral("platform_id")).toString();
        product.name               = object.value(QStringLiteral("name")).toString();
        product.summaryKey         = object.value(QStringLiteral("summary_key")).toString();
        product.historyKey         = object.value(QStringLiteral("history_key")).toString();
        product.warningKey         = object.value(QStringLiteral("warning_key")).toString();
        product.status             = object.value(QStringLiteral("status")).toString();
        product.period             = object.value(QStringLiteral("period")).toString();
        product.aliases            = stringArray(object.value(QStringLiteral("aliases")));
        product.tags               = stringArray(object.value(QStringLiteral("tags")));
        product.facets             = object.value(QStringLiteral("facets")).toObject();
        product.filterProfiles     = object.value(QStringLiteral("filter_profiles")).toArray();
        product.hardware           = object.value(QStringLiteral("hardware")).toObject();
        product.firmware           = object.value(QStringLiteral("firmware")).toObject();
        product.storage            = object.value(QStringLiteral("storage")).toObject();
        product.relationships      = object.value(QStringLiteral("relationships")).toObject();
        product.media              = object.value(QStringLiteral("media")).toObject();
        product.creation           = object.value(QStringLiteral("creation")).toObject();
        product.implementation     = object.value(QStringLiteral("implementation")).toObject();
        product.technical          = object.value(QStringLiteral("technical")).toArray();
        m_products.append(product);
    }

    reloadLocale();
    return true;
}

void
BluMachCatalog::reloadLocale()
{
    m_english.clear();
    m_localized.clear();
    loadLocale(QStringLiteral("en"), &m_english);

    QString requested = Preferences::languageIdToCode(lang_id);
    if (requested == QStringLiteral("system")) {
        const auto languages = QLocale::system().uiLanguages();
        requested            = languages.isEmpty() ? QStringLiteral("en") : languages.constFirst();
    }
    requested.replace('_', '-');
    m_localeCode = requested.section('-', 0, 0).toLower();
    if (m_localeCode != QStringLiteral("en"))
        loadLocale(m_localeCode, &m_localized);
}

bool
BluMachCatalog::loadLocale(const QString &locale, QHash<QString, QString> *target) const
{
    const auto document = readJson(QStringLiteral(":/blumach/catalog/locales/%1.json").arg(locale), nullptr);
    if (!document.isObject())
        return false;
    const auto translations = document.object();
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it)
        target->insert(it.key(), it.value().toString());
    return true;
}

const QVector<BluMachManufacturer> &BluMachCatalog::manufacturers() const { return m_manufacturers; }
const QVector<BluMachFamily> &BluMachCatalog::families() const { return m_families; }
const QVector<BluMachPlatform> &BluMachCatalog::platforms() const { return m_platforms; }
const QVector<BluMachProduct> &BluMachCatalog::products() const { return m_products; }
const QVector<BluMachFilterFacet> &BluMachCatalog::filterFacets() const { return m_filterFacets; }

QString
BluMachCatalog::text(const QString &key) const
{
    if (m_localized.contains(key))
        return m_localized.value(key);
    return m_english.value(key, key);
}

QString BluMachCatalog::localeCode() const { return m_localeCode; }

QString
BluMachCatalog::statusText(const QString &status) const
{
    return text(QStringLiteral("status.%1").arg(status));
}

QString
BluMachCatalog::facetLabel(const QString &facetId) const
{
    for (const auto &facet : m_filterFacets)
        if (facet.id == facetId)
            return text(facet.labelKey);
    return facetId;
}

QString
BluMachCatalog::facetValueText(const QString &facetId, const QString &valueId) const
{
    for (const auto &facet : m_filterFacets) {
        if (facet.id != facetId)
            continue;
        for (const auto &value : facet.values)
            if (value.id == valueId)
                return text(value.labelKey);
    }
    return valueId;
}

const BluMachManufacturer *
BluMachCatalog::manufacturer(const QString &id) const
{
    for (const auto &item : m_manufacturers)
        if (item.id == id)
            return &item;
    return nullptr;
}

const BluMachFamily *
BluMachCatalog::family(const QString &id) const
{
    for (const auto &item : m_families)
        if (item.id == id)
            return &item;
    return nullptr;
}

const BluMachPlatform *
BluMachCatalog::platform(const QString &id) const
{
    for (const auto &item : m_platforms)
        if (item.id == id)
            return &item;
    return nullptr;
}

const BluMachProduct *
BluMachCatalog::product(const QString &id) const
{
    for (const auto &item : m_products)
        if (item.id == id)
            return &item;
    return nullptr;
}
