/* Runtime-machine-control registry tests. SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <86box/machine.h>

#include "../src/machine/machine_controls.c"

static int control_value;

static int
control_get(void)
{
    return control_value;
}

static void
control_set(int value)
{
    control_value = value;
}

static const char *const values[] = { "Off", "On" };
static const machine_runtime_control_t controls[] = {
    { "test.toggle", "Test toggle", MACHINE_RUNTIME_CONTROL_SELECTOR,
      values, 2, control_get, control_set }
};
static const machine_runtime_control_t unrelated_controls[] = {
    { "test.unrelated", "Unrelated", MACHINE_RUNTIME_CONTROL_SELECTOR,
      values, 2, control_get, control_set }
};

int
main(void)
{
    const machine_runtime_control_t *active;
    size_t count = 99;

    active = machine_runtime_controls_get(&count);
    assert(active == NULL && count == 0);

    machine_runtime_controls_set(controls, 1);
    active = machine_runtime_controls_get(&count);
    assert(active == controls && count == 1);
    active[0].set(1);
    assert(active[0].get() == 1);

    machine_runtime_controls_clear(unrelated_controls);
    active = machine_runtime_controls_get(&count);
    assert(active == controls && count == 1);

    machine_runtime_controls_clear(controls);
    active = machine_runtime_controls_get(&count);
    assert(active == NULL && count == 0);
    return 0;
}
