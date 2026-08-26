/*
 * BluMach historical machine catalog.
 *
 * Copyright 2026 BluMach contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QT_BLUMACH_CATALOG_HPP
#define QT_BLUMACH_CATALOG_HPP

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct BluMachManufacturer {
    QString id;
    QString name;
    QString descriptionKey;
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

struct BluMachProduct {
    QString     id;
    QString     manufacturerId;
    QString     familyId;
    QString     platformId;
    QString     name;
    QString     summaryKey;
    QString     historyKey;
    QString     status;
    QString     period;
    QStringList aliases;
    QStringList tags;
    QJsonObject hardware;
    QJsonObject firmware;
    QJsonObject storage;
    QJsonObject relationships;
};

class BluMachCatalog final {
public:
    bool load(QString *errorMessage = nullptr);
    void reloadLocale();

    const QVector<BluMachManufacturer> &manufacturers() const;
    const QVector<BluMachFamily>       &families() const;
    const QVector<BluMachPlatform>     &platforms() const;
    const QVector<BluMachProduct>      &products() const;

    QString text(const QString &key) const;
    QString localeCode() const;
    QString statusText(const QString &status) const;

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
    QHash<QString, QString>      m_english;
    QHash<QString, QString>      m_localized;
    QString                      m_localeCode;
};

#endif
