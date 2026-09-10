/*
 * BluMach historical machine catalog.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_CATALOG_HPP
#define QT_BLUMACH_CATALOG_HPP

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct BluMachHistoryReference {
    QString title;
    QString publisher;
    QString url;
};

struct BluMachManufacturer {
    QString id;
    QString name;
    QString descriptionKey;
    QString historyKey;
    QString historySourceUrl;
    QVector<BluMachHistoryReference> historyReferences;
};

struct BluMachFamily {
    QString id;
    QString manufacturerId;
    QString parentFamilyId;
    QString name;
    QString descriptionKey;
};

struct BluMachPlatform {
    QString id;
    QString emulatorMachineId;
    QString architecture;
};

struct BluMachFacetValue {
    QString id;
    QString labelKey;
};

struct BluMachFilterFacet {
    QString                     id;
    QString                     labelKey;
    QVector<BluMachFacetValue> values;
};

struct BluMachProduct {
    QString     id;
    QString     manufacturerId;
    QString     familyId;
    QString     platformId;
    QString     name;
    QString     summaryKey;
    QString     historyKey;
    QString     warningKey;
    QString     status;
    QString     period;
    QStringList aliases;
    QStringList tags;
    QJsonObject facets;
    QJsonArray  filterProfiles;
    QJsonObject hardware;
    QJsonObject firmware;
    QJsonObject storage;
    QJsonObject relationships;
    QJsonObject media;
    QJsonObject creation;
    QJsonObject implementation;
    QJsonArray  technical;
};

class BluMachCatalog final {
public:
    bool load(QString *errorMessage = nullptr);
    void reloadLocale();

    const QVector<BluMachManufacturer> &manufacturers() const;
    const QVector<BluMachFamily>       &families() const;
    const QVector<BluMachPlatform>     &platforms() const;
    const QVector<BluMachProduct>      &products() const;
    const QVector<BluMachFilterFacet>  &filterFacets() const;

    QString text(const QString &key) const;
    QString localeCode() const;
    QString statusText(const QString &status) const;
    QString facetLabel(const QString &facetId) const;
    QString facetValueText(const QString &facetId, const QString &valueId) const;

    const BluMachManufacturer *manufacturer(const QString &id) const;
    const BluMachFamily       *family(const QString &id) const;
    const BluMachPlatform     *platform(const QString &id) const;
    const BluMachProduct      *product(const QString &id) const;

private:
    bool loadLocale(const QString &locale, QHash<QString, QString> *target) const;

    QVector<BluMachManufacturer> m_manufacturers;
    QVector<BluMachFamily>       m_families;
    QVector<BluMachPlatform>     m_platforms;
    QVector<BluMachProduct>      m_products;
    QVector<BluMachFilterFacet>  m_filterFacets;
    QHash<QString, QString>      m_english;
    QHash<QString, QString>      m_localized;
    QString                      m_localeCode;
};

#endif
