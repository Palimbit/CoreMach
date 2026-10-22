/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "frontend_internal.h"

#include <blumach/systems/olivetti_m10.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct m10_frontend_machine {
    bm_frontend_machine_t base;
    bm_m10_config_t config;
    uint8_t ram_state[BM_M10_RAM_STATE_SIZE];
    uint8_t rtc_state[BM_M10_RTC_STATE_SIZE];
} m10_frontend_machine_t;

static const uint64_t firmware_sizes[] = { BM_M10_FIRMWARE_SIZE };

static const bm_frontend_asset_requirement_t assets[] = {
    { .role = "firmware", .label = "M10 system ROM (local 32 KiB image)",
      .kind = BM_FRONTEND_ASSET_BLOB, .required = 1,
      .accepted_sizes = firmware_sizes, .accepted_size_count = 1U }
};

/* Both are battery-backed on the M10. Without saved data the RAM is blank,
 * so the firmware cold-starts, and the clock starts at the time chosen by
 * the rtc_time option. */
static const bm_frontend_persistent_state_requirement_t persistent_states[] = {
    { "ram", "Battery-backed RAM", BM_M10_RAM_STATE_SIZE, NULL, 1 },
    { "rtc", "uPD1990AC clock", BM_M10_RTC_STATE_SIZE, NULL, 1 }
};

/* rtc_time option values. */
#define RTC_TIME_HOST 0U
#define RTC_TIME_FIXED 1U

/* The frontend, not the machine, reads the host clock. */
static int64_t
host_wall_clock(void *context)
{
    (void) context;
    return (int64_t) time(NULL);
}

static bm_upd1990ac_time_t
host_local_time(void)
{
    const time_t now = time(NULL);
    struct tm local;
    bm_upd1990ac_time_t result = { 0, 0, 0, 1, 0, 1 };

#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0)
        return result;
#else
    if (localtime_r(&now, &local) == NULL)
        return result;
#endif
    result.second = (uint8_t) (local.tm_sec > 59 ? 59 : local.tm_sec);
    result.minute = (uint8_t) local.tm_min;
    result.hour = (uint8_t) local.tm_hour;
    result.day = (uint8_t) local.tm_mday;
    result.weekday = (uint8_t) local.tm_wday;   /* 0 = Sunday, as the ROM */
    result.month = (uint8_t) (local.tm_mon + 1);
    return result;
}

static void
destroy_machine(bm_frontend_machine_t *base)
{
    free(base);
}

static bm_status_t
open_machine(const bm_frontend_asset_binding_t *bindings, size_t binding_count,
             const bm_frontend_persistent_state_binding_t *state_bindings,
             size_t state_binding_count,
             const bm_frontend_machine_option_t *options, size_t option_count,
             bm_frontend_machine_t **out_machine)
{
    const bm_frontend_asset_binding_t *firmware = bm_frontend_binding_find(
        bindings, binding_count, "firmware");
    const bm_frontend_persistent_state_binding_t *ram_state =
        bm_frontend_persistent_state_binding_find(
            state_bindings, state_binding_count, "ram");
    const bm_frontend_persistent_state_binding_t *rtc_state =
        bm_frontend_persistent_state_binding_find(
            state_bindings, state_binding_count, "rtc");
    m10_frontend_machine_t *machine;
    size_t index;
    uint32_t ram_kib = 32U;
    uint32_t language = BM_M10_LANGUAGE_ITALIAN;
    uint32_t rtc_time = RTC_TIME_HOST;

    if ((firmware == NULL) || (firmware->kind != BM_FRONTEND_ASSET_BLOB) ||
        (firmware->value.blob.data == NULL) ||
        (firmware->value.blob.size != BM_M10_FIRMWARE_SIZE))
        return BM_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < option_count; ++index) {
        const char *name = options[index].name;
        const uint32_t value = options[index].value;
        if (strcmp(name, "ram_kib") == 0) {
            if ((value != 8U) && (value != 16U) && (value != 24U) &&
                (value != 32U))
                return BM_STATUS_INVALID_ARGUMENT;
            ram_kib = value;
        } else if (strcmp(name, "language") == 0) {
            if (value > BM_M10_LANGUAGE_FRENCH)
                return BM_STATUS_INVALID_ARGUMENT;
            language = value;
        } else if (strcmp(name, "rtc_time") == 0) {
            if (value > RTC_TIME_FIXED)
                return BM_STATUS_INVALID_ARGUMENT;
            rtc_time = value;
        } else {
            return BM_STATUS_INVALID_ARGUMENT;
        }
    }

    machine = calloc(1U, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    machine->base.destroy = destroy_machine;
    machine->config.firmware = firmware->value.blob;
    machine->config.ram_kib = ram_kib;
    machine->config.language = (bm_m10_language_t) language;
    if (ram_state != NULL) {
        memcpy(machine->ram_state, ram_state->data, sizeof(machine->ram_state));
        machine->config.ram_state = machine->ram_state;
        machine->config.ram_state_size = sizeof(machine->ram_state);
    }
    if (rtc_state != NULL) {
        memcpy(machine->rtc_state, rtc_state->data, sizeof(machine->rtc_state));
        machine->config.rtc_state = machine->rtc_state;
        machine->config.rtc_state_size = sizeof(machine->rtc_state);
    }
    if (rtc_time == RTC_TIME_HOST) {
        /* The clock starts at host time and runs on while switched off. */
        machine->config.rtc_initial_time = host_local_time();
        machine->config.wall_clock = host_wall_clock;
    } else {
        /* Reproducible: Sunday 1 January 00:00:00, stopped while off. */
        machine->config.rtc_initial_time =
            (bm_upd1990ac_time_t) { 0, 0, 0, 1, 0, 1 };
    }
    machine->base.configuration = bm_m10_machine_config(&machine->config);
    *out_machine = &machine->base;
    return BM_STATUS_OK;
}

const bm_frontend_adapter_t bm_frontend_m10_adapter = {
    bm_m10_machine_definition,
    assets,
    sizeof(assets) / sizeof(assets[0]),
    persistent_states,
    sizeof(persistent_states) / sizeof(persistent_states[0]),
    open_machine
};
