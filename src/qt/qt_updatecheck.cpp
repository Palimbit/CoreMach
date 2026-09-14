/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Update check module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

#include "qt_updatecheck.hpp"
#include "qt_downloader.hpp"
#include "qt_updatedetails.hpp"

extern "C" {
#include <86box/version.h>
}

UpdateCheck::
    UpdateCheck(QObject *parent)
    : QObject(parent)
{
    currentVersion = getCurrentVersion();
}

UpdateCheck::~UpdateCheck()
    = default;

void
UpdateCheck::checkForUpdates()
{
    const auto githubDownloader = new Downloader(Downloader::DownloadLocation::Temp);
    connect(githubDownloader, &Downloader::downloadCompleted, this, &UpdateCheck::githubDownloadComplete);
    connect(githubDownloader, &Downloader::errorOccurred, this, &UpdateCheck::generalDownloadError);
    githubDownloader->download(githubReleaseApi, "github_releases.json");
}

void
UpdateCheck::generalDownloadError(const QString &error)
{
    emit updateCheckError(error);
}

void
UpdateCheck::githubDownloadComplete(const QString &filename)
{
    const auto generalError            = tr("Unable to determine release information");
    const auto githubReleaseListResult = parseGithubJson(filename);
    QString    latestVersion           = "0.0";
    if (!githubReleaseListResult.has_value() || githubReleaseListResult.value().isEmpty()) {
        generalDownloadError(generalError);
        return;
    }
    auto githubReleaseList = githubReleaseListResult.value();
    // Warning: this check (using the tag name) relies on a consistent naming scheme: "v<number>"
    // where <number> is the release number. For example, 4.2 from v4.2 as the tag name.
    // Another option would be parsing the release name, but
    // either option requires a consistent naming scheme.
    latestVersion = githubReleaseList.first().tag_name.replace("v", "");
    for (const auto &release : githubReleaseList) {
        qDebug().noquote().nospace() << release.name << ": " << release.html_url << " (" << release.created_at << ")";
    }

    // const auto updateDetails = new UpdateDetails(githubReleaseList, currentVersion);
    bool updateAvailable = false;
    bool upToDate        = true;
    if (currentVersion.isEmpty() || (versionCompare(currentVersion, latestVersion) < 0)) {
        updateAvailable = true;
        upToDate        = false;
    }

    const auto updateResult = UpdateResult {
        .updateAvailable = updateAvailable,
        .upToDate        = upToDate,
        .currentVersion  = currentVersion,
        .latestVersion   = latestVersion,
        .githubInfo      = githubReleaseList,
    };

    emit updateCheckComplete(updateResult);
}

QString
UpdateCheck::getCurrentVersion()
{
    return { EMU_VERSION };
}

std::optional<QList<UpdateCheck::GithubReleaseInfo>>
UpdateCheck::parseGithubJson(const QString &filename)
{
    QList<GithubReleaseInfo> releaseInfoList;
    QFile                    json_file(filename);
    if (!json_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Couldn't open the json file: error %d", json_file.error());
        return std::nullopt;
    }

    const QString read_file = json_file.readAll();
    json_file.close();

    const auto json_doc = QJsonDocument::fromJson(read_file.toUtf8());

    if (json_doc.isNull()) {
        qWarning("Failed to create QJsonDocument, possibly invalid JSON");
        return std::nullopt;
    }

    if (!json_doc.isArray()) {
        qWarning("JSON does not have the expected format (array in root), cannot continue");
        return std::nullopt;
    }

    auto release_array = json_doc.array();

    for (const auto &each_release : release_array) {
        if (auto release = parseGithubRelease(each_release.toObject()); release.has_value()) {
            releaseInfoList.append(release.value());
        }
    }
    return releaseInfoList;
}
std::optional<UpdateCheck::GithubReleaseInfo>
UpdateCheck::parseGithubRelease(const QJsonObject &json)
{
    // Perform some basic validation
    if (!json.contains("name") || !json.contains("tag_name") || !json.contains("html_url")) {
        return std::nullopt;
    }

    auto githubRelease = GithubReleaseInfo {
        .name             = json["name"].toString(),
        .tag_name         = json["tag_name"].toString(),
        .html_url         = json["html_url"].toString(),
        .target_commitish = json["target_commitish"].toString(),
        .created_at       = json["created_at"].toString(),
        .published_at     = json["published_at"].toString(),
        .body             = json["body"].toString(),
    };

    return githubRelease;
}

// A simple method to compare version numbers
// Should work for comparing x.y.z and x.y. Missing
// values (parts) will be treated as zeroes
int
UpdateCheck::versionCompare(const QString &version1, const QString &version2)
{
    // Split both
    QStringList v1List = version1.split('.');
    QStringList v2List = version2.split('.');

    // Out of the two versions get the maximum amount of "parts"
    const int maxParts = std::max(v1List.size(), v2List.size());

    // Initialize both with zeros
    QVector<int> v1Parts(maxParts, 0);
    QVector<int> v2Parts(maxParts, 0);

    for (int i = 0; i < v1List.size(); ++i) {
        v1Parts[i] = v1List[i].toInt();
    }

    for (int i = 0; i < v2List.size(); ++i) {
        v2Parts[i] = v2List[i].toInt();
    }

    for (int i = 0; i < maxParts; ++i) {
        // First version is greater
        if (v1Parts[i] > v2Parts[i])
            return 1;
        // First version is less
        if (v1Parts[i] < v2Parts[i])
            return -1;
    }
    // They are equal
    return 0;
}
