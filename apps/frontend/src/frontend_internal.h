/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLUMACH_FRONTEND_INTERNAL_H
#define BLUMACH_FRONTEND_INTERNAL_H

#include <blumach/frontend/frontend.h>

struct bm_frontend_adapter {
    const bm_machine_definition_t *(*definition)(void);
    const bm_frontend_asset_requirement_t *assets;
    size_t asset_count;
    const bm_frontend_persistent_state_requirement_t *persistent_states;
    size_t persistent_state_count;
    bm_status_t (*open)(const bm_frontend_asset_binding_t *bindings,
                        size_t binding_count,
                        const bm_frontend_persistent_state_binding_t *state_bindings,
                        size_t state_binding_count,
                        bm_frontend_machine_t **out_machine);
};

struct bm_frontend_machine {
    bm_machine_config_t configuration;
    bm_frontend_diagnostics_t diagnostics;
    bm_frontend_debug_observer_fn debug_observer;
    void *debug_context;
    uint64_t debug_sequence;
    void (*destroy)(bm_frontend_machine_t *machine);
};

extern const bm_frontend_adapter_t bm_frontend_pcs86_adapter;
extern const bm_frontend_adapter_t bm_frontend_m15_adapter;

const bm_frontend_asset_binding_t *bm_frontend_binding_find(
    const bm_frontend_asset_binding_t *bindings, size_t binding_count,
    const char *role);
const bm_frontend_persistent_state_binding_t *
bm_frontend_persistent_state_binding_find(
    const bm_frontend_persistent_state_binding_t *bindings,
    size_t binding_count, const char *role);

#endif
