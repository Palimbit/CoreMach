/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/pit8253.h>
#include <blumach/components/pit8253_clock.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>

typedef struct output_trace {
    bm_engine_t *engine;
    bm_pit8253_t *pit;
    bm_time_point_t when[4];
    int level[4];
    size_t count;
    int close_gate_on_high;
} output_trace_t;

static const bm_clock_rate_t pc_pit_rate = {
    UINT64_C(14318180), 12U
};

static uint64_t
greatest_common_divisor(uint64_t left, uint64_t right)
{
    while (right != 0U) {
        uint64_t remainder = left % right;

        left = right;
        right = remainder;
    }
    return left;
}

static void
assert_time_at_pit_edge(const bm_time_point_t *when, uint64_t edge)
{
    const uint64_t denominator = UINT64_C(3579545);
    uint64_t numerator = edge * UINT64_C(3000000000);
    uint64_t remainder = numerator % denominator;
    uint64_t divisor = greatest_common_divisor(remainder, denominator);

    assert(when->nanoseconds == numerator / denominator);
    if (remainder == 0U) {
        assert(when->subnanosecond_numerator == 0U);
        assert(when->subnanosecond_denominator == 1U);
    } else {
        assert(when->subnanosecond_numerator == remainder / divisor);
        assert(when->subnanosecond_denominator == denominator / divisor);
    }
}

static bm_status_t
write_port(bm_bus_t *bus, uint16_t port, uint8_t value)
{
    bm_bus_transaction_t transaction = {
        BM_ADDRESS_IO, BM_BUS_WRITE, port, value, 1U, 1U, 0U,
        BM_ENDIAN_LITTLE, 0U
    };

    return bm_bus_transact(bus, &transaction);
}

static void
capture_output(void *context, unsigned int channel, int output)
{
    output_trace_t *trace = context;

    if (channel != 0U)
        return;
    assert(trace->count < (sizeof(trace->when) / sizeof(trace->when[0])));
    assert(bm_engine_now_exact(trace->engine, &trace->when[trace->count]) ==
           BM_STATUS_OK);
    trace->level[trace->count] = output;
    ++trace->count;
    if (trace->close_gate_on_high && output)
        assert(bm_pit8253_set_gate(trace->pit, 0U, 0) == BM_STATUS_OK);
}

static void
test_real_pit_follows_fractional_clock_domain(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_pit8253_t *pit = NULL;
    output_trace_t trace = { 0 };
    bm_pit8253_config_t pit_config = { 0x0040U, capture_output, &trace };
    bm_timed_source_id_t source_id = UINT32_MAX;
    uint16_t count = 0U;
    int level = 0;

    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    trace.engine = engine;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);
    trace.pit = pit;
    assert(bm_pit8253_attach_clock(engine, pit, &pc_pit_rate, &source_id) ==
           BM_STATUS_OK);
    assert(source_id == 0U);

    assert(write_port(bus, 0x0043U, 0x34U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 0x04U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 0x00U) == BM_STATUS_OK);
    /* Programming mode 2 raises OUT immediately; observe only clocked edges. */
    trace.count = 0U;

    assert(bm_engine_run_for(engine, 4191U) == BM_STATUS_OK);
    assert(trace.count == 2U);
    assert(trace.level[0] == 0);
    assert(trace.level[1] == 1);
    assert_time_at_pit_edge(&trace.when[0], 4U);
    assert_time_at_pit_edge(&trace.when[1], 5U);
    assert(bm_pit8253_count(pit, 0U, &count) == BM_STATUS_OK);
    assert(count == 4U);
    assert(bm_pit8253_output(pit, 0U, &level) == BM_STATUS_OK);
    assert(level == 1);

    bm_engine_destroy(engine);
    bm_pit8253_destroy(pit);
    bm_bus_destroy(bus);
}

static void
test_lazy_progress_and_gate_rearming(void)
{
    static const bm_clock_rate_t one_ghz = { UINT64_C(1000000000), 1U };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_pit8253_t *pit = NULL;
    output_trace_t trace = { 0 };
    bm_pit8253_config_t pit_config = { 0x0040U, capture_output, &trace };
    uint16_t count = 0U;

    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    trace.engine = engine;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);
    trace.pit = pit;
    assert(bm_pit8253_attach_clock(engine, pit, &one_ghz, NULL) ==
           BM_STATUS_OK);

    assert(write_port(bus, 0x0043U, 0x30U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 100U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 0U) == BM_STATUS_OK);
    trace.count = 0U;

    /* No OUT transition is due yet. A direct observation still catches the
     * counter up to the exact engine cursor instead of returning stale state. */
    assert(bm_engine_run_for(engine, 50U) == BM_STATUS_OK);
    assert(trace.count == 0U);
    assert(bm_pit8253_count(pit, 0U, &count) == BM_STATUS_OK);
    assert(count == 51U);

    assert(bm_pit8253_set_gate(pit, 0U, 0) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 100U) == BM_STATUS_OK);
    assert(bm_pit8253_count(pit, 0U, &count) == BM_STATUS_OK);
    assert(count == 51U);

    assert(bm_pit8253_set_gate(pit, 0U, 1) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 52U) == BM_STATUS_OK);
    assert(trace.count == 1U);
    assert(trace.level[0] == 1);
    assert(trace.when[0].nanoseconds == 201U);
    assert(trace.when[0].subnanosecond_numerator == 0U);

    bm_engine_destroy(engine);
    bm_pit8253_destroy(pit);
    bm_bus_destroy(bus);
}

static void
test_large_idle_gap_and_reentrant_output_change(void)
{
    static const bm_clock_rate_t one_ghz = { UINT64_C(1000000000), 1U };
    const uint64_t idle_gap = (uint64_t) UINT32_MAX + 5U;
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_pit8253_t *pit = NULL;
    output_trace_t trace = { 0 };
    bm_pit8253_config_t pit_config = { 0x0040U, capture_output, &trace };

    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    trace.engine = engine;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);
    trace.pit = pit;
    assert(bm_pit8253_attach_clock(engine, pit, &one_ghz, NULL) ==
           BM_STATUS_OK);

    /* Exercise cursor catch-up beyond the 32-bit component advance API. */
    assert(bm_engine_run_for(engine, idle_gap) == BM_STATUS_OK);
    assert(write_port(bus, 0x0043U, 0x30U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 4U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 0U) == BM_STATUS_OK);
    trace.count = 0U;
    trace.close_gate_on_high = 1;

    /* The output callback mutates the PIT while its own timed source fires.
     * This must update the following deadline without self-arm rejection. */
    assert(bm_engine_run_for(engine, 6U) == BM_STATUS_OK);
    assert(trace.count == 1U);
    assert(trace.level[0] == 1);
    assert(trace.when[0].nanoseconds == idle_gap + 5U);

    bm_engine_destroy(engine);
    bm_pit8253_destroy(pit);
    bm_bus_destroy(bus);
}

static void
test_engine_reset_starts_a_new_clock_epoch(void)
{
    static const bm_clock_rate_t one_ghz = { UINT64_C(1000000000), 1U };
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U, 1U };
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_pit8253_t *pit = NULL;
    output_trace_t trace = { 0 };
    bm_pit8253_config_t pit_config = { 0x0040U, capture_output, &trace };

    assert(bm_engine_create_clocked(&host, &engine_config, &engine) ==
           BM_STATUS_OK);
    trace.engine = engine;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);
    trace.pit = pit;
    assert(bm_pit8253_attach_clock(engine, pit, &one_ghz, NULL) ==
           BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 100U) == BM_STATUS_OK);

    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    bm_pit8253_reset(pit);
    assert(write_port(bus, 0x0043U, 0x30U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 4U) == BM_STATUS_OK);
    assert(write_port(bus, 0x0040U, 0U) == BM_STATUS_OK);
    trace.count = 0U;
    assert(bm_engine_run_for(engine, 6U) == BM_STATUS_OK);
    assert(trace.count == 1U);
    assert(trace.level[0] == 1);
    assert(trace.when[0].nanoseconds == 5U);

    bm_engine_destroy(engine);
    bm_pit8253_destroy(pit);
    bm_bus_destroy(bus);
}

static void
test_adapter_rejects_wrong_engine_mode_and_capacity(void)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t config = { 1U, 1U, 0U };
    bm_engine_t *legacy = NULL;
    bm_engine_t *clocked = NULL;
    bm_engine_t *working = NULL;
    bm_bus_t *bus = NULL;
    bm_pit8253_t *pit = NULL;
    bm_pit8253_config_t pit_config = { 0x0040U, NULL, NULL };

    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    assert(bm_pit8253_create(&host, bus, &pit_config, &pit) == BM_STATUS_OK);
    assert(bm_engine_create(&host, &config, &legacy) == BM_STATUS_OK);
    assert(bm_pit8253_attach_clock(legacy, pit, &pc_pit_rate, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_engine_create_clocked(&host, &config, &clocked) == BM_STATUS_OK);
    assert(bm_pit8253_attach_clock(clocked, pit, &pc_pit_rate, NULL) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    assert(bm_pit8253_attach_clock(NULL, pit, &pc_pit_rate, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(bm_pit8253_attach_clock(clocked, NULL, &pc_pit_rate, NULL) ==
           BM_STATUS_INVALID_ARGUMENT);

    bm_engine_destroy(clocked);
    config.max_timed_sources = 1U;
    assert(bm_engine_create_clocked(&host, &config, &working) ==
           BM_STATUS_OK);
    assert(bm_pit8253_attach_clock(working, pit, &pc_pit_rate, NULL) ==
           BM_STATUS_OK);
    assert(bm_pit8253_attach_clock(working, pit, &pc_pit_rate, NULL) ==
           BM_STATUS_INVALID_STATE);
    bm_engine_destroy(working);
    bm_engine_destroy(legacy);
    bm_pit8253_destroy(pit);
    bm_bus_destroy(bus);
}

int
main(void)
{
    test_real_pit_follows_fractional_clock_domain();
    test_lazy_progress_and_gate_rearming();
    test_large_idle_gap_and_reentrant_output_change();
    test_engine_reset_starts_a_new_clock_epoch();
    test_adapter_rejects_wrong_engine_mode_and_capacity();
    return 0;
}
