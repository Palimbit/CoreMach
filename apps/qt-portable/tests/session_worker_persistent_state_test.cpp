/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "session_worker.h"

#include <blumach/frontend/frontend.h>
#include <blumach/platforms/null_host.h>

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
#include <chrono>
#include <thread>

template<class Predicate> static void waitFor(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(predicate());
}

int
main()
{
    static uint8_t evenBytes[32768] {};
    static uint8_t oddBytes[32768] {};
    // Authored reset-vector loop; no preserved firmware in this test.
    evenBytes[32760] = 0xeb;
    oddBytes[32760] = 0xfe;
    bm_frontend_asset_binding_t bindings[2] {};
    const bm_frontend_adapter_t *adapter =
        bm_frontend_adapter_find("olivetti-pcs86");
    bm_frontend_machine_t *machine = nullptr;
    bm_host_services_t host = bm_null_host_services();
    SessionWorker::PersistentState rtc;
    rtc.role = "rtc";
    std::vector<SessionWorker::PersistentState> states;
    states.push_back(std::move(rtc));

    bindings[0].role = "firmware-even";
    bindings[0].kind = BM_FRONTEND_ASSET_BLOB;
    bindings[0].value.blob = {
        "test-even", evenBytes, sizeof(evenBytes), nullptr
    };
    bindings[1].role = "firmware-odd";
    bindings[1].kind = BM_FRONTEND_ASSET_BLOB;
    bindings[1].value.blob = {
        "test-odd", oddBytes, sizeof(oddBytes), nullptr
    };

    assert(adapter != nullptr);
    assert(bm_frontend_machine_open(
               adapter, bindings, sizeof(bindings) / sizeof(bindings[0]),
               &machine) == BM_STATUS_OK);
    {
        SessionWorker worker(
            host, bm_frontend_machine_config(machine),
            [](SessionWorker::Snapshot) {}, std::move(states));
        assert(worker.start() == BM_STATUS_OK);
        assert(!worker.unlimited());
        worker.setUnlimited(true);
        waitFor([&] { return worker.unlimited(); });
        worker.pause();
        waitFor([&] { return worker.state() == BM_SESSION_PAUSED; });
        const auto pausedTicks = worker.ticks();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(worker.ticks() == pausedTicks && worker.realTimePercent() < 0.0);
        worker.setUnlimited(false);
        waitFor([&] { return !worker.unlimited(); });
        assert(worker.state() == BM_SESSION_PAUSED);
        worker.resume();
        waitFor([&] { return worker.state() == BM_SESSION_RUNNING; });
        worker.reset();
        worker.shutdown();
        const auto &captured = worker.persistentStates();
        assert(captured.size() == 1U);
        assert(captured[0].role == "rtc");
        assert(captured[0].status == BM_STATUS_OK);
        assert(captured[0].data.size() == 32U);
        assert(captured[0].data[4] == 0x22U);
        assert(captured[0].data[6] == 0x18U);
        assert(captured[0].data[7] == 0x09U);
    }
    bm_frontend_machine_close(machine);
    return 0;
}
