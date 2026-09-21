/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_MACHINE_DIALOG_H
#define BLUMACH_PORTABLE_MACHINE_DIALOG_H

#include <blumach/frontend/frontend.h>

#include <QDialog>
#include <QHash>
#include <QString>

class PortableCatalog;
class QComboBox;
class QCheckBox;
class QFormLayout;
class QLineEdit;

class MachineDialog final : public QDialog {
public:
    explicit MachineDialog(const PortableCatalog &catalog,
                           QWidget *parent = nullptr);

    const bm_frontend_adapter_t *adapter() const;
    QString productId() const;
    QString profileName() const;
    bool saveProfile() const;
    QHash<QString, QString> paths() const;
    void selectMachine(const QString &machineId);
    void selectProduct(const QString &productId);
    void setPaths(const QHash<QString, QString> &paths);
    void setProfileName(const QString &name);
    void setEditingExistingProfile(bool editing);

protected:
    void accept() override;

private:
    void rebuildAssets();
    QString requirementText(const bm_frontend_asset_requirement_t &asset) const;

    QComboBox *machines_;
    QLineEdit *name_;
    QCheckBox *save_;
    QFormLayout *assetsLayout_;
    QHash<QString, QLineEdit *> editors_;
    QString defaultName_;
};

#endif
