/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_catalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QSet>
#include <QRegularExpression>

#include <utility>
#include <cmath>
#include <limits>

namespace {
QString hardwareValue(const QJsonValue &value)
{
    if (value.isArray()) {
        QStringList items;
        for (const QJsonValue &item : value.toArray())
            items.append(item.toVariant().toString());
        return items.join(QStringLiteral(", "));
    }
    if (value.isBool())
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    return value.toVariant().toString();
}
}

bool
PortableCatalog::load(QString *error)
{
    QFile file(QStringLiteral(":/blumach/catalog/catalog.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    const QByteArray catalog = file.readAll();
    QString language = QLocale::system().name().left(2).toLower();
    if (language != QStringLiteral("es") &&
        language != QStringLiteral("en") &&
        language != QStringLiteral("fr") &&
        language != QStringLiteral("it") &&
        language != QStringLiteral("pt"))
        language = QStringLiteral("en");
    QFile locale(QStringLiteral(":/blumach/catalog/locales/%1.json")
                     .arg(language));
    if (!locale.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = locale.errorString();
        return false;
    }
    return parse(catalog, locale.readAll(), error);
}

bool
PortableCatalog::parse(const QByteArray &json, QString *error)
{
    return parse(json, QByteArrayLiteral("{}"), error);
}

bool
PortableCatalog::parse(const QByteArray &json, const QByteArray &translations,
                       QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (error != nullptr)
            *error = parseError.error == QJsonParseError::NoError ?
                     QStringLiteral("Catalogue root is not an object") :
                     parseError.errorString();
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() !=
        QStringLiteral("blumach-catalog-v3")) {
        if (error != nullptr)
            *error = QStringLiteral("Unsupported catalogue schema");
        return false;
    }

    const QJsonDocument locale = QJsonDocument::fromJson(translations,
                                                        &parseError);
    if (!locale.isObject()) {
        if (error != nullptr)
            *error = QStringLiteral("Invalid catalogue translations");
        return false;
    }
    const QJsonObject words = locale.object();
    const auto translated = [&words](const QJsonValue &key) {
        return words.value(key.toString()).toString();
    };

    QVector<PortableCatalogManufacturer> manufacturers;
    QSet<QString> manufacturerIds;
    for (const QJsonValue &value :
         root.value(QStringLiteral("manufacturers")).toArray()) {
        const QJsonObject source = value.toObject();
        PortableCatalogManufacturer item;
        item.id = source.value(QStringLiteral("id")).toString();
        item.name = source.value(QStringLiteral("name")).toString();
        if (item.id.isEmpty() || item.name.isEmpty() ||
            manufacturerIds.contains(item.id)) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid catalogue manufacturer");
            return false;
        }
        item.description = translated(source.value(QStringLiteral("description_key")));
        item.history = translated(source.value(QStringLiteral("history_key")));
        for (const QJsonValue &referenceValue :
             source.value(QStringLiteral("history_references")).toArray()) {
            const QJsonObject reference = referenceValue.toObject();
            item.references.append({ reference.value(QStringLiteral("title")).toString(),
                reference.value(QStringLiteral("publisher")).toString(),
                reference.value(QStringLiteral("url")).toString() });
        }
        manufacturerIds.insert(item.id);
        manufacturers.append(std::move(item));
    }

    QVector<PortableCatalogFamily> families;
    QSet<QString> familyIds;
    for (const QJsonValue &value :
         root.value(QStringLiteral("families")).toArray()) {
        const QJsonObject source = value.toObject();
        PortableCatalogFamily item;
        item.id = source.value(QStringLiteral("id")).toString();
        item.manufacturerId = source.value(QStringLiteral("manufacturer_id")).toString();
        item.name = source.value(QStringLiteral("name")).toString();
        if (item.id.isEmpty() || item.name.isEmpty() ||
            familyIds.contains(item.id)) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid catalogue family");
            return false;
        }
        item.description = translated(source.value(QStringLiteral("description_key")));
        familyIds.insert(item.id);
        families.append(std::move(item));
    }

    QHash<QString, QString> adapters;
    QHash<QString, QString> architectures;
    for (const QJsonValue &value :
         root.value(QStringLiteral("platforms")).toArray()) {
        const QJsonObject platform = value.toObject();
        const QString id = platform.value(QStringLiteral("id")).toString();
        const QString adapter =
            platform.value(QStringLiteral("portable_adapter_id")).toString();
        if (!id.isEmpty() && !adapter.isEmpty())
            adapters.insert(id, adapter);
        if (!id.isEmpty())
            architectures.insert(id,
                platform.value(QStringLiteral("architecture")).toString());
    }

    QVector<PortableCatalogMachine> parsed;
    QSet<QString> productIds;
    for (const QJsonValue &value :
         root.value(QStringLiteral("products")).toArray()) {
        const QJsonObject product = value.toObject();
        PortableCatalogMachine item;
        item.productId = product.value(QStringLiteral("id")).toString();
        item.manufacturerId = product.value(QStringLiteral("manufacturer_id")).toString();
        item.familyId = product.value(QStringLiteral("family_id")).toString();
        item.name = product.value(QStringLiteral("name")).toString();
        item.status = product.value(QStringLiteral("status")).toString();
        item.statusLabel = words.value(
            QStringLiteral("status.%1").arg(item.status)).toString();
        item.adapterId = adapters.value(
            product.value(QStringLiteral("platform_id")).toString());
        item.architecture = architectures.value(
            product.value(QStringLiteral("platform_id")).toString());
        if (item.productId.isEmpty() || item.name.isEmpty() ||
            item.status.isEmpty() || productIds.contains(item.productId)) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid portable catalogue product");
            return false;
        }
        item.period = product.value(QStringLiteral("period")).toString();
        item.summary = translated(product.value(QStringLiteral("summary_key")));
        item.history = translated(product.value(QStringLiteral("history_key")));
        item.warning = translated(product.value(QStringLiteral("warning_key")));
        const QJsonObject media = product.value(QStringLiteral("media")).toObject();
        item.mediaResource = media.value(QStringLiteral("resource")).toString();
        item.mediaLabel = translated(media.value(QStringLiteral("label_key")));
        const QJsonObject implementation = product.value(
            QStringLiteral("implementation")).toObject();
        item.implementationDocument = implementation.value(
            QStringLiteral("document")).toString();
        item.implementationLanguage = implementation.value(
            QStringLiteral("language")).toString();
        for (const QJsonValue &slotValue :
             product.value(QStringLiteral("expansion_slots")).toArray()) {
            const QJsonObject source = slotValue.toObject();
            PortableCatalogExpansionSlot slot;
            slot.id = source.value(QStringLiteral("id")).toString();
            slot.bus = source.value(QStringLiteral("bus")).toString();
            slot.length = source.value(QStringLiteral("length")).toString();
            slot.modelled = source.value(QStringLiteral("modelled")).toBool();
            if (slot.id.isEmpty() || slot.bus.isEmpty() || slot.length.isEmpty()) {
                if (error != nullptr)
                    *error = QStringLiteral("Invalid expansion slot metadata");
                return false;
            }
            item.expansionSlots.append(std::move(slot));
        }
        const QJsonObject creation = product.value(QStringLiteral("creation")).toObject();
        for (const QJsonValue &factValue :
             creation.value(QStringLiteral("facts")).toArray()) {
            const QJsonObject fact = factValue.toObject();
            if (fact.value(QStringLiteral("label_key")).toString() ==
                QStringLiteral("creation.label.commercial_model"))
                item.commercialConfiguration = translated(
                    fact.value(QStringLiteral("value_key")));
        }
        const QString portableCommercial = translated(
            creation.value(QStringLiteral("portable_commercial_key")));
        if (!portableCommercial.isEmpty())
            item.commercialConfiguration = portableCommercial;
        for (const QJsonValue &fieldValue :
             creation.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject fieldSource = fieldValue.toObject();
            PortableCatalogCreationField field;
            field.id = fieldSource.value(QStringLiteral("id")).toString();
            field.label = translated(fieldSource.value(QStringLiteral("label_key")));
            field.help = translated(fieldSource.value(QStringLiteral("help_key")));
            field.defaultId = fieldSource.value(QStringLiteral("default")).toString();
            field.portableOption = fieldSource.value(
                QStringLiteral("portable_option")).toString();
            field.portableKind = fieldSource.value(
                QStringLiteral("portable_kind")).toString();
            const QJsonValue advanced = fieldSource.value(
                QStringLiteral("portable_advanced"));
            if (!advanced.isUndefined() && !advanced.isBool()) {
                if (error != nullptr)
                    *error = QStringLiteral("Invalid portable advanced field flag");
                return false;
            }
            field.advanced = advanced.toBool();
            field.quick = fieldSource.value(QStringLiteral("portable_quick")).toBool();
            if (!field.portableOption.isEmpty() &&
                !QRegularExpression(QStringLiteral("^[a-z][a-z0-9_]*$"))
                     .match(field.portableOption).hasMatch()) {
                if (error != nullptr)
                    *error = QStringLiteral("Invalid portable machine option");
                return false;
            }
            for (const QJsonValue &choiceValue :
                 fieldSource.value(QStringLiteral("choices")).toArray()) {
                const QJsonObject choiceSource = choiceValue.toObject();
                PortableCatalogCreationChoice choice;
                choice.id = choiceSource.value(QStringLiteral("id")).toString();
                choice.label = translated(choiceSource.value(QStringLiteral("label_key")));
                choice.status = choiceSource.value(QStringLiteral("status")).toString();
                const QJsonValue portableValue = choiceSource.value(
                    QStringLiteral("portable_value"));
                if (!field.portableOption.isEmpty() && portableValue.isDouble()) {
                    const double number = portableValue.toDouble();
                    if (std::isfinite(number) && number >= 0 &&
                        number <= std::numeric_limits<quint32>::max() &&
                        std::floor(number) == number) {
                        choice.portableValue = static_cast<quint32>(number);
                        choice.available = choice.status != QStringLiteral("unavailable");
                    }
                }
                const QJsonObject setOptions = choiceSource.value(
                    QStringLiteral("portable_set")).toObject();
                for (const QJsonValue &role : choiceSource.value(
                         QStringLiteral("portable_require_assets")).toArray())
                    choice.requiredAssets.append(role.toString());
                const QJsonValue generatedAssets = choiceSource.value(
                    QStringLiteral("portable_generate_assets"));
                if (!generatedAssets.isUndefined() && !generatedAssets.isArray()) {
                    if (error != nullptr)
                        *error = QStringLiteral("Invalid generated asset list");
                    return false;
                }
                for (const QJsonValue &role : generatedAssets.toArray()) {
                    if (!role.isString() || role.toString().isEmpty() ||
                        choice.generatedAssets.contains(role.toString())) {
                        if (error != nullptr)
                            *error = QStringLiteral("Invalid generated asset role");
                        return false;
                    }
                    choice.generatedAssets.append(role.toString());
                }
                for (const QJsonValue &role : choiceSource.value(
                         QStringLiteral("portable_forbid_assets")).toArray())
                    choice.forbiddenAssets.append(role.toString());
                choice.mediaRole = choiceSource.value(
                    QStringLiteral("portable_media_role")).toString();
                choice.mediaMaxBytes = static_cast<qint64>(choiceSource.value(
                    QStringLiteral("portable_media_max_bytes")).toDouble());
                for (auto it = setOptions.begin(); it != setOptions.end(); ++it) {
                    const double number = it.value().toDouble(-1);
                    if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9_]*$"))
                             .match(it.key()).hasMatch() ||
                        !it.value().isDouble() || !std::isfinite(number) ||
                        number < 0 || number > std::numeric_limits<quint32>::max() ||
                        std::floor(number) != number) {
                        if (error != nullptr)
                            *error = QStringLiteral("Invalid portable preset option");
                        return false;
                    }
                    choice.setOptions.insert(it.key(), static_cast<quint32>(number));
                }
                field.choices.append(std::move(choice));
            }
            if (field.portableKind == QStringLiteral("hex_byte_auto")) {
                if (field.choices.size() != 1 ||
                    field.choices.first().portableValue != 0U) {
                    if (error != nullptr)
                        *error = QStringLiteral("Invalid hex-byte option");
                    return false;
                }
                for (quint32 value = 0; value < 256U; ++value) {
                    PortableCatalogCreationChoice byte;
                    byte.id = QStringLiteral("byte_%1").arg(value, 2, 16,
                                                               QLatin1Char('0'));
                    byte.label = QStringLiteral("0x") + QString::number(value, 16)
                        .rightJustified(2, QLatin1Char('0')).toUpper();
                    byte.status = QStringLiteral("validated");
                    byte.portableValue = value + 1U;
                    byte.available = true;
                    field.choices.append(std::move(byte));
                }
            }
            if (!field.id.isEmpty() && !field.choices.isEmpty())
                item.configurationFields.append(std::move(field));
        }
        if (!item.implementationDocument.isEmpty() &&
            !QRegularExpression(QStringLiteral("^[a-z0-9-]+-implementation\\.md$"))
                 .match(item.implementationDocument).hasMatch()) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid engineering document name");
            return false;
        }
        const QJsonObject hardware = product.value(QStringLiteral("hardware")).toObject();
        const QStringList preferredHardwareKeys {
            QStringLiteral("cpu"), QStringLiteral("memory"),
            QStringLiteral("video"), QStringLiteral("floppy"),
            QStringLiteral("rtc"), QStringLiteral("coprocessor"),
            QStringLiteral("expansion"), QStringLiteral("interfaces"),
            QStringLiteral("dimensions")
        };
        QStringList hardwareKeys = preferredHardwareKeys;
        for (const QString &key : hardware.keys()) {
            if (!hardwareKeys.contains(key))
                hardwareKeys.append(key);
        }
        for (const QString &key : hardwareKeys) {
            if (!hardware.contains(key))
                continue;
            const QString label = words.value(
                QStringLiteral("hardware.%1").arg(key)).toString(key);
            const QString value = hardwareValue(hardware.value(key));
            if (!value.isEmpty())
                item.hardware.append({label, value, QString()});
        }
        for (const QJsonValue &alias : product.value(QStringLiteral("aliases")).toArray())
            item.aliases.append(alias.toString());
        for (const QJsonValue &tag : product.value(QStringLiteral("tags")).toArray())
            item.tags.append(tag.toString());
        for (const QJsonValue &sectionValue :
             product.value(QStringLiteral("technical")).toArray()) {
            const QJsonObject source = sectionValue.toObject();
            PortableCatalogSection section;
            const QString titleKey = source.value(QStringLiteral("title_key")).toString();
            section.title = translated(source.value(QStringLiteral("title_key")));
            section.evidence = translated(source.value(QStringLiteral("evidence_key")));
            section.description = translated(source.value(QStringLiteral("description_key")));
            section.sources = titleKey == QStringLiteral("technical.section.sources") ||
                              titleKey == QStringLiteral("technical.section.research_material");
            for (const QJsonValue &factValue :
                 source.value(QStringLiteral("entries")).toArray()) {
                const QJsonObject fact = factValue.toObject();
                const QString label = translated(fact.value(QStringLiteral("label_key")));
                const QString value = translated(fact.value(QStringLiteral("value_key")));
                if (!label.isEmpty() && !value.isEmpty())
                    section.facts.append({ label, value,
                        fact.value(QStringLiteral("url")).toString() });
            }
            if (!section.facts.isEmpty() || !section.description.isEmpty())
                item.sections.append(std::move(section));
        }
        QSet<QString> resourceRoles;
        const QJsonValue resourcesValue = product.value(QStringLiteral("portable_resources"));
        if (!resourcesValue.isUndefined() && !resourcesValue.isArray()) {
            if (error != nullptr)
                *error = QStringLiteral("Invalid portable resource metadata");
            return false;
        }
        static const QRegularExpression sha256Pattern(
            QStringLiteral("^[0-9a-fA-F]{64}$"));
        for (const QJsonValue &resourceValue : resourcesValue.toArray()) {
            const QJsonObject source = resourceValue.toObject();
            PortableResourceRole resource;
            resource.role = source.value(QStringLiteral("role")).toString();
            resource.folder = source.value(QStringLiteral("folder")).toString();
            const QStringList parts = resource.folder.split(QLatin1Char('/'));
            if (resource.role.isEmpty() || resourceRoles.contains(resource.role) ||
                resource.folder.isEmpty() || resource.folder.startsWith(QLatin1Char('/')) ||
                parts.contains(QStringLiteral("..")) ||
                parts.contains(QStringLiteral(".")) ||
                resource.folder.contains(QLatin1Char('\\')) ||
                resource.folder.contains(QLatin1Char(':')) ||
                !source.value(QStringLiteral("known")).isArray()) {
                if (error != nullptr)
                    *error = QStringLiteral("Invalid portable resource role");
                return false;
            }
            for (const QJsonValue &identityValue :
                 source.value(QStringLiteral("known")).toArray()) {
                const QJsonObject identitySource = identityValue.toObject();
                PortableResourceIdentity identity;
                identity.name = identitySource.value(QStringLiteral("name")).toString();
                identity.sha256 = identitySource.value(QStringLiteral("sha256"))
                                      .toString().toLower();
                identity.size = identitySource.value(QStringLiteral("size")).toVariant()
                                    .toLongLong();
                identity.preferred = identitySource.value(QStringLiteral("preferred"))
                                         .toBool(false);
                if (identity.name.isEmpty() || identity.size <= 0 ||
                    !sha256Pattern.match(identity.sha256).hasMatch()) {
                    if (error != nullptr)
                        *error = QStringLiteral("Invalid portable resource identity");
                    return false;
                }
                resource.known.append(std::move(identity));
            }
            resourceRoles.insert(resource.role);
            item.resources.append(std::move(resource));
        }
        for (const PortableCatalogCreationField &field : item.configurationFields) {
            for (const PortableCatalogCreationChoice &choice : field.choices) {
                for (const QString &role : choice.generatedAssets) {
                    if (!resourceRoles.contains(role) ||
                        !choice.requiredAssets.contains(role)) {
                        if (error != nullptr)
                            *error = QStringLiteral("Generated asset is not a required resource");
                        return false;
                    }
                }
            }
        }
        productIds.insert(item.productId);
        parsed.append(std::move(item));
    }
    machines_ = std::move(parsed);
    manufacturers_ = std::move(manufacturers);
    families_ = std::move(families);
    return true;
}

const QVector<PortableCatalogMachine> &
PortableCatalog::machines() const
{
    return machines_;
}

const PortableCatalogMachine *
PortableCatalog::product(const QString &productId) const
{
    for (const PortableCatalogMachine &machine : machines_) {
        if (machine.productId == productId)
            return &machine;
    }
    return nullptr;
}

const PortableCatalogManufacturer *
PortableCatalog::manufacturer(const QString &id) const
{
    for (const PortableCatalogManufacturer &item : manufacturers_) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

const PortableCatalogFamily *
PortableCatalog::family(const QString &id) const
{
    for (const PortableCatalogFamily &item : families_) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}
