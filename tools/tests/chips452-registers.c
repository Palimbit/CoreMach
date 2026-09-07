/* BluMach, a preservation-focused fork of 86Box.
 * Author: rtzor
 * Project: BluMach
 * Register-contract tests. The VGA engine and memory mapper are test doubles;
 * these tests do not claim to validate rendering or firmware execution.
 */
#include <assert.h>
#include <stdio.h>
#include "../../src/video/vid_chips_452.c"

double cpuclock = 16000000.0;
monitor_t monitors[MONITORS_NUM] = {{.mon_changeframecount = 2}};
int monitor_index_global;
static unsigned forwarded;
static uint16_t last_port;
static uint8_t last_value;
static unsigned reads;
static uint16_t last_read_port;
static uint8_t last_attribute_phase;
void mem_mapping_enable(mem_mapping_t *m) { m->enable = 1; }
void mem_mapping_disable(mem_mapping_t *m) { m->enable = 0; }
void mem_mapping_set_addr(mem_mapping_t *m, uint32_t a, uint32_t n) { m->base = a; m->size = n; }
void svga_recalctimings(svga_t *s) { (void) s; }
uint8_t vga_in(uint16_t p, void *d)
{
    vga_t *v = d;
    reads++;
    last_read_port = p;
    if (p == 0x3da || p == 0x3ba)
        v->svga.attrff = 0;
    return 0x5a;
}
void vga_out(uint16_t p, uint8_t v, void *d)
{
    vga_t *vga = d;
    forwarded++;
    last_port = p;
    last_value = v;
    if (p == 0x3c0) {
        last_attribute_phase = vga->svga.attrff;
        vga->svga.attrff ^= 1;
    }
}

static void xr(chips452_t *d, uint8_t r, uint8_t v)
{
    chips452_out(0x3d6, r, d);
    chips452_out(0x3d7, v, d);
}

int main(void)
{
    chips452_t d = {0};
    svga_t *s = &d.vga.svga;
    s->priv = &d;
    s->miscout = 3;
    chips452_reset(&d);
    assert(d.xr[0] == 0x14 && d.xr[6] == 0x4a);
    assert(d.vga.bios_rom.mapping.enable && !s->mapping.enable);
    assert(chips452_in(0x104, &d) == 0xff);
    chips452_out(0x102, 1, &d); /* Ignored outside setup. */
    assert(!d.awake);
    chips452_out(0x46e8, 0x18, &d);
    assert(chips452_in(0x104, &d) == 0xa5);
    chips452_out(0x102, 1, &d);
    chips452_out(0x103, 0x80, &d);
    assert(!s->mapping.enable); /* Setup excludes normal accesses. */
    chips452_out(0x46e8, 8, &d);
    assert(s->mapping.enable);
    xr(&d, 0, 0xff);
    assert(chips452_in(0x3d7, &d) == 0x14);
    xr(&d, 6, 0xaa);
    assert(chips452_in(0x3d7, &d) == 0xaa);
    xr(&d, 3, 1);
    assert(!d.vga.bios_rom.mapping.enable);
    xr(&d, 3, 0);
    assert(d.vga.bios_rom.mapping.enable);
    /* Relocation uses 103 bit 6 independently of mono/colour selection. */
    chips452_out(0x46e8, 0x18, &d);
    chips452_out(0x103, 0xc0, &d);
    chips452_out(0x46e8, 8, &d);
    chips452_out(0x3b6, 6, &d);
    assert(chips452_in(0x3b7, &d) == 0xaa);
    assert(chips452_in(0x3d7, &d) != 0xaa);
    chips452_out(0x46e8, 0x18, &d);
    chips452_out(0x103, 0x80, &d);
    chips452_out(0x46e8, 8, &d);
    /* C&T p.48: read-only latch and attribute-state readback, with both
       mono/colour decoding and extension access disabled/enabled. */
    for (unsigned color = 0; color < 2; color++) {
        s->miscout = 2 | color;
        uint16_t data = color ? 0x3d5 : 0x3b5;
        for (unsigned extended = 0; extended < 2; extended++) {
            d.enable = extended ? 0x80 : 0;
            for (unsigned value = 0; value < 256; value++) {
                for (unsigned plane = 0; plane < 4; plane++)
                    s->latch.b[plane] = value ^ (plane * 0x55);
                uint64_t saved = s->latch.q;
                s->crtcreg = 0x22;
                for (unsigned plane = 0; plane < 4; plane++) {
                    s->gdcreg[4] = plane;
                    unsigned before = reads;
                    assert(chips452_in(data, &d) == (value ^ (plane * 0x55)));
                    assert(chips452_in(data ^ 0x60, &d) == 0xff);
                    assert(reads == before && s->latch.q == saved);
                }
                unsigned before = forwarded;
                chips452_out(data, value, &d);
                assert(forwarded == before && s->latch.q == saved);
                s->crtcreg = 0x24;
                s->attraddr = value & 31;
                s->attr_palette_enable = value & 0x20;
                s->attrff = !!(value & 0x80);
                unsigned before_read = reads;
                assert(chips452_in(data, &d) == (value & 0xbf));
                assert(chips452_in(data ^ 0x60, &d) == 0xff);
                chips452_out(data, ~value, &d);
                assert(reads == before_read && forwarded == before);
                assert(s->attraddr == (value & 31));
                assert(s->attr_palette_enable == (value & 0x20));
                assert(s->attrff == !!(value & 0x80));
            }
        }
    }
    s->miscout = 3;
    d.enable = 0x80;
    s->attrff = 0;
    /* Memory page boundaries, both CPU windows, and packed addressing. */
    xr(&d, 0x10, 2);
    xr(&d, 0x11, 8);
    xr(&d, 0x0b, 3);
    assert(chips452_address(&d, 0xaffff) == 0x11fff);
    assert(chips452_address(&d, 0xb0000) == 0x8000);
    s->gdcreg[6] = 4;
    chips452_paging(&d);
    assert(chips452_address(&d, 0xa7fff) == 0x9fff);
    assert(chips452_address(&d, 0xa8000) == 0x8000);
    xr(&d, 0x0b, 5);
    assert(s->packed_chain4);
    assert(chips452_address(&d, 0xa0000) == 0x8000);
    xr(&d, 0x0b, 0);
    assert(!s->packed_chain4 && s->mapping.base == 0xa0000 && s->mapping.size == 65536);
    /* Protect palette and CRTC bits independently. */
    xr(&d, 0x15, 0x20);
    unsigned old = forwarded;
    chips452_out(0x3c9, 63, &d);
    assert(forwarded == old);
    xr(&d, 0x15, 4);
    s->crtcreg = 0x11;
    s->crtc[0x11] = 0x30;
    chips452_out(0x3d5, 0, &d);
    assert(last_port == 0x3d5 && last_value == 0x30);
    xr(&d, 0x15, 0);
    /* Additional DAC decode is off after reset; enabled accesses reach the
       same DAC, while protected reads must not advance its internal state. */
    for (unsigned enable = 0; enable < 2; enable++) {
        xr(&d, 2, enable ? 0x40 : 0);
        for (unsigned protect = 0; protect < 2; protect++) {
            xr(&d, 0x15, protect ? 0x20 : 0);
            for (unsigned reg = 0; reg < 4; reg++) {
                for (unsigned alias = 0; alias < 2; alias++) {
                    uint16_t port = 0x3c6 + reg + (alias ? 0x8000 : 0);
                    int allowed = !protect && (!alias || enable);
                    unsigned before_read = reads, before_write = forwarded;
                    assert(chips452_in(port, &d) == (allowed ? 0x5a : 0xff));
                    assert(reads == before_read + allowed);
                    if (allowed) assert(last_read_port == 0x3c6 + reg);
                    chips452_out(port, 0x96, &d);
                    assert(forwarded == before_write + allowed);
                    if (allowed) assert(last_port == 0x3c6 + reg && last_value == 0x96);
                }
            }
        }
    }
    xr(&d, 0x15, 0);
    /* Enter split mapping from the data phase. Repeated writes to either
       port must select its fixed role and leave XR02 bit7 clear. */
    s->attrff = 1;
    xr(&d, 2, 8);
    assert(!s->attrff && !(chips452_in(0x3d7, &d) & 0x80));
    for (unsigned port = 0x3c0; port <= 0x3c1; port++) {
        for (unsigned value = 0; value < 256; value++) {
            chips452_out(port, value, &d);
            assert(last_port == 0x3c0 && last_value == value);
            assert(last_attribute_phase == (port == 0x3c1));
            assert(!s->attrff);
        }
    }
    xr(&d, 2, 0x10); /* EGA: either port participates in the same sequence. */
    chips452_out(0x3c1, 5, &d);
    assert(last_port == 0x3c0 && !last_attribute_phase && s->attrff);
    chips452_out(0x3c0, 7, &d);
    assert(last_attribute_phase && !s->attrff);
    chips452_out(0x3c0, 5, &d);
    chips452_in(0x3da, &d);
    assert(!s->attrff);
    xr(&d, 2, 0); /* VGA mapping remains unchanged. */
    chips452_out(0x3c1, 7, &d);
    assert(last_port == 0x3c1 && !s->attrff);
    chips452_out(0x3c0, 5, &d);
    assert(s->attrff);
    chips452_out(0x3c0, 7, &d);
    assert(!s->attrff);
    s->crtc[0x11] = 0x10;
    xr(&d, 0x2a, 1);
    chips452_vsync(s);
    assert(!d.interrupt_pending);
    chips452_vsync(s);
    assert(chips452_in(0x3c2, &d) & 0x80);
    chips452_out(0x3d5, 0, &d);
    assert(!d.interrupt_pending);
    /* Exercise the I/O path, not just the backing array: every possible
       write to the documented extended fields must preserve implemented
       bits and discard reserved ones. Full-width cursor colours included. */
    static const struct { uint8_t reg, mask; } fields[] = {
        {0x20, 0x03}, {0x21, 0xff}, {0x22, 0xff}, {0x23, 0xff},
        {0x24, 0xff}, {0x27, 0x3f}, {0x28, 0x0f}, {0x29, 0xfc},
        {0x2a, 0x1f}, {0x2b, 0xff}, {0x2c, 0x0f}, {0x2d, 0xff},
        {0x2e, 0x03}, {0x2f, 0xff}, {0x30, 0xff}, {0x31, 0xff},
        {0x32, 0xff}, {0x33, 0x8f}, {0x34, 0xff}, {0x35, 0x0f},
        {0x36, 0xff}, {0x37, 0x1f}, {0x38, 0xff}, {0x39, 0xff},
        {0x3a, 0xff}
    };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        for (unsigned value = 0; value < 256; value++) {
            xr(&d, fields[i].reg, value);
            assert(chips452_in(0x3d7, &d) == (value & fields[i].mask));
        }
    }
    /* Divider endpoints and intermediate periods: no early pending flag,
       acknowledge, then a second full period. No assertion of a PIC IRQ. */
    for (unsigned count = 0; count < 32; count++) {
        xr(&d, 0x2a, count);
        d.frame_count = 0;
        d.interrupt_pending = 0;
        s->crtc[0x11] = 0x10;
        for (unsigned cycle = 0; cycle < 2; cycle++) {
            for (unsigned frame = 0; frame < count; frame++) {
                chips452_vsync(s);
                assert(!(chips452_in(0x3c2, &d) & 0x80));
            }
            chips452_vsync(s);
            assert(chips452_in(0x3c2, &d) & 0x80);
            chips452_out(0x3d5, 0, &d);
            assert(!d.interrupt_pending);
        }
        for (unsigned control = 0; control <= 0x30; control += 0x10) {
            if (control == 0x10)
                continue;
            s->crtc[0x11] = control;
            for (unsigned frame = 0; frame < 64; frame++)
                chips452_vsync(s);
            assert(!d.interrupt_pending);
        }
    }
    chips452_out(0x46e8, 0, &d);
    assert(!s->mapping.enable && chips452_in(0x3d6, &d) == 0xff);
    assert(chips452_in(0x83c6, &d) == 0xff);
    s->attrff = 1;
    chips452_reset(&d);
    assert(!s->attrff && !(d.xr[2] & 0x40));
    assert(d.xr[6] == 0x4a && !d.xr[0x0b] && !d.awake);
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        assert(!d.xr[fields[i].reg]);
    assert(!d.frame_count && !d.interrupt_pending);
    puts("82C452 register contracts: PASS");
    return 0;
}
