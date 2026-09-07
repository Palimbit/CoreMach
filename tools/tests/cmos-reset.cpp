/* BluMach modifications: rtzor, Project BluMach, 2026. */
#include <cassert>
#include <QCoreApplication>
#include <QTemporaryDir>
#include "../../src/qt/qt_cmos_reset.hpp"

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    assert(f.open(QIODevice::ReadOnly));
    return f.readAll();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    assert(temp.isValid());
    assert(blumach::resetMotherboardCmos(temp.path(), "pc5286").ok);
    QDir dir(temp.path());
    assert(dir.mkdir("nvr"));
    const auto cmos = dir.filePath("nvr/pc5286.nvr");
    const auto card = dir.filePath("nvr/expansion.nvr");
    QByteArray original;
    for (int i = 0; i < 128; ++i) original.append(char(i));
    for (const auto &path : {cmos, card}) {
        QFile f(path);
        assert(f.open(QIODevice::WriteOnly));
        assert(f.write(original) == original.size());
    }
    assert(!blumach::resetMotherboardCmos(temp.path(), "../escape").ok);
    assert(!blumach::resetMotherboardCmos(temp.path(), "C:escape").ok);
    assert(!blumach::resetMotherboardCmos(temp.path(), "").ok);
    assert(readFile(cmos) == original);
    const auto result = blumach::resetMotherboardCmos(temp.path(), "pc5286");
    assert(result.ok && !result.backup.isEmpty());
    assert(!QFile::exists(cmos));
    assert(readFile(result.backup) == original);
    assert(readFile(card) == original);
    const auto again = blumach::resetMotherboardCmos(temp.path(), "pc5286");
    assert(again.ok && again.backup.isEmpty());
    assert(readFile(result.backup) == original);
    assert(QFile::rename(result.backup, cmos));
    assert(readFile(cmos) == original);
    assert(QDir(temp.path()).mkdir("nvr/directory.nvr"));
    assert(!blumach::resetMotherboardCmos(temp.path(), "directory").ok);
    puts("CMOS reset: isolated backup, repeat, exact restore, traversal and non-file rejection PASS");
}
