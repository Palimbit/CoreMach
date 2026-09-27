/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * AT dual-8237 functional normal/compressed transfer model.
 */
#ifndef BLUMACH_COMPONENTS_AT_DMA_H
#define BLUMACH_COMPONENTS_AT_DMA_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_at_dma bm_at_dma_t;
/* Pure IBM 5170 Type-1 page-selection wiring (6280070, sheet15, p1-90).
 * Each bit is an asserted DACK, including INTERNAL cascade DACK4; refresh is
 * a semantic assertion (any nonzero value), not the physical active-low pin.
 * Returns the canonical 80h-8Fh register port selected at the LS612 MA inputs.
 * DACK1/5 are not decoder inputs. No DACK selects 8Bh; DACK4 alone selects
 * 83h for BOTH lower-controller mem2mem phases; refresh alone selects 8Fh.
 * This combinational result does not assert AEN, grant a bus, provide a latch
 * value, validate simultaneous inputs or implement refresh/mem2mem service.
 * It is IBM board wiring, not an 8237 rule or verified Headland behavior. */
uint16_t bm_at_dma_ibm_page_port(uint8_t dack_asserted, int refresh_asserted);
/* value carries one device data unit: byte on 0-3, word on 5-7.
 * channel 4 is cascade, never a usable transfer channel. */
typedef bm_status_t (*bm_at_dma_read_fn)(void *context, uint16_t *value);
typedef bm_status_t (*bm_at_dma_write_fn)(void *context, uint16_t value);
typedef struct bm_at_dma_endpoint {
    void *context;
    bm_at_dma_read_fn read;
    bm_at_dma_write_fn write;
    bm_at_line_fn dack;
    bm_at_line_fn terminal_count; /* internally generated TC/EOP pulse only */
} bm_at_dma_endpoint_t;

/* Explicit board opt-in, not a claim about the PCS286's integrated DMA.
 * Zero/default preserves the public mem2mem stop. See the bounded contract
 * below and pcs286-at-dma.md before selecting the IBM matched-count profile. */
typedef enum bm_at_dma_mem2mem_profile {
    BM_AT_DMA_MEM2MEM_DISABLED = 0,
    BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS
} bm_at_dma_mem2mem_profile_t;
typedef struct bm_at_dma_config {
    bm_at_access_fn memory;
    void *memory_context;
    bm_at_line_fn bus_request;
    void *bus_context;
    bm_clock_rate_t clock;
    bm_at_dma_endpoint_t endpoints[8];
    bm_at_dma_mem2mem_profile_t mem2mem_profile;
} bm_at_dma_config_t;
typedef struct bm_at_dma_channel_state {
    uint16_t base_address, current_address, base_count, current_count;
    uint8_t page, mode; /* mode stores bits 7:2; the channel selector is not state */
    int masked, requested, terminal_count;
} bm_at_dma_channel_state_t;

/* Nominal clock intervals, excluding propagation delays and READY waits.
 * These are adapter inputs, not measured pulse widths or live pin states. */
typedef struct bm_at_dma_timing {
    unsigned transfer_clocks; /* S2/S3/S4, or S2/S4 under compression; excludes S1 */
    unsigned read_pulse_clocks; /* IOR or MEMR: 2 normal, 1 compressed, 0 verify */
    unsigned write_pulse_clocks; /* IOW or MEMW: 1 late/compressed, 2 extended, 0 verify */
} bm_at_dma_timing_t;

/* Pure programmed-channel profile, usable from endpoint callbacks and while
 * masked/disabled/idle. Does not select, grant, assert pins or certify routing.
 * UNSUPPORTED for mem2mem, cascade (including channel4, no local transfer
 * timing profile) and illegal type.
 * Output zeroed on any error when supplied. Other state remains untouched.
 * Service adds initial/high-byte S1 and adapter EXTRA waits to transfer_clocks.
 * Pulse intervals exclude Intel AC delay margins; physical READY, strobes and
 * electrical timing remain board-owned. Verify has no data strobes or waits. */
bm_status_t bm_at_dma_channel_timing(const bm_at_dma_t *dma, unsigned int channel,
                                    bm_at_dma_timing_t *out_timing);
typedef struct bm_at_dma_state {
    uint8_t command[2], mask[2], software_request[2], byte_high[2];
    uint8_t dreq; /* semantic external requests; bit 4 stays zero (internal cascade) */
    int bus_request, bus_grant, stopped;
    uint8_t priority_first[2]; /* rotating priority; fixed mode always starts at zero */
    int pending_channel; /* -1 or next winner; inspect immediately before grant */
    int selected_channel; /* -1 or winner latched by grant, stable through service */
    int dack, release_wait;
    uint8_t eop[2]; /* semantic external EOP assertions on lower/upper controller */
    int cascade_active; /* selected external cascade channel has DACK asserted */
    int mem2mem_active; /* lower byte pair owns bus without local DACK */
    int last_pair_read_complete, last_pair_write_complete;
    uint64_t last_pair_completed_clocks; /* successful phases, even on a host stop */
} bm_at_dma_state_t;
/* Construction allocates only instance state; no bus mappings or pin edges.
 * Undocumented power-up address/count/mode/pages start at zero as an explicit
 * deterministic model policy, not a hardware-reset claim. Reset/master clear
 * preserve those registers and page latches, clear command/software/TC/temp/FF,
 * and mask all channels. External DREQ/grant/EOP levels persist across reset.
 * Config/callbacks are copied; contexts must outlive destroy. Callbacks may
 * inspect state and change DREQ/EOP only; no reentrant programming/reset/service/
 * grant/destroy. destroy/reset withdraw DACK/HRQ; reset preserves external grant.
 * memory and a positive rational clock are mandatory. Device read/write is
 * required only for the selected direction; verify invokes neither endpoint.
 * External cascade requires dack; data/TC callbacks are never invoked for it.
 * Channel 4 has no external endpoint, including DACK/TC callbacks. */
bm_status_t bm_at_dma_create(const bm_host_services_t *host,
                             const bm_at_dma_config_t *config,
                             bm_at_dma_t **out_dma);
void bm_at_dma_destroy(bm_at_dma_t *dma);
void bm_at_dma_reset(bm_at_dma_t *dma);
bm_status_t bm_at_dma_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_at_dma_set_dreq(bm_at_dma_t *dma, unsigned int channel, int level);
/* One semantic input per controller: 0 owns channels0-3, 1 owns5-7. An assertion
 * is not a pin-voltage level (physical EOP is active low). Stores the level only;
 * no asynchronous register/counter mutation or pin callback. Sample once after
 * a complete successful unit, before TC/DACK/HRQ callbacks. An assertion held
 * from idle/grant terminates the first unit; a pulse gone before the sample is
 * not queued. No electrical pulse-width/S2/S4 synchronization claim.
 * The cascade-only upper controller does not terminate a lower-controller unit.
 * Board owns routing/wired-OR behavior; no automatic EOP cross-connection.
 * Accepted external EOP sets status TC, clears software request and masks or
 * autoinitializes, without forcing countFFFF or emitting an internal TC pulse.
 * Coincident count terminal emits its one normal internal pulse. Host failure
 * takes precedence over EOP and cannot be converted into successful termination. */
bm_status_t bm_at_dma_set_eop(bm_at_dma_t *dma, unsigned int controller, int asserted);
bm_status_t bm_at_dma_set_bus_grant(bm_at_dma_t *dma, int level);
/* Programming ports: 00-0F, C0-DE even; DMA page latches 87/83/81/82/8B/89/8A.
 * Port mirrors, refresh 8F and spare/checkpoint latches are board-owned.
 * One-byte I/O only; board owns width splitting, decode aliases and I/O waits.
 * DEBUG reads preserve the byte flip-flop/status; DEBUG writes are rejected.
 * Non-DEBUG I/O during grant is invalid host usage, without state mutation.
 * DREQ and HRQ levels are semantic assertions, not physical polarity. Masks
 * suppress external DREQ; Intel's software requests remain non-maskable.
 * Lower HRQ feeds upper channel 4, which must be programmed as cascade.
 * Fixed/rotating arbitration selects upper first, then lower when appropriate.
 * Before granting, the coordinator inspects pending_channel and claims the AT
 * bus as DMA8 (0-3) or DMA16 (5-7) for local transfers. For external cascade,
 * inspect the selected channel mode and claim the external master's board
 * route (e.g. ISA); no DMA page/width substitution. No input changes between that inspection and
 * granting. Grant latches the winner; hold it until HRQ falls. A falling grant
 * during an active burst is INVALID_STATE; reset may explicitly abort it.
 * One service call completes at most one byte/word/verify transfer. Supported:
 * single, demand, block; increment/decrement; TC, autoinit, software block
 * requests. DACK spans a demand/block service; single always releases HRQ.
 * After release, HRQ cannot reassert until grant falls (no stale-HLDA reuse).
 * Normal transfer clocks: S2-S4 plus S1 on first/address-high change, plus
 * memory callback EXTRA DMA-clock waits. Compressed timing omits S3: S2+S4,
 * still S1 on first/address-high change. Extended-write bit is ignored when
 * compressed. Normal extended write lengthens the nominal write pulse from
 * one to two clocks without changing transfer clocks. channel_timing exposes
 * that distinction for adapter wait policy. Cascade-only upper
 * timing bits do not change lower clocks. These durations do not expose
 * electrical strobe edges or READY sampling. The board memory adapter can
 * inspect command state to apply its timing-specific EXTRA wait policy.
 * Callback must include board/device
 * READY duration there; device callbacks add no independent waits. Idle/S0,
 * cascade propagation and electrical synchronization are coordinator-owned.
 * This is documented component duration accounting, not measured PCS286 timing.
 * Host errors retain partial endpoint effects, do not commit address/count/TC,
 * withdraw DACK/HRQ and stop until public reset. Error cycles report zero
 * completed transfer clocks, not elapsed electrical time. Endpoint IDLE under
 * grant becomes INVALID_STATE, forbidding replay. Guest master clear cannot
 * evade the stop. Unsupported modes/controls fail before DACK or data access:
 * mem2mem outside the explicit profile below, invalid type outside cascade, unsafe cascade ordering,
 * software requests outside block. External EOP uses the bounded contract above.
 * See doc/architecture/pcs286-at-dma.md for scope and acceptance evidence. */
/* External cascade (channels0-3/5-7): service asserts DACK after the outer bus
 * grant and returns IDLE/zero local transfer clocks. Repeated service is idle
 * without new edges; cascade_active identifies the delegated grant. The board
 * coordinator must advance the external master separately, never from a DACK
 * callback or by spinning zero-cycle local service. External DREQ withdrawal
 * releases DACK/HRQ, rotates priority and waits for outer grant to fall.
 * Type/autoinit/address-direction/timing bits do not transfer data; no memory,
 * device data, address/count/page/mask/TC mutation, internal TC pulse or EOP
 * termination occurs. External master owns its addresses, clocks, READY and
 * failures; coordinator handles error stops and teardown without guest faults
 * or replay. This component does not receive that master's access statuses.
 * Intel p19 requires cascade starting at local channel0: if any local1-3 is
 * cascade while local0 is not, service stops UNSUPPORTED before effects.
 * Software cascade requests and mem2mem remain unsupported. Missing DACK is a
 * host stop; channel4 stays internal-only. No physical DREQ/HLDA timing claim. */
bm_status_t bm_at_dma_service(bm_at_dma_t *dma, uint64_t *consumed_cycles);
/* Optional IBM_MATCHED_COUNTS functional profile:
 * lower channel0 software block request, channel0 READ/channel1 WRITE modes,
 * equal current counts and equal autoinit selection (and equal base counts
 * when autoinit is enabled); upper channel4 cascade.
 * Shared port83h page, one byte pair/service, eight clocks plus both waits.
 * Source hold, independent +/- addresses, modulo64K, fixed/rotating priority.
 * Compressed command is don't-care; extended write remains unsupported.
 * Both counters decrement after a successful pair. Matched terminal or EOP
 * seen in BOTH phases sets both status bits and clears both software bits;
 * both mask or both reload base registers. Internal TC pulses endpoint1 only.
 * Source TC/status/mask is the literal per-channel Intel functional reading,
 * not independently verified silicon behavior. No local DACK/data callbacks.
 * TEMP captures a successful source read even if the write fails. Address/
 * count/status commit requires both accesses OK. Error cycles=0 completed
 * PAIRS; last_pair_* preserves completed phases and their clocks. No replay.
 * EOP different between phases stops UNSUPPORTED AFTER the two accesses;
 * preserve partial effects/TEMP, no architectural completion/TC. This is an
 * evidence stop, not a hardware EOP behavior. Mismatched counts/autoinit,
 * nonstandard modes, upper mem2mem, no software0 request and extended timing
 * stop BEFORE accesses. Normal default configs never enable this profile.
 * State/diagnostics distinguish owned bus from DACK; falling active grant is
 * invalid, terminal release requires fresh HLDA. Last-pair diagnostics persist
 * until the next pair or public reset; guest master clear cannot evade a stop.
 * Headland must not opt in based solely on IBM wiring or these synthetic tests. */
bm_status_t bm_at_dma_channel_state(const bm_at_dma_t *dma, unsigned int channel,
                                     bm_at_dma_channel_state_t *out_state);
bm_status_t bm_at_dma_state(const bm_at_dma_t *dma, bm_at_dma_state_t *out_state);
#ifdef __cplusplus
}
#endif
#endif
