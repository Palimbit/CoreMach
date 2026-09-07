/* BluMach modifications: rtzor, Project BluMach, 2026. */
#ifndef QT_CMOS_RESET_HPP
#define QT_CMOS_RESET_HPP

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace blumach {
struct CmosResetResult {
    bool ok = false;
    QString backup;
    QString error;
};

/* Caller must keep the VM stopped. Move only its motherboard state out of
   the loader's filename; a failed rename leaves the original in place. */
inline CmosResetResult resetMotherboardCmos(const QString &vmPath, const QString &name)
{
    if (name.isEmpty() || name == "." || name == ".." ||
        name.contains('/') || name.contains('\\') || name.contains(':'))
        return {false, {}, QStringLiteral("Invalid CMOS filename")};
    const QString vm = QFileInfo(vmPath).canonicalFilePath();
    if (vm.isEmpty() || !QFileInfo(vm).isDir())
        return {false, {}, QStringLiteral("VM directory is unavailable")};
    const QFileInfo folder(QDir(vm).filePath("nvr"));
    if (folder.isSymLink())
        return {false, {}, QStringLiteral("NVR directory is a link")};
    if (!folder.exists())
        return {true, {}, {}};
    const QString nvr = folder.canonicalFilePath();
    if (!folder.isDir() || nvr != QDir(vm).filePath("nvr"))
        return {false, {}, QStringLiteral("NVR directory is redirected")};
    const QString source = QDir(nvr).filePath(name + ".nvr");
    const QFileInfo info(source);
    if (info.isSymLink())
        return {false, {}, QStringLiteral("CMOS state is a link")};
    if (!info.exists())
        return {true, {}, {}};
    if (!info.isFile() || info.canonicalPath() != nvr)
        return {false, {}, QStringLiteral("CMOS state is not a regular local file")};
    const QString backup = source + ".backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile file(source);
    if (!file.rename(backup))
        return {false, {}, file.errorString()};
    return {true, backup, {}};
}
}
#endif
