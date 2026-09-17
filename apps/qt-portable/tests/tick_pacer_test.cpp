/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tick_pacer.h"

#include <cassert>
#include <chrono>

int
main()
{
    using namespace std::chrono_literals;
    const TickPacer::Clock::time_point start {};
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
    return 0;
}
