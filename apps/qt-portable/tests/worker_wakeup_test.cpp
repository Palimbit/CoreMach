/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "worker_wakeup.h"
#include <cassert>
#include <chrono>
#include <thread>

int main()
{
    WorkerWakeup wakeup;
    std::mutex mutex;
    bool pending = true;
    std::unique_lock<std::mutex> lock(mutex);
    // Already queued work must not block, including a paused worker.
    wakeup.wait(lock, false, [&] { return pending; });
    pending = false;
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        { std::lock_guard<std::mutex> guard(mutex); pending = true; }
        wakeup.notify();
    });
    // Spurious wakeups are permitted, but no command may be lost.
    while (!pending) wakeup.wait(lock, false, [&] { return pending; });
    lock.unlock();
    sender.join();
    lock.lock();
    assert(pending);
    pending = false;
    wakeup.wait(lock, true, [&] { return pending; });
    assert(!pending); // Running waits expire even without input.
    return 0;
}
