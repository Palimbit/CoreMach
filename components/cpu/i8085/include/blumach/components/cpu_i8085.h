/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_COMPONENTS_CPU_I8085_H
#define BLUMACH_COMPONENTS_CPU_I8085_H

#include <stdint.h>
#include <blumach/components/bus.h>
#include <blumach/engine/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sources cited by this component:
 *  [MCS85]  Intel MCS-80/85 Family User's Manual, January 1983.
 *  [ASM85]  Intel 8080/8085 Assembly Language Programming Manual 9800301D.
 *  [OKI86]  OKI 1986 Microprocessor Data Book, MSM80C85A RS/GS.
 *  [KS1302] K. Shirriff, "Looking at silicon to understanding the 8085's
 *           undocumented flags", righto.com, February 2013.
 *  [DS79]   W. Dehnhardt and V. M. Sorensen, "Unspecified 8085 op codes
 *           enhance programming", Electronics, 18 January 1979, pp. 144-145.
 *  [TUN97]  Tundra Semiconductor, CA80C85B data sheet, Table 2-10.
 *
 * The OKI MSM80C85A is selected separately so that a machine records the part
 * it carries. [OKI86] pp. 16-32 documents no difference from the Intel 8085A
 * in instruction cycles, interrupts or machine cycles, so both profiles share
 * the same semantics and timing until a difference has its own evidence. */
typedef enum bm_i8085_model {
    BM_I8085_INTEL_8085A = 0,
    BM_I8085_OKI_MSM80C85A = 1
} bm_i8085_model_t;

/* Lines passed through bm_cpu_ops.signal, all in positive logic: asserted
 * means the documented active level, including active-low RESET IN and
 * READY low. Inputs are sampled at architectural boundaries.
 *  INTR, RST 5.5, RST 6.5: high level until sampled ([OKI86] Table 1).
 *  RST 7.5: a rising edge sets the internal flip-flop, even while masked.
 *  TRAP: a rising edge is latched and is accepted only while still high.
 *  RESET: while asserted the CPU is held and every boundary consumes one
 *         TRESET clock; on release execution restarts at 0000h.
 *  WAIT: READY held low. Each boundary consumes one TWAIT clock without
 *        progress; wait placement inside a machine cycle is not modelled.
 *  SID: serial input level, read into RIM bit 7. */
typedef enum bm_i8085_signal {
    BM_I8085_SIGNAL_INTR = 0,
    BM_I8085_SIGNAL_TRAP = 1,
    BM_I8085_SIGNAL_RST55 = 2,
    BM_I8085_SIGNAL_RST65 = 3,
    BM_I8085_SIGNAL_RST75 = 4,
    BM_I8085_SIGNAL_RESET = 5,
    BM_I8085_SIGNAL_WAIT = 6,
    BM_I8085_SIGNAL_SID = 7
} bm_i8085_signal_t;

typedef enum bm_i8085_opcode_class {
    BM_I8085_OPCODE_DOCUMENTED = 0,
    /* Present on 8085 silicon but absent from Intel's published ISA: DSUB,
     * ARHL, RDEL, LDHI, LDSI, RSTV, SHLX, JNK, LHLX and JK ([DS79]; JNK and
     * JK are JNX5/JX5 there and JNUI/JUI in [TUN97]). For a strict host the
     * component does not execute them: after the opcode fetch it restores
     * PC and returns BM_STATUS_UNSUPPORTED without other architectural
     * change. For a host with BM_HOST_CAPABILITY_TOLERANCE it executes them
     * as [DS79] describes, with [DS79]/[TUN97] timing, and reports each
     * opcode once; the OKI MSM80C85A report is marked as assumed behaviour.
     * Unconfirmed flag cases are listed in doc/architecture/intel-8085-cpu.md. */
    BM_I8085_OPCODE_UNDOCUMENTED = 1
} bm_i8085_opcode_class_t;

typedef enum bm_i8085_boundary_kind {
    BM_I8085_BOUNDARY_INSTRUCTION = 0,
    /* TRAP, RST 7.5/6.5/5.5 or INTR acknowledge consumed the boundary. */
    BM_I8085_BOUNDARY_INTERRUPT = 1,
    /* READY low: one TWAIT clock without progress. */
    BM_I8085_BOUNDARY_WAIT = 2,
    /* RESET IN asserted: one TRESET clock. */
    BM_I8085_BOUNDARY_RESET = 3
} bm_i8085_boundary_kind_t;

typedef enum bm_i8085_interrupt_source {
    BM_I8085_INTERRUPT_NONE = 0,
    BM_I8085_INTERRUPT_TRAP = 1,
    BM_I8085_INTERRUPT_RST75 = 2,
    BM_I8085_INTERRUPT_RST65 = 3,
    BM_I8085_INTERRUPT_RST55 = 4,
    BM_I8085_INTERRUPT_INTR = 5
} bm_i8085_interrupt_source_t;

typedef enum bm_i8085_clock_kind {
    BM_I8085_CLOCKS_UNKNOWN = 0,
    BM_I8085_CLOCKS_EXACT = 1
} bm_i8085_clock_kind_t;

#define BM_I8085_TIMING_OBSERVATION_VERSION 1U

/* Host-neutral timing observation for one completed boundary, in CLK
 * (T-state) units. Conditional instructions report the realised path, so a
 * documented value is EXACT after execution. t_states excludes bus wait
 * states, which are reported separately. UNKNOWN is an explicit
 * classification for a value without a cited source, never a zero-cycle
 * claim. */
typedef struct bm_i8085_timing_observation {
    uint32_t size;
    uint32_t version;
    bm_i8085_boundary_kind_t kind;
    bm_i8085_interrupt_source_t interrupt_source;
    uint8_t opcode;
    uint8_t machine_cycles;
    uint8_t branch_taken;
    uint8_t reserved;
    bm_i8085_clock_kind_t clock_kind;
    uint32_t t_states;
    uint64_t logical_bus_transactions;
    uint64_t reported_wait_states;
} bm_i8085_timing_observation_t;

typedef void (*bm_i8085_timing_fn)(
    void *context, const bm_i8085_timing_observation_t *observation);

/* Called once per INTA machine cycle after INTR is accepted; cycle is 0 for
 * the opcode and 1-2 for the CALL address bytes ([MCS85] Figures 2-17 and
 * 2-18, pp. 2-13..2-14). Only RST n and CALL are accepted; any other opcode
 * returns BM_STATUS_UNSUPPORTED. A null callback leaves INTR unconnected. */
typedef bm_status_t (*bm_i8085_interrupt_ack_fn)(void *context,
                                                 unsigned int cycle,
                                                 uint8_t *value);

/* Called when SIM loads the SOD latch (accumulator bit 6 set). The pin
 * changes during M1-T2 with CLK low of the following machine cycle
 * ([MCS85] Figure 2-25, p. 2-20): half_clocks_after_boundary is that delay
 * from the end of the SIM boundary, counted in half CLK periods. */
typedef void (*bm_i8085_sod_fn)(void *context, int level,
                                uint32_t half_clocks_after_boundary);

typedef struct bm_i8085_config {
    bm_i8085_model_t model;
    /* CLK output frequency, half of the X1/X2 input. Olivetti M10:
     * 4 915 200 Hz crystal, 2 457 600 Hz CLK. */
    uint32_t frequency_hz;
    /* Mandatory. Opcode bytes use BM_BUS_FETCH (M1); operands and data use
     * BM_BUS_READ/WRITE. Memory addresses are 16-bit; I/O ports 0..255 are
     * issued in BM_ADDRESS_IO. Endpoint wait_states count CLK periods. */
    bm_bus_t *bus;
    bm_i8085_interrupt_ack_fn interrupt_ack;
    void *interrupt_context;
    bm_i8085_sod_fn sod;
    void *sod_context;
    /* Optional observer; it cannot affect execution. */
    bm_i8085_timing_fn timing;
    void *timing_context;
} bm_i8085_config_t;

/* PSW flag bits as pushed by PUSH PSW. Bit 3 always reads as zero. V and K
 * are undocumented by Intel ([MCS85] p. 5-15 marks bits 1, 3 and 5 as
 * undefined); their behaviour follows the silicon analysis in [KS1302]:
 * V is the carry into bit 7 XOR the carry out of bit 7 of the ALU, and K is
 * V XOR S for ALU operations or the incrementer carry for INX/DCX. Cases
 * that [KS1302] does not establish leave the previous value unchanged. */
#define BM_I8085_FLAG_CY 0x01U
#define BM_I8085_FLAG_V 0x02U
#define BM_I8085_FLAG_P 0x04U
#define BM_I8085_FLAG_AC 0x10U
#define BM_I8085_FLAG_K 0x20U
#define BM_I8085_FLAG_Z 0x40U
#define BM_I8085_FLAG_S 0x80U

/* SIM/RIM mask bits: bit 0 RST 5.5, bit 1 RST 6.5, bit 2 RST 7.5. */
#define BM_I8085_MASK_RST55 0x01U
#define BM_I8085_MASK_RST65 0x02U
#define BM_I8085_MASK_RST75 0x04U

#define BM_I8085_ARCH_STATE_VERSION 1U

/* Architectural state at an instruction boundary. Pin levels (INTR, TRAP,
 * RST 5.5/6.5/7.5, RESET, READY and SID) are external and excluded; the
 * RST 7.5 flip-flop and a latched TRAP edge are internal and included. */
typedef struct bm_i8085_arch_state {
    uint32_t size;
    uint32_t version;
    bm_i8085_model_t model;
    uint8_t a;
    uint8_t f;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint16_t sp;
    uint16_t pc;
    uint8_t interrupt_enable;
    /* EI enables maskable interrupts only after the following instruction
     * ([MCS85] p. 5-17). */
    uint8_t interrupt_enable_delay;
    uint8_t halted;
    uint8_t interrupt_mask;
    uint8_t rst75_pending;
    uint8_t trap_pending;
    /* IE before the last accepted TRAP, reported by the next RIM only. */
    uint8_t trap_saved_interrupt_enable;
    uint8_t trap_saved_valid;
    uint8_t sod;
    uint8_t reserved[3];
} bm_i8085_arch_state_t;

bm_status_t bm_i8085_create(const bm_host_services_t *host,
                            const bm_i8085_config_t *config,
                            bm_cpu_t *out_cpu);

bm_status_t bm_i8085_get_arch_state(const bm_cpu_t *cpu,
                                    bm_i8085_arch_state_t *out_state);
bm_status_t bm_i8085_set_arch_state(bm_cpu_t *cpu,
                                    const bm_i8085_arch_state_t *state);

bm_i8085_opcode_class_t bm_i8085_classify_opcode(uint8_t opcode);

/* Execute one architectural boundary, as bm_cpu_ops.run with budget 1.
 * A halted CPU without an acceptable interrupt returns BM_STATUS_IDLE. */
bm_status_t bm_i8085_step(bm_cpu_t *cpu, bm_tick_t *consumed);

/* Clocked engine callback. Reported cycles include bus wait states. It
 * returns BM_STATUS_IDLE with zero cycles while halted, so the engine wakes
 * the CPU when an interrupt line is asserted; RESET and WAIT boundaries
 * consume one clock each instead. UNKNOWN boundaries return
 * BM_STATUS_UNSUPPORTED with zero cycles. The context must be the one owned
 * by a CPU created by bm_i8085_create(). */
bm_status_t bm_i8085_step_clocked(void *context, bm_tick_t start_ns,
                                  uint64_t *cycles);

#ifdef __cplusplus
}
#endif

#endif
