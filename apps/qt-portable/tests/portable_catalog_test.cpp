/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_catalog.h"

#include <cassert>

int
main()
{
    const QByteArray json = R"({
      "schema": "blumach-catalog-v3",
      "platforms": [
        {"id":"supported","portable_adapter_id":"machine-a"},
        {"id":"legacy-only","emulator_machine_id":"machine_b"}
      ],
      "products": [
        {"id":"product-a","name":"Machine A","status":"validated",
         "platform_id":"supported"},
        {"id":"product-b","name":"Machine B","status":"experimental",
         "platform_id":"legacy-only"}
      ]
    })";
    PortableCatalog catalog;
    QString error;
    assert(catalog.parse(json, &error));
    assert(error.isEmpty());
    assert(catalog.machines().size() == 1);
    const PortableCatalogMachine *machine = catalog.product(
        QStringLiteral("product-a"));
    assert(machine != nullptr);
    assert(machine->name == QStringLiteral("Machine A"));
    assert(machine->status == QStringLiteral("validated"));
    assert(machine->adapterId == QStringLiteral("machine-a"));
    assert(catalog.product(QStringLiteral("product-b")) == nullptr);

    assert(!catalog.parse(QByteArrayLiteral("{}"), &error));
    return 0;
}
