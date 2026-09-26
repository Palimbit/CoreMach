/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 */
#ifndef BM_EXECUTION_286_H
#define BM_EXECUTION_286_H
#include <blumach/components/cpu_80286.h>

/* PRIVATE test compatibility spelling, never installed or a diagnostic bypass.
 * It now calls public step; cpu.ops.run uses that same functional profile.
 * Accepts reset/real state or a consistent protected context. No full CPU
 * or timing certification. See pcs286-protected-public.md for activation.
 * Shares the real decoder/handlers for MOV, LEA, XLAT, register XCHG and NOP;
 * ordinary IRET, near/direct nonconforming JMP, LLDT, table/MSW and CLTS join
 * the private path, including LMSW entry with retained real caches.
 * D3 adds LAR/LSL/VERR/VERW; D9 resolves type rules under the PRM-1987
 * contract recorded in descriptor_286.h. Rejection is ZF=0, not a fault.
 * D4 adds FLAGS/CLI/STI, CPL0 HLT and scalar IN/OUT with 286 CPL/IOPL checks.
 * D5 adds ordinary PUSH/POP (including segments), PUSHA/POPA, LEAVE, near
 * CALL/RET/JMP and Jcc/LOOP/JCXZ through shared protected access.
 * D6 adds ENTER with whole allocation/display preflight and interleaved
 * reads/writes for overlapping frames. D7 joins LDS/LES, BOUND, scalar ALU,
 * shifts/multiply/divide/decimal, ARPL, SLDT/STR and software INT/INT3/INTO.
 * D8 joins valid string/REP/INS/OUTS paths and reviewed memory LOCK forms.
 * D9 delivers string protection/IOPL and scalar write-protection exceptions,
 * with the documented/explicitly interpreted fault policy described in
 * pcs286-protected-instruction-policy.md. LOCK MOV segment loads borrow their
 * instruction's exclusion window. No guarantee of revision-specific fault
 * microstate or restart for documented nonrestartable operations is implied.
 * E1 adds same/outer-CPL RETF and outer IRET, including SS:SP restoration,
 * old-CPL FLAGS permissions and cached DS/ES cleanup. E2a adds ordinary far
 * CALL/JMP direct/conforming/call-gate forms, inner call stacks from an already
 * loaded consistent TR and parameter copying. E2b adds LTR with locked busy
 * test/set and ordinary inner IDT entry using checked cached TSS stack slots.
 * F adds task save/load, busy/backlink, task gates and current-NT IRET under
 * task_286.h, including faults in a newly selected context. Prefix forms
 * outside the selected contract remain unsupported. D10 audited the D path;
 * pcs286-protected-full-joint.md covers the composed private C/D/E/F profile;
 * public activation is covered separately by pcs286-protected-public.md.
 * Bounded event delivery, HOLD, shadows and TF sampling join the same CPU
 * instance. Ordinary faults restart at the first prefix; task faults use the
 * incoming context. String restart corrections belong to the outgoing image.
 * Host errors retain their
 * status/effects and stop without replay. Timing remains UNKNOWN. Only reset
 * clears a latched CPU stop; conformance import is rejected on a stopped CPU.
 * Import at an otherwise idle boundary discards decode/REP continuation.
 * Callbacks obey the CPU contract. */
static inline bm_status_t bm_286_pm_step_subset(bm_cpu_t *cpu, bm_286_boundary_t *boundary)
{
    return bm_286_step(cpu, boundary);
}
#endif
