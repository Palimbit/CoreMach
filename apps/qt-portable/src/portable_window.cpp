/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_window.h"
#include "machine_dialog.h"

#include <blumach/platforms/null_host.h>

#include <QAction>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QStatusBar>
#include <QToolBar>

PortableWindow::AssetStorage::~AssetStorage()
{
    bm_frontend_readonly_media_close(&media);
    bm_frontend_blob_release(&blob);
}

PortableWindow::PortableWindow(QWidget *parent)
    : QMainWindow(parent), display_(new DisplayWidget), status_(new QLabel),
      host_(bm_null_host_services())
{
    auto *toolbar = addToolBar(tr("Machine"));
    QAction *openAction = toolbar->addAction(tr("Open…"));
    pauseAction_ = toolbar->addAction(tr("Pause"));
    resetAction_ = toolbar->addAction(tr("Reset"));
    stopAction_ = toolbar->addAction(tr("Stop"));
    setWindowTitle(tr("BluMach Portable"));
    setCentralWidget(display_);
    statusBar()->addPermanentWidget(status_, 1);
    connect(openAction, &QAction::triggered, this,
            [this] { chooseMachine(); });
    connect(pauseAction_, &QAction::triggered, this,
            [this] { togglePause(); });
    connect(resetAction_, &QAction::triggered, this,
            [this] { resetMachine(); });
    connect(stopAction_, &QAction::triggered, this,
            [this] { stopMachine(); });
    display_->setKeyHandler(
        [this](QKeyEvent *event, bool pressed) { sendKey(event, pressed); });
    resize(960, 600);
    updateActions();
    showStatus(tr("Open a machine to begin"));
}

PortableWindow::~PortableWindow()
{
    closeMachine();
}

bool
PortableWindow::openInitial(const QString &machineId,
                            const QHash<QString, QString> &paths)
{
    const QByteArray id = machineId.toUtf8();
    const bm_frontend_adapter_t *adapter = bm_frontend_adapter_find(id.constData());
    return adapter != nullptr && openMachine(adapter, paths);
}

void
PortableWindow::chooseMachine()
{
    MachineDialog dialog(this);
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
                QMetaObject::invokeMethod(
                    this,
                    [this, generation,
                     snapshot = std::move(snapshot)]() mutable {
                        if (generation == workerGeneration_)
                            handleSnapshot(std::move(snapshot));
                    },
                    Qt::QueuedConnection);
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
    lastError_ = BM_STATUS_OK;
    lifecyclePending_ = false;
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
    bm_frontend_machine_close(machine_);
    machine_ = nullptr;
    bindings_.clear();
    assets_.clear();
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
PortableWindow::handleSnapshot(SessionWorker::Snapshot snapshot)
{
    if (!snapshot.frame.isNull())
        display_->setFrame(snapshot.frame);
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
    status_->setText(tr("%1 — %2 ticks")
                         .arg(QString::fromLatin1(state))
                         .arg(static_cast<qulonglong>(
                             worker_->ticks())));
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
