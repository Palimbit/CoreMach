/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors
 * Draft PCS286 board glue, distinct from the reusable IOC02 register model.
 */
#ifndef BLUMACH_SYSTEMS_PCS286_BOARD_H
#define BLUMACH_SYSTEMS_PCS286_BOARD_H
#include <blumach/components/at_bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bm_pcs286_board bm_pcs286_board_t;
typedef struct bm_pcs286_board_config {
    bm_at_line_fn a20;
    bm_at_line_fn nmi;
    bm_at_line_fn pit2_gate;
    bm_at_line_fn speaker_enable;
    bm_at_line_fn cpu_reset_request;
    void *output_context;
} bm_pcs286_board_config_t;
typedef struct bm_pcs286_board_state {
    uint8_t port61, port62, port63;
    int a20_enabled, nmi_asserted, reset_requested;
} bm_pcs286_board_state_t;
bm_status_t bm_pcs286_board_create(const bm_host_services_t *host,
                                   const bm_pcs286_board_config_t *config,
                                   bm_pcs286_board_t **out_board);
void bm_pcs286_board_destroy(bm_pcs286_board_t *board);
void bm_pcs286_board_reset(bm_pcs286_board_t *board);
bm_status_t bm_pcs286_board_io(void *context, bm_bus_transaction_t *transaction);
bm_status_t bm_pcs286_board_kbc_a20(bm_pcs286_board_t *board, int level);
bm_status_t bm_pcs286_board_kbc_reset(bm_pcs286_board_t *board, int level);
bm_status_t bm_pcs286_board_nmi_mask(bm_pcs286_board_t *board, int level);
bm_status_t bm_pcs286_board_parity(bm_pcs286_board_t *board, int level);
bm_status_t bm_pcs286_board_io_check(bm_pcs286_board_t *board, int level);
bm_status_t bm_pcs286_board_pit_output(bm_pcs286_board_t *board,
                                       unsigned int channel, int level);
bm_status_t bm_pcs286_board_state(const bm_pcs286_board_t *board,
                                  bm_pcs286_board_state_t *out_state);
/* Port 61h-63h decode, refresh toggle, parity/NMI latches and A20-source
 * combination require board evidence. KBC reset requests are latched and
 * applied after the current CPU boundary; never re-enter CPU reset from an
 * I/O callback. CPU warm reset preserves RAM and CMOS. The runtime's user
 * reset is a separate full-machine reset path. Do not add generic port 92h
 * merely because a later PC implements it. */
#ifdef __cplusplus
}
#endif
#endif
