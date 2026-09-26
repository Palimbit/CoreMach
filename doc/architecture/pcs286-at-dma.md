# AT DMA: functional transfers, cascade and bounded block copies

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status (2026-09-25): **registers, requests and normal functional byte/word
transfers, late/extended/compressed nominal timing, external EOP and external
cascade arbitration implemented**, plus an explicitly selected IBM matched-count
memory-copy profile, under the functional sampling contract below. DMA is ready
for Headland composition with its recorded exclusions; other timing/transfer
modes remain unsupported. The previous register-only checkpoint is recorded in the canonical
worklog; its blanket service gate is superseded by the bounded profile below.
`blumach_at_dma` is a separate, instance-owned dual
8237 component. It does not change the existing XT DMA component or enable a
PCS 286 machine. The public functional 286 profile remains as documented in
[the CPU contract](pcs286-protected-public.md); its strict clock gate remains
closed.

## Evidence and provenance

The programming and transfer paths are a derived rewrite of
`components/pc/src/dma8237.c` at
`4769e40524bc194747b142f3e7ec908ae4df0897`. Sarah Walker, Miran Grca,
Fred N. van Kempen and BluMach notices are retained. Tests are authored
synthetic cases. No external emulator code is incorporated.

Primary references:

- Intel **8237A High Performance Programmable DMA Controller**, 231466-005,
  September 1993, pp. 2–11, p. 14 note 3, figure 11 on p. 16 and figure 14 on p. 18: reset, cascade, priority,
  transfer modes, address/count/TC, software requests, masks and programming.
  Figure 11 was visually checked for S1/S2/S3/S4 and held DACK during a burst.
  Figure 14 and the p. 8 command diagram were visually checked for compressed
  S2/S4 duration and the extended-write bit's don't-care qualification.
  Figure 11's dashed extended-write trace and the AC pulse-width note were
  checked for the distinction between write-pulse length and transfer duration.
  [Manufacturer datasheet hosted by Open Watcom](https://openwatcom.org/ftp/devel/docs/231466.pdf).
- IBM **Personal Computer AT Technical Reference**, 6280070, September 1985,
  printed pp. 1-9–1-11 (PDF pages 25–27): dual-controller wiring, port table,
  page latches, byte/word channels and channel 4 cascade.
  [IBM manual scan](https://www.minuszerodegrees.net/manuals/IBM_5170_Technical_Reference_6280070_SEP85.pdf).

Intel's register diagram and IBM's port/word-address tables were visually
checked. These are generic component/AT development references, **not evidence
that the PCS 286 Headland implementation is identical in every respect**.
Preserved scans and hashes are in the canonical pcs286 development record,
marked local-only; no scan or extract belongs in this repository.

## Implemented boundary

| Area | Current behavior |
|---|---|
| Controller I/O | Byte ports 00–0F and C0–DE, even addresses on the upper controller |
| Page I/O | Channels 0/1/2/3/5/6/7 at 87/83/81/82/8B/89/8A; all eight latch bits readable |
| Address/count | Independent base/current words per channel; writes update both, reads return current |
| Byte selection | One shared low/high flip-flop per controller, toggled by address/count reads and writes; clear command resets it |
| Programming | Command, mode (stored bits 7:2), single/all masks, software request set/reset, master clear, status and temporary read |
| Requests | Raw external/software requests appear in status independently of masks and controller disable; lower eligible HRQ feeds upper channel 4 |
| Output | Upper eligibility raises semantic HRQ; repeated levels do not duplicate callback edges; release waits for falling grant before a new request |
| Inspection | DEBUG reads and state accessors preserve flip-flop/status; DEBUG writes are unsupported |
| Lifecycle | Allocate without mappings or pin edges; copied callbacks/configuration; isolated instances; release HRQ on destruction |

The Intel software-request register is explicitly non-maskable. Channel masks
suppress external DREQ; command disable suppresses eligibility from both
sources. Intel requires block mode for software-request service; other software-request
modes stop before DACK. Upper channel 4 must be cascade and cannot receive a
software request. Storing an unsupported mode never certifies its behavior.

Intel p. 19 requires cascades to start at local channel 0. If any local channel
1–3 is programmed for cascade while local 0 is not, service stops before effects.
This explicitly rejects a configuration Intel documents as unsafe; it does not
invent its malfunction, silently repair programming or raise a guest exception.

Intel reset/master clear resets command, software requests, TC status,
temporary register and byte flip-flop, and sets all masks. It does not specify
reset values for address/count/mode. Those registers, external input levels
and page latches are preserved here. Initial construction fills unspecified
state with zero as a deterministic **model policy**, not a silicon claim.

DREQ, HRQ and grant are semantic assertions, not physical pin voltages. The
command polarity bits are retained, but no electrical inversion or timing is
claimed. Channel 4 is internal cascade and cannot receive external DREQ.
Port mirrors remain board-owned. The PCS286 board composition now supplies
independent readable latches at 80h,84h-86h,88h and8Ch-8Fh; they do not feed
DMA addresses. BIOS1.42 has been observed writing/reading rotating patterns
across80h-8Eh before both controllers pass. Physical glue ownership and any
refresh use of8Fh remain unqualified. Upper page
bit 0 is retained in its latch and excluded from the generated word address.
Byte address is `(page << 16) | address`; word address is
`((page & 0xFE) << 16) | (address << 1)`. The 16-bit address wraps without
carrying into the page; word counts count words, not bytes.

Guest I/O while grant is asserted returns host `INVALID_STATE` before
mutation; DEBUG inspection still works. Invalid width, operation, address or
attributes cannot advance a flip-flop or change a transaction. Successful
register I/O returns zero component waits; board decode owns I/O wait policy.
Write-only register reads and unowned ports return `UNMAPPED`.

## Transfer, grant and failure contract

One service call completes at most one byte (channels 0–3), word (5–7) or
verify operation. Intel write means device-to-memory, read means memory-to-device.
Verify advances address/count without invoking either data endpoint. Normal
transfers leave the memory-to-memory temporary register unchanged.

Fixed priority chooses 0,1,2,3 within each controller; upper channel 4 arbitrates
lower requests. Rotating priority makes the last serviced channel lowest at the
end of that service, separately on both levels. The winner is latched when grant
rises and remains selected throughout its service; later requests cannot preempt
it. The coordinator reads `pending_channel`, claims the AT bus as DMA8 or DMA16,
and grants without intervening input changes. External cascade instead uses the
external master's board route, as described below. No callbacks recursively advance
the bus/CPU. CPU LOCK and completed-boundary HOLD/HLDA sequencing remain owned by
the AT coordinator; the integration test uses the real `bm_at_bus` component.

Single releases HRQ and DACK after each transfer. Demand retains them while DREQ
is asserted and resumes with current registers after a pause. Block ignores
DREQ withdrawal after the first DACK and continues until TC or accepted EOP. Both keep DACK
asserted throughout the burst. Every completed service waits for grant to fall
before HRQ can reassert; stale HLDA cannot acknowledge a new transfer. A falling
grant during an active burst is invalid host usage. Explicit reset/destruction
withdraws DACK/HRQ; reset preserves external grant, DREQ and EOP levels.

Address increments/decrements modulo 65536. TC occurs when count zero decrements
to FFFF, after all endpoints succeed. Status TC sets and the software request
clears. Without autoinit the channel masks; with autoinit its base address/count
reload and its mask stays unchanged. TC callbacks pulse only after that commit,
while DACK is still asserted. DEBUG status reads preserve TC; guest status reads
clear it. The whole programmed FFFF block performs 65536 transfers.

Callbacks run synchronously, source before destination. Device and memory
control strobes overlap in hardware; this callback ordering is a **functional
model policy**, not a silicon bus trace. Callbacks may inspect state and change
DREQ/EOP, including at DACK or during an access; other reentrant mutations are
outside the contract. Before the first DACK, missing direction callbacks or
unsupported controls stop with no endpoint effects.

Any data endpoint failure retains all external effects already performed,
including a consumed device unit or one written byte of a split word. Current
address/count and TC do not commit, DACK/HRQ withdraw, and the host stop prevents
replay until public reset. Guest master clear cannot recover it. Endpoint IDLE
under grant is reported as INVALID_STATE, never retryable idle after a partial
operation. Error cycle output is zero **completed transfer** clocks, not a claim
of zero elapsed electrical time. Completed earlier transfers remain committed.

## External EOP: functional sampling contract

`bm_at_dma_set_eop(dma, controller, asserted)` stores an external input for
controller 0 (channels 0–3) or 1 (5–7). Assertion is semantic; the actual pin is
active low. This setter does not change registers, generate callbacks or queue
an event. Construction starts deasserted. Reset and guest master clear preserve
external levels; invalid controller/level arguments change nothing.

After all data endpoints of a unit return OK, the component samples the selected
controller's EOP level, before TC/DACK/HRQ callbacks. A held assertion terminates
that service, including single and verify. Address/count first advance normally;
status TC sets and software request clears. Without autoinit the current count
is the remaining count, **not forced to FFFF**, and the channel masks. Autoinit
reloads base address/count and preserves the mask. Service releases DACK/HRQ and
requires falling grant before another request can take ownership.

The status TC bit records either source of termination, as Intel pp. 3/9 specify.
The endpoint `terminal_count` callback represents the **internally generated**
pulse. External EOP alone does not emit it. If count also reaches terminal on
the same unit, exactly one normal pulse occurs. Board glue owns external line
routing and wired-OR behavior; the component does not synthesize feedback.

Held EOP from idle or grant terminates the first successfully completed unit.
A pulse deasserted before the sample is not queued. An input asserted between
burst calls terminates the next completed unit. DACK-low/TC callbacks occur
after sampling and cannot retroactively alter that decision. DEBUG inspection
does not sample EOP or clear status. A host error takes precedence over any
held EOP: retain partial effects, no count/status/autoinit commit, no internal
pulse, reset-only stop. The external level still persists for inspection/reset.

EOP on an inactive controller does not affect the active one. The upper cascade
controller does not terminate or modify channel 4 on lower-controller EOP.
This functional isolation follows Intel's cascade-only role and is corroborated
by the later [82C37A manufacturer datasheet, FN2967 Rev 4, pp. 8/13](https://www.renesas.com/en/document/dst/82c37a-datasheet).
That CMOS derivative explicitly describes ignoring EOP at the cascade controller
and also an active-state latch acted on at S2. **Those electrical latch/timing
rules are not implemented or asserted for this Intel 8237 model.** Its API works
at completed-unit boundaries, not clock edges; pulses shorter than that boundary
are outside the physical fidelity claim. No change of the claimed PCS286 chip
identity or proof of its EOP wiring follows from this comparative reference.

## Duration accounting

Normal successful transfers report S2+S3+S4, plus S1 on the first transfer of a
service or when address bits 8–15 change, plus the memory adapter's extra waits
already converted into the configured DMA clock. Both widths use the 8237 word
address for this high-byte decision. Addition widens the uint32 wait count into
uint64 before combining it with the three/four normal states.

With command bit 3 set, successful transfers omit S3 and report S2+S4, plus
the same S1 refresh and adapter waits. This applies to byte, word and verify
transfers in single/demand/block service. A fresh grant after a single transfer
or demand pause pays S1 again. Carry/borrow tests use the controller's 16-bit
address even on a word channel, including wrap without carrying into the page.
Compressed timing does not change data, priority, masks, TC, autoinit, EOP
sampling or partial-failure behavior. Command bit 5 is retained but ignored
when bit 3 is set, as specified by Intel's command diagram.

With normal timing and command bit 5 set, **extended write is supported at the
nominal interval level**. It advances write assertion by one clock and doubles
the nominal write interval, without adding a transfer state. The read interval,
S1 refresh, transfer clock count, data ordering and commit rules do not change.
This applies to both IOW (memory-to-device) and MEMW (device-to-memory).

`bm_at_dma_channel_timing` exposes a pure programmed-channel profile for adapters:

| Programmed timing | Transfer clocks, excluding S1/waits | Nominal read interval | Nominal write interval |
|---|---:|---:|---:|
| Normal, late write | 3 | 2 | 1 |
| Normal, extended write | 3 | 2 | 2 |
| Compressed, either write bit | 2 | 1 | 1 |
| Verify | 3 or 2 | 0 | 0 |

Intervals are in controller clocks. They deliberately exclude Intel's AC
propagation-delay margins and READY waits, so they are **not guaranteed physical
pulse widths**. The accessor works while idle, masked, disabled or stopped and
inside callbacks; it does not grant a transfer, advance time or change any
register, status, byte pointer or output line. Unsupported mem2mem/cascade/illegal
type returns `UNSUPPORTED`; invalid arguments return `INVALID_ARGUMENT`, with
the supplied timing output zeroed on errors. Its result describes the channel's
programming, not request eligibility or upper-controller routing. Service uses
the same base duration, adding S1 and adapter waits only on a successful unit.

The cascade-only upper controller generates no memory/I/O strobes of its own;
its compressed/extended bits cannot change lower-controller duration or reject
an otherwise supported lower transfer. Memory-to-memory remains a pre-effect
stop under the default configuration; the explicit matched-count profile below
has its own eight-state duration and does not use this pulse-profile accessor.

The board adapter must include device READY extension in its returned extra
waits; device callbacks add no independent wait count. Verify ignores READY
and calls neither endpoint. An adapter needing different waits for late,
extended or compressed timing can inspect the nominal channel timing; command
programming is excluded while granted. There is no new electrical READY or
write-strobe interface and no assumption that a given board supports the shorter
cycle. A synthetic adapter test uses different minimum memory read/write windows
to verify that extended write can remove a wait without inventing a longer base
transfer. This is a test policy, not a PCS286 memory-controller specification.
Idle, S0 waiting for grant, cascade propagation,
programming recovery and electrical clock synchronization are coordinator-owned.
This models documented **component transfer duration**, not complete physical
PCS286 timing. The public CPU strict-clock gate is unchanged.

## External cascade: delegated ownership

Any usable channel 0–3/5–7 may arbitrate an external master in cascade mode,
subject to the channel-0 ordering rule above. Channel 4 remains the internal
lower-to-upper connection. Intel pp. 5–6 describes DREQ/DACK as request/grant
for the downstream controller: the selected cascade channel generates no data
addresses or memory/I/O strobes and ignores READY. Its type bits are don't-care.

After outer grant, `bm_at_dma_service` asserts the selected endpoint's DACK and
returns **IDLE with zero completed local transfer clocks**. The new state field
`cascade_active` identifies that delegated grant. This result does not mean the
CPU has regained ownership. The coordinator must advance the external master
separately, not spin the DMA service or execute a device from a DACK callback.
Repeated service calls leave DACK held without duplicate edges or data accesses.
Missing DACK notification is a host configuration stop before any effects.

Address/base/count/page/mask/TC remain unchanged, even for a zero count or
autoinit. No data or terminal-count callbacks run. EOP is retained as an input
but does not end a cascade grant or fabricate TC. This follows the cascade-only
role; the previously cited later 82C37A reference corroborates EOP isolation,
without supplying Intel electrical timing. Direction, autoinit and timing bits
do not convert cascade into a local transfer. The timing accessor continues to
return `UNSUPPORTED` because cascade has no local transfer-pulse profile.

External DREQ withdrawal releases DACK/HRQ, rotates service priority when
enabled, and requires falling outer grant before a new service. Withdrawal
before DACK produces no phantom grant; withdrawal inside its callback is handled
after notification. Reset/destruction withdraw an active grant once. A later,
higher-priority request cannot preempt an active cascade. Software cascade and
memory-to-memory controls remain rejected before effects. These are semantic
boundary rules, not a simulation of Intel's DREQ setup/hold or its documented
asynchronous-input failure behavior.

The coordinator inspects the pending channel's mode before claiming the AT bus.
The integration fixture claims `BM_AT_MASTER_ISA` for a synthetic external actor;
its address, width, clock and memory waits come from that actor, not the cascade
channel's page/count/address registers. This is an example board route, not an
implemented ISA card or evidence of a particular PCS286 slot. CPU and other DMA
master accesses remain excluded under external ownership.

The DMA receives no access results from that separately advanced actor. Child
host failures belong to the actor/coordinator: preserve partial external effects,
stop that actor, and explicitly withdraw ownership without retry, DMA TC or guest
exception conversion. The tests demonstrate teardown after before/after errors,
including an invalid child IDLE under a promised grant; they do not claim that
the DMA automatically detects an error it cannot observe. Propagation latency,
physical clock synchronization and a full board scheduler remain pending.

## IBM page selection without a local DACK

The preserved IBM 5170 Technical Reference 6280070, Type 1 planar sheet 15
(printed 1-90, PDF page 106), explicitly wires U124 LS612 through U120/U110:
MA3 receives active-low DACK4, MA2 is NAND of active-low DACK0 and REFRESH,
MA1 is AND of active-low DACK2/6, and MA0 is AND of active-low DACK3/7.
Sheet 14 (1-89) supplies the cascaded controllers. DACK1/5 do not enter this
decoder. These are documented connections; the selections below are logical
inferences from that circuit, not measured hardware observations.

`bm_at_dma_ibm_page_port` exposes this pure combinational mapping using semantic
assertions. Normal lower transfers include the internal upper DACK4; the seven
normal routes retain ports 87/83/81/82/8B/89/8A. The transfer engine now derives
its page latch from this mapping, retaining existing address, word shift, waits,
failure handling and callbacks. It does not emit an external channel-4 callback.

Intel 231466-005 p. 6 specifies no DACK on the controller executing mem2mem.
For a lower-controller pair granted through upper cascade, DACK4 alone therefore
selects **83h for both source and destination**. It does not select 87h for the
source. With all DACKs inactive the decoder selects 8Bh; REFRESH alone selects
8Fh. These latter values are only register selections: they do not establish
bus ownership, memory width, address drive, an upper mem2mem implementation or
a refresh controller. The helper supplies no latch values or AEN transitions.
Simultaneous inputs have their combinational result, without legitimizing an
otherwise invalid board cycle. Refresh/spare I/O latches remain board-owned.

This resolves the Type 1 IBM page-selection question only. The filename of the
preserved Headland reference is insufficient evidence of identical internal
wiring; its GC101/GC102 block diagram does not specify this truth table.
Headland routing and unrestricted Intel mem2mem subcycles remain pending.
The default mem2mem gate stays closed; the opt-in profile below uses the IBM
page inference explicitly. No restricted schematic is copied here.

## Private memory-pair transport

`components/pc/src/at_dma_pair.h` is a private preparation/stepping contract,
not installed API and not an alternative public service entry point. Intel
231466-005 pp. 4/6 and figure 12 (p. 17) establish the source read into TEMP,
then the destination write, with S11-S14 and S21-S24. The new primitive executes
one phase per call. Preparation supplies two offsets in the same byte page,
the memory adapter, requester clock and optional borrowed EOP level. For a
future lower IBM copy that page must be the value of port 83h. No port reads,
DMA register changes, grants or endpoint notifications happen in preparation.

After a successful read, the private plan holds the captured byte and returns
to its caller before any write. The write uses that byte even if source memory
changes or aliases the destination. Each successful phase accounts for four
nominal clocks plus its independent adapter waits, widened before addition.
This is the documented eight-state pair split into functional transactions,
not strobe edges or a new compressed/extended-write timing profile.

EOP is sampled separately after each successful memory callback. A caller can
inspect or abandon the pending write; the transport does not decide whether
EOP terminates a controller or causes autoinit. A pulse absent at both samples
is not queued. These completion samples are an explicit functional policy;
figure 12's setup/hold windows are not emulated. Counts, TC, mask, software
requests, source hold, address stepping and autoinit belong to the future
controller integration and are intentionally absent from this transport.

The phase is busy before a callback, rejecting recursive steps. Host failures
consume the plan, retain successful prefix/TEMP and external partial effects,
and return zero completed clocks for the failed phase. The plan's cumulative
clocks retain the completed source phase. A failed write may have changed memory
without being a completed destination phase. IDLE under the caller's promised
grant becomes INVALID_STATE; neither failure nor completion allows replay.
Preparing another object does not authorize host-error recovery. Callback and
EOP storage lifetime, ownership through the pair and register publication remain
the caller's responsibility. No host error becomes a guest exception or TC.

The default `bm_at_dma_service` mem2mem gate is unchanged. In particular, this
does not resolve source-counter underflow status/masking, per-channel autoinit
or controller termination when EOP is seen only in the source phase. Intel's
general count-decrement description and matched-count requirement constrain
the eventual rule, but do not justify importing a later CMOS exception wholesale.
Headland page routing and physical READY/EOP behavior remain separate questions.

## Opt-in IBM matched-count block copies

The functional DMA prerequisite for beginning Headland work is closed **with
the exclusions below**, not as a complete silicon implementation. Default
configuration still selects `BM_AT_DMA_MEM2MEM_DISABLED`. A board or synthetic
fixture must explicitly choose `BM_AT_DMA_MEM2MEM_IBM_MATCHED_COUNTS`; this
does not authorize a PCS286 board to assume IBM wiring or Intel source-TC
behavior. Existing ordinary DMA behavior is unchanged when the profile is set.

Supported preparation is lower channel0 software-requested Block READ and
channel1 Block WRITE, equal current counts, and matching autoinit enables.
Autoinit also requires equal base counts, including after earlier ordinary DMA
has advanced one current count. The upper controller must remain a valid
internal cascade. Channel1 software requests, other mode types, upper mem2mem
and extended-write command are rejected before accesses. Compressed selection
is don't-care for the eight-state pair. Polarity remains semantic.

Each service performs one read/TEMP/write pair through DMA8 with the shared
port83h page. Source hold and independently selected address increment/decrement
are supported, with modulo64K wrap and no page carry. Both current counters
decrement after a successful pair; destination zero-to-FFFF ends the block.
On matched terminal, or EOP observed in both phases, both status bits set and
both software request bits clear. Without autoinit both masks set; with it both
base address/count pairs reload and masks retain their prior values. Rearm
requires a new software request and falling old grant. Only destination TC
emits a terminal-count pulse (endpoint1); no local DACK or device-data callback
runs. Upper-controller external EOP does not terminate the lower pair.

**Evidence boundary:** Intel pp4/6 and figure12 document the pair; p7 supplies
per-channel decrement and p9 per-channel TC/EOP status rules; p6 requires
matched counts to autoinitialize both channels and external EOP in both bus
cycles. Applying the general status/mask rules to source TC in this restricted
case is an explicit functional interpretation, not an independently established
silicon fact. It is not the later CMOS82C37A's source-TC/EOP exception. The
profile stays opt-in for that reason as well as its IBM page routing. Unequal
counts/autoinit and source-only TC are not silently approximated.

EOP completion snapshots must agree. If they differ, service returns
`UNSUPPORTED` **after both memory accesses**, preserving their effects and
TEMP, without committing address/count/status or reporting a completed pair.
That is an evidence stop, not a model of the chip's response to that pulse.
Physical EOP sampling and asymmetric termination remain outside this profile.

`mem2mem_active` exposes held ownership without DACK. A block cannot be
preempted or surrender an active grant between pairs. On release, rotating
priority follows the last destination channel1 (lower priority starts at2),
and rotating upper cascade advances past its channel0. Reset/destruction
withdraw HRQ without inventing DACK edges. Public reset clears pair diagnostics;
guest master clear cannot evade a stopped operation.

TEMP publishes after the source callback succeeds and survives a destination
error. Other register updates wait for both accesses and accepted EOP snapshots.
Host errors stop without replay, TC or guest exception conversion. Service error
cycles count zero **completed pairs**; `last_pair_read_complete`,
`last_pair_write_complete` and `last_pair_completed_clocks` preserve the actual
successful prefix. A failed write may still have changed memory. Completion
flags are independent from architectural pair commit, including a two-access
asymmetric-EOP stop. Callers must inspect the status, not just those flags.

## Validation

The original programming tests remain: 1,048,576 address/count word cases,
12,288 request/mask/disable/cascade combinations, all page/mode/command bytes,
full unmapped port space, pure DEBUG, invalid I/O and lifecycle checks. The
previous blanket service-gate test now programs an illegal transfer type and
still verifies stop/no effects; valid transfers have their own positive suite.

The register suite adds 72 IBM decoder cases: no DACK, internal DACK4 alone,
all seven normal routes, superposed refresh and independence from DACK1/5;
36 further calls check nonzero refresh normalization. The existing exhaustive
transfer/page/wrap/word-width and failure suites run through the new decoder.

`pcs286-component.at-dma-pair` tests the private transport with 327,680 address/
data cases, covering every 16-bit offset, all page and byte values, same-address
aliases, boundary destinations, changed source memory between phases and
recursive-step rejection. It adds 64 EOP/wait combinations (including maximum
uint32 waits), 224 before/after read/write failures with aliases and EOP, invalid
arguments, and real AT LOCK/HLDA tests for success and ownership loss before
each phase. No case is claimed as a completed public mem2mem controller service.

`pcs286-component.at-dma-mem2mem` tests the opt-in controller profile:

- 196,608 address/count cases: every16-bit count with and without autoinit,
  every offset, all page values, both address directions, source hold, TEMP,
  status read/DEBUG and mask preservation. Port87h intentionally differs.
- 64 complete blocks, including sixteen65,536-byte blocks with sequential
  overlap or fill, wrapping, both directions, exact independent wait totals
  and explicit software rearm after autoinit.
- 144 EOP/command cases: coherent early EOP, asymmetric evidence stops,
  coincident TC, compressed don't-care, disable/extended gates and maximum waits.
- 112 before/after phase failures following a completed prefix; retained data,
  counters and TEMP, diagnostic clocks, no replay or guest-clear recovery.
- Pre-effect profile/mode/count/autoinit/upper-controller gates, ordinary
  verify under opt-in, unequal base counts after ordinary DMA, non-preemption,
  two-level rotation, stale HLDA, reset/destruction and real AT LOCK/HLDA/waits.

`pcs286-component.at-dma-transfers` adds:

- 263,936 address/direction/width/page cases, modulo wrap without page carry,
  word-page bit 0, values, maximum uint32 waits and fresh-grant single release.
- 262,144 count/TC/autoinit/width cases, status read/DEBUG, plus a complete
  65,536-transfer block with exact S1/S2/S3/S4 accounting.
- Demand/single/block DREQ withdrawal at DACK and data callbacks, resumed
  service, autoinit, two-level rotation, non-preemption and software block.
- 280 before/after endpoint failures, including invalid grant/IDLE, with
  retained completed prefix and exact side effects; 36 further split-word
  failures assert retained individual bytes and no false count/TC update.
- Real AT LOCK/HOLD/HLDA ownership, byte/word bus masters, native wait forwarding,
  active reset/destruction, instance isolation and missing callbacks.

The same transfer suite additionally covers external EOP:

- 262,144 full counter/width/autoinit cases, status versus internal pulse,
  software clearing, retained remainder and coincident true terminal count.
- 1,008 channel/mode/direction/callback/level-pulse cases, controller isolation,
  held-from-idle input and changes from late DACK-low/TC callbacks.
- 184 before/after ordinary and split-word errors with EOP held, including count
  zero, preserving the completed prefix and exact endpoint effects; reset and
  guest master clear cannot manufacture successful termination or replay.
- Four real AT bus early-termination/opposite-width handoffs with native waits,
  old HLDA rejection and held EOP isolated from the next controller.

Compressed timing adds, in the same suite:

- 262,144 complete address/direction/width cases, checking first and subsequent
  cycles, latch carry/borrow, wrapping, pages, data, autoinit and maximum waits;
  four complete 65,536-transfer blocks with independent duration totals.
- 2,268 channel/mode/type/autoinit/TC/EOP/DREQ/wait cases, resumed demand/single
  grants, masked software block requests and upper cascade timing isolation.
- 920 before/after endpoint failures, including split words and IDLE, preserving
  completed prefixes and partial effects with held EOP and coincident count zero.
- Four real AT LOCK/HOLD/HLDA blocks across both widths with maximum waits and
  stale-grant rejection; all 1,792 channel/command-byte combinations check
  disable, semantic polarity, compressed don't-care bits and unsupported gates.

Extended write additionally reruns the existing normal data/address/page/count,
autoinit, priority, lifecycle, failure and EOP oracles with bit 5 set. Expected
cycles stay unchanged: 263,936 address/page cases, 262,144 count cases plus a
complete 65,536-unit block, 280 ordinary and 36 split-word failures, 262,144 EOP
count cases, 1,008 EOP boundary cases, 184 EOP failures and four real AT handoffs.
All 1,792 command/channel cases now accept normal extended write while retaining
the mem2mem gate. A further 131,072 channel/command/mode profiles check nominal
intervals and pure inspection, including masked/disabled and unsupported states;
112 direct/real-AT blocks test profile-driven adapter waits across every usable
channel, direction and timing selection. Host-stop inspection cannot replay data.

`pcs286-component.at-dma-cascade` adds 28,672 channel/mode/command cases with
unchanged registers and no data/TC effects; 35 withdrawal/reset/destruction
boundaries; priority, non-preemption and fresh-grant checks; unsafe-order,
software/mem2mem and missing-callback gates. Another 84 real AT blocks use an
external master with independent addresses and clock, checking LOCK/HLDA,
CPU/DMA exclusion, wait forwarding and retained before/after failure effects.

GCC 16.2 UCRT64 Debug/Release, assertions and Werror, engine on and legacy/portable
Qt off: each suite registers 132 tests, 131 pass and the existing Headland test
skips. Python tools: 50 tests. Provenance: 42 components/236 files, zero errors.
These are synthetic functional results, not firmware or hardware captures.

## Remaining work

| Area | Status at this handoff | Consequence for Headland |
|---|---|---|
| Registers, byte/word/verify transfers, TC/autoinit, functional EOP, priorities and cascade | Implemented and regression-tested | Available for composition through the existing bus adapter |
| Normal/extended/compressed nominal durations and IBM page decoder | Implemented with documented scope | Board supplies clocks, waits and verified page/port wiring |
| Matched-count IBM mem2mem profile | Implemented/tested, explicit opt-in and functional source-TC interpretation | Keep disabled until chipset evidence supports selecting it |
| Unequal counts/autoinit, source-only TC, asymmetric EOP, nonstandard/upper mem2mem and extended mem2mem timing | Explicit unsupported boundaries; not hardware conclusions | Separate evidence work, not a reason to postpone ordinary Headland decode |
| Physical READY/EOP/polarity edges, propagation and oscillator synchronization | Not implemented/certified | Resolve with chipset/board timing, not before its wiring is known |

Next work is documented Headland/IOC02 registers, decode and board connections,
using the default mem2mem gate. The existing Headland acceptance test still skips;
this handoff is permission to develop that component, not a claim it passes.
No firmware shortcut, permanent alias or restricted asset is used. No bootable
portable PCS286 or complete Intel/Headland silicon fidelity is claimed.
