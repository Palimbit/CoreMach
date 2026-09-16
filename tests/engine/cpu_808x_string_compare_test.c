/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/components/bus.h>
#include <blumach/components/cpu_808x.h>
#include <blumach/components/linear_memory.h>
#include <blumach/engine/engine.h>
#include <blumach/platforms/null_host.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_ZF 0x0040U

typedef struct cpu_result {
    uint64_t cx;
    uint64_t si;
    uint64_t di;
    uint64_t flags;
    uint64_t halted;
    bm_808x_trace_t prefixed_trace;
    int saw_prefixed_trace;
} cpu_result_t;

static void
capture_trace(void *context, const bm_808x_trace_t *trace)
{
    cpu_result_t *result = context;
    if (trace->prefix_count != 0U) {
        result->prefixed_trace = *trace;
        result->saw_prefixed_trace = 1;
    }
}

static uint64_t
inspect(bm_engine_t *engine, const char *name)
{
    uint64_t value = UINT64_MAX;
    assert(bm_engine_inspect_cpu(engine, 0U, name, &value) == BM_STATUS_OK);
    return value;
}

static cpu_result_t
run_program(uint8_t *image, const uint8_t *program, size_t program_size)
{
    bm_host_services_t host = bm_null_host_services();
    bm_engine_config_t engine_config = { 1U, 1U };
    bm_linear_memory_config_t memory_config;
    bm_808x_config_t cpu_config;
    bm_engine_t *engine = NULL;
    bm_bus_t *bus = NULL;
    bm_linear_memory_t *memory = NULL;
    bm_cpu_t cpu;
    cpu_result_t result = { 0 };

    memcpy(image + 0xf0000U, program, program_size);
    image[0xffff0U] = 0xeaU;
    image[0xffff1U] = 0x00U;
    image[0xffff2U] = 0x00U;
    image[0xffff3U] = 0x00U;
    image[0xffff4U] = 0xf0U;
    assert(bm_bus_create(&host, 1U, &bus) == BM_STATUS_OK);
    memory_config = (bm_linear_memory_config_t) {
        BM_ADDRESS_MEMORY, 0U, 0x100000U, 0, image, 0x100000U
    };
    assert(bm_linear_memory_create(&host, bus, &memory_config, &memory) ==
           BM_STATUS_OK);
    assert(bm_engine_create(&host, &engine_config, &engine) == BM_STATUS_OK);
    cpu_config = (bm_808x_config_t) {
        BM_808X_NEC_V30, 10000000U, bus, capture_trace, &result, NULL, NULL
    };
    assert(bm_808x_create(&host, &cpu_config, &cpu) == BM_STATUS_OK);
    assert(bm_engine_add_cpu(engine, &cpu, NULL) == BM_STATUS_OK);
    assert(bm_engine_reset(engine) == BM_STATUS_OK);
    assert(bm_engine_run_for(engine, 64U) == BM_STATUS_OK);
    result.cx = inspect(engine, "cx");
    result.si = inspect(engine, "si");
    result.di = inspect(engine, "di");
    result.flags = inspect(engine, "flags");
    result.halted = inspect(engine, "halted");

    bm_engine_destroy(engine);
    bm_linear_memory_destroy(memory);
    bm_bus_destroy(bus);
    return result;
}

static void
test_repe_stops_on_mismatch(void)
{
    static const uint8_t program[] = {
        0xbe, 0x00, 0x01,       /* MOV SI,0100h. */
        0xbf, 0x00, 0x02,       /* MOV DI,0200h. */
        0xb9, 0x04, 0x00,       /* MOV CX,4. */
        0xf3, 0xa6,             /* REPE CMPSB. */
        0xf4                    /* HLT. */
    };
    uint8_t *image = calloc(1U, 0x100000U);
    cpu_result_t result;

    assert(image != NULL);
    image[0x0100U] = 0x11U;
    image[0x0101U] = 0x22U;
    image[0x0102U] = 0x33U;
    image[0x0200U] = 0x11U;
    image[0x0201U] = 0x22U;
    image[0x0202U] = 0x44U;
    result = run_program(image, program, sizeof(program));
    assert(result.halted == 1U);
    assert(result.cx == 1U);
    assert(result.si == 0x0103U && result.di == 0x0203U);
    assert((result.flags & FLAG_ZF) == 0U);
    free(image);
}

static void
test_repne_stops_on_equality(void)
{
    static const uint8_t program[] = {
        0xbe, 0x00, 0x01,       /* MOV SI,0100h. */
        0xbf, 0x00, 0x02,       /* MOV DI,0200h. */
        0xb9, 0x03, 0x00,       /* MOV CX,3. */
        0xf2, 0xa6,             /* REPNE CMPSB. */
        0xf4                    /* HLT. */
    };
    uint8_t *image = calloc(1U, 0x100000U);
    cpu_result_t result;

    assert(image != NULL);
    image[0x0100U] = 0x11U;
    image[0x0101U] = 0x22U;
    image[0x0200U] = 0x33U;
    image[0x0201U] = 0x22U;
    result = run_program(image, program, sizeof(program));
    assert(result.halted == 1U);
    assert(result.cx == 1U);
    assert(result.si == 0x0102U && result.di == 0x0202U);
    assert((result.flags & FLAG_ZF) != 0U);
    free(image);
}

static void
test_direction_and_word_width(void)
{
    static const uint8_t reverse_program[] = {
        0xbe, 0x01, 0x01,       /* MOV SI,0101h. */
        0xbf, 0x01, 0x02,       /* MOV DI,0201h. */
        0xb9, 0x02, 0x00,       /* MOV CX,2. */
        0xb8, 0x02, 0x04,       /* MOV AX,0402h: DF and reserved bit. */
        0x50,                   /* PUSH AX. */
        0x9d,                   /* POPF. */
        0xf3, 0xa6,             /* REPE CMPSB backwards. */
        0xf4                    /* HLT. */
    };
    static const uint8_t word_program[] = {
        0xbe, 0x00, 0x01,       /* MOV SI,0100h. */
        0xbf, 0x00, 0x02,       /* MOV DI,0200h. */
        0xa7,                   /* CMPSW. */
        0xf4                    /* HLT. */
    };
    uint8_t *image = calloc(1U, 0x100000U);
    cpu_result_t result;

    assert(image != NULL);
    image[0x0100U] = 0x11U;
    image[0x0101U] = 0x22U;
    image[0x0200U] = 0x11U;
    image[0x0201U] = 0x22U;
    result = run_program(image, reverse_program, sizeof(reverse_program));
    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x00ffU && result.di == 0x01ffU);
    assert((result.flags & FLAG_ZF) != 0U);
    free(image);

    image = calloc(1U, 0x100000U);
    assert(image != NULL);
    image[0x0100U] = 0x34U;
    image[0x0101U] = 0x12U;
    image[0x0200U] = 0x34U;
    image[0x0201U] = 0x12U;
    result = run_program(image, word_program, sizeof(word_program));
    assert(result.halted == 1U);
    assert(result.si == 0x0102U && result.di == 0x0202U);
    assert((result.flags & FLAG_ZF) != 0U);
    free(image);
}

static void
test_source_override_and_zero_count(void)
{
    static const uint8_t override_program[] = {
        0xb8, 0x10, 0x00,       /* MOV AX,0010h. */
        0x8e, 0xd8,             /* MOV DS,AX. */
        0xb8, 0x20, 0x00,       /* MOV AX,0020h. */
        0x8e, 0xc0,             /* MOV ES,AX. */
        0xbe, 0x00, 0x01,       /* MOV SI,0100h. */
        0xbf, 0x00, 0x02,       /* MOV DI,0200h. */
        0xb9, 0x01, 0x00,       /* MOV CX,1. */
        0x2e, 0xf3, 0xa6,       /* CS: REPE CMPSB. */
        0xf4                    /* HLT. */
    };
    static const uint8_t zero_program[] = {
        0xbe, 0x00, 0x01,       /* MOV SI,0100h. */
        0xbf, 0x00, 0x02,       /* MOV DI,0200h. */
        0xb9, 0x00, 0x00,       /* MOV CX,0. */
        0xb8, 0x00, 0x40,       /* MOV AX,4000h. */
        0x9e,                   /* SAHF: set ZF. */
        0xf2, 0xa6,             /* REPNE CMPSB: no operation. */
        0xf4                    /* HLT. */
    };
    uint8_t *image = calloc(1U, 0x100000U);
    cpu_result_t result;

    assert(image != NULL);
    image[0xf0100U] = 0x5aU; /* Source selected by CS override. */
    image[0x0200U] = 0x99U;  /* DS:SI must not be used. */
    image[0x0400U] = 0x5aU;  /* ES:DI destination. */
    image[0xf0200U] = 0x00U; /* Segment override must not affect destination. */
    result = run_program(image, override_program, sizeof(override_program));
    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x0101U && result.di == 0x0201U);
    assert((result.flags & FLAG_ZF) != 0U);
    assert(result.saw_prefixed_trace);
    assert(result.prefixed_trace.opcode == 0x2eU);
    assert(result.prefixed_trace.effective_opcode == 0xa6U);
    assert(result.prefixed_trace.prefix_count == 2U);
    free(image);

    image = calloc(1U, 0x100000U);
    assert(image != NULL);
    result = run_program(image, zero_program, sizeof(zero_program));
    assert(result.halted == 1U);
    assert(result.cx == 0U);
    assert(result.si == 0x0100U && result.di == 0x0200U);
    assert((result.flags & FLAG_ZF) != 0U);
    free(image);
}

int
main(void)
{
    test_repe_stops_on_mismatch();
    test_repne_stops_on_equality();
    test_direction_and_word_width();
    test_source_override_and_zero_count();
    return 0;
}
