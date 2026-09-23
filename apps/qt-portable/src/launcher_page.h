/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_LAUNCHER_PAGE_H
#define BLUMACH_PORTABLE_LAUNCHER_PAGE_H

#include "portable_catalog.h"
#include "machine_profile_store.h"

#include <QWidget>
#include <QHash>
#include <QImage>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QComboBox;
class QTabWidget;
class QFrame;

class LauncherPage final : public QWidget {
public:
    LauncherPage(const PortableCatalog &catalog, const QString &catalogError,
                 std::function<void(const QString &)> launch,
                 std::function<void(const QString &)> openSaved,
                 std::function<void(const QString &)> editSaved,
                 std::function<void()> showRunning,
                 QWidget *parent = nullptr);
    void setProfiles(const QVector<PortableMachineProfile> &profiles);
    void setProfileRoot(const QString &root);
    void setActiveMachine(const QString &name,
                          const QString &profileId = QString());
    void setRunningPreview(const QImage &frame);
    void setProfilePreview(const QString &profileId, const QImage &frame);

private:
    void filter(const QString &query);
    void select(const PortableCatalogMachine &machine);
    void selectProduct(const QString &productId);

    const PortableCatalog &catalog_;
    std::function<void(const QString &)> launch_;
    std::function<void(const QString &)> openSaved_;
    std::function<void(const QString &)> editSaved_;
    std::function<void()> showRunning_;
    QListWidget *saved_;
    QLabel *emptyLibrary_;
    QPushButton *showRunningButton_;
    QLabel *runningStatus_;
    QLabel *runningPreview_;
    QFrame *runningCard_;
    QTabWidget *sections_;
    QString profileRoot_;
    QString activeProfileId_;
    QHash<QString, QImage> previews_;
    QHash<QString, QLabel *> profilePreviewLabels_;
    QHash<QString, QLabel *> profileStatusLabels_;
    QHash<QString, bool> profileAvailable_;
    QLineEdit *search_;
    QComboBox *availabilityFilter_;
    QLabel *results_;
    QTreeWidget *tree_;
    QLabel *image_;
    QLabel *mediaCaption_;
    QLabel *title_;
    QLabel *period_;
    QLabel *summary_;
    QLabel *catalogStatus_;
    QLabel *availability_;
    QLabel *warning_;
    QTextBrowser *details_;
    QTextBrowser *technical_;
    QTextBrowser *engineering_;
    QTextBrowser *sources_;
    QTabWidget *tabs_;
    QPushButton *launchButton_;
    QString selectedProductId_;
};

#endif
