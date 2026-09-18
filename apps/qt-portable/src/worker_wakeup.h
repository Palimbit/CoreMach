/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_WORKER_WAKEUP_H
#define BLUMACH_PORTABLE_WORKER_WAKEUP_H
#include <condition_variable>
#include <functional>
#include <mutex>

// Host pacing only. The engine continues to consume explicit guest ticks.
class WorkerWakeup final {
public:
    WorkerWakeup();
    ~WorkerWakeup();
    WorkerWakeup(const WorkerWakeup &) = delete;
    WorkerWakeup &operator=(const WorkerWakeup &) = delete;
    void notify();
    void wait(std::unique_lock<std::mutex> &lock, bool running,
              const std::function<bool()> &pending);
private:
    std::condition_variable fallback_;
#ifdef _WIN32
    void *event_ = nullptr;
    void *timer_ = nullptr;
#endif
};
#endif
