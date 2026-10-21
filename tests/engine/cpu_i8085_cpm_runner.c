/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Manual runner for CP/M instruction exercisers such as 8080EXM or CPUTEST.
 * The program is supplied by path and is never part of the repository or a
 * CTest. Only run locally preserved, scanned copies. BDOS functions 2 and 9
 * are emulated; a jump to 0000h ends the run. These suites target the 8080,
 * so 8085 differences (ANA sets AC, PSW bits 1 and 5 hold V and K, faster
 * conditional instructions) can legitimately change their CRCs. */
#include "cpu_i8085_test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct counters {
    uint64_t t_states;
} counters_t;

static void
count(void *context, const bm_i8085_timing_observation_t *observation)
{
    counters_t *counters = context;
    counters->t_states += observation->t_states;
}

static int
load_program(i8085_test_machine_t *machine, const char *path)
{
    FILE *file = fopen(path, "rb");
    size_t size;

    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0;
    }
    size = fread(machine->memory + 0x0100U, 1U, 0xe400U - 0x0100U, file);
    if (!feof(file) || (size == 0U)) {
        fprintf(stderr, "%s is empty or larger than the 57 KiB TPA\n", path);
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static void
bdos(const i8085_test_machine_t *machine, const bm_i8085_arch_state_t *state)
{
    if (state->c == 2U) {
        putchar(state->e);
    } else if (state->c == 9U) {
        uint16_t address = (uint16_t) (((unsigned int) state->d << 8U) | state->e);
        while (machine->memory[address] != '$') {
            putchar(machine->memory[address]);
            address = (uint16_t) (address + 1U);
        }
    }
    fflush(stdout);
}

int
main(int argc, char **argv)
{
    i8085_test_machine_t machine;
    i8085_test_config_t config = { 0 };
    counters_t counters = { 0 };
    bm_i8085_arch_state_t state;
    uint64_t instructions = 0U;
    uint64_t limit = argc > 2 ? strtoull(argv[2], NULL, 10) : UINT64_C(50000000000);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <program.com> [instruction-limit]\n", argv[0]);
        return 2;
    }
    config.timing = count;
    config.timing_context = &counters;
    i8085_test_create(&machine, &config);
    if (!load_program(&machine, argv[1])) {
        i8085_test_destroy(&machine);
        return 2;
    }
    machine.memory[0x0005U] = 0xc9U;            /* BDOS entry: RET */
    machine.memory[0x0006U] = 0x00U;            /* top of TPA at E400h */
    machine.memory[0x0007U] = 0xe4U;
    state = i8085_test_state(&machine);
    state.pc = 0x0100U;
    state.sp = 0xe400U;
    i8085_test_set_state(&machine, &state);

    for (;;) {
        bm_tick_t consumed = 0U;
        bm_status_t status;

        state = i8085_test_state(&machine);
        if (state.pc == 0x0000U)
            break;
        if (state.pc == 0x0005U)
            bdos(&machine, &state);
        if (instructions == limit) {
            fprintf(stderr, "\ninstruction limit reached at PC %04X\n", state.pc);
            i8085_test_destroy(&machine);
            return 3;
        }
        status = bm_i8085_step(&machine.cpu, &consumed);
        if (status != BM_STATUS_OK) {
            fprintf(stderr, "\nstatus %d at PC %04X\n", (int) status, state.pc);
            i8085_test_destroy(&machine);
            return 1;
        }
        ++instructions;
    }
    printf("\n%llu instructions, %llu T-states\n",
           (unsigned long long) instructions,
           (unsigned long long) counters.t_states);
    i8085_test_destroy(&machine);
    return 0;
}
