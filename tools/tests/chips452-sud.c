/* BluMach. Author: rtzor. Project: BluMach.
 * Real VGA memory pipeline, synthetic data, no firmware execution.
 * Carry-register expectations cover the documented experimental model.
 */
#include <assert.h>
#include "../../src/video/vid_svga.c"
#include "../../src/video/vid_chips_452.c"

cpu_state_t cpu_state;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
void xga_write_test(uint32_t a, uint8_t v, void *s) { (void)a; (void)v; (void)s; }
uint8_t xga_read_test(uint32_t a, void *s) { (void)a; (void)s; return 0xff; }
uint8_t vga_in(uint16_t port, void *priv)
{
    (void)port; (void)priv;
    assert(0 && "CR22 must be served by the C&T device, not generic VGA");
    return 0xff;
}

/* Independent bit-stream oracle, deliberately not a shift/OR formula. */
static uint8_t slide_reference(uint8_t now, uint8_t old, unsigned n, int right)
{
    unsigned result = 0;
    for (int out = 0; out < 8; out++) {
        int source = right ? out + n : out - (int)n;
        uint8_t input = source >= 0 && source < 8 ? now : old;
        result |= ((input >> ((source + 8) % 8)) & 1) << out;
    }
    return result;
}

int main(void)
{
    chips452_t d = {0};
    svga_t *s = &d.vga.svga;
    monitor_t monitor = {0};
    s->priv = &d; s->monitor = &monitor;
    monitor.mon_changeframecount = 2;
    monitor.mon_video_timing_write_b = 1;
    s->vram = calloc(0x40000, 1);
    s->changedvram = calloc(64, 1);
    assert(s->vram && s->changedvram);
    s->vram_max = 0x40000; s->vram_mask = s->decode_mask = 0x3ffff;
    s->plane_write = chips452_plane_write;
    s->gdcreg[6] = 5; /* Graphics, A0000-AFFFF. */
    /* All modes, directions, alignments, raster operations and plane masks.
       The latch value differs from destination memory to detect accidental
       destination reads instead of using the hardware latches. */
    for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned right = 0; right < 2; right++)
    for (unsigned shift = 0; shift < 8; shift++)
    for (unsigned rop = 0; rop < 4; rop++)
    for (unsigned planes = 0; planes < 16; planes++)
    for (unsigned value = 0; value < 256; value++) {
        s->writemode = mode; s->writemask = planes;
        s->gdcreg[3] = shift | (rop << 3);
        s->gdcreg[0] = value & 15;
        s->gdcreg[1] = (value >> 4) & 15;
        s->gdcreg[8] = value ^ 0x96;
        d.xr[0x20] = 1 | (right << 1);
        uint8_t old[4], latched[4];
        for (unsigned p = 0; p < 4; p++) {
            old[p] = d.xr[0x21+p] = 0x36 ^ (p * 0x29);
            latched[p] = s->latch.b[p] = value ^ (0x43 + p * 31);
            s->vram[0x100+p] = 0xa5;
        }
        uint64_t saved_latch = s->latch.q;
        cycles = 100;
        chips452_write(0xa0040, value, &d);
        assert(cycles == 99 && s->latch.q == saved_latch);
        assert(s->gdcreg[3] == (shift | (rop << 3)));
        assert(s->gdcreg[8] == (value ^ 0x96));
        for (unsigned p = 0; p < 4; p++) {
            unsigned expected = 0;
            unsigned aligned = mode == 2 ? 0 : slide_reference(mode == 1 ? latched[p] : value, old[mode == 1 ? p : 0], shift, right);
            for (unsigned bit = 0; bit < 8; bit++) {
                unsigned dst = (latched[p] >> bit) & 1;
                unsigned src = (aligned >> bit) & 1;
                if (mode == 2) src = (value >> p) & 1;
                if (mode == 3 || (mode == 0 && (s->gdcreg[1] & (1 << p))))
                    src = (s->gdcreg[0] >> p) & 1;
                if (mode != 1) {
                    if (rop == 1) src &= dst;
                    if (rop == 2) src |= dst;
                    if (rop == 3) src ^= dst;
                    if (!(s->gdcreg[8] & (1 << bit)) || (mode == 3 && !(aligned & (1 << bit))))
                        src = dst;
                }
                expected |= src << bit;
            }
            assert(s->vram[0x100+p] == ((planes & (1 << p)) ? expected : 0xa5));
            uint8_t held = old[p];
            if (planes && mode != 2 && (mode == 1 || p == 0))
                held = mode == 1 ? latched[p] : value;
            assert(d.xr[0x21+p] == held);
        }
    }
    /* Long streams keep carry between writes, including shift zero. */
    s->writemode = 0; s->writemask = 15;
    s->gdcreg[1] = 0; s->gdcreg[8] = 255;
    for (unsigned right = 0; right < 2; right++)
    for (unsigned shift = 0; shift < 8; shift++) {
        d.xr[0x20] = 1 | (right << 1); s->gdcreg[3] = shift;
        uint8_t previous = d.xr[0x21] = 0x97;
        for (unsigned n = 0; n < 256; n++) {
            uint8_t value = n * 37;
            chips452_write(0xa0100 + n, value, &d);
            for (unsigned p = 0; p < 4; p++)
                assert(s->vram[0x400+n*4+p] == slide_reference(value, previous, shift, right));
            assert(d.xr[0x21] == value);
            previous = value;
        }
    }
    /* Last physical group fits exactly inside provisional256KiB VRAM. */
    s->gdcreg[3] = 0;
    chips452_write(0xaffff, 0x37, &d);
    for (unsigned p = 0; p < 4; p++) assert(s->vram[0x3fffc+p] == 0x37);
    /* A real memory read loads four source latches, then mode1 slides them. */
    s->chain4 = s->chain2_write = 0; s->readmode = 0; s->readplane = 2;
    s->writemode = 1; s->writemask = 15; s->gdcreg[3] = 3;
    d.xr[0x20] = 3;
    for (unsigned p = 0; p < 4; p++) {
        s->vram[0x200+p] = 0x19 + p * 41;
        d.xr[0x21+p] = 0;
    }
    (void)svga_read_linear(0x80, s);
    /* CR22 must expose those actual memory-loaded latches through both
       active CRTC bases, without a second load or attribute-phase change. */
    d.setup = 8; d.awake = 1; s->crtcreg = 0x22;
    for (unsigned color = 0; color < 2; color++) {
        s->miscout = color; s->attrff = 1;
        for (unsigned p = 0; p < 4; p++) {
            s->gdcreg[4] = p;
            uint64_t before = s->latch.q;
            assert(chips452_in(color ? 0x3d5 : 0x3b5, &d) == (uint8_t)(0x19+p*41));
            assert(s->latch.q == before && s->attrff == 1);
            assert(chips452_in(color ? 0x3b5 : 0x3d5, &d) == 0xff);
        }
    }
    chips452_write(0xa00c0, 0xff, &d);
    for (unsigned p = 0; p < 4; p++)
        assert(s->vram[0x300+p] == slide_reference(0x19+p*41, 0, 3, 1));
    /* Packed extended page path still decodes addresses and selects planes. */
    s->chain4 = s->packed_chain4 = 1; s->writemode = 0;
    s->gdcreg[1] = 0; s->gdcreg[3] = 0; s->gdcreg[8] = 255;
    d.xr[0x0b] = 5; d.xr[0x10] = 1;
    chips452_write(0xa0003, 0x6c, &d);
    assert(s->vram[0x4003] == 0x6c);
    /* Disabled SUD performs ordinary VGA rotation and preserves carry. */
    d.xr[0x20] = 2; d.xr[0x21] = 0x5a; s->gdcreg[3] = 1;
    chips452_write(0xa0003, 1, &d);
    assert(s->vram[0x4003] == 0x80 && d.xr[0x21] == 0x5a);
    /* Invalid normal aperture writes never consume the carry input. */
    d.xr[0x0b] = 0; d.xr[0x20] = 1;
    chips452_write(0xb0000, 0xff, &d);
    assert(d.xr[0x21] == 0x5a);
    free(s->vram); free(s->changedvram);
    puts("82C452 SUD: real memory pipeline, bit-stream oracle, carry approximation and VGA fallback PASS");
    puts("82C452 CR22: real VRAM-loaded latches, mono/color ports, inactive rejection and no read side effects PASS");
    return 0;
}
