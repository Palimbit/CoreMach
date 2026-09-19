# PCS 86 Customer startup boundary (2026-09-17)

Base: `686595f73`, branch `architecture/pcs86-customer-diagnostic`.
The owner reports responsive DOS/GWBASIC and approximately 58.5 presentation
FPS after the host-wait correction, but Customer stops with status -5.

Read-only headless reproduction uses the canonical `bios-even-109`,
`bios-odd-109`, and `dos-330a-validation` assets. Type `customer` plus Enter
at tick 60,000,000 with 4,000 ticks per keyboard transition.
Before correction, execution stops at tick 63,603,264, CS:IP 33D8:0016,
on `IN AL,71h`, with 621 disk reads and zero writes.

The inherited `src/machine/m_xt_olivetti_pcs86.c` installs the PCS86 MM58167
and no AT CMOS data device at 71h. At this historical cut, the portable
composition explicitly mapped that one absent port to its existing open-bus
handler: FFh reads and ignored writes. Port 70h's existing opaque write latch
and the actual RTC remained unchanged; 72h was deliberately left unmapped.
This followed inherited composition, not a physical measurement.

Synthetic tests cover reads before and after a write and the adjacent unmapped
port; they contain no firmware. Headless error reporting now includes captured
instruction bytes and DX through existing public inspection contracts.

After correction: 100,000,000 ticks complete with status 0, 928 reads, zero
writes, framebuffer CRC32 B230662A (unchanged from the initial dialog).
A second run adds Enter at tick 90,000,000 and completes 120,000,000 ticks,
119,998,685 instructions, 1,441,179 I/O operations and 1,146 reads, zero writes,
CRC32 F3E8790B. This establishes keyboard-driven progress, not completion of
Customer Test or visual identification of every screen.

Windows UCRT64 GCC strict build and 65/65 CTest pass; provenance audit has
zero errors and its three unit tests pass. The original floppy hash remains
C1A3BBD618E86719425B0EFED650BC9FE831A0989DC67B4726886268C9DB1D78.
No media, firmware, screenshots or build artifacts belong in this change.
The already-running GUI is not replaced by the headless reproduction.

The later portable bus-response cut supersedes the one-port implementation:
the PCS 86 board now supplies one general passive-I/O response for every
unclaimed or decoded-but-non-readable cycle. Consequently 71h and 72h both
read `FFh` and discard writes without either port becoming an implemented CMOS
device. The original measurements above remain the record of this dated cut.
