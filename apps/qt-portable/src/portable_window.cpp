/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_window.h"
#include "machine_dialog.h"
#include "qt_key_map.h"
#include "latency_trace.h"

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
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QToolBar>

#include <algorithm>

namespace {
constexpr auto settingsOrganization = "BluMach";
constexpr auto settingsApplication = "BluMach Portable";

QString
persistentStateKey(const QString &machineId, const QString &role,
                   const QString &field)
{
    return QStringLiteral("machines/%1/persistent-state/%2/%3")
        .arg(machineId, role, field);
}
}

PortableWindow::AssetStorage::~AssetStorage()
{
    bm_frontend_readonly_media_close(&media);
    bm_frontend_blob_release(&blob);
}

PortableWindow::PortableWindow(QWidget *parent)
    : QMainWindow(parent), display_(new DisplayWidget), status_(new QLabel),
      storageStatus_(new QLabel), keyboardStatus_(new QLabel),
      machineToolbar_(addToolBar(tr("Machine"))),
      pauseAction_(new QAction(tr("Pause"), this)),
      resetAction_(new QAction(tr("Reset"), this)),
      stopAction_(new QAction(tr("Stop"), this)),
      retainStateAction_(new QAction(tr("Retain battery-backed state"), this)),
      insertFloppyAction_(new QAction(tr("Insert disk in A…"), this)),
      ejectFloppyAction_(new QAction(tr("Eject disk from A"), this)),
      fullScreenAction_(new QAction(tr("Fullscreen"), this)),
      smoothScalingAction_(new QAction(tr("Smooth scaling"), this)),
      statusBarAction_(new QAction(tr("Status bar"), this)),
      copyFrameAction_(new QAction(tr("Copy frame"), this)),
      saveFrameAction_(new QAction(tr("Save frame as…"), this)),
      scaleGroup_(new QActionGroup(this)),
      rendererGroup_(new QActionGroup(this)),
      effectGroup_(new QActionGroup(this)),
      host_(bm_null_host_services())
{
    auto *openAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open…"), this);
    auto *quitAction = new QAction(tr("Quit"), this);
    pauseAction_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    resetAction_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    stopAction_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    retainStateAction_->setCheckable(true);
    retainStateAction_->setVisible(false);
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
    machineToolbar_->addSeparator();
    machineToolbar_->addAction(insertFloppyAction_);
    machineToolbar_->addAction(ejectFloppyAction_);

    auto *machineMenu = menuBar()->addMenu(tr("Machine"));
    machineMenu->addAction(openAction);
    machineMenu->addSeparator();
    machineMenu->addAction(pauseAction_);
    machineMenu->addAction(resetAction_);
    machineMenu->addAction(stopAction_);
    machineMenu->addSeparator();
    machineMenu->addAction(retainStateAction_);
    machineMenu->addSeparator();
    machineMenu->addAction(quitAction);

    auto *mediaMenu = menuBar()->addMenu(tr("Media"));
    mediaMenu->addAction(insertFloppyAction_);
    mediaMenu->addAction(ejectFloppyAction_);

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
    auto *rendererMenu = viewMenu->addMenu(tr("Renderer"));
    rendererGroup_->setExclusive(true);
    const auto addRendererAction =
        [this, rendererMenu](const QString &label,
                             DisplayWidget::Renderer renderer) {
            auto *action = rendererMenu->addAction(label);
            action->setCheckable(true);
            action->setData(static_cast<int>(renderer));
            rendererGroup_->addAction(action);
            return action;
        };
    addRendererAction(tr("OpenGL"),
                      DisplayWidget::Renderer::OpenGL);
    addRendererAction(tr("Software"), DisplayWidget::Renderer::Software)->setChecked(true);
    auto *effectMenu = viewMenu->addMenu(tr("Effects"));
    effectGroup_->setExclusive(true);
    const auto addEffectAction =
        [this, effectMenu](const QString &label, DisplayWidget::Effect effect) {
            auto *action = effectMenu->addAction(label);
            action->setCheckable(true);
            action->setData(static_cast<int>(effect));
            effectGroup_->addAction(action);
            return action;
        };
    addEffectAction(tr("None"), DisplayWidget::Effect::None)->setChecked(true);
    addEffectAction(tr("CRT scanlines and vignette"),
                    DisplayWidget::Effect::Crt);
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
    keyboardStatus_->setTextFormat(Qt::RichText);
    keyboardStatus_->setVisible(false);
    statusBar()->addPermanentWidget(keyboardStatus_);
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
    connect(retainStateAction_, &QAction::toggled, this,
            [this](bool enabled) { setPersistentStateRetention(enabled); });
    connect(insertFloppyAction_, &QAction::triggered, this,
            [this] { insertFloppy(); });
    connect(ejectFloppyAction_, &QAction::triggered, this,
            [this] { ejectFloppy(); });
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
    connect(rendererGroup_, &QActionGroup::triggered, this,
            [this](QAction *action) {
                display_->setRenderer(static_cast<DisplayWidget::Renderer>(
                    action->data().toInt()));
                showStatus();
            });
    connect(effectGroup_, &QActionGroup::triggered, this,
            [this](QAction *action) {
                display_->setEffect(static_cast<DisplayWidget::Effect>(
                    action->data().toInt()));
                showStatus();
            });
    display_->setPointerHandler(
        [this](int32_t deltaX, int32_t deltaY, Qt::MouseButtons qtButtons) {
            uint8_t buttons = 0U;
            if (qtButtons.testFlag(Qt::LeftButton))
                buttons |= BM_POINTER_BUTTON_LEFT;
            if (qtButtons.testFlag(Qt::RightButton))
                buttons |= BM_POINTER_BUTTON_RIGHT;
            if (qtButtons.testFlag(Qt::MiddleButton))
                buttons |= BM_POINTER_BUTTON_MIDDLE;
            sendPointer(deltaX, deltaY, buttons);
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
    size_t persistentStateCount = 0U;
    const bm_frontend_asset_requirement_t *requirements =
        bm_frontend_adapter_assets(adapter, &requirementCount);
    const bm_frontend_persistent_state_requirement_t *stateRequirements =
        bm_frontend_adapter_persistent_states(adapter, &persistentStateCount);
    const bm_machine_definition_t *definition =
        bm_frontend_adapter_definition(adapter);
    const QString machineId = definition != nullptr ?
        QString::fromUtf8(definition->id) : QString();
    std::vector<SessionWorker::PersistentState> captureStates;
    closeMachine();
    activeMachineId_ = machineId;
    for (size_t index = 0U; index < requirementCount; ++index) {
        if (requirements[index].replaceable &&
            (requirements[index].storage_kind == BM_STORAGE_DEVICE_FLOPPY) &&
            (requirements[index].storage_unit == 0U)) {
            replaceableFloppy_ = &requirements[index];
            break;
        }
    }
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
            const bool writable = requirements[index].kind ==
                                  BM_FRONTEND_ASSET_BLOCK_MEDIA;
            const int opened = writable ?
                bm_frontend_working_media_open(
                    nativePath.constData(), requirements[index].block_size,
                    &storage->media) :
                bm_frontend_readonly_media_open(
                    nativePath.constData(), requirements[index].block_size,
                    &storage->media);
            if (!opened) {
                QMessageBox::critical(this, tr("Invalid asset"),
                                      (writable ?
                                       tr("Could not open %1 as a writable working image.") :
                                       tr("Could not open %1 as read-only media."))
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
    {
        QSettings settings(settingsOrganization, settingsApplication);
        persistentStates_.reserve(persistentStateCount);
        persistentBindings_.reserve(persistentStateCount);
        captureStates.reserve(persistentStateCount);
        for (size_t index = 0U; index < persistentStateCount; ++index) {
            const auto &requirement = stateRequirements[index];
            auto storage = std::make_unique<PersistentStateStorage>();
            const QString role = QString::fromUtf8(requirement.role);
            storage->requirement = &requirement;
            storage->retain = !requirement.battery_backed || settings.value(
                persistentStateKey(machineId, role, QStringLiteral("retain")),
                true).toBool();
            const QByteArray saved = settings.value(
                persistentStateKey(machineId, role, QStringLiteral("data")))
                .toByteArray();
            if (!storage->retain) {
                storage->data = QByteArray(
                    static_cast<qsizetype>(requirement.size), '\0');
            } else if (saved.size() == static_cast<qsizetype>(requirement.size)) {
                storage->data = saved;
            } else if (requirement.default_data != nullptr) {
                storage->data = QByteArray(
                    reinterpret_cast<const char *>(requirement.default_data),
                    static_cast<qsizetype>(requirement.size));
            } else {
                storage->data = QByteArray(
                    static_cast<qsizetype>(requirement.size), '\0');
            }
            persistentBindings_.push_back({
                requirement.role,
                reinterpret_cast<const uint8_t *>(storage->data.constData()),
                requirement.size
            });
            SessionWorker::PersistentState capture;
            capture.role = requirement.role;
            captureStates.push_back(std::move(capture));
            persistentStates_.push_back(std::move(storage));
        }
    }
    {
        bool hasBatteryBackedState = false;
        bool retainBatteryBackedState = true;
        for (const auto &state : persistentStates_) {
            if (!state->requirement->battery_backed)
                continue;
            hasBatteryBackedState = true;
            retainBatteryBackedState = retainBatteryBackedState && state->retain;
        }
        const QSignalBlocker blocker(retainStateAction_);
        retainStateAction_->setVisible(hasBatteryBackedState);
        retainStateAction_->setChecked(retainBatteryBackedState);
    }
    bm_status_t result = bm_frontend_machine_open_with_persistent_state(
        adapter, bindings_.data(), bindings_.size(), persistentBindings_.data(),
        persistentBindings_.size(), &machine_);
    if (result == BM_STATUS_OK) {
        const uint64_t generation = workerGeneration_;
        worker_ = std::make_unique<SessionWorker>(
            host_, bm_frontend_machine_config(machine_),
            [this, generation](SessionWorker::Snapshot snapshot) {
                queueSnapshot(generation, std::move(snapshot));
            }, std::move(captureStates));
        result = worker_->start();
    }
    if (result != BM_STATUS_OK) {
        QMessageBox::critical(this, tr("Could not start machine"),
                              tr("The portable session rejected the configuration "
                                 "(status %1).").arg(static_cast<int>(result)));
        closeMachine();
        return false;
    }
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
    if (replaceableFloppy_ != nullptr) {
        const QString role = QString::fromUtf8(replaceableFloppy_->role);
        replaceableFloppyPresent_ = paths.contains(role) &&
                                    !paths.value(role).isEmpty();
    }
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
    if (worker_ != nullptr) {
        worker_->shutdown();
        QSettings settings(settingsOrganization, settingsApplication);
        for (const SessionWorker::PersistentState &captured :
             worker_->persistentStates()) {
            const auto stored = std::find_if(
                persistentStates_.begin(), persistentStates_.end(),
                [&captured](const auto &entry) {
                    return entry->requirement->role == captured.role;
                });
            if ((stored == persistentStates_.end()) || !(*stored)->retain ||
                (captured.status != BM_STATUS_OK) ||
                (captured.data.size() != (*stored)->requirement->size))
                continue;
            const QString role = QString::fromUtf8((*stored)->requirement->role);
            settings.setValue(
                persistentStateKey(activeMachineId_, role,
                                   QStringLiteral("data")),
                QByteArray(reinterpret_cast<const char *>(captured.data.data()),
                           static_cast<qsizetype>(captured.data.size())));
        }
    }
    worker_.reset();
    snapshotMailbox_.clear();
    bm_frontend_machine_close(machine_);
    machine_ = nullptr;
    bindings_.clear();
    assets_.clear();
    persistentBindings_.clear();
    persistentStates_.clear();
    replaceableFloppy_ = nullptr;
    replaceableFloppyPresent_ = false;
    activeMachineId_.clear();
    hasVideoGeometry_ = false;
    presentedFrames_ = 0U;
    presentationFps_ = 0.0;
    frameRateTimer_.invalidate();
    activityTimer_.invalidate();
    storagePresentation_.clear();
    keyboardStatus_->clear();
    keyboardStatus_->setVisible(false);
    storageStatus_->clear();
    {
        const QSignalBlocker blocker(retainStateAction_);
        retainStateAction_->setVisible(false);
        retainStateAction_->setChecked(false);
    }
    setWindowTitle(tr("BluMach Portable"));
    display_->setFrame(QImage());
    updateActions();
}

void
PortableWindow::setPersistentStateRetention(bool enabled)
{
    if (activeMachineId_.isEmpty())
        return;
    QSettings settings(settingsOrganization, settingsApplication);
    for (auto &state : persistentStates_) {
        if (!state->requirement->battery_backed)
            continue;
        state->retain = enabled;
        const QString role = QString::fromUtf8(state->requirement->role);
        settings.setValue(
            persistentStateKey(activeMachineId_, role,
                               QStringLiteral("retain")), enabled);
        if (!enabled)
            settings.remove(persistentStateKey(
                activeMachineId_, role, QStringLiteral("data")));
    }
    statusBar()->showMessage(
        enabled ? tr("Battery-backed state will be retained after power-off") :
                  tr("Battery depleted: state will be lost after power-off"),
        4000);
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
PortableWindow::insertFloppy()
{
    if ((worker_ == nullptr) || (replaceableFloppy_ == nullptr) ||
        lifecyclePending_)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert disk in A"), QString(),
        tr("Disk images (*.img *.ima);;All files (*)"));
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    bool accepted = false;
    for (size_t index = 0U; index < replaceableFloppy_->accepted_size_count;
         ++index) {
        if (static_cast<uint64_t>(info.size()) ==
            replaceableFloppy_->accepted_sizes[index]) {
            accepted = true;
            break;
        }
    }
    if (!accepted) {
        QMessageBox::critical(
            this, tr("Invalid disk image"),
            tr("The selected image does not have a supported size."));
        return;
    }
    auto storage = std::make_shared<AssetStorage>();
    const QByteArray nativePath = path.toLocal8Bit();
    if (!bm_frontend_readonly_media_open(nativePath.constData(),
                                         replaceableFloppy_->block_size,
                                         &storage->media)) {
        QMessageBox::critical(
            this, tr("Invalid disk image"),
            tr("The selected image could not be opened read-only."));
        return;
    }
    const bm_storage_media_change_t change {
        1, 1, storage->media.media
    };
    lifecyclePending_ = true;
    worker_->replaceStorageMedia(replaceableFloppy_->storage_kind,
                                 replaceableFloppy_->storage_unit, change,
                                 storage);
    updateActions();
}

void
PortableWindow::ejectFloppy()
{
    if ((worker_ == nullptr) || (replaceableFloppy_ == nullptr) ||
        lifecyclePending_)
        return;
    const bm_storage_media_change_t change {};
    lifecyclePending_ = true;
    worker_->replaceStorageMedia(replaceableFloppy_->storage_kind,
                                 replaceableFloppy_->storage_unit, change,
                                 {});
    updateActions();
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
    const int rendererValue = settings.value(
        QStringLiteral("display/renderer"),
        static_cast<int>(DisplayWidget::Renderer::Software)).toInt();
    if ((rendererValue >= static_cast<int>(DisplayWidget::Renderer::Software)) &&
        (rendererValue <= static_cast<int>(DisplayWidget::Renderer::OpenGL))) {
        display_->setRenderer(
            static_cast<DisplayWidget::Renderer>(rendererValue));
        for (QAction *action : rendererGroup_->actions()) {
            if (action->data().toInt() == rendererValue)
                action->setChecked(true);
        }
    }
    const int effectValue = settings.value(
        QStringLiteral("display/effect"),
        static_cast<int>(DisplayWidget::Effect::None)).toInt();
    if ((effectValue >= static_cast<int>(DisplayWidget::Effect::None)) &&
        (effectValue <= static_cast<int>(DisplayWidget::Effect::Crt))) {
        display_->setEffect(static_cast<DisplayWidget::Effect>(effectValue));
        for (QAction *action : effectGroup_->actions()) {
            if (action->data().toInt() == effectValue)
                action->setChecked(true);
        }
    }
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
    settings.setValue(QStringLiteral("display/renderer"),
                      static_cast<int>(display_->renderer()));
    settings.setValue(QStringLiteral("display/effect"),
                      static_cast<int>(display_->effect()));
}

void
PortableWindow::sendKey(QKeyEvent *event, bool pressed)
{
    if ((worker_ == nullptr) || (worker_->state() != BM_SESSION_RUNNING)) {
        event->ignore();
        return;
    }
    if (!bmQtShouldForwardKey(pressed, event->isAutoRepeat())) {
        event->accept();
        return;
    }
    const bm_key_code_t key = bmQtKeyCode(event->key(),
                                         event->nativeScanCode(),
                                         event->nativeVirtualKey());
    if (key == static_cast<bm_key_code_t>(0)) {
        event->ignore();
        return;
    }
    const bm_input_event_t input {
        BM_INPUT_KEY, key, pressed ? 1 : 0,
        event->isAutoRepeat() ? 1 : 0, 0, 0, 0U
    };
    worker_->sendInput(input);
    event->accept();
}

void
PortableWindow::sendPointer(int32_t deltaX, int32_t deltaY, uint8_t buttons)
{
    if ((worker_ == nullptr) || (worker_->state() != BM_SESSION_RUNNING))
        return;
    const bm_input_event_t input {
        BM_INPUT_RELATIVE_POINTER, static_cast<bm_key_code_t>(0), 0, 0,
        deltaX, deltaY, buttons
    };
    worker_->sendInput(input);
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
    updateKeyboardStatus(snapshot.hasKeyboardLeds, snapshot.keyboardLeds);
    if (!snapshot.frame.isNull()) {
        LatencyTrace::event("ui-frame", snapshot.traceFrame, snapshot.traceInput);
        display_->setFrame(snapshot.frame, snapshot.traceFrame);
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
PortableWindow::updateKeyboardStatus(
    bool available, const bm_keyboard_led_state_t &state)
{
    keyboardStatus_->setVisible(available);
    if (!available) {
        keyboardStatus_->clear();
        return;
    }
    const auto indicator = [](const QString &name, bool active) {
        const QString color = active ? QStringLiteral("#2eae4e") :
                                       QStringLiteral("#777777");
        return QStringLiteral("<span style=\"color:%1\">● %2</span>")
            .arg(color, name.toHtmlEscaped());
    };
    keyboardStatus_->setText(
        indicator(tr("Caps"),
                  (state.indicators & BM_KEYBOARD_LED_CAPS_LOCK) != 0U) +
        QStringLiteral(" &nbsp; ") +
        indicator(tr("Num"),
                  (state.indicators & BM_KEYBOARD_LED_NUM_LOCK) != 0U) +
        QStringLiteral(" &nbsp; ") +
        indicator(tr("Scroll"),
                  (state.indicators & BM_KEYBOARD_LED_SCROLL_LOCK) != 0U));
    keyboardStatus_->setToolTip(
        tr("Keyboard indicators controlled by the guest"));
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
        if ((replaceableFloppy_ != nullptr) &&
            (device.kind == replaceableFloppy_->storage_kind) &&
            (device.unit == replaceableFloppy_->storage_unit))
            replaceableFloppyPresent_ = device.media_present;
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
            tr("HD%1").arg(device.unit);
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
        tr("Storage state and completed read/write operations"));
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
    const bool canChangeMedia = !lifecyclePending_ &&
        (replaceableFloppy_ != nullptr) &&
        (state == BM_SESSION_RUNNING || state == BM_SESSION_PAUSED);
    insertFloppyAction_->setEnabled(canChangeMedia);
    ejectFloppyAction_->setEnabled(canChangeMedia &&
                                    replaceableFloppyPresent_);
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
    const bm_machine_config_t *config = machine_ != nullptr ?
        bm_frontend_machine_config(machine_) : nullptr;
    if ((config != nullptr) && (config->definition != nullptr) &&
        (config->definition->scheduler_ticks_per_second != 0U)) {
        const double guestSeconds = static_cast<double>(worker_->ticks()) /
            static_cast<double>(config->definition->scheduler_ticks_per_second);
        text += tr(" — guest %1 s").arg(
            QLocale().toString(guestSeconds, 'f', 1));
    }
    if (hasVideoGeometry_) {
        QString refresh;
        if ((videoGeometry_.refresh_numerator != 0U) &&
            (videoGeometry_.refresh_denominator != 0U)) {
            const double hz = static_cast<double>(
                videoGeometry_.refresh_numerator) /
                static_cast<double>(videoGeometry_.refresh_denominator);
            refresh = QLocale().toString(hz, 'f', 1);
        }
        const QSize output = display_->outputPixelSize();
        text += tr(" — %1×%2")
                    .arg(videoGeometry_.width)
                    .arg(videoGeometry_.height);
        if (!refresh.isEmpty())
            text += tr(" @ %1 Hz").arg(refresh);
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
