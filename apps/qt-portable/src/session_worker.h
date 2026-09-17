/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_SESSION_WORKER_H
#define BLUMACH_PORTABLE_SESSION_WORKER_H

#include <blumach/runtime/runtime.h>

#include <QImage>
#include "worker_wakeup.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class TickPacer;

class SessionWorker final {
public:
    struct Snapshot {
        bm_status_t status = BM_STATUS_OK;
        bm_session_state_t state = BM_SESSION_NEW;
        uint64_t ticks = 0U;
        bm_video_geometry_t geometry {};
        bool hasVideo = false;
        bool hasKeyboardLeds = false;
        bm_keyboard_led_state_t keyboardLeds {};
        std::vector<bm_storage_device_status_t> storage;
        QImage frame;
        bool lifecycleResult = false;
        uint64_t traceFrame = 0U;
        uint64_t traceInput = 0U;
    };
    using SnapshotHandler = std::function<void(Snapshot)>;

    SessionWorker(const bm_host_services_t &host,
                  const bm_machine_config_t *configuration,
                  SnapshotHandler handler);
    ~SessionWorker();

    SessionWorker(const SessionWorker &) = delete;
    SessionWorker &operator=(const SessionWorker &) = delete;

    bm_status_t start();
    void pause();
    void resume();
    void reset();
    void stop();
    void sendInput(const bm_input_event_t &event);
    void replaceStorageMedia(bm_storage_device_kind_t kind, uint32_t unit,
                             const bm_storage_media_change_t &change,
                             std::shared_ptr<void> owner);
    bm_session_state_t state() const;
    uint64_t ticks() const;

private:
    enum class CommandKind {
        Pause, Resume, Reset, Stop, Input, StorageMedia, Shutdown
    };
    struct Command {
        explicit Command(CommandKind value) : kind(value) {}
        CommandKind kind;
        bm_input_event_t input {};
        uint64_t traceInput = 0U;
        bm_storage_device_kind_t storageKind = BM_STORAGE_DEVICE_FLOPPY;
        uint32_t storageUnit = 0U;
        bm_storage_media_change_t mediaChange {};
        std::shared_ptr<void> mediaOwner;
    };
    struct MountedMedia {
        bm_storage_device_kind_t kind;
        uint32_t unit;
        std::shared_ptr<void> owner;
    };

    void enqueue(Command command);
    void run();
    bool processCommands(bm_session_t *session, TickPacer &pacer);
    bm_status_t renderFrame(bm_session_t *session, QImage &frame,
                            bm_video_geometry_t &geometry);
    static bm_status_t collectStorage(
        bm_session_t *session,
        std::vector<bm_storage_device_status_t> &storage);
    void publish(bm_session_t *session, bm_status_t status,
                 QImage frame = QImage(), bool lifecycleResult = false,
                 const bm_video_geometry_t *geometry = nullptr,
                 std::vector<bm_storage_device_status_t> storage = {});

    bm_host_services_t host_;
    const bm_machine_config_t *configuration_;
    uint64_t ticksPerSecond_;
    SnapshotHandler handler_;
    std::atomic<bm_session_state_t> state_ { BM_SESSION_NEW };
    std::atomic<uint64_t> ticks_ { 0U };
    std::mutex mutex_;
    std::condition_variable condition_;
    WorkerWakeup wakeup_;
    std::deque<Command> commands_;
    std::vector<MountedMedia> mountedMedia_;
    std::thread thread_;
    bool ready_ = false;
    uint64_t traceInput_ = 0U;
    bm_status_t startStatus_ = BM_STATUS_INVALID_STATE;
};

#endif
