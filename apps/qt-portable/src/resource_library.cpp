/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "resource_library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace {
constexpr int maxFiles = 1000;
constexpr int maxDepth = 8;
constexpr qint64 maxFileBytes = 4 * 1024 * 1024;
constexpr qint64 maxHashBytes = 64 * 1024 * 1024;

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty())
            return QString();
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}
}

QString PortableResourceScan::preferredPath(const QString &role) const
{
    if (limited || !error.isEmpty())
        return QString();
    QString path;
    for (const PortableResourceMatch &match : matches) {
        if (match.role != role || !match.preferred)
            continue;
        if (!path.isEmpty())
            return QString(); // Ambiguous, even if both copies are identical.
        path = match.path;
    }
    return path;
}

QString PortableResourceScan::nameForPath(const QString &role,
                                           const QString &path) const
{
    for (const PortableResourceMatch &match : matches) {
        if (match.role == role && match.path == path)
            return match.name;
    }
    return QString();
}

PortableResourceScan scanPortableResources(const QString &root,
                                            const PortableCatalogMachine &machine)
{
    PortableResourceScan result;
    if (root.isEmpty())
        return result;
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || rootInfo.isSymLink() || !rootInfo.isReadable()) {
        result.error = QStringLiteral("Resource folder is unavailable: %1").arg(root);
        return result;
    }

    QSet<qint64> acceptedSizes;
    for (const PortableResourceRole &role : machine.resources) {
        for (const PortableResourceIdentity &identity : role.known) {
            if (identity.size <= maxFileBytes)
                acceptedSizes.insert(identity.size);
        }
    }
    if (acceptedSizes.isEmpty())
        return result;

    struct Pending { QString path; int depth; };
    QVector<Pending> pending {{ rootInfo.absoluteFilePath(), 0 }};
    int filesSeen = 0;
    qint64 bytesHashed = 0;
    while (!pending.isEmpty()) {
        const Pending current = pending.takeLast();
        const QFileInfoList entries = QDir(current.path).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
            QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (entry.isSymLink())
                continue;
            if (entry.isDir()) {
                if (current.depth < maxDepth)
                    pending.append({entry.absoluteFilePath(), current.depth + 1});
                else
                    result.limited = true;
                continue;
            }
            if (++filesSeen > maxFiles) {
                result.limited = true;
                return result;
            }
            if (!entry.isFile() || !entry.isReadable() ||
                !acceptedSizes.contains(entry.size()))
                continue;
            if (bytesHashed + entry.size() > maxHashBytes) {
                result.limited = true;
                return result;
            }
            bytesHashed += entry.size();
            const QString digest = hashFile(entry.absoluteFilePath());
            if (digest.isEmpty())
                continue;
            for (const PortableResourceRole &role : machine.resources) {
                for (const PortableResourceIdentity &identity : role.known) {
                    if (entry.size() == identity.size && digest == identity.sha256)
                        result.matches.append({role.role, identity.name,
                                               entry.absoluteFilePath(),
                                               identity.sha256,
                                               identity.preferred});
                }
            }
        }
    }
    return result;
}
