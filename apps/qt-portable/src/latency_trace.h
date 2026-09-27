/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_PORTABLE_LATENCY_TRACE_H
#define BLUMACH_PORTABLE_LATENCY_TRACE_H

#include <QDebug>
#include <QtGlobal>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

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
    if (!enabled()) return;
    // Bounded, opt-in diagnostics. Per-event stderr writes perturb Windows
    // scheduling severely. Stage strings are static literals at every callsite.
    struct Record { const char *stage; uint64_t id, time, value; };
    struct Buffer {
        std::mutex mutex;
        std::vector<Record> records;
        Buffer() { records.reserve(65536); }
        ~Buffer() {
            char chunk[65536];
            size_t used = 0;
            for (const auto &record : records) {
                if (used > sizeof(chunk) - 256) {
                    (void)std::fwrite(chunk, 1, used, stderr); used = 0;
                }
                const int count = std::snprintf(chunk + used, sizeof(chunk) - used,
                    "bm-latency %s id=%llu us=%llu value=%llu\n", record.stage,
                    static_cast<unsigned long long>(record.id),
                    static_cast<unsigned long long>(record.time),
                    static_cast<unsigned long long>(record.value));
                if (count > 0 && static_cast<size_t>(count) < sizeof(chunk) - used)
                    used += static_cast<size_t>(count);
            }
            if (used) (void)std::fwrite(chunk, 1, used, stderr);
            (void)std::fflush(stderr);
        }
    };
    static Buffer buffer;
    const uint64_t timestamp = now();
    std::lock_guard<std::mutex> guard(buffer.mutex);
    if (buffer.records.size() < 65536)
        buffer.records.push_back({stage, id, timestamp, value});
}
}
#endif
