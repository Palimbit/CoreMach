/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_profile_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <cassert>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString firmware = QDir(temporary.path()).filePath(
        QStringLiteral("local-firmware.bin"));
    QFile asset(firmware);
    assert(asset.open(QIODevice::WriteOnly));
    assert(asset.write("test", 4) == 4);
    asset.close();

    MachineProfileStore store(QDir(temporary.path()).filePath(
        QStringLiteral("profiles")));
    PortableMachineProfile profile;
    profile.name = QStringLiteral("My PCS 86");
    profile.productId = QStringLiteral("olivetti-pcs86");
    profile.adapterId = QStringLiteral("olivetti-pcs86");
    profile.assets.insert(QStringLiteral("bios"), firmware);
    QString error;
    assert(store.save(&profile, &error));
    assert(error.isEmpty());
    assert(!profile.id.isEmpty());

    QFile config(QDir(store.root()).filePath(profile.id +
        QStringLiteral("/machine.blumach.json")));
    assert(config.open(QIODevice::ReadOnly));
    const QByteArray serialized = config.readAll();
    const QJsonObject json = QJsonDocument::fromJson(serialized).object();
    assert(json.value(QStringLiteral("schema")).toString() ==
           QStringLiteral("blumach-machine-config-v1"));
    const QString path = json.value(QStringLiteral("configuration")).toObject()
        .value(QStringLiteral("assets")).toObject()
        .value(QStringLiteral("bios")).toString();
    assert(!QDir::isAbsolutePath(path));
    assert(!serialized.contains("test"));
    config.close();

    QStringList warnings;
    QVector<PortableMachineProfile> profiles = store.load(&warnings);
    assert(warnings.isEmpty() && profiles.size() == 1);
    assert(profiles[0].assets.value(QStringLiteral("bios")) == firmware);
    assert(profiles[0].name == profile.name);
    profile.name = QStringLiteral("Renamed PCS 86");
    assert(store.save(&profile, &error));
    profiles = store.load(&warnings);
    assert(profiles.size() == 1);
    assert(profiles[0].name == profile.name);
    assert(!store.save(nullptr, &error));

    PortableMachineProfile invalid = profile;
    invalid.assets.insert(QStringLiteral("bios"), QStringLiteral("missing.bin"));
    assert(!store.save(&invalid, &error));
    assert(store.load().size() == 1);

    QJsonObject absoluteDocument = json;
    QJsonObject configuration = absoluteDocument.value(
        QStringLiteral("configuration")).toObject();
    QJsonObject assets = configuration.value(QStringLiteral("assets")).toObject();
    assets.insert(QStringLiteral("bios"),
        QUrl::fromLocalFile(firmware).toString(QUrl::FullyEncoded));
    configuration.insert(QStringLiteral("assets"), assets);
    absoluteDocument.insert(QStringLiteral("configuration"), configuration);
    assert(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray withFileReference = QJsonDocument(absoluteDocument).toJson();
    assert(config.write(withFileReference) == withFileReference.size());
    config.close();
    warnings.clear();
    profiles = store.load(&warnings);
    assert(warnings.isEmpty() && profiles.size() == 1);
    assert(profiles[0].assets.value(QStringLiteral("bios")) == firmware);

    assert(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(config.write("{}", 2) == 2);
    config.close();
    warnings.clear();
    assert(store.load(&warnings).isEmpty());
    assert(warnings.size() == 1);
    assert(asset.open(QIODevice::ReadOnly));
    assert(asset.readAll() == QByteArrayLiteral("test"));
    return 0;
}
