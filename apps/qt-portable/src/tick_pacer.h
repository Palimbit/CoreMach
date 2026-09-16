/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_TICK_PACER_H
#define BLUMACH_PORTABLE_TICK_PACER_H

#include <chrono>
#include <cstdint>

class TickPacer final {
public:
    using Clock = std::chrono::steady_clock;

    TickPacer(uint64_t ticksPerSecond, uint64_t maximumChunk);
    void reset(Clock::time_point now);
    uint64_t ticksDue(Clock::time_point now);

private:
    uint64_t targetTicks(std::chrono::nanoseconds elapsed) const;

    uint64_t ticksPerSecond_;
    uint64_t maximumChunk_;
    uint64_t emitted_ = 0U;
    Clock::time_point origin_ {};
};

#endif
