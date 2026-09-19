/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "session_worker.h"

#include <blumach/frontend/frontend.h>
#include <blumach/platforms/null_host.h>

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

int
main()
{
    static uint8_t evenBytes[32768] {};
    static uint8_t oddBytes[32768] {};
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
