/* BluMach. Author: rtzor. Project: BluMach.
 * Calls the same offline CMOS-clear helper used by the Qt menu.
 */
#include <cassert>
#include <QCoreApplication>
#include "../../src/qt/qt_cmos_reset.hpp"
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    assert(argc == 2);
    const auto result = blumach::resetMotherboardCmos(QString::fromLocal8Bit(argv[1]), "pc5286");
    assert(result.ok && !result.backup.isEmpty());
    puts(result.backup.toUtf8().constData());
    return 0;
}
