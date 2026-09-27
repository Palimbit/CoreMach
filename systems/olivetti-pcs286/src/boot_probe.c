/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Bounded local diagnostic runner composing existing portable board components.
 * External firmware only; no BIOS-specific fixups or public machine factory.
 */
#include "board_services.h"
#include "board_io.h"
#include "dma_coordinator.h"
#include "page_spare_latches.h"
#include <blumach/components/lpt_spp.h>
#include <blumach/components/pvga1a.h>
#include <blumach/components/wd37c65.h>
#include <blumach/platforms/null_host.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACE_SIZE 64U
#define ACTION_LIMIT 32U
#define FLOPPY_1440_BYTES 1474560U
typedef struct action {
    uint64_t step;
    const char *path; /* Non-NULL means capture before any keys at this step. */
    bm_input_event_t key;
} action_t;
typedef struct io_event {
    uint64_t step, address, value;
    uint32_t pc, size;
    int op, status;
} io_event_t;
typedef struct cpu_event {
    uint64_t step;
    bm_286_boundary_t b;
} cpu_event_t;
typedef struct video_status_site {
    uint32_t pc;
    uint64_t port, reads, first_step, last_step, transitions;
    uint8_t first_value, last_value, value_or, value_and;
    uint8_t sequencer1, crtc0, crtc1, crtc6, crtc7, crtc10, crtc11, crtc12;
    uint8_t attribute0, attribute10, attribute12, attribute14, attribute15;
} video_status_site_t;
typedef struct probe {
    bm_pcs286_control_t control;
    bm_gc103_memory_t routes;
    bm_headland_at_memory_t memory;
    bm_pcs286_io_t io;
    bm_ioc02_legacy_registers_t ioc;
    bm_pcs286_memory_t *bytes;
    bm_cpu_t cpu;
    bm_at_bus_t *bus;
    bm_at_pic_t *pic;
    bm_at_dma_t *dma;
    bm_pcs286_dma_coordinator_t dma_coordinator;
    bm_wd37c65_t *fdc;
    bm_floppy_drive_t *floppy;
    const uint8_t *floppy_image;
    size_t floppy_size;
    bm_lpt_spp_t *lpt;
    bm_bus_t *video_bus;
    bm_pvga1a_t *video;
    uint64_t video_ns;
    bm_pcs286_services_t *services;
    bm_pcs286_page_spare_latches_t page_spares;
    uint64_t step, boundaries, io_count, memory_calls, post_count;
    uint32_t pc, first_fetch;
    int fetched, last_post;
    io_event_t first_io[TRACE_SIZE], recent_io[TRACE_SIZE], failed_access;
    io_event_t kbc_io[TRACE_SIZE];
    io_event_t video_status_io[TRACE_SIZE];
    io_event_t pit_io[TRACE_SIZE];
    io_event_t board_diagnostic_io[TRACE_SIZE];
    uint64_t kbc_count, video_status_count, pit_count, board_diagnostic_count;
    video_status_site_t video_status_sites[TRACE_SIZE];
    unsigned video_status_site_count;
    uint64_t video_status_site_overflow;
    cpu_event_t recent_cpu[TRACE_SIZE];
} probe_t;

static bm_status_t video_io(void *context, bm_bus_transaction_t *t)
{
    return bm_bus_transact(((probe_t *)context)->video_bus,t);
}
static bm_status_t external_memory(void *context, bm_bus_transaction_t *t)
{
    probe_t *p=context;
    if (t->address>=0xa0000U && t->address<=0xbffffU) {
        bm_bus_transaction_t local=*t;
        bm_status_t s;
        local.space=BM_ADDRESS_MEMORY;
        s=bm_bus_transact(p->video_bus,&local);
        if (s==BM_STATUS_OK) { t->value=local.value; t->wait_states=local.wait_states; }
        return s;
    }
    /* Explicit unpopulated external space; installed video errors never fall
     * through to this policy. Headland sends native one-byte fragments. */
    if (t->operation!=BM_BUS_WRITE) t->value=0xffU;
    return BM_STATUS_OK;
}

static bm_status_t memory_access(void *context, bm_at_transfer_t *t)
{
    probe_t *p=context;
    bm_status_t s;
    ++p->memory_calls;
    if (!p->fetched && t->bus.operation==BM_BUS_FETCH) {
        p->first_fetch=(uint32_t)t->bus.address; p->fetched=1;
    }
    s=bm_headland_at_memory_access(&p->memory,t);
    if (t->bus.operation==BM_BUS_WRITE && t->bus.address<=0x48aU &&
        t->bus.address+t->bus.size>0x487U)
        p->board_diagnostic_io[p->board_diagnostic_count++%TRACE_SIZE]=
            (io_event_t){p->step,t->bus.address,t->bus.value,p->pc,t->bus.size,
                         t->bus.operation,s};
    if (s!=BM_STATUS_OK)
        p->failed_access=(io_event_t){p->step,t->bus.address,t->bus.value,p->pc,t->bus.size,t->bus.operation,s};
    return s;
}
static bm_status_t dma_memory_access(void *context, bm_at_transfer_t *t)
{
    probe_t *p=context;
    return p->bus ? bm_at_bus_access(p->bus,t) : BM_STATUS_INVALID_STATE;
}
static bm_status_t io_access(void *context, bm_at_transfer_t *t)
{
    probe_t *p=context;
    bm_status_t s=bm_pcs286_io_access(&p->io,t);
    p->recent_io[p->io_count++%TRACE_SIZE]=
        (io_event_t){p->step,t->bus.address,t->bus.value,p->pc,t->bus.size,t->bus.operation,s};
    if (p->io_count<=TRACE_SIZE) p->first_io[p->io_count-1U]=p->recent_io[p->io_count-1U];
    if (s!=BM_STATUS_OK) p->failed_access=p->recent_io[(p->io_count-1U)%TRACE_SIZE];
    /* Retain actual data reads/writes and command writes despite long status
     * polling loops. Observational only; no extra device transaction. */
    if (t->bus.address==0x60U || (t->bus.address==0x64U && t->bus.operation==BM_BUS_WRITE))
        p->kbc_io[p->kbc_count++%TRACE_SIZE]=p->recent_io[(p->io_count-1U)%TRACE_SIZE];
    if ((t->bus.address==0x3baU || t->bus.address==0x3daU) &&
        t->bus.operation==BM_BUS_READ) {
        unsigned n;
        uint8_t value=(uint8_t)t->bus.value;
        p->video_status_io[p->video_status_count++%TRACE_SIZE]=
            p->recent_io[(p->io_count-1U)%TRACE_SIZE];
        for (n=0;n<p->video_status_site_count;++n)
            if (p->video_status_sites[n].pc==p->pc &&
                p->video_status_sites[n].port==t->bus.address) break;
        if (n==p->video_status_site_count) {
            if (n==TRACE_SIZE) ++p->video_status_site_overflow;
            else {
                video_status_site_t *site=&p->video_status_sites[p->video_status_site_count++];
                *site=(video_status_site_t){0};
                site->pc=p->pc; site->port=t->bus.address; site->reads=1;
                site->first_step=site->last_step=p->step;
                site->first_value=site->last_value=site->value_or=site->value_and=value;
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_SEQUENCER,1,&site->sequencer1);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,0,&site->crtc0);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,1,&site->crtc1);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,6,&site->crtc6);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,7,&site->crtc7);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,0x10,&site->crtc10);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,0x11,&site->crtc11);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,0x12,&site->crtc12);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_ATTRIBUTE,0,&site->attribute0);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_ATTRIBUTE,0x10,&site->attribute10);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_ATTRIBUTE,0x12,&site->attribute12);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_ATTRIBUTE,0x14,&site->attribute14);
                (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_ATTRIBUTE,0x0f,&site->attribute15);
            }
        } else {
            video_status_site_t *site=&p->video_status_sites[n];
            ++site->reads;
            site->last_step=p->step;
            if (site->last_value!=value) ++site->transitions;
            site->last_value=value;
            site->value_or|=value;
            site->value_and&=value;
        }
    }
    if (t->bus.address==0x40U || t->bus.address==0x43U)
        p->pit_io[p->pit_count++%TRACE_SIZE]=
            p->recent_io[(p->io_count-1U)%TRACE_SIZE];
    return s;
}
static bm_status_t page_spare_access(void *context, bm_bus_transaction_t *t)
{
    probe_t *p=context;
    bm_status_t s=bm_pcs286_page_spare_latches_io(&p->page_spares,t);
    if (s!=BM_STATUS_OK) return s;
    if (t->address==0x80U && t->operation==BM_BUS_WRITE) {
        p->last_post=(int)(t->value&0xffU); ++p->post_count;
        printf("{\"event\":\"post\",\"step\":%" PRIu64 ",\"pc\":%u,\"value\":%d}\n",
               p->step,p->pc,p->last_post);
    }
    return BM_STATUS_OK;
}
static bm_status_t lpt_access(void *context, bm_bus_transaction_t *t)
{
    probe_t *p=context;
    unsigned index=(unsigned)t->address-0x378U;
    uint8_t value;
    bm_status_t s;
    if (t->operation==BM_BUS_WRITE) {
        s=bm_lpt_spp_write(p->lpt,index,(uint8_t)t->value);
        if (s==BM_STATUS_OK && index==0U)
            printf("{\"event\":\"lpt_data\",\"step\":%" PRIu64 ",\"pc\":%u,\"value\":%u}\n",
                   p->step,p->pc,(unsigned)t->value);
    } else {
        s=bm_lpt_spp_read(p->lpt,index,&value);
        if (s==BM_STATUS_OK) t->value=value;
    }
    return s;
}
static bm_status_t inta(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    *waits=0;
    return bm_at_pic_acknowledge(((probe_t *)context)->pic,phase,vector);
}
static void fdc_irq(void *context, int level)
{
    probe_t *p=context;
    if (p->pic) (void)bm_at_pic_set_irq(p->pic,6U,level);
}
static void fdc_dreq(void *context, int level)
{
    probe_t *p=context;
    if (p->dma) (void)bm_at_dma_set_dreq(p->dma,2U,level);
}
static bm_status_t fdc_dma_read(void *context, uint16_t *value)
{
    uint8_t byte=0;
    bm_status_t s=bm_wd37c65_dma_read(context,&byte);
    if (s==BM_STATUS_OK) *value=byte;
    return s;
}
static bm_status_t fdc_dma_write(void *context, uint16_t value)
{
    return value<=0xffU ? bm_wd37c65_dma_write(context,(uint8_t)value) :
                          BM_STATUS_INVALID_ARGUMENT;
}
static void fdc_terminal_count(void *context, int level)
{
    (void)bm_wd37c65_set_terminal_count(context,level);
}
static bm_status_t floppy_read(void *context, uint64_t first_block,
                               uint32_t block_count, uint8_t *destination)
{
    probe_t *p=context;
    size_t offset, count;
    if (!destination || first_block > SIZE_MAX/512U ||
        (block_count && ((size_t)block_count*512U)/512U != block_count))
        return BM_STATUS_INVALID_ARGUMENT;
    offset=(size_t)first_block*512U; count=(size_t)block_count*512U;
    if (offset>p->floppy_size || count>p->floppy_size-offset)
        return BM_STATUS_INVALID_ARGUMENT;
    memcpy(destination,p->floppy_image+offset,count);
    return BM_STATUS_OK;
}
static void trace(void *context, const bm_286_boundary_t *b)
{
    probe_t *p=context;
    p->recent_cpu[p->boundaries++%TRACE_SIZE]=(cpu_event_t){p->step,*b};
}

/* Initialization follows the already tested board composition. Every child
 * retains its own implementation, clocks, error propagation and reset policy. */
static bm_status_t initialize(probe_t *p, const uint8_t *image, unsigned ram_mib,
                              int ff, int classic_rtc, int cmos_mode,
                              const uint8_t *external_cmos, int video,
                              const uint8_t *floppy_image, size_t floppy_size)
{
    bm_host_services_t h=bm_null_host_services();
    bm_pcs286_firmware_t fw={0};
    bm_headland_at_config_t m={0};
    bm_at_bus_config_t bus={0};
    bm_at_pic_config_t pic={0};
    bm_at_dma_config_t dma={0};
    bm_wd37c65_config_t fdc={0};
    bm_floppy_drive_config_t floppy={0};
    bm_286_config_t cpu={0};
    bm_pcs286_control_config_t control;
    bm_pcs286_services_config_t services={0};
    bm_pcs286_io_config_t io={0};
    bm_lpt_spp_config_t lpt={0}; /* Existing SPP, no attached printer. */
    bm_pvga1a_config_t vga={BM_PVGA1A_VRAM_SIZE};
    uint8_t cmos[128]={0};
    bm_status_t s;
#define TRY(call) do { s=(call); if (s!=BM_STATUS_OK) return s; } while (0)
    fw.image[0].data=image; fw.image[0].size=BM_PCS286_FIRMWARE_BYTES;
    TRY(bm_pcs286_memory_create(&h,ram_mib*0x100000U,&fw,&p->bytes));
    TRY(bm_gc103_memory_initialize(&p->routes,ram_mib*0x100000U));
    m.profile=BM_HEADLAND_AT_LEGACY_GC103; m.routes=&p->routes;
    m.backing=bm_headland_at_memory_backing; m.backing_context=p->bytes;
    if (video) {
        TRY(bm_bus_create(&h,2,&p->video_bus));
        TRY(bm_pvga1a_create(&h,p->video_bus,&vga,&p->video));
        TRY(bm_pvga1a_advance_ns(p->video,0));
        m.external=external_memory; m.external_context=p;
    }
    m.external_width=1; m.holes=BM_HEADLAND_AT_HOLES_FF;
    m.protected_writes=BM_HEADLAND_AT_PROTECTED_IGNORE;
    m.timing=BM_HEADLAND_AT_PROVISIONAL_SERVICE_CLOCK;
    m.service_clock=(bm_clock_rate_t){8000000,1};
    m.cpu_a20=1; /* Explicit initial board input; reset ROM is at FFFFF0h. */
    TRY(bm_headland_at_memory_initialize(&p->memory,&m));
    pic.master_base=0x20; pic.slave_base=0xa0; pic.cascade_line=2;
    pic.intr=bm_pcs286_control_intr; pic.intr_context=&p->control;
    TRY(bm_at_pic_create(&h,&pic,&p->pic));
    if (floppy_image) {
        p->floppy_image=floppy_image; p->floppy_size=floppy_size;
        floppy.installed=floppy.media_present=floppy.write_protected=1;
        floppy.geometry=(bm_floppy_geometry_t){80U,2U,18U,512U};
        floppy.media=(bm_block_media_t){p,FLOPPY_1440_BYTES/512U,512U,1,floppy_read,NULL};
        TRY(bm_floppy_drive_create(&h,&floppy,&p->floppy));
    }
    fdc.io_base=0x3f0U; fdc.irq=fdc_irq; fdc.dreq=fdc_dreq;
    fdc.output_context=p; fdc.clock=(bm_clock_rate_t){8000000,1};
    fdc.drives[0]=p->floppy;
    TRY(bm_wd37c65_create(&h,&fdc,&p->fdc));
    dma.clock=(bm_clock_rate_t){4000000,1}; dma.memory=dma_memory_access; dma.memory_context=p;
    dma.endpoints[2].context=p->fdc;
    dma.endpoints[2].read=fdc_dma_read;
    dma.endpoints[2].write=fdc_dma_write;
    dma.endpoints[2].terminal_count=fdc_terminal_count;
    TRY(bm_at_dma_create(&h,&dma,&p->dma));
    TRY(bm_lpt_spp_create(&h,&lpt,&p->lpt));
    TRY(bm_ioc02_legacy_initialize(&p->ioc));
    bm_pcs286_page_spare_latches_initialize(&p->page_spares);
    bus.memory=memory_access; bus.io=io_access; bus.decode_context=p;
    bus.cpu_clock=(bm_clock_rate_t){12000000,1}; bus.isa_clock=m.service_clock;
    bus.hold=bm_pcs286_control_hold; bus.hold_context=&p->control;
    TRY(bm_at_bus_create(&h,&bus,&p->bus));
    {
        bm_pcs286_dma_coordinator_config_t coordinator={p->bus,p->dma};
        TRY(bm_pcs286_dma_coordinator_initialize(&p->dma_coordinator,&coordinator));
    }
    cpu.size=sizeof(cpu); cpu.version=BM_286_CONTRACT_VERSION;
    cpu.access=bm_at_bus_cpu_access; cpu.access_context=p->bus;
    cpu.interrupt_ack=inta; cpu.interrupt_context=p;
    cpu.hold_ack=bm_pcs286_control_hlda; cpu.bus_lock=bm_pcs286_control_lock;
    cpu.pin_context=&p->control; cpu.trace=trace; cpu.trace_context=p;
    TRY(bm_286_create(&h,&cpu,&p->cpu));
    control=(bm_pcs286_control_config_t){&p->cpu,p->bus,p->pic,&p->memory};
    TRY(bm_pcs286_control_initialize(&p->control,&control));
    services.control=&p->control; services.pit_clock=(bm_clock_rate_t){1193182,1};
    services.rtc.io_base=0x70; services.rtc.cmos_size=128;
    services.rtc.divider_policy=classic_rtc ? BM_AT_RTC_DIVIDER_CLASSIC_STOP : BM_AT_RTC_DIVIDER_QUALIFIED;
    /* Caller-selected calendar only: 1980-01-01 Tuesday, 01:00:00 BCD.
     * Hour 01 is valid in both 12/24h modes. No vendor setup/checksum or host
     * date; subsequent guest writes are never repaired. Default is depleted. */
    if (cmos_mode==1) {
        cmos[4]=1; cmos[6]=3; cmos[7]=1; cmos[8]=1; cmos[9]=0x80;
        cmos[10]=0x60; cmos[11]=0x82;
        services.rtc.initial_cmos=cmos; services.rtc.initial_cmos_size=sizeof(cmos);
        services.rtc.battery_valid=1;
    } else if (cmos_mode==2) {
        services.rtc.initial_cmos=external_cmos;
        services.rtc.initial_cmos_size=sizeof(cmos);
        services.rtc.battery_valid=1;
    }
    services.keyboard.controller.data_port=0x60;
    services.keyboard.controller.command_port=0x64;
    services.keyboard.controller.command_profile=BM_KBC8042_COMMANDS_OLIVETTI_PCS286;
    services.keyboard.controller.clock=services.keyboard.keyboard.clock=(bm_clock_rate_t){1500000,1};
    services.keyboard.controller.input_cycles=2; services.keyboard.controller.output_cycles=3;
    services.keyboard.controller.self_test_cycles=5; services.keyboard.controller.pulse_cycles=6;
    services.keyboard.controller.input_port=0xa0; services.keyboard.controller.initial_output_port=0xc3;
    services.keyboard.keyboard.power_on_cycles=3; services.keyboard.keyboard.bat_cycles=5;
    services.keyboard.keyboard.byte_cycles=2; services.keyboard.keyboard.reset_accept_cycles=3;
    TRY(bm_pcs286_services_create(&h,&services,&p->services));
    io.profile=BM_PCS286_IO_LEGACY_GC103_AT; io.headland=&p->routes;
    io.ioc02=&p->ioc; io.pic=p->pic; io.dma=p->dma;
    io.holes=ff ? BM_PCS286_IO_FF : BM_PCS286_IO_REJECT;
    io.timing=BM_PCS286_IO_PROVISIONAL; io.service_clock=m.service_clock;
    io.resource_count=11;
    io.resources[0]=(bm_pcs286_io_resource_t){0x40,0x43,1,bm_pcs286_services_io,p->services,0};
    io.resources[1]=(bm_pcs286_io_resource_t){0x60,0x61,1,bm_pcs286_services_io,p->services,0};
    io.resources[2]=(bm_pcs286_io_resource_t){0x64,0x64,1,bm_pcs286_services_io,p->services,0};
    io.resources[3]=(bm_pcs286_io_resource_t){0x70,0x71,1,bm_pcs286_services_io,p->services,0};
    io.resources[4]=(bm_pcs286_io_resource_t){0x80,0x80,1,page_spare_access,p,0};
    io.resources[5]=(bm_pcs286_io_resource_t){0x378,0x37a,1,lpt_access,p,0};
    io.resources[6]=(bm_pcs286_io_resource_t){0x84,0x86,1,page_spare_access,p,0};
    io.resources[7]=(bm_pcs286_io_resource_t){0x88,0x88,1,page_spare_access,p,0};
    io.resources[8]=(bm_pcs286_io_resource_t){0x8c,0x8f,1,page_spare_access,p,0};
    /* Standard AT split: 3F6h remains the ATA alternate-status/control port. */
    io.resources[9]=(bm_pcs286_io_resource_t){0x3f0,0x3f5,1,bm_wd37c65_io,p->fdc,0};
    io.resources[10]=(bm_pcs286_io_resource_t){0x3f7,0x3f7,1,bm_wd37c65_io,p->fdc,0};
    if (video) {
        io.resources[11]=(bm_pcs286_io_resource_t){0x3b0,0x3df,1,video_io,p,0};
        ++io.resource_count;
    }
    TRY(bm_pcs286_io_initialize(&p->io,&io));
#undef TRY
    return BM_STATUS_OK;
}
static void destroy(probe_t *p)
{
    bm_pcs286_services_destroy(p->services);
    if (p->cpu.ops.destroy) p->cpu.ops.destroy(p->cpu.context);
    bm_at_bus_destroy(p->bus); bm_wd37c65_destroy(p->fdc);
    bm_floppy_drive_destroy(p->floppy);
    bm_at_pic_destroy(p->pic); bm_at_dma_destroy(p->dma);
    bm_lpt_spp_destroy(p->lpt);
    bm_bus_destroy(p->video_bus); bm_pvga1a_destroy(p->video);
    bm_pcs286_memory_destroy(p->bytes);
}
/* The existing renderer supplies pixels from guest VRAM/fonts/palette. PPM is
 * a dependency-free diagnostic export; exclusive creation protects originals.
 * Rendering and host file errors never become guest exceptions. */
static bm_status_t capture(probe_t *p, const char *path)
{
    bm_video_geometry_t g={0};
    bm_video_framebuffer_t frame={0};
    bm_status_t s;
    FILE *out;
    int error=0;
    if (!p->video) return BM_STATUS_INVALID_STATE;
    s=bm_pvga1a_video_geometry(p->video,&g);
    if (s!=BM_STATUS_OK) return s;
    if (!g.width || !g.height || g.width>2880U || g.height>1024U)
        return BM_STATUS_CAPACITY_EXCEEDED;
    frame.pixel_capacity=(size_t)g.width*g.height;
    frame.stride=g.width;
    frame.pixels=malloc(frame.pixel_capacity*sizeof(*frame.pixels));
    if (!frame.pixels) return BM_STATUS_OUT_OF_MEMORY;
    s=bm_pvga1a_render(p->video,p->video_ns,1000000000U,&frame);
    if (s!=BM_STATUS_OK) { free(frame.pixels); return s; }
    out=fopen(path,"wbx");
    if (!out) { free(frame.pixels); return BM_STATUS_DEVICE_ERROR; }
    if (fprintf(out,"P6\n%u %u\n255\n",g.width,g.height)<0) error=1;
    for (size_t n=0;n<frame.pixel_capacity && !error;++n) {
        uint32_t pixel=frame.pixels[n];
        uint8_t rgb[3]={(uint8_t)(pixel>>16),(uint8_t)(pixel>>8),(uint8_t)pixel};
        if (fwrite(rgb,1,3,out)!=3) error=1;
    }
    if (fclose(out)) error=1;
    free(frame.pixels);
    printf("{\"event\":\"frame\",\"step\":%" PRIu64 ",\"time_ns\":%" PRIu64
           ",\"width\":%u,\"height\":%u,\"status\":%d}\n",
           p->step,p->video_ns,g.width,g.height,error ? BM_STATUS_DEVICE_ERROR : BM_STATUS_OK);
    if (fflush(stdout)) error=1;
    return error ? BM_STATUS_DEVICE_ERROR : BM_STATUS_OK;
}
static void report(probe_t *p, const char *reason, bm_status_t status)
{
    bm_286_arch_state_t a={0};
    bm_pcs286_services_state_t state={0};
    bm_pcs286_dma_coordinator_state_t dma={0};
    uint64_t i;
    (void)bm_286_get_arch_state(&p->cpu,&a);
    if (p->services) (void)bm_pcs286_services_state(p->services,&state);
    if (p->dma) (void)bm_pcs286_dma_coordinator_state(&p->dma_coordinator,&dma);
    if (p->bytes) {
        bm_bus_transaction_t t={BM_ADDRESS_MEMORY,BM_BUS_READ,0x487,0,4,1,0,
                                BM_ENDIAN_LITTLE,BM_BUS_TRANSACTION_DEBUG};
        bm_status_t memory_status=bm_pcs286_memory_access(
            p->bytes,BM_PCS286_MEMORY_RAM,0x487,&t);
        printf("{\"event\":\"board_snapshot\",\"memory_status\":%d,"
               "\"bda_87_8a\":[%u,%u,%u,%u]}\n",memory_status,
               (unsigned)(t.value&0xffU),(unsigned)((t.value>>8U)&0xffU),
               (unsigned)((t.value>>16U)&0xffU),(unsigned)((t.value>>24U)&0xffU));
    }
    if (p->video) {
        bm_video_geometry_t g={0};
        bm_status_t geometry_status=bm_pvga1a_video_geometry(p->video,&g);
        bm_bus_transaction_t t={BM_ADDRESS_IO,BM_BUS_READ,0x3cc,0,1,1,0,
                                BM_ENDIAN_LITTLE,BM_BUS_TRANSACTION_DEBUG};
        (void)bm_bus_transact(p->video_bus,&t);
        printf("{\"event\":\"video_snapshot\",\"time_ns\":%" PRIu64
               ",\"misc\":%u,\"geometry_status\":%d,\"width\":%u,\"height\":%u,\"crtc\":[",
               p->video_ns,(unsigned)t.value,geometry_status,g.width,g.height);
        for (unsigned n=0;n<25;++n) {
            uint8_t value=0;
            (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_CRTC,(uint8_t)n,&value);
            printf("%s%u",n ? "," : "",value);
        }
        printf("],\"sequencer\":[");
        for (unsigned n=0;n<8;++n) {
            uint8_t value=0;
            (void)bm_pvga1a_inspect_register(p->video,BM_PVGA1A_SEQUENCER,(uint8_t)n,&value);
            printf("%s%u",n ? "," : "",value);
        }
        puts("]}");
    }
    if (p->services) {
        bm_at_rtc_state_t rtc;
        uint8_t bytes[128];
        if (bm_pcs286_services_inspect_rtc(p->services,&rtc,bytes,sizeof(bytes))==BM_STATUS_OK) {
            printf("{\"event\":\"rtc_snapshot\",\"cycles\":%" PRIu64
                   ",\"phase\":%u,\"updating\":%d,\"failure\":%d,\"registers\":[",
                   rtc.cycles,rtc.divider_phase,rtc.updating,rtc.failure);
            for (unsigned n=0;n<14;++n) printf("%s%u",n ? "," : "",bytes[n]);
            puts("]}");
        }
    }
    if (p->io_count>TRACE_SIZE) for (i=0;i<TRACE_SIZE;++i) {
        const io_event_t *e=&p->first_io[i];
        printf("{\"event\":\"first_io\",\"step\":%" PRIu64 ",\"pc\":%u,\"port\":%" PRIu64
               ",\"op\":%d,\"size\":%u,\"value\":%" PRIu64 ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->op,e->size,e->value,e->status);
    }
    for (i=p->kbc_count>TRACE_SIZE ? p->kbc_count-TRACE_SIZE : 0;i<p->kbc_count;++i) {
        const io_event_t *e=&p->kbc_io[i%TRACE_SIZE];
        printf("{\"event\":\"kbc_io\",\"step\":%" PRIu64 ",\"pc\":%u,\"port\":%" PRIu64
               ",\"op\":%d,\"size\":%u,\"value\":%" PRIu64 ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->op,e->size,e->value,e->status);
    }
    for (i=p->video_status_count>TRACE_SIZE ? p->video_status_count-TRACE_SIZE : 0;
         i<p->video_status_count;++i) {
        const io_event_t *e=&p->video_status_io[i%TRACE_SIZE];
        printf("{\"event\":\"video_status_io\",\"step\":%" PRIu64
               ",\"pc\":%u,\"port\":%" PRIu64 ",\"value\":%" PRIu64
               ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->value,e->status);
    }
    for (i=0;i<p->video_status_site_count;++i) {
        const video_status_site_t *site=&p->video_status_sites[i];
        printf("{\"event\":\"video_status_site\",\"pc\":%u"
               ",\"port\":%" PRIu64 ",\"reads\":%" PRIu64
               ",\"first_step\":%" PRIu64 ",\"last_step\":%" PRIu64
               ",\"transitions\":%" PRIu64 ",\"first_value\":%u"
               ",\"last_value\":%u,\"value_or\":%u,\"value_and\":%u"
               ",\"sequencer1\":%u,\"crtc\":[%u,%u,%u,%u,%u,%u,%u]"
               ",\"attribute\":[%u,%u,%u,%u,%u]}\n",
               site->pc,site->port,site->reads,site->first_step,site->last_step,
               site->transitions,site->first_value,site->last_value,
               site->value_or,site->value_and,site->sequencer1,site->crtc0,
               site->crtc1,site->crtc6,site->crtc7,site->crtc10,
               site->crtc11,site->crtc12,site->attribute0,site->attribute10,
               site->attribute12,site->attribute14,site->attribute15);
    }
    if (p->video_status_site_overflow)
        printf("{\"event\":\"video_status_site_overflow\",\"count\":%" PRIu64 "}\n",
               p->video_status_site_overflow);
    for (i=p->board_diagnostic_count>TRACE_SIZE ?
           p->board_diagnostic_count-TRACE_SIZE : 0;
         i<p->board_diagnostic_count;++i) {
        const io_event_t *e=&p->board_diagnostic_io[i%TRACE_SIZE];
        printf("{\"event\":\"board_diagnostic_write\",\"step\":%" PRIu64
               ",\"pc\":%u,\"address\":%" PRIu64 ",\"size\":%u"
               ",\"value\":%" PRIu64 ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->size,e->value,e->status);
    }
    for (i=p->pit_count>TRACE_SIZE ? p->pit_count-TRACE_SIZE : 0;
         i<p->pit_count;++i) {
        const io_event_t *e=&p->pit_io[i%TRACE_SIZE];
        printf("{\"event\":\"pit_io\",\"step\":%" PRIu64
               ",\"pc\":%u,\"port\":%" PRIu64 ",\"op\":%d"
               ",\"value\":%" PRIu64 ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->op,e->value,e->status);
    }
    for (i=p->boundaries>TRACE_SIZE ? p->boundaries-TRACE_SIZE : 0;i<p->boundaries;++i) {
        const cpu_event_t *e=&p->recent_cpu[i%TRACE_SIZE];
        printf("{\"event\":\"cpu\",\"step\":%" PRIu64 ",\"pc\":%u,\"kind\":%d,\"has_vector\":%u,\"vector\":%u}\n",
            e->step,e->b.instruction_address,e->b.kind,e->b.has_vector,e->b.vector);
    }
    for (i=p->io_count>TRACE_SIZE ? p->io_count-TRACE_SIZE : 0;i<p->io_count;++i) {
        const io_event_t *e=&p->recent_io[i%TRACE_SIZE];
        printf("{\"event\":\"io\",\"step\":%" PRIu64 ",\"pc\":%u,\"port\":%" PRIu64
               ",\"op\":%d,\"size\":%u,\"value\":%" PRIu64 ",\"status\":%d}\n",
               e->step,e->pc,e->address,e->op,e->size,e->value,e->status);
    }
    printf("{\"event\":\"summary\",\"reason\":\"%s\",\"status\":%d,\"steps\":%" PRIu64
           ",\"boundaries\":%" PRIu64 ",\"memory_calls\":%" PRIu64 ",\"io_calls\":%" PRIu64
           ",\"post_count\":%" PRIu64 ",\"last_post\":%d,\"fetched\":%d,\"first_fetch\":%u,"
           "\"cs\":%u,\"cs_base\":%u,\"ip\":%u,\"ax\":%u,\"bx\":%u,\"cx\":%u,\"dx\":%u,"
           "\"flags\":%u,\"msw\":%u,\"halted\":%u,\"shutdown\":%u,"
           "\"failed_address\":%" PRIu64 ",\"failed_status\":%d,"
           "\"dma_units\":%" PRIu64 ",\"dma_clocks\":%" PRIu64
           ",\"peripheral_ns\":%" PRIu64 ",\"boot_claim\":false}\n",
           reason,status,p->step,p->boundaries,p->memory_calls,p->io_count,p->post_count,p->last_post,
           p->fetched,p->first_fetch,a.cs.selector,a.cs.base,a.ip,a.ax,a.bx,a.cx,a.dx,a.flags,a.msw,
           a.halted,a.shutdown,p->failed_access.address,p->failed_access.status,
           dma.completed_units,dma.completed_clocks,state.time.nanoseconds);
}
static int number(const char *text, uint64_t max, uint64_t *out)
{
    char *end;
    unsigned long long value;
    if (!text[0] || text[0]=='-') return 0;
    errno=0; value=strtoull(text,&end,10);
    if (errno || *end || value==0 || value>max) return 0;
    *out=(uint64_t)value; return 1;
}
static int parse_action(const char *text, int frame, action_t *a)
{
    const char *colon=strchr(text,':');
    char digits[24], name[24];
    size_t n;
    unsigned key=0;
    if (!colon || (n=(size_t)(colon-text))==0 || n>=sizeof(digits)) return 0;
    memcpy(digits,text,n); digits[n]=0;
    if (!number(digits,100000000,&a->step)) return 0;
    text=colon+1;
    if (frame) { a->path=text; return *text!=0; }
    colon=strchr(text,':');
    if (!colon || (n=(size_t)(colon-text))==0 || n>=sizeof(name)) return 0;
    memcpy(name,text,n); name[n]=0;
    if (n==1 && name[0]>='A' && name[0]<='Z') key=BM_KEY_A+(unsigned)(name[0]-'A');
    else if (n==1 && name[0]>='1' && name[0]<='9') key=BM_KEY_1+(unsigned)(name[0]-'1');
    else if (!strcmp(name,"0")) key=BM_KEY_0;
    else if (name[0]=='F') {
        uint64_t f;
        if (number(name+1,10,&f)) key=BM_KEY_F1+(unsigned)f-1U;
    } else if (!strcmp(name,"ENTER")) key=BM_KEY_ENTER;
    else if (!strcmp(name,"ESC")) key=BM_KEY_ESCAPE;
    else if (!strcmp(name,"SPACE")) key=BM_KEY_SPACE;
    else if (!strcmp(name,"UP")) key=BM_KEY_UP;
    else if (!strcmp(name,"DOWN")) key=BM_KEY_DOWN;
    else if (!strcmp(name,"LEFT")) key=BM_KEY_LEFT;
    else if (!strcmp(name,"RIGHT")) key=BM_KEY_RIGHT;
    if (!key) return 0;
    a->key.kind=BM_INPUT_KEY; a->key.key=(bm_key_code_t)key;
    if (!strcmp(colon+1,"down")) a->key.pressed=1;
    else if (strcmp(colon+1,"up")) return 0;
    return 1;
}
int main(int argc, char **argv)
{
    const char *bios=NULL, *reason="budget", *final_frame=NULL, *cmos_path=NULL;
    const char *floppy_path=NULL, *live_dir=NULL;
    action_t actions[ACTION_LIMIT]={0};
    unsigned action_count=0;
    uint64_t steps=1000000, ns=1000, ram=1, live_every=0;
    int ff=0, classic_rtc=1, cmos_mode=0, cmos_selected=0, video=0, i, code=2;
    uint8_t image[BM_PCS286_FIRMWARE_BYTES];
    uint8_t external_cmos[128];
    uint8_t *floppy_image=NULL;
    probe_t p={0};
    bm_status_t s;
    FILE *file;
    p.last_post=-1;
    for (i=1;i<argc;++i) {
        if (i+1>=argc) goto usage;
        if (!strcmp(argv[i],"--bios")) bios=argv[++i];
        else if (!strcmp(argv[i],"--floppy")) floppy_path=argv[++i];
        else if (!strcmp(argv[i],"--frame")) final_frame=argv[++i];
        else if (!strcmp(argv[i],"--live-dir")) live_dir=argv[++i];
        else if (!strcmp(argv[i],"--live-every")) {
            if (!number(argv[++i],100000000,&live_every)) goto usage;
        }
        else if (!strcmp(argv[i],"--capture") || !strcmp(argv[i],"--key")) {
            int frame=!strcmp(argv[i],"--capture");
            if (action_count==ACTION_LIMIT || !parse_action(argv[++i],frame,&actions[action_count])) goto usage;
            ++action_count;
        }
        else if (!strcmp(argv[i],"--steps")) { if (!number(argv[++i],100000000,&steps)) goto usage; }
        else if (!strcmp(argv[i],"--peripheral-ns")) { if (!number(argv[++i],1000000000,&ns)) goto usage; }
        else if (!strcmp(argv[i],"--ram-mib")) { if (!number(argv[++i],4,&ram)) goto usage; }
        else if (!strcmp(argv[i],"--cmos")) {
            if (cmos_selected++) goto usage;
            ++i;
            if (!strcmp(argv[i],"initialized")) cmos_mode=1;
            else if (!strcmp(argv[i],"depleted")) cmos_mode=0;
            else goto usage;
        }
        else if (!strcmp(argv[i],"--cmos-file")) {
            if (cmos_selected++) goto usage;
            cmos_path=argv[++i]; cmos_mode=2;
        }
        else if (!strcmp(argv[i],"--video")) {
            ++i;
            if (!strcmp(argv[i],"pvga1a")) video=1;
            else if (!strcmp(argv[i],"none")) video=0;
            else goto usage;
        }
        else if (!strcmp(argv[i],"--rtc-divider")) {
            ++i;
            if (!strcmp(argv[i],"classic")) classic_rtc=1;
            else if (!strcmp(argv[i],"strict")) classic_rtc=0;
            else goto usage;
        }
        else if (!strcmp(argv[i],"--io-holes")) {
            ++i;
            if (!strcmp(argv[i],"ff")) ff=1;
            else if (!strcmp(argv[i],"reject")) ff=0;
            else goto usage;
        } else goto usage;
    }
    if (!bios) goto usage;
    if (final_frame && (!video || !*final_frame)) goto usage;
    if ((live_dir && (!video || !*live_dir || !live_every)) ||
        (live_every && !live_dir)) goto usage;
    for (unsigned n=0;n<action_count;++n)
        if (actions[n].step>=steps || (actions[n].path && !video)) goto usage;
    file=fopen(bios,"rb");
    if (!file) { fprintf(stderr,"Cannot open external BIOS\n"); return 3; }
    if (fread(image,1,sizeof(image),file)!=sizeof(image) || fgetc(file)!=EOF || ferror(file)) {
        fclose(file); fprintf(stderr,"BIOS must be exactly 131072 bytes\n"); return 3;
    }
    if (fclose(file)) return 3;
    if (cmos_path) {
        file=fopen(cmos_path,"rb");
        if (!file) { fprintf(stderr,"Cannot open external CMOS\n"); return 3; }
        if (fread(external_cmos,1,sizeof(external_cmos),file)!=sizeof(external_cmos) ||
            fgetc(file)!=EOF || ferror(file)) {
            fclose(file); fprintf(stderr,"CMOS must be exactly 128 bytes\n"); return 3;
        }
        if (fclose(file)) return 3;
    }
    if (floppy_path) {
        floppy_image=malloc(FLOPPY_1440_BYTES);
        if (!floppy_image) { fprintf(stderr,"Cannot allocate floppy image\n"); return 3; }
        file=fopen(floppy_path,"rb");
        if (!file) { free(floppy_image); fprintf(stderr,"Cannot open external floppy\n"); return 3; }
        if (fread(floppy_image,1,FLOPPY_1440_BYTES,file)!=FLOPPY_1440_BYTES ||
            fgetc(file)!=EOF || ferror(file)) {
            fclose(file); free(floppy_image);
            fprintf(stderr,"Floppy must be exactly 1474560 bytes\n"); return 3;
        }
        if (fclose(file)) { free(floppy_image); return 3; }
    }
    printf("{\"event\":\"config\",\"profile\":\"corrected-classic-gc103\",\"ram_mib\":%" PRIu64
           ",\"step_budget\":%" PRIu64 ",\"peripheral_ns_per_attempt\":%" PRIu64
           ",\"timing\":\"provisional-boundary-quantum\",\"io_holes\":\"%s\","
           "\"rtc_divider\":\"%s\",\"initial_a20\":1,\"cmos\":\"%s\",\"lpt\":\"spp-378-no-printer\","
           "\"kbc_commands\":\"olivetti-pcs286-classic-p2-cf\","
           "\"video\":\"%s\",\"fdc\":\"%s\","
           "\"storage\":%s,\"dma_service\":true}\n",
           ram,steps,ns,ff ? "ff" : "reject",classic_rtc ? "classic-stop" : "qualified",
           cmos_mode==2 ? "external-128-byte-image" :
           (cmos_mode==1 ? "calendar-1980-01-01-010000" : "depleted"),
           video ? "pvga1a-clocked" : "none",
           floppy_image ? "wd37c65-functional-drive0-read-only" : "wd37c65-functional-no-drive",
           floppy_image ? "true" : "false");
    s=initialize(&p,image,(unsigned)ram,ff,classic_rtc,cmos_mode,external_cmos,video,
                 floppy_image,floppy_image ? FLOPPY_1440_BYTES : 0U);
    if (s!=BM_STATUS_OK) { reason="initialize_error"; code=1; goto end; }
    while (p.step<steps) {
        bm_pcs286_services_step_t event;
        bm_286_arch_state_t a;
        uint64_t dma_clocks;
        /* Caller-selected attempt boundary, never a BIOS address predicate.
         * Captures precede keys at the same boundary; keys retain CLI order. */
        for (unsigned pass=0;pass<2;++pass) for (unsigned n=0;n<action_count;++n) {
            action_t *action=&actions[n];
            if (action->step!=p.step || (action->path!=NULL)!=(pass==0)) continue;
            if (action->path) {
                s=capture(&p,action->path);
                if (s!=BM_STATUS_OK) { reason="capture_error"; code=3; goto end; }
            } else {
                s=bm_pcs286_services_input(p.services,&action->key);
                printf("{\"event\":\"key\",\"step\":%" PRIu64 ",\"key\":%u,\"pressed\":%d,\"status\":%d}\n",
                       p.step,(unsigned)action->key.key,action->key.pressed,s);
                if (s!=BM_STATUS_OK) { reason="input_error"; code=1; goto end; }
            }
        }
        s=bm_286_get_arch_state(&p.cpu,&a);
        if (s!=BM_STATUS_OK) { reason="inspect_error"; code=1; break; }
        if (a.shutdown) { reason="guest_shutdown"; s=BM_STATUS_IDLE; code=1; break; }
        p.pc=(a.cs.base+a.ip)&0xffffffU;
        s=bm_pcs286_services_step(p.services,&event); ++p.step;
        if (s!=BM_STATUS_OK && s!=BM_STATUS_IDLE) { reason="step_error"; code=1; break; }
        s=bm_pcs286_dma_coordinator_step(&p.dma_coordinator,&dma_clocks);
        if (s!=BM_STATUS_OK && s!=BM_STATUS_IDLE) { reason="dma_error"; code=1; break; }
        s=bm_pcs286_services_advance(p.services,ns);
        if (s!=BM_STATUS_OK) { reason="peripheral_error"; code=1; break; }
        if (p.video) {
            s=bm_pvga1a_advance_ns(p.video,ns);
            if (s!=BM_STATUS_OK) { reason="video_clock_error"; code=1; break; }
            p.video_ns+=ns;
        }
        if (live_dir && p.step%live_every==0U) {
            char path[4096];
            int length=snprintf(path,sizeof(path),"%s/frame-%020" PRIu64 ".ppm",
                                live_dir,p.step);
            if (length<0 || (size_t)length>=sizeof(path)) {
                reason="live_frame_path_error"; code=3; break;
            }
            s=capture(&p,path);
            if (s!=BM_STATUS_OK) { reason="live_frame_error"; code=3; break; }
            /* The consumer derives the filename from STEP and its own live
             * directory. Avoid serializing a host path into JSON here: Windows
             * separators would otherwise require a second escaping contract. */
            printf("{\"event\":\"live_frame\",\"step\":%" PRIu64 "}\n",p.step);
            if (fflush(stdout)) { reason="live_frame_output_error"; code=3; break; }
        }
    }
end:
    if (final_frame && p.video) {
        bm_status_t frame_status=capture(&p,final_frame);
        if (frame_status!=BM_STATUS_OK) {
            fprintf(stderr,"Final framebuffer export failed: %d\n",frame_status);
            code=3; /* Preserve the original guest/engine terminal reason. */
        }
    }
    report(&p,reason,s); destroy(&p); free(floppy_image);
    if (fflush(stdout) || ferror(stdout)) return 3;
    return code; /* 2=budget exhausted, 1=explicit stop, 3=CLI/file error. Never boot success. */
usage:
    fprintf(stderr,"Usage: pcs286-boot-probe --bios PATH [--ram-mib 1..4] [--steps N] "
                   "[--peripheral-ns N] [--io-holes reject|ff] [--rtc-divider classic|strict] "
                   "[--cmos depleted|initialized | --cmos-file PATH] [--video none|pvga1a] [--frame NEW.ppm] "
                   "[--live-dir DIRECTORY --live-every STEPS] "
                   "[--floppy READ_ONLY_1440K.img] [--capture STEP:NEW.ppm] [--key STEP:NAME:down|up]\n");
    return 3;
}
