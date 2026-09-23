/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_CATALOG_H
#define BLUMACH_PORTABLE_CATALOG_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct PortableCatalogFact {
    QString label;
    QString value;
    QString url;
};

struct PortableCatalogSection {
    QString title;
    QString evidence;
    QString description;
    bool sources = false;
    QVector<PortableCatalogFact> facts;
};

/* Recognition data is catalogue metadata, never a packaged asset. */
struct PortableResourceIdentity {
    QString name;
    QString sha256;
    qint64 size = 0;
    bool preferred = false;
};

struct PortableResourceRole {
    QString role;
    QString folder;
    QVector<PortableResourceIdentity> known;
};

struct PortableCatalogReference {
    QString title;
    QString publisher;
    QString url;
};

struct PortableCatalogCreationChoice {
    QString id;
    QString label;
    QString status;
    quint32 portableValue = 0;
    bool available = false;
    QHash<QString, quint32> setOptions;
    QStringList requiredAssets;
    QStringList generatedAssets;
    QStringList forbiddenAssets;
    QString mediaRole;
    qint64 mediaMaxBytes = 0;
};

struct PortableCatalogCreationField {
    QString id;
    QString label;
    QString help;
    QString defaultId;
    QString portableOption;
    QString portableKind;
    bool advanced = false;
    bool quick = false;
    QVector<PortableCatalogCreationChoice> choices;
};

struct PortableCatalogExpansionSlot {
    QString id;
    QString bus;
    QString length;
    bool modelled = false;
};

struct PortableCatalogManufacturer {
    QString id;
    QString name;
    QString description;
    QString history;
    QVector<PortableCatalogReference> references;
};

struct PortableCatalogFamily {
    QString id;
    QString manufacturerId;
    QString name;
    QString description;
};

struct PortableCatalogMachine {
    QString productId;
    QString manufacturerId;
    QString familyId;
    QString name;
    QString status;
    QString statusLabel;
    QString adapterId;
    QString architecture;
    QString period;
    QString summary;
    QString history;
    QString warning;
    QString mediaResource;
    QString mediaLabel;
    QString implementationDocument;
    QString implementationLanguage;
    QString commercialConfiguration;
    QStringList aliases;
    QStringList tags;
    QVector<PortableCatalogFact> hardware;
    QVector<PortableCatalogSection> sections;
    QVector<PortableCatalogCreationField> configurationFields;
    QVector<PortableCatalogExpansionSlot> expansionSlots;
    QVector<PortableResourceRole> resources;
};

class PortableCatalog final {
public:
    bool load(QString *error = nullptr);
    bool parse(const QByteArray &json, QString *error = nullptr);
    bool parse(const QByteArray &json, const QByteArray &translations,
               QString *error = nullptr);

    const QVector<PortableCatalogMachine> &machines() const;
    const PortableCatalogMachine *product(const QString &productId) const;
    const PortableCatalogManufacturer *manufacturer(const QString &id) const;
    const PortableCatalogFamily *family(const QString &id) const;

private:
    QVector<PortableCatalogMachine> machines_;
    QVector<PortableCatalogManufacturer> manufacturers_;
    QVector<PortableCatalogFamily> families_;
};

#endif
