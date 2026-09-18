# PCS 86 Spanish original-media corrections (2026-09-18)

Worktree: BluMach-portable-gui-shell; branch: fix/pcs86-spanish-diagnostics.
Base: f59a8e577, the local preview of PR121 plus the PR122 XTA narrowing fix.
Unfinished writable-media frontend changes are excluded from this change.
All probes use the preserved Spanish 720 KiB originals, read-only, with the
original BIOS 1.09 EPROM pair, 640 KiB conventional RAM, 1920 KiB EMS,
one floppy, no HDD and no fitted coprocessor. The unidentified 1.44 MiB image
is excluded. No firmware, executable media or local captures are distributed.

## Corrections and evidence

- **EMS relocation (software-derived):** original MEM_DIA computes frame
  segment N000h from its load footprint, selector port N400h, and port-6Bh
  bit N-3 from a table for N=4 through 9. The observed run chooses 40000h,
  4400h and bit 1. The BIOS uses 80000h, 8400h and bit 5 and explicitly clears
  that gate afterwards. The previous inherited/portable implementation only
  mapped four selectors at 8400h and ignored the gate. The portable model now
  supplies six separately gated four-window banks over 40000h-9FFFFh, sharing
  the configured EMS backing pages. Clearing a gate or selecting an invalid
  page reveals the unchanged conventional RAM. This is an inference from
  original software, not a schematic or physical bus measurement. Remaining
  control bits in 64h/6Fh/70h and the 6Ch protection sequence are not certified.
- **Absent-device composition:** inherited machine_common_init installs XT
  page ports 80h-87h, not the AT high-channel pages or another device in
  88h-9Fh. These absent locations now read FFh and ignore writes. They are not
  page aliases or invented storage. Other unmapped ports remain errors.
- **Page-latch readback:** src/dma.c retains the full page_l byte while masking
  the effective XT address page. The portable component previously masked
  both. Readback now preserves all eight bits without widening DMA addresses.
- **Absent 8087:** the board supplies ready to the V30 POLL contract, matching
  the nonblocking WAIT path in the inherited CPU. No FPO callback is installed:
  no floating-point result is manufactured. A CPU with an unspecified POLL
  callback remains unsupported; a future fitted 8087 needs real busy wiring.
- **PVGA1A graphics:** logical 256-colour width respects Paradise PR4 bit 0
  (the inherited paradise_recalctimings high/low-resolution selection) and
  sequencer clock division. CRTC maximum scan line and double-scan both count
  repeated output lines rather than extra VRAM rows. This removes the tutorial's
  doubled horizontal image and unused lower half, without a BIOS-mode case.

## Reproduction and actual results

Headless physical-key schedule: Enter at 60M and 62M ticks for date/time,
`cd customer` at 65M, `set LANG=S` at 70M, module `/a` at 80M;
4,000 ticks per key transition. Ticks are the pilot's instruction-based
scheduler units, not V30 clock cycles. Captures were inspected outside Git.

| Probe | Before | After |
| --- | --- | --- |
| MEM_DIA /A | -5 at 0E06:0A1A, OUT 4400h | Returns to A>; at 200M type conditional ERRORLEVEL checks; at 210M only MEM_OK appears, status 0, 302 reads, CRC 063B2626 |
| MAIN_DIA /A | -5 at 0E06:0ED8, OUT 88h | At 210M status 0, 297 reads, CRC B9F53106; guest still displays **Error del Controlador DMA** |
| CPR_DIA /A | -8 at 0EF0:004F, WAIT/POLL | At 200M status 0, 319 reads, CRC E367CB30; guest displays its coprocessor error (none fitted), not a passing 8087 test |
| Tutorial boot | 640x400, duplicated image, CRC F9D141BB | At 60M status 0, 605 reads, 320x200, CRC 283BD33B; one intact language selector |

All probes report zero disk writes. Memory returning success does not certify
every RAM/EMS timing or every Customer test. No red BIOS-checksum error was
observed in these runs; that earlier report is not declared resolved here.

## Remaining DMA disagreement

A temporary trace (removed before publication) observes the original board
test write AA,12,44,DD,... to successive ports 87h-96h, then read 87h=AAh
and 88h=FFh rather than its expected 12h. It repeats overlapping runs from
83h, 81h and 82h. Static inspection confirms the consecutive-port loop; this
is not merely a mislabeled exception. The first real page latch now reads back
correctly; the next address is absent in the inherited machine composition.

Do not turn those absent ports into pattern memory to pass the test. The next
step is to establish the actual PCS 86 decode with documentation or the owner's
physical machine, or identify why this diagnostic selects that test. The old
implementation is evidence of software lineage, not proof of hardware behaviour.

## Follow-up: board sequence and checksum

On 2026-09-18, the same read-only configuration was traced through the seven
MAIN_DIA /A dispatch-table entries. A temporary host probe injected F2 at
100,000,000 ticks to acknowledge the DMA error; it did not alter guest memory,
device responses or firmware. At 160,000,000 ticks the guest displayed the
board-test failure summary, with status 0, 159,998,356 retired instructions,
865,771 I/O accesses, 297 floppy reads, zero writes and framebuffer C7CAC508.

| Guest test | Return at the common dispatcher |
| --- | --- |
| CPU | 1 (success) |
| ROM checksum | 1 (success) |
| DMA | 0 (failure) |
| Interrupt controller | 1 (success) |
| Timer | 1 (success) |
| Clock | 1 (success) |
| Speaker routine | 1 (success; no audible-output validation) |

The ROM routine at relocated 0F09:0086 sums 65,536 bytes from F000:0000;
at 0F09:009B it returns AX=0000, which its wrapper converts to success.
The earlier red-checksum report from the excluded unidentified 1.44 MiB media
is not reproduced by these Spanish originals with BIOS 1.09. This is not a
claim that the old diagnostic/media combination has been repaired.

No DMA decode change follows from this result. A bounded documentation search
did not establish the PCS 86 behaviour at 88h-9Fh; other Olivetti models' port
maps cannot justify it. The next decisive comparison is the same Spanish
MAIN_DIA on the owner's physical PCS 86, recording BIOS revision and the DMA
result. If necessary, follow with a small purpose-built register probe under
controlled conditions, not arbitrary writes during a live DOS session.

Temporary tracing and key injection were removed. No live Qt visual test,
hardware measurement or new emulation fix is claimed in this follow-up.

## ROM-free coverage

Synthetic tests cover all six EMS frames with 0/384/1920 KiB, page switching,
shared-page aliases, invalid pages, gate-off conventional RAM, reset, full-byte
DMA readback with masked effective addresses, absent-port reads after writes,
adjacent unmapped ports and absent-coprocessor WAIT. Video tests cover pixels,
CRTC scan repetitions, combined double-scan, PR4 and clock division.

Windows UCRT64 GCC strict build passes, with 65/65 CTest tests passing;
provenance audit reports 19 components, 103 files, zero errors, and its three
unit tests pass. Qt tests run offscreen. Live Qt visual feedback and physical PCS 86 comparison
are not claimed by the headless results. Source remains host-path/Qt independent;
licenses, author notices and component provenance are retained.
