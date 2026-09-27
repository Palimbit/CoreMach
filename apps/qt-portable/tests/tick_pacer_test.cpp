/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tick_pacer.h"

#include <cassert>
#include <chrono>

int
main()
{
    using namespace std::chrono_literals;
    const TickPacer::Clock::time_point start {};
    SimulationSpeedMeter speed(1000);
    assert(speed.sample(start, 0) < 0);
    assert(speed.sample(start + 500ms, 500) == 100.0);
    assert(speed.sample(start + 1000ms, 750) == 50.0);
    assert(speed.sample(start + 1500ms, 1750) == 200.0);
    assert(speed.sample(start + 2000ms, 0) < 0); // Guest reset.
    speed.reset(); // Pause/resume or speed-mode change starts a fresh sample.
    assert(speed.sample(start + 10000ms, 0) < 0);
    assert(speed.sample(start + 10500ms, 500) == 100.0);
    SimulationSpeedMeter noRate(0);
    assert(noRate.sample(start, 0) < 0);
    assert(noRate.sample(start + 1s, 1000) < 0);
    FramePacer frames;
    frames.reset(start);
    assert(frames.due(start));
    frames.presented(start);
    assert(!frames.due(start + 15ms));
    assert(frames.due(start + 18ms));
    frames.presented(start + 18ms);
    assert(frames.due(start + 32ms)); // Not delayed to 34 ms.
    frames.presented(start + 1000ms);
    assert(!frames.due(start + 1000ms)); // No catch-up burst.
    assert(frames.due(start + 1008ms));
    frames.reset(start);
    unsigned presented = 0;
    for (unsigned ms = 0; ms < 960; ms += 6) {
        if (frames.due(start + std::chrono::milliseconds(ms))) {
            ++presented; frames.presented(start + std::chrono::milliseconds(ms));
        }
    }
    assert(presented == 60); // 62.5 Hz despite 6 ms polling, no accumulated drift.
    TickPacer pacer(UINT64_C(10000000), UINT64_C(250000));

    pacer.reset(start);
    assert(pacer.ticksDue(start) == 0U);
    assert(pacer.ticksDue(start + 10ms) == 100000U);
    assert(pacer.ticksDue(start + 10ms) == 0U);
    assert(pacer.ticksDue(start + 20ms) == 100000U);

    pacer.reset(start);
    pacer.account(20000U);
    assert(pacer.ticksDue(start + 3ms) == 10000U);
    assert(pacer.ticksDue(start + 3ms) == 0U);

    pacer.reset(start);
    assert(pacer.ticksDue(start + 100ms) == 250000U);
    assert(pacer.ticksDue(start + 100ms) == 0U);
    assert(pacer.ticksDue(start + 101ms) == 10000U);

    pacer.reset(start + 1s);
    assert(pacer.ticksDue(start + 500ms) == 0U);
    assert(pacer.ticksDue(start + 1010ms) == 100000U);

    TickPacer pcs86Pacer(UINT64_C(2000000), UINT64_C(10000));
    pcs86Pacer.reset(start);
    assert(pcs86Pacer.ticksDue(start + 5ms) == 10000U);
    assert(pcs86Pacer.ticksDue(start + 10ms) == 10000U);
    assert(!pcs86Pacer.needsCatchUp(start + 10ms));
    assert(!pcs86Pacer.needsCatchUp(start + 10500us));
    assert(pcs86Pacer.needsCatchUp(start + 11ms));
    assert(pcs86Pacer.needsCatchUp(start + 11ms)); // Query consumes nothing.
    assert(pcs86Pacer.ticksDue(start + 11ms) == 2000U);
    assert(!pcs86Pacer.needsCatchUp(start + 11ms));
    pcs86Pacer.account(4000U); // Input advanced the guest ahead of wall time.
    assert(!pcs86Pacer.needsCatchUp(start + 12ms));
    assert(pcs86Pacer.needsCatchUp(start + 14ms));
    pcs86Pacer.reset(start + 20ms); // Pause/resume and mode switches.
    assert(!pcs86Pacer.needsCatchUp(start + 19ms));
    assert(!pcs86Pacer.needsCatchUp(start + 20ms));
    TickPacer disabled(0, 0);
    disabled.reset(start);
    assert(!disabled.needsCatchUp(start + 1s));

    // A host requiring 5 ms per 5 ms slice must not add another 1 ms sleep.
    TickPacer loaded(1000000, 5000);
    loaded.reset(start);
    for (unsigned ms = 5; ms <= 500; ms += 5) {
        assert(loaded.ticksDue(start + std::chrono::milliseconds(ms)) == 5000);
        assert(loaded.needsCatchUp(start + std::chrono::milliseconds(ms + 5)));
    }
    return 0;
}
