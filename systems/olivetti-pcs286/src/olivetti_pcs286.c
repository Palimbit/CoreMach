/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Experimental runtime binding of the shared, provisional diagnostic profile.
 */
#include "composition.h"
#include <string.h>

typedef struct machine {
    bm_host_services_t host;
    bm_pcs286_config_t config;
    bm_pcs286_composition_t board;
    bm_status_t failure;
    uint8_t cmos[128];
    uint64_t ns_remainder, manual_steps, fallback_steps;
} machine_t;

static bm_status_t validate(const bm_configuration_view_t *view)
{
    const bm_pcs286_config_t *c;
    if (!view || !view->type || strcmp(view->type,BM_PCS286_CONFIG_TYPE) ||
        view->version!=BM_PCS286_CONFIG_VERSION || view->size!=sizeof(*c) || !view->data)
        return BM_STATUS_INVALID_ARGUMENT;
    c=view->data;
    if (c->size!=sizeof(*c) || c->version!=BM_PCS286_CONFIG_VERSION ||
        c->ram_kib!=1024 || c->firmware.layout!=BM_PCS286_FIRMWARE_COMBINED ||
        !c->firmware.image[0].data || c->firmware.image[0].size!=BM_PCS286_FIRMWARE_BYTES ||
        c->firmware.image[1].data || c->firmware.image[1].size ||
        (c->initial_cmos ? c->initial_cmos_size!=128 : c->initial_cmos_size!=0))
        return BM_STATUS_INVALID_ARGUMENT;
    /* Only the currently exercised diagnostic board profile is exposed. */
    if (c->floppy[1].installed || c->hard_disk[0].installed || c->hard_disk[1].installed || c->cpu_trace)
        return BM_STATUS_UNSUPPORTED;
    return BM_STATUS_OK;
}

static bm_status_t initialize(machine_t *m)
{
    bm_status_t s=bm_pcs286_composition_initialize(&m->board,&m->host,
        m->config.firmware.image[0].data,1,1,1,2,m->cmos,1,NULL,0,&m->config.floppy[0]);
    /* Explicit diagnostic fallback: old 1 us boundary at nominal 12 MHz.
     * This is NOT a documented instruction or DMA duration. */
    m->board.control.provisional_fallback=12;
    m->ns_remainder=m->manual_steps=m->fallback_steps=0;
    return s;
}

static bm_status_t fire(bm_engine_t *engine, void *context,
    const bm_time_point_t *when, uint64_t *next)
{
    machine_t *m=context;
    bm_pcs286_services_step_t event={0};
    uint64_t clocks, elapsed=12, ns;
    bm_status_t s;
    (void)engine; (void)when;
    *next=0;
    if (m->failure!=BM_STATUS_OK) return m->failure;
    s=bm_pcs286_services_step(m->board.services,&event);
    if (s!=BM_STATUS_OK && s!=BM_STATUS_IDLE) goto failed;
    if (event.cpu_completed && event.cpu.estimate.estimated_clocks)
        elapsed=event.cpu.estimate.estimated_clocks;
    if (event.cpu_completed && event.cpu.estimate.source==BM_286_ESTIMATE_MANUAL)
        ++m->manual_steps;
    else ++m->fallback_steps;
    s=bm_pcs286_dma_coordinator_step(&m->board.dma_coordinator,&clocks);
    if (s!=BM_STATUS_OK && s!=BM_STATUS_IDLE) goto failed;
    /* DMA/refresh retain a diagnostic minimum quantum; their reported clock
     * units are not assumed to be CPU clocks. CPU HOLD prevents instructions
     * during granted DMA. No unqualified DMA wait count is added as MHz. */
    if ((clocks || event.refresh_completed) && elapsed<12) elapsed=12;
    if (elapsed>(UINT64_MAX-m->ns_remainder)/250) {
        s=BM_STATUS_INVALID_STATE; goto failed;
    }
    ns=(elapsed*250+m->ns_remainder)/3;
    m->ns_remainder=(elapsed*250+m->ns_remainder)%3;
    s=bm_pcs286_services_advance(m->board.services,ns);
    if (s!=BM_STATUS_OK) goto failed;
    s=bm_pvga1a_advance_ns(m->board.video,ns);
    if (s!=BM_STATUS_OK) goto failed;
    m->board.video_ns+=ns; ++m->board.step;
    *next=elapsed;
    return BM_STATUS_OK;
failed:
    m->failure=s; return s;
}

static void destroy(void *context)
{
    machine_t *m=context;
    if (!m) return;
    bm_pcs286_composition_destroy(&m->board);
    m->host.release(m->host.context,m);
}

static bm_status_t create(bm_engine_t *engine,const bm_host_services_t *host,
    const bm_configuration_view_t *view,void **out)
{
    machine_t *m;
    bm_timed_source_id_t id;
    const bm_clock_rate_t provisional={12000000,1};
    bm_status_t s=validate(view);
    if (!out || !host || !engine) return BM_STATUS_INVALID_ARGUMENT;
    *out=NULL;
    if (s!=BM_STATUS_OK) return s;
    m=host->allocate(host->context,sizeof(*m));
    if (!m) return BM_STATUS_OUT_OF_MEMORY;
    memset(m,0,sizeof(*m)); m->host=*host; m->config=*(const bm_pcs286_config_t *)view->data;
    if (m->config.initial_cmos) memcpy(m->cmos,m->config.initial_cmos,128);
    else {
        /* Diagnostic calendar, NOT factory CMOS/checksum or a captured dump. */
        m->cmos[4]=1; m->cmos[6]=3; m->cmos[7]=1; m->cmos[8]=1;
        m->cmos[9]=0x80; m->cmos[10]=0x60; m->cmos[11]=0x82;
    }
    *out=m; /* Runtime destroys engine before this partially initialized owner. */
    s=initialize(m);
    if (s==BM_STATUS_OK) s=bm_engine_add_timed_source(engine,fire,m,&provisional,12,&id);
    m->failure=s;
    return s;
}

static bm_status_t reset(void *context)
{
    machine_t *m=context;
    bm_at_rtc_state_t rtc;
    bm_status_t s;
    if (!m) return BM_STATUS_INVALID_ARGUMENT;
    s=bm_pcs286_services_inspect_rtc(m->board.services,&rtc,m->cmos,sizeof(m->cmos));
    if (s!=BM_STATUS_OK) return s;
    /* Emulator cold reset, not the KBC CPU-only reset: rebuild board, retain CMOS. */
    bm_pcs286_composition_destroy(&m->board);
    memset(&m->board,0,sizeof(m->board));
    m->failure=initialize(m);
    return m->failure;
}
static bm_status_t input(void *context,const bm_input_event_t *event)
{
    machine_t *m=context;
    if (m->failure!=BM_STATUS_OK) return m->failure;
    return bm_pcs286_services_input(m->board.services,event);
}
static bm_status_t geometry(const void *context,bm_video_geometry_t *out)
{ return bm_pvga1a_video_geometry(((const machine_t *)context)->board.video,out); }
static bm_status_t render(const void *context,bm_tick_t now,bm_video_framebuffer_t *out)
{
    const machine_t *m=context;
    (void)now;
    return bm_pvga1a_render(m->board.video,m->board.video_ns,1000000000,out);
}
static bm_status_t inspect(const void *context,const char *name,uint64_t *value)
{
    const machine_t *m=context;
    if (!name || !value) return BM_STATUS_INVALID_ARGUMENT;
    if (!strcmp(name,"timing_provisional")) { *value=1; return BM_STATUS_OK; }
    if (!strcmp(name,"timing_manual_steps")) { *value=m->manual_steps; return BM_STATUS_OK; }
    if (!strcmp(name,"timing_fallback_steps")) { *value=m->fallback_steps; return BM_STATUS_OK; }
    if (!strcmp(name,"peripheral_ns")) { *value=m->board.video_ns; return BM_STATUS_OK; }
    if (!strcmp(name,"boundaries")) { *value=m->board.step; return BM_STATUS_OK; }
    if (!strcmp(name,"memory_calls")) { *value=m->board.memory_calls; return BM_STATUS_OK; }
    if (!strcmp(name,"io_calls")) { *value=m->board.io_count; return BM_STATUS_OK; }
    return m->board.cpu.ops.inspect(m->board.cpu.context,name,value);
}
static bm_status_t state_size(const void *context,const char *name,size_t *size)
{
    (void)context;
    if (!name || !size) return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name,"rtc")) return BM_STATUS_UNSUPPORTED;
    *size=128; return BM_STATUS_OK;
}
static bm_status_t save_state(const void *context,const char *name,uint8_t *data,size_t size)
{
    const machine_t *m=context;
    bm_at_rtc_state_t rtc;
    if (!name || strcmp(name,"rtc") || !data || size!=128) return BM_STATUS_INVALID_ARGUMENT;
    return bm_pcs286_services_inspect_rtc(m->board.services,&rtc,data,size);
}
static size_t storage_count(const void *context) { (void)context; return 1; }
static bm_status_t storage_status(const void *context,size_t index,bm_storage_device_status_t *out)
{
    const machine_t *m=context;
    bm_floppy_drive_state_t s;
    bm_status_t status;
    if (index || !out) return BM_STATUS_INVALID_ARGUMENT;
    status=bm_floppy_drive_state(m->board.floppy,&s);
    if (status!=BM_STATUS_OK) return status;
    memset(out,0,sizeof(*out)); out->kind=BM_STORAGE_DEVICE_FLOPPY;
    out->installed=s.installed; out->media_present=s.media_present;
    out->write_protected=s.write_protected; out->read_operations=s.read_operations;
    out->write_operations=s.write_operations; return BM_STATUS_OK;
}
static const bm_machine_definition_t definition={
 .id="olivetti-pcs286-experimental",
 .scheduler_ticks_per_second=BM_MACHINE_CLOCKED_TICKS_PER_SECOND,
 .engine_mode=BM_MACHINE_ENGINE_CLOCKED,
 .configuration={BM_PCS286_CONFIG_TYPE,BM_PCS286_CONFIG_VERSION,sizeof(bm_pcs286_config_t)},
 .ops={.validate=validate,.create=create,.destroy=destroy,.reset=reset,.input=input,
       .inspect=inspect,.video_geometry=geometry,.video_render=render,
       .persistent_state_size=state_size,.save_persistent_state=save_state,
       .storage_count=storage_count,.storage_status=storage_status},
 .engine={1,8,1}
};
const bm_machine_definition_t *bm_pcs286_machine_definition(void) { return &definition; }
bm_machine_config_t bm_pcs286_machine_config(const bm_pcs286_config_t *c)
{
    bm_machine_config_t result={&definition,{BM_PCS286_CONFIG_TYPE,BM_PCS286_CONFIG_VERSION,sizeof(*c),c}};
    return result;
}
