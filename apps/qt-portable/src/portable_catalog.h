/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_CATALOG_H
#define BLUMACH_PORTABLE_CATALOG_H

#include <QByteArray>
#include <QString>
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

struct PortableCatalogMachine {
    QString productId;
    QString name;
    QString status;
    QString adapterId;
    QString period;
    QString summary;
    QString history;
    QString warning;
    QString mediaResource;
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

private:
    QVector<PortableCatalogMachine> machines_;
};

#endif
