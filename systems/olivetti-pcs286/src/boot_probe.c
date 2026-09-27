/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include "composition.h"
#include <blumach/platforms/null_host.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ACTION_LIMIT 32U
typedef struct action { uint64_t step; const char *path; bm_input_event_t key; } action_t;
typedef bm_pcs286_composition_t probe_t;
static void diagnostic(void *context, const char *event, uint64_t step, uint32_t pc, unsigned value)
{
 (void)context;
 printf("{\"event\":\"%s\",\"step\":%" PRIu64 ",\"pc\":%u,\"value\":%u}\n",event,step,pc,value);
}
static bm_status_t initialize(probe_t *p, const uint8_t *image, unsigned ram_mib,
 int ff, int classic_rtc, int cmos_mode, const uint8_t *cmos, int video,
 const uint8_t *floppy, size_t size)
{
 bm_host_services_t host=bm_null_host_services();
 p->diagnostic=diagnostic;
 return bm_pcs286_composition_initialize(p,&host,image,ram_mib,ff,classic_rtc,cmos_mode,cmos,video,floppy,size,NULL);
}
static void destroy(probe_t *p) { bm_pcs286_composition_destroy(p); }
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
