/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_LATENCY_TRACE_H
#define BLUMACH_PORTABLE_LATENCY_TRACE_H

#include <QDebug>
#include <QtGlobal>
#include <atomic>
#include <chrono>
#include <cstdint>

// Opt-in host diagnostics. Never records key values, paths or guest contents.
namespace LatencyTrace {
inline bool enabled()
{
    static const bool value = qEnvironmentVariableIsSet("BLUMACH_TRACE_LATENCY");
    return value;
}
inline uint64_t now()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now()
                                      .time_since_epoch()).count());
}
inline uint64_t nextId()
{
    static std::atomic<uint64_t> serial { 0U };
    return enabled() ? ++serial : 0U;
}
inline void event(const char *stage, uint64_t id, uint64_t value = 0U)
{
    if (enabled())
        qInfo("bm-latency %s id=%llu us=%llu value=%llu", stage,
              static_cast<unsigned long long>(id),
              static_cast<unsigned long long>(now()),
              static_cast<unsigned long long>(value));
}
}
#endif
