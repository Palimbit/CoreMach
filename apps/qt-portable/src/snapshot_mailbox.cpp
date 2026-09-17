/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "snapshot_mailbox.h"

#include <utility>

bool
SnapshotMailbox::replaceableFrame(const SessionWorker::Snapshot &snapshot)
{
    return !snapshot.frame.isNull() && !snapshot.lifecycleResult &&
           (snapshot.status == BM_STATUS_OK);
}

bool
SnapshotMailbox::submit(SessionWorker::Snapshot snapshot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (replaceableFrame(snapshot) && !pending_.empty() &&
        replaceableFrame(pending_.back()))
        pending_.back() = std::move(snapshot);
    else
        pending_.push_back(std::move(snapshot));
    if (deliveryScheduled_)
        return false;
    deliveryScheduled_ = true;
    return true;
}

std::deque<SessionWorker::Snapshot>
SnapshotMailbox::take()
{
    std::deque<SessionWorker::Snapshot> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.swap(pending_);
    deliveryScheduled_ = false;
    return result;
}

void
SnapshotMailbox::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    deliveryScheduled_ = false;
}
