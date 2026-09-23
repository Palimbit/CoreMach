/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_dialog.h"
#include "portable_catalog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>

namespace {
bool createBlankWorkingImage(const QString &root, const QString &folder,
                             const QString &role, quint64 bytes,
                             QString *path, QString *error)
{
    static const QRegularExpression safeFolder(
        QStringLiteral("^[a-z0-9-]+(?:/[a-z0-9-]+)*$"));
    static const QRegularExpression safeRole(
        QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    if (!safeFolder.match(folder).hasMatch() ||
        !safeRole.match(role).hasMatch() ||
        bytes == 0U || bytes > 64U * 1024U * 1024U) {
        *error = QObject::tr("La definición del disco generado no es válida.");
        return false;
    }
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || rootInfo.isSymLink() || !rootInfo.isWritable()) {
        *error = QObject::tr("Selecciona una carpeta de recursos escribible.");
        return false;
    }
    QDir destination(rootInfo.absoluteFilePath());
    for (const QString &part : folder.split(QLatin1Char('/'))) {
        const QFileInfo child(destination.filePath(part));
        if (child.exists()) {
            if (!child.isDir() || child.isSymLink()) {
                *error = QObject::tr("La carpeta del disco contiene una ruta no segura.");
                return false;
            }
        } else if (!destination.mkdir(part)) {
            *error = QObject::tr("No se pudo crear la carpeta del disco.");
            return false;
        }
        if (!destination.cd(part)) {
            *error = QObject::tr("No se pudo abrir la carpeta del disco.");
            return false;
        }
    }
    const QByteArray zeroes(64 * 1024, '\0');
    for (int attempt = 0; attempt < 3; ++attempt) {
        const QString candidate = destination.filePath(
            QStringLiteral("blumach-%1-%2.img").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces), role));
        QFile output(candidate);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(candidate))
                continue;
            *error = output.errorString();
            return false;
        }
        quint64 remaining = bytes;
        while (remaining != 0U) {
            const qint64 count = static_cast<qint64>(
                std::min<quint64>(remaining, static_cast<quint64>(zeroes.size())));
            if (output.write(zeroes.constData(), count) != count) {
                *error = output.errorString();
                output.close();
                QFile::remove(candidate);
                return false;
            }
            remaining -= static_cast<quint64>(count);
        }
        if (!output.flush()) {
            *error = output.errorString();
            output.close();
            QFile::remove(candidate);
            return false;
        }
        output.close();
        if (output.error() != QFile::NoError ||
            QFileInfo(candidate).size() != static_cast<qint64>(bytes)) {
            *error = QObject::tr("No se pudo completar la imagen de disco.");
            QFile::remove(candidate);
            return false;
        }
        *path = candidate;
        return true;
    }
    *error = QObject::tr("No se pudo reservar un nombre de disco único.");
    return false;
}
}

MachineDialog::MachineDialog(const PortableCatalog &catalog,
                             const QString &resourceRoot, QWidget *parent)
    : QDialog(parent), name_(new QLineEdit), save_(new QCheckBox(
          tr("Guardar esta máquina para próximas sesiones"))),
      catalog_(catalog), resourceRoot_(resourceRoot),
      rootLabel_(new QLabel), scanStatus_(new QLabel)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open |
                                         QDialogButtonBox::Cancel);
    machines_ = new QComboBox;
    machines_->setObjectName(QStringLiteral("machine-choice"));
    assetsLayout_ = new QFormLayout;
    advancedAssetsLayout_ = new QFormLayout;
    configurationLayout_ = new QFormLayout;
    advancedConfigurationLayout_ = new QFormLayout;
    formTabs_ = new QTabWidget;
    formTabs_->setObjectName(QStringLiteral("machine-form-tabs"));
    auto *generalPage = new QWidget;
    generalPage->setObjectName(QStringLiteral("machine-general-page"));
    auto *generalLayout = new QVBoxLayout(generalPage);
    advancedPage_ = new QWidget;
    advancedPage_->setObjectName(QStringLiteral("machine-advanced-page"));
    auto *advancedLayout = new QVBoxLayout(advancedPage_);
    formTabs_->addTab(generalPage, tr("General"));
    formTabs_->addTab(advancedPage_, tr("Hardware avanzado"));
    setWindowTitle(tr("Open portable machine"));
    setMinimumWidth(620);
    for (const PortableCatalogMachine &machine : catalog.machines()) {
        const QByteArray adapterId = machine.adapterId.toUtf8();
        if (bm_frontend_adapter_find(adapterId.constData()) == nullptr)
            continue;
        machines_->addItem(machine.name, machine.adapterId);
        machines_->setItemData(machines_->count() - 1, machine.productId,
                               Qt::UserRole + 1);
    }
    form->addRow(tr("Machine"), machines_);
    name_->setObjectName(QStringLiteral("machine-profile-name"));
    name_->setPlaceholderText(tr("Nombre de esta máquina"));
    form->addRow(tr("Nombre"), name_);
    generalLayout->addLayout(form);
    generalLayout->addLayout(configurationLayout_);
    auto *rootRow = new QHBoxLayout;
    auto *changeRoot = new QPushButton(tr("Carpeta de recursos…"));
    auto *rescan = new QPushButton(tr("Volver a buscar"));
    rootLabel_->setObjectName(QStringLiteral("resource-root-label"));
    rootLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootRow->addWidget(rootLabel_, 1);
    rootRow->addWidget(changeRoot);
    rootRow->addWidget(rescan);
    generalLayout->addLayout(rootRow);
    scanStatus_->setObjectName(QStringLiteral("resource-scan-status"));
    scanStatus_->setWordWrap(true);
    generalLayout->addWidget(scanStatus_);
    generalLayout->addLayout(assetsLayout_);
    generalLayout->addStretch();
    advancedLayout->addLayout(advancedConfigurationLayout_);
    advancedLayout->addLayout(advancedAssetsLayout_);
    advancedEmpty_ = new QLabel(tr("Esta máquina aún no tiene ajustes avanzados "
                                   "configurables."));
    advancedEmpty_->setObjectName(QStringLiteral("machine-advanced-empty"));
    advancedEmpty_->setWordWrap(true);
    advancedLayout->addWidget(advancedEmpty_);
    advancedLayout->addStretch();
    layout->addWidget(formTabs_, 1);
    save_->setChecked(true);
    layout->addWidget(save_);
    layout->addStretch();
    layout->addWidget(buttons);
    connect(machines_, &QComboBox::currentIndexChanged, this,
            [this, catalogPtr = &catalog] {
                const auto *machine = catalogPtr->product(productId());
                const QString newDefault = machine != nullptr ? machine->name : QString();
                if (name_->text().isEmpty() || name_->text() == defaultName_)
                    name_->setText(newDefault);
                defaultName_ = newDefault;
                rebuildConfigurations();
                rebuildAssets();
                scanResources();
            });
    connect(changeRoot, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Selecciona la carpeta raíz de recursos"), resourceRoot_);
        if (!chosen.isEmpty()) {
            if (!editingExisting_) {
                for (auto it = editors_.cbegin(); it != editors_.cend(); ++it) {
                    const QString selected = scan_.preferredPath(it.key());
                    if (!selected.isEmpty() && it.value()->text() == selected)
                        it.value()->clear();
                }
            }
            resourceRoot_ = chosen;
            scanResources();
        }
    });
    connect(rescan, &QPushButton::clicked, this,
            [this] { scanResources(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &MachineDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuildConfigurations();
    rebuildAssets();
    scanResources();
    if (const auto *machine = catalog.product(productId())) {
        defaultName_ = machine->name;
        name_->setText(defaultName_);
    }
}

QString MachineDialog::resourceRoot() const
{
    return resourceRoot_;
}

QString MachineDialog::productId() const
{
    return machines_->currentData(Qt::UserRole + 1).toString();
}

QString MachineDialog::profileName() const
{
    return name_->text().trimmed();
}

bool MachineDialog::saveProfile() const
{
    return save_->isChecked();
}

const bm_frontend_adapter_t *
MachineDialog::adapter() const
{
    const QByteArray id = machines_->currentData().toString().toUtf8();
    return bm_frontend_adapter_find(id.constData());
}

QHash<QString, QString>
MachineDialog::paths() const
{
    QHash<QString, QString> result;
    for (auto it = editors_.cbegin(); it != editors_.cend(); ++it) {
        if (!it.value()->text().isEmpty())
            result.insert(it.key(), it.value()->text());
    }
    return result;
}

QHash<QString, quint32> MachineDialog::options() const
{
    QHash<QString, quint32> result;
    const auto *machine = catalog_.product(productId());
    if (machine == nullptr)
        return result;
    for (const PortableCatalogCreationField &field : machine->configurationFields) {
        QComboBox *choice = configurationChoices_.value(field.id);
        if (choice == nullptr || field.portableOption.isEmpty())
            continue;
        const QString choiceId = choice->currentData().toString();
        for (const PortableCatalogCreationChoice &item : field.choices) {
            if (item.id == choiceId && item.available) {
                result.insert(field.portableOption, item.portableValue);
                break;
            }
        }
    }
    return result;
}

void
MachineDialog::selectMachine(const QString &machineId)
{
    const int index = machines_->findData(machineId);
    if (index >= 0)
        machines_->setCurrentIndex(index);
}

void
MachineDialog::selectProduct(const QString &productId)
{
    const int index = machines_->findData(productId, Qt::UserRole + 1);
    if (index >= 0)
        machines_->setCurrentIndex(index);
}

void
MachineDialog::setMachineSelectionLocked(bool locked)
{
    machines_->setEnabled(!locked);
}

void
MachineDialog::setPaths(const QHash<QString, QString> &paths)
{
    for (auto it = editors_.cbegin(); it != editors_.cend(); ++it) {
        it.value()->setText(paths.value(it.key()));
        if (firmwareChoices_.contains(it.key()))
            refreshFirmwareChoices(it.key());
    }
}

void MachineDialog::setOptions(const QHash<QString, quint32> &options)
{
    const auto *machine = catalog_.product(productId());
    if (machine == nullptr)
        return;
    for (const PortableCatalogCreationField &field : machine->configurationFields) {
        QComboBox *choice = configurationChoices_.value(field.id);
        if (choice == nullptr)
            continue;
        if (editingExisting_ && !options.contains(field.portableOption)) {
            if (choice == presetChoice_) {
                const QSignalBlocker blocker(choice);
                choice->setCurrentIndex(0);
            } else {
                QString mediaRole;
                for (const PortableCatalogCreationChoice &item : field.choices) {
                    if (!item.mediaRole.isEmpty()) {
                        mediaRole = item.mediaRole;
                        break;
                    }
                }
                if (!mediaRole.isEmpty()) {
                    const QString mediaPath = paths().value(mediaRole);
                    const qint64 bytes = mediaPath.isEmpty() ? 0 :
                        QFileInfo(mediaPath).size();
                    for (const PortableCatalogCreationChoice &item : field.choices) {
                        if ((bytes == 0 && item.forbiddenAssets.contains(mediaRole)) ||
                            (bytes > 0 && item.mediaRole == mediaRole &&
                             bytes <= item.mediaMaxBytes)) {
                            const QSignalBlocker blocker(choice);
                            choice->setCurrentIndex(choice->findData(item.id));
                            break;
                        }
                    }
                }
            }
        }
        if (!options.contains(field.portableOption))
            continue;
        const QSignalBlocker blocker(choice);
        for (const PortableCatalogCreationChoice &item : field.choices) {
            if (item.available && item.portableValue == options.value(field.portableOption)) {
                choice->setCurrentIndex(choice->findData(item.id));
                break;
            }
        }
    }
    if (presetChoice_ != nullptr) {
        const QString selected = presetChoice_->currentData().toString();
        for (const PortableCatalogCreationField &field : machine->configurationFields) {
            if (configurationChoices_.value(field.id) != presetChoice_)
                continue;
            for (const PortableCatalogCreationChoice &preset : field.choices) {
                if (preset.id != selected)
                    continue;
                for (const PortableCatalogCreationField &target :
                     machine->configurationFields) {
                    if (options.contains(target.portableOption) ||
                        !preset.setOptions.contains(target.portableOption))
                        continue;
                    QComboBox *choice = configurationChoices_.value(target.id);
                    if (choice == nullptr)
                        continue;
                    const QSignalBlocker blocker(choice);
                    for (const PortableCatalogCreationChoice &item : target.choices) {
                        if (item.portableValue ==
                            preset.setOptions.value(target.portableOption)) {
                            choice->setCurrentIndex(choice->findData(item.id));
                            break;
                        }
                    }
                }
            }
        }
    }
    syncGeneratedAssetsWithPreset();
    for (auto it = editors_.cbegin(); it != editors_.cend(); ++it)
        updateAssetStatus(it.key());
}

void MachineDialog::selectCustomFirmware()
{
    for (QComboBox *choice : firmwareChoices_)
        choice->setCurrentIndex(choice->count() - 1);
}

void MachineDialog::setProfileName(const QString &name)
{
    name_->setText(name);
}

void MachineDialog::setEditingExistingProfile(bool editing)
{
    editingExisting_ = editing;
    machines_->setEnabled(!editing);
    save_->setChecked(true);
    save_->setEnabled(!editing);
    for (auto it = editors_.cbegin(); it != editors_.cend(); ++it)
        updateAssetStatus(it.key());
}

QString
MachineDialog::requirementText(
    const bm_frontend_asset_requirement_t &asset) const
{
    QStringList sizes;
    for (size_t index = 0U; index < asset.accepted_size_count; ++index) {
        const uint64_t size = asset.accepted_sizes[index];
        sizes.append(size % 1024U == 0U ?
                     tr("%1 KiB").arg(size / 1024U) :
                     tr("%1 bytes").arg(size));
    }
    QString text = QString::fromUtf8(asset.label);
    if (!sizes.isEmpty())
        text += tr(" (%1)").arg(sizes.join(tr(" or ")));
    if (!asset.required)
        text += tr(" — optional");
    if (asset.kind == BM_FRONTEND_ASSET_BLOCK_MEDIA)
        text += tr(" — writable working image");
    return text;
}

void MachineDialog::rebuildConfigurations()
{
    const auto clearRows = [](QFormLayout *form) {
        while (QLayoutItem *item = form->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    };
    clearRows(configurationLayout_);
    clearRows(advancedConfigurationLayout_);
    configurationChoices_.clear();
    presetChoice_ = nullptr;
    const auto *machine = catalog_.product(productId());
    if (machine == nullptr) {
        updateAdvancedTab();
        return;
    }
    const bool hasPresets = std::any_of(
        machine->configurationFields.cbegin(), machine->configurationFields.cend(),
        [](const PortableCatalogCreationField &field) {
            return std::any_of(field.choices.cbegin(), field.choices.cend(),
                [](const PortableCatalogCreationChoice &choice) {
                    return !choice.setOptions.isEmpty();
                });
        });
    if (!hasPresets && !machine->commercialConfiguration.isEmpty()) {
        auto *commercial = new QComboBox;
        commercial->setObjectName(QStringLiteral("commercial-configuration"));
        commercial->addItem(machine->commercialConfiguration);
        commercial->setToolTip(tr("Configuración comercial documentada; las opciones "
                                  "de hardware se eligen por separado."));
        configurationLayout_->addRow(tr("Configuración comercial"), commercial);
    }
    for (const PortableCatalogCreationField &field : machine->configurationFields) {
        auto *choice = new QComboBox;
        choice->setObjectName(QStringLiteral("configuration-%1").arg(field.id));
        if (!field.help.isEmpty())
            choice->setToolTip(field.help);
        int selected = -1;
        for (const PortableCatalogCreationChoice &item : field.choices) {
            QString label = item.label;
            if (item.status == QStringLiteral("documented"))
                label += tr(" — documentada");
            else if (item.status == QStringLiteral("experimental"))
                label += tr(" — experimental");
            if (!item.available)
                label += tr(" — no disponible en el motor portable");
            choice->addItem(label, item.id);
            const int index = choice->count() - 1;
            if (!item.available)
                choice->setItemData(index, 0, Qt::UserRole - 1);
            if (item.id == field.defaultId && item.available)
                selected = index;
        }
        if (selected < 0) {
            for (const PortableCatalogCreationChoice &item : field.choices) {
                if (item.available) {
                    selected = choice->findData(item.id);
                    break;
                }
            }
        }
        choice->setCurrentIndex(selected);
        (field.advanced ? advancedConfigurationLayout_ : configurationLayout_)
            ->addRow(field.label, choice);
        configurationChoices_.insert(field.id, choice);
        if (std::any_of(field.choices.cbegin(), field.choices.cend(),
            [](const PortableCatalogCreationChoice &item) {
                return !item.setOptions.isEmpty();
            }))
            presetChoice_ = choice;
    }
    if (!machine->expansionSlots.isEmpty()) {
        QMap<QString, int> slotCounts;
        for (const PortableCatalogExpansionSlot &slot : machine->expansionSlots)
            ++slotCounts[slot.bus.toUpper()];
        QStringList descriptions;
        for (auto it = slotCounts.cbegin(); it != slotCounts.cend(); ++it)
            descriptions.append(tr("%1 × %2").arg(it.value()).arg(it.key()));
        auto *slotNote = new QLabel(tr("%1. La configuración de tarjetas aún no está "
                                       "disponible en el motor portable.")
                                        .arg(descriptions.join(QStringLiteral(", "))));
        slotNote->setObjectName(QStringLiteral("expansion-slots-note"));
        slotNote->setWordWrap(true);
        advancedConfigurationLayout_->addRow(tr("Ranuras declaradas"), slotNote);
    }
    updateAdvancedTab();
    if (presetChoice_ != nullptr) {
        connect(presetChoice_, &QComboBox::currentIndexChanged, this,
                [this, machine] {
            if (applyingPreset_)
                return;
            const QString selected = presetChoice_->currentData().toString();
            for (const PortableCatalogCreationField &field :
                 machine->configurationFields) {
                if (configurationChoices_.value(field.id) != presetChoice_)
                    continue;
                for (const PortableCatalogCreationChoice &preset : field.choices) {
                    if (preset.id != selected || preset.setOptions.isEmpty())
                        continue;
                    applyingPreset_ = true;
                    for (const PortableCatalogCreationField &target :
                         machine->configurationFields) {
                        if (!preset.setOptions.contains(target.portableOption))
                            continue;
                        QComboBox *control = configurationChoices_.value(target.id);
                        if (control == nullptr)
                            continue;
                        for (const PortableCatalogCreationChoice &item : target.choices) {
                            if (item.portableValue ==
                                preset.setOptions.value(target.portableOption)) {
                                control->setCurrentIndex(control->findData(item.id));
                                break;
                            }
                        }
                    }
                    applyingPreset_ = false;
                    return;
                }
            }
        });
        connect(presetChoice_, &QComboBox::currentIndexChanged, this,
                [this] {
            syncGeneratedAssetsWithPreset();
            for (auto it = editors_.cbegin(); it != editors_.cend(); ++it)
                updateAssetStatus(it.key());
        });
        for (const PortableCatalogCreationField &field :
             machine->configurationFields) {
            QComboBox *choice = configurationChoices_.value(field.id);
            if (choice == nullptr || choice == presetChoice_)
                continue;
            connect(choice, &QComboBox::currentIndexChanged, this,
                    [this, machine, fieldPtr = &field, choice] {
                if (presetChoice_ == nullptr || applyingPreset_)
                    return;
                const QString selected = presetChoice_->currentData().toString();
                for (const PortableCatalogCreationField &presetField :
                     machine->configurationFields) {
                    if (configurationChoices_.value(presetField.id) != presetChoice_)
                        continue;
                    for (const PortableCatalogCreationChoice &preset :
                         presetField.choices) {
                        if (preset.id != selected)
                            continue;
                        if (!preset.setOptions.contains(fieldPtr->portableOption))
                            return;
                        for (const PortableCatalogCreationChoice &item :
                             fieldPtr->choices) {
                            if (item.id == choice->currentData().toString() &&
                                item.portableValue == preset.setOptions.value(
                                    fieldPtr->portableOption))
                                return;
                        }
                        presetChoice_->setCurrentIndex(0);
                        return;
                    }
                }
            });
        }
    }
}

void MachineDialog::syncGeneratedAssetsWithPreset()
{
    const PortableCatalogMachine *machine = catalog_.product(productId());
    if (machine == nullptr || presetChoice_ == nullptr)
        return;
    const QString selected = presetChoice_->currentData().toString();
    for (const PortableCatalogCreationField &field :
         machine->configurationFields) {
        if (configurationChoices_.value(field.id) != presetChoice_)
            continue;
        for (const PortableCatalogCreationChoice &choice : field.choices) {
            if (choice.id != selected)
                continue;
            if (choice.setOptions.isEmpty())
                return; // A customised machine retains its explicit disk choice.
            for (auto it = generateAssetChecks_.cbegin();
                 it != generateAssetChecks_.cend(); ++it) {
                const QLineEdit *editor = editors_.value(it.key());
                it.value()->setChecked(editor != nullptr &&
                    editor->text().trimmed().isEmpty() &&
                    choice.generatedAssets.contains(it.key()));
            }
            return;
        }
    }
}

void
MachineDialog::rebuildAssets()
{
    const auto clearRows = [](QFormLayout *form) {
        while (QLayoutItem *item = form->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    };
    clearRows(assetsLayout_);
    clearRows(advancedAssetsLayout_);
    editors_.clear();
    firmwareChoices_.clear();
    generateAssetChecks_.clear();
    customFirmwarePaths_.clear();
    assetStatus_.clear();
    folderButtons_.clear();
    if (adapter() == nullptr) {
        updateAdvancedTab();
        return;
    }
    size_t count = 0U;
    const bm_frontend_asset_requirement_t *assets =
        bm_frontend_adapter_assets(adapter(), &count);
    const PortableCatalogMachine *machine = catalog_.product(productId());
    for (size_t index = 0U; index < count; ++index) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        auto *editor = new QLineEdit;
        auto *status = new QLabel;
        auto *browse = new QPushButton(tr("Browse…"));
        auto *openFolder = new QPushButton(tr("Abrir carpeta"));
        const QString role = QString::fromUtf8(assets[index].role);
        editor->setObjectName(QStringLiteral("asset-%1").arg(role));
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("asset-status-%1").arg(role));
        rowLayout->setContentsMargins(0, 0, 0, 0);
        if (assets[index].kind == BM_FRONTEND_ASSET_BLOB) {
            auto *choice = new QComboBox;
            choice->setObjectName(QStringLiteral("firmware-choice-%1").arg(role));
            choice->setMinimumWidth(230);
            rowLayout->addWidget(choice, 1);
            firmwareChoices_.insert(role, choice);
            connect(choice, &QComboBox::currentIndexChanged, this,
                    [this, role, editor, choice] {
                if (!editor->isHidden())
                    customFirmwarePaths_.insert(role, editor->text());
                const QString recognizedPath = choice->currentData().toString();
                if (recognizedPath.isEmpty()) {
                    editor->setText(customFirmwarePaths_.value(role));
                    editor->setHidden(false);
                } else {
                    editor->setText(recognizedPath);
                    editor->setHidden(true);
                }
            });
        }
        rowLayout->addWidget(editor, 1);
        bool canGenerate = false;
        if (machine != nullptr &&
            assets[index].kind == BM_FRONTEND_ASSET_BLOCK_MEDIA) {
            for (const PortableCatalogCreationField &field :
                 machine->configurationFields) {
                for (const PortableCatalogCreationChoice &choice : field.choices)
                    canGenerate |= choice.generatedAssets.contains(role);
            }
        }
        if (canGenerate) {
            auto *generate = new QCheckBox(tr("Crear disco nuevo"));
            generate->setObjectName(QStringLiteral("generate-%1").arg(role));
            generate->setToolTip(tr("Crea una imagen vacía y escribible al abrir. "
                                    "No modifica ninguna imagen existente."));
            rowLayout->addWidget(generate);
            generateAssetChecks_.insert(role, generate);
            connect(generate, &QCheckBox::toggled, this,
                    [this, role, editor, browse](bool checked) {
                if (checked)
                    editor->clear();
                editor->setEnabled(!checked);
                browse->setEnabled(!checked);
                updateAssetStatus(role);
            });
        }
        rowLayout->addWidget(browse);
        rowLayout->addWidget(openFolder);
        editors_.insert(role, editor);
        assetStatus_.insert(role, status);
        folderButtons_.insert(role, openFolder);
        connect(editor, &QLineEdit::textChanged, this,
                [this, role] {
            if (QCheckBox *generate = generateAssetChecks_.value(role)) {
                if (!editors_.value(role)->text().trimmed().isEmpty())
                    generate->setChecked(false);
            }
            updateAssetStatus(role);
        });
        connect(browse, &QPushButton::clicked, this, [this, editor, role] {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Select machine asset"));
            if (!path.isEmpty()) {
                if (QComboBox *choice = firmwareChoices_.value(role))
                    choice->setCurrentIndex(choice->count() - 1);
                editor->setText(path);
                if (firmwareChoices_.contains(role))
                    customFirmwarePaths_.insert(role, path);
            }
        });
        connect(openFolder, &QPushButton::clicked, this, [this, role] {
            if (resourceRoot_.isEmpty())
                return;
            const PortableCatalogMachine *machine = catalog_.product(productId());
            if (machine == nullptr)
                return;
            for (const PortableResourceRole &resource : machine->resources) {
                if (resource.role == role) {
                    QDir root(resourceRoot_);
                    if (root.mkpath(resource.folder))
                        QDesktopServices::openUrl(QUrl::fromLocalFile(
                            root.filePath(resource.folder)));
                    return;
                }
            }
        });
        QFormLayout *destination =
            assets[index].kind == BM_FRONTEND_ASSET_BLOB ||
            (assets[index].kind == BM_FRONTEND_ASSET_READ_ONLY_MEDIA &&
             assets[index].storage_kind == BM_STORAGE_DEVICE_FLOPPY &&
             assets[index].storage_unit == 0U) ?
            assetsLayout_ : advancedAssetsLayout_;
        auto *label = new QLabel(requirementText(assets[index]));
        label->setWordWrap(true);
        label->setMaximumWidth(220);
        destination->addRow(label, row);
        destination->addRow(QString(), status);
    }
    syncGeneratedAssetsWithPreset();
    updateAdvancedTab();
}

void MachineDialog::updateAdvancedTab()
{
    const bool visible = advancedConfigurationLayout_->rowCount() != 0 ||
                         advancedAssetsLayout_->rowCount() != 0;
    advancedEmpty_->setVisible(!visible);
}

void MachineDialog::scanResources()
{
    rootLabel_->setText(resourceRoot_.isEmpty() ?
        tr("Sin carpeta de recursos") :
        tr("Recursos: %1").arg(QDir::toNativeSeparators(resourceRoot_)));
    const PortableCatalogMachine *machine = catalog_.product(productId());
    scan_ = machine == nullptr ? PortableResourceScan {} :
        scanPortableResources(resourceRoot_, *machine);
    scanStatus_->setText(!scan_.error.isEmpty() ? scan_.error :
        scan_.limited ? tr("Búsqueda limitada; elige los archivos no encontrados manualmente.") :
        tr("%1 recursos reconocidos por SHA-256.").arg(scan_.matches.size()));
    if (!editingExisting_ && !scan_.limited) {
        for (auto it = editors_.cbegin(); it != editors_.cend(); ++it) {
            const QCheckBox *generate = generateAssetChecks_.value(it.key());
            if (it.value()->text().isEmpty() &&
                (generate == nullptr || !generate->isChecked())) {
                const QString preferred = scan_.preferredPath(it.key());
                if (!preferred.isEmpty())
                    it.value()->setText(preferred);
            }
        }
    }
    for (auto it = firmwareChoices_.cbegin(); it != firmwareChoices_.cend(); ++it)
        refreshFirmwareChoices(it.key());
    for (auto it = editors_.cbegin(); it != editors_.cend(); ++it)
        updateAssetStatus(it.key());
    for (auto it = folderButtons_.cbegin(); it != folderButtons_.cend(); ++it) {
        bool hasFolder = false;
        if (machine != nullptr) {
            for (const PortableResourceRole &resource : machine->resources)
                hasFolder |= resource.role == it.key();
        }
        it.value()->setEnabled(hasFolder && !resourceRoot_.isEmpty() &&
                               scan_.error.isEmpty());
    }
}

void MachineDialog::refreshFirmwareChoices(const QString &role)
{
    QComboBox *choice = firmwareChoices_.value(role);
    QLineEdit *editor = editors_.value(role);
    if (choice == nullptr || editor == nullptr)
        return;
    const QString selectedPath = editor->text();
    if (!editor->isHidden() && !selectedPath.isEmpty() && choice->count() > 0 &&
        choice->currentData().toString().isEmpty())
        customFirmwarePaths_.insert(role, selectedPath);
    const QSignalBlocker block(choice);
    choice->clear();
    for (const PortableResourceMatch &match : scan_.matches) {
        if (match.role != role)
            continue;
        const QString relative = QDir(resourceRoot_).relativeFilePath(match.path);
        choice->addItem(tr("%1 — %2").arg(match.name, relative), match.path);
        choice->setItemData(choice->count() - 1,
                            tr("SHA-256: %1\n%2").arg(match.sha256, match.path),
                            Qt::ToolTipRole);
    }
    choice->addItem(tr("Otra BIOS… (archivo personalizado)"), QString());
    int index = choice->findData(selectedPath);
    if (selectedPath.isEmpty() || index < 0)
        index = choice->count() - 1;
    choice->setCurrentIndex(index);
    if (index == choice->count() - 1) {
        if (!selectedPath.isEmpty())
            customFirmwarePaths_.insert(role, selectedPath);
        editor->setText(selectedPath);
        editor->setHidden(false);
    } else {
        editor->setText(choice->currentData().toString());
        editor->setHidden(true);
    }
}

void MachineDialog::updateAssetStatus(const QString &role)
{
    QLabel *status = assetStatus_.value(role);
    QLineEdit *editor = editors_.value(role);
    if (status == nullptr || editor == nullptr)
        return;
    const QString path = editor->text().trimmed();
    if (path.isEmpty()) {
        const PortableCatalogMachine *machine = catalog_.product(productId());
        QString folder;
        if (const QCheckBox *generate = generateAssetChecks_.value(role)) {
            if (generate->isChecked()) {
                status->setText(resourceRoot_.isEmpty() ?
                    tr("Selecciona una carpeta de recursos para crear el disco.") :
                    tr("Al abrir se creará una imagen de disco vacía y escribible; "
                       "también puedes elegir una existente."));
                return;
            }
        }
        if (machine != nullptr) {
            for (const PortableResourceRole &resource : machine->resources) {
                if (resource.role == role) {
                    folder = resource.folder;
                    break;
                }
            }
        }
        status->setText(generateAssetChecks_.contains(role) ?
            tr("Elige una imagen existente o marca «Crear disco nuevo».") :
            folder.isEmpty() ? tr("Sin seleccionar") :
            tr("No encontrado. Coloca tu archivo en %1 o elígelo manualmente.")
                .arg(folder));
    } else if (!QFileInfo::exists(path)) {
        status->setText(tr("Archivo no encontrado; corrige la ruta."));
    } else {
        const QString identity = scan_.nameForPath(role, path);
        status->setText(identity.isEmpty() ?
            tr("Archivo seleccionado manualmente; identidad no verificada.") :
            tr("Reconocido: %1").arg(identity));
    }
}

void
MachineDialog::accept()
{
    if (profileName().isEmpty()) {
        QMessageBox::warning(this, tr("Missing name"),
                             tr("Give this machine a name."));
        return;
    }
    if (adapter() == nullptr) {
        QMessageBox::warning(this, tr("No portable machine"),
                             tr("The catalogue has no available portable machine."));
        return;
    }
    size_t count = 0U;
    const bm_frontend_asset_requirement_t *assets =
        bm_frontend_adapter_assets(adapter(), &count);
    for (size_t index = 0U; index < count; ++index) {
        const QString role = QString::fromUtf8(assets[index].role);
        if (assets[index].required && editors_.value(role)->text().isEmpty()) {
            QMessageBox::warning(this, tr("Missing asset"),
                                 tr("%1 is required.")
                                     .arg(QString::fromUtf8(assets[index].label)));
            return;
        }
        const QString path = editors_.value(role)->text().trimmed();
        if (!path.isEmpty() && !QFileInfo(path).isFile()) {
            QMessageBox::warning(this, tr("Archivo no encontrado"),
                                 tr("No se encuentra %1: %2")
                                     .arg(QString::fromUtf8(assets[index].label), path));
            return;
        }
        if (!path.isEmpty() && assets[index].accepted_size_count != 0U) {
            const quint64 bytes = static_cast<quint64>(QFileInfo(path).size());
            bool accepted = false;
            for (size_t size = 0U; size < assets[index].accepted_size_count;
                 ++size)
                accepted |= bytes == assets[index].accepted_sizes[size];
            if (!accepted) {
                QMessageBox::warning(this, tr("Tamaño de recurso no admitido"),
                    tr("El archivo seleccionado para %1 no tiene un tamaño admitido.")
                        .arg(QString::fromUtf8(assets[index].label)));
                return;
            }
        }
    }
    const PortableCatalogMachine *machine = catalog_.product(productId());
    QStringList generate;
    const QHash<QString, QString> selectedPaths = paths();
    for (auto it = generateAssetChecks_.cbegin();
         it != generateAssetChecks_.cend(); ++it) {
        if (!it.value()->isChecked())
            continue;
        if (selectedPaths.contains(it.key())) {
            QMessageBox::warning(this, tr("Hardware incompatible"),
                tr("Elige una imagen existente o crea una nueva, no ambas."));
            return;
        }
        generate.append(it.key());
    }
    if (machine != nullptr) {
        for (const PortableCatalogCreationField &field :
             machine->configurationFields) {
            const QComboBox *control = configurationChoices_.value(field.id);
            if (control == nullptr)
                continue;
            for (const PortableCatalogCreationChoice &choice : field.choices) {
                if (choice.id != control->currentData().toString())
                    continue;
                for (const QString &role : choice.requiredAssets) {
                    if (!selectedPaths.contains(role) &&
                        !generate.contains(role)) {
                        if (choice.generatedAssets.contains(role) &&
                            !generateAssetChecks_.contains(role)) {
                            if (!generate.contains(role))
                                generate.append(role);
                            continue;
                        }
                        QMessageBox::warning(this, tr("Falta un recurso"),
                            tr("La opción «%1» necesita un archivo para %2.")
                                .arg(choice.label, role));
                        return;
                    }
                }
                for (const QString &role : choice.forbiddenAssets) {
                    if (selectedPaths.contains(role) || generate.contains(role)) {
                        QMessageBox::warning(this, tr("Hardware incompatible"),
                            tr("La opción «%1» no admite un archivo en %2. "
                               "Retíralo o elige una configuración personalizada.")
                                .arg(choice.label, role));
                        return;
                    }
                }
                if (!choice.mediaRole.isEmpty() &&
                    selectedPaths.contains(choice.mediaRole) &&
                    QFileInfo(selectedPaths.value(choice.mediaRole)).size() >
                        choice.mediaMaxBytes) {
                    QMessageBox::warning(this, tr("Medio incompatible"),
                        tr("El archivo de %1 supera la capacidad de «%2».")
                            .arg(choice.mediaRole, choice.label));
                    return;
                }
                break;
            }
        }
    }
    QHash<QString, QString> created;
    for (const QString &role : generate) {
        const bm_frontend_asset_requirement_t *requirement = nullptr;
        QString folder;
        for (size_t index = 0U; index < count; ++index) {
            if (role == QString::fromUtf8(assets[index].role)) {
                requirement = &assets[index];
                break;
            }
        }
        for (const PortableResourceRole &resource : machine->resources) {
            if (resource.role == role) {
                folder = resource.folder;
                break;
            }
        }
        QString path;
        QString error;
        if (requirement == nullptr || folder.isEmpty() ||
            requirement->kind != BM_FRONTEND_ASSET_BLOCK_MEDIA ||
            requirement->accepted_size_count != 1U ||
            !createBlankWorkingImage(resourceRoot_, folder, role,
                 requirement->accepted_sizes[0], &path, &error)) {
            for (auto it = created.cbegin(); it != created.cend(); ++it) {
                QFile::remove(it.value());
                editors_.value(it.key())->clear();
            }
            QMessageBox::warning(this, tr("No se pudo crear el disco"),
                error.isEmpty() ? tr("El recurso no admite una imagen generada.") :
                                  error);
            return;
        }
        created.insert(role, path);
        editors_.value(role)->setText(path);
    }
    QDialog::accept();
}
