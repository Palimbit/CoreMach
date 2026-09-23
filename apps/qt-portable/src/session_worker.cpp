/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "session_worker.h"
#include "tick_pacer.h"
#include "latency_trace.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {
/* Keep the emulation boundary short enough that input queued by the UI is not
 * held behind a long catch-up slice. */
constexpr uint64_t maximumChunksPerSecond = UINT64_C(200);
/* Give every make/break transition two milliseconds of guest time. This keeps
 * a Qt event burst ordered like a keyboard byte stream while the pacer account
 * prevents the extra work from making the guest run ahead of wall time. */
constexpr uint64_t inputTransitionsPerSecond = UINT64_C(500);
constexpr auto framePeriod = std::chrono::milliseconds(16);

uint64_t
ticksForInterval(uint64_t ticksPerSecond, uint64_t intervalsPerSecond)
{
    return std::max(UINT64_C(1), ticksPerSecond / intervalsPerSecond);
}

int32_t
saturatingPointerDelta(int32_t current, int32_t added)
{
    const int64_t result = static_cast<int64_t>(current) + added;
    return static_cast<int32_t>(std::clamp(
        result, static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
        static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
}
}

SessionWorker::SessionWorker(const bm_host_services_t &host,
                             const bm_machine_config_t *configuration,
                             SnapshotHandler handler,
                             std::vector<PersistentState> persistentStates)
    : host_(host), configuration_(configuration),
      ticksPerSecond_(configuration->definition->scheduler_ticks_per_second),
      handler_(std::move(handler)),
      persistentStates_(std::move(persistentStates))
{
}

SessionWorker::~SessionWorker()
{
    shutdown();
}

void
SessionWorker::shutdown()
{
    if (!thread_.joinable())
        return;
    enqueue(Command(CommandKind::Shutdown));
    thread_.join();
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

void SessionWorker::pause() { enqueue(Command(CommandKind::Pause)); }
void SessionWorker::resume() { enqueue(Command(CommandKind::Resume)); }
void SessionWorker::reset() { enqueue(Command(CommandKind::Reset)); }
void SessionWorker::stop() { enqueue(Command(CommandKind::Stop)); }

void
SessionWorker::sendInput(const bm_input_event_t &event)
{
    Command command(CommandKind::Input);
    command.input = event;
    command.traceInput = LatencyTrace::nextId();
    LatencyTrace::event("input-queued", command.traceInput);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ((event.kind == BM_INPUT_RELATIVE_POINTER) && !commands_.empty() &&
            (commands_.back().kind == CommandKind::Input) &&
            (commands_.back().input.kind == BM_INPUT_RELATIVE_POINTER)) {
            Command &pending = commands_.back();
            pending.input.delta_x = saturatingPointerDelta(
                pending.input.delta_x, event.delta_x);
            pending.input.delta_y = saturatingPointerDelta(
                pending.input.delta_y, event.delta_y);
            pending.input.buttons = event.buttons;
            pending.traceInput = command.traceInput;
        } else
            commands_.push_back(std::move(command));
    }
    wakeup_.notify();
}

void
SessionWorker::replaceStorageMedia(bm_storage_device_kind_t kind,
                                   uint32_t unit,
                                   const bm_storage_media_change_t &change,
                                   std::shared_ptr<void> owner)
{
    Command command(CommandKind::StorageMedia);
    command.storageKind = kind;
    command.storageUnit = unit;
    command.mediaChange = change;
    command.mediaOwner = std::move(owner);
    enqueue(std::move(command));
}

bm_session_state_t SessionWorker::state() const { return state_.load(); }
uint64_t SessionWorker::ticks() const { return ticks_.load(); }

const std::vector<SessionWorker::PersistentState> &
SessionWorker::persistentStates() const
{
    return persistentStates_;
}

void
SessionWorker::enqueue(Command command)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back(command);
    }
    wakeup_.notify();
}

bool
SessionWorker::processCommands(bm_session_t *session, TickPacer &pacer)
{
    std::deque<Command> commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands.swap(commands_);
    }
    for (const Command &command : commands) {
        bm_status_t status = BM_STATUS_OK;
        const bool lifecycleResult = command.kind != CommandKind::Input;
        FailurePhase failurePhase = FailurePhase::Lifecycle;
        switch (command.kind) {
            case CommandKind::Pause: status = bm_session_pause(session); break;
            case CommandKind::Resume: status = bm_session_resume(session); break;
            case CommandKind::Reset: status = bm_session_reset(session); break;
            case CommandKind::Stop:
                capturePersistentStates(session);
                status = bm_session_stop(session);
                break;
            case CommandKind::Input:
                failurePhase = FailurePhase::Input;
                traceInput_ = command.traceInput;
                LatencyTrace::event("input-dispatch", traceInput_);
                status = bm_session_send_input(session, &command.input);
                if ((status == BM_STATUS_OK) &&
                    (command.input.kind == BM_INPUT_KEY) &&
                    (bm_session_state(session) == BM_SESSION_RUNNING)) {
                    const uint64_t inputTransitionTicks = ticksForInterval(
                        ticksPerSecond_, inputTransitionsPerSecond);
                    status = bm_session_run_for(session, inputTransitionTicks);
                    if (status == BM_STATUS_OK)
                        pacer.account(inputTransitionTicks);
                }
                LatencyTrace::event("input-complete", traceInput_);
                break;
            case CommandKind::StorageMedia:
                failurePhase = FailurePhase::MediaChange;
                status = bm_session_replace_storage_media(
                    session, command.storageKind, command.storageUnit,
                    &command.mediaChange);
                if (status == BM_STATUS_OK) {
                    auto mounted = std::find_if(
                        mountedMedia_.begin(), mountedMedia_.end(),
                        [&command](const MountedMedia &entry) {
                            return entry.kind == command.storageKind &&
                                   entry.unit == command.storageUnit;
                        });
                    if (mounted == mountedMedia_.end()) {
                        mountedMedia_.push_back({ command.storageKind,
                                                  command.storageUnit,
                                                  command.mediaOwner });
                    } else {
                        mounted->owner = command.mediaOwner;
                    }
                }
                break;
            case CommandKind::Shutdown: return false;
        }
        publish(session, status, QImage(), lifecycleResult, nullptr, {},
                failurePhase);
    }
    return true;
}

void
SessionWorker::capturePersistentStates(bm_session_t *session)
{
    if ((session == nullptr) ||
        ((bm_session_state(session) != BM_SESSION_RUNNING) &&
         (bm_session_state(session) != BM_SESSION_PAUSED)))
        return;
    for (PersistentState &state : persistentStates_) {
        size_t requiredSize = 0U;
        state.status = bm_session_persistent_state_size(
            session, state.role.c_str(), &requiredSize);
        if (state.status != BM_STATUS_OK) {
            state.data.clear();
            continue;
        }
        state.data.resize(requiredSize);
        state.status = bm_session_save_persistent_state(
            session, state.role.c_str(), state.data.data(), state.data.size());
        if (state.status != BM_STATUS_OK)
            state.data.clear();
    }
}

bm_status_t
SessionWorker::renderFrame(bm_session_t *session, QImage &frame,
                           bm_video_geometry_t &geometry)
{
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

bm_status_t
SessionWorker::collectStorage(
    bm_session_t *session,
    std::vector<bm_storage_device_status_t> &storage)
{
    size_t count = 0U;
    bm_status_t status = bm_session_storage_device_count(session, &count);
    if (status == BM_STATUS_UNSUPPORTED) {
        storage.clear();
        return BM_STATUS_OK;
    }
    if (status != BM_STATUS_OK)
        return status;
    storage.resize(count);
    for (size_t index = 0U; index < count; ++index) {
        status = bm_session_storage_device_status(session, index,
                                                  &storage[index]);
        if (status != BM_STATUS_OK) {
            storage.clear();
            return status;
        }
    }
    return BM_STATUS_OK;
}

void
SessionWorker::publish(bm_session_t *session, bm_status_t status, QImage frame,
                       bool lifecycleResult,
                       const bm_video_geometry_t *geometry,
                       std::vector<bm_storage_device_status_t> storage,
                       FailurePhase failurePhase)
{
    Snapshot snapshot;
    snapshot.status = status;
    snapshot.failurePhase = failurePhase;
    snapshot.state = session != nullptr ? bm_session_state(session) :
                                          BM_SESSION_NEW;
    snapshot.ticks = session != nullptr ? bm_session_time(session) : 0U;
    if ((session != nullptr) &&
        ((snapshot.state == BM_SESSION_RUNNING) ||
         (snapshot.state == BM_SESSION_PAUSED))) {
        const bm_status_t keyboardStatus = bm_session_keyboard_leds(
            session, &snapshot.keyboardLeds);
        if (keyboardStatus == BM_STATUS_OK)
            snapshot.hasKeyboardLeds = true;
        else if ((keyboardStatus != BM_STATUS_UNSUPPORTED) &&
                 (snapshot.status == BM_STATUS_OK)) {
            snapshot.status = keyboardStatus;
            snapshot.failurePhase = FailurePhase::KeyboardStatus;
        }
    }
    if (geometry != nullptr) {
        snapshot.geometry = *geometry;
        snapshot.hasVideo = true;
    }
    snapshot.storage = std::move(storage);
    snapshot.frame = std::move(frame);
    if (!snapshot.frame.isNull()) {
        snapshot.traceFrame = LatencyTrace::nextId();
        snapshot.traceInput = traceInput_;
        LatencyTrace::event("frame-ready", snapshot.traceFrame, traceInput_);
        LatencyTrace::event("guest-ticks", snapshot.traceFrame, snapshot.ticks);
    }
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
    publish(session, status, QImage(), false, nullptr, {},
            FailurePhase::Startup);
    if (status != BM_STATUS_OK) {
        bm_session_destroy(session);
        return;
    }

    TickPacer pacer(
        ticksPerSecond_,
        ticksForInterval(ticksPerSecond_, maximumChunksPerSecond));
    auto now = TickPacer::Clock::now();
    auto nextFrame = now;
    pacer.reset(now);
    bool active = true;
    while (active) {
        active = processCommands(session, pacer);
        if (!active)
            break;
        now = TickPacer::Clock::now();
        if (bm_session_state(session) == BM_SESSION_RUNNING) {
            FailurePhase failurePhase = FailurePhase::Unknown;
            const uint64_t due = pacer.ticksDue(now);
            if (due != 0U) {
                status = bm_session_run_for(session, due);
                if (status != BM_STATUS_OK)
                    failurePhase = FailurePhase::Emulation;
            }
            if ((status == BM_STATUS_OK) && (now >= nextFrame)) {
                const uint64_t renderStart = LatencyTrace::enabled() ? LatencyTrace::now() : 0U;
                QImage frame;
                bm_video_geometry_t geometry {};
                std::vector<bm_storage_device_status_t> storage;
                status = renderFrame(session, frame, geometry);
                if (status != BM_STATUS_OK)
                    failurePhase = FailurePhase::Video;
                if (LatencyTrace::enabled())
                    LatencyTrace::event("render-us", 0U, LatencyTrace::now() - renderStart);
                if (status == BM_STATUS_OK) {
                    status = collectStorage(session, storage);
                    if (status != BM_STATUS_OK)
                        failurePhase = FailurePhase::StorageStatus;
                }
                publish(session, status, std::move(frame), false, &geometry,
                        std::move(storage), failurePhase);
                nextFrame = now + framePeriod;
            }
            if (status != BM_STATUS_OK) {
                capturePersistentStates(session);
                (void) bm_session_stop(session);
                publish(session, status, QImage(), false, nullptr, {},
                        failurePhase);
            }
        } else {
            pacer.reset(now);
            nextFrame = now;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        wakeup_.wait(lock, bm_session_state(session) == BM_SESSION_RUNNING,
                     [this] { return !commands_.empty(); });
    }
    capturePersistentStates(session);
    bm_session_destroy(session);
}
