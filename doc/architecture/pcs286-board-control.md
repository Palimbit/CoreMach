# Functional board A20, reset and interrupt wiring

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_pcs286_control` connects existing 286, AT bus, PIC and private Headland
memory components. The [keyboard pair](pcs286-keyboard-pair.md) supplies its
IRQ1, A20 and semantic reset callbacks. This is private board composition;
the public PCS286 factory and exact GC103/IOC02 wiring remain unqualified.

## Evidence and scope

The existing [8042 contract](pcs286-at-kbc.md) cites IBM 5170 Technical Reference
6280070 (1985), pp.1-42–1-55: output-port bit1 is A20 and bit0 is active-low
reset. [Headland routing](pcs286-headland.md) already distinguishes CPU A20
from DMA/ISA addresses using the GC102 reference p.13, with explicit limits on
its applicability to GC103. No new silicon conclusion or document acquisition
is made here. These sources remain outside Git in the canonical library.

The integration reuses those behaviors. It does not add port92, infer IOC02
output pins, restore a permanent diagnostic alias or invent a BIOS-specific
reset path. Native keyboard/KBC and CPU/chipset implementations are unchanged.
No oscillator, reset pulse width, clock-edge phase or full CPU timing is
certified. The strict CPU-clock gate remains closed; Headland mem2mem stays off.

## Ownership and reset policy

Initialize unused caller storage with initialized, borrowed CPU/bus/PIC/memory.
Creation publishes nothing. Connect bus HOLD, PIC INTR, CPU LOCK/HLDA and the
fallible NMI endpoint; checks/RTC remain the producers of qualified NMI levels.
Supply pair outputs and explicit initial publication before execution. All
callback recipients must outlive CPU destruction; creation order must prevent
callbacks into an uninitialized owner. The owner is single-threaded and is the
exclusive execution/reset path after connection. Direct CPU execution/reset,
state import and component mutation from callbacks violate that ownership.

`control_step` reports either a real CPU boundary or a distinct CPU-reset event.
It is not an engine CPU-clock callback. A pending request at entry resets without
fetch. A request inside a successful synchronous CPU boundary waits for that
boundary to return; the result retains the completed boundary and indicates
that reset followed it. Reentrant step/reset reject. No peripherals advance
implicitly, and the caller continues their engine while CPU reset is held.

Reset assertion latches on a low-to-high semantic transition. Deassertion cannot
erase an unserviced pulse, repeated high does not reset repeatedly, and held reset
prevents further fetch after the first reset. Multiple pulses before service
coalesce into one reset. These are explicit functional boundary policies, not
physical reset sampling/timing certification.

CPU-only reset clears CPU stop/REP, releases its HLDA/LOCK and re-presents actual
PIC INTR, bus HOLD and stored NMI. Presenting a still-high NMI as a fresh input
to a reset CPU is an explicit model policy. External bus requests survive;
they require a fresh CPU HLDA before access. RAM, Headland registers/mappings,
A20, DMA/PIC programming, CMOS, keyboard buffers/timers and engine epoch are not
reset. A user-requested full-machine reset still needs the larger coordinator.

A20 forwards directly to the existing memory adapter, which snapshots routes
for each transfer and rejects A20 mutation while busy. No in-progress transfer
is reinterpreted or retried. IRQ1 forwards to the actual PIC, including the
existing acknowledge protocol; no vector/ACK is fabricated by the owner.

## Failures and observation

The first host failure is retained, including errors from void CPU/bus pin
callbacks. Ordinary invalid API arguments and reentrant step/reset reject
without poisoning state. Accepted peripheral/memory effects survive. A failed
CPU boundary retains any pending reset and stops before automatic recovery;
the original error remains visible on subsequent calls. Host errors never
become guest exceptions. A void-adapter failure can be noticed only after the
current synchronous CPU call returns; its completed effects remain visible.

`control_reset_cpu` is an explicit recovery operation, not an automatic retry.
The caller must separately repair/resynchronize failed peripherals and their
output levels. It cannot clear their sticky failures. A new pin failure during
CPU reset remains latched. State inspection is pure even after failures, and
step leaves the caller's event untouched on IDLE/error.

## Validation and remaining work

`pcs286-component.board-control` composes actual components, maps pair clock I/O
at60h/64h through the existing board decoder and delivers host keys through the
existing clock input API. Its authored byte programs and conformance imports
contain no BIOS or external executable media. Tests cover:

- D1 A20 on/off with distinct low/relocated RAM; CPU versus DMA8/DMA16/ISA,
  DEBUG under external ownership, retained requests and fresh HLDA after reset;
- FE reset inside CPU OUT, no reset inside its trace callback, held reset,
  pulses completed between boundaries, duplicate assertions and release;
- real keyboard make/translation → PIC IRQ1 → 286 INTA/vector31 → IRET;
  INTR and NMI resampling and retained RAM, DMA page, Headland CR0, pair and epoch;
- reset from imported protected/ring3, HLT and shutdown states; LOCK REP reset
  releases arbitration and discards continuation without copying another byte;
- 96 before/after failures at every transfer of an odd-word store, six host
  statuses, independent partial-write expectations, unchanged failed output,
  no replay and explicit recovery; active-memory A20 rejection and void-pin errors;
- invalid arguments, independent instances, reentry and strict-clock refusal.

The fixture uses 1.5MHz logical keyboard/KBC service with input2/output3/
self-test5/pulse6 and keyboard POR3/BAT5/byte2/accept3 cycles. Its explicit
2000ns advancement during an I/O callback is a synthetic event-placement test,
not an inferred instruction duration. CPU timing remains UNKNOWN.

Current local GCC16.2 UCRT64 Debug/Release validation: 148 passed and one public
Headland acceptance skipped (149 registered). Python50, provenance57 components/
292 files, catalogue32 machines/five locales. Initial build fixes supplied the
contract include dependency and corrected a test field name. Two fixture
expectations were corrected: DEBUG bypasses arbitration, and CR0 retains RAM
straps. Existing component rejection/decoding rules were not weakened.
No new MSVC, ASan, GUI, CI, physical capture or firmware result is claimed.

The [peripheral services owner](pcs286-board-services.md) now composes
PIT/RTC/port61/checks/refresh and these control paths, with separate peripheral
time and functional CPU boundaries. Complete machine lifecycle, DMA service,
GC103/IOC02 qualification, storage/audio and portable boot remain pending.
This closes the bounded control-wiring block, not the machine factory or the
public Headland acceptance gate. Work remains local and uncommitted.
