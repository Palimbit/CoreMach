/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_catalog.h"

#include <cassert>

int
main()
{
    const QByteArray json = R"({
      "schema": "blumach-catalog-v3",
      "manufacturers": [
        {"id":"maker","name":"Maker","description_key":"maker.description",
         "history_key":"maker.history","history_references":[
           {"title":"Archive","publisher":"Museum","url":"https://example.org/archive"}]}],
      "families": [
        {"id":"family","manufacturer_id":"maker","name":"Family",
         "description_key":"family.description"}],
      "platforms": [
        {"id":"supported","portable_adapter_id":"machine-a"},
        {"id":"legacy-only","emulator_machine_id":"machine_b"}
      ],
      "products": [
        {"id":"product-a","name":"Machine A","status":"validated",
         "manufacturer_id":"maker","family_id":"family",
         "platform_id":"supported","period":"1987",
         "aliases":["Alternate"],"tags":["8088"],
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
      "maker.description":"Maker description", "maker.history":"Maker history",
      "family.description":"Family description",
      "a.summary":"Resumen", "a.history":"Historia",
      "a.warning":"Limitación", "section.title":"Técnica",
      "fact.label":"Procesador", "fact.value":"8088"
    })";
    PortableCatalog catalog;
    QString error;
    assert(catalog.parse(json, locale, &error));
    assert(error.isEmpty());
    assert(catalog.machines().size() == 2);
    const PortableCatalogMachine *machine = catalog.product(
        QStringLiteral("product-a"));
    assert(machine != nullptr);
    assert(machine->name == QStringLiteral("Machine A"));
    assert(machine->status == QStringLiteral("validated"));
    assert(machine->adapterId == QStringLiteral("machine-a"));
    assert(machine->manufacturerId == QStringLiteral("maker"));
    assert(machine->familyId == QStringLiteral("family"));
    assert(machine->aliases.contains(QStringLiteral("Alternate")));
    assert(machine->tags.contains(QStringLiteral("8088")));
    assert(machine->period == QStringLiteral("1987"));
    assert(machine->summary == QStringLiteral("Resumen"));
    assert(machine->history == QStringLiteral("Historia"));
    assert(machine->warning == QStringLiteral("Limitación"));
    assert(machine->mediaResource == QStringLiteral(":/example.jpg"));
    assert(machine->sections.size() == 1);
    assert(machine->sections[0].facts[0].value == QStringLiteral("8088"));
    assert(machine->sections[0].facts[0].url ==
           QStringLiteral("https://example.org/source"));
    assert(catalog.product(QStringLiteral("product-b")) != nullptr);
    assert(catalog.product(QStringLiteral("product-b"))->adapterId.isEmpty());
    assert(catalog.manufacturer(QStringLiteral("maker"))->history ==
           QStringLiteral("Maker history"));
    assert(catalog.manufacturer(QStringLiteral("maker"))->references.size() == 1);
    assert(catalog.family(QStringLiteral("family"))->description ==
           QStringLiteral("Family description"));

    assert(!catalog.parse(QByteArrayLiteral("{}"), &error));
    assert(!catalog.parse(json, QByteArrayLiteral("[]"), &error));
    return 0;
}
