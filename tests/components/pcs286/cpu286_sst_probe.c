/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Authored private test transport, not part of the runtime ABI.
 * Input: 14 LE16 registers, LE32 RAM count, (LE32 address, byte) entries.
 * Output: LE32 status, 14 LE16 registers, LE32 write count, same RAM entries.
 */
#include <blumach/components/cpu_80286.h>
#include <blumach/platforms/null_host.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

enum { RAM_SIZE = 0x1000000, MAX_ENTRIES = 65536 };
typedef struct probe {
    unsigned char *ram;
    unsigned *generation;
    unsigned current, writes, addresses[MAX_ENTRIES];
} probe_t;

static int read_number(unsigned bytes, unsigned *value)
{
    unsigned i;
    *value = 0U;
    for (i = 0U; i < bytes; ++i) {
        int c = getchar();
        if (c == EOF) return 0;
        *value |= (unsigned) c << (8U * i);
    }
    return 1;
}

static void write_number(unsigned value, unsigned bytes)
{
    unsigned i;
    for (i = 0U; i < bytes; ++i)
        if (putchar((int) ((value >> (8U * i)) & 255U)) == EOF) exit(2);
}

static bm_status_t access_bus(void *context, bm_bus_transaction_t *t)
{
    probe_t *p = context;
    unsigned i;
    if (t->size != 1U && t->size != 2U) return BM_STATUS_DEVICE_ERROR;
    if (t->space == BM_ADDRESS_IO) return BM_STATUS_UNSUPPORTED;
    if (t->address > RAM_SIZE - t->size) return BM_STATUS_DEVICE_ERROR;
    if (t->operation != BM_BUS_WRITE) t->value = 0U;
    for (i = 0U; i < t->size; ++i) {
        unsigned a = (unsigned) t->address + i;
        if (t->operation == BM_BUS_WRITE) {
            if (p->writes == MAX_ENTRIES) return BM_STATUS_DEVICE_ERROR;
            p->ram[a] = (unsigned char) (t->value >> (8U * i));
            p->generation[a] = p->current;
            p->addresses[p->writes++] = a;
        } else {
            /* Missing input bytes are a transport/evidence error, not zero RAM. */
            if (p->generation[a] != p->current) return BM_STATUS_DEVICE_ERROR;
            t->value |= (uint64_t) p->ram[a] << (8U * i);
        }
    }
    return BM_STATUS_OK;
}

static void set_segment(bm_286_segment_state_t *s, unsigned value)
{
    s->selector = (uint16_t) value;
    s->base = value << 4;
    s->limit = 0xffffU;
    s->valid = 1U;
}

int main(void)
{
    probe_t p = {0};
    bm_host_services_t host = bm_null_host_services();
    bm_286_config_t config = {0};
    bm_cpu_t cpu;
    unsigned first;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    p.ram = calloc(RAM_SIZE, 1U);
    p.generation = calloc(RAM_SIZE, sizeof(*p.generation));
    if (p.ram == NULL || p.generation == NULL) return 2;
    config.size = sizeof(config); config.version = BM_286_CONTRACT_VERSION;
    config.access = access_bus; config.access_context = &p;
    if (bm_286_create(&host, &config, &cpu) != BM_STATUS_OK) return 2;
    while ((first = (unsigned) getchar()) != (unsigned) EOF) {
        unsigned r[14], count, i, high;
        bm_286_arch_state_t s;
        bm_286_boundary_t boundary;
        bm_status_t status;
        if (!read_number(1U, &high)) return 2;
        r[0] = first | (high << 8);
        for (i = 1U; i < 14U; ++i)
            if (!read_number(2U, &r[i])) return 2;
        if (!read_number(4U, &count) || count > MAX_ENTRIES) return 2;
        if (++p.current == 0U) return 2;
        p.writes = 0U;
        for (i = 0U; i < count; ++i) {
            unsigned a, v;
            if (!read_number(4U, &a) || a >= RAM_SIZE || !read_number(1U, &v)) return 2;
            p.ram[a] = (unsigned char) v; p.generation[a] = p.current;
        }
        if (cpu.ops.reset(cpu.context) != BM_STATUS_OK ||
            bm_286_get_arch_state(&cpu, &s) != BM_STATUS_OK) return 2;
        s.ax = (uint16_t) r[0]; s.bx = (uint16_t) r[1];
        s.cx = (uint16_t) r[2]; s.dx = (uint16_t) r[3];
        set_segment(&s.cs, r[4]); set_segment(&s.ss, r[5]);
        set_segment(&s.ds, r[6]); set_segment(&s.es, r[7]);
        s.sp = (uint16_t) r[8]; s.bp = (uint16_t) r[9];
        s.si = (uint16_t) r[10]; s.di = (uint16_t) r[11];
        s.ip = (uint16_t) r[12]; s.flags = (uint16_t) r[13];
        status = bm_286_set_arch_state(&cpu, &s);
        if (status == BM_STATUS_OK) status = bm_286_step(&cpu, &boundary);
        if (bm_286_get_arch_state(&cpu, &s) != BM_STATUS_OK) return 2;
        r[0]=s.ax; r[1]=s.bx; r[2]=s.cx; r[3]=s.dx;
        r[4]=s.cs.selector; r[5]=s.ss.selector; r[6]=s.ds.selector; r[7]=s.es.selector;
        r[8]=s.sp; r[9]=s.bp; r[10]=s.si; r[11]=s.di; r[12]=s.ip; r[13]=s.flags;
        write_number((unsigned) status, 4U);
        for (i = 0U; i < 14U; ++i) write_number(r[i], 2U);
        write_number(p.writes, 4U);
        for (i = 0U; i < p.writes; ++i) {
            write_number(p.addresses[i], 4U); write_number(p.ram[p.addresses[i]], 1U);
        }
    }
    cpu.ops.destroy(cpu.context);
    free(p.ram); free(p.generation);
    return ferror(stdin) ? 2 : 0;
}
