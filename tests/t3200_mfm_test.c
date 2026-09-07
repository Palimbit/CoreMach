/* BluMach native T3200 MFM contract. GPL-2.0-or-later.
 * Copyright 2026 rtzor, Project BluMach.
 * Real controller callbacks; timer/PIC/disk are bounded host doubles.
 * No firmware or proprietary media is executed. */
#include <assert.h>
#include "../src/disk/hdc_st506_xt.c"

hard_disk_t hdd[HDD_NUM];
uint64_t tsc;
uint64_t TIMER_USEC = 1;
static unsigned irq_lines;
static uint8_t disk[615*8*17*512];
static unsigned writes;
void timer_enable(pc_timer_t *t) { t->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *t) { t->flags &= ~TIMER_ENABLED; }
void picint_common(uint16_t v, int mode, int level, uint8_t *priv) {
    (void)mode; (void)priv;
    if (level) irq_lines |= v; else irq_lines &= ~v;
}
void dma_set_drq(int c, int v) { (void)c; assert(!v); }
int dma_get_drq(int c) { (void)c; return 0; }
int dma_channel_read(int c) { (void)c; assert(0); return 0; }
int dma_channel_write(int c, uint16_t v) { (void)c; (void)v; assert(0); return 0; }
void ui_sb_update_icon(int t, int v) { (void)t; (void)v; }
double hdd_seek_get_time(hard_disk_t *d,uint32_t a,uint8_t o,uint8_t c,double m) {
    (void)d;(void)a;(void)o;(void)c;(void)m; return 1;
}
int hdd_image_read(uint8_t id,uint32_t s,uint32_t n,uint8_t *b) {
    assert(id==0 && (uint64_t)(s+n)*512<=sizeof(disk)); memcpy(b,disk+s*512,n*512); return 0;
}
int hdd_image_write(uint8_t id,uint32_t s,uint32_t n,uint8_t *b) {
    assert(id==0 && (uint64_t)(s+n)*512<=sizeof(disk)); memcpy(disk+s*512,b,n*512); writes++; return 0;
}
int hdd_image_zero(uint8_t id,uint32_t s,uint32_t n) {
    assert(id==0 && (uint64_t)(s+n)*512<=sizeof(disk)); memset(disk+s*512,0,n*512); return 0;
}
void fatal(const char *fmt, ...) { (void)fmt; abort(); }
void hdd_audio_seek(hard_disk_t *d, uint32_t c) { (void)d; (void)c; }
void ui_sb_update_icon_write(int t,int v) { (void)t; (void)v; }
int hdd_image_load(int id) { assert(id==0); return 1; }
void hdd_image_close(uint8_t id) { (void)id; }
int rom_present(const char *f) { (void)f; return 0; }
FILE *rom_fopen(const char *f,char *m) { (void)f; (void)m; assert(0); return NULL; }
int device_get_config_int(const char *n) { (void)n; assert(0); return 0; }
int device_get_config_hex16(const char *n) { (void)n; assert(0); return 0; }
int device_get_config_hex20(const char *n) { (void)n; assert(0); return 0; }
void timer_add(pc_timer_t *t,void (*cb)(void *),void *v,int start) {
    t->callback=cb; t->priv=v; t->flags=start?TIMER_ENABLED:0;
}
void io_sethandler(uint16_t b,uint16_t s,
 uint8_t (*r)(uint16_t,void *),uint16_t (*rw)(uint16_t,void *),uint32_t (*rl)(uint16_t,void *),
 void (*w)(uint16_t,uint8_t,void *),void (*ww)(uint16_t,uint16_t,void *),void (*wl)(uint16_t,uint32_t,void *),void *v) {
    assert(b==0x1f0 && s==4 && r==st506_read && w==st506_write && !rw && !rl && !ww && !wl && v);
}
void mem_mapping_add(mem_mapping_t *m,uint32_t b,uint32_t s,
 uint8_t (*r)(uint32_t,void *),uint16_t (*rw)(uint32_t,void *),uint32_t (*rl)(uint32_t,void *),
 void (*w)(uint32_t,uint8_t,void *),void (*ww)(uint32_t,uint16_t,void *),void (*wl)(uint32_t,uint32_t,void *),uint8_t *e,uint32_t f,void *v) {
    (void)m;(void)b;(void)s;(void)r;(void)rw;(void)rl;(void)w;(void)ww;(void)wl;(void)e;(void)f;(void)v; assert(0);
}
static void tick(hdc_t *d) {
    assert(timer_is_enabled(&d->timer)); timer_disable(&d->timer); st506_callback(d);
}
static void command(hdc_t *d,uint8_t op,unsigned cyl,unsigned head,unsigned sec,unsigned n) {
    uint8_t b[6]={op,head,(sec&63)|((cyl>>2)&0xc0),cyl,n,0};
    st506_write(0x1f3,2,d); st506_write(0x1f2,0,d);
    for(unsigned i=0;i<6;i++) { assert((st506_read(0x1f1,d)&15)==13); st506_write(0x1f0,b[i],d); }
    tick(d);
}
static uint8_t finish(hdc_t *d) {
    assert((st506_read(0x1f1,d)&15)==15);
    assert(irq_lines==(1u<<14));
    uint8_t r=st506_read(0x1f0,d); assert(irq_lines==0); return r;
}
int main(void) {
    hdd[0].bus_type=HDD_BUS_MFM; hdd[0].spt=17; hdd[0].hpc=8; hdd[0].tracks=615;
    hdc_t *created=st506_xt_toshiba_t3200_device.init(&st506_xt_toshiba_t3200_device);
    hdc_t d=*created; free(created);
    assert(d.base==0x1f0 && d.irq==14 && d.spt==17 && !d.bios_rom.rom);
    /* Replay POST detection: no AT register echo, reset zero, select 0Dh. */
    st506_write(0x1f2,0x55,&d); assert(st506_read(0x1f2,&d)!=0x55);
    st506_write(0x1f1,0x55,&d); assert((st506_read(0x1f1,&d)&15)==0);
    st506_write(0x1f2,0x55,&d); assert(st506_read(0x1f1,&d)==13);
    t3200_hdc_reset(&d);
    command(&d,0,0,0,0,0); assert(finish(&d)==0);
    /* Distinct full-sector pattern, physical final CHS, readback. */
    command(&d,0x0a,614,7,16,1);
    for(unsigned i=0;i<512;i++) st506_write(0x1f0,(uint8_t)(i*13+7),&d);
    tick(&d); assert(finish(&d)==0 && writes==1);
    command(&d,8,614,7,16,1);
    for(unsigned i=0;i<512;i++) assert(st506_read(0x1f0,&d)==(uint8_t)(i*13+7));
    tick(&d); assert(finish(&d)==0);
    command(&d,8,615,0,0,1); assert(finish(&d)&2);
    /* Reset cancels pending writes, data and IRQ rather than only status. */
    command(&d,0x0a,0,0,0,1);
    for(unsigned i=0;i<512;i++) st506_write(0x1f0,0xcc,&d);
    assert(timer_is_enabled(&d.timer)); st506_write(0x1f1,0,&d);
    assert(!timer_is_enabled(&d.timer) && d.state==STATE_IDLE && irq_lines==0 && writes==1);
    d.drives[0].present=0; command(&d,0,0,0,0,0); assert(finish(&d)&2);
    d.drives[0].present=1;
    command(&d,8,0,8,0,1); assert(finish(&d)&2);
    command(&d,8,0,0,17,1); assert(finish(&d)&2);
    /* Two sectors across the final head of a cylinder, without aliasing. */
    command(&d,0x0a,0,7,16,2);
    for(unsigned sector=0;sector<2;sector++) {
        for(unsigned i=0;i<512;i++) st506_write(0x1f0,(uint8_t)(i+sector*71),&d);
        tick(&d);
    }
    assert(finish(&d)==0);
    assert(disk[135*512+7]==7 && disk[136*512+7]==78);
    command(&d,8,0,7,16,2);
    for(unsigned sector=0;sector<2;sector++) {
        for(unsigned i=0;i<512;i++) assert(st506_read(0x1f0,&d)==(uint8_t)(i+sector*71));
        tick(&d);
    }
    assert(finish(&d)==0);
    /* Crossing the physical end must fail, never wrap onto an old cylinder. */
    command(&d,8,614,7,16,2);
    for(unsigned i=0;i<512;i++) (void)st506_read(0x1f0,&d);
    tick(&d); assert(finish(&d)&2);
    command(&d,0,0,0,0,0); st506_write(0x1f3,0,&d);
    assert(irq_lines==0 && !(st506_read(0x1f1,&d)&STAT_IRQ));
    assert(st506_read(0x1f0,&d)==0);
    /* Constructor regression: the T1200 retains IRQ5 and its own switches. */
    created=st506_xt_toshiba_t1200_device.init(&st506_xt_toshiba_t1200_device);
    assert(created->irq==5 && created->switches==0x0c); free(created);
    puts("PASS T3200 constructor/native POST, PIO write/read, cylinder crossing, last sector, invalid CHS/end crossing, IRQ14/mask/ack, reset cancellation, absent drive; T1200 constructor unchanged");
}
