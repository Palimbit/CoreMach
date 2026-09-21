/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_profile_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <utility>

namespace {
constexpr auto profileFileName = "machine.blumach.json";
constexpr auto profileSchema = "blumach-machine-config-v1";

bool validId(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() &&
           uuid.toString(QUuid::WithoutBraces) == id;
}

bool validAssetReference(const QString &path)
{
    if (path.startsWith(QStringLiteral("file:"))) {
        const QUrl url(path);
        return url.isValid() && url.isLocalFile() && url.host().isEmpty() &&
               !url.toLocalFile().isEmpty();
    }
    return !path.isEmpty() && !QDir::isAbsolutePath(path) &&
           !path.contains(QLatin1Char(':'));
}

bool parseProfile(const QByteArray &json, const QFileInfo &directory,
                  PortableMachineProfile *profile)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject())
        return false;
    const QJsonObject root = document.object();
    const QJsonObject machine = root.value(QStringLiteral("machine")).toObject();
    const QJsonObject configuration = root.value(QStringLiteral("configuration")).toObject();
    if (root.value(QStringLiteral("schema")).toString() !=
            QString::fromLatin1(profileSchema) ||
        root.value(QStringLiteral("id")).toString() != directory.fileName() ||
        !validId(directory.fileName()) ||
        root.value(QStringLiteral("name")).toString().trimmed().isEmpty() ||
        root.value(QStringLiteral("catalog_product_id")).toString().isEmpty() ||
        machine.value(QStringLiteral("definition")).toString().isEmpty() ||
        machine.value(QStringLiteral("variant")).toString() != QStringLiteral("default") ||
        !configuration.value(QStringLiteral("assets")).isObject())
        return false;

    profile->id = directory.fileName();
    profile->name = root.value(QStringLiteral("name")).toString().trimmed();
    profile->productId = root.value(QStringLiteral("catalog_product_id")).toString();
    profile->adapterId = machine.value(QStringLiteral("definition")).toString();
    const QJsonObject assets = configuration.value(QStringLiteral("assets")).toObject();
    const QDir profileDir(directory.absoluteFilePath());
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        if (it.key().isEmpty() || !it.value().isString() ||
            !validAssetReference(it.value().toString()))
            return false;
        const QString reference = it.value().toString();
        profile->assets.insert(it.key(), QDir::cleanPath(
            reference.startsWith(QStringLiteral("file:")) ?
                QUrl(reference).toLocalFile() :
                profileDir.absoluteFilePath(reference)));
    }
    return true;
}
}

MachineProfileStore::MachineProfileStore(QString root)
    : root_(std::move(root))
{
    if (root_.isEmpty()) {
        const QString appData = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        if (!appData.isEmpty())
            root_ = QDir(appData).filePath(QStringLiteral("machines"));
    }
}

QString MachineProfileStore::root() const
{
    return root_;
}

bool MachineProfileStore::save(PortableMachineProfile *profile,
                               QString *error) const
{
    if (profile == nullptr || profile->name.trimmed().isEmpty() ||
        profile->productId.isEmpty() || profile->adapterId.isEmpty() ||
        root_.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("Invalid machine profile");
        return false;
    }
    const QString id = profile->id.isEmpty() ?
        QUuid::createUuid().toString(QUuid::WithoutBraces) : profile->id;
    if (!validId(id)) {
        if (error != nullptr)
            *error = QStringLiteral("Invalid machine profile ID");
        return false;
    }
    const QString directory = QDir(root_).filePath(id);
    const QDir profileDir(directory);
    QJsonObject assets;
    for (auto it = profile->assets.cbegin(); it != profile->assets.cend(); ++it) {
        if (it.key().isEmpty() || it.value().isEmpty())
            continue;
        const QFileInfo input(it.value());
        if (!input.isFile()) {
            if (error != nullptr)
                *error = QStringLiteral("Asset does not exist: %1").arg(it.value());
            return false;
        }
        const QString relative = profileDir.relativeFilePath(input.absoluteFilePath());
        assets.insert(it.key(), QDir::isAbsolutePath(relative) ?
            QUrl::fromLocalFile(input.absoluteFilePath()).toString(QUrl::FullyEncoded) :
            QDir::fromNativeSeparators(relative));
    }
    if (!QDir().mkpath(directory)) {
        if (error != nullptr)
            *error = QStringLiteral("Could not create machine directory");
        return false;
    }
    const QJsonObject document {
        { QStringLiteral("schema"), QString::fromLatin1(profileSchema) },
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), profile->name.trimmed() },
        { QStringLiteral("catalog_product_id"), profile->productId },
        { QStringLiteral("machine"), QJsonObject {
            { QStringLiteral("definition"), profile->adapterId },
            { QStringLiteral("variant"), QStringLiteral("default") }
        } },
        { QStringLiteral("configuration"), QJsonObject {
            { QStringLiteral("assets"), assets }
        } }
    };
    QSaveFile output(profileDir.filePath(QString::fromLatin1(profileFileName)));
    const QByteArray encoded = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(encoded) != encoded.size() || !output.commit()) {
        if (error != nullptr)
            *error = output.errorString();
        return false;
    }
    profile->id = id;
    profile->name = profile->name.trimmed();
    return true;
}

QVector<PortableMachineProfile> MachineProfileStore::load(QStringList *warnings) const
{
    QVector<PortableMachineProfile> profiles;
    if (root_.isEmpty())
        return profiles;
    const QDir root(root_);
    if (!root.exists())
        return profiles;
    const QFileInfoList directories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &directory : directories) {
        QFile input(QDir(directory.absoluteFilePath()).filePath(
            QString::fromLatin1(profileFileName)));
        PortableMachineProfile profile;
        if (!input.open(QIODevice::ReadOnly) ||
            !parseProfile(input.readAll(), directory, &profile)) {
            if (warnings != nullptr)
                warnings->append(QStringLiteral("Invalid machine profile: %1")
                                     .arg(directory.fileName()));
            continue;
        }
        profiles.append(std::move(profile));
    }
    return profiles;
}
