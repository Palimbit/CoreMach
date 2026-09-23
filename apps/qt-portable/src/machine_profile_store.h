/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_MACHINE_PROFILE_STORE_H
#define BLUMACH_PORTABLE_MACHINE_PROFILE_STORE_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct PortableMachineProfile {
    QString id;
    QString name;
    QString productId;
    QString adapterId;
    QHash<QString, QString> assets;
    QHash<QString, quint32> options;
};

class MachineProfileStore final {
public:
    explicit MachineProfileStore(QString root = QString());

    bool save(PortableMachineProfile *profile, QString *error = nullptr) const;
    QVector<PortableMachineProfile> load(QStringList *warnings = nullptr) const;
    QString root() const;

private:
    QString root_;
};

#endif
