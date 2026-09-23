/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "resource_library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cassert>

static QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile output(path);
    assert(output.open(QIODevice::WriteOnly));
    assert(output.write(bytes) == bytes.size());
    output.close();
    return path;
}

int main()
{
    QTemporaryDir temporary;
    assert(temporary.isValid());
    PortableCatalogMachine machine;
    PortableResourceRole firmware;
    firmware.role = QStringLiteral("firmware");
    firmware.folder = QStringLiteral("maker/machine/firmware");
    const QByteArray valid("test firmware data");
    firmware.known.append({QStringLiteral("Known BIOS"),
        QString::fromLatin1(QCryptographicHash::hash(
            valid, QCryptographicHash::Sha256).toHex()), valid.size(), true});
    machine.resources.append(firmware);

    auto scan = scanPortableResources(temporary.path(), machine);
    assert(scan.preferredPath(QStringLiteral("firmware")).isEmpty());
    assert(scan.matches.isEmpty());
    assert(QDir().mkpath(QDir(temporary.path()).filePath(firmware.folder)));
    const QString folder = QDir(temporary.path()).filePath(firmware.folder);
    const QString wrong = writeFile(QDir(folder).filePath(QStringLiteral("wrong.bin")),
                                    QByteArray(valid.size(), 'x'));
    scan = scanPortableResources(temporary.path(), machine);
    assert(scan.matches.isEmpty()); // Same size is never enough.
    assert(QFile::exists(wrong));
    const QString renamed = writeFile(QDir(folder).filePath(QStringLiteral("renamed.bin")),
                                      valid);
    scan = scanPortableResources(temporary.path(), machine);
    assert(scan.preferredPath(QStringLiteral("firmware")) == renamed);
    assert(scan.nameForPath(QStringLiteral("firmware"), renamed) ==
           QStringLiteral("Known BIOS"));
    writeFile(QDir(folder).filePath(QStringLiteral("copy.bin")), valid);
    scan = scanPortableResources(temporary.path(), machine);
    assert(scan.matches.size() == 2);
    assert(scan.preferredPath(QStringLiteral("firmware")).isEmpty());
    scan = scanPortableResources(QDir(temporary.path()).filePath(
        QStringLiteral("missing")), machine);
    assert(!scan.error.isEmpty());

    const QString localRoot = qEnvironmentVariable("BLUMACH_TEST_RESOURCE_ROOT");
    if (!localRoot.isEmpty()) {
        PortableCatalog catalog;
        QString error;
        assert(catalog.load(&error));
        for (const QString &id : {QStringLiteral("olivetti-pcs86"),
                                  QStringLiteral("olivetti-m15")}) {
            const auto *product = catalog.product(id);
            assert(product != nullptr);
            const auto found = scanPortableResources(localRoot, *product);
            assert(found.error.isEmpty() && !found.limited);
            assert(!found.preferredPath(QStringLiteral("floppy-0")).isEmpty());
            assert(!found.preferredPath(id == QStringLiteral("olivetti-pcs86") ?
                QStringLiteral("firmware-even") : QStringLiteral("firmware")).isEmpty());
        }
    }
    return 0;
}
