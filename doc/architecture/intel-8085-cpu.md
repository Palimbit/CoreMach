# Portable Intel 8085 CPU

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: functional model of the documented 8085 instruction set and interrupt
system with exact documented T-states per instruction boundary. It is not a
cycle-exact bus model, and it has not been validated against hardware or a
real firmware image.

## Scope and component boundary

`components/cpu/i8085` is a separate component with instance-owned state. Two
models are selectable: `BM_I8085_INTEL_8085A` and `BM_I8085_OKI_MSM80C85A`.
The OKI data book documents no difference from the 8085A in instruction
timing, interrupts or machine cycles, so both profiles share one behaviour
until a difference has its own evidence. The component knows nothing about
any machine; the Olivetti M10 will compose it in a separate system.

The 8080-compatible semantics were adapted from the NEC V30 8080 emulation
mode in `components/cpu/808x`. No code is shared and the 808x component is
unchanged.

## Sources

Downloaded documents are preserved outside Git in the local research library
(`olivetti/_shared`), with SHA-256 and acquisition records:

| Key | Document | Used for |
|---|---|---|
| MCS85 | Intel MCS-80/85 Family User's Manual, January 1983 | §5.6 pp. 5-4..5-18 cycles and states; §2.3.4-2.3.8 pp. 2-13..2-20 INA, Bus Idle, HALT, RESET IN, SID/SOD |
| ASM85 | Intel 8080/8085 Assembly Language Programming Manual 9800301D | Priorities (p. 1-24), conditional instructions skip byte 3 (p. 1-25) |
| OKI86 | OKI 1986 Microprocessor Data Book, MSM80C85A RS/GS | Table 1 interrupt sensitivity; Table 8 clock cycles, pp. 30-32 |
| KS1302 | K. Shirriff, silicon analysis of the 8085 undocumented flags, Feb. 2013 | V, K and bit 3 |

MAME's i8085 source was consulted only as a cross-check. Its own TODO list
flags its V/K behaviour as inaccurate, so it is not a source for those flags.

## Behaviour

- Opcode fetches are `BM_BUS_FETCH`, operands and data `BM_BUS_READ` or
  `BM_BUS_WRITE`; every transfer is one byte. I/O ports 0-255 use
  `BM_ADDRESS_IO`. Endpoint wait states count CLK periods.
- PUSH, CALL, RST and interrupts write the high byte to SP-1 before the low
  byte to SP-2. False conditional jumps and calls do not read the third byte.
- ANA sets AC, XRA/ORA clear it, PSW bit 3 is always zero.
- Interrupts are sampled at instruction boundaries: TRAP > RST 7.5 > RST 6.5
  > RST 5.5 > INTR, vectors 24h, 3Ch, 34h, 2Ch. TRAP needs a latched rising
  edge and a high level; RST 7.5 latches its edge even while masked; RST 6.5,
  RST 5.5 and INTR are levels. EI takes effect after the following
  instruction. INTR reads RST n or CALL through the acknowledge callback.
- RIM and SIM implement masks, the RST 7.5 reset, SID and SOD. The first RIM
  after TRAP reports the enable that existed before it.
- RESET IN clears PC, IE, RST 7.5, TRAP and SOD and sets all masks. While
  asserted, and while READY is low, each boundary consumes one clock.
- A halted CPU returns `BM_STATUS_IDLE`; a clocked engine suspends it until a
  line is asserted.

## Timing

Each boundary reports exact documented T-states and machine cycles. The
clocked step adds bus wait states. Interrupt boundaries use 12 T-states for an
internal RESTART or an INTR RST, 18 for an INTR CALL, plus one THALT clock when
leaving HALT. A test transcribes OKI Table 8 independently and checks every
documented opcode on both condition paths, with and without wait states.

## Undocumented flags V and K

Following KS1302, V is the carry into bit 7 XOR the carry out of bit 7, and K
is V XOR S for ALU operations. AND, OR and XOR give V = 0 and K = S. INR/DCR
set V only for 7Fh->80h and 80h->7Fh. RLC/RAL behave as A + A for V; RRC/RAR
clear V. DAD sets V from 16-bit signed overflow. INX/DCX set K from the
incrementer carry. POP PSW loads both bits.

The source does not establish these cases, which keep the previous value:

| Instruction | Flag left unchanged |
|---|---|
| DAD | K |
| RLC, RRC, RAL, RAR | K |
| INX, DCX | V |
| STC, CMC, CMA | V and K |

The analysis is of Intel NMOS silicon; nothing confirms that the OKI CMOS
part behaves the same.

## Undocumented instructions

DSUB, ARHL, RDEL, LDHI, LDSI, RSTV, SHLX, JNK, LHLX and JK are classified but
not executed. After the opcode fetch the component restores PC and returns
`BM_STATUS_UNSUPPORTED` with no other architectural change. A static scan of
the Olivetti M10 system ROM found none of them in the 69.2 % reachable from
the vectors and three confirmed dispatch tables. The remaining 30.8 % is not
covered, so this is not proof of absence. The scan and its limits are recorded
in the research library, not in Git.

## Known limitations

- Interrupt and pin sampling is per boundary, not per T-state.
- READY low is modelled as whole clocks between instructions, not as TWAIT
  states inside a machine cycle. HOLD/HLDA, ALE, S0/S1 and IO/M are not
  modelled.
- A bus error stops the instruction midway; earlier writes and PC updates
  remain.
- The order of the two XTHL stack writes is not stated in the cited pages.
- RESET IN preserves general registers and flags; on hardware they are
  indeterminate after a reset that occurs mid-instruction.
- V and K are not covered by the exerciser results below, which mask them.

## External exercisers

`portable-engine-i8085-cpm-runner <program.com>` runs CP/M exercisers passed by
path. The programs, their scans and the run logs are kept in the research
library, never in Git. Results at commit a4b72f2e1:

| Program | Result | Meaning |
|---|---|---|
| 8085EXM (flag mask D5h, CRCs from Intel D8085AH-1 silicon) | 25/25 pass | S, Z, AC (including ANA), P and CY match real 8085 hardware |
| 8080EXM (flag mask FFh, 8080 CRCs) | 25/25 differ | Expected: it checks the 8080's fixed PSW bits 1, 3 and 5 |
| SuperSoft CPUTEST | 1 error, INR B flags 00h vs 02h | Expected: 8080 bit 1 is always 1; on the 8085 it is V, which is 0 here |

In 8080EXM, every test family that does not change V or K produces the 8085
silicon CRC from Ian Bartholomew's published table, confirming that the only
difference is PSW bit 1.
