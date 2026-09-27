/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_TICK_PACER_H
#define BLUMACH_PORTABLE_TICK_PACER_H

#include <chrono>
#include <cstdint>

// Host presentation only: retain phase, skip missed frames, never burst-replay.
class FramePacer final {
public:
    using Clock = std::chrono::steady_clock;
    void reset(Clock::time_point now) { next_ = now; }
    bool due(Clock::time_point now) const { return now >= next_; }
    void presented(Clock::time_point now) {
        if (now >= next_) next_ += period_ * ((now - next_) / period_ + 1);
    }
private:
    static constexpr auto period_ = std::chrono::milliseconds(16);
    Clock::time_point next_ {};
};

// Rolling guest/wall ratio; pause, reset and mode changes start a fresh sample.
class SimulationSpeedMeter final {
public:
    using Clock = std::chrono::steady_clock;
    explicit SimulationSpeedMeter(uint64_t rate) : rate_(rate) {}
    void reset() { valid_ = false; percent_ = -1.0; }
    double sample(Clock::time_point now, uint64_t ticks) {
        if (!valid_ || ticks < ticks_ || now < time_) {
            valid_ = true; time_ = now; ticks_ = ticks; percent_ = -1.0;
        } else if (now - time_ >= std::chrono::milliseconds(500)) {
            const double seconds = std::chrono::duration<double>(now - time_).count();
            percent_ = rate_ ? (static_cast<double>(ticks - ticks_) /
                                static_cast<double>(rate_)) / seconds * 100.0 : -1.0;
            time_ = now; ticks_ = ticks;
        }
        return percent_;
    }
private:
    uint64_t rate_, ticks_ = 0;
    Clock::time_point time_ {};
    bool valid_ = false;
    double percent_ = -1.0;
};

class TickPacer final {
public:
    using Clock = std::chrono::steady_clock;

    TickPacer(uint64_t ticksPerSecond, uint64_t maximumChunk);
    void reset(Clock::time_point now);
    void account(uint64_t ticks);
    uint64_t ticksDue(Clock::time_point now);
    // Read-only: do not consume a slice merely to decide whether to sleep.
    bool needsCatchUp(Clock::time_point now) const;

private:
    uint64_t targetTicks(std::chrono::nanoseconds elapsed) const;

    uint64_t ticksPerSecond_;
    uint64_t maximumChunk_;
    uint64_t emitted_ = 0U;
    Clock::time_point origin_ {};
};

#endif
