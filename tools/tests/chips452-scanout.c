/* BluMach. Author: rtzor. Project: BluMach.
 * Exercise the actual VGA scanout state machine without ROM or host UI.
 * Device register-to-stride mapping is covered by chips452-text.c.
 */
#include <assert.h>
#include "../../src/video/vid_svga.c"

double cpuclock = 16000000.0;
int enable_overscan, suppress_overscan;
/* Only host scheduling, display submission and unrelated renderers are doubles.
   Both halves of svga_poll and the row/field/split machinery are real. */
void timer_enable(pc_timer_t *t) { (void)t; }
void video_wait_for_buffer_monitor(int m) { (void)m; }
void video_lightpen_hsync(void) {}
void video_lightpen_vsync(void) {}
void video_lightpen_check_trigger_strobe(int x,int y,int h,int f,double c,int m)
{ (void)x;(void)y;(void)h;(void)f;(void)c;(void)m; }
void video_blit_memtoscreen_monitor(int x,int y,int w,int h,int m)
{ (void)x;(void)y;(void)w;(void)h;(void)m; }
void set_screen_size_monitor(int w,int h,int m) { (void)w;(void)h;(void)m; }
uint8_t video_force_resize_get_monitor(int m) { (void)m; return 0; }
void video_force_resize_set_monitor(uint8_t r,int m) { (void)r;(void)m; }
void svga_render_blank(svga_t *s) { (void)s; }
void svga_render_overscan_left(svga_t *s) { (void)s; }
void svga_render_overscan_right(svga_t *s) { (void)s; }
void svga_render_2bpp_lowres(svga_t *s) { (void)s; }
void svga_render_2bpp_highres(svga_t *s) { (void)s; }
void svga_render_4bpp_lowres(svga_t *s) { (void)s; }
void svga_render_4bpp_highres(svga_t *s) { (void)s; }
void svga_render_8bpp_lowres(svga_t *s) { (void)s; }

static unsigned rendered;
static uint32_t fetched;
static void capture(svga_t *s)
{
    rendered++;
    fetched = s->memaddr;
    /* A renderer consumes characters; poll must restore the row base. */
    s->memaddr += 0x40;
}

static void line(svga_t *s)
{
    assert(s->linepos == 0);
    svga_poll(s);
    assert(s->linepos == 1);
    svga_poll(s);
    assert(s->linepos == 0);
}

static unsigned cases;
static void frame_case(unsigned offset, unsigned fine, unsigned height,
                       int doubled, int interlace, unsigned split, uint32_t base)
{
    svga_t s = {0}; monitor_t monitor = {0}; uint8_t dirty[64] = {0};
    s.monitor = &monitor; s.changedvram = dirty;
    s.vram_mask = s.vram_display_mask = 0x3ffff;
    s.rowoffset = offset; s.rowoffset_extra = fine;
    s.rowcount = height - 1; s.linedbl = doubled; s.interlace = interlace;
    s.memaddr_latch = base; s.hblank_sub = 3;
    s.render = capture; s.seqregs[1] = 1;
    s.dispend = 48; s.vsyncstart = 50; s.vtotal = 54; s.split = split;
    s.clock = cpuclock * (double)(1ULL << 32) / 25175000.0;
    s.crtc[0xe] = 0x12; s.crtc[0xf] = 0x34; s.ca_adj = 0x10000;
    s.vc = 49; /* Real vsync initializes addresses for the first odd field. */
    s.linepos = 1; svga_poll(&s);
    uint32_t stride = offset * 8 + fine;
    for (unsigned field = 0; field < 4; field++) {
        unsigned parity = (field + 1) & 1;
        assert(s.oddeven == parity);
        assert(s.memaddr == (base + 3) * 4 + (interlace && parity ? stride : 0));
        assert(s.cursoraddr == 0x448d0);
        while (s.vc != 0) line(&s);
        for (unsigned y = 0; y < 48; y++) {
            unsigned local = y >= split ? y - split : y;
            uint32_t origin = y >= split ? 12 : (base + 3) * 4;
            unsigned row = local / (height * (doubled ? 2 : 1));
            uint32_t expected = (origin + stride * (row * (interlace ? 2 : 1) +
                                  (interlace ? parity : 0))) & 0x3ffff;
            unsigned before = rendered;
            line(&s);
            assert(rendered == before + 1);
            assert(fetched == expected);
        }
        while (s.vc != 50) line(&s);
    }
    cases++;
}

int main(void)
{
    for (unsigned offset = 0; offset < 256; offset += 17)
    for (unsigned fine = 0; fine <= 4; fine += 4)
    for (unsigned height = 1; height <= 4; height++)
    for (int doubled = 0; doubled < 2; doubled++)
    for (int interlace = 0; interlace < 2; interlace++)
    for (unsigned split = 16; split <= 64; split += 48)
    for (unsigned wrap = 0; wrap < 2; wrap++)
        frame_case(offset, fine, height, doubled, interlace, split,
                   wrap ? 0xffe0 : 0x1234);
    printf("VGA scanout: %u configurations, four fields each, row/double-scan/interlace/split/wrap PASS\n", cases);
    return 0;
}
