/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_MACHINE_DIALOG_H
#define BLUMACH_PORTABLE_MACHINE_DIALOG_H

#include <blumach/frontend/frontend.h>

#include <QDialog>
#include <QHash>
#include <QString>
#include "resource_library.h"

class PortableCatalog;
class QComboBox;
class QCheckBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

class MachineDialog final : public QDialog {
public:
    explicit MachineDialog(const PortableCatalog &catalog,
                           const QString &resourceRoot,
                           QWidget *parent = nullptr);

    const bm_frontend_adapter_t *adapter() const;
    QString productId() const;
    QString profileName() const;
    bool saveProfile() const;
    QHash<QString, QString> paths() const;
    QHash<QString, quint32> options() const;
    QString resourceRoot() const;
    void selectMachine(const QString &machineId);
    void selectProduct(const QString &productId);
    void setMachineSelectionLocked(bool locked);
    void setPaths(const QHash<QString, QString> &paths);
    void setOptions(const QHash<QString, quint32> &options);
    void selectCustomFirmware();
    void setProfileName(const QString &name);
    void setEditingExistingProfile(bool editing);

protected:
    void accept() override;

private:
    void rebuildAssets();
    void rebuildConfigurations();
    void scanResources();
    void syncGeneratedAssetsWithPreset();
    void refreshFirmwareChoices(const QString &role);
    void updateAssetStatus(const QString &role);
    QString requirementText(const bm_frontend_asset_requirement_t &asset) const;
    void updateAdvancedTab();

    QComboBox *machines_;
    QLineEdit *name_;
    QCheckBox *save_;
    QFormLayout *assetsLayout_;
    QFormLayout *advancedAssetsLayout_;
    QFormLayout *configurationLayout_;
    QFormLayout *advancedConfigurationLayout_;
    QTabWidget *formTabs_;
    QWidget *advancedPage_;
    QLabel *advancedEmpty_;
    QHash<QString, QComboBox *> configurationChoices_;
    QComboBox *presetChoice_ = nullptr;
    bool applyingPreset_ = false;
    QHash<QString, QLineEdit *> editors_;
    QHash<QString, QComboBox *> firmwareChoices_;
    QHash<QString, QCheckBox *> generateAssetChecks_;
    QHash<QString, QString> customFirmwarePaths_;
    QHash<QString, QLabel *> assetStatus_;
    QHash<QString, QPushButton *> folderButtons_;
    const PortableCatalog &catalog_;
    QString resourceRoot_;
    PortableResourceScan scan_;
    QLabel *rootLabel_;
    QLabel *scanStatus_;
    bool editingExisting_ = false;
    QString defaultName_;
};

#endif
