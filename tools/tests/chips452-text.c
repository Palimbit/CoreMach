/* BluMach. Author: rtzor. Project: BluMach.
 * Synthetic fonts in the layout observed in BIOS220. No firmware execution.
 */
#define CHIPS452_CURSOR_MAIN cursor_contracts_main
#include "chips452-cursor.c"

#ifndef CHIPS452_TEXT_MAIN
#define CHIPS452_TEXT_MAIN main
#endif
int CHIPS452_TEXT_MAIN(void)
{
    cursor_contracts_main();
    chips452_t d = {0};
    svga_t *s = &d.vga.svga;
    static bitmap_t target;
    static uint32_t pixels[128];
    monitor_t monitor = {0};
    s->priv = &d;
    s->vram = calloc(0x40000, 1);
    assert(s->vram);
    s->vram_display_mask = s->vram_mask = 0x3ffff;
    s->monitor = &monitor;
    monitor.target_buffer = &target;
    target.w = 128; target.h = 1; target.line[0] = pixels;
    s->remap_func = direct_address;
    s->fullchange = 1;
    s->bpp = 8;
    s->attr_palette_enable = 1;
    s->crtc[0x17] = 0x80;
    s->dac_mask = 255;
    s->map8 = s->pallook;
    for (unsigned i = 0; i < 256; i++) s->pallook[i] = i;
    for (unsigned i = 0; i < 16; i++) s->egapal[i] = i;
    d.setup = 8; d.awake = 1; d.enable = 0x80;
    s->fullchange = 0;
    xr(&d, 0x0e, 1);
    assert(s->fullchange);
    chips452_recalc(s);
    assert(s->text_glyph);
    /* Independent row-major upload: all eight VGA font blocks, 256 glyphs,
       32 rows. Includes the last byte of physical VRAM. */
    for (unsigned bank = 0; bank < 8; bank++) {
        s->charseta = bank * 32768 + 2;
        for (unsigned row = 0; row < 32; row++)
            for (unsigned ch = 0; ch < 256; ch++)
                s->vram[bank * 32768 + row * 1024 + ch * 4 + 2] = ch ^ (row * 7);
        for (unsigned row = 0; row < 32; row++) {
            s->scanline = row;
            for (unsigned ch = 0; ch < 256; ch++)
                assert(s->text_glyph(s, ch) == (uint8_t)(ch ^ (row * 7)));
        }
    }
    s->charseta = 0x8002;
    s->charsetb = 0x10002;
    s->scanline = 5;
    s->vram[0] = 0xc4; s->vram[1] = 0x19;
    s->vram[0x8002 + 5 * 1024 + 0xc4 * 4] = 0x81;
    /* Contradictory normal and alternate-bank data catch incorrect fetches. */
    s->vram[s->charsetb + 0xc4 * 128 + 20] = 0x40;
    s->attrregs[0x10] = 4;
    void (*renders[])(svga_t *) = {svga_render_text_80, svga_render_text_40};
    for (unsigned low = 0; low < 2; low++) {
        unsigned scale = low + 1;
        for (unsigned nine = 0; nine < 2; nine++) {
            s->seqregs[1] = nine ? 0 : 1;
            s->hdisp = (8 + nine) * scale;
            s->memaddr = 0;
            renders[low](s);
            for (unsigned x = 0; x < 8 * scale; x++)
                assert(pixels[x] == ((x / scale == 0 || x / scale == 7) ? 9 : 1));
            if (nine) assert(pixels[8 * scale] == 9);
            /* Text cursor and attribute blink stay in the generic renderer. */
            s->cursorvisible = s->cursoron = 1; s->cursoraddr = 0;
            s->memaddr = 0; renders[low](s);
            assert(pixels[0] == 1 && pixels[scale] == 9);
            s->cursorvisible = s->cursoron = 0;
            s->vram[1] = 0x99; s->attrregs[0x10] |= 8; s->blink = 16;
            s->memaddr = 0; renders[low](s);
            assert(pixels[0] == 1 && pixels[scale] == 1);
            s->vram[1] = 0x19; s->attrregs[0x10] = 4; s->blink = 0;
        }
        /* Existing graphics cursor composes with the new text fetch. */
        d.xr[0x30] = 1; d.xr[0x32] = 0; d.xr[0x37] = 1; d.xr[0x38] = 15;
        set_pattern(&d, 0, 0, 1);
        s->render = renders[low]; chips452_recalc(s);
        s->memaddr = 0; s->render(s);
        assert(pixels[0] == (9 ^ 15));
        d.xr[0x37] = 0; chips452_recalc(s);
    }
    /* Disable restores ordinary VGA attribute-selected font addressing. */
    d.xr[0x0e] = 0; chips452_recalc(s);
    assert(!s->text_glyph);
    s->seqregs[1] = 1; s->hdisp = 8; s->memaddr = 0;
    svga_render_text_80(s);
    assert(pixels[0] == 1 && pixels[1] == 9);
    d.xr[0x0e] = 1; chips452_recalc(s);
    chips452_reset(&d); chips452_recalc(s);
    assert(!s->text_glyph);
    free(d.cursor_line); free(s->vram);
    /* Family-derived auxiliary LSB: all CR13 values, both bits of XR0D,
       and byte/word/dword controls. Start/cursor units must stay unchanged. */
    for (unsigned offset = 0; offset < 256; offset++)
    for (unsigned control = 0; control < 4; control++)
    for (unsigned auxiliary = 0; auxiliary < 4; auxiliary++) {
        s->rowoffset = offset;
        s->crtc[0x17] = (control & 1) ? 0x40 : 0;
        s->crtc[0x14] = (control & 2) ? 0x40 : 0;
        s->memaddr_latch = 0x1234;
        s->adv_flags = 0;
        d.xr[0x0d] = auxiliary;
        chips452_recalc(s);
        unsigned fine = (auxiliary & 1) && control != 1 ? 4 : 0;
        assert(svga_display_row_step(s) == offset * 8 + fine);
        assert(s->rowoffset_extra == fine);
        assert(s->rowoffset == offset && s->memaddr_latch == 0x1234);
        assert(!s->adv_flags && !s->ca_adj);
    }
    d.xr[0x0d] = 1; s->crtc[0x17] = 0; chips452_recalc(s);
    assert(s->rowoffset_extra == 4);
    chips452_reset(&d); chips452_recalc(s);
    assert(!s->rowoffset_extra);
    puts("82C452 extended text: row-major fonts, bank approximation, VGA attributes and cursor: PASS");
    puts("82C452 auxiliary offset: 4096 family-derived address contracts and reset PASS");
    return 0;
}
