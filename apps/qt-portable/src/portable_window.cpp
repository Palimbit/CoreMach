/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_window.h"
#include "machine_dialog.h"

#include <blumach/platforms/null_host.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QToolBar>

namespace {
constexpr auto settingsOrganization = "BluMach";
constexpr auto settingsApplication = "BluMach Portable";
}

PortableWindow::AssetStorage::~AssetStorage()
{
    bm_frontend_readonly_media_close(&media);
    bm_frontend_blob_release(&blob);
}

PortableWindow::PortableWindow(QWidget *parent)
    : QMainWindow(parent), display_(new DisplayWidget), status_(new QLabel),
      storageStatus_(new QLabel),
      machineToolbar_(addToolBar(tr("Machine"))),
      pauseAction_(new QAction(tr("Pause"), this)),
      resetAction_(new QAction(tr("Reset"), this)),
      stopAction_(new QAction(tr("Stop"), this)),
      fullScreenAction_(new QAction(tr("Fullscreen"), this)),
      smoothScalingAction_(new QAction(tr("Smooth scaling"), this)),
      statusBarAction_(new QAction(tr("Status bar"), this)),
      copyFrameAction_(new QAction(tr("Copy frame"), this)),
      saveFrameAction_(new QAction(tr("Save frame as…"), this)),
      scaleGroup_(new QActionGroup(this)),
      host_(bm_null_host_services())
{
    auto *openAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open…"), this);
    auto *quitAction = new QAction(tr("Quit"), this);
    pauseAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    resetAction_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    stopAction_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    openAction->setShortcut(QKeySequence::Open);
    quitAction->setShortcut(QKeySequence::Quit);
    fullScreenAction_->setShortcut(Qt::Key_F11);
    fullScreenAction_->setCheckable(true);
    smoothScalingAction_->setCheckable(true);
    statusBarAction_->setCheckable(true);
    copyFrameAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    saveFrameAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));

    machineToolbar_->setObjectName(QStringLiteral("machine-toolbar"));
    machineToolbar_->addAction(openAction);
    machineToolbar_->addSeparator();
    machineToolbar_->addAction(pauseAction_);
    machineToolbar_->addAction(resetAction_);
    machineToolbar_->addAction(stopAction_);

    auto *machineMenu = menuBar()->addMenu(tr("Machine"));
    machineMenu->addAction(openAction);
    machineMenu->addSeparator();
    machineMenu->addAction(pauseAction_);
    machineMenu->addAction(resetAction_);
    machineMenu->addAction(stopAction_);
    machineMenu->addSeparator();
    machineMenu->addAction(quitAction);

    auto *viewMenu = menuBar()->addMenu(tr("View"));
    auto *scaleMenu = viewMenu->addMenu(tr("Scaling"));
    scaleGroup_->setExclusive(true);
    const auto addScaleAction = [this, scaleMenu](const QString &label,
                                                  DisplayWidget::ScaleMode mode) {
        auto *action = scaleMenu->addAction(label);
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        scaleGroup_->addAction(action);
        return action;
    };
    addScaleAction(tr("Fit, preserve source aspect"),
                   DisplayWidget::ScaleMode::Fit)->setChecked(true);
    addScaleAction(tr("Integer pixels"), DisplayWidget::ScaleMode::Integer);
    addScaleAction(tr("Correct to 4:3"),
                   DisplayWidget::ScaleMode::CorrectedFourThree);
    addScaleAction(tr("Stretch to window"), DisplayWidget::ScaleMode::Stretch);
    viewMenu->addAction(smoothScalingAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(machineToolbar_->toggleViewAction());
    viewMenu->addAction(statusBarAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fullScreenAction_);

    auto *captureMenu = menuBar()->addMenu(tr("Capture"));
    captureMenu->addAction(copyFrameAction_);
    captureMenu->addAction(saveFrameAction_);

    setWindowTitle(tr("BluMach Portable"));
    setCentralWidget(display_);
    statusBar()->addPermanentWidget(status_, 1);
    storageStatus_->setTextFormat(Qt::RichText);
    statusBar()->addPermanentWidget(storageStatus_);
    statusBarAction_->setChecked(true);
    connect(openAction, &QAction::triggered, this,
            [this] { chooseMachine(); });
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    connect(pauseAction_, &QAction::triggered, this,
            [this] { togglePause(); });
    connect(resetAction_, &QAction::triggered, this,
            [this] { resetMachine(); });
    connect(stopAction_, &QAction::triggered, this,
            [this] { stopMachine(); });
    connect(fullScreenAction_, &QAction::toggled, this,
            [this](bool enabled) { toggleFullscreen(enabled); });
    connect(smoothScalingAction_, &QAction::toggled, display_,
            &DisplayWidget::setSmoothScaling);
    connect(statusBarAction_, &QAction::toggled, statusBar(),
            &QStatusBar::setVisible);
    connect(copyFrameAction_, &QAction::triggered, this,
            [this] { copyFrame(); });
    connect(saveFrameAction_, &QAction::triggered, this,
            [this] { saveFrame(); });
    connect(scaleGroup_, &QActionGroup::triggered, this,
            [this](QAction *action) {
                display_->setScaleMode(static_cast<DisplayWidget::ScaleMode>(
                    action->data().toInt()));
            });
    display_->setKeyHandler(
        [this](QKeyEvent *event, bool pressed) { sendKey(event, pressed); });
    (void) catalog_.load(&catalogError_);
    resize(960, 600);
    readSettings();
    updateActions();
    showStatus(catalogError_.isEmpty() ? tr("Open a machine to begin") :
               tr("Catalogue unavailable: %1").arg(catalogError_));
}

PortableWindow::~PortableWindow()
{
    closeMachine();
}

void
PortableWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();
    QMainWindow::closeEvent(event);
}

bool
PortableWindow::openInitial(const QString &machineId,
                            const QHash<QString, QString> &paths)
{
    const QByteArray id = machineId.toUtf8();
    const bm_frontend_adapter_t *adapter = bm_frontend_adapter_find(id.constData());
    return adapter != nullptr && openMachine(adapter, paths);
}

bool
PortableWindow::openInitialProduct(const QString &productId,
                                   const QHash<QString, QString> &paths)
{
    const PortableCatalogMachine *product = catalog_.product(productId);
    return product != nullptr && openInitial(product->adapterId, paths);
}

void
PortableWindow::chooseMachine()
{
    if (!catalogError_.isEmpty()) {
        QMessageBox::critical(this, tr("Catalogue unavailable"), catalogError_);
        return;
    }
    MachineDialog dialog(catalog_, this);
    if (dialog.exec() == QDialog::Accepted)
        (void) openMachine(dialog.adapter(), dialog.paths());
}

bool
PortableWindow::openMachine(const bm_frontend_adapter_t *adapter,
                            const QHash<QString, QString> &paths)
{
    size_t requirementCount = 0U;
    const bm_frontend_asset_requirement_t *requirements =
        bm_frontend_adapter_assets(adapter, &requirementCount);
    closeMachine();
    for (size_t index = 0U; index < requirementCount; ++index) {
        const QString role = QString::fromUtf8(requirements[index].role);
        const QString path = paths.value(role);
        if (path.isEmpty())
            continue;
        auto storage = std::make_unique<AssetStorage>();
        const QByteArray nativePath = path.toLocal8Bit();
        bm_frontend_asset_binding_t binding {};
        binding.role = requirements[index].role;
        binding.kind = requirements[index].kind;
        if (requirements[index].kind == BM_FRONTEND_ASSET_BLOB) {
            if ((requirements[index].accepted_size_count != 1U) ||
                !bm_frontend_blob_read_exact(
                    nativePath.constData(),
                    static_cast<size_t>(requirements[index].accepted_sizes[0]),
                    &storage->blob)) {
                QMessageBox::critical(this, tr("Invalid asset"),
                                      tr("Could not load %1 with its required size.")
                                          .arg(QString::fromUtf8(
                                              requirements[index].label)));
                closeMachine();
                return false;
            }
            binding.value.blob = { requirements[index].role,
                                   storage->blob.data, storage->blob.size,
                                   nullptr };
        } else {
            if (!bm_frontend_readonly_media_open(
                    nativePath.constData(), requirements[index].block_size,
                    &storage->media)) {
                QMessageBox::critical(this, tr("Invalid asset"),
                                      tr("Could not open %1 as read-only media.")
                                          .arg(QString::fromUtf8(
                                              requirements[index].label)));
                closeMachine();
                return false;
            }
            binding.value.media = storage->media.media;
        }
        assets_.push_back(std::move(storage));
        bindings_.push_back(binding);
    }
    bm_status_t result = bm_frontend_machine_open(
        adapter, bindings_.data(), bindings_.size(), &machine_);
    if (result == BM_STATUS_OK) {
        const uint64_t generation = workerGeneration_;
        worker_ = std::make_unique<SessionWorker>(
            host_, bm_frontend_machine_config(machine_), UINT64_C(10000000),
            [this, generation](SessionWorker::Snapshot snapshot) {
                queueSnapshot(generation, std::move(snapshot));
            });
        result = worker_->start();
    }
    if (result != BM_STATUS_OK) {
        QMessageBox::critical(this, tr("Could not start machine"),
                              tr("The portable session rejected the configuration "
                                 "(status %1).").arg(static_cast<int>(result)));
        closeMachine();
        return false;
    }
    const bm_machine_definition_t *definition =
        bm_frontend_adapter_definition(adapter);
    activeMachineId_ = definition != nullptr ?
        QString::fromUtf8(definition->id) : QString();
    setWindowTitle(activeMachineId_.isEmpty() ? tr("BluMach Portable") :
        tr("%1 — BluMach Portable").arg(activeMachineId_));
    lastError_ = BM_STATUS_OK;
    lifecyclePending_ = false;
    hasVideoGeometry_ = false;
    presentedFrames_ = 0U;
    presentationFps_ = 0.0;
    frameRateTimer_.start();
    activityTimer_.start();
    storagePresentation_.clear();
    display_->setFocus();
    updateActions();
    showStatus();
    return true;
}

void
PortableWindow::closeMachine()
{
    ++workerGeneration_;
    lifecyclePending_ = false;
    worker_.reset();
    snapshotMailbox_.clear();
    bm_frontend_machine_close(machine_);
    machine_ = nullptr;
    bindings_.clear();
    assets_.clear();
    activeMachineId_.clear();
    hasVideoGeometry_ = false;
    presentedFrames_ = 0U;
    presentationFps_ = 0.0;
    frameRateTimer_.invalidate();
    activityTimer_.invalidate();
    storagePresentation_.clear();
    storageStatus_->clear();
    setWindowTitle(tr("BluMach Portable"));
    display_->setFrame(QImage());
    updateActions();
}

void
PortableWindow::togglePause()
{
    if ((worker_ == nullptr) || lifecyclePending_)
        return;
    if (worker_->state() == BM_SESSION_RUNNING) {
        lifecyclePending_ = true;
        worker_->pause();
    } else if (worker_->state() == BM_SESSION_PAUSED) {
        lifecyclePending_ = true;
        worker_->resume();
    }
    updateActions();
}

void
PortableWindow::resetMachine()
{
    if ((worker_ != nullptr) && !lifecyclePending_) {
        lifecyclePending_ = true;
        worker_->reset();
        updateActions();
    }
}

void
PortableWindow::stopMachine()
{
    if ((worker_ != nullptr) && !lifecyclePending_) {
        lifecyclePending_ = true;
        worker_->stop();
        updateActions();
    }
}

void
PortableWindow::toggleFullscreen(bool enabled)
{
    if (enabled) {
        wasMaximizedBeforeFullscreen_ = isMaximized();
        showFullScreen();
    } else if (wasMaximizedBeforeFullscreen_) {
        showMaximized();
    } else {
        showNormal();
    }
    display_->setFocus();
}

void
PortableWindow::copyFrame()
{
    const QImage frame = display_->frame();
    if (frame.isNull())
        return;
    QApplication::clipboard()->setImage(frame);
    statusBar()->showMessage(tr("Frame copied to the clipboard"), 3000);
}

void
PortableWindow::saveFrame()
{
    const QImage frame = display_->frame();
    if (frame.isNull())
        return;
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString suggestedPath = directory.isEmpty() ?
        QStringLiteral("BluMach.png") :
        QDir(directory).filePath(QStringLiteral("BluMach.png"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save frame"), suggestedPath,
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    if (!frame.save(path, "PNG")) {
        QMessageBox::critical(this, tr("Could not save frame"),
                              tr("The PNG image could not be written."));
        return;
    }
    statusBar()->showMessage(tr("Frame saved"), 3000);
}

void
PortableWindow::readSettings()
{
    QSettings settings(settingsOrganization, settingsApplication);
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry"))
                                    .toByteArray();
    if (!geometry.isEmpty())
        (void) restoreGeometry(geometry);
    const int modeValue = settings.value(
        QStringLiteral("display/scale-mode"),
        static_cast<int>(DisplayWidget::ScaleMode::Fit)).toInt();
    if ((modeValue >= static_cast<int>(DisplayWidget::ScaleMode::Fit)) &&
        (modeValue <= static_cast<int>(DisplayWidget::ScaleMode::Stretch))) {
        const auto mode = static_cast<DisplayWidget::ScaleMode>(modeValue);
        display_->setScaleMode(mode);
        for (QAction *action : scaleGroup_->actions()) {
            if (action->data().toInt() == modeValue)
                action->setChecked(true);
        }
    }
    smoothScalingAction_->setChecked(settings.value(
        QStringLiteral("display/smooth-scaling"), false).toBool());
    machineToolbar_->setVisible(settings.value(
        QStringLiteral("window/toolbar-visible"), true).toBool());
    statusBarAction_->setChecked(settings.value(
        QStringLiteral("window/status-visible"), true).toBool());
}

void
PortableWindow::writeSettings() const
{
    QSettings settings(settingsOrganization, settingsApplication);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/toolbar-visible"),
                      machineToolbar_->isVisible());
    settings.setValue(QStringLiteral("window/status-visible"),
                      statusBar()->isVisible());
    settings.setValue(QStringLiteral("display/scale-mode"),
                      static_cast<int>(display_->scaleMode()));
    settings.setValue(QStringLiteral("display/smooth-scaling"),
                      display_->smoothScaling());
}

void
PortableWindow::sendKey(QKeyEvent *event, bool pressed)
{
    if ((worker_ == nullptr) || (worker_->state() != BM_SESSION_RUNNING)) {
        event->ignore();
        return;
    }
    const bm_key_code_t key = mapKey(event->key());
    if (key == static_cast<bm_key_code_t>(0)) {
        event->ignore();
        return;
    }
    const bm_input_event_t input {
        BM_INPUT_KEY, key, pressed ? 1 : 0, event->isAutoRepeat() ? 1 : 0
    };
    worker_->sendInput(input);
    event->accept();
}

void
PortableWindow::queueSnapshot(uint64_t generation,
                              SessionWorker::Snapshot snapshot)
{
    if (!snapshotMailbox_.submit(std::move(snapshot)))
        return;
    QMetaObject::invokeMethod(
        this, [this, generation] { drainSnapshots(generation); },
        Qt::QueuedConnection);
}

void
PortableWindow::drainSnapshots(uint64_t generation)
{
    std::deque<SessionWorker::Snapshot> snapshots = snapshotMailbox_.take();
    if (generation != workerGeneration_)
        return;
    for (SessionWorker::Snapshot &snapshot : snapshots)
        handleSnapshot(std::move(snapshot));
}

void
PortableWindow::handleSnapshot(SessionWorker::Snapshot snapshot)
{
    if (!snapshot.frame.isNull()) {
        display_->setFrame(snapshot.frame);
        if (snapshot.hasVideo) {
            videoGeometry_ = snapshot.geometry;
            hasVideoGeometry_ = true;
        }
        ++presentedFrames_;
        if (frameRateTimer_.isValid() && (frameRateTimer_.elapsed() >= 500)) {
            presentationFps_ = static_cast<double>(presentedFrames_) * 1000.0 /
                               static_cast<double>(frameRateTimer_.elapsed());
            presentedFrames_ = 0U;
            frameRateTimer_.restart();
        }
        updateStorageStatus(snapshot.storage);
    }
    if (snapshot.lifecycleResult)
        lifecyclePending_ = false;
    if ((snapshot.status != BM_STATUS_OK) &&
        (snapshot.status != lastError_)) {
        lastError_ = snapshot.status;
        QMessageBox::critical(this, tr("Portable session error"),
                              tr("The worker reported status %1.")
                                  .arg(static_cast<int>(snapshot.status)));
    }
    updateActions();
    showStatus();
}

void
PortableWindow::updateStorageStatus(
    const std::vector<bm_storage_device_status_t> &storage)
{
    constexpr qint64 pulseMilliseconds = 180;
    const qint64 now = activityTimer_.isValid() ? activityTimer_.elapsed() : 0;
    if (storagePresentation_.size() < storage.size())
        storagePresentation_.resize(storage.size());
    QStringList labels;
    for (size_t index = 0U; index < storage.size(); ++index) {
        const bm_storage_device_status_t &device = storage[index];
        StoragePresentation &presentation = storagePresentation_[index];
        if (!device.installed)
            continue;
        if (presentation.initialized) {
            if (device.read_operations > presentation.reads)
                presentation.readPulseUntil = now + pulseMilliseconds;
            if (device.write_operations > presentation.writes)
                presentation.writePulseUntil = now + pulseMilliseconds;
        }
        presentation.reads = device.read_operations;
        presentation.writes = device.write_operations;
        presentation.initialized = true;
        const QString unit = device.kind == BM_STORAGE_DEVICE_FLOPPY ?
            QString(QChar(static_cast<char16_t>(u'A' + device.unit))) :
            QString::number(device.unit);
        if (!device.media_present) {
            labels.push_back(tr("%1: empty").arg(unit));
            continue;
        }
        const QString readColor = now < presentation.readPulseUntil ?
            QStringLiteral("#2eae4e") : QStringLiteral("#777777");
        const QString writeColor = now < presentation.writePulseUntil ?
            QStringLiteral("#e6a700") : QStringLiteral("#777777");
        QString label = tr("%1: %2").arg(
            unit, device.write_protected ? tr("RO") : tr("RW"));
        if (device.motor_active)
            label += tr(" · motor");
        label += QStringLiteral(
            " · <span style=\"color:%1\">● R</span> %2"
            " · <span style=\"color:%3\">● W</span> %4")
            .arg(readColor)
            .arg(static_cast<qulonglong>(device.read_operations))
            .arg(writeColor)
            .arg(static_cast<qulonglong>(device.write_operations));
        labels.push_back(label);
    }
    storageStatus_->setText(labels.join(QStringLiteral(" &nbsp; ")));
    storageStatus_->setToolTip(
        tr("Removable media state and completed read/write operations"));
}

void
PortableWindow::updateActions()
{
    const bm_session_state_t state = worker_ != nullptr ? worker_->state() :
                                                          BM_SESSION_NEW;
    pauseAction_->setEnabled(!lifecyclePending_ &&
                             (state == BM_SESSION_RUNNING ||
                              state == BM_SESSION_PAUSED));
    pauseAction_->setText(state == BM_SESSION_PAUSED ? tr("Resume") :
                                                       tr("Pause"));
    resetAction_->setEnabled(!lifecyclePending_ &&
                             (state == BM_SESSION_RUNNING ||
                              state == BM_SESSION_PAUSED));
    stopAction_->setEnabled(!lifecyclePending_ &&
                            (state == BM_SESSION_RUNNING ||
                             state == BM_SESSION_PAUSED));
    copyFrameAction_->setEnabled(display_->hasFrame());
    saveFrameAction_->setEnabled(display_->hasFrame());
}

void
PortableWindow::showStatus(const QString &detail)
{
    if (!detail.isEmpty()) {
        status_->setText(detail);
        return;
    }
    if (worker_ == nullptr) {
        status_->setText(tr("No machine"));
        return;
    }
    const char *state = "unknown";
    switch (worker_->state()) {
        case BM_SESSION_RUNNING: state = "running"; break;
        case BM_SESSION_PAUSED: state = "paused"; break;
        case BM_SESSION_STOPPED: state = "stopped"; break;
        case BM_SESSION_CONFIGURED: state = "configured"; break;
        case BM_SESSION_NEW: state = "new"; break;
    }
    QString text = tr("%1").arg(QString::fromLatin1(state));
    if (hasVideoGeometry_) {
        QString refresh = tr("unknown");
        if ((videoGeometry_.refresh_numerator != 0U) &&
            (videoGeometry_.refresh_denominator != 0U)) {
            const double hz = static_cast<double>(
                videoGeometry_.refresh_numerator) /
                static_cast<double>(videoGeometry_.refresh_denominator);
            refresh = QLocale().toString(hz, 'f', 1);
        }
        const QSize output = display_->outputPixelSize();
        text += tr(" — %1×%2 @ %3 Hz")
                    .arg(videoGeometry_.width)
                    .arg(videoGeometry_.height)
                    .arg(refresh);
        if (!output.isEmpty())
            text += tr(" → %1×%2").arg(output.width()).arg(output.height());
        if (presentationFps_ > 0.0)
            text += tr(" — %1 FPS").arg(
                QLocale().toString(presentationFps_, 'f', 1));
    }
    status_->setText(text);
    status_->setToolTip(tr("Emulated time: %1 ticks")
                            .arg(static_cast<qulonglong>(worker_->ticks())));
}

bm_key_code_t
PortableWindow::mapKey(int key)
{
    if ((key >= Qt::Key_A) && (key <= Qt::Key_Z))
        return static_cast<bm_key_code_t>(BM_KEY_A + key - Qt::Key_A);
    if ((key >= Qt::Key_1) && (key <= Qt::Key_9))
        return static_cast<bm_key_code_t>(BM_KEY_1 + key - Qt::Key_1);
    if (key == Qt::Key_0)
        return BM_KEY_0;
    switch (key) {
        case Qt::Key_Return: case Qt::Key_Enter: return BM_KEY_ENTER;
        case Qt::Key_Escape: return BM_KEY_ESCAPE;
        case Qt::Key_Backspace: return BM_KEY_BACKSPACE;
        case Qt::Key_Tab: return BM_KEY_TAB;
        case Qt::Key_Space: return BM_KEY_SPACE;
        case Qt::Key_Minus: return BM_KEY_MINUS;
        case Qt::Key_Equal: return BM_KEY_EQUAL;
        case Qt::Key_BracketLeft: return BM_KEY_LEFT_BRACKET;
        case Qt::Key_BracketRight: return BM_KEY_RIGHT_BRACKET;
        case Qt::Key_Backslash: return BM_KEY_BACKSLASH;
        case Qt::Key_Semicolon: return BM_KEY_SEMICOLON;
        case Qt::Key_Apostrophe: return BM_KEY_APOSTROPHE;
        case Qt::Key_QuoteLeft: return BM_KEY_GRAVE;
        case Qt::Key_Comma: return BM_KEY_COMMA;
        case Qt::Key_Period: return BM_KEY_PERIOD;
        case Qt::Key_Slash: return BM_KEY_SLASH;
        case Qt::Key_CapsLock: return BM_KEY_CAPS_LOCK;
        case Qt::Key_F1: return BM_KEY_F1;
        case Qt::Key_F2: return BM_KEY_F2;
        case Qt::Key_F3: return BM_KEY_F3;
        case Qt::Key_F4: return BM_KEY_F4;
        case Qt::Key_F5: return BM_KEY_F5;
        case Qt::Key_F6: return BM_KEY_F6;
        case Qt::Key_F7: return BM_KEY_F7;
        case Qt::Key_F8: return BM_KEY_F8;
        case Qt::Key_F9: return BM_KEY_F9;
        case Qt::Key_F10: return BM_KEY_F10;
        case Qt::Key_Insert: return BM_KEY_INSERT;
        case Qt::Key_Home: return BM_KEY_HOME;
        case Qt::Key_PageUp: return BM_KEY_PAGE_UP;
        case Qt::Key_Delete: return BM_KEY_DELETE;
        case Qt::Key_End: return BM_KEY_END;
        case Qt::Key_PageDown: return BM_KEY_PAGE_DOWN;
        case Qt::Key_Right: return BM_KEY_RIGHT;
        case Qt::Key_Left: return BM_KEY_LEFT;
        case Qt::Key_Down: return BM_KEY_DOWN;
        case Qt::Key_Up: return BM_KEY_UP;
        case Qt::Key_Control: return BM_KEY_LEFT_CONTROL;
        case Qt::Key_Shift: return BM_KEY_LEFT_SHIFT;
        case Qt::Key_Alt: return BM_KEY_LEFT_ALT;
        default: return static_cast<bm_key_code_t>(0);
    }
}
