/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_MACHINE_DIALOG_H
#define BLUMACH_PORTABLE_MACHINE_DIALOG_H

#include <blumach/frontend/frontend.h>

#include <QDialog>
#include <QHash>
#include <QString>

class QComboBox;
class QFormLayout;
class QLineEdit;

class MachineDialog final : public QDialog {
public:
    explicit MachineDialog(QWidget *parent = nullptr);

    const bm_frontend_adapter_t *adapter() const;
    QHash<QString, QString> paths() const;
    void selectMachine(const QString &machineId);
    void setPaths(const QHash<QString, QString> &paths);

protected:
    void accept() override;

private:
    void rebuildAssets();
    QString requirementText(const bm_frontend_asset_requirement_t &asset) const;

    QComboBox *machines_;
    QFormLayout *assetsLayout_;
    QHash<QString, QLineEdit *> editors_;
};

#endif
