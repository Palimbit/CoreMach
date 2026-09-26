# PCS286 private protected ENTER (D6)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local uncommitted continuation of D5. Public PE gates remain closed; no new
remote CI, OS, hardware timing or portable PCS286 acceptance claim.

## Source and behavior

Intel [80286/80287 PRM 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
ENTER B-40/B-41 (PDF248/249), LEAVE B-64 (PDF272), and protected stack/access
contracts. The original remains outside Git; no firmware/media/external code.

Documented: nesting is the immediate byte modulo 32. ENTER first pushes old BP
and remembers that SP as the new frame pointer. For a nonzero level it copies
level-1 words from decreasing old BP addresses, then pushes the remembered frame
pointer. Finally it installs BP and subtracts the immediate local-byte allocation
from SP. A protected stack-limit violation during the operation causes #SS(0).

D6 permits C8 only through the private decoder. The shared handler preflights the
complete frame plus local reservation with the protected SS range checker. It
also preflights each display word at its 16-bit BP-derived offset. This includes
local bytes without reading or writing them. A reservation crossing the segment
boundary is rejected; SP=0 can reserve up to the whole normal 64KiB segment when
the complete interval fits. Expand-down bounds use the same cached checker.
The source display offsets retain 16-bit arithmetic and individual word checks.

After preflight the existing push/read order is retained: old BP write, display
read then corresponding write, and final frame-pointer write for nonzero levels.
Overlapping frames therefore observe earlier writes. Bulk-copying the original
display before all pushes would give a different result and is not used.

BP/SP commit only after all transfers succeed. Completed endpoint writes survive
host failure; the original status is returned, architectural registers do not
partially commit and the instance stops without replay. Callback signal latches
survive the final register commit. Guest faults use first-prefix IP and bounded
same-CPL delivery. Whole preflight, bus splitting and endpoint ordering are
functional policy, not a claim about physical 286 bus timing or simultaneous
fault precedence. Real-mode ENTER behavior remains unchanged.

## Observed synthetic tests

`pcs286-component.protected-enter` adds 18,432 ENTER/LEAVE cases: every immediate
nesting byte (including modulo-32 aliases), four CPLs, two alignments, allocations
0/1/31, and disjoint plus two overlapping BP/SP relationships. An independent
sequential stack model checks the entire RAM image, untouched locals and complete
architecture, then LEAVE must restore the original frame and stack pointers.

Boundary tests include normal/expand-down reservations, complete 64KiB normal
reservation, display-source and local-allocation faults, more-than-64KiB frame
plus allocation, frame wrap and BP-source offset wrapping. Fault cases check
#SS(0), prefix-inclusive return IP, preserved BP and no pre-delivery writes.
Two instruction streams execute #SS -> MOV SS -> IRET -> ENTER retry with no
fixture state edits during execution. Two callback-NMI cases verify retention.

6,210 injected failures cover every fetch/read/write before and after its endpoint
effect, five host statuses, both alignments, eight successful/faulting/overlapping
routes including maximum nesting. Exact RAM effects, unchanged architecture,
lock cleanup and stopped-instance no-replay are checked.

GCC16.2 UCRT64 Debug/Release: 116 ordinary CTests pass and two existing Headland/
AT DMA skips per build (118 registered), asserts active and warnings as errors.
This retains the prior real-mode and protected subset suites. No chip vectors.

## Pending public activation

Remaining ALU/RMW and operand consumers (including LDS/LES), strings/REP and
INS/OUTS, LOCK, software events and joint exception/restart audit remain next.
D3 query evidence gaps, E/F privilege/task transfers, ED=1/FFFF, REP errata,
faulting-IRET NMI, consecutive inhibition and timing remain open. Public PE,
full protected programs, 80287 and portable PCS286 boot are not enabled.
