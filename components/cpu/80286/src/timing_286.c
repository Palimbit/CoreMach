/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * New implementation from Intel 210498-005 (1987), not another emulator.
 * See doc/architecture/pcs286-instruction-timing.md for assumptions/sources. */
#include <blumach/components/cpu_80286_timing.h>

static bm_status_t nominal_clocks(const uint8_t *bytes, size_t length,
    int has_cl, uint8_t cl, uint64_t *clocks)
{
    uint64_t value;
    size_t required = 1;
    uint8_t opcode;
    if (!bytes || !clocks || !length) return BM_STATUS_INVALID_ARGUMENT;
    opcode = bytes[0];
    /* Intel B-19/20/33/80/99/109/116: eight regular ALU families.
     * Only ModRM mod=11; memory has distinct costs and is not approximated. */
    if (opcode == 0xc0 || opcode == 0xc1 ||
        (opcode >= 0xd0 && opcode <= 0xd3)) {
        /* Intel B-90/91/97: successful register shifts/rotates only.
         * The masked count is NOT reduced modulo operand width for timing. */
        if (length < 2 || (bytes[1] & 0xc0U) != 0xc0U ||
            ((bytes[1] >> 3) & 7U) == 6U)
            return BM_STATUS_UNSUPPORTED;
        required = opcode < 0xd0 ? 3 : 2;
        if (length != required) return BM_STATUS_UNSUPPORTED;
        if (opcode == 0xd0 || opcode == 0xd1) value = 2;
        else {
            uint8_t count;
            if (opcode < 0xd0) count = bytes[2];
            else {
                if (!has_cl) return BM_STATUS_UNSUPPORTED;
                count = cl; /* Caller supplies pre-execution CL, even for CL destination. */
            }
            value = 5U + (count & 31U);
        }
    } else if (opcode <= 0x3d && (opcode & 7U) <= 5U) {
        if ((opcode & 7U) <= 3U) {
            if (length < 2 || (bytes[1] & 0xc0U) != 0xc0U)
                return BM_STATUS_UNSUPPORTED;
            required = 2; value = 2;
        } else {
            required = (opcode & 1U) ? 3 : 2; value = 3;
        }
    } else if (opcode == 0x69 || opcode == 0x6b) {
        /* IMUL rw,rw,imm: Intel B-44, not the 8086 operand-dependent model. */
        if (length < 2 || (bytes[1] & 0xc0U) != 0xc0U)
            return BM_STATUS_UNSUPPORTED;
        required = opcode == 0x69 ? 4 : 3; value = 21;
    } else if (opcode == 0xd4 || opcode == 0xd5) {
        /* Only the documented decimal AAM/AAD encodings, B-16/17. */
        if (length < 2 || bytes[1] != 0x0a) return BM_STATUS_UNSUPPORTED;
        required = 2; value = opcode == 0xd4 ? 16 : 14;
    } else if (opcode >= 0x40 && opcode <= 0x4f) {
        value = 2; /* INC/DEC rw: B-38/46. */
    } else if (opcode >= 0x90 && opcode <= 0x97) {
        value = 3; /* XCHG AX,rw / NOP: B-78/114. */
    } else if (opcode >= 0xb0 && opcode <= 0xbf) {
        required = opcode < 0xb8 ? 2 : 3; value = 2; /* MOV B-73. */
    } else if (opcode == 0xa8 || opcode == 0xa9) {
        required = opcode == 0xa8 ? 2 : 3; value = 3; /* TEST B-110. */
    } else if (opcode == 0x80 || opcode == 0x81 || opcode == 0x83 ||
               (opcode >= 0x84 && opcode <= 0x8b) ||
               opcode == 0xc6 || opcode == 0xc7 ||
               opcode == 0xf6 || opcode == 0xf7 ||
               opcode == 0xfe || opcode == 0xff) {
        unsigned group;
        if (length < 2 || (bytes[1] & 0xc0U) != 0xc0U)
            return BM_STATUS_UNSUPPORTED;
        group = (bytes[1] >> 3) & 7U;
        required = 2; value = 2;
        if (opcode == 0x80 || opcode == 0x81 || opcode == 0x83) {
            /* The selected manual omits 83 /1,/4,/6 timing rows. Leave
             * those queries unqualified, not an assertion of invalid ISA. */
            if (opcode == 0x83 && (group == 1 || group == 4 || group == 6))
                return BM_STATUS_UNSUPPORTED;
            required = opcode == 0x81 ? 4 : 3; value = 3;
        } else if (opcode == 0x86 || opcode == 0x87) {
            value = 3; /* XCHG B-114; TEST/MOV remain two clocks. */
        } else if (opcode == 0xc6 || opcode == 0xc7) {
            if (group != 0) return BM_STATUS_UNSUPPORTED;
            required = opcode == 0xc6 ? 3 : 4;
        } else if (opcode == 0xf6 || opcode == 0xf7) {
            if (group == 0) { /* TEST B-110. */
                required = opcode == 0xf6 ? 3 : 4; value = 3;
            } else if (group >= 4) {
                /* Successful MUL/IMUL/DIV/IDIV, B-76/44/39/43.
                 * These are not costs for divide-error delivery. */
                value = (group <= 5 ? 13U : group == 6 ? 14U : 17U)
                    + (opcode == 0xf7 ? 8U : 0U);
            } else if (group == 1) {
                return BM_STATUS_UNSUPPORTED; /* Undefined group; NOT/NEG = 2. */
            }
        } else if ((opcode == 0xfe || opcode == 0xff) && group > 1) {
            return BM_STATUS_UNSUPPORTED; /* INC/DEC only. */
        }
    } else switch (opcode) {
    case 0x27: /* DAA B-36 */
    case 0x2f: /* DAS B-37 */
    case 0x37: /* AAA B-15 */
    case 0x3f: /* AAS B-18 */
        value = 3; break;
    case 0x98: /* CBW B-27 */
    case 0x99: /* CWD B-35 */
    case 0xf8: /* CLC B-28 */
    case 0xfc: /* CLD B-29 */
    case 0xf5: /* CMC B-32 */
    case 0xf9: /* STC B-104 */
    case 0xfd: /* STD B-105 */
    case 0xfb: /* STI B-106 */
        value = 2; break;
    case 0x90: /* NOP B-78 */
    case 0xfa: /* CLI B-30: three clocks, unlike STI */
        value = 3; break;
    default: return BM_STATUS_UNSUPPORTED;
    }
    if (length != required) return BM_STATUS_UNSUPPORTED;
    *clocks = value;
    return BM_STATUS_OK;
}

bm_status_t bm_286_nominal_instruction_clocks(const uint8_t *bytes,
    size_t length, uint64_t *clocks)
{
    return nominal_clocks(bytes, length, 0, 0, clocks);
}

bm_status_t bm_286_nominal_instruction_clocks_with_cl(const uint8_t *bytes,
    size_t length, uint8_t initial_cl, uint64_t *clocks)
{
    return nominal_clocks(bytes, length, 1, initial_cl, clocks);
}

static bm_status_t nominal_memory_clocks(const uint8_t *bytes, size_t length,
    uint32_t physical_address, int has_cl, uint8_t initial_cl,
    bm_286_nominal_memory_cost_t *cost)
{
    bm_286_nominal_memory_cost_t result = {0};
    size_t required;
    unsigned opcode, word;
    int read_modify_write = 0;
    if (!bytes || !cost || !length || physical_address > 0xffffffU)
        return BM_STATUS_INVALID_ARGUMENT;
    opcode = bytes[0];
    word = opcode & 1U;
    if (opcode >= 0xa0 && opcode <= 0xa3) {
        required = 3;
        result.instruction_clocks = opcode < 0xa2 ? 5 : 3;
    } else if ((opcode >= 0x88 && opcode <= 0x8b) ||
               opcode == 0xc6 || opcode == 0xc7 ||
               (opcode <= 0x3b && (opcode & 7U) <= 3U) ||
               opcode == 0x80 || opcode == 0x81 || opcode == 0x83 ||
               (opcode >= 0x84 && opcode <= 0x87) ||
               opcode == 0x69 || opcode == 0x6b ||
               opcode == 0xc0 || opcode == 0xc1 ||
               (opcode >= 0xd0 && opcode <= 0xd3) ||
               opcode == 0xfe || opcode == 0xff ||
               opcode == 0xf6 || opcode == 0xf7) {
        unsigned mod, rm, group;
        if (length < 2) return BM_STATUS_UNSUPPORTED;
        mod = bytes[1] >> 6;
        rm = bytes[1] & 7U;
        group = (bytes[1] >> 3) & 7U;
        if (mod == 3) return BM_STATUS_UNSUPPORTED;
        required = 2;
        if (mod == 1) required += 1;
        else if (mod == 2 || (mod == 0 && rm == 6)) required += 2;
        if (opcode == 0xc0 || opcode == 0xc1 ||
            (opcode >= 0xd0 && opcode <= 0xd3)) {
            /* Intel B-90/91/97/98: variable count costs 8+(count&31),
             * not 7*count, and not modulo the operand/carry width. */
            if (group == 6) return BM_STATUS_UNSUPPORTED;
            read_modify_write = 1;
            if (opcode == 0xd0 || opcode == 0xd1)
                result.instruction_clocks = 7;
            else {
                uint8_t count;
                if (opcode < 0xd0) {
                    if (length != required+1) return BM_STATUS_UNSUPPORTED;
                    count = bytes[required++]; /* After displacement, not ModRM. */
                } else {
                    if (!has_cl) return BM_STATUS_UNSUPPORTED;
                    count = initial_cl;
                }
                result.instruction_clocks = 8U + (count & 31U);
            }
        } else if (opcode <= 0x3b) {
            /* B-19/20/33/80/99/109/116: CMP direction matters, even
             * though both directions are read-only memory references. */
            result.instruction_clocks = opcode >= 0x3a ? 6 : 7;
            read_modify_write = opcode < 0x38 && !(opcode & 2U);
        } else if (opcode == 0x80 || opcode == 0x81 || opcode == 0x83) {
            if (opcode == 0x83 && (group == 1 || group == 4 || group == 6))
                return BM_STATUS_UNSUPPORTED; /* Same source gap as register forms. */
            required += opcode == 0x81 ? 2 : 1;
            result.instruction_clocks = group == 7 ? 6 : 7;
            read_modify_write = group != 7;
        } else if (opcode == 0x69 || opcode == 0x6b) {
            required += opcode == 0x69 ? 2 : 1;
            result.instruction_clocks = 24; /* IMUL B-44, word source for both. */
        } else if (opcode == 0x86 || opcode == 0x87) {
            result.instruction_clocks = 5; /* XCHG B-114; lock is implicit. */
            read_modify_write = 1;
        } else if (opcode == 0xfe || opcode == 0xff) {
            if (group > 1) return BM_STATUS_UNSUPPORTED;
            result.instruction_clocks = 7; /* INC/DEC B-38/46. */
            read_modify_write = 1;
        } else if (opcode == 0x84 || opcode == 0x85) {
            result.instruction_clocks = 6; /* TEST B-110, no write. */
        } else if (opcode == 0xf6 || opcode == 0xf7) {
            if (group == 0) {
                required += word ? 2 : 1;
                result.instruction_clocks = 6; /* TEST B-110. */
            } else if (group == 1) return BM_STATUS_UNSUPPORTED;
            else if (group <= 3) {
                result.instruction_clocks = 7; /* NOT/NEG B-79/77. */
                read_modify_write = 1;
            } else {
                /* MUL/IMUL/DIV/IDIV B-76/44/39/43: successful execution
                 * only. Memory source is read once, not written back. */
                result.instruction_clocks = (group <= 5 ? 16U : group == 6 ? 17U : 20U)
                    + (word ? 8U : 0U);
            }
        } else {
            if (opcode == 0xc6 || opcode == 0xc7) {
                if (group != 0) return BM_STATUS_UNSUPPORTED;
                required += word ? 2 : 1;
            }
            result.instruction_clocks = opcode == 0x8a || opcode == 0x8b ? 5 : 3;
        }
        /* BX/BP + SI/DI + encoded displacement. A zero displacement still
         * selects this addressing form; a direct disp16 is not indexed. */
        result.addressing_clocks = mod != 0 && rm < 4 ? 1 : 0;
    } else return BM_STATUS_UNSUPPORTED;
    if (length != required) return BM_STATUS_UNSUPPORTED;
    /* B-6 alone does not settle counting the read AND write penalties.
     * Do not choose +2 or +4 for odd-word RMW until corroborated. Byte
     * and aligned-word forms require no such assumption. */
    if (read_modify_write && word && (physical_address & 1U))
        return BM_STATUS_UNSUPPORTED;
    result.alignment_clocks = word && (physical_address & 1U) ? 2 : 0;
    result.total_clocks = result.instruction_clocks +
        result.addressing_clocks + result.alignment_clocks;
    *cost = result;
    return BM_STATUS_OK;
}

bm_status_t bm_286_nominal_segment_base_clocks(const uint8_t *bytes, size_t length,
    bm_286_timing_mode_t mode, uint64_t *clocks)
{
    unsigned mod, rm, segment;
    size_t required = 2;
    uint64_t value;
    if (!bytes || !clocks || !length ||
        (mode != BM_286_TIMING_REAL && mode != BM_286_TIMING_PROTECTED))
        return BM_STATUS_INVALID_ARGUMENT;
    if (length < 2 || (bytes[0] != 0x8c && bytes[0] != 0x8e))
        return BM_STATUS_UNSUPPORTED;
    mod = bytes[1] >> 6; rm = bytes[1] & 7U;
    segment = (bytes[1] >> 3) & 7U;
    if (segment > 3 || (bytes[0] == 0x8e && segment == 1))
        return BM_STATUS_UNSUPPORTED;
    if (mod == 1) ++required;
    else if (mod == 2 || (mod == 0 && rm == 6)) required += 2;
    if (length != required) return BM_STATUS_UNSUPPORTED;
    /* Table bases only; descriptor access effects must not be guessed here. */
    if (bytes[0] == 0x8c) value = mod == 3 ? 2 : 3;
    else if (mode == BM_286_TIMING_PROTECTED) value = mod == 3 ? 17 : 19;
    else value = mod == 3 ? 2 : 5;
    *clocks = value;
    return BM_STATUS_OK;
}

bm_status_t bm_286_nominal_memory_clocks(const uint8_t *bytes, size_t length,
    uint32_t physical_address, bm_286_nominal_memory_cost_t *cost)
{
    return nominal_memory_clocks(bytes,length,physical_address,0,0,cost);
}

bm_status_t bm_286_nominal_memory_clocks_with_cl(const uint8_t *bytes, size_t length,
    uint32_t physical_address, uint8_t initial_cl, bm_286_nominal_memory_cost_t *cost)
{
    return nominal_memory_clocks(bytes,length,physical_address,1,initial_cl,cost);
}
