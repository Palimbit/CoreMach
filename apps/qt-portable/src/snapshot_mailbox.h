/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_SNAPSHOT_MAILBOX_H
#define BLUMACH_PORTABLE_SNAPSHOT_MAILBOX_H

#include "session_worker.h"

#include <deque>
#include <mutex>

class SnapshotMailbox final {
public:
    /* Returns true only when the caller must schedule a consumer wake-up. */
    bool submit(SessionWorker::Snapshot snapshot);
    std::deque<SessionWorker::Snapshot> take();
    void clear();

private:
    static bool replaceableFrame(const SessionWorker::Snapshot &snapshot);

    std::mutex mutex_;
    std::deque<SessionWorker::Snapshot> pending_;
    bool deliveryScheduled_ = false;
};

#endif
