/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#ifndef BLUMACH_COMPONENTS_CPU_80286_TIMING_H
#define BLUMACH_COMPONENTS_CPU_80286_TIMING_H
#include <blumach/components/cpu_80286.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Intel 210498-005 Appendix B nominal execution cost, NOT elapsed clocks.
 * Supply the exact bytes of one successfully completed instruction, without
 * refetching guest memory. This lookup neither executes nor checks privileges.
 * Fault/interrupt delivery must not use a successful-instruction baseline.
 * Unprefixed fixed control/conversion instructions and documented register/
 * immediate MOV, XCHG, ALU, TEST, INC/DEC, NEG/NOT forms. No memory operands,
 * segment moves or undocumented aliases. Successful register MUL/IMUL/DIV/
 * IDIV and decimal adjustments are supported (AAM/AAD only base 10).
 * Divide faults must not use this successful-instruction cost. Register shifts/
 * rotates by one or immediate are supported; CL forms require the API below.
 * See the
 * coverage table in doc/architecture/pcs286-instruction-timing.md.
 * Missing forms (including prefixes) return UNSUPPORTED; output is unchanged
 * on every error. No default cost, wait addition, prefetch or MHz conversion.
 * Do not pass this number directly to the strict clocked CPU scheduler.
 * NULL arguments or zero length are INVALID_ARGUMENT. */
bm_status_t bm_286_nominal_instruction_clocks(const uint8_t *bytes,
    size_t length, uint64_t *clocks);

/* Same contract plus the pre-execution value of CL for D2/D3. No CPU state
 * is read or modified. Use initial CL even when the destination is CL/CX.
 * CL is ignored for forms whose count comes from the encoding. */
bm_status_t bm_286_nominal_instruction_clocks_with_cl(const uint8_t *bytes,
    size_t length, uint8_t initial_cl, uint64_t *clocks);

/* Zero-wait nominal memory cost, kept separate from actual bus timing.
 * MOV 88..8B, C6/C7 /0, A0..A3; ALU 00..3B ModRM and 80/81/83;
 * TEST 84/85 and F6/F7 /0; NOT/NEG/MUL/IMUL/DIV/IDIV F6/F7 /2..7;
 * IMUL 69/6B, INC/DEC FE/FF /0,/1 and XCHG 86/87;
 * shifts/rotates C0/C1,D0/D1, and D2/D3 with explicit initial CL below.
 * Memory forms only, no prefixes or segment moves. Divide faults excluded.
 * Logical 83 /1,/4,/6 remain unqualified. Odd-word read-modify-write forms
 * return UNSUPPORTED pending evidence for counting both references; byte
 * and aligned-word RMW are supported. Supply the resolved 24-bit physical
 * operand address, NOT its segment offset: protected segment bases may be odd.
 * Exact successfully executed instruction bytes are required, as above.
 * No address translation, memory access or access-permission checking occurs.
 * Includes Intel B-6 base+index+displacement and odd-word adjustments.
 * Read waits, write/fetch overlap and prefetch starvation are not included.
 * Missing forms return UNSUPPORTED. NULL/empty inputs or an address outside
 * the 24-bit CPU space return INVALID_ARGUMENT. Output unchanged on failure.
 * The register-only APIs deliberately continue rejecting memory forms. */
typedef struct bm_286_nominal_memory_cost {
    uint64_t instruction_clocks;
    uint64_t addressing_clocks;
    uint64_t alignment_clocks;
    uint64_t total_clocks;
} bm_286_nominal_memory_cost_t;

bm_status_t bm_286_nominal_memory_clocks(const uint8_t *bytes, size_t length,
    uint32_t physical_address, bm_286_nominal_memory_cost_t *cost);

/* Same contract, supplying pre-execution CL for D2/D3. Other forms ignore CL.
 * The API above refuses D2/D3 rather than assuming count zero. Even masked
 * zero does not qualify odd-word RMW alignment; no bus behavior is inferred. */
bm_status_t bm_286_nominal_memory_clocks_with_cl(const uint8_t *bytes, size_t length,
    uint32_t physical_address, uint8_t initial_cl, bm_286_nominal_memory_cost_t *cost);

typedef enum bm_286_timing_mode {
    BM_286_TIMING_REAL = 0,
    BM_286_TIMING_PROTECTED = 1
} bm_286_timing_mode_t;

/* Intel B-73 TABLE BASE ONLY for successful unprefixed MOV 8C/8E.
 * Unlike the memory query, this excludes ALL addressing/alignment terms,
 * including explicit operands and implicit descriptor accesses, and all waits.
 * Protected 8E assumes a successful non-null selector load; null selectors,
 * faults, descriptor accessed-bit variations and interrupt shadow timing are
 * not qualified here. This function cannot check that caller precondition.
 * No selector, descriptor or CPU state is read. Mode must be explicit.
 * CS may be read (8C /1) but not loaded (8E /1). Other aliases unsupported.
 * Exact bytes/length required; INVALID_ARGUMENT for NULL/empty/invalid mode,
 * UNSUPPORTED for other forms. Output unchanged on failure.
 * Do not use this table base directly as elapsed scheduler clocks. */
bm_status_t bm_286_nominal_segment_base_clocks(const uint8_t *bytes, size_t length,
    bm_286_timing_mode_t mode, uint64_t *clocks);

typedef enum bm_286_estimate_source {
    BM_286_ESTIMATE_PENDING = 0,
    BM_286_ESTIMATE_MANUAL,
    BM_286_ESTIMATE_DIAGNOSTIC_FALLBACK
} bm_286_estimate_source_t;

enum {
    BM_286_ESTIMATE_SERIALIZED_WAITS = 1u,
    BM_286_ESTIMATE_NO_PIPELINE = 2u,
    BM_286_ESTIMATE_SEGMENT_BASE_ONLY = 4u
};

typedef struct bm_286_timing_estimate {
    bm_286_timing_quality_t quality;
    bm_286_estimate_source_t source;
    uint64_t base_clocks, wait_clocks, estimated_clocks;
    uint32_t assumptions;
    uint8_t bytes[10], length;
} bm_286_timing_estimate_t;

/* Opt-in provisional execution, NOT bm_286_step_clocked qualification.
 * Executes exactly once through the existing functional real/protected core.
 * Observes the original fetch responses; never rereads memory for timing.
 * Successful supported instructions: manual nominal base + returned bus waits.
 * All waits are serialized, without write/fetch overlap or pipeline modeling;
 * therefore quality is always PROVISIONAL, never DOCUMENTED.
 * Other successful boundaries remain PENDING unless caller supplies nonzero
 * fallback_clocks. That diagnostic base is labeled FALLBACK, not manual data.
 * fallback0 does NOT stop execution: estimate is UNKNOWN/0, waits separate.
 * Functional errors remain errors, not hidden by fallback. Existing boundary
 * timing and strict clocked API stay unchanged. No automatic scheduler switch.
 * CPU callbacks must honor the usual no nested execution/configuration rule.
 */
bm_status_t bm_286_step_provisional(bm_cpu_t *cpu, uint64_t fallback_clocks,
    bm_286_boundary_t *boundary, bm_286_timing_estimate_t *estimate);

#ifdef __cplusplus
}
#endif
#endif
