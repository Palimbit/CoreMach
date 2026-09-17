/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_CATALOG_H
#define BLUMACH_PORTABLE_CATALOG_H

#include <QByteArray>
#include <QString>
#include <QVector>

struct PortableCatalogMachine {
    QString productId;
    QString name;
    QString status;
    QString adapterId;
};

class PortableCatalog final {
public:
    bool load(QString *error = nullptr);
    bool parse(const QByteArray &json, QString *error = nullptr);

    const QVector<PortableCatalogMachine> &machines() const;
    const PortableCatalogMachine *product(const QString &productId) const;

private:
    QVector<PortableCatalogMachine> machines_;
};

#endif
