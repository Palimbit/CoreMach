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
         "platform_id":"supported","period":"1987",
         "summary_key":"a.summary","history_key":"a.history",
         "warning_key":"a.warning",
         "media":{"resource":":/example.jpg"},
         "technical":[{"title_key":"section.title","entries":[
           {"label_key":"fact.label","value_key":"fact.value",
            "url":"https://example.org/source"}]}]},
        {"id":"product-b","name":"Machine B","status":"experimental",
         "platform_id":"legacy-only"}
      ]
    })";
    const QByteArray locale = R"({
      "a.summary":"Resumen", "a.history":"Historia",
      "a.warning":"Limitación", "section.title":"Técnica",
      "fact.label":"Procesador", "fact.value":"8088"
    })";
    PortableCatalog catalog;
    QString error;
    assert(catalog.parse(json, locale, &error));
    assert(error.isEmpty());
    assert(catalog.machines().size() == 1);
    const PortableCatalogMachine *machine = catalog.product(
        QStringLiteral("product-a"));
    assert(machine != nullptr);
    assert(machine->name == QStringLiteral("Machine A"));
    assert(machine->status == QStringLiteral("validated"));
    assert(machine->adapterId == QStringLiteral("machine-a"));
    assert(machine->period == QStringLiteral("1987"));
    assert(machine->summary == QStringLiteral("Resumen"));
    assert(machine->history == QStringLiteral("Historia"));
    assert(machine->warning == QStringLiteral("Limitación"));
    assert(machine->mediaResource == QStringLiteral(":/example.jpg"));
    assert(machine->sections.size() == 1);
    assert(machine->sections[0].facts[0].value == QStringLiteral("8088"));
    assert(machine->sections[0].facts[0].url ==
           QStringLiteral("https://example.org/source"));
    assert(catalog.product(QStringLiteral("product-b")) == nullptr);

    assert(!catalog.parse(QByteArrayLiteral("{}"), &error));
    assert(!catalog.parse(json, QByteArrayLiteral("[]"), &error));
    return 0;
}
