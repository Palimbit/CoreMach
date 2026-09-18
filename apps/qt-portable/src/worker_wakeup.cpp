/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "worker_wakeup.h"
#include <chrono>
#include <QDebug>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

WorkerWakeup::WorkerWakeup()
{
#ifdef _WIN32
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!event_ || !timer_) {
        if (event_) CloseHandle(event_);
        if (timer_) CloseHandle(timer_);
        event_ = timer_ = nullptr;
        qWarning("High-resolution worker wait unavailable; using standard wait");
    }
#endif
}

WorkerWakeup::~WorkerWakeup()
{
#ifdef _WIN32
    if (timer_) CloseHandle(timer_);
    if (event_) CloseHandle(event_);
#endif
}

void WorkerWakeup::notify()
{
#ifdef _WIN32
    if (event_) SetEvent(event_);
#endif
    fallback_.notify_one();
}

void WorkerWakeup::wait(std::unique_lock<std::mutex> &lock, bool running,
                        const std::function<bool()> &pending)
{
    if (pending()) return;
#ifdef _WIN32
    if (timer_ && event_) {
        LARGE_INTEGER due;
        due.QuadPart = -10000; // Relative 1 ms in 100 ns units.
        if (!running || SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            if (!running) CancelWaitableTimer(timer_);
            HANDLE handles[] = { event_, timer_ };
            lock.unlock();
            const DWORD result = WaitForMultipleObjects(running ? 2 : 1,
                                                         handles, FALSE, INFINITE);
            lock.lock();
            if (result != WAIT_FAILED) return;
            qWarning("Worker wait failed; using standard wait");
        }
    }
#endif
    if (running)
        fallback_.wait_for(lock, std::chrono::milliseconds(1), pending);
    else
        fallback_.wait(lock, pending);
}
