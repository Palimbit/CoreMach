/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager system details module
 *
 * Authors: cold-brewed
 *          rtzor
 *
 *          Copyright 2024 cold-brewed
 *          Copyright 2026 rtzor
 */
#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>

extern "C" {
#include <86box/86box.h>
}

#include "qt_preferences.hpp"
#include "qt_util.hpp"
#include "qt_vmmanager_details.hpp"
#include "ui_qt_vmmanager_details.h"

#define TOOLBUTTON_STYLESHEET_LIGHT "QToolButton {background: transparent; border: none; padding: 5px} QToolButton:hover {background: palette(midlight)} QToolButton:pressed {background: palette(mid)}"
#ifdef Q_OS_WINDOWS
#    define TOOLBUTTON_STYLESHEET_DARK       "QToolButton {padding: 5px}"
#    define SCREENSHOTBORDER_STYLESHEET_DARK "QLabel { border: 1px solid gray }"
#else
#    define TOOLBUTTON_STYLESHEET_DARK "QToolButton {background: transparent; border: none; padding: 5px} QToolButton:hover {background: palette(dark)} QToolButton:pressed {background: palette(mid)}"
#endif
#define SCROLLAREA_STYLESHEET_LIGHT  ""
#define SYSTEMLABEL_STYLESHEET_LIGHT ""

using namespace VMManager;

VMManagerDetails::VMManagerDetails(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VMManagerDetails)
{
    ui->setupUi(this);

    ui->verticalLayout->setContentsMargins(20, 14, 18, 16);
    ui->verticalLayout->setSpacing(12);
    ui->horizontalLayout->setSpacing(16);

    machineHeader = new QFrame(this);
    machineHeader->setObjectName(QStringLiteral("blumachMachineHeader"));
    auto *machineHeaderLayout = new QHBoxLayout(machineHeader);
    machineHeaderLayout->setContentsMargins(14, 10, 12, 10);
    machineHeaderLayout->setSpacing(12);
    auto *machineIdentityLayout = new QVBoxLayout();
    machineIdentityLayout->setContentsMargins(0, 0, 0, 0);
    machineIdentityLayout->setSpacing(2);
    ui->verticalLayout->removeWidget(ui->systemLabel);
    machineIdentityLayout->addWidget(ui->systemLabel);
    machineMetaLabel = new QLabel(machineHeader);
    machineMetaLabel->setObjectName(QStringLiteral("blumachMachineMeta"));
    machineMetaLabel->setWordWrap(true);
    machineIdentityLayout->addWidget(machineMetaLabel);
    machineHeaderLayout->addLayout(machineIdentityLayout, 1);
    machineHeaderLayout->addWidget(ui->statusLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
    ui->verticalLayout->insertWidget(0, machineHeader);

    ui->systemLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->systemLabel->setMargin(0);
    ui->systemLabel->setWordWrap(true);
    ui->systemLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->scrollArea->setFrameShape(QFrame::NoFrame);
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    ui->rightColumn->setMinimumWidth(250);
    ui->rightColumn->setMaximumWidth(320);
    auto *rightColumnLayout = qobject_cast<QVBoxLayout *>(ui->rightColumn->layout());
    rightColumnLayout->setSpacing(8);
    const auto screenshotHeading = new QLabel(tr("Screenshots"), ui->rightColumn);
    screenshotHeading->setObjectName(QStringLiteral("blumachSupplementaryHeading"));
    rightColumnLayout->insertWidget(0, screenshotHeading);
    const auto notesHeading = new QLabel(tr("Notes"), ui->rightColumn);
    notesHeading->setObjectName(QStringLiteral("blumachSupplementaryHeading"));
    rightColumnLayout->insertWidget(rightColumnLayout->indexOf(ui->notesTextEdit), notesHeading);
    ui->notesTextEdit->setMinimumHeight(120);
    ui->toolButtonHolder->hide();
    ui->LeftRight->hide();

    const auto leftColumnLayout = qobject_cast<QVBoxLayout *>(ui->leftColumn->layout());
    leftColumnLayout->setSpacing(8);

    // Each section here gets its own VMManagerDetailSection, named in the constructor.
    // When a system is selected in the list view it is updated through this object
    // See updateData() for the implementation

    systemSection = new VMManagerDetailSection(tr("System", "Header for System section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(systemSection);
    // These horizontal lines are used for the alternate layout which may possibly
    // be a preference one day.
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    videoSection = new VMManagerDetailSection(tr("Display", "Header for Display section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(videoSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    storageSection = new VMManagerDetailSection(tr("Storage", "Header for Storage section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(storageSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    audioSection = new VMManagerDetailSection(tr("Audio", "Header for Audio section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(audioSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    networkSection = new VMManagerDetailSection(tr("Network", "Header for Network section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(networkSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    inputSection = new VMManagerDetailSection(tr("Input devices", "Header for Input section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(inputSection);
    // ui->leftColumn->layout()->addWidget(createHorizontalLine());

    portsSection = new VMManagerDetailSection(tr("Ports", "Header for Input section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(portsSection);

    otherSection = new VMManagerDetailSection(tr("Other devices", "Header for Other devices section in VM Manager Details"));
    ui->leftColumn->layout()->addWidget(otherSection);

    // This is like adding a spacer
    leftColumnLayout->addStretch();

    // Event filter for the notes to save when it loses focus
    ui->notesTextEdit->installEventFilter(this);

    // Default screenshot label and thumbnail (image inside the label) sizes
    screenshotThumbnailSize = QSize(240, 160);

    // Set the icons for the screenshot navigation buttons
    ui->screenshotNext->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowRight));
    ui->screenshotPrevious->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowLeft));
    // Disabled by default
    ui->screenshotNext->setEnabled(false);
    ui->screenshotPrevious->setEnabled(false);
    ui->screenshotNavButtons->hide();
    // Connect their signals
    connect(ui->screenshotNext, &QToolButton::clicked, this, &VMManagerDetails::nextScreenshot);
    connect(ui->screenshotPrevious, &QToolButton::clicked, this, &VMManagerDetails::previousScreenshot);
    QString toolButtonStyleSheet;
    // Simple method to try and determine if light mode is enabled
#ifdef Q_OS_WINDOWS
    const bool lightMode = util::isWindowsLightTheme();
#else
    const bool lightMode = QApplication::palette().window().color().value() > QApplication::palette().windowText().color().value();
#endif
    if (lightMode) {
        toolButtonStyleSheet = TOOLBUTTON_STYLESHEET_LIGHT;
    } else {
        toolButtonStyleSheet = TOOLBUTTON_STYLESHEET_DARK;
    }
    ui->ssNavTBHolder->setStyleSheet(toolButtonStyleSheet);

    pauseIcon = QIcon(":/menuicons/qt/icons/pause.ico");
    runIcon   = QIcon(":/menuicons/qt/icons/run.ico");

    // Experimenting
    startPauseButton = new QToolButton();
    startPauseButton->setIcon(runIcon);
    startPauseButton->setAutoRaise(true);
    startPauseButton->setEnabled(false);
    startPauseButton->setToolTip(tr("Start"));
    ui->toolButtonHolder->setStyleSheet(toolButtonStyleSheet);
    resetButton = new QToolButton();
    resetButton->setIcon(QIcon(":/menuicons/qt/icons/hard_reset.ico"));
    resetButton->setEnabled(false);
    resetButton->setToolTip(tr("Hard reset"));
    stopButton = new QToolButton();
    stopButton->setIcon(QIcon(":/menuicons/qt/icons/acpi_shutdown.ico"));
    stopButton->setEnabled(false);
    stopButton->setToolTip(tr("Force shutdown"));
    configureButton = new QToolButton();
    configureButton->setIcon(QIcon(":/menuicons/qt/icons/settings.ico"));
    configureButton->setEnabled(false);
    configureButton->setToolTip(tr("Settings…"));
    cadButton = new QToolButton();
    cadButton->setIcon(QIcon(":menuicons/qt/icons/send_cad.ico"));
    cadButton->setEnabled(false);
    cadButton->setToolTip(tr("Ctrl+Alt+Del"));

    ui->toolButtonHolder->layout()->addWidget(startPauseButton);
    ui->toolButtonHolder->layout()->addWidget(resetButton);
    ui->toolButtonHolder->layout()->addWidget(stopButton);
    ui->toolButtonHolder->layout()->addWidget(cadButton);
    ui->toolButtonHolder->layout()->addWidget(configureButton);

    ui->notesTextEdit->setEnabled(false);
    applyAppearance();

#ifdef Q_OS_WINDOWS
    connect(this, &VMManagerDetails::styleUpdated, systemSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, videoSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, storageSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, audioSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, networkSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, inputSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, portsSection, &VMManagerDetailSection::updateStyle);
    connect(this, &VMManagerDetails::styleUpdated, otherSection, &VMManagerDetailSection::updateStyle);

    QApplication::setFont(Preferences::getUIFont());
#endif

    sysconfig = new VMManagerSystem();
}

VMManagerDetails::~VMManagerDetails()
{
    delete ui;
}

QSize
VMManagerDetails::minimumSizeHint() const
{
    // The optional screenshot/notes column must not force the entire manager
    // wider than a compact window. updateResponsiveLayout() moves it below the
    // hardware cards when the available details width is small.
    return QSize(0, 0);
}

void
VMManagerDetails::reset()
{
    systemSection->clear();
    videoSection->clear();
    storageSection->clear();
    audioSection->clear();
    networkSection->clear();
    inputSection->clear();
    portsSection->clear();
    otherSection->clear();
    systemSection->setSections();
    videoSection->setSections();
    storageSection->setSections();
    audioSection->setSections();
    networkSection->setSections();
    inputSection->setSections();
    portsSection->setSections();
    otherSection->setSections();

    ui->screenshotNext->setEnabled(false);
    ui->screenshotPrevious->setEnabled(false);
    ui->screenshot->setPixmap(QString());
    ui->screenshot->setFixedSize(240, 160);
    ui->screenshot->setFrameStyle(QFrame::NoFrame);
    ui->screenshot->setText(tr("No screenshot"));
    ui->screenshot->setEnabled(true);
    ui->screenshot->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
#ifdef Q_OS_WINDOWS
    if (!util::isWindowsLightTheme()) {
        ui->screenshot->setStyleSheet(SCREENSHOTBORDER_STYLESHEET_DARK);
    } else {
        ui->screenshot->setStyleSheet("");
    }
#endif

    startPauseButton->setEnabled(false);
    resetButton->setEnabled(false);
    stopButton->setEnabled(false);
    configureButton->setEnabled(false);
    cadButton->setEnabled(false);

    ui->systemLabel->setText(tr("No machines found"));
    ui->systemLabel->setStyleSheet("");
    machineMetaLabel->clear();
    machineMetaLabel->hide();
    ui->statusLabel->setText("");
    ui->statusLabel->hide();
    ui->statusLabel->setProperty("machineState", QString());
    ui->scrollArea->setStyleSheet("");

    ui->notesTextEdit->setPlainText("");
    ui->notesTextEdit->setEnabled(false);
    ui->LeftRight->hide();

    sysconfig = new VMManagerSystem();
}

void
VMManagerDetails::updateData(VMManagerSystem *passed_sysconfig)
{
    ui->LeftRight->show();
    updateResponsiveLayout();

    // Set the scrollarea background but also set the scroll bar to none. Otherwise it will also
    // set the scrollbar background to the same.
#ifdef Q_OS_WINDOWS
    if (util::isWindowsLightTheme())
#endif
    {
        ui->scrollArea->setStyleSheet(SCROLLAREA_STYLESHEET_LIGHT);
        ui->systemLabel->setStyleSheet(SYSTEMLABEL_STYLESHEET_LIGHT);
    }

    // disconnect old signals before assigning the passed systemconfig object
    disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
    disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    disconnect(resetButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::restartButtonPressed);
    disconnect(stopButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::shutdownForceButtonPressed);
    disconnect(configureButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::launchSettings);
    disconnect(cadButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::cadButtonPressed);

    disconnect(sysconfig, &VMManagerSystem::configurationChanged, this, &VMManagerDetails::onConfigUpdated);

    sysconfig = passed_sysconfig;
    connect(resetButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::restartButtonPressed);
    connect(stopButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::shutdownForceButtonPressed);
    connect(configureButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::launchSettings);
    connect(cadButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::cadButtonPressed);
    cadButton->setEnabled(true);

    bool running = sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running || sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::RunningWaiting;
    if (running) {
        startPauseButton->setIcon(pauseIcon);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    } else {
        startPauseButton->setIcon(runIcon);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
    }
    startPauseButton->setEnabled(true);
    configureButton->setEnabled(true);

    updateConfig(passed_sysconfig);
    updateScreenshots(passed_sysconfig);

    ui->systemLabel->setText(passed_sysconfig->displayName);
    const QString machineDescription = passed_sysconfig->getDisplayValue(VMManager::Display::Name::Machine);
    machineMetaLabel->setText(machineDescription);
    machineMetaLabel->setVisible(!machineDescription.isEmpty());
    ui->statusLabel->show();
    ui->notesTextEdit->setPlainText(passed_sysconfig->notes);
    ui->notesTextEdit->setEnabled(true);

    disconnect(sysconfig->process, &QProcess::stateChanged, this, &VMManagerDetails::updateProcessStatus);
    connect(sysconfig->process, &QProcess::stateChanged, this, &VMManagerDetails::updateProcessStatus);

    disconnect(sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerDetails::updateWindowStatus);
    connect(sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerDetails::updateWindowStatus);

    disconnect(sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerDetails::updateProcessStatus);
    connect(sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerDetails::updateProcessStatus);

    connect(sysconfig, &VMManagerSystem::configurationChanged, this, &VMManagerDetails::onConfigUpdated);

    updateProcessStatus();
}

void
VMManagerDetails::onConfigUpdated(VMManagerSystem *passed_sysconfig)
{
    updateConfig(passed_sysconfig);
    updateScreenshots(passed_sysconfig);
}

void
VMManagerDetails::updateConfig(VMManagerSystem *passed_sysconfig)
{
    // Each detail section here has its own VMManagerDetailSection.
    // When a system is selected in the list view it is updated here, through this object:
    // * First you clear it with VMManagerDetailSection::clear()
    // * Then you add each line with VMManagerDetailSection::addSection()

    // System
    systemSection->clear();
    systemSection->addSection("CPU", passed_sysconfig->getDisplayValue(VMManager::Display::Name::CPU));
    systemSection->addSection("Memory", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Memory));

    // Video
    videoSection->clear();
    videoSection->addSection("Video", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Video));
    if (!passed_sysconfig->getDisplayValue(VMManager::Display::Name::Voodoo).isEmpty()) {
        videoSection->addSection("Voodoo", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Voodoo));
    }

    // Disks
    storageSection->clear();
    storageSection->addSection("Disks", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Disks));
    storageSection->addSection("Floppy", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Floppy));
    storageSection->addSection("CD-ROM", passed_sysconfig->getDisplayValue(VMManager::Display::Name::CD));
    storageSection->addSection("Removable disks", passed_sysconfig->getDisplayValue(VMManager::Display::Name::RDisk));
    storageSection->addSection("MO", passed_sysconfig->getDisplayValue(VMManager::Display::Name::MO));
    storageSection->addSection("Tape drives", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Tape));
    storageSection->addSection("SCSI", passed_sysconfig->getDisplayValue(VMManager::Display::Name::SCSIController));
    storageSection->addSection("Controllers", passed_sysconfig->getDisplayValue(VMManager::Display::Name::StorageController));

    // Audio
    audioSection->clear();
    audioSection->addSection("Audio", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Audio));
    audioSection->addSection("MIDI Out", passed_sysconfig->getDisplayValue(VMManager::Display::Name::MidiOut));

    // Network
    networkSection->clear();
    networkSection->addSection("NIC", passed_sysconfig->getDisplayValue(VMManager::Display::Name::NIC));

    // Input
    inputSection->clear();
    inputSection->addSection("Keyboard", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Keyboard));
    inputSection->addSection("Mouse", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Mouse));
    inputSection->addSection("Tablet", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Tablet));
    inputSection->addSection("Joystick", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Joystick));

    // Ports
    portsSection->clear();
    portsSection->addSection("Serial ports", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Serial));
    portsSection->addSection("Parallel ports", passed_sysconfig->getDisplayValue(VMManager::Display::Name::Parallel));

    // Other devices
    otherSection->clear();
    otherSection->addSection("ISA RTC", passed_sysconfig->getDisplayValue(VMManager::Display::Name::IsaRtc));
    otherSection->addSection("ISA RAM", passed_sysconfig->getDisplayValue(VMManager::Display::Name::IsaMem));
    otherSection->addSection("ISA ROM", passed_sysconfig->getDisplayValue(VMManager::Display::Name::IsaRom));

    systemSection->setSections();
    videoSection->setSections();
    storageSection->setSections();
    audioSection->setSections();
    networkSection->setSections();
    inputSection->setSections();
    portsSection->setSections();
    otherSection->setSections();
}

void
VMManagerDetails::updateScreenshots(VMManagerSystem *passed_sysconfig)
{
    // Disable screenshot navigation buttons by default
    ui->screenshotNext->setEnabled(false);
    ui->screenshotPrevious->setEnabled(false);
    ui->screenshotNavButtons->hide();

    // Different actions are taken depending on the existence and number of screenshots
    screenshots = passed_sysconfig->getScreenshots();
    if (!screenshots.empty()) {
        ui->screenshot->setFrameStyle(QFrame::NoFrame);
        ui->screenshot->setEnabled(true);
        if (screenshots.size() > 1) {
            ui->screenshotNext->setEnabled(true);
            ui->screenshotPrevious->setEnabled(true);
            ui->screenshotNavButtons->show();
        }
#ifdef Q_OS_WINDOWS
        ui->screenshot->setStyleSheet("");
#endif
        if (QFileInfo::exists(screenshots.last().filePath())) {
            screenshotIndex = screenshots.size() - 1;
            const QPixmap pic(screenshots.at(screenshotIndex).filePath());
            ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        ui->screenshotNext->setEnabled(false);
        ui->screenshotPrevious->setEnabled(false);
        ui->screenshot->setPixmap(QString());
        ui->screenshot->setFixedSize(240, 160);
        ui->screenshot->setFrameStyle(QFrame::NoFrame);
        ui->screenshot->setText(tr("No screenshot"));
        ui->screenshot->setEnabled(true);
        ui->screenshot->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
#ifdef Q_OS_WINDOWS
        if (!util::isWindowsLightTheme()) {
            ui->screenshot->setStyleSheet(SCREENSHOTBORDER_STYLESHEET_DARK);
        } else {
            ui->screenshot->setStyleSheet("");
        }
#endif
    }
}

void
VMManagerDetails::updateProcessStatus()
{
    const bool running = sysconfig->process->state() == QProcess::ProcessState::Running;
    ui->statusLabel->setText(sysconfig->getProcessStatusString());
    ui->statusLabel->setToolTip(running
                                    ? QStringLiteral("PID %1").arg(sysconfig->process->processId())
                                    : QString());
    updateStatusAppearance();
    resetButton->setEnabled(running);
    stopButton->setEnabled(running);
    cadButton->setEnabled(running);
    if (running) {
        if (sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running) {
            startPauseButton->setIcon(pauseIcon);
            startPauseButton->setToolTip(tr("Pause"));
        } else {
            startPauseButton->setIcon(runIcon);
            startPauseButton->setToolTip(tr("Continue"));
        }

        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
    } else {
        startPauseButton->setIcon(runIcon);
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::pauseButtonPressed);
        disconnect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
        connect(startPauseButton, &QToolButton::clicked, sysconfig, &VMManagerSystem::startButtonPressed);
        startPauseButton->setToolTip(tr("Start"));
    }

    if (sysconfig->window_obscured) {
        resetButton->setDisabled(true);
        stopButton->setDisabled(true);
        cadButton->setDisabled(true);
        startPauseButton->setDisabled(true);
        configureButton->setDisabled(true);
    } else {
        configureButton->setDisabled(false);
        startPauseButton->setDisabled(false);
    }
}

void
VMManagerDetails::updateWindowStatus()
{
    qInfo("Window status changed: %i", sysconfig->window_obscured);
    updateProcessStatus();
}

#ifdef Q_OS_WINDOWS
void
VMManagerDetails::updateStyle()
{
    applyAppearance();
    emit styleUpdated();
}
#endif

void
VMManagerDetails::updateStatusAppearance()
{
    QString state = QStringLiteral("stopped");
    if (sysconfig->process->state() == QProcess::ProcessState::Running) {
        const auto processStatus = sysconfig->getProcessStatus();
        state = (sysconfig->window_obscured
                 || processStatus == VMManagerSystem::ProcessStatus::Paused
                 || processStatus == VMManagerSystem::ProcessStatus::PausedWaiting
                 || processStatus == VMManagerSystem::ProcessStatus::RunningWaiting)
            ? QStringLiteral("waiting")
            : QStringLiteral("running");
    }

    if (ui->statusLabel->property("machineState").toString() == state)
        return;

    ui->statusLabel->setProperty("machineState", state);
    ui->statusLabel->style()->unpolish(ui->statusLabel);
    ui->statusLabel->style()->polish(ui->statusLabel);
}

void
VMManagerDetails::applyAppearance()
{
    const auto pal = QApplication::palette();
    bool dark = pal.color(QPalette::Window).lightnessF() < 0.5;
#ifdef Q_OS_WINDOWS
    dark = !util::isWindowsLightTheme();
#endif
    const QColor text = pal.color(QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor accent = pal.color(QPalette::Highlight);
    const auto blend = [](const QColor &background, const QColor &foreground, const qreal amount) {
        const qreal inverse = 1.0 - amount;
        return QColor::fromRgbF(background.redF() * inverse + foreground.redF() * amount,
                                background.greenF() * inverse + foreground.greenF() * amount,
                                background.blueF() * inverse + foreground.blueF() * amount);
    };
    const QColor border = blend(base, text, dark ? 0.28 : 0.16);
    const QColor muted = blend(base, text, dark ? 0.68 : 0.56);
    const QColor card = blend(base, text, dark ? 0.035 : 0.02);
    const QColor stoppedStatus = blend(base, text, dark ? 0.15 : 0.07);
    const QColor runningStatus = blend(base, accent, dark ? 0.30 : 0.14);
    const QColor waitingStatus = blend(base, pal.color(QPalette::Link), dark ? 0.26 : 0.12);
    auto contentPalette = pal;
    contentPalette.setColor(QPalette::Base, base);
    contentPalette.setColor(QPalette::Text, text);
    contentPalette.setColor(QPalette::WindowText, text);
    ui->scrollArea->setPalette(contentPalette);
    ui->leftColumn->setPalette(contentPalette);
    setStyleSheet(QStringLiteral(
        "QWidget#VMManagerDetails { background: transparent; color: palette(text); }"
        "QFrame#blumachMachineHeader { background: %3; border: 1px solid %1; border-radius: 9px; }"
        "QLabel#systemLabel { color: palette(text); font-weight: 600; background: transparent; }"
        "QLabel#blumachMachineMeta { color: %2; background: transparent; }"
        "QScrollArea#scrollArea { background: transparent; border: 0; }"
        "QWidget#leftColumn { background: transparent; }"
        "QWidget#blumachMachineDetailSection { background: %3; border: 1px solid %1; border-radius: 8px; }"
        "QWidget#blumachMachineDetailHeader { background: transparent; border: 0; border-bottom: 1px solid %1; }"
        "QFrame#detailFrame { background: transparent; border: 0; }"
        "QLabel#blumachSupplementaryHeading { color: palette(text); background: transparent; font-weight: 600; padding: 2px 0; }"
        "QLabel#screenshot { color: %2; background: palette(base); border: 1px solid %1; border-radius: 8px; }"
        "QPlainTextEdit#notesTextEdit { color: palette(text); background: palette(base); border: 1px solid %1; border-radius: 8px; padding: 8px; }"
        "QLabel#statusLabel { color: palette(text); background: %4; border-radius: 9px; padding: 4px 9px; }"
        "QLabel#statusLabel[machineState=\"running\"] { background: %5; }"
        "QLabel#statusLabel[machineState=\"waiting\"] { background: %6; }"
        "QLabel#blumachMachineDetailKey { color: %2; font-weight: 600; }"
        "QLabel#blumachMachineDetailValue { color: palette(text); }")
        .arg(border.name())
        .arg(muted.name())
        .arg(card.name())
        .arg(stoppedStatus.name())
        .arg(runningStatus.name())
        .arg(waitingStatus.name()));
}

void
VMManagerDetails::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void
VMManagerDetails::updateResponsiveLayout()
{
    const bool compact = width() < 760;
    ui->verticalLayout->setContentsMargins(compact ? 8 : 20,
                                           compact ? 8 : 14,
                                           compact ? 8 : 18,
                                           compact ? 10 : 16);
    ui->verticalLayout->setSpacing(compact ? 8 : 12);
    ui->horizontalLayout->setSpacing(compact ? 8 : 16);
    auto titleFont = ui->systemLabel->font();
    titleFont.setPointSize(compact ? 14 : 18);
    ui->systemLabel->setFont(titleFont);
    if (const auto headerLayout = qobject_cast<QHBoxLayout *>(machineHeader->layout())) {
        headerLayout->setContentsMargins(compact ? 10 : 14,
                                         compact ? 8 : 10,
                                         compact ? 10 : 12,
                                         compact ? 8 : 10);
        headerLayout->setSpacing(compact ? 8 : 12);
    }
    if (compact != compactSupplementaryLayout) {
        if (compact) {
            const auto leftColumnLayout = qobject_cast<QVBoxLayout *>(ui->leftColumn->layout());
            leftColumnLayout->insertWidget(qMax(0, leftColumnLayout->count() - 1), ui->rightColumn);
            ui->rightColumn->setMinimumWidth(0);
            ui->rightColumn->setMaximumWidth(QWIDGETSIZE_MAX);
            ui->notesTextEdit->setMaximumHeight(180);
        } else {
            ui->horizontalLayout->addWidget(ui->rightColumn, 0, Qt::AlignTop);
            ui->rightColumn->setMinimumWidth(250);
            ui->rightColumn->setMaximumWidth(320);
            ui->notesTextEdit->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        compactSupplementaryLayout = compact;
    }

    ui->rightColumn->setVisible(!ui->LeftRight->isHidden());
}

QWidget *
VMManagerDetails::createHorizontalLine(const int leftSpacing, const int rightSpacing)
{
    const auto container = new QWidget;
    const auto hLayout   = new QHBoxLayout(container);

    hLayout->addSpacing(leftSpacing);

    const auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    hLayout->addWidget(line);
    hLayout->addSpacing(rightSpacing);
    hLayout->setContentsMargins(0, 5, 0, 5);

    return container;
}

void
VMManagerDetails::saveNotes() const
{
    sysconfig->setNotes(ui->notesTextEdit->toPlainText());
}

void
VMManagerDetails::nextScreenshot()
{
    screenshotIndex = (screenshotIndex + 1) % screenshots.size();
    const QPixmap pic(screenshots.at(screenshotIndex).filePath());
    ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void
VMManagerDetails::previousScreenshot()
{
    screenshotIndex = screenshotIndex == 0 ? screenshots.size() - 1 : screenshotIndex - 1;
    const QPixmap pic(screenshots.at(screenshotIndex).filePath());
    ui->screenshot->setPixmap(pic.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool
VMManagerDetails::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->isWidgetType() && event->type() == QEvent::FocusOut) {
        // Make sure it's the textedit
        if (const auto *textEdit = qobject_cast<QPlainTextEdit *>(watched); textEdit) {
            saveNotes();
        }
    }
    return QWidget::eventFilter(watched, event);
}
