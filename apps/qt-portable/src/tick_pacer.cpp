/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tick_pacer.h"

#include <algorithm>
#include <limits>

namespace {
constexpr uint64_t nanosecondsPerSecond = UINT64_C(1000000000);
}

TickPacer::TickPacer(uint64_t ticksPerSecond, uint64_t maximumChunk)
    : ticksPerSecond_(ticksPerSecond), maximumChunk_(maximumChunk)
{
}

void
TickPacer::reset(Clock::time_point now)
{
    origin_ = now;
    emitted_ = 0U;
}

void
TickPacer::account(uint64_t ticks)
{
    emitted_ = ticks > std::numeric_limits<uint64_t>::max() - emitted_ ?
        std::numeric_limits<uint64_t>::max() : emitted_ + ticks;
}

uint64_t
TickPacer::targetTicks(std::chrono::nanoseconds elapsed) const
{
    const uint64_t nanoseconds = elapsed.count() > 0 ?
        static_cast<uint64_t>(elapsed.count()) : 0U;
    const uint64_t seconds = nanoseconds / nanosecondsPerSecond;
    const uint64_t remainder = nanoseconds % nanosecondsPerSecond;
    if ((ticksPerSecond_ != 0U) &&
        (seconds > std::numeric_limits<uint64_t>::max() / ticksPerSecond_))
        return std::numeric_limits<uint64_t>::max();
    const uint64_t whole = seconds * ticksPerSecond_;
    const uint64_t fractionWhole =
        remainder * (ticksPerSecond_ / nanosecondsPerSecond);
    const uint64_t fractionRemainder =
        remainder * (ticksPerSecond_ % nanosecondsPerSecond) /
        nanosecondsPerSecond;
    if (fractionRemainder >
        std::numeric_limits<uint64_t>::max() - fractionWhole)
        return std::numeric_limits<uint64_t>::max();
    const uint64_t fraction = fractionWhole + fractionRemainder;
    return fraction > std::numeric_limits<uint64_t>::max() - whole ?
           std::numeric_limits<uint64_t>::max() : whole + fraction;
}

uint64_t
TickPacer::ticksDue(Clock::time_point now)
{
    if ((ticksPerSecond_ == 0U) || (maximumChunk_ == 0U) || (now <= origin_))
        return 0U;
    const uint64_t target = targetTicks(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - origin_));
    if (target <= emitted_)
        return 0U;
    const uint64_t lag = target - emitted_;
    if ((maximumChunk_ <= std::numeric_limits<uint64_t>::max() / 4U) &&
        (lag >= maximumChunk_ * 4U))
        emitted_ = target - maximumChunk_;
    const uint64_t due = std::min(target - emitted_, maximumChunk_);
    emitted_ += due;
    return due;
}
