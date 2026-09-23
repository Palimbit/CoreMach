/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_WINDOW_H
#define BLUMACH_PORTABLE_WINDOW_H

#include "display_widget.h"
#include "machine_profile_store.h"
#include "portable_catalog.h"
#include "session_worker.h"
#include "snapshot_mailbox.h"

#include <blumach/frontend/file_inputs.h>
#include <blumach/frontend/frontend.h>
#include <blumach/runtime/runtime.h>

#include <QHash>
#include <QByteArray>
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
class LauncherPage;
class QToolBar;

class PortableWindow final : public QMainWindow {
public:
    explicit PortableWindow(QWidget *parent = nullptr);
    ~PortableWindow() override;

    bool openInitial(const QString &machineId,
                     const QHash<QString, QString> &paths);
    bool openInitialProduct(const QString &productId,
                            const QHash<QString, QString> &paths);
    void showLauncher();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct AssetStorage {
        bm_frontend_blob_t blob { nullptr, 0U };
        bm_frontend_readonly_media_t media {};
        ~AssetStorage();
    };

    struct PersistentStateStorage {
        const bm_frontend_persistent_state_requirement_t *requirement = nullptr;
        QByteArray data;
        bool retain = true;
    };

    void chooseMachine(const QString &productId = QString());
    void chooseSavedMachine(const QString &profileId);
    void editSavedMachine(const QString &profileId);
    void refreshProfiles();
    void savePreview();
    bool openMachine(const bm_frontend_adapter_t *adapter,
                     const QHash<QString, QString> &paths,
                     const QString &stateId = QString(),
                     const QString &displayName = QString(),
                     const QHash<QString, quint32> &options = {});
    void closeMachine();
    void togglePause();
    void resetMachine();
    void stopMachine();
    void setPersistentStateRetention(bool enabled);
    void insertFloppy();
    void ejectFloppy();
    void toggleFullscreen(bool enabled);
    void copyFrame();
    void saveFrame();
    void readSettings();
    void writeSettings() const;
    void sendKey(QKeyEvent *event, bool pressed);
    void sendPointer(int32_t deltaX, int32_t deltaY, uint8_t buttons);
    void queueSnapshot(uint64_t generation, SessionWorker::Snapshot snapshot);
    void drainSnapshots(uint64_t generation);
    void handleSnapshot(SessionWorker::Snapshot snapshot);
    void updateStorageStatus(
        const std::vector<bm_storage_device_status_t> &storage);
    void updateKeyboardStatus(bool available,
                              const bm_keyboard_led_state_t &state);
    void updateActions();
    void showStatus(const QString &detail = QString());
    DisplayWidget *display_;
    LauncherPage *launcher_ = nullptr;
    QLabel *status_;
    QLabel *storageStatus_;
    QLabel *keyboardStatus_;
    QToolBar *machineToolbar_;
    QAction *pauseAction_;
    QAction *resetAction_;
    QAction *stopAction_;
    QAction *retainStateAction_;
    QAction *insertFloppyAction_;
    QAction *ejectFloppyAction_;
    QAction *fullScreenAction_;
    QAction *smoothScalingAction_;
    QAction *statusBarAction_;
    QAction *copyFrameAction_;
    QAction *saveFrameAction_;
    QAction *catalogAction_;
    QActionGroup *scaleGroup_;
    QActionGroup *rendererGroup_;
    QActionGroup *effectGroup_;
    bm_host_services_t host_;
    MachineProfileStore profileStore_;
    QVector<PortableMachineProfile> profiles_;
    PortableCatalog catalog_;
    QString catalogError_;
    QString resourceRoot_;
    bm_frontend_machine_t *machine_ = nullptr;
    std::unique_ptr<SessionWorker> worker_;
    SnapshotMailbox snapshotMailbox_;
    std::vector<std::unique_ptr<AssetStorage>> assets_;
    std::vector<bm_frontend_asset_binding_t> bindings_;
    std::vector<std::unique_ptr<PersistentStateStorage>> persistentStates_;
    std::vector<bm_frontend_persistent_state_binding_t> persistentBindings_;
    const bm_frontend_asset_requirement_t *replaceableFloppy_ = nullptr;
    bm_status_t lastError_ = BM_STATUS_OK;
    uint64_t workerGeneration_ = 0U;
    bool lifecyclePending_ = false;
    bool wasMaximizedBeforeFullscreen_ = false;
    QString activeMachineId_;
    QString activeStateId_;
    QString activeDisplayName_;
    bm_video_geometry_t videoGeometry_ {};
    QElapsedTimer frameRateTimer_;
    QElapsedTimer previewTimer_;
    QImage lastPreview_;
    bool previewSaved_ = false;
    unsigned int presentedFrames_ = 0U;
    double presentationFps_ = 0.0;
    bool hasVideoGeometry_ = false;
    bool replaceableFloppyPresent_ = false;
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
