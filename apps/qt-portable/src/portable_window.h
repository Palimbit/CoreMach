/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_WINDOW_H
#define BLUMACH_PORTABLE_WINDOW_H

#include "display_widget.h"
#include "portable_catalog.h"
#include "session_worker.h"
#include "snapshot_mailbox.h"

#include <blumach/frontend/file_inputs.h>
#include <blumach/frontend/frontend.h>
#include <blumach/runtime/runtime.h>

#include <QHash>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>

#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QCloseEvent;
class QKeyEvent;
class QLabel;
class QToolBar;

class PortableWindow final : public QMainWindow {
public:
    explicit PortableWindow(QWidget *parent = nullptr);
    ~PortableWindow() override;

    bool openInitial(const QString &machineId,
                     const QHash<QString, QString> &paths);
    bool openInitialProduct(const QString &productId,
                            const QHash<QString, QString> &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

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
    void togglePause();
    void resetMachine();
    void stopMachine();
    void toggleFullscreen(bool enabled);
    void copyFrame();
    void saveFrame();
    void readSettings();
    void writeSettings() const;
    void sendKey(QKeyEvent *event, bool pressed);
    void queueSnapshot(uint64_t generation, SessionWorker::Snapshot snapshot);
    void drainSnapshots(uint64_t generation);
    void handleSnapshot(SessionWorker::Snapshot snapshot);
    void updateStorageStatus(
        const std::vector<bm_storage_device_status_t> &storage);
    void updateActions();
    void showStatus(const QString &detail = QString());
    DisplayWidget *display_;
    QLabel *status_;
    QLabel *storageStatus_;
    QToolBar *machineToolbar_;
    QAction *pauseAction_;
    QAction *resetAction_;
    QAction *stopAction_;
    QAction *fullScreenAction_;
    QAction *smoothScalingAction_;
    QAction *statusBarAction_;
    QAction *copyFrameAction_;
    QAction *saveFrameAction_;
    QActionGroup *scaleGroup_;
    bm_host_services_t host_;
    PortableCatalog catalog_;
    QString catalogError_;
    bm_frontend_machine_t *machine_ = nullptr;
    std::unique_ptr<SessionWorker> worker_;
    SnapshotMailbox snapshotMailbox_;
    std::vector<std::unique_ptr<AssetStorage>> assets_;
    std::vector<bm_frontend_asset_binding_t> bindings_;
    bm_status_t lastError_ = BM_STATUS_OK;
    uint64_t workerGeneration_ = 0U;
    bool lifecyclePending_ = false;
    bool wasMaximizedBeforeFullscreen_ = false;
    QString activeMachineId_;
    bm_video_geometry_t videoGeometry_ {};
    QElapsedTimer frameRateTimer_;
    unsigned int presentedFrames_ = 0U;
    double presentationFps_ = 0.0;
    bool hasVideoGeometry_ = false;
    struct StoragePresentation {
        uint64_t reads = 0U;
        uint64_t writes = 0U;
        qint64 readPulseUntil = 0;
        qint64 writePulseUntil = 0;
        bool initialized = false;
    };
    QElapsedTimer activityTimer_;
    std::vector<StoragePresentation> storagePresentation_;
};

#endif
