/* BluMach. Author: rtzor. Project: BluMach.
 * Default-video pixels under the documented conventional-DAC approximation.
 */
#define CHIPS452_TEXT_MAIN text_contracts_main
#include "chips452-text.c"

int main(void)
{
    text_contracts_main();
    chips452_t d = {0}; svga_t *s = &d.vga.svga;
    static bitmap_t target; uint32_t pixels[96]; monitor_t monitor = {0};
    s->priv = &d; s->monitor = &monitor; monitor.target_buffer = &target;
    target.w = 96; target.h = 1; target.line[0] = pixels;
    s->scrblank = 0x20; s->crtc[0x17] = 0x80; s->attr_palette_enable = 0x20;
    s->hdisp = 2; s->x_add = 3; s->bpp = 8;
    d.setup = 8; d.awake = 1; d.enable = 0x80;
    for (unsigned i = 0; i < 256; i++) s->pallook[i] = 0x810000 | (i * 257);
    for (unsigned i = 0; i < 16; i++) s->egapal[i] = 255-i;
    unsigned cases = 0;
    for (unsigned control = 0; control < 16; control++)
    for (unsigned color = 0; color < 256; color++)
    for (unsigned mask = 0; mask < 256; mask += 85)
    for (unsigned width = 0; width < 4; width++) {
        s->seqregs[1] = 0x20 | (width & 1) | ((width & 2) << 2);
        s->dac_mask = mask; s->fullchange = 0;
        xr(&d, 0x28, control); assert(s->fullchange);
        s->fullchange = 0; xr(&d, 0x2b, color); assert(s->fullchange);
        s->hdisp = 2; s->render = svga_render_blank; chips452_recalc(s);
        int active = control == 4 || control == 12;
        assert((s->render == chips452_render_default) == active);
        if (!active) continue;
        for (unsigned i = 0; i < 96; i++) pixels[i] = 0xdeadbeef;
        s->memaddr = 0x1234; s->firstline_draw = 2000;
        s->render(s);
        unsigned dots = (width & 1) ? 8 : 9;
        if (width & 2) dots *= 2;
        assert(s->hdisp == 2*dots && s->dots_per_clock == dots);
        uint32_t expected = control == 12 ? 0 : s->pallook[color & mask];
        for (unsigned x = 0; x < 96; x++)
            assert(pixels[x] == (x >= 3 && x < 3+2*dots ? expected : 0xdeadbeef));
        assert(s->memaddr == 0x1234 && s->firstline_draw == 0);
        cases++;
    }
    /* Clip panning and oversized rows; a cursor cannot cover screen-off. */
    d.xr[0x28] = 4; d.xr[0x37] = 1; s->render = svga_render_blank;
    chips452_recalc(s); assert(s->render == chips452_render_default);
    s->x_add = -5; s->hdisp = 100; s->render(s);
    s->y_add = -1; s->render(s); s->y_add = 1; s->render(s); s->y_add = 0;
    s->attr_palette_enable = 0; chips452_recalc(s);
    assert(s->render == svga_render_blank);
    s->attr_palette_enable = 0x20; s->scrblank = 0;
    s->render = svga_render_blank; d.xr[0x37] = 0; chips452_recalc(s);
    assert(s->render == svga_render_blank);
    s->scrblank = 0x20; chips452_recalc(s);
    chips452_reset(&d); chips452_recalc(s);
    assert(d.xr[0x28] == 0 && d.xr[0x2b] == 0 && s->render == svga_render_blank);
    printf("82C452 default video: 65536 selection contracts, %u pixel cases, clipping/disable/reset PASS\n", cases);
    return 0;
}
