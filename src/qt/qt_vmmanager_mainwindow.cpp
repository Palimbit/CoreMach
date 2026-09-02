/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager main window
 *
 * Authors: cold-brewed
 *          rtzor
 *
 *          Copyright 2024 cold-brewed
 *          Copyright 2026 rtzor
 */
#include "qt_vmmanager_mainwindow.hpp"
#include "qt_vmmanager_main.hpp"
#include "qt_blumach_collection.hpp"
#include "qt_vmmanager_preferences.hpp"
#include "qt_vmmanager_windarkmodefilter.hpp"
#include "ui_qt_vmmanager_mainwindow.h"
#if EMU_BUILD_NUM != 0
#    include "qt_updatecheckdialog.hpp"
#endif
#include "qt_about.hpp"
#include "qt_preferences.hpp"
#include "qt_util.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

extern "C" {
extern void config_load_global();
extern void config_save_global();
}

namespace {
QColor
blendShellColor(const QColor &background, const QColor &foreground, const qreal amount)
{
    const qreal inverse = 1.0 - amount;
    return QColor::fromRgbF(background.redF() * inverse + foreground.redF() * amount,
                            background.greenF() * inverse + foreground.greenF() * amount,
                            background.blueF() * inverse + foreground.blueF() * amount);
}

QIcon
toolbarIcon(const QString &resourcePath)
{
    const QPixmap source(resourcePath);
    if (source.isNull())
        return QIcon(resourcePath);

    QPixmap disabled(source.size());
    disabled.fill(Qt::transparent);
    disabled.setDevicePixelRatio(source.devicePixelRatio());
    {
        QPainter painter(&disabled);
        painter.setOpacity(0.34);
        painter.drawPixmap(0, 0, source);
    }

    QIcon icon;
    icon.addPixmap(source, QIcon::Normal, QIcon::Off);
    icon.addPixmap(source, QIcon::Active, QIcon::Off);
    icon.addPixmap(disabled, QIcon::Disabled, QIcon::Off);
    return icon;
}
} // namespace

VMManagerMainWindow          *vmm_main_window = nullptr;
extern WindowsDarkModeFilter *vmm_dark_mode_filter;

VMManagerMainWindow::
    VMManagerMainWindow(QWidget *parent)
    : ui(new Ui::VMManagerMainWindow)
    , vmm(new VMManagerMain(this))
    , collection(new BluMachCollectionWidget(this))
    , mainStack(new QStackedWidget(this))
    , navigationHeader(new QFrame(this))
    , collectionNavButton(new QPushButton(this))
    , machinesNavButton(new QPushButton(this))
    , primaryActionButton(new QPushButton(this))
    , moreActionsButton(new QToolButton(this))
    , moreActionsWidgetAction(nullptr)
    , statusLeft(new QLabel)
    , statusRight(new QLabel)
{
    ui->setupUi(this);

    vmm_main_window = this;

    runIcon = toolbarIcon(QStringLiteral(":/blumach/ui/play.png"));
    pauseIcon = toolbarIcon(QStringLiteral(":/blumach/ui/pause.png"));
    ui->actionStartPause->setIcon(runIcon);
    ui->actionHard_Reset->setIcon(toolbarIcon(QStringLiteral(":/blumach/ui/restart.png")));
    ui->actionForce_Shutdown->setIcon(toolbarIcon(QStringLiteral(":/blumach/ui/power.png")));
    ui->actionCtrl_Alt_Del->setIcon(toolbarIcon(QStringLiteral(":/blumach/ui/keyboard-command.png")));
    ui->actionSettings->setIcon(toolbarIcon(QStringLiteral(":/blumach/ui/settings.png")));
    ui->actionNew_Machine->setIcon(toolbarIcon(QStringLiteral(":/blumach/ui/new-machine.png")));

    // Connect signals from the VMManagerMain widget
    connect(vmm, &VMManagerMain::selectionOrStateChanged, this, &VMManagerMainWindow::vmmStateChanged);

    setWindowTitle(tr("%1 Historical Computer Collection").arg(EMU_NAME));

    navigationHeader->setObjectName(QStringLiteral("blumachNavigationHeader"));
    collectionNavButton->setObjectName(QStringLiteral("blumachCollectionNav"));
    machinesNavButton->setObjectName(QStringLiteral("blumachMachinesNav"));
    primaryActionButton->setObjectName(QStringLiteral("blumachPrimaryAction"));
    collectionNavButton->setText(tr("Collection"));
    machinesNavButton->setText(tr("My machines"));
    collectionNavButton->setMinimumWidth(96);
    machinesNavButton->setMinimumWidth(112);
    collectionNavButton->setCheckable(true);
    machinesNavButton->setCheckable(true);
    collectionNavButton->setAutoExclusive(true);
    machinesNavButton->setAutoExclusive(true);
    collectionNavButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    machinesNavButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    collectionNavButton->setToolTip(QStringLiteral("%1 (%2)")
                                        .arg(collectionNavButton->text(),
                                             collectionNavButton->shortcut().toString(QKeySequence::NativeText)));
    machinesNavButton->setToolTip(QStringLiteral("%1 (%2)")
                                      .arg(machinesNavButton->text(),
                                           machinesNavButton->shortcut().toString(QKeySequence::NativeText)));

    auto *navigationGroup = new QButtonGroup(this);
    navigationGroup->setExclusive(true);
    navigationGroup->addButton(collectionNavButton);
    navigationGroup->addButton(machinesNavButton);

    auto *headerLayout = new QHBoxLayout(navigationHeader);
    headerLayout->setContentsMargins(18, 7, 18, 7);
    headerLayout->setSpacing(6);
    auto *brandLabel = new QLabel(QStringLiteral("BluMach"), navigationHeader);
    brandLabel->setObjectName(QStringLiteral("blumachBrand"));
    headerLayout->addWidget(brandLabel);
    headerLayout->addSpacing(14);
    headerLayout->addWidget(collectionNavButton);
    headerLayout->addWidget(machinesNavButton);
    headerLayout->addStretch(1);
    headerLayout->addWidget(primaryActionButton);
    setTabOrder(collectionNavButton, machinesNavButton);
    setTabOrder(machinesNavButton, primaryActionButton);

    mainStack->addWidget(collection);
    mainStack->addWidget(vmm);
    // Let the active page reflow inside compact windows instead of propagating
    // the larger desktop-oriented size hint from either stacked page.
    mainStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *centralWidget = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    removeToolBar(ui->toolBar);
    ui->toolBar->setParent(centralWidget);
    ui->toolBar->setMovable(false);
    ui->toolBar->setFloatable(false);
    ui->toolBar->removeAction(ui->actionNew_Machine);
    if (!ui->toolBar->actions().isEmpty() && ui->toolBar->actions().constFirst()->isSeparator())
        ui->toolBar->removeAction(ui->toolBar->actions().constFirst());

    moreActionsButton->setObjectName(QStringLiteral("blumachMoreMachineActions"));
    moreActionsButton->setText(tr("More"));
    moreActionsButton->setToolTip(tr("More"));
    moreActionsButton->setPopupMode(QToolButton::InstantPopup);
    moreActionsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    const auto moreActionsMenu = new QMenu(moreActionsButton);
    moreActionsMenu->addAction(ui->actionHard_Reset);
    moreActionsMenu->addAction(ui->actionForce_Shutdown);
    moreActionsMenu->addAction(ui->actionCtrl_Alt_Del);
    moreActionsButton->setMenu(moreActionsMenu);
    moreActionsWidgetAction = ui->toolBar->insertWidget(ui->actionSettings, moreActionsButton);
    moreActionsWidgetAction->setVisible(false);

    ui->toolBar->setIconSize(QSize(20, 20));
    ui->toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    if (const auto startButton = qobject_cast<QToolButton *>(ui->toolBar->widgetForAction(ui->actionStartPause)))
        startButton->setObjectName(QStringLiteral("blumachPrimaryMachineAction"));
    centralLayout->addWidget(navigationHeader);
    centralLayout->addWidget(ui->toolBar);
    centralLayout->addWidget(mainStack, 1);
    setCentralWidget(centralWidget);

    connect(collectionNavButton, &QPushButton::clicked, this, [this] { mainStack->setCurrentWidget(collection); });
    connect(machinesNavButton, &QPushButton::clicked, this, [this] { mainStack->setCurrentWidget(vmm); });
    connect(primaryActionButton, &QPushButton::clicked, this, [this] {
        if (mainStack->currentWidget() == collection)
            collection->createSelectedMachine();
        else
            vmm->newMachineWizard();
    });
    const auto primaryShortcut = new QShortcut(QKeySequence::New, this);
    primaryShortcut->setContext(Qt::WindowShortcut);
    connect(primaryShortcut, &QShortcut::activated, primaryActionButton, &QPushButton::click);

    connect(collection, &BluMachCollectionWidget::showMachinesRequested, this, [this] {
        mainStack->setCurrentWidget(vmm);
    });
    connect(collection, &BluMachCollectionWidget::createMachineRequested, this, [this](const QString &productId, const QString &machineId) {
        mainStack->setCurrentWidget(vmm);
        vmm->newHistoricalMachine(productId, machineId);
    });
    connect(collection, &BluMachCollectionWidget::selectionContextChanged, this, [this](const QString &, const QString &, const bool canCreate) {
        collectionSelectionCanCreate = canCreate;
        if (mainStack->currentWidget() == collection)
            primaryActionButton->setEnabled(canCreate);
    });
    connect(mainStack, &QStackedWidget::currentChanged, this, [this](int) {
        updateActiveSection();
    });

    // Set up the buttons
    connect(ui->actionNew_Machine, &QAction::triggered, vmm, &VMManagerMain::newMachineWizard);
    connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    connect(ui->actionSettings, &QAction::triggered, vmm, &VMManagerMain::settingsButtonPressed);
    connect(ui->actionHard_Reset, &QAction::triggered, vmm, &VMManagerMain::restartButtonPressed);
    connect(ui->actionForce_Shutdown, &QAction::triggered, vmm, &VMManagerMain::shutdownForceButtonPressed);
    connect(ui->actionCtrl_Alt_Del, &QAction::triggered, vmm, &VMManagerMain::cadButtonPressed);

// Set up menu actions
// (Disable this if the EMU_BUILD_NUM == 0)
#if EMU_BUILD_NUM == 0
    ui->actionCheck_for_updates->setVisible(false);
#else
    connect(ui->actionCheck_for_updates, &QAction::triggered, this, &VMManagerMainWindow::checkForUpdatesTriggered);
#endif

    // Set up the toolbar
    ui->actionStartPause->setEnabled(false);
    ui->actionStartPause->setIcon(runIcon);
    ui->actionStartPause->setText(tr("Start"));
    ui->actionStartPause->setToolTip(tr("Start"));
    ui->actionHard_Reset->setEnabled(false);
    ui->actionForce_Shutdown->setEnabled(false);
    ui->actionCtrl_Alt_Del->setEnabled(false);
    ui->actionSettings->setEnabled(false);

    // Preferences
    connect(ui->actionPreferences, &QAction::triggered, this, &VMManagerMainWindow::preferencesTriggered);

#ifdef Q_OS_WINDOWS
    ui->toolBar->setBackgroundRole(QPalette::Light);
#endif

    // Status bar widgets
    statusLeft->setAlignment(Qt::AlignLeft);
    statusRight->setAlignment(Qt::AlignRight);
    ui->statusbar->addPermanentWidget(statusLeft, 1);
    ui->statusbar->addPermanentWidget(statusRight, 1);
    connect(vmm, &VMManagerMain::updateStatusLeft, this, &VMManagerMainWindow::setStatusLeft);
    connect(vmm, &VMManagerMain::updateStatusRight, this, &VMManagerMainWindow::setStatusRight);

    // Inform the main view when preferences are updated
    connect(this, &VMManagerMainWindow::preferencesUpdated, vmm, &VMManagerMain::onPreferencesUpdated);
    connect(this, &VMManagerMainWindow::languageUpdated, vmm, &VMManagerMain::onLanguageUpdated);
#ifdef Q_OS_WINDOWS
    connect(this, &VMManagerMainWindow::darkModeUpdated, vmm, &VMManagerMain::onDarkModeUpdated);
    connect(this, &VMManagerMainWindow::darkModeUpdated, collection, &BluMachCollectionWidget::updateAppearance);
    connect(this, &VMManagerMainWindow::preferencesUpdated, []() { vmm_dark_mode_filter->reselectDarkMode(); });
#endif

    {
        auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
        toolBarHiddenByUser = !!config->getStringValue("hide_tool_bar").toInt();
        ui->actionHide_tool_bar->setChecked(toolBarHiddenByUser);
        if (!!config->getStringValue("window_remember").toInt()) {
            QString coords = config->getStringValue("window_coordinates");
            if (!coords.isEmpty()) {
                QStringList list = coords.split(',');
                for (auto &cur : list) {
                    cur = cur.trimmed();
                }
                QRect geom;
                geom.setX(list[0].toInt());
                geom.setY(list[1].toInt());
                geom.setWidth(list[2].toInt());
                geom.setHeight(list[3].toInt());

                setGeometry(geom);
            }

            if (!!config->getStringValue("window_maximized").toInt()) {
                setWindowState(windowState() | Qt::WindowMaximized);
            }

            QString splitter = config->getStringValue("window_splitter");
            if (!splitter.isEmpty()) {
                QStringList list = splitter.split(',');
                for (auto &cur : list) {
                    cur = cur.trimmed();
                }
                QList<int> paneSizes;
                paneSizes.append(list[0].toInt());
                paneSizes.append(list[1].toInt());

                vmm->setPaneSizes(paneSizes);
            }
        } else {
            config->setStringValue("window_coordinates", "");
            config->setStringValue("window_maximized", "");
            config->setStringValue("window_splitter", "");
        }
        delete config;
    }

    updateActiveSection();
    updateShellAppearance();
    updateResponsiveLayout();
}

VMManagerMainWindow::~VMManagerMainWindow()
    = default;

void
VMManagerMainWindow::vmmStateChanged(const VMManagerSystem *sysconfig) const
{
    if (sysconfig == nullptr) {
        // This doubles both as a safety check and a way to disable
        // all machine-related buttons when no machines are present
        ui->actionStartPause->setEnabled(false);
        ui->actionSettings->setEnabled(false);
        ui->actionHard_Reset->setEnabled(false);
        ui->actionForce_Shutdown->setEnabled(false);
        ui->actionCtrl_Alt_Del->setEnabled(false);
        return;
    }
    const bool running = sysconfig->process->state() == QProcess::ProcessState::Running;

    if (running) {
        if (sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running) {
            ui->actionStartPause->setIcon(pauseIcon);
            ui->actionStartPause->setText(tr("&Pause"));
            ui->actionStartPause->setToolTip(tr("Pause"));
            ui->actionStartPause->setIconText(tr("Pause"));
        } else {
            ui->actionStartPause->setIcon(runIcon);
            ui->actionStartPause->setText(tr("&Continue"));
            ui->actionStartPause->setToolTip(tr("Continue"));
            ui->actionStartPause->setIconText(tr("Continue"));
        }
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
    } else {
        ui->actionStartPause->setIcon(runIcon);
        ui->actionStartPause->setText(tr("&Start"));
        ui->actionStartPause->setToolTip(tr("Start"));
        ui->actionStartPause->setIconText(tr("Start"));
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    }

    ui->actionStartPause->setEnabled(!sysconfig->window_obscured);
    ui->actionSettings->setEnabled(!sysconfig->window_obscured);
    ui->actionHard_Reset->setEnabled(sysconfig->window_obscured ? false : running);
    ui->actionForce_Shutdown->setEnabled(sysconfig->window_obscured ? false : running);
    ui->actionCtrl_Alt_Del->setEnabled(sysconfig->window_obscured ? false : running);
}

void
VMManagerMainWindow::updateActiveSection()
{
    const bool machinesActive = mainStack->currentWidget() == vmm;
    collectionNavButton->setChecked(!machinesActive);
    machinesNavButton->setChecked(machinesActive);
    ui->toolBar->setVisible(machinesActive && !toolBarHiddenByUser);
    ui->statusbar->setVisible(machinesActive);
    ui->actionHide_tool_bar->setVisible(machinesActive);
    ui->actionNew_Machine->setVisible(machinesActive);
    primaryActionButton->setText(machinesActive ? tr("New machine…") : tr("Create this machine…"));
    primaryActionButton->setToolTip(QStringLiteral("%1 (%2)")
                                        .arg(primaryActionButton->text(),
                                             QKeySequence(QKeySequence::New).toString(QKeySequence::NativeText)));
    primaryActionButton->setEnabled(machinesActive || collectionSelectionCanCreate);
    vmmStateChanged(machinesActive ? vmm->getSelectedSystem() : nullptr);
}

void
VMManagerMainWindow::updateShellAppearance()
{
    const auto shellPalette = QApplication::palette();
    bool dark = shellPalette.color(QPalette::Window).lightnessF() < 0.5;
#ifdef Q_OS_WINDOWS
    dark = !util::isWindowsLightTheme();
#endif
    const QColor windowColor = shellPalette.color(QPalette::Window);
    const QColor textColor = shellPalette.color(QPalette::WindowText);
    const QColor highlightColor = shellPalette.color(QPalette::Highlight);
    // Keep the BluMach brand action recognisable even on Windows themes whose
    // Link and Highlight roles are neutral grey or white.
    const QColor accentColor = dark ? QColor(QStringLiteral("#2388df"))
                                    : QColor(QStringLiteral("#0b70c9"));
    const QColor accentTextColor(Qt::white);
    const QColor accentHoverColor = blendShellColor(accentColor, accentTextColor, dark ? 0.13 : 0.09);
    const QColor borderColor = blendShellColor(windowColor, textColor, dark ? 0.28 : 0.16);
    const QColor hoverColor = blendShellColor(windowColor, textColor, dark ? 0.10 : 0.045);
    const QColor selectedColor = blendShellColor(windowColor, highlightColor, dark ? 0.34 : 0.13);
    const QColor mutedColor = blendShellColor(windowColor, textColor, dark ? 0.52 : 0.46);
    ui->menubar->setStyleSheet(QStringLiteral(
        "QMenuBar { color: %1; background: %2; }"
        "QMenuBar::item { color: %1; background: transparent; padding: 5px 9px; }"
        "QMenuBar::item:selected { background: %3; }"
        "QMenu { color: %1; background: %2; border: 1px solid %4; }"
        "QMenu::item:selected { background: %3; }")
        .arg(textColor.name())
        .arg(windowColor.name())
        .arg(hoverColor.name())
        .arg(borderColor.name()));
    navigationHeader->setStyleSheet(QStringLiteral(
        "QFrame#blumachNavigationHeader { background: %1; border-bottom: 1px solid %2; }"
        "QLabel#blumachBrand { color: %3; background: transparent; font-size: 18px; font-weight: 600; padding-right: 8px; }"
        "QPushButton#blumachCollectionNav, QPushButton#blumachMachinesNav { color: %3; background: transparent; border: 0; border-radius: 7px; padding: 7px 12px; }"
        "QPushButton#blumachCollectionNav:hover, QPushButton#blumachMachinesNav:hover { background: %4; }"
        "QPushButton#blumachCollectionNav:checked, QPushButton#blumachMachinesNav:checked { color: %3; background: %5; font-weight: 600; }"
        "QPushButton#blumachPrimaryAction { color: %6; background: %7; border: 0; border-radius: 7px; padding: 8px 15px; font-weight: 600; }"
        "QPushButton#blumachPrimaryAction:disabled { color: %8; background: %4; }"
        "QPushButton#blumachPrimaryAction:hover:!disabled { background: %7; }")
        .arg(windowColor.name(), borderColor.name(), textColor.name(), hoverColor.name(),
             selectedColor.name(), accentTextColor.name(), accentColor.name(), mutedColor.name()));
    ui->toolBar->setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; border-bottom: 1px solid %1; padding: 5px 12px; spacing: 4px; }"
        "QToolButton { color: %2; border: 1px solid transparent; border-radius: 6px; padding: 6px 9px; }"
        "QToolButton:hover:!disabled { background: %3; }"
        "QToolButton:pressed:!disabled { background: %4; }"
        "QToolButton:focus { border-color: %5; }"
        "QToolButton:disabled { color: %6; }"
        "QToolButton#blumachPrimaryMachineAction:!disabled { color: %7; background: %5; font-weight: 600; }"
        "QToolButton#blumachPrimaryMachineAction:hover:!disabled { background: %8; }"
        "QToolButton#blumachMoreMachineActions { padding-left: 11px; padding-right: 11px; }")
        .arg(borderColor.name(), textColor.name(), hoverColor.name(), selectedColor.name(),
             accentColor.name(), mutedColor.name(), accentTextColor.name(), accentHoverColor.name()));
}

void
VMManagerMainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateResponsiveLayout();
}

void
VMManagerMainWindow::updateResponsiveLayout()
{
    const bool compact = width() < 900;
    if (compact == compactShell)
        return;
    compactShell = compact;

    if (compact) {
        ui->toolBar->removeAction(ui->actionHard_Reset);
        ui->toolBar->removeAction(ui->actionForce_Shutdown);
        ui->toolBar->removeAction(ui->actionCtrl_Alt_Del);
        moreActionsWidgetAction->setVisible(true);
    } else {
        const auto toolbarActions = ui->toolBar->actions();
        if (!toolbarActions.contains(ui->actionHard_Reset))
            ui->toolBar->insertAction(ui->actionSettings, ui->actionHard_Reset);
        if (!ui->toolBar->actions().contains(ui->actionForce_Shutdown))
            ui->toolBar->insertAction(ui->actionSettings, ui->actionForce_Shutdown);
        if (!ui->toolBar->actions().contains(ui->actionCtrl_Alt_Del))
            ui->toolBar->insertAction(ui->actionSettings, ui->actionCtrl_Alt_Del);
        moreActionsWidgetAction->setVisible(false);
    }

    ui->toolBar->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly
                                            : Qt::ToolButtonTextBesideIcon);
    if (const auto startButton = qobject_cast<QToolButton *>(ui->toolBar->widgetForAction(ui->actionStartPause)))
        startButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ui->toolBar->setIconSize(compact ? QSize(22, 22) : QSize(20, 20));
    navigationHeader->layout()->setContentsMargins(compact ? 12 : 18, 7,
                                                    compact ? 12 : 18, 7);
}

void
VMManagerMainWindow::preferencesTriggered()
{
    bool machinesRunning = (vmm->getActiveMachineCount() > 0);
    auto old_vmm_path = QString(vmm_path_cfg);
    const auto prefs = new VMManagerPreferences(this, machinesRunning);
    if (prefs->exec() == QDialog::Accepted) {
        emit preferencesUpdated();
        updateLanguage();

        auto new_vmm_path = QString(vmm_path_cfg);
        if (!machinesRunning && (new_vmm_path != old_vmm_path)) {
            qDebug() << "Machine path changed: old path " << old_vmm_path << ", new path " << new_vmm_path;
            strncpy(vmm_path, vmm_path_cfg, sizeof(vmm_path));
            vmm->reload();
        }
    }
}

void
VMManagerMainWindow::updateSettings()
{
    config_load_global();
    emit preferencesUpdated();
    updateLanguage();
}

void
VMManagerMainWindow::saveSettings() const
{
    const auto currentSelection = vmm->getCurrentSelection();
    const auto config           = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    config->setStringValue("last_selection", currentSelection);
    config->setStringValue("hide_tool_bar", toolBarHiddenByUser ? "1" : "0");
    if (!!config->getStringValue("window_remember").toInt()) {
        config->setStringValue("window_coordinates", QString::asprintf("%i, %i, %i, %i", this->geometry().x(), this->geometry().y(), this->geometry().width(), this->geometry().height()));
        config->setStringValue("window_maximized", this->isMaximized() ? "1" : "");
        config->setStringValue("window_splitter", QString::asprintf("%i, %i", vmm->getPaneSizes()[0], vmm->getPaneSizes()[1]));
    } else {
        config->setStringValue("window_coordinates", "");
        config->setStringValue("window_maximized", "");
        config->setStringValue("window_splitter", "");
    }
    // Sometimes required to ensure the settings save before the app exits
    config->sync();
}

void
VMManagerMainWindow::updateLanguage()
{
    Preferences::loadTranslators(QCoreApplication::instance());
    Preferences::reloadStrings();
    ui->retranslateUi(this);
    setWindowTitle(tr("%1 Historical Computer Collection").arg(EMU_NAME));
    collectionNavButton->setText(tr("Collection"));
    machinesNavButton->setText(tr("My machines"));
    collectionNavButton->setToolTip(QStringLiteral("%1 (%2)")
                                        .arg(collectionNavButton->text(),
                                             collectionNavButton->shortcut().toString(QKeySequence::NativeText)));
    machinesNavButton->setToolTip(QStringLiteral("%1 (%2)")
                                      .arg(machinesNavButton->text(),
                                           machinesNavButton->shortcut().toString(QKeySequence::NativeText)));
    moreActionsButton->setText(tr("More"));
    moreActionsButton->setToolTip(tr("More"));
    collection->reloadLanguage();
    updateActiveSection();
    emit languageUpdated();
}

#ifdef Q_OS_WINDOWS
void
VMManagerMainWindow::updateDarkMode()
{
    updateShellAppearance();
    emit darkModeUpdated();
}
#endif

void
VMManagerMainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        updateShellAppearance();
        collection->updateAppearance();
    }
#ifdef Q_OS_WINDOWS
    if (event->type() == QEvent::LanguageChange) {
        QApplication::setFont(QFont(Preferences::getUIFont()));
    }
#endif
    QWidget::changeEvent(event);
}

void
VMManagerMainWindow::closeEvent(QCloseEvent *event)
{
    int running = vmm->getActiveMachineCount();
    if (running > 0) {
        QMessageBox warningbox(QMessageBox::Icon::Warning, tr("%1 VM Manager").arg(EMU_NAME), tr("%n machine(s) are currently active. Are you sure you want to exit the VM manager anyway?", "", running), QMessageBox::Yes | QMessageBox::No, this);
        warningbox.exec();
        if (warningbox.result() == QMessageBox::No) {
            event->ignore();
            return;
        }
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void
VMManagerMainWindow::setStatusLeft(const QString &text) const
{
    statusLeft->setText(text);
}

void
VMManagerMainWindow::setStatusRight(const QString &text) const
{
    statusRight->setText(text);
}

void
VMManagerMainWindow::on_actionHide_tool_bar_triggered()
{
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    toolBarHiddenByUser = ui->actionHide_tool_bar->isChecked();
    ui->toolBar->setVisible(mainStack->currentWidget() == vmm && !toolBarHiddenByUser);
    config->setStringValue("hide_tool_bar", toolBarHiddenByUser ? "1" : "0");
    config->sync();
    delete config;
}

#if EMU_BUILD_NUM != 0
void
VMManagerMainWindow::checkForUpdatesTriggered()
{
    auto updateChannel = UpdateCheck::UpdateChannel::CI;
#    ifdef RELEASE_BUILD
    updateChannel = UpdateCheck::UpdateChannel::Stable;
#    endif
    const auto updateCheck = new UpdateCheckDialog(updateChannel, this);
    updateCheck->exec();
}
#endif

void
VMManagerMainWindow::on_actionExit_triggered()
{
    this->close();
}

void
VMManagerMainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

void
VMManagerMainWindow::on_actionAbout_86Box_triggered()
{
    const auto msgBox = new About(this);
    msgBox->exec();
}

void
VMManagerMainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(EMU_DOCS_URL));
}
