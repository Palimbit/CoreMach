/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_808x_test_harness.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#    define BM_SCANF scanf_s
#else
#    define BM_SCANF scanf
#endif

typedef struct queue_capture {
    bm_8088_queue_event_t events[256];
    size_t count;
    int overflow;
} queue_capture_t;

static void
capture_queue(void *context, const bm_8088_queue_event_t *event)
{
    queue_capture_t *capture = context;

    if (capture->count == sizeof(capture->events) / sizeof(capture->events[0])) {
        capture->overflow = 1;
        return;
    }
    capture->events[capture->count++] = *event;
}

static int
read_command(char *command)
{
#ifdef _MSC_VER
    return scanf_s(" %c", command, 1U) == 1;
#else
    return scanf(" %c", command) == 1;
#endif
}

static int
read_word(uint16_t *value)
{
    unsigned int parsed;
    if (BM_SCANF("%x", &parsed) != 1 || parsed > UINT16_MAX)
        return 0;
    *value = (uint16_t) parsed;
    return 1;
}

static int
read_state(bm_808x_arch_state_t *state)
{
    return read_word(&state->ax) && read_word(&state->cx) &&
           read_word(&state->dx) && read_word(&state->bx) &&
           read_word(&state->sp) && read_word(&state->bp) &&
           read_word(&state->si) && read_word(&state->di) &&
           read_word(&state->es) && read_word(&state->cs) &&
           read_word(&state->ss) && read_word(&state->ds) &&
           read_word(&state->ip) && read_word(&state->flags);
}

static void
write_state(const bm_808x_arch_state_t *state)
{
    printf(" %04" PRIx16 " %04" PRIx16 " %04" PRIx16 " %04" PRIx16,
           state->ax, state->cx, state->dx, state->bx);
    printf(" %04" PRIx16 " %04" PRIx16 " %04" PRIx16 " %04" PRIx16,
           state->sp, state->bp, state->si, state->di);
    printf(" %04" PRIx16 " %04" PRIx16 " %04" PRIx16 " %04" PRIx16,
           state->es, state->cs, state->ss, state->ds);
    printf(" %04" PRIx16 " %04" PRIx16, state->ip, state->flags);
}

static int
read_model(int argc, char **argv, bm_808x_model_t *model)
{
    if ((argc == 1) || ((argc == 3) && (strcmp(argv[1], "--model") == 0) &&
                        (strcmp(argv[2], "nec-v30") == 0))) {
        *model = BM_808X_NEC_V30;
        return 1;
    }
    if ((argc == 3) && (strcmp(argv[1], "--model") == 0) &&
        (strcmp(argv[2], "intel-8088") == 0)) {
        *model = BM_808X_INTEL_8088;
        return 1;
    }
    fprintf(stderr,
            "usage: %s [--model nec-v30|intel-8088]\n",
            argc > 0 ? argv[0] : "portable-engine-808x-vector-runner");
    return 0;
}

int
main(int argc, char **argv)
{
    cpu_808x_test_machine_t machine;
    cpu_808x_test_config_t config = { 0 };
    bm_808x_arch_state_t state;
    static const uint8_t empty_program[] = { 0U };
    queue_capture_t queue_capture = { 0 };
    char command;

    if (!read_model(argc, argv, &config.model))
        return 2;
    if (config.model == BM_808X_INTEL_8088) {
        config.intel_queue_event = capture_queue;
        config.intel_queue_event_context = &queue_capture;
    }
    cpu_808x_test_machine_create(&machine, &config, empty_program, 0U);
    state = cpu_808x_test_get_state(&machine);
    while (read_command(&command)) {
        unsigned int initial_count;
        unsigned int queue_count = 0U;
        unsigned int query_count;
        unsigned int index;
        bm_808x_prefetch_state_t prefetch = { 0 };
        bm_tick_t consumed = 0U;
        bm_status_t status;

        if (command == 'Q')
            break;
        if ((command != 'C' && command != 'H') || !read_state(&state) ||
            BM_SCANF("%u", &initial_count) != 1)
            return 2;
        state.halted = 0U;
        for (index = 0U; index < initial_count; ++index) {
            uint32_t address;
            unsigned int value;
            if (BM_SCANF("%" SCNx32 " %x", &address, &value) != 2 ||
                address >= CPU_808X_TEST_IMAGE_SIZE || value > UINT8_MAX)
                return 2;
            cpu_808x_test_poke(&machine, address, (uint8_t) value);
        }
        if (command == 'H') {
            if (BM_SCANF("%u", &queue_count) != 1 ||
                queue_count > BM_808X_MAX_PREFETCH_QUEUE_CAPACITY)
                return 2;
            prefetch = (bm_808x_prefetch_state_t) {
                .size = sizeof(prefetch),
                .version = BM_808X_PREFETCH_STATE_VERSION,
                .pointer = state.ip,
                .count = (uint8_t) queue_count,
                .capacity = config.model == BM_808X_INTEL_8088 ?
                    BM_808X_8088_PREFETCH_QUEUE_CAPACITY :
                    BM_808X_V30_PREFETCH_QUEUE_CAPACITY,
                .bytes = { 0U }
            };
            for (index = 0U; index < queue_count; ++index) {
                unsigned int value;
                if (BM_SCANF("%x", &value) != 1 || value > UINT8_MAX)
                    return 2;
                prefetch.bytes[index] = (uint8_t) value;
            }
            prefetch.pointer = (uint16_t) (prefetch.pointer + queue_count);
        }
        if (BM_SCANF("%u", &query_count) != 1)
            return 2;
        cpu_808x_test_set_state(&machine, &state);
        if (command == 'H') {
            status = bm_808x_set_prefetch_state(&machine.cpu, &prefetch);
            if (status != BM_STATUS_OK)
                return 2;
        }
        queue_capture.count = 0U;
        queue_capture.overflow = 0;
        status = cpu_808x_test_step(&machine, &consumed);
        if (queue_capture.overflow)
            return 2;
        state = cpu_808x_test_get_state(&machine);
        printf("%c %d %" PRIu64, command == 'H' ? 'H' : 'R',
               (int) status, (uint64_t) consumed);
        write_state(&state);
        printf(" %u", query_count);
        for (index = 0U; index < query_count; ++index) {
            uint32_t address;
            if (BM_SCANF("%" SCNx32, &address) != 1 ||
                address >= CPU_808X_TEST_IMAGE_SIZE)
                return 2;
            printf(" %02" PRIx8, cpu_808x_test_peek(&machine, address));
        }
        if (command == 'H') {
            if (bm_808x_get_prefetch_state(&machine.cpu, &prefetch) !=
                BM_STATUS_OK)
                return 2;
            printf(" %u", (unsigned int) prefetch.count);
            for (index = 0U; index < prefetch.count; ++index)
                printf(" %02" PRIx8, prefetch.bytes[index]);
            printf(" %04" PRIx16, prefetch.pointer);
            printf(" %zu", queue_capture.count);
            for (index = 0U; index < queue_capture.count; ++index) {
                const bm_8088_queue_event_t *event =
                    &queue_capture.events[index];
                const char kind = event->kind == BM_8088_QUEUE_READ_FIRST ?
                    'F' : event->kind == BM_8088_QUEUE_READ_SUBSEQUENT ?
                    'S' : 'E';
                printf(" %c %02" PRIx8, kind, event->value);
            }
        }
        putchar('\n');
        fflush(stdout);
    }
    cpu_808x_test_machine_destroy(&machine);
    return 0;
}
