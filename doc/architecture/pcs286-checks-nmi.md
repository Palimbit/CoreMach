# Functional AT error capture and NMI

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_pcs286_checks` supplies the private port61 check-enable/status endpoints
and a fallible NMI level output. It composes the existing port61 adapter, refresh
producer and portable 286 signal/interrupt implementation. It does not implement
the RTC, parity-bit RAM storage, a full board factory or physical gate timing.

## Evidence and reuse

The primary generic AT reference is IBM *Personal Computer AT Technical
Reference*, 6280070, September 1985, printed 1-38 through 1-40 and Type 1 sheets
3, 10 and 17 (printed 1-78, 1-85 and 1-92; PDF94/101/108). The schematics were
visually checked, not inferred from OCR. The following are functional
interpretations of their stable signal relationships:

- Sheet17's inverted port61 bits2/3 enable the RAM/I/O check paths.
- Sheet10 captures the qualified memory parity result, retains the error with
  feedback, and clears it when RAM checking is disabled.
- Sheet3 captures the external active-low I/O-check signal and combines the
  error requests with the global NMI mask. Disabling clears stored I/O state,
  but does not deassert a fault still driven by the adapter.
- Port70 bit7=1 blocks NMI; bit7=0 permits it. The manual's wording "mask on/off"
  is easy to misread, so the API names the semantic **masked** level explicitly.
  Masking the output does not reset either error source.

Sheet3's I/O latch has separate set/clear inputs. The helper retains their
software-relevant stable result: while external /IOCHCK stays asserted, bit6
remains high even if checking is disabled, while that disabled source cannot
drive NMI. On enabling with the external fault still active, it is latched
again. Analog races on simultaneous release are not modeled or qualified.

The more legible GC101/GC102 publication (`headland-ht101a`) was visually checked
on PDF6/7: /IOCHCK reports an external device error through enabled NMI;
/MDPCKN reports a parity failure and MDPCKE enables checking. PDF8 describes the
NMI output. It supports these semantic inputs, but does not prove every IBM
discrete latch detail or exact PCS286/GC103 equivalence. Qualification remains
explicitly separate from this generic AT functional component.

The immutable local-only sources remain outside Git:

- `ibm-at-dma-technical-reference-1985`, SHA-256
  `04e83b4df038ddc9d2c45f7465ff19c1ed5628ae9826d0e3066aadd2c0740276`.
- `headland-ht101a`, SHA-256
  `c3e9f6e5d92ffe797df4ff74fe773ee167faa68b63cbca62b92f202415c0f1d0`.

Classic `src/port_6x.c` and `src/nvr_at.c` at
`4769e40524bc194747b142f3e7ec908ae4df0897` were consulted. The former has no
functional error producer and its high status bits remain clear; the latter
confirms the 70h mask-bit integration point with an internally inverted global.
The new code implements the missing capture/routing only; the already attributed
port adapter, memory store, CPU NMI edge latch and IRET remain unchanged.
No external code or restricted asset was copied into the worktree.

## Observable behavior

| Input/control | Functional result |
|---|---|
| Bad parity on a qualified RAM-read sample | Latches bit7 when RAM checking is enabled. |
| Subsequent good read | Does not erase the earlier error. |
| Disable RAM checking (61h bit2=1) | Clears the parity latch and ignores new samples until enabled. |
| Assert external I/O error | Bit6 high; stored while I/O checking is enabled. |
| Remove external I/O error | Stored error survives until checking is disabled. |
| Disable I/O checking (61h bit3=1) | Clears storage, suppresses its NMI contribution; an actively driven fault remains visible. |
| Assert global mask | NMI low; source state and readback remain. |
| Remove global mask with an error retained | NMI high; CPU may capture a new rising edge. |
| Read status, including DEBUG | Pure observation; no clearing, pulses or time advancement. |

NMI is the OR of the eligible errors gated by the global mask. Changed levels
alone are published: repeated error reports or enabling writes cannot invent
new pulses, and clearing one source leaves NMI high if the other still requests
it. The existing CPU owns edge capture, vector2 delivery, inhibition and IRET.
Masking after an edge has reached the CPU cannot retract that pending edge.

`memory_sample()` accepts a guest parity observation after a qualified successful
RAM read. It must not be called for DEBUG, refresh, non-RAM accesses, or host
endpoint failures. The backing memory still has no separate parity-bit storage;
the final memory-controller adapter must identify and supply those observations
when a guest parity model requires them. No host error is translated into NMI.
`io_input()` accepts semantic asserted=1 for physical /IOCHCK low. The device
owns deassertion; a board clear does not "repair" it.

`status()` returns bits6/7 for the board aggregator to combine with refresh bit4.
`enable()` plugs into the existing port61 callback; `mask()` is the semantic
input for the future RTC address-port bit7. There is no production 70h handler
that pretends to accept CMOS accesses before the RTC exists.

## Ownership and failure handling

Caller-owned storage copies a mandatory NMI callback and borrows its context.
Initialization allocates and publishes nothing; the consumer starts low. The
global mask starts asserted. Check enables start true to match the existing
zero port61 latch after model reset. This is the component's deterministic reset
policy, not a certification of the complete power-on/POST sequence described by
IBM or a measured PCS286 startup state.

Full board reset restores enables/mask, clears the RAM error and preserves an
externally active I/O fault. It lowers NMI and forces output resynchronization
after a prior publication failure. CPU-only reset preserves board state; the
owner must separately re-present the current NMI level to the reset CPU, whose
input latch has reset. Disconnect while callback recipients are still alive.

The callback may signal the CPU but must not execute it recursively. Mutator
reentry is rejected and pure inspection is allowed. On publication failure,
accepted input/latch/output state remains, the first host failure is retained,
and further mutations stop until explicit reset. There is no rollback or retry.
An IDLE response from the output is an invalid host state, not successful signal
delivery. Pure state/status inspection remains available after failure.

## Validation and pending work

`pcs286-component.checks-nmi` checks 32 enable/mask/error combinations, separate
sources, held I/O faults, 1,000 repeated disabled-input/read cycles without
extra NMI edges, reset, two-instance isolation, invalid arguments and reentry.
Failure injection covers both rising/falling publication, before/after endpoint
effects, retained state, no retry, reset recovery and invalid IDLE responses.

An authored integration uses real RAM/ROM backing, AT bus, 80286, PIT clock,
refresh and port61. Guest code initializes SS/SP and IVT, unmasks through an
real RTC port70 mask output, and halts with IF clear. A qualified
RAM parity observation or external I/O pulse reaches vector2; the handler reads
61h, records the cause combined with actual REF DET, clears/re-enables checks
through OUT and returns with IRET. Mask-after-edge, stack return, warm reset,
DEBUG purity and absence of host-error-to-parity conversion are checked. A real
check-output failure during OUT61 propagates through the port/bus, retains
accepted effects and permits only DEBUG inspection without automatic retry.
These are authored functional inputs, not hardware captures or defective DRAM.

GCC16.2 UCRT64 Debug and Release: 141 tests pass with one public Headland skip,
142 registered. Python:50; provenance:52 components/270 files, zero errors;
catalogue:32 machines/five locales; diff check passes. No MSVC, GUI, ASan,
hardware, BIOS/media or CI validation is claimed for this delivery.

The subsequent [RTC delivery](pcs286-at-rtc.md) replaces the original test-only
70h mask fixture with `blumach_at_rtc`. This CPU roundtrip deliberately keeps
that RTC depleted/stopped; RTC native calendar and IRQ8 behavior have separate
tests. It does not claim an engine RTC clock link or complete board scheduler.

Next: RTC clock attachment, KBC, complete board scheduling and
memory/error-source composition. Physical parity circuitry and analog timing
are not prerequisites; exact PCS286 qualification remains separate. CPU strict
timing stays blocked, Headland mem2mem remains off, public Headland acceptance
still skips, and the portable PCS286 is not yet bootable. All work is local.
