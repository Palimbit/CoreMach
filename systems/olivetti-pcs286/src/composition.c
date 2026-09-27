/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Extracted from boot_probe.c: shared functional board wiring, no host files/UI.
 */
#include "composition.h"
#include <string.h>
static bm_status_t video_io(void *context, bm_bus_transaction_t *t)
{
    return bm_bus_transact(((bm_pcs286_composition_t *)context)->video_bus,t);
}
static bm_status_t external_memory(void *context, bm_bus_transaction_t *t)
{
    bm_pcs286_composition_t *p=context;
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
    bm_pcs286_composition_t *p=context;
    bm_status_t s;
    ++p->memory_calls;
    if (!p->fetched && t->bus.operation==BM_BUS_FETCH) {
        p->first_fetch=(uint32_t)t->bus.address; p->fetched=1;
    }
    if (p->profile_now) {
        uint64_t before=p->profile_now();
        s=bm_headland_at_memory_access(&p->memory,t);
        uint64_t after=p->profile_now();
        if (after>=before) p->profile_memory_ns+=after-before;
    } else s=bm_headland_at_memory_access(&p->memory,t);
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
    bm_pcs286_composition_t *p=context;
    return p->bus ? bm_at_bus_access(p->bus,t) : BM_STATUS_INVALID_STATE;
}
static bm_status_t io_access(void *context, bm_at_transfer_t *t)
{
    bm_pcs286_composition_t *p=context;
    bm_status_t s;
    if (p->profile_now) {
        uint64_t before=p->profile_now();
        s=bm_pcs286_io_access(&p->io,t);
        uint64_t after=p->profile_now();
        if (after>=before) p->profile_io_ns+=after-before;
    } else s=bm_pcs286_io_access(&p->io,t);
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
    bm_pcs286_composition_t *p=context;
    bm_status_t s=bm_pcs286_page_spare_latches_io(&p->page_spares,t);
    if (s!=BM_STATUS_OK) return s;
    if (t->address==0x80U && t->operation==BM_BUS_WRITE) {
        p->last_post=(int)(t->value&0xffU); ++p->post_count;
        if (p->diagnostic) p->diagnostic(p->diagnostic_context,"post",p->step,p->pc,(unsigned)p->last_post);
    }
    return BM_STATUS_OK;
}
static bm_status_t lpt_access(void *context, bm_bus_transaction_t *t)
{
    bm_pcs286_composition_t *p=context;
    unsigned index=(unsigned)t->address-0x378U;
    uint8_t value;
    bm_status_t s;
    if (t->operation==BM_BUS_WRITE) {
        s=bm_lpt_spp_write(p->lpt,index,(uint8_t)t->value);
        if (s==BM_STATUS_OK && index==0U)
            if (p->diagnostic) p->diagnostic(p->diagnostic_context,"lpt_data",p->step,p->pc,(unsigned)t->value);
    } else {
        s=bm_lpt_spp_read(p->lpt,index,&value);
        if (s==BM_STATUS_OK) t->value=value;
    }
    return s;
}
static bm_status_t inta(void *context, unsigned phase, uint8_t *vector, uint32_t *waits)
{
    *waits=0;
    return bm_at_pic_acknowledge(((bm_pcs286_composition_t *)context)->pic,phase,vector);
}
static void fdc_irq(void *context, int level)
{
    bm_pcs286_composition_t *p=context;
    if (p->pic) (void)bm_at_pic_set_irq(p->pic,6U,level);
}
static void fdc_dreq(void *context, int level)
{
    bm_pcs286_composition_t *p=context;
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
    bm_pcs286_composition_t *p=context;
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
    bm_pcs286_composition_t *p=context;
    p->recent_cpu[p->boundaries++%TRACE_SIZE]=(cpu_event_t){p->step,*b};
}

/* Initialization follows the already tested board composition. Every child
 * retains its own implementation, clocks, error propagation and reset policy. */
bm_status_t bm_pcs286_composition_initialize(bm_pcs286_composition_t *p, const bm_host_services_t *host, const uint8_t *image, unsigned ram_mib,
                              int ff, int classic_rtc, int cmos_mode,
                              const uint8_t *external_cmos, int video,
                              const uint8_t *floppy_image, size_t floppy_size, const bm_floppy_drive_config_t *drive)
{
    bm_host_services_t h=*host;
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
    if (floppy_image || drive) {
        p->floppy_image=floppy_image; p->floppy_size=floppy_size;
        floppy.installed=floppy.media_present=floppy.write_protected=1;
        floppy.geometry=(bm_floppy_geometry_t){80U,2U,18U,512U};
        floppy.media=(bm_block_media_t){p,FLOPPY_1440_BYTES/512U,512U,1,floppy_read,NULL};
        if (drive) floppy=*drive;
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
void bm_pcs286_composition_destroy(bm_pcs286_composition_t *p)
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
