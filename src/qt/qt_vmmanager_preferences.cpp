/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager preferences module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>
#include <cstring>

#include "qt_preferences.hpp"
#include "qt_blumach_skin.hpp"
#include "qt_vmmanager_preferences.hpp"
#include "qt_vmmanager_config.hpp"
#include "ui_qt_vmmanager_preferences.h"

#ifdef Q_OS_WINDOWS
#    include "qt_vmmanager_windarkmodefilter.hpp"
extern WindowsDarkModeFilter *vmm_dark_mode_filter;
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/version.h>
}

VMManagerPreferences::
    VMManagerPreferences(QWidget *parent, bool machinesRunning)
    : ui(new Ui::VMManagerPreferences)
{
    ui->setupUi(this);
    ui->dirSelectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(ui->dirSelectButton, &QPushButton::clicked, this, &VMManagerPreferences::chooseDirectoryLocation);
    ui->catalogSkinBrowseButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(ui->catalogSkinBrowseButton, &QPushButton::clicked, this, &VMManagerPreferences::chooseCatalogSkinDirectory);
    connect(ui->catalogSkinClearButton, &QPushButton::clicked, this, &VMManagerPreferences::clearCatalogSkinDirectory);

    const auto config          = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    const auto configSystemDir = QString(vmm_path_cfg);
    if (!configSystemDir.isEmpty()) {
        // Prefer this one
        ui->systemDirectory->setText(QDir::toNativeSeparators(configSystemDir));
    } else if (!QString(vmm_path).isEmpty()) {
        // If specified on command line
        ui->systemDirectory->setText(QDir::toNativeSeparators(QDir(vmm_path).path()));
    }

    if (machinesRunning) {
        ui->systemDirectory->setEnabled(false);
        ui->dirSelectButton->setEnabled(false);
        ui->pushButtonDefaultSystemDir->setEnabled(false);
        ui->dirSelectButton->setToolTip(tr("To change the system directory, stop all running machines."));
    }

    ui->comboBoxLanguage->setItemData(0, 0);
    for (int i = 1; i < Preferences::languages.length(); i++) {
        ui->comboBoxLanguage->addItem(Preferences::languages[i].second, i);
        if (i == lang_id) {
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i));
        }
    }
    ui->comboBoxLanguage->model()->sort(Qt::AscendingOrder);

#if EMU_BUILD_NUM != 0
    const auto configUpdateCheck = config->getStringValue("update_check").toInt();
    ui->updateCheckBox->setChecked(configUpdateCheck);
#else
    ui->updateCheckBox->setVisible(false);
#endif
    const auto useRegexSearch = config->getStringValue("regex_search").toInt();
    ui->regexSearchCheckBox->setChecked(useRegexSearch);
    const auto rememberSizePosition = config->getStringValue("window_remember").toInt();
    ui->rememberSizePositionCheckBox->setChecked(rememberSizePosition);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const auto deleteToTrash = config->getStringValue("delete_to_trash").toInt();
    ui->deleteToTrashCheckBox->setChecked(deleteToTrash);
#else
    ui->deleteToTrashCheckBox->setVisible(false);
#endif
    ui->catalogSkinDirectory->setText(QDir::toNativeSeparators(
        config->getStringValue(QStringLiteral("blumach_catalog_skin_directory"))));
    ui->catalogSkinManufacturerMarks->setChecked(
        config->getStringValue(QStringLiteral("blumach_catalog_skin_manufacturer_marks")) != QStringLiteral("0"));
    updateCatalogSkinSummary();

    ui->radioButtonSystem->setChecked(color_scheme == 0);
    ui->radioButtonLight->setChecked(color_scheme == 1);
    ui->radioButtonDark->setChecked(color_scheme == 2);

#ifndef Q_OS_WINDOWS
    ui->groupBoxColorScheme->setHidden(true);
#endif
}

VMManagerPreferences::~VMManagerPreferences()
    = default;

// Bad copy pasta from machine add
void
VMManagerPreferences::chooseDirectoryLocation()
{
    QFileDialog::Options options = QFileDialog::ShowDirsOnly;
#ifdef Q_OS_LINUX
    options |= QFileDialog::DontUseNativeDialog;
#endif
    const auto directory = QFileDialog::getExistingDirectory(this, tr("Choose directory"), ui->systemDirectory->text(), options);
    if (!directory.isEmpty())
        ui->systemDirectory->setText(QDir::toNativeSeparators(directory));
}

void
VMManagerPreferences::on_pushButtonDefaultSystemDir_released()
{
    char temp[1024];
    plat_get_vmm_dir(temp, sizeof(temp));
    ui->systemDirectory->setText(QDir::toNativeSeparators(QDir(temp).path()));
}

void
VMManagerPreferences::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}

void
VMManagerPreferences::chooseCatalogSkinDirectory()
{
    const auto directory = QFileDialog::getExistingDirectory(
        this, tr("Choose BluMach skin package"), ui->catalogSkinDirectory->text(),
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;

    QString name;
    QString error;
    if (!BluMachCatalogSkin::inspectDirectory(directory, &name, &error)) {
        QMessageBox::warning(this, tr("Skin package unavailable"), error);
        return;
    }
    ui->catalogSkinDirectory->setText(QDir::toNativeSeparators(directory));
    ui->catalogSkinManufacturerMarks->setChecked(true);
    updateCatalogSkinSummary();
}

void
VMManagerPreferences::clearCatalogSkinDirectory()
{
    ui->catalogSkinDirectory->clear();
    ui->catalogSkinManufacturerMarks->setChecked(false);
    updateCatalogSkinSummary();
}

void
VMManagerPreferences::updateCatalogSkinSummary()
{
    const QString directory = ui->catalogSkinDirectory->text();
    ui->catalogSkinClearButton->setEnabled(!directory.isEmpty());
    ui->catalogSkinManufacturerMarks->setEnabled(!directory.isEmpty());
    if (directory.isEmpty()) {
        ui->catalogSkinStatus->setText(tr("No visual package selected. BluMach uses its neutral appearance."));
        return;
    }
    QString name;
    QString error;
    if (BluMachCatalogSkin::inspectDirectory(directory, &name, &error))
        ui->catalogSkinStatus->setText(tr("Selected package: %1").arg(name));
    else
        ui->catalogSkinStatus->setText(tr("Selected package is unavailable: %1").arg(error));
}

void
VMManagerPreferences::accept()
{
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);

    strncpy(vmm_path_cfg, QDir::cleanPath(ui->systemDirectory->text()).toUtf8().constData(), sizeof(vmm_path_cfg) - 1);
    lang_id      = ui->comboBoxLanguage->currentData().toInt();
    color_scheme = (ui->radioButtonSystem->isChecked()) ? 0 : (ui->radioButtonLight->isChecked() ? 1 : 2);
    config_save_global();

#if EMU_BUILD_NUM != 0
    config->setStringValue("update_check", ui->updateCheckBox->isChecked() ? "1" : "0");
#endif
    config->setStringValue("window_remember", ui->rememberSizePositionCheckBox->isChecked() ? "1" : "0");
    config->setStringValue("regex_search", ui->regexSearchCheckBox->isChecked() ? "1" : "0");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    config->setStringValue("delete_to_trash", ui->deleteToTrashCheckBox->isChecked() ? "1" : "0");
#endif
    config->setStringValue(QStringLiteral("blumach_catalog_skin_directory"),
                           QDir::cleanPath(ui->catalogSkinDirectory->text()));
    config->setStringValue(QStringLiteral("blumach_catalog_skin_manufacturer_marks"),
                           ui->catalogSkinManufacturerMarks->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    config->sync();
    QDialog::accept();
}

void
VMManagerPreferences::reject()
{
    QDialog::reject();
}
