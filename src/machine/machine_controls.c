/*
 * BluMach runtime-machine-control registry.
 *
 * Machine implementations register static descriptors after their device is
 * live.  The UI consumes the descriptors without knowing any product ID.
 */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <86box/machine.h>

static _Atomic(const machine_runtime_control_t *) runtime_controls = NULL;
static atomic_size_t                             runtime_control_count;
static atomic_uint                               runtime_control_generation;

const machine_runtime_control_t *
machine_runtime_controls_get(size_t *count)
{
    const machine_runtime_control_t *controls;
    unsigned                         generation_before;
    unsigned                         generation_after;
    size_t                           local_count;

    do {
        generation_before = atomic_load(&runtime_control_generation);
        if (generation_before & 1)
            continue;
        controls          = atomic_load(&runtime_controls);
        local_count       = atomic_load(&runtime_control_count);
        generation_after  = atomic_load(&runtime_control_generation);
    } while (generation_before != generation_after || (generation_after & 1));

    if (count != NULL)
        *count = local_count;
    return controls;
}

void
machine_runtime_controls_set(const machine_runtime_control_t *controls, size_t count)
{
    atomic_fetch_add(&runtime_control_generation, 1);
    atomic_store(&runtime_control_count, count);
    atomic_store(&runtime_controls, controls);
    atomic_fetch_add(&runtime_control_generation, 1);
}

void
machine_runtime_controls_clear(const machine_runtime_control_t *controls)
{
    const machine_runtime_control_t *expected = controls;

    atomic_fetch_add(&runtime_control_generation, 1);
    if (atomic_compare_exchange_strong(&runtime_controls, &expected, NULL))
        atomic_store(&runtime_control_count, 0);
    atomic_fetch_add(&runtime_control_generation, 1);
}
