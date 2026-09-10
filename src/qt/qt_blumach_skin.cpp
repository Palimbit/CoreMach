/*
 * BluMach local catalogue skin packages.
 *
 * Author: rtzor
 * Copyright 2026 rtzor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qt_blumach_skin.hpp"
#include "qt_vmmanager_config.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr auto SkinSchema = "blumach-catalog-skin-v1";
constexpr qint64 MaximumManifestSize = 128 * 1024;
constexpr qint64 MaximumImageSize = 2 * 1024 * 1024;
constexpr int MaximumImageDimension = 4096;

bool
readJsonObject(const QString &path, QJsonObject *object, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Unable to read the skin manifest: %1").arg(file.errorString());
        return false;
    }
    if (file.size() > MaximumManifestSize) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The skin manifest is too large.");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The skin manifest is not valid JSON.");
        return false;
    }
    *object = document.object();
    return true;
}

bool
resolveImage(const QString &root, const QString &relativePath, QString *imagePath)
{
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath))
        return false;

    const QString cleanRelative = QDir::cleanPath(relativePath);
    if (cleanRelative == QStringLiteral(".") || cleanRelative == QStringLiteral("..") ||
        cleanRelative.startsWith(QStringLiteral("../")))
        return false;

    const QFileInfo imageInfo(QDir(root).filePath(cleanRelative));
    if (!imageInfo.isFile() || imageInfo.size() > MaximumImageSize)
        return false;
    const QString canonicalImage = imageInfo.canonicalFilePath();
    const QString relativeCanonical = QDir(root).relativeFilePath(canonicalImage);
    if (canonicalImage.isEmpty() || relativeCanonical == QStringLiteral("..") ||
        relativeCanonical.startsWith(QStringLiteral("../")))
        return false;

    const QSet<QString> supportedFormats = { QStringLiteral("png"), QStringLiteral("jpg"),
                                             QStringLiteral("jpeg"), QStringLiteral("webp") };
    if (!supportedFormats.contains(imageInfo.suffix().toLower()))
        return false;
    QImageReader reader(canonicalImage);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid() || size.width() > MaximumImageDimension ||
        size.height() > MaximumImageDimension)
        return false;

    *imagePath = canonicalImage;
    return true;
}

bool
readManifest(const QString &directory, QJsonObject *manifest, QString *rootPath, QString *errorMessage)
{
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.isDir()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Choose a skin package folder.");
        return false;
    }
    const QString canonicalRoot = directoryInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The skin package folder cannot be resolved.");
        return false;
    }
    if (!readJsonObject(QDir(canonicalRoot).filePath(QStringLiteral("skin.json")), manifest, errorMessage))
        return false;

    const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
    if (manifest->value(QStringLiteral("schema")).toString() != QString::fromLatin1(SkinSchema) ||
        !idPattern.match(manifest->value(QStringLiteral("id")).toString()).hasMatch() ||
        manifest->value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("This folder does not contain a supported BluMach skin.");
        return false;
    }
    *rootPath = canonicalRoot;
    return true;
}
} // namespace

QString
BluMachCatalogSkin::configuredDirectory()
{
    const VMManagerConfig config(VMManagerConfig::ConfigType::General);
    return config.getStringValue(QStringLiteral("blumach_catalog_skin_directory"));
}

bool
BluMachCatalogSkin::manufacturerMarksEnabled()
{
    const VMManagerConfig config(VMManagerConfig::ConfigType::General);
    return config.getStringValue(QStringLiteral("blumach_catalog_skin_manufacturer_marks")) != QStringLiteral("0");
}

bool
BluMachCatalogSkin::inspectDirectory(const QString &directory, QString *name,
                                     QString *errorMessage, QString *warningMessage)
{
    QJsonObject manifest;
    QString root;
    if (!readManifest(directory, &manifest, &root, errorMessage))
        return false;
    if (name)
        *name = manifest.value(QStringLiteral("name")).toString().trimmed();
    int rejectedMarks = 0;
    const auto marks = manifest.value(QStringLiteral("manufacturer_marks")).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        const auto mark = it.value().toObject();
        QString imagePath;
        if (!resolveImage(root, mark.value(QStringLiteral("asset")).toString(), &imagePath))
            ++rejectedMarks;
    }
    if (warningMessage) {
        warningMessage->clear();
        if (rejectedMarks > 0)
            *warningMessage = QStringLiteral("%1 manufacturer mark(s) could not be loaded.")
                                  .arg(rejectedMarks);
    }
    return true;
}

bool
BluMachCatalogSkin::loadConfigured(QString *errorMessage)
{
    m_displayName.clear();
    m_manufacturerMarks.clear();
    const QString directory = configuredDirectory();
    if (directory.isEmpty() || !manufacturerMarksEnabled())
        return true;
    return loadDirectory(directory, errorMessage);
}

bool
BluMachCatalogSkin::loadDirectory(const QString &directory, QString *errorMessage)
{
    m_displayName.clear();
    m_manufacturerMarks.clear();

    QJsonObject manifest;
    QString root;
    if (!readManifest(directory, &manifest, &root, errorMessage))
        return false;
    m_displayName = manifest.value(QStringLiteral("name")).toString().trimmed();

    const auto marks = manifest.value(QStringLiteral("manufacturer_marks")).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        const auto mark = it.value().toObject();
        QString imagePath;
        if (!resolveImage(root, mark.value(QStringLiteral("asset")).toString(), &imagePath))
            continue;
        const QString background = mark.value(QStringLiteral("background")).toString();
        m_manufacturerMarks.insert(it.key(), { imagePath, background });
    }
    return true;
}

QString
BluMachCatalogSkin::displayName() const
{
    return m_displayName;
}

BluMachManufacturerMark
BluMachCatalogSkin::manufacturerMark(const QString &manufacturerId) const
{
    return m_manufacturerMarks.value(manufacturerId);
}
