/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "portable_window.h"
#include "machine_dialog.h"

#include <blumach/platforms/null_host.h>

#include <QAction>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include <limits>

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
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    display_->setKeyHandler(
        [this](QKeyEvent *event, bool pressed) { sendKey(event, pressed); });
    timer_.setInterval(16);
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
    if (result == BM_STATUS_OK)
        result = bm_session_create(&host_, &session_);
    if (result == BM_STATUS_OK)
        result = bm_session_configure(session_,
                                      bm_frontend_machine_config(machine_));
    if (result == BM_STATUS_OK)
        result = bm_session_start(session_);
    if (result != BM_STATUS_OK) {
        QMessageBox::critical(this, tr("Could not start machine"),
                              tr("The portable session rejected the configuration "
                                 "(status %1).").arg(static_cast<int>(result)));
        closeMachine();
        return false;
    }
    timer_.start();
    display_->setFocus();
    updateActions();
    showStatus();
    return true;
}

void
PortableWindow::closeMachine()
{
    timer_.stop();
    bm_session_destroy(session_);
    session_ = nullptr;
    bm_frontend_machine_close(machine_);
    machine_ = nullptr;
    bindings_.clear();
    assets_.clear();
    pixels_.clear();
    display_->setFrame(QImage());
    updateActions();
}

void
PortableWindow::advance()
{
    if ((session_ == nullptr) ||
        (bm_session_state(session_) != BM_SESSION_RUNNING))
        return;
    bm_status_t result = bm_session_run_for(session_, UINT64_C(100000));
    bm_video_geometry_t geometry {};
    if (result == BM_STATUS_OK)
        result = bm_session_video_geometry(session_, &geometry);
    if ((result == BM_STATUS_OK) && (geometry.width != 0U) &&
        (geometry.height != 0U) &&
        (geometry.width <= std::numeric_limits<size_t>::max() /
                              geometry.height)) {
        pixels_.resize(static_cast<size_t>(geometry.width) * geometry.height);
        bm_video_framebuffer_t framebuffer {
            pixels_.data(), pixels_.size(), geometry.width, geometry
        };
        result = bm_session_render_video(session_, &framebuffer);
        if (result == BM_STATUS_OK) {
            const QImage frame(reinterpret_cast<const uchar *>(pixels_.data()),
                               static_cast<int>(geometry.width),
                               static_cast<int>(geometry.height),
                               static_cast<qsizetype>(geometry.width * 4U),
                               QImage::Format_RGB32);
            display_->setFrame(frame.copy());
        }
    }
    if (result != BM_STATUS_OK) {
        timer_.stop();
        QMessageBox::critical(this, tr("Machine stopped"),
                              tr("Portable execution failed with status %1.")
                                  .arg(static_cast<int>(result)));
        (void) bm_session_stop(session_);
        updateActions();
    }
    showStatus();
}

void
PortableWindow::togglePause()
{
    if (session_ == nullptr)
        return;
    bm_status_t result = BM_STATUS_INVALID_STATE;
    if (bm_session_state(session_) == BM_SESSION_RUNNING) {
        timer_.stop();
        result = bm_session_pause(session_);
    } else if (bm_session_state(session_) == BM_SESSION_PAUSED) {
        result = bm_session_resume(session_);
        if (result == BM_STATUS_OK)
            timer_.start();
    }
    if (result != BM_STATUS_OK)
    {
        if (bm_session_state(session_) == BM_SESSION_RUNNING)
            timer_.start();
        QMessageBox::warning(this, tr("Lifecycle error"),
                             tr("The requested transition failed (%1).")
                                 .arg(static_cast<int>(result)));
    }
    updateActions();
    showStatus();
}

void
PortableWindow::resetMachine()
{
    if (session_ != nullptr) {
        const bm_status_t result = bm_session_reset(session_);
        if (result != BM_STATUS_OK)
            QMessageBox::warning(this, tr("Reset failed"),
                                 tr("Reset failed with status %1.")
                                     .arg(static_cast<int>(result)));
        showStatus();
    }
}

void
PortableWindow::stopMachine()
{
    if (session_ != nullptr) {
        timer_.stop();
        const bm_status_t result = bm_session_stop(session_);
        if (result != BM_STATUS_OK)
            QMessageBox::warning(this, tr("Stop failed"),
                                 tr("Stop failed with status %1.")
                                     .arg(static_cast<int>(result)));
        updateActions();
        showStatus();
    }
}

void
PortableWindow::sendKey(QKeyEvent *event, bool pressed)
{
    if ((session_ == nullptr) ||
        (bm_session_state(session_) != BM_SESSION_RUNNING)) {
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
    if (bm_session_send_input(session_, &input) == BM_STATUS_OK)
        event->accept();
    else
        event->ignore();
}

void
PortableWindow::updateActions()
{
    const bm_session_state_t state = session_ != nullptr ?
                                     bm_session_state(session_) : BM_SESSION_NEW;
    pauseAction_->setEnabled(state == BM_SESSION_RUNNING ||
                             state == BM_SESSION_PAUSED);
    pauseAction_->setText(state == BM_SESSION_PAUSED ? tr("Resume") :
                                                       tr("Pause"));
    resetAction_->setEnabled(state == BM_SESSION_RUNNING ||
                             state == BM_SESSION_PAUSED);
    stopAction_->setEnabled(state == BM_SESSION_RUNNING ||
                            state == BM_SESSION_PAUSED);
}

void
PortableWindow::showStatus(const QString &detail)
{
    if (!detail.isEmpty()) {
        status_->setText(detail);
        return;
    }
    if (session_ == nullptr) {
        status_->setText(tr("No machine"));
        return;
    }
    const char *state = "unknown";
    switch (bm_session_state(session_)) {
        case BM_SESSION_RUNNING: state = "running"; break;
        case BM_SESSION_PAUSED: state = "paused"; break;
        case BM_SESSION_STOPPED: state = "stopped"; break;
        case BM_SESSION_CONFIGURED: state = "configured"; break;
        case BM_SESSION_NEW: state = "new"; break;
    }
    status_->setText(tr("%1 — %2 ticks")
                         .arg(QString::fromLatin1(state))
                         .arg(static_cast<qulonglong>(
                             bm_session_time(session_))));
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
