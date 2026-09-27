/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include <blumach/components/cpu_80286_timing.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void check(const uint8_t *bytes, size_t length, uint64_t expected)
{
    uint8_t saved[8], prefixed[9];
    uint64_t value = UINT64_MAX;
    assert(length < sizeof(saved));
    memcpy(saved, bytes, length);
    assert(bm_286_nominal_instruction_clocks(bytes,length,&value) ==
           (expected ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
    assert(value == (expected ? expected : UINT64_MAX));
    assert(memcmp(saved, bytes, length) == 0);
    if (!expected) return;
    for (size_t n = 1; n < length; ++n) {
        value = UINT64_MAX;
        assert(bm_286_nominal_instruction_clocks(bytes,n,&value) == BM_STATUS_UNSUPPORTED);
        assert(value == UINT64_MAX);
    }
    saved[length] = 0x90; value = UINT64_MAX;
    assert(bm_286_nominal_instruction_clocks(saved,length+1,&value) == BM_STATUS_UNSUPPORTED);
    assert(value == UINT64_MAX);
    static const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e,0xf0,0xf2,0xf3};
    for (size_t i = 0; i < sizeof(prefixes); ++i) {
        prefixed[0] = prefixes[i]; memcpy(prefixed+1,bytes,length);
        value = UINT64_MAX;
        assert(bm_286_nominal_instruction_clocks(prefixed,length+1,&value) == BM_STATUS_UNSUPPORTED);
        assert(value == UINT64_MAX);
    }
}

static void test_register_and_immediate_forms(void)
{
    static const uint8_t alu[] = {0x00,0x08,0x10,0x18,0x20,0x28,0x30,0x38};
    static const uint8_t pairs[] = {0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b};
    uint8_t b[5] = {0};
    for (unsigned m = 0; m < 256; ++m) {
        b[1] = (uint8_t)m;
        for (size_t i = 0; i < sizeof(alu); ++i)
            for (unsigned direction_width = 0; direction_width < 4; ++direction_width) {
                b[0] = alu[i] + direction_width;
                check(b,2,m >= 192 ? 2 : 0);
            }
        for (size_t i = 0; i < sizeof(pairs); ++i) {
            b[0] = pairs[i];
            check(b,2,m < 192 ? 0 : (i == 2 || i == 3) ? 3 : 2);
        }
        unsigned group = (m / 8) % 8;
        b[2] = 0x80; b[3] = 0xff;
        b[0] = 0x80; check(b,3,m >= 192 ? 3 : 0);
        b[0] = 0x81; check(b,4,m >= 192 ? 3 : 0);
        b[0] = 0x83;
        check(b,3,m >= 192 && (group == 0 || group == 2 || group == 3 || group == 5 || group == 7) ? 3 : 0);
        for (unsigned width = 0; width < 2; ++width) {
            b[0] = 0xc6 + width;
            check(b,3+width,m >= 192 && group == 0 ? 2 : 0);
            b[0] = 0xfe + width;
            check(b,2,m >= 192 && group <= 1 ? 2 : 0);
            b[0] = 0xf6 + width;
            static const unsigned group_cost[2][8] = {
                {0,0,2,2,13,13,14,17}, {0,0,2,2,21,21,22,25}
            };
            check(b,2,m >= 192 ? group_cost[width][group] : 0);
            check(b,3+width,m >= 192 && group == 0 ? 3 : 0);
        }
    }
    for (unsigned immediate = 0; immediate < 256; ++immediate) {
        b[1] = (uint8_t)immediate; b[2] = (uint8_t)(255-immediate);
        for (size_t i = 0; i < sizeof(alu); ++i) {
            b[0] = alu[i]+4; check(b,2,3);
            b[0] = alu[i]+5; check(b,3,3);
        }
        for (unsigned reg = 0; reg < 8; ++reg) {
            b[0] = 0xb0+reg; check(b,2,2);
            b[0] = 0xb8+reg; check(b,3,2);
        }
        b[0] = 0xa8; check(b,2,3);
        b[0] = 0xa9; check(b,3,3);
    }
    /* Complete memory encoding, segment moves and alias are still refused. */
    const uint8_t memory[] = {0x81,0x86,0x00,0x10,0x34,0x12};
    const uint8_t segment[] = {0x8e,0xd8}, alias[] = {0x82,0xc0,0x01};
    check(memory,sizeof(memory),0); check(segment,sizeof(segment),0);
    check(alias,sizeof(alias),0);
}

static void test_multiply_immediate_and_adjust(void)
{
    for (unsigned m = 0; m < 256; ++m)
        for (unsigned imm = 0; imm < 256; ++imm) {
            uint8_t b[] = {0x6b,(uint8_t)m,(uint8_t)imm,(uint8_t)(255-imm)};
            check(b,3,m >= 192 ? 21 : 0);
            b[0] = 0x69;
            check(b,4,m >= 192 ? 21 : 0);
        }
    for (unsigned base = 0; base < 256; ++base) {
        uint8_t b[] = {0xd4,(uint8_t)base};
        check(b,2,base == 10 ? 16 : 0);
        b[0] = 0xd5; check(b,2,base == 10 ? 14 : 0);
    }
    /* Fully encoded memory operands stay outside this register-only block. */
    const uint8_t memory_mul[] = {0xf7,0xa6,0x34,0x12};
    const uint8_t memory_imul[] = {0x69,0x86,0x34,0x12,0xff,0xff};
    check(memory_mul,sizeof(memory_mul),0);
    check(memory_imul,sizeof(memory_imul),0);
}

static void test_shift_counts(void)
{
    const uint8_t opcodes[] = {0xc0,0xc1,0xd0,0xd1,0xd2,0xd3};
    for (size_t op = 0; op < sizeof(opcodes); ++op)
        for (unsigned m = 0; m < 256; ++m)
            for (unsigned count = 0; count < 256; ++count) {
                uint8_t bytes[4] = {opcodes[op],(uint8_t)m,(uint8_t)count,0x90};
                size_t length = op < 2 ? 3 : 2;
                int supported = m >= 192 && (m / 8) % 8 != 6;
                uint64_t expected = op == 2 || op == 3 ? 2 : 5 + count % 32;
                uint64_t value = UINT64_MAX;
                assert(bm_286_nominal_instruction_clocks_with_cl(bytes,length,
                    (uint8_t)count,&value) == (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
                assert(value == (supported ? expected : UINT64_MAX));
                /* Immediate and implicit forms must ignore unrelated CL. */
                if (op < 4) {
                    value = UINT64_MAX;
                    assert(bm_286_nominal_instruction_clocks_with_cl(bytes,length,
                        (uint8_t)(255-count),&value) == (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
                    assert(value == (supported ? expected : UINT64_MAX));
                    check(bytes,length,supported ? expected : 0);
                } else {
                    check(bytes,length,0); /* No implicit assumption CL=0. */
                    for (size_t n = 1; n <= 3; n += 2) {
                        value = UINT64_MAX;
                        assert(bm_286_nominal_instruction_clocks_with_cl(bytes,n,
                            (uint8_t)count,&value) == BM_STATUS_UNSUPPORTED);
                        assert(value == UINT64_MAX);
                    }
                }
                assert(bytes[0] == opcodes[op] && bytes[1] == m && bytes[2] == count);
            }
    uint64_t value = UINT64_MAX;
    const uint8_t rotate_cl[] = {0xd2,0xc1};
    assert(bm_286_nominal_instruction_clocks_with_cl(NULL,2,1,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_instruction_clocks_with_cl(rotate_cl,0,1,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_instruction_clocks_with_cl(rotate_cl,2,1,NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(value == UINT64_MAX);
    /* Eight rotations may restore an 8-bit value, but still cost 5+8 clocks. */
    assert(bm_286_nominal_instruction_clocks_with_cl(rotate_cl,2,8,&value) == BM_STATUS_OK);
    assert(value == 13);
    const uint8_t prefixed[] = {0x26,0xd2,0xc1};
    value = UINT64_MAX;
    assert(bm_286_nominal_instruction_clocks_with_cl(prefixed,3,1,&value) == BM_STATUS_UNSUPPORTED);
    assert(value == UINT64_MAX);
}

static void check_memory(const uint8_t *bytes, size_t length, uint32_t address,
    uint64_t base, uint64_t ea, uint64_t alignment)
{
    const bm_286_nominal_memory_cost_t sentinel = {101,102,103,104};
    bm_286_nominal_memory_cost_t result = sentinel;
    uint8_t saved[8];
    assert(length < sizeof(saved));
    memcpy(saved,bytes,length);
    assert(bm_286_nominal_memory_clocks(bytes,length,address,&result) ==
        (base ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
    assert(memcmp(saved,bytes,length) == 0);
    if (!base) {
        assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
        return;
    }
    assert(result.instruction_clocks == base);
    assert(result.addressing_clocks == ea);
    assert(result.alignment_clocks == alignment);
    assert(result.total_clocks == base+ea+alignment);
    for (size_t n = 1; n <= length+1; ++n) {
        if (n == length) continue;
        saved[length] = 0x90; result = sentinel;
        assert(bm_286_nominal_memory_clocks(saved,n,address,&result) == BM_STATUS_UNSUPPORTED);
        assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
    }
    static const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e,0xf0,0xf2,0xf3};
    for (size_t p = 0; p < sizeof(prefixes); ++p) {
        saved[0] = prefixes[p]; memcpy(saved+1,bytes,length); result = sentinel;
        assert(bm_286_nominal_memory_clocks(saved,length+1,address,&result) == BM_STATUS_UNSUPPORTED);
        assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
    }
    /* No silent fallback to alignment-blind costs through the old APIs. */
    check(bytes,length,0);
}

static void test_memory_mov(void)
{
    const uint8_t opcodes[] = {0x88,0x89,0x8a,0x8b,0xc6,0xc7};
    const uint32_t addresses[] = {0,1,0x10000,0x10001,0xfffffe,0xffffff};
    /* Independent layout oracle: rows = mod, columns = r/m. */
    static const unsigned displacement[4][8] = {
        {0,0,0,0,0,0,2,0}, {1,1,1,1,1,1,1,1},
        {2,2,2,2,2,2,2,2}, {0,0,0,0,0,0,0,0}
    };
    static const unsigned address_cost[4][8] = {
        {0,0,0,0,0,0,0,0}, {1,1,1,1,0,0,0,0},
        {1,1,1,1,0,0,0,0}, {0,0,0,0,0,0,0,0}
    };
    for (size_t op = 0; op < sizeof(opcodes); ++op)
        for (unsigned m = 0; m < 256; ++m)
            for (unsigned fill = 0; fill < 3; ++fill)
                for (size_t a = 0; a < sizeof(addresses)/sizeof(addresses[0]); ++a) {
                    uint8_t b[7]; memset(b,fill == 0 ? 0 : fill == 1 ? 0x80 : 0xff,sizeof(b));
                    b[0] = opcodes[op]; b[1] = (uint8_t)m;
                    size_t length = 2 + displacement[m/64][m%8] + (op >= 4 ? op-3 : 0);
                    int supported = m < 192 && (op < 4 || (m/8)%8 == 0);
                    check_memory(b,length,addresses[a],supported ? (op == 2 || op == 3 ? 5 : 3) : 0,
                        address_cost[m/64][m%8],(op%2 && addresses[a]%2) ? 2 : 0);
                }
    for (unsigned op = 0xa0; op <= 0xa3; ++op)
        for (unsigned offset = 0; offset < 2; ++offset)
            for (size_t a = 0; a < sizeof(addresses)/sizeof(addresses[0]); ++a) {
                uint8_t b[] = {(uint8_t)op,(uint8_t)offset,0x12};
                /* Physical parity is independent of offset parity. */
                check_memory(b,sizeof(b),addresses[a],op < 0xa2 ? 5 : 3,0,
                    (op%2 && addresses[a]%2) ? 2 : 0);
            }
    const uint8_t valid[] = {0x8b,0x00};
    bm_286_nominal_memory_cost_t result = {101,102,103,104};
    const bm_286_nominal_memory_cost_t saved = result;
    assert(bm_286_nominal_memory_clocks(NULL,2,0,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks(valid,0,0,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks(valid,2,0,NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks(valid,2,0x1000000,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&result,&saved,sizeof(result)) == 0);
    const uint8_t segment[] = {0x8e,0x00}, shift[] = {0xd1,0x30};
    check_memory(segment,2,0,0,0,0); check_memory(shift,2,0,0,0,0);
}

static void test_memory_arithmetic(void)
{
    /* Costs independently transcribed by direction: CMP is not symmetric. */
    static const unsigned costs[8][4] = {
        {7,7,7,7}, {7,7,7,7}, {7,7,7,7}, {7,7,7,7},
        {7,7,7,7}, {7,7,7,7}, {7,7,7,7}, {7,7,6,6}
    };
    for (unsigned m = 0; m < 256; ++m)
        for (unsigned odd = 0; odd < 2; ++odd) {
            unsigned mod = m/64, rm = m%8, group = (m/8)%8;
            size_t size = 2 + (mod == 1 ? 1 : mod == 2 || (mod == 0 && rm == 6) ? 2 : 0);
            unsigned ea = (mod == 1 || mod == 2) && rm < 4;
            uint8_t b[7] = {0,(uint8_t)m,0x80,0xff,0x00,0x80,0};
            for (unsigned family = 0; family < 8; ++family)
                for (unsigned form = 0; form < 4; ++form) {
                    b[0] = (uint8_t)(8*family+form);
                    int refused = mod == 3 || (family != 7 && form == 1 && odd);
                    check_memory(b,size,0x120000+odd,refused ? 0 : costs[family][form],ea,
                        (form%2 && odd) ? 2 : 0);
                }
            const uint8_t immediates[] = {0x80,0x81,0x83};
            for (unsigned i = 0; i < 3; ++i) {
                b[0] = immediates[i];
                int refused = mod == 3 || (i == 2 && (group == 1 || group == 4 || group == 6)) ||
                    (i != 0 && odd && group != 7);
                check_memory(b,size+(i == 1 ? 2 : 1),0x120000+odd,
                    refused ? 0 : group == 7 ? 6 : 7,ea,(i != 0 && odd) ? 2 : 0);
            }
            for (unsigned width = 0; width < 2; ++width) {
                b[0] = (uint8_t)(0x84+width);
                check_memory(b,size,0x120000+odd,mod != 3 ? 6 : 0,ea,width && odd ? 2 : 0);
                b[0] = (uint8_t)(0xf6+width);
                check_memory(b,size+1+width,0x120000+odd,
                    mod != 3 && group == 0 ? 6 : 0,ea,width && odd ? 2 : 0);
            }
        }
}

static void test_memory_unary_multiply_exchange(void)
{
    static const unsigned f6_cost[] = {6,0,7,7,16,16,17,20};
    static const unsigned f7_cost[] = {6,0,7,7,24,24,25,28};
    const uint8_t opcodes[] = {0xf6,0xf7,0xfe,0xff,0x86,0x87,0x69,0x6b};
    for (unsigned op = 0; op < sizeof(opcodes); ++op)
        for (unsigned m = 0; m < 256; ++m)
            for (unsigned parity = 0; parity < 2; ++parity)
                for (unsigned fill = 0; fill < 3; ++fill) {
                    unsigned mod = m/64, rm = m%8, group = (m/8)%8;
                    uint8_t b[7]; memset(b,fill == 0 ? 0 : fill == 1 ? 0x80 : 0xff,sizeof(b));
                    b[0] = opcodes[op]; b[1] = (uint8_t)m;
                    size_t size = 2 + (mod == 1 ? 1 : mod == 2 || (mod == 0 && rm == 6) ? 2 : 0);
                    unsigned base, word = op == 1 || op == 3 || op >= 5;
                    int rmw;
                    if (op < 2) {
                        base = op == 0 ? f6_cost[group] : f7_cost[group];
                        rmw = group == 2 || group == 3;
                        if (group == 0) size += op+1;
                    } else if (op < 4) {
                        base = group <= 1 ? 7 : 0; rmw = 1;
                    } else if (op < 6) {
                        base = 5; rmw = 1;
                    } else {
                        base = 24; rmw = 0; size += op == 6 ? 2 : 1;
                    }
                    if (mod == 3 || (rmw && word && parity)) base = 0;
                    check_memory(b,size,0x123400+parity,base,
                        (mod == 1 || mod == 2) && rm < 4,word && parity ? 2 : 0);
                }
}

static void test_memory_shifts(void)
{
    const uint8_t ops[] = {0xc0,0xc1,0xd0,0xd1,0xd2,0xd3};
    const bm_286_nominal_memory_cost_t sentinel = {101,102,103,104};
    for (unsigned op = 0; op < sizeof(ops); ++op)
        for (unsigned m = 0; m < 256; ++m)
            for (unsigned count = 0; count < 256; ++count)
                for (unsigned odd = 0; odd < 2; ++odd) {
                    unsigned mod = m/64, rm = m%8;
                    size_t size = 2 + (mod == 1 ? 1 : mod == 2 || (mod == 0 && rm == 6) ? 2 : 0);
                    uint8_t b[7] = {ops[op],(uint8_t)m,0xa5,0x5a,0,0,0};
                    if (op < 2) b[size++] = (uint8_t)count;
                    uint8_t saved[7]; memcpy(saved,b,sizeof(b));
                    int supported = mod != 3 && (m/8)%8 != 6 && !(op%2 && odd);
                    unsigned base = op == 2 || op == 3 ? 7 : 8+count%32;
                    unsigned ea = (mod == 1 || mod == 2) && rm < 4;
                    bm_286_nominal_memory_cost_t result = sentinel;
                    assert(bm_286_nominal_memory_clocks_with_cl(b,size,0x120000+odd,
                        (uint8_t)count,&result) == (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
                    if (supported) {
                        assert(result.instruction_clocks == base && result.addressing_clocks == ea);
                        assert(result.alignment_clocks == 0 && result.total_clocks == base+ea);
                    } else assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
                    assert(memcmp(saved,b,sizeof(b)) == 0);
                    if (op < 4) {
                        check_memory(b,size,0x120000+odd,supported ? base : 0,ea,0);
                        bm_286_nominal_memory_cost_t other = sentinel;
                        assert(bm_286_nominal_memory_clocks_with_cl(b,size,0x120000+odd,
                            (uint8_t)(255-count),&other) == (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
                        assert(memcmp(&result,&other,sizeof(result)) == 0);
                    } else {
                        check_memory(b,size,0x120000+odd,0,0,0); /* Missing CL. */
                        for (size_t n = 1; n <= size+1; ++n) {
                            if (n == size) continue;
                            result = sentinel;
                            assert(bm_286_nominal_memory_clocks_with_cl(b,n,0x120000+odd,
                                (uint8_t)count,&result) == BM_STATUS_UNSUPPORTED);
                            assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
                        }
                    }
                }
    const uint8_t b[] = {0xd3,0x20};
    bm_286_nominal_memory_cost_t result = sentinel;
    assert(bm_286_nominal_memory_clocks_with_cl(NULL,2,0,1,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks_with_cl(b,0,0,1,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks_with_cl(b,2,0,1,NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_memory_clocks_with_cl(b,2,0x1000000,1,&result) == BM_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
    const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e,0xf0,0xf2,0xf3};
    for (unsigned p = 0; p < sizeof(prefixes); ++p) {
        uint8_t prefixed[] = {prefixes[p],0xd3,0x20};
        assert(bm_286_nominal_memory_clocks_with_cl(prefixed,3,0,1,&result) == BM_STATUS_UNSUPPORTED);
        assert(memcmp(&result,&sentinel,sizeof(result)) == 0);
    }
}

static void test_segment_bases(void)
{
    static const unsigned sizes[4][8] = {
        {2,2,2,2,2,2,4,2}, {3,3,3,3,3,3,3,3},
        {4,4,4,4,4,4,4,4}, {2,2,2,2,2,2,2,2}
    };
    const uint8_t prefixes[] = {0x26,0x2e,0x36,0x3e,0xf0,0xf2,0xf3};
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned m = 0; m < 256; ++m)
            for (unsigned mode = 0; mode < 2; ++mode) {
                uint8_t b[6] = {(uint8_t)(load ? 0x8e : 0x8c),(uint8_t)m,0x80,0xff,0x90,0};
                uint8_t saved[6]; memcpy(saved,b,sizeof(b));
                unsigned segment = (m/8)%8, size = sizes[m/64][m%8];
                int supported = segment < 4 && !(load && segment == 1);
                uint64_t expected = !load ? (m >= 192 ? 2 : 3) :
                    mode ? (m >= 192 ? 17 : 19) : (m >= 192 ? 2 : 5);
                uint64_t value = UINT64_MAX;
                assert(bm_286_nominal_segment_base_clocks(b,size,(bm_286_timing_mode_t)mode,&value) ==
                    (supported ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
                assert(value == (supported ? expected : UINT64_MAX));
                assert(memcmp(saved,b,sizeof(b)) == 0);
                for (unsigned n = 1; n <= size+1; ++n) {
                    if (n == size) continue;
                    value = UINT64_MAX;
                    assert(bm_286_nominal_segment_base_clocks(b,n,(bm_286_timing_mode_t)mode,&value) == BM_STATUS_UNSUPPORTED);
                    assert(value == UINT64_MAX);
                }
                for (unsigned p = 0; p < sizeof(prefixes); ++p) {
                    uint8_t prefixed[6]; prefixed[0] = prefixes[p]; memcpy(prefixed+1,b,size);
                    value = UINT64_MAX;
                    assert(bm_286_nominal_segment_base_clocks(prefixed,size+1,(bm_286_timing_mode_t)mode,&value) == BM_STATUS_UNSUPPORTED);
                    assert(value == UINT64_MAX);
                }
                check(b,size,0); /* No mode guessed by the old API. */
            }
    const uint8_t b[] = {0x8e,0xd8}, other[] = {0x89,0xd8};
    uint64_t value = UINT64_MAX;
    assert(bm_286_nominal_segment_base_clocks(NULL,2,BM_286_TIMING_REAL,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_segment_base_clocks(b,0,BM_286_TIMING_REAL,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_segment_base_clocks(b,2,BM_286_TIMING_REAL,NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_segment_base_clocks(b,2,(bm_286_timing_mode_t)2,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_segment_base_clocks(other,2,BM_286_TIMING_REAL,&value) == BM_STATUS_UNSUPPORTED);
    assert(value == UINT64_MAX);
}

int main(void)
{
    /* Independently transcribed from Intel 210498-005 Appendix B. */
    static const struct { uint8_t opcode; uint64_t clocks; } cases[] = {
        {0x98,2}, {0x99,2}, {0x90,3}, {0xf8,2}, {0xfc,2},
        {0xfa,3}, {0xf5,2}, {0xf9,2}, {0xfd,2}, {0xfb,2},
        {0x27,3}, {0x2f,3}, {0x37,3}, {0x3f,3}
    };
    uint64_t value = UINT64_MAX;
    uint8_t bytes[2] = {0x90,0x90};
    assert(bm_286_nominal_instruction_clocks(NULL,1,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_instruction_clocks(bytes,0,&value) == BM_STATUS_INVALID_ARGUMENT);
    assert(bm_286_nominal_instruction_clocks(bytes,1,NULL) == BM_STATUS_INVALID_ARGUMENT);
    assert(value == UINT64_MAX);
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        uint64_t expected = 0;
        bytes[0] = (uint8_t)opcode;
        for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
            if (cases[i].opcode == opcode) expected = cases[i].clocks;
        if (opcode >= 0x40 && opcode <= 0x4f) expected = 2;
        if (opcode >= 0x90 && opcode <= 0x97) expected = 3;
        value = UINT64_MAX;
        bm_status_t status = bm_286_nominal_instruction_clocks(bytes,1,&value);
        assert(status == (expected ? BM_STATUS_OK : BM_STATUS_UNSUPPORTED));
        assert(value == (expected ? expected : UINT64_MAX));
        assert(bytes[0] == opcode);
        check(bytes,1,expected);
    }
    test_register_and_immediate_forms();
    test_shift_counts();
    test_multiply_immediate_and_adjust();
    test_memory_mov();
    test_memory_arithmetic();
    test_memory_unary_multiply_exchange();
    test_memory_shifts();
    test_segment_bases();
    return 0;
}
