/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_WINDOW_H
#define BLUMACH_PORTABLE_WINDOW_H

#include "display_widget.h"

#include <blumach/frontend/file_inputs.h>
#include <blumach/frontend/frontend.h>
#include <blumach/runtime/runtime.h>

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

class QAction;
class QKeyEvent;
class QLabel;

class PortableWindow final : public QMainWindow {
public:
    explicit PortableWindow(QWidget *parent = nullptr);
    ~PortableWindow() override;

    bool openInitial(const QString &machineId,
                     const QHash<QString, QString> &paths);

private:
    struct AssetStorage {
        bm_frontend_blob_t blob { nullptr, 0U };
        bm_frontend_readonly_media_t media {};
        ~AssetStorage();
    };

    void chooseMachine();
    bool openMachine(const bm_frontend_adapter_t *adapter,
                     const QHash<QString, QString> &paths);
    void closeMachine();
    void advance();
    void togglePause();
    void resetMachine();
    void stopMachine();
    void sendKey(QKeyEvent *event, bool pressed);
    void updateActions();
    void showStatus(const QString &detail = QString());
    static bm_key_code_t mapKey(int key);

    DisplayWidget *display_;
    QLabel *status_;
    QAction *pauseAction_;
    QAction *resetAction_;
    QAction *stopAction_;
    QTimer timer_;
    bm_host_services_t host_;
    bm_frontend_machine_t *machine_ = nullptr;
    bm_session_t *session_ = nullptr;
    std::vector<std::unique_ptr<AssetStorage>> assets_;
    std::vector<bm_frontend_asset_binding_t> bindings_;
    std::vector<uint32_t> pixels_;
};

#endif
