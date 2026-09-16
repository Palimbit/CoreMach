/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

MachineDialog::MachineDialog(QWidget *parent) : QDialog(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open |
                                         QDialogButtonBox::Cancel);
    machines_ = new QComboBox;
    assetsLayout_ = new QFormLayout;
    setWindowTitle(tr("Open portable machine"));
    setMinimumWidth(620);
    for (size_t index = 0U; index < bm_frontend_adapter_count(); ++index) {
        const bm_frontend_adapter_t *item = bm_frontend_adapter_at(index);
        const bm_machine_definition_t *definition =
            bm_frontend_adapter_definition(item);
        machines_->addItem(QString::fromUtf8(definition->id),
                           QVariant::fromValue<qulonglong>(index));
    }
    form->addRow(tr("Machine"), machines_);
    layout->addLayout(form);
    layout->addLayout(assetsLayout_);
    layout->addStretch();
    layout->addWidget(buttons);
    connect(machines_, &QComboBox::currentIndexChanged, this,
            [this] { rebuildAssets(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &MachineDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuildAssets();
}

const bm_frontend_adapter_t *
MachineDialog::adapter() const
{
    return bm_frontend_adapter_at(
        static_cast<size_t>(machines_->currentData().toULongLong()));
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

void
MachineDialog::selectMachine(const QString &machineId)
{
    const int index = machines_->findText(machineId);
    if (index >= 0)
        machines_->setCurrentIndex(index);
}

void
MachineDialog::setPaths(const QHash<QString, QString> &paths)
{
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        if (editors_.contains(it.key()))
            editors_.value(it.key())->setText(it.value());
    }
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
    return text;
}

void
MachineDialog::rebuildAssets()
{
    while (QLayoutItem *item = assetsLayout_->takeAt(0)) {
        if (item->widget() != nullptr)
            delete item->widget();
        if (item->layout() != nullptr)
            delete item->layout();
        delete item;
    }
    editors_.clear();
    size_t count = 0U;
    const bm_frontend_asset_requirement_t *assets =
        bm_frontend_adapter_assets(adapter(), &count);
    for (size_t index = 0U; index < count; ++index) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        auto *editor = new QLineEdit;
        auto *browse = new QPushButton(tr("Browse…"));
        const QString role = QString::fromUtf8(assets[index].role);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(editor, 1);
        rowLayout->addWidget(browse);
        editors_.insert(role, editor);
        connect(browse, &QPushButton::clicked, this, [this, editor] {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Select machine asset"));
            if (!path.isEmpty())
                editor->setText(path);
        });
        assetsLayout_->addRow(requirementText(assets[index]), row);
    }
}

void
MachineDialog::accept()
{
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
    }
    QDialog::accept();
}
