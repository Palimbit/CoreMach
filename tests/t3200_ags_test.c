/* BluMach T3200 experimental contract tests. GPL-2.0-or-later.
 * Copyright 2026 rtzor, Project BluMach.
 * Runs host callbacks, never firmware. Generic EGA is a bounded test double;
 * I/O dispatch is the real src/io.c implementation, including word accesses.
 */
#include <assert.h>
#include <stdarg.h>
#define T3200_CALLBACK_TEST
#include "../src/machine/m_at_t3200.c"

cpu_state_t cpu_state;
static int test_fn;
int keyboard_recv_ui(uint16_t scan) { return scan == 0x11d && test_fn; }
static uint8_t test_ram[0x400000 + 32];
uint8_t *ram = test_ram;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
static unsigned ega_reads, ega_writes;
static uint32_t ega_last_addr;
static uint8_t ega_last_val;
static unsigned ega_outs;
void ega_recalctimings(ega_t *ega) { (void)ega; }
uint8_t ega_in(uint16_t port, void *priv) { (void)port; (void)priv; return 0xa5; }
void ega_out(uint16_t port, uint8_t val, void *priv) { (void)port; (void)val; (void)priv; ega_outs++; }
uint8_t ega_read(uint32_t addr, void *priv) {
    (void)priv; ega_reads++; ega_last_addr=addr; return 0xa5;
}
void ega_write(uint32_t addr, uint8_t val, void *priv) {
    (void)priv; ega_writes++; ega_last_addr=addr; ega_last_val=val;
}
void mem_mapping_set_addr(mem_mapping_t *map, uint32_t base, uint32_t size) {
    (void)map; (void)base; (void)size;
}
void mem_mapping_set_exec(mem_mapping_t *map, uint8_t *exec) { map->exec=exec; }
void mem_mapping_enable(mem_mapping_t *map) { map->enable=1; }
void mem_mapping_disable(mem_mapping_t *map) { map->enable=0; }
void pclog(const char *fmt, ...) { (void)fmt; }

/* Unused platform paths referenced by the real dispatcher. */
#include <86box/pci.h>
int pci_flags;
uint32_t pci_base, pci_size;
uint32_t amstrad_latch;
int io_delay = 7;
uint16_t io_port;
uint32_t io_val;
int machine;
const machine_t machines[1] = {{0}};
int machine_xt_ibm5550_init(const machine_t *m) { (void)m; return 0; }
uint8_t pci_read(uint16_t p, void *v) { (void)p; (void)v; return 0xff; }
uint16_t pci_readw(uint16_t p, void *v) { (void)p; (void)v; return 0xffff; }
uint32_t pci_readl(uint16_t p, void *v) { (void)p; (void)v; return 0xffffffff; }
void pci_write(uint16_t p, uint8_t x, void *v) { (void)p; (void)x; (void)v; }
void pci_writew(uint16_t p, uint16_t x, void *v) { (void)p; (void)x; (void)v; }
void pci_writel(uint16_t p, uint32_t x, void *v) { (void)p; (void)x; (void)v; }

static uint8_t test_pages[2];
static uint8_t page_in(uint16_t p, void *v) { (void)v; return test_pages[p-0x80]; }
static void page_out(uint16_t p, uint8_t x, void *v) { (void)v; test_pages[p-0x80]=x; }

static void unlock(uint16_t p) { inb(p); inb(p); }

static void test_lim(t3200_t *dev)
{
    for (unsigned reg=0;reg<8;reg++)
        io_sethandler(t3200_lim_ports[reg],1,t3200_lim_in,NULL,NULL,t3200_lim_out,NULL,NULL,dev);

    /* The actual BIOS register test, including independent latches. */
    static const uint8_t patterns[]={0xff,0xaa,0x55,0x00};
    for (unsigned p=0;p<sizeof(patterns);p++) {
        for (unsigned reg=0;reg<8;reg++) outb(t3200_lim_ports[reg],patterns[p]);
        for (unsigned reg=0;reg<8;reg++) assert(inb(t3200_lim_ports[reg])==patterns[p]);
    }
    for (unsigned reg=0;reg<8;reg++) outb(t3200_lim_ports[reg],reg+1);
    for (unsigned reg=0;reg<8;reg++) assert(inb(t3200_lim_ports[reg])==reg+1);
    /* Optional card: write all pages before remapping into another slot;
       detect aliases across the two banks and check absent-page bounds. */
    dev->lim_card = 1;
    for (unsigned page=24;page<216;page++) {
        unsigned bank=page/128;
        outb(t3200_lim_ports[bank*4],0x80|(page%128));
        t3200_lim_write(0xd0000,page,dev);
        t3200_lim_write(0xd3fff,page^0xa5,dev);
    }
    for (unsigned page=24;page<216;page++) {
        unsigned bank=page/128;
        outb(t3200_lim_ports[bank*4+3],0x80|(page%128));
        assert(t3200_lim_read(0xdc000,dev)==(uint8_t)page);
        assert(t3200_lim_read(0xdffff,dev)==(uint8_t)(page^0xa5));
    }
    outb(0x0218,0xd8); /* global page216: beyond installed card */
    assert(!dev->lim_mapping[0].enable);
    dev->lim_card=0;
    for (unsigned reg=0;reg<8;reg++) outb(t3200_lim_ports[reg],0);

    memset(test_ram,0x6d,sizeof(test_ram));
    memset(dev->sram,0xa5,sizeof(dev->sram));
    /* Fill all six frame groups before reading any back. Distinct page data
       detects aliases, including the last two bytes of every 16 KiB page. */
    for (unsigned group=0;group<24;group+=4) {
        for (unsigned slot=0;slot<4;slot++) outb(t3200_lim_ports[slot],0x80+group+slot);
        for (unsigned offset=0;offset<0x10000;offset++) {
            uint8_t val=(uint8_t)((group+(offset>>14))*7+(offset&0xff));
            t3200_lim_write(0xd0000+offset,val,dev);
        }
    }
    for (unsigned group=0;group<24;group+=4) {
        for (unsigned slot=0;slot<4;slot++) outb(t3200_lim_ports[slot],0x80+group+slot);
        for (unsigned offset=0;offset<0x10000;offset++) {
            uint8_t val=(uint8_t)((group+(offset>>14))*7+(offset&0xff));
            assert(t3200_lim_read(0xd0000+offset,dev)==val);
        }
    }
    /* Reverse/non-adjacent pages across a frame boundary. Byte callbacks
       must select the next slot, never overrun the first backing page. */
    outb(0x0208,0x97); outb(0x4208,0x80);
    t3200_lim_write(0xd3fff,0x12,dev); t3200_lim_write(0xd4000,0x34,dev);
    assert(ram[0xfffff]==0x12 && ram[0xa0000]==0x34);
    assert(dev->lim_mapping[0].enable && dev->lim_mapping[0].exec==ram+0xfc000);
    assert(t3200_lim_read(0xd3fff,dev)==0x12 && t3200_lim_read(0xd4000,dev)==0x34);
    /* Mapping the same page into two slots intentionally shares its data. */
    outb(0x8208,0x80);
    assert(t3200_lim_read(0xd8000,dev)==0x34);

    /* Second bank is independently readable but unpopulated. Its write
       supersedes the first bank for that slot; it must not alias page zero. */
    outb(0x0218,0x80);
    assert(inb(0x0208)==0x97 && inb(0x0218)==0x80);
    assert(!dev->lim_mapping[0].enable && dev->lim_mapping[0].exec==NULL);
    assert(t3200_lim_read(0xd0000,dev)==0xff);
    t3200_lim_write(0xd0000,0x56,dev); assert(ram[0xa0000]==0x34);
    outb(0x0208,0x80);
    assert(t3200_lim_read(0xd0000,dev)==0x34 && inb(0x0218)==0x80);
    for (unsigned val=0;val<256;val++) {
        outb(0x0208,val);
        if (val<0x80 || val>=0x98) {
            assert(!dev->lim_mapping[0].enable && dev->lim_mapping[0].exec==NULL);
            assert(t3200_lim_read(0xd0000,dev)==0xff);
            t3200_lim_write(0xd0000,0,dev);
        }
    }
    assert(t3200_lim_read(0xcffff,dev)==0xff && t3200_lim_read(0xe0000,dev)==0xff);
    t3200_lim_write(0xcffff,0,dev); t3200_lim_write(0xe0000,0,dev);
    for (unsigned a=0;a<0xa0000;a++) assert(ram[a]==0x6d);
    for (unsigned a=0x100000;a<sizeof(test_ram);a++) assert(ram[a]==0x6d);
    for (unsigned a=0;a<sizeof(dev->sram);a++) assert(dev->sram[a]==0xa5);
    assert(ega_reads==0 && ega_writes==0);
    for (unsigned reg=0;reg<8;reg++) outb(t3200_lim_ports[reg],0);
}

int main(void)
{
    t3200_t dev = {0};
    assert(t3200_display_get() == -1);
    t3200_display_request(1);
    assert(atomic_load(&t3200_display_target) == 0);
    atomic_store(&t3200_display_active, 0);
    assert(!t3200_display_hotkey(1, 0x14f));
    test_fn = 1;
    assert(t3200_display_hotkey(1, 0x14f));
    assert(atomic_load(&t3200_display_target) == 1 && t3200_display_get() == 0);
    assert(t3200_notification_status(0xfd) == 0x8d); /* End notification, FIFO bits intact */
    assert(t3200_display_hotkey(1, 0x150));
    assert(t3200_notification_status(0) & 0x20);
    assert(t3200_display_hotkey(1, 0x150));
    assert(t3200_notification_status(0) & 0x20);
    assert(t3200_display_hotkey(0, 0x150));
    assert(t3200_display_hotkey(1, 0x150));
    assert(!(t3200_notification_status(0) & 0x20));
    assert(t3200_display_hotkey(0, 0x150));
    test_fn = 0;
    assert(t3200_display_hotkey(0, 0x14f));
    test_fn = 1;
    assert(t3200_display_hotkey(1, 0x147));
    assert(atomic_load(&t3200_display_target) == 0);
    assert(t3200_notification_status(0xfd) == 0x0d); /* Home clears notification bit */
    assert(t3200_display_hotkey(0, 0x147));
    test_fn = 0;
    io_init();
    io_sethandler(0x3b0,0x30,t3200_video_in,NULL,NULL,t3200_video_out,NULL,NULL,&dev);
    io_sethandler(0x80,2,page_in,NULL,NULL,page_out,NULL,NULL,NULL);
    test_lim(&dev);

    /* Word I/O keeps both adjacent handlers, including the page-81 side effect. */
    outw(0x80,0x1d3a);
    assert(test_pages[0]==0x3a && test_pages[1]==0x1d && inw(0x80)==0x1d3a);
    assert(io_access_width==2);
    uint64_t sequence=io_access_sequence;
    int before=cycles;
    assert(inb(0x1234)==0xff && cycles==before-io_delay);
    assert(io_access_sequence==sequence+1 && io_access_width==1);

    outb(0x3df,0x40); assert(dev.control==0);
    inb(0x3d8); outb(0x3df,0x40); assert(dev.control==0);
    unlock(0x3d8); inb(0x1234); outb(0x3df,0x40); assert(dev.control==0);
    inb(0x3d8); outb(0x80,0); inb(0x3d8); outb(0x3df,0x40); assert(dev.control==0);
    inb(0x3b8); inb(0x3d8); outb(0x3df,0x40); assert(dev.control==0);
    inw(0x3d8); inb(0x3d8); outb(0x3df,0x40); assert(dev.control==0);
    unlock(0x3d8); outb(0x3df,0x40); assert(dev.control==0x40);

    /* Diagnostic patterns cover exactly 2 KiB; font/pixel RAM stays separate. */
    static const uint8_t patterns[]={0x00,0xff,0x55,0xaa};
    for (unsigned p=0;p<sizeof(patterns);p++) {
        for (unsigned a=0;a<0x800;a++) t3200_video_write(0xa0000+a,patterns[p],&dev);
        for (unsigned a=0;a<0x800;a++) assert(t3200_video_read(0xa0000+a,&dev)==patterns[p]);
    }
    assert(ega_reads==0 && ega_writes==0);
    t3200_video_write(0xa0800,0x12,&dev);
    assert(ega_writes==1 && ega_last_addr==0xa0800 && ega_last_val==0x12);
    assert(t3200_video_read(0xa0800,&dev)==0xa5 && ega_reads==1);
    unlock(0x3b8); outb(0x3df,0); assert(t3200_video_read(0xa0000,&dev)==0xa5);
    unlock(0x3b8); outb(0x3df,0x40); assert(t3200_video_read(0xa0000,&dev)==0xaa);
    unlock(0x3d8); outb(0x3df,0x50); assert(dev.control==0x50);
    assert(t3200_video_read(0xa0000,&dev)==0xa5); /* unsupported, no silent alias */
    dev.provisional_50=1;
    assert(t3200_video_read(0xa0000,&dev)==0xaa); /* explicitly selected approximation */
    assert(dev.control==0x50); /* original value and unknown bit retained */
    dev.ega.gdcreg[6]=0x0c;
    assert(t3200_video_read(0xa0800,&dev)==0xff);
    assert(t3200_video_read(0xb8000,&dev)==0xa5);

    /* An unlocked PEGA control command must not overwrite horizontal total;
       an ordinary write of the same byte still reaches the EGA core. */
    unsigned before_outs=ega_outs;
    dev.ega.crtcreg=0;
    unlock(0x3d8); outb(0x3d5,0x85);
    assert(dev.crtc_control==0x85 && ega_outs==before_outs);
    unlock(0x3d8); outb(0x3d5,0x8a);
    assert(dev.crtc_control==0x8a && ega_outs==before_outs);
    outb(0x3d5,0x8a);
    assert(ega_outs==before_outs+1);
    dev.ega.crtcreg=0x1a;
    unlock(0x3d8); outb(0x3d5,0x0c);
    assert(dev.crtc_extension==0x0c && ega_outs==before_outs+1);

    t3200_t before_cursor = dev;
    /* Captured plasma underline must cover two glyph scanlines, not the
       bottom ten lines of the expanded cell. External and custom shapes
       retain the generic cursor state and CRTC bytes stay readable. */
    dev.provisional_display = 1;
    dev.external_display = 0;
    dev.ega.priv_parent = &dev;
    dev.ega.dispend = 400; dev.ega.rowcount = 15;
    dev.ega.gdcreg[6] = 0; dev.ega.crtc[9] = 7;
    dev.ega.crtc[10] = 6; dev.ega.crtc[11] = 0;
    dev.ega.cursorvisible = 1;
    for (int line = 0; line < 16; line++) {
        dev.ega.scanline = line;
        assert(t3200_cursor_scanline(&dev.ega) == (line == 12 || line == 13));
    }
    assert(dev.ega.crtc[10] == 6 && dev.ega.crtc[11] == 0);
    dev.external_display = 1;
    assert(t3200_cursor_scanline(&dev.ega) == 1);
    dev.external_display = 0;
    dev.ega.crtc[10] = 0; dev.ega.crtc[11] = 7;
    assert(t3200_cursor_scanline(&dev.ega) == 1);

    dev = before_cursor;
    /* Captured firmware table: preserve readable registers while correcting
       the explicitly selected pilot geometry. Strict mode keeps the defect
       reproducible; a 200-line table and graphics mode retain their row count. */
    dev.ega.priv_parent=&dev;
    dev.ega.crtc[6]=0x9f; dev.ega.crtc[7]=0x3f;
    dev.ega.crtc[0x10]=0x9f; dev.ega.crtc[0x12]=0x8f;
    dev.ega.crtc[9]=7; dev.ega.miscout=0x3f; dev.ega.rowcount=7;
    t3200_recalctimings(&dev.ega);
    assert(dev.ega.vtotal==417 && dev.ega.dispend==400 && dev.ega.vsyncstart==416);
    assert(dev.ega.vres==1 && dev.ega.rowcount==7);
    dev.provisional_display=1;
    t3200_recalctimings(&dev.ega);
    assert(dev.ega.vres==0 && dev.ega.rowcount==15 && dev.ega.crtc[9]==7);
    assert(monitors[0].mon_pixel_height_ratio == 1.2);
    assert(monitors[1].mon_pixel_height_ratio == 0.0);
    dev.external_display = 1;
    t3200_recalctimings(&dev.ega);
    assert(monitors[0].mon_pixel_height_ratio == 0.0);
    dev.external_display = 0;
    t3200_recalctimings(&dev.ega);
    dev.ega.gdcreg[6]|=1; dev.ega.rowcount=0;
    t3200_recalctimings(&dev.ega);
    assert(dev.ega.rowcount==0);
    dev.ega.crtc[0x12]=0x5d; dev.ega.miscout=0x7f;
    t3200_recalctimings(&dev.ega);
    assert(dev.ega.dispend==350 && dev.ega.vres==0); /* 350 must never become700 */
    dev.ega.crtc[7]=0; dev.ega.crtc[0x12]=199; dev.ega.rowcount=7;
    t3200_recalctimings(&dev.ega);
    assert(dev.ega.dispend==200 && dev.ega.vres==1 && dev.ega.rowcount==7);
    dev.ega.crtc[7] = 2; dev.ega.crtc[0x12] = 0x5d;
    dev.crtc_extension = 0x78;
    t3200_recalctimings(&dev.ega);
    assert(monitors[0].mon_pixel_height_ratio > 1.37 && monitors[0].mon_pixel_height_ratio < 1.38);
    assert(dev.ega.dispend == 350);
    dev.crtc_extension = 0x48;
    t3200_recalctimings(&dev.ega);
    assert(monitors[0].mon_pixel_height_ratio == 1.2);
    dev.control = 0x41;
    t3200_video_write(0xa0023, 0x96, &dev);
    assert(t3200_video_read(0xa0023, &dev) == 0x96);
    dev.control = 0x40;
    assert(t3200_video_read(0xa0023, &dev) == 0x96);
    dev.control = 0x01;
    assert(t3200_video_read(0xa0023, &dev) != 0x96);
    /* BIOS/XCHAD codes must increase brightness in level order. RGB
       luminance incorrectly made code2 brighter than code4. */
    t3200_t other = {0};
    t3200_plasma_palette(&dev);
    assert(other.ega.output_palette16 == NULL && other.ega.output_palette64 == NULL);
    assert(dev.plasma16[0] == 0 && dev.plasma64[0] == 0);
    assert(dev.plasma16[4] != 0); /* red FDISK prompts remain readable */
    for (unsigned level = 1; level < 4; level++) {
        uint32_t previous = dev.plasma64[(level - 1) * 2];
        uint32_t current = dev.plasma64[level * 2];
        for (unsigned shift = 0; shift <= 16; shift += 8)
            assert(((current >> shift) & 255) > ((previous >> shift) & 255));
        assert(dev.plasma16[level * 2] == current);
    }
    /* Palette construction must not overwrite the ROM's CELT. */
    dev.sram[0x23] = 0xc5;
    t3200_plasma_palette(&dev);
    assert(dev.sram[0x23] == 0xc5);
    for (unsigned table = 0; table < 2; table++) {
        uint32_t *pal = table ? dev.plasma64 : dev.plasma16;
        uint32_t seen[4] = {0}; unsigned count = 0;
        for (unsigned c = 0; c < 64; c++) {
            unsigned i;
            for (i = 0; i < count && seen[i] != pal[c]; i++);
            if (i == count) { assert(count < 4); seen[count++] = pal[c]; }
            if (pal[c]) {
                assert((pal[c] >> 16) > ((pal[c] >> 8) & 255));
                assert(((pal[c] >> 8) & 255) > (pal[c] & 255));
            }
        }
        assert(count == 4);
    }
    io_init();
    puts("PASS: real I/O dispatch, LIM/AGS/video contracts and isolated monotonic BIOS plasma levels.");
    return 0;
}
