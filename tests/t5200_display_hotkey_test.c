/* BluMach T5200 documented display-hotkey contract test. GPL-2.0-or-later.
 * Copyright 2026 rtzor, Project BluMach.
 * Runs the host-key filter only; it never executes proprietary firmware.
 */
#include <assert.h>
#include <string.h>

#define T5200_HOTKEY_TEST
#include "../src/machine/m_at_toshiba_t5200.c"

static const char *test_machine = "t5200";
static int         left_ctrl;
static int         right_ctrl;
static int         panel_calls;
static int         panel_value;

const char *
machine_get_internal_name(void)
{
    return test_machine;
}

int
keyboard_recv_ui(uint16_t scan)
{
    if (scan == 0x01d)
        return left_ctrl;
    if (scan == 0x11d)
        return right_ctrl;
    return 0;
}

void
paradise_t5200_panel_set(void *priv, int enabled)
{
    assert(priv == (void *) 1);
    panel_calls++;
    panel_value = enabled;
}

int
main(void)
{
    t5200_video = (void *) 1;

    test_machine = "other";
    left_ctrl = 1;
    assert(!t5200_display_hotkey(1, 0x47));
    assert(panel_calls == 0);

    test_machine = "t5200";
    left_ctrl = 0;
    assert(!t5200_display_hotkey(1, 0x47));
    assert(panel_calls == 0);

    left_ctrl = 1;
    assert(t5200_display_hotkey(1, 0x47));
    assert(panel_calls == 1 && panel_value == 1);
    assert(t5200_display_hotkey(1, 0x47));
    assert(panel_calls == 1);
    assert(t5200_display_hotkey(0, 0x47));

    left_ctrl = 0;
    right_ctrl = 1;
    assert(t5200_display_hotkey(1, 0x147));
    assert(panel_calls == 2 && panel_value == 1);
    assert(t5200_display_hotkey(0, 0x147));

    assert(!t5200_display_hotkey(1, 0x14f));
    assert(panel_calls == 2);

    t5200_video = NULL;
    assert(!t5200_display_hotkey(1, 0x47));
    return 0;
}
