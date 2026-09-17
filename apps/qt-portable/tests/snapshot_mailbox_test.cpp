/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "snapshot_mailbox.h"

#include <cassert>

static SessionWorker::Snapshot
frame(uint64_t ticks, uint32_t color)
{
    SessionWorker::Snapshot snapshot;
    snapshot.ticks = ticks;
    snapshot.frame = QImage(1, 1, QImage::Format_RGB32);
    snapshot.frame.fill(color);
    return snapshot;
}

int
main()
{
    SnapshotMailbox mailbox;
    assert(mailbox.submit(frame(1U, 0xff0000U)));
    assert(!mailbox.submit(frame(2U, 0x00ff00U)));
    auto pending = mailbox.take();
    assert(pending.size() == 1U);
    assert(pending.front().ticks == 2U);
    assert(pending.front().frame.pixel(0, 0) == 0xff00ff00U);

    SessionWorker::Snapshot lifecycle;
    lifecycle.lifecycleResult = true;
    assert(mailbox.submit(frame(3U, 0U)));
    assert(!mailbox.submit(lifecycle));
    assert(!mailbox.submit(frame(4U, 0U)));
    pending = mailbox.take();
    assert(pending.size() == 3U);
    assert(pending[0].ticks == 3U);
    assert(pending[1].lifecycleResult);
    assert(pending[2].ticks == 4U);

    assert(mailbox.submit(frame(5U, 0U)));
    mailbox.clear();
    assert(mailbox.take().empty());
    assert(mailbox.submit(frame(6U, 0U)));
    return 0;
}
