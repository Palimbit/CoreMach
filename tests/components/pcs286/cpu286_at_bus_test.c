/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Synthetic CPU/AT wiring test, not chipset, DMA or timing conformance.
 */
#include <blumach/components/at_bus.h>
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>

typedef struct board {
    bm_cpu_t cpu;
    bm_at_bus_t *bus;
    unsigned int cpu_fetches;
    unsigned int dma_accesses;
    unsigned int debug_accesses;
    unsigned int hold_edges;
    unsigned int ack_edges;
    int request_during_fetch;
    bm_status_t endpoint_status;
    bm_clock_rate_t last_clock;
} board_t;

static void hold_changed(void *context, int asserted)
{
    board_t *board = context;
    ++board->hold_edges;
    assert(board->cpu.ops.signal(board->cpu.context,
                                BM_286_SIGNAL_HOLD, asserted) == BM_STATUS_OK);
}

static void acknowledge_changed(void *context, int asserted)
{
    board_t *board = context;
    ++board->ack_edges;
    assert(bm_at_bus_hold_ack(board->bus, asserted) == BM_STATUS_OK);
}

static bm_status_t decode(void *context, bm_at_transfer_t *transfer)
{
    board_t *board = context;
    board->last_clock = transfer->requester_clock;
    if (transfer->bus.attributes & BM_BUS_TRANSACTION_DEBUG) {
        ++board->debug_accesses;
    } else if (transfer->master == BM_AT_MASTER_CPU) {
        assert(transfer->bus.operation == BM_BUS_FETCH);
        assert(transfer->bus.space == BM_ADDRESS_PROGRAM);
        assert(transfer->bus.address >= 0xfffff0U);
        ++board->cpu_fetches;
        if (board->request_during_fetch) {
            board->request_during_fetch = 0;
            /* A peripheral raises HOLD while the current CPU boundary is
             * in progress. Signalling must not recursively execute the CPU. */
            assert(bm_at_bus_request(board->bus, BM_AT_MASTER_DMA16, 1) ==
                   BM_STATUS_OK);
        }
    } else {
        assert(transfer->master == BM_AT_MASTER_DMA16);
        ++board->dma_accesses;
    }
    transfer->bus.value = 0x90U; /* authored NOP, no firmware */
    transfer->bus.wait_states = 3U; /* synthetic, already requester clocks */
    return board->endpoint_status;
}

int main(void)
{
    bm_host_services_t host = bm_null_host_services();
    board_t board = {0};
    bm_at_bus_config_t bus_config = {0};
    bm_286_config_t cpu_config = {0};
    bm_286_boundary_t boundary = {0};
    bm_286_arch_state_t arch = {0};
    bm_at_transfer_t dma = {0};
    bm_at_transfer_t debug;

    bus_config.cpu_clock = (bm_clock_rate_t) {12000000U, 1U};
    bus_config.isa_clock = (bm_clock_rate_t) {8000000U, 1U};
    bus_config.memory = decode;
    bus_config.io = decode;
    bus_config.decode_context = &board;
    bus_config.hold = hold_changed;
    bus_config.hold_context = &board;
    assert(bm_at_bus_create(&host, &bus_config, &board.bus) == BM_STATUS_OK);
    cpu_config.size = sizeof(cpu_config);
    cpu_config.version = BM_286_CONTRACT_VERSION;
    cpu_config.access = bm_at_bus_cpu_access;
    cpu_config.access_context = board.bus;
    cpu_config.hold_ack = acknowledge_changed;
    cpu_config.pin_context = &board;
    assert(bm_286_create(&host, &cpu_config, &board.cpu) == BM_STATUS_OK);

    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.instruction_address == 0xfffff0U);
    assert(boundary.timing == BM_286_TIMING_UNKNOWN);
    assert(boundary.bus_wait_cycles == 3U);
    assert(board.last_clock.cycles_per_second_numerator == 12000000U);

    dma.master = BM_AT_MASTER_DMA16;
    dma.requester_clock = (bm_clock_rate_t) {4000000U, 1U};
    dma.bus.space = BM_ADDRESS_MEMORY;
    dma.bus.operation = BM_BUS_READ;
    dma.bus.size = 2U;
    dma.bus.endianness = BM_ENDIAN_LITTLE;
    assert(bm_at_bus_set_lock(board.bus, 1) == BM_STATUS_OK);
    assert(bm_at_bus_request(board.bus, BM_AT_MASTER_DMA16, 1) == BM_STATUS_OK);
    assert(board.hold_edges == 0U);
    assert(bm_at_bus_access(board.bus, &dma) == BM_STATUS_IDLE);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(board.cpu_fetches == 2U);

    assert(bm_at_bus_set_lock(board.bus, 0) == BM_STATUS_OK);
    assert(board.hold_edges == 1U && board.ack_edges == 0U);
    assert(bm_at_bus_access(board.bus, &dma) == BM_STATUS_IDLE);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_IDLE);
    assert(boundary.kind == BM_286_BOUNDARY_HOLD);
    assert(board.cpu_fetches == 2U && board.ack_edges == 1U);
    assert(bm_at_bus_access(board.bus, &dma) == BM_STATUS_OK);
    assert(dma.bus.wait_states == 3U && board.dma_accesses == 1U);
    assert(board.last_clock.cycles_per_second_numerator == 4000000U);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_IDLE);
    assert(board.cpu_fetches == 2U && board.ack_edges == 1U);

    debug = dma;
    debug.master = BM_AT_MASTER_CPU;
    debug.bus.attributes = BM_BUS_TRANSACTION_DEBUG;
    debug.bus.wait_states = 0U;
    assert(bm_at_bus_access(board.bus, &debug) == BM_STATUS_OK);
    assert(debug.bus.wait_states == 0U && board.debug_accesses == 1U);
    assert(board.ack_edges == 1U && board.cpu_fetches == 2U);

    /* Cancelling HOLD synchronously lowers the CPU's HLDA via board wiring. */
    assert(bm_at_bus_request(board.bus, BM_AT_MASTER_DMA16, 0) == BM_STATUS_OK);
    assert(board.hold_edges == 2U && board.ack_edges == 2U);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(board.cpu_fetches == 3U);
    assert(bm_286_get_arch_state(&board.cpu, &arch) == BM_STATUS_OK);
    assert(arch.ip == 0xfff3U);

    assert(bm_at_bus_request(board.bus, BM_AT_MASTER_DMA16, 1) == BM_STATUS_OK);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_IDLE);
    bm_at_bus_reset(board.bus);
    assert(board.cpu.ops.reset(board.cpu.context) == BM_STATUS_OK);
    assert(board.hold_edges == 4U && board.ack_edges == 4U);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.instruction_address == 0xfffff0U);
    /* A request raised inside a fetch must wait for the completed boundary. */
    board.request_during_fetch = 1;
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(board.hold_edges == 5U && board.ack_edges == 4U);
    assert(bm_286_get_arch_state(&board.cpu, &arch) == BM_STATUS_OK);
    assert(arch.ip == 0xfff2U);
    dma.bus.wait_states = 0U;
    assert(bm_at_bus_access(board.bus, &dma) == BM_STATUS_IDLE);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_IDLE);
    assert(board.ack_edges == 5U && board.cpu_fetches == 5U);
    assert(bm_at_bus_access(board.bus, &dma) == BM_STATUS_OK);
    assert(bm_at_bus_request(board.bus, BM_AT_MASTER_DMA16, 0) == BM_STATUS_OK);
    assert(board.hold_edges == 6U && board.ack_edges == 6U);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(board.cpu_fetches == 6U);

    /* A real endpoint failure propagates through both adapters unchanged.
     * The failed instruction never advances IP, and a retry cannot touch
     * the endpoint again. Reset is required before resuming the CPU. */
    board.endpoint_status = BM_STATUS_DEVICE_ERROR;
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_DEVICE_ERROR);
    assert(board.cpu_fetches == 7U);
    assert(bm_286_get_arch_state(&board.cpu, &arch) == BM_STATUS_OK);
    assert(arch.ip == 0xfff3U);
    board.endpoint_status = BM_STATUS_OK;
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_INVALID_STATE);
    assert(board.cpu_fetches == 7U);
    assert(board.cpu.ops.reset(board.cpu.context) == BM_STATUS_OK);
    assert(bm_286_step(&board.cpu, &boundary) == BM_STATUS_OK);
    assert(boundary.instruction_address == 0xfffff0U);
    assert(board.cpu_fetches == 8U);
    /* Reset/disconnect pins before destroying callback recipients. */
    bm_at_bus_destroy(board.bus);
    board.cpu.ops.destroy(board.cpu.context);
    return 0;
}
