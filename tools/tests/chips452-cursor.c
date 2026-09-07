/* BluMach. Author: rtzor. Project: BluMach.
 * Synthetic VRAM and real VGA scanline renderers; no ROM/guest execution.
 * Address/end expectations test the documented experimental interpretation,
 * not measured silicon behaviour.
 */
#define main register_contracts_main
#include "chips452-registers.c"
#undef main
#include "../../src/video/vid_svga_render.c"

static uint32_t direct_address(svga_t *s, uint32_t addr) { (void)s; return addr; }

static void set_pattern(chips452_t *d, int row, int pixel, unsigned value)
{
    /* Explicit byte map for A,B,E,F (low) and C,D,G,H (high). */
    static const unsigned offsets[] = {0, 1, 8, 9};
    unsigned base = (((d->xr[0x30] << 8) | d->xr[0x31]) * 16 + row * 16) & 0x3ffff;
    unsigned addr = (base + offsets[pixel / 8]) & 0x3ffff;
    unsigned bit = 0x80 >> (pixel % 8);
    d->vga.svga.vram[addr] = (d->vga.svga.vram[addr] & ~bit) | ((value & 1) ? bit : 0);
    addr = (addr + 2) & 0x3ffff;
    d->vga.svga.vram[addr] = (d->vga.svga.vram[addr] & ~bit) | ((value & 2) ? bit : 0);
}

#ifndef CHIPS452_CURSOR_MAIN
#define CHIPS452_CURSOR_MAIN main
#endif
int CHIPS452_CURSOR_MAIN(void)
{
    register_contracts_main();
    static chips452_t d;
    static bitmap_t target;
    static monitor_t monitor;
    static uint32_t output[2112];
    svga_t *s = &d.vga.svga;
    s->priv = &d;
    s->vram = calloc(0x40000, 1);
    s->changedvram = calloc(128, 1);
    assert(s->vram && s->changedvram);
    s->vram_display_mask = s->vram_mask = 0x3ffff;
    s->remap_func = direct_address;
    s->monitor = &monitor;
    monitor.target_buffer = &target;
    target.w = 2112;
    target.h = 2048;
    for (unsigned y = 0; y < 2112; y++)
        target.line[y] = output;
    s->bpp = 8;
    s->attr_palette_enable = 1;
    s->crtc[0x17] = 0xc3;
    s->plane_mask = 15;
    s->dac_mask = 255;
    s->map8 = s->pallook;
    s->seqregs[1] = 1;
    s->x_add = 8;
    s->y_add = 3;
    s->hdisp = 64;
    for (unsigned i = 0; i < 512; i++)
        s->pallook[i] = (i * 123457) & 0xffffff;
    for (unsigned i = 0; i < 16; i++)
        s->egapal[i] = i;
    d.xr[0x30] = 1; /* Physical 0x1000, away from background. */
    d.xr[0x32] = 15; /* Experimental 32 scanlines. */
    d.xr[0x37] = 1;
    d.xr[0x38] = 255;
    d.xr[0x39] = 0x52;
    d.xr[0x3a] = 0xac;

    /* Exhaustive mask/background truth table for all four pattern values. */
    for (unsigned pattern = 0; pattern < 4; pattern++) {
        set_pattern(&d, 0, 0, pattern);
        for (unsigned mask = 0; mask < 256; mask++) {
            d.xr[0x38] = mask;
            for (unsigned bg = 0; bg < 256; bg++) {
                unsigned expected = 0;
                for (unsigned bit = 0; bit < 8; bit++) {
                    unsigned b = (bg >> bit) & 1;
                    if (mask & (1 << bit)) {
                        if (pattern == 1) b = !b;
                        if (pattern >= 2) b = ((pattern == 2 ? 0x52 : 0xac) >> bit) & 1;
                    }
                    expected |= b << bit;
                }
                assert(chips452_cursor_pixel(&d, 0, 0, bg) == expected);
            }
        }
    }
    d.xr[0x38] = 255;
    /* All byte/bit positions and skipped bytes in two different lines. */
    for (unsigned row = 0; row < 2; row++) {
        for (unsigned x = 0; x < 32; x++)
            set_pattern(&d, row, x, (x + row) % 4);
        memset(s->vram + 0x1000 + row * 16 + 4, 0xff, 4);
        memset(s->vram + 0x1000 + row * 16 + 12, 0xff, 4);
        for (unsigned x = 0; x < 32; x++) {
            const uint8_t expected[] = {0x36, 0xc9, 0x52, 0xac};
            assert(chips452_cursor_pixel(&d, row, x, 0x36) == expected[(x + row) % 4]);
        }
    }
    assert(chips452_cursor_pixel(&d, 0, -1, 19) == 19);
    assert(chips452_cursor_pixel(&d, 0, 32, 19) == 19);
    d.xr[0x33] = 0x80;
    d.xr[0x34] = 63; /* -1 */
    assert(chips452_cursor_pixel(&d, 0, 0, 0x36) == 0xc9);
    d.xr[0x34] = 0; /* -64: completely clipped even with zoom. */
    d.xr[0x37] = 5;
    assert(chips452_cursor_pixel(&d, 0, 0, 19) == 19);
    d.xr[0x33] = 0;
    d.xr[0x34] = 1;
    assert(chips452_cursor_pixel(&d, 0, 0, 19) == 19);
    assert(chips452_cursor_pixel(&d, 0, 1, 19) == 19);
    assert(chips452_cursor_pixel(&d, 0, 2, 19) == 19);
    assert(chips452_cursor_pixel(&d, 0, 3, 19) == (19 ^ 255));
    assert(chips452_cursor_pixel(&d, 0, 4, 19) == (19 ^ 255));
    d.xr[0x34] = 0;
    d.xr[0x37] = 1;
    d.xr[0x33] = 1;
    assert(chips452_cursor_pixel(&d, 0, 255, 19) == 19);
    assert(chips452_cursor_pixel(&d, 0, 257, 19) == (19 ^ 255));
    d.xr[0x33] = 0;
    d.xr[0x35] = 1;
    assert(chips452_cursor_row(&d, 255) == -1);
    assert(chips452_cursor_row(&d, 256) == 0);
    d.xr[0x35] = 0;
    d.xr[0x36] = 7;
    assert(chips452_cursor_row(&d, 6) == -1);
    assert(chips452_cursor_row(&d, 7) == 0);
    assert(chips452_cursor_row(&d, 38) == 31);
    assert(chips452_cursor_row(&d, 39) == -1);
    d.xr[0x36] = 0;
    for (unsigned rate = 0; rate < 2; rate++) {
        d.xr[0x37] = 9 | (rate << 4);
        d.cursor_frames = 0;
        d.xr[0x2a] = 31;
        for (unsigned frame = 0; frame < 64; frame++) {
            assert((chips452_cursor_row(&d, 0) >= 0) == !((frame / (rate ? 16 : 8)) & 1));
            chips452_vsync(s);
        }
    }
    d.xr[0x37] = 1;
    d.xr[0x31] = 255;
    d.xr[0x32] = 0; /* End-field wrap: four lines in this interpretation. */
    assert(chips452_cursor_row(&d, 3) == 3);
    assert(chips452_cursor_row(&d, 4) == -1);
    d.xr[0x31] = 0;
    d.xr[0x32] = 255;
    assert(chips452_cursor_row(&d, 511) == 511);
    assert(chips452_cursor_row(&d, 512) == -1);
    d.xr[0x30] = 0xff;
    d.xr[0x31] = 0xff;
    set_pattern(&d, 1, 31, 3); /* Fetch wraps within physical 256 KiB. */
    assert(chips452_cursor_pixel(&d, 1, 31, 0) == 0xac);
    d.xr[0x30] = 1;
    d.xr[0x31] = 0;
    d.xr[0x32] = 15;

    /* Real planar, packed and text renderers. Compare transparent output to
       the same renderer without composition; then invert one physical pixel
       inside a duplicated pair with an intentionally non-invertible palette. */
    void (*renderers[])(svga_t *) = {svga_render_4bpp_highres,
        svga_render_8bpp_highres, svga_render_8bpp_lowres,
        svga_render_text_80, svga_render_text_40};
    s->pallook[0] = s->pallook[1] = 0x123456;
    s->pallook[255] = 0x654321;
    for (unsigned mode = 0; mode < 5; mode++) {
        uint32_t baseline[2112];
        memset(s->vram, 0, 0x40000);
        memset(output, 0x77, sizeof(output));
        s->gdcreg[5] = (mode == 1 || mode == 2) ? 0x40 : 0;
        s->gdcreg[6] = mode < 3 ? 1 : 0;
        s->memaddr = 0;
        s->fullchange = 1;
        renderers[mode](s);
        memcpy(baseline, output, sizeof(baseline));
        uint32_t final_address = s->memaddr;
        s->render = renderers[mode];
        chips452_recalc(s);
        assert(s->render == chips452_render);
        s->memaddr = 0;
        s->fullchange = 0;
        s->render(s);
        assert(!memcmp(output, baseline, sizeof(baseline)));
        assert(s->memaddr == final_address && !s->fullchange);
        assert(s->monitor == &monitor && s->map8 == s->pallook && s->dac_mask == 255);
        set_pattern(&d, 0, 1, 1);
        s->memaddr = 0;
        s->render(s);
        assert(output[8] == 0x123456 && output[9] == 0x654321);
        assert(output[10] == 0x123456);
        /* Moving with unchanged VRAM must erase the former cursor pixel. */
        d.xr[0x34] = 4;
        s->memaddr = 0;
        s->render(s);
        assert(output[9] == 0x123456 && output[13] == 0x654321);
        d.xr[0x34] = 0;
        /* DAC mask is applied after cursor inversion. */
        s->dac_mask = 15;
        s->memaddr = 0;
        s->render(s);
        assert(output[9] == s->pallook[15]);
        s->dac_mask = 255;
        /* Same background address on consecutive double-scan output lines
           must still consume a new cursor row. */
        set_pattern(&d, 1, 1, 2);
        s->displine = 1;
        s->memaddr = 0;
        s->render(s);
        assert(output[9] == s->pallook[0x52]);
        s->displine = 0;
        /* Disable restores original renderer; reset clears cursor phase. */
        d.xr[0x37] = 0;
        chips452_recalc(s);
        assert(s->render == renderers[mode]);
        d.xr[0x37] = 1;
    }
    /* A narrow host target must not constrain the VGA's final character
       fetch or permit the compositor to write beyond the visible row. */
    s->render = svga_render_8bpp_lowres;
    s->gdcreg[5] = 0x40;
    chips452_recalc(s);
    target.w = 17;
    output[17] = 0xabcdef;
    s->memaddr = 0;
    s->render(s);
    assert(output[17] == 0xabcdef);
    target.w = 2112;
    s->displine = 2048;
    s->render(s); /* Out-of-range output line is ignored. */
    s->displine = 0;
    s->scrblank = 1;
    chips452_recalc(s);
    assert(s->render == svga_render_8bpp_lowres);
    chips452_reset(&d);
    assert(!d.xr[0x37] && !d.cursor_frames && s->fullchange);
    free(d.cursor_line);
    free(s->changedvram);
    free(s->vram);
    puts("82C452 cursor truth table, geometry, blink and real scanline renderers: PASS");
    return 0;
}
