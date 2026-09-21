/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_CATALOG_H
#define BLUMACH_PORTABLE_CATALOG_H

#include <QByteArray>
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
    QVector<PortableCatalogFact> facts;
};

struct PortableCatalogReference {
    QString title;
    QString publisher;
    QString url;
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
    QString adapterId;
    QString period;
    QString summary;
    QString history;
    QString warning;
    QString mediaResource;
    QStringList aliases;
    QStringList tags;
    QVector<PortableCatalogSection> sections;
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
