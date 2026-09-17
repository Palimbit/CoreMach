/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_SESSION_WORKER_H
#define BLUMACH_PORTABLE_SESSION_WORKER_H

#include <blumach/runtime/runtime.h>

#include <QImage>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class SessionWorker final {
public:
    struct Snapshot {
        bm_status_t status = BM_STATUS_OK;
        bm_session_state_t state = BM_SESSION_NEW;
        uint64_t ticks = 0U;
        bm_video_geometry_t geometry {};
        bool hasVideo = false;
        std::vector<bm_storage_device_status_t> storage;
        QImage frame;
        bool lifecycleResult = false;
    };
    using SnapshotHandler = std::function<void(Snapshot)>;

    SessionWorker(const bm_host_services_t &host,
                  const bm_machine_config_t *configuration,
                  uint64_t ticksPerSecond, SnapshotHandler handler);
    ~SessionWorker();

    SessionWorker(const SessionWorker &) = delete;
    SessionWorker &operator=(const SessionWorker &) = delete;

    bm_status_t start();
    void pause();
    void resume();
    void reset();
    void stop();
    void sendInput(const bm_input_event_t &event);
    bm_session_state_t state() const;
    uint64_t ticks() const;

private:
    enum class CommandKind { Pause, Resume, Reset, Stop, Input, Shutdown };
    struct Command {
        CommandKind kind;
        bm_input_event_t input {};
    };

    void enqueue(Command command);
    void run();
    bool processCommands(bm_session_t *session);
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
    std::deque<Command> commands_;
    std::thread thread_;
    bool ready_ = false;
    bm_status_t startStatus_ = BM_STATUS_INVALID_STATE;
};

#endif
