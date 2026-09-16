/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "session_worker.h"
#include "tick_pacer.h"

#include <chrono>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {
constexpr uint64_t maximumChunk = UINT64_C(250000);
constexpr auto framePeriod = std::chrono::milliseconds(16);
}

SessionWorker::SessionWorker(const bm_host_services_t &host,
                             const bm_machine_config_t *configuration,
                             uint64_t ticksPerSecond,
                             SnapshotHandler handler)
    : host_(host), configuration_(configuration),
      ticksPerSecond_(ticksPerSecond), handler_(std::move(handler))
{
}

SessionWorker::~SessionWorker()
{
    if (thread_.joinable()) {
        enqueue({ CommandKind::Shutdown, {} });
        thread_.join();
    }
}

bm_status_t
SessionWorker::start()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_.joinable())
        return BM_STATUS_INVALID_STATE;
    try {
        thread_ = std::thread(&SessionWorker::run, this);
    } catch (const std::system_error &) {
        return BM_STATUS_OUT_OF_MEMORY;
    }
    condition_.wait(lock, [this] { return ready_; });
    return startStatus_;
}

void SessionWorker::pause() { enqueue({ CommandKind::Pause, {} }); }
void SessionWorker::resume() { enqueue({ CommandKind::Resume, {} }); }
void SessionWorker::reset() { enqueue({ CommandKind::Reset, {} }); }
void SessionWorker::stop() { enqueue({ CommandKind::Stop, {} }); }

void
SessionWorker::sendInput(const bm_input_event_t &event)
{
    enqueue({ CommandKind::Input, event });
}

bm_session_state_t SessionWorker::state() const { return state_.load(); }
uint64_t SessionWorker::ticks() const { return ticks_.load(); }

void
SessionWorker::enqueue(Command command)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back(command);
    }
    condition_.notify_one();
}

bool
SessionWorker::processCommands(bm_session_t *session)
{
    std::deque<Command> commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands.swap(commands_);
    }
    for (const Command &command : commands) {
        bm_status_t status = BM_STATUS_OK;
        const bool lifecycleResult = command.kind != CommandKind::Input;
        switch (command.kind) {
            case CommandKind::Pause: status = bm_session_pause(session); break;
            case CommandKind::Resume: status = bm_session_resume(session); break;
            case CommandKind::Reset: status = bm_session_reset(session); break;
            case CommandKind::Stop: status = bm_session_stop(session); break;
            case CommandKind::Input:
                status = bm_session_send_input(session, &command.input);
                break;
            case CommandKind::Shutdown: return false;
        }
        publish(session, status, QImage(), lifecycleResult);
    }
    return true;
}

bm_status_t
SessionWorker::renderFrame(bm_session_t *session, QImage &frame)
{
    bm_video_geometry_t geometry {};
    bm_status_t status = bm_session_video_geometry(session, &geometry);
    if (status != BM_STATUS_OK)
        return status;
    if ((geometry.width == 0U) || (geometry.height == 0U) ||
        (static_cast<size_t>(geometry.width) >
         std::numeric_limits<size_t>::max() / geometry.height))
        return BM_STATUS_INVALID_STATE;
    std::vector<uint32_t> pixels(
        static_cast<size_t>(geometry.width) * geometry.height);
    bm_video_framebuffer_t framebuffer {
        pixels.data(), pixels.size(), geometry.width, geometry
    };
    status = bm_session_render_video(session, &framebuffer);
    if (status != BM_STATUS_OK)
        return status;
    const QImage view(reinterpret_cast<const uchar *>(pixels.data()),
                      static_cast<int>(geometry.width),
                      static_cast<int>(geometry.height),
                      static_cast<qsizetype>(geometry.width * 4U),
                      QImage::Format_RGB32);
    frame = view.copy();
    return frame.isNull() ? BM_STATUS_OUT_OF_MEMORY : BM_STATUS_OK;
}

void
SessionWorker::publish(bm_session_t *session, bm_status_t status, QImage frame,
                       bool lifecycleResult)
{
    Snapshot snapshot;
    snapshot.status = status;
    snapshot.state = session != nullptr ? bm_session_state(session) :
                                          BM_SESSION_NEW;
    snapshot.ticks = session != nullptr ? bm_session_time(session) : 0U;
    snapshot.frame = std::move(frame);
    snapshot.lifecycleResult = lifecycleResult;
    state_.store(snapshot.state);
    ticks_.store(snapshot.ticks);
    if (handler_)
        handler_(std::move(snapshot));
}

void
SessionWorker::run()
{
    bm_session_t *session = nullptr;
    bm_status_t status = bm_session_create(&host_, &session);
    if (status == BM_STATUS_OK)
        status = bm_session_configure(session, configuration_);
    if (status == BM_STATUS_OK)
        status = bm_session_start(session);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        startStatus_ = status;
        ready_ = true;
    }
    condition_.notify_all();
    publish(session, status);
    if (status != BM_STATUS_OK) {
        bm_session_destroy(session);
        return;
    }

    TickPacer pacer(ticksPerSecond_, maximumChunk);
    auto now = TickPacer::Clock::now();
    auto nextFrame = now;
    pacer.reset(now);
    bool active = true;
    while (active) {
        active = processCommands(session);
        if (!active)
            break;
        now = TickPacer::Clock::now();
        if (bm_session_state(session) == BM_SESSION_RUNNING) {
            const uint64_t due = pacer.ticksDue(now);
            if (due != 0U)
                status = bm_session_run_for(session, due);
            if ((status == BM_STATUS_OK) && (now >= nextFrame)) {
                QImage frame;
                status = renderFrame(session, frame);
                publish(session, status, std::move(frame));
                nextFrame = now + framePeriod;
            }
            if (status != BM_STATUS_OK) {
                (void) bm_session_stop(session);
                publish(session, status);
            }
        } else {
            pacer.reset(now);
            nextFrame = now;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::milliseconds(1),
                            [this] { return !commands_.empty(); });
    }
    bm_session_destroy(session);
}
