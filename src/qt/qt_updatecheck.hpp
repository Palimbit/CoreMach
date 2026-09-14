/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the update check module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_UPDATECHECK_HPP
#define QT_UPDATECHECK_HPP

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QWidget>

#include <optional>

class UpdateCheck final : public QObject {
    Q_OBJECT
public:
    struct GithubReleaseInfo {
        QString name;
        QString tag_name;
        QString html_url;
        QString target_commitish;
        QString created_at;
        QString published_at;
        QString body;
    };

    struct UpdateResult {
        bool                     updateAvailable = false;
        bool                     upToDate        = false;
        QString                  currentVersion;
        QString                  latestVersion;
        QList<GithubReleaseInfo> githubInfo;
    };

    explicit UpdateCheck(QObject *parent = nullptr);
    ~UpdateCheck() override;
    void                         checkForUpdates();
    static int                   versionCompare(const QString &version1, const QString &version2);
    [[nodiscard]] static QString getCurrentVersion();

signals:
    void updateCheckComplete(const UpdateCheck::UpdateResult &result);
    void updateCheckError(const QString &errorMsg);

private:
    const QUrl githubReleaseApi = QUrl("https://api.github.com/repos/BluMach/BluMach/releases");
    QString    currentVersion;

    static std::optional<QList<GithubReleaseInfo>> parseGithubJson(const QString &filename);
    static std::optional<GithubReleaseInfo>        parseGithubRelease(const QJsonObject &json);

private slots:
    void githubDownloadComplete(const QString &filename);
    void generalDownloadError(const QString &error);
};

#endif // QT_UPDATECHECK_HPP
