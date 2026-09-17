/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 rtzor
 *
 * Derived rewrite of BluMach's original PCS 86 machine support. This first
 * stage contains the documented V30, conventional RAM, system ROM map and
 * minimum motherboard I/O used to establish the platform contract.
 */
#include <blumach/systems/olivetti_pcs86.h>

#include <blumach/components/bus.h>
#include <blumach/components/dma8237.h>
#include <blumach/components/dma_page_registers.h>
#include <blumach/components/fdc765.h>
#include <blumach/components/floppy_drive.h>
#include <blumach/components/linear_memory.h>
#include <blumach/components/lpt_spp.h>
#include <blumach/components/pic8259.h>
#include <blumach/components/pit8253.h>
#include <blumach/components/pvga1a.h>
#include <blumach/components/rtc_mm58167.h>
#include <blumach/components/uart16450.h>
#include <blumach/components/xta.h>

#include <ctype.h>
#include <string.h>

typedef struct bm_pcs86_machine {
    bm_host_services_t host;
    bm_bus_t *bus;
    uint8_t *conventional_ram;
    uint8_t *ems_ram;
    size_t ems_size;
    uint16_t ems_pages;
    bm_linear_memory_t *rom;
    bm_dma8237_t *dma;
    bm_dma_page_registers_t *dma_pages;
    bm_floppy_drive_t *floppy[2];
    bm_fdc765_t *fdc;
    bm_pic8259_t *pic;
    bm_pit8253_t *pit;
    bm_pvga1a_t *video;
    bm_mm58167_t *rtc;
    bm_lpt_spp_t *lpt;
    bm_uart16450_t *uart;
    bm_xta_t *xta;
    bm_engine_t *engine;
    bm_cpu_id_t cpu_id;
    int cpu_ready;
    uint8_t port61;
    uint8_t control;
    uint8_t memory_blocks;
    uint8_t glue[16];
    uint8_t ps2[5];
    uint8_t ps2_queue[2][16];
    uint8_t ps2_queue_start[2];
    uint8_t ps2_queue_end[2];
    uint8_t ps2_pending_command[2];
    uint8_t keyboard_leds;
    uint8_t scan_queue[64];
    uint8_t scan_queue_start;
    uint8_t scan_queue_end;
    uint8_t keyboard_data_latch;
    uint8_t jumpers;
    uint8_t nmi_mask;
    uint8_t diagnostic_port;
    uint8_t memory_control_latch;
    uint8_t video_setup_latch;
    uint8_t video_select_latch;
    uint8_t ems_page_selector[4];
    bm_tick_t last_clock_time;
    uint64_t pit_clock_remainder;
    uint64_t rtc_clock_remainder;
    bm_pcs86_io_trace_fn io_trace;
    void *io_trace_context;
} bm_pcs86_machine_t;

/* The current interpreter reports retired instructions, not V30 clock cycles.
 * Use the measured functional scheduler rate until cycle accounting becomes
 * part of the CPU contract; this is not a claim of cycle accuracy. */
#define PCS86_SCHEDULER_TICKS_PER_SECOND UINT64_C(2000000)
#define PCS86_PIT_TICKS_PER_SECOND UINT64_C(1193182)
#define PCS86_CLOCK_QUANTUM UINT64_C(64)
#define PCS86_EMS_APERTURE_BASE UINT32_C(0x80000)
#define PCS86_EMS_APERTURE_SIZE UINT32_C(0x10000)
#define PCS86_EMS_WINDOW_SIZE UINT32_C(0x4000)

static uint8_t *
pcs86_memory_pointer(bm_pcs86_machine_t *machine, uint64_t address)
{
    if ((address >= PCS86_EMS_APERTURE_BASE) &&
        (address < PCS86_EMS_APERTURE_BASE + PCS86_EMS_APERTURE_SIZE)) {
        size_t aperture_offset = (size_t) (address - PCS86_EMS_APERTURE_BASE);
        size_t window = aperture_offset / PCS86_EMS_WINDOW_SIZE;
        uint8_t selector = machine->ems_page_selector[window];
        size_t page = (size_t) (selector & 0x7fU);

        if (((selector & 0x80U) != 0U) && (page < machine->ems_pages))
            return &machine->ems_ram[page * PCS86_EMS_WINDOW_SIZE +
                                     (aperture_offset & (PCS86_EMS_WINDOW_SIZE - 1U))];
    }
    return &machine->conventional_ram[address];
}

static bm_status_t
pcs86_memory_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint32_t index;

    if ((transaction == NULL) || (transaction->size == 0U) ||
        (transaction->size > sizeof(transaction->value)) ||
        (transaction->address >= BM_PCS86_MEMORY_SIZE) ||
        (transaction->size > BM_PCS86_MEMORY_SIZE - (size_t) transaction->address))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->operation == BM_BUS_WRITE) {
        for (index = 0U; index < transaction->size; ++index) {
            uint32_t shift = (transaction->endianness == BM_ENDIAN_LITTLE)
                                 ? index * 8U
                                 : (transaction->size - index - 1U) * 8U;
            *pcs86_memory_pointer(machine, transaction->address + index) =
                (uint8_t) (transaction->value >> shift);
        }
        return BM_STATUS_OK;
    }
    if ((transaction->operation != BM_BUS_READ) &&
        (transaction->operation != BM_BUS_FETCH))
        return BM_STATUS_INVALID_ARGUMENT;
    transaction->value = 0U;
    for (index = 0U; index < transaction->size; ++index) {
        uint32_t shift = (transaction->endianness == BM_ENDIAN_LITTLE)
                             ? index * 8U
                             : (transaction->size - index - 1U) * 8U;
        transaction->value |=
            (uint64_t) *pcs86_memory_pointer(machine, transaction->address + index) << shift;
    }
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_open_bus_access(void *context, bm_bus_transaction_t *transaction)
{
    uint32_t index;
    (void) context;
    if ((transaction == NULL) || (transaction->size == 0U) ||
        (transaction->size > sizeof(transaction->value)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->operation == BM_BUS_WRITE)
        return BM_STATUS_OK;
    if (transaction->operation == BM_BUS_FETCH)
        return BM_STATUS_UNSUPPORTED;
    transaction->value = 0;
    for (index = 0; index < transaction->size; ++index)
        transaction->value |= UINT64_C(0xff) << (index * 8U);
    return BM_STATUS_OK;
}

static void
pcs86_lpt_irq(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    (void) bm_pic8259_set_irq(machine->pic, 7U,
                              ((machine->control & 0x02U) != 0) && asserted);
}

static void
pcs86_uart_irq(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    (void) bm_pic8259_set_irq(machine->pic, 4U,
                              ((machine->control & 0x10U) != 0) && asserted);
}

static void
pcs86_xta_irq(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    (void) bm_pic8259_set_irq(machine->pic, 5U, asserted);
}

static void
pcs86_fdc_irq(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    (void) bm_pic8259_set_irq(machine->pic, 6U, asserted);
}

static bm_status_t
pcs86_lpt_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint8_t value = (uint8_t) transaction->value;
    unsigned int register_index;

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    register_index = (unsigned int) (transaction->address - 0x0378U);
    if ((transaction->operation == BM_BUS_WRITE) && (register_index == 0U))
        machine->diagnostic_port = value;
    if ((machine->control & 0x02U) == 0) {
        if (transaction->operation == BM_BUS_READ)
            transaction->value = 0xffU;
        return BM_STATUS_OK;
    }
    if (transaction->operation == BM_BUS_READ) {
        bm_status_t status = bm_lpt_spp_read(machine->lpt, register_index, &value);
        transaction->value = value;
        return status;
    }
    return bm_lpt_spp_write(machine->lpt, register_index, value);
}

static bm_status_t
pcs86_uart_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint8_t value = (uint8_t) transaction->value;
    unsigned int register_index;

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if ((machine->control & 0x10U) == 0) {
        if (transaction->operation == BM_BUS_READ)
            transaction->value = 0xffU;
        return BM_STATUS_OK;
    }
    register_index = (unsigned int) (transaction->address - 0x03f8U);
    if (transaction->operation == BM_BUS_READ) {
        bm_status_t status = bm_uart16450_read(machine->uart, register_index, &value);
        transaction->value = value;
        return status;
    }
    return bm_uart16450_write(machine->uart, register_index, value);
}

static bm_status_t
pcs86_queue_push(uint8_t *queue, uint8_t *start, uint8_t *end,
                 uint8_t mask, uint8_t value)
{
    uint8_t next = (uint8_t) ((*end + 1U) & mask);
    if (next == *start)
        return BM_STATUS_CAPACITY_EXCEEDED;
    queue[*end] = value;
    *end = next;
    return BM_STATUS_OK;
}

static unsigned int
pcs86_queue_free(uint8_t start, uint8_t end, uint8_t mask)
{
    return (unsigned int) mask - (unsigned int) ((end - start) & mask);
}

static bm_status_t
pcs86_ps2_queue_push(bm_pcs86_machine_t *machine, unsigned int channel,
                     uint8_t value)
{
    return pcs86_queue_push(machine->ps2_queue[channel],
                            &machine->ps2_queue_start[channel],
                            &machine->ps2_queue_end[channel], 0x0fU, value);
}

static uint8_t
pcs86_ps2_queue_pop(bm_pcs86_machine_t *machine, unsigned int channel,
                    uint8_t fallback)
{
    uint8_t value = fallback;
    if (machine->ps2_queue_start[channel] != machine->ps2_queue_end[channel]) {
        value = machine->ps2_queue[channel][machine->ps2_queue_start[channel]];
        machine->ps2_queue_start[channel] =
            (uint8_t) ((machine->ps2_queue_start[channel] + 1U) & 0x0fU);
    }
    return value;
}

static bm_status_t
pcs86_ps2_command(bm_pcs86_machine_t *machine, unsigned int channel,
                  uint8_t command)
{
    unsigned int response_size = 1U;
    bm_status_t status;

    if ((channel == 0U) && (command < 0xedU) &&
        (machine->ps2_pending_command[channel] == 0xedU)) {
        if (pcs86_queue_free(machine->ps2_queue_start[channel],
                             machine->ps2_queue_end[channel], 0x0fU) < 1U)
            return BM_STATUS_CAPACITY_EXCEEDED;
        status = pcs86_ps2_queue_push(machine, channel, 0xfaU);
        if (status == BM_STATUS_OK) {
            machine->keyboard_leds = command & 0x07U;
            machine->ps2_pending_command[channel] = 0U;
        }
        return status;
    }

    if ((command == 0xf2U) || (command == 0xffU))
        response_size = (channel == 1U) ? 3U :
                        ((command == 0xf2U) ? 3U : 2U);
    if (pcs86_queue_free(machine->ps2_queue_start[channel],
                         machine->ps2_queue_end[channel], 0x0fU) < response_size)
        return BM_STATUS_CAPACITY_EXCEEDED;

    status = pcs86_ps2_queue_push(machine, channel, 0xfaU);
    if (status != BM_STATUS_OK)
        return status;
    machine->ps2_pending_command[channel] =
        ((channel == 0U) && (command == 0xedU)) ? command : 0U;
    if (command == 0xf2U) {
        if (channel == 0U) {
            status = pcs86_ps2_queue_push(machine, channel, 0xabU);
            if (status == BM_STATUS_OK)
                status = pcs86_ps2_queue_push(machine, channel, 0x83U);
        } else
            status = pcs86_ps2_queue_push(machine, channel, 0x00U);
    } else if (command == 0xffU) {
        if (channel == 0U)
            machine->keyboard_leds = 0U;
        status = pcs86_ps2_queue_push(machine, channel, 0xaaU);
        if ((status == BM_STATUS_OK) && (channel == 1U))
            status = pcs86_ps2_queue_push(machine, channel, 0x00U);
    }
    return status;
}

static bm_status_t
pcs86_scan_queue_push(bm_pcs86_machine_t *machine, uint8_t value)
{
    bm_status_t status = pcs86_queue_push(machine->scan_queue,
                                          &machine->scan_queue_start,
                                          &machine->scan_queue_end,
                                          0x3fU, value);
    if (status == BM_STATUS_OK)
        (void) bm_pic8259_set_irq(machine->pic, 1U, 1);
    return status;
}

static bm_status_t
pcs86_key_to_set1(bm_key_code_t key, uint8_t *scan, int *extended)
{
    *extended = 0;
    switch (key) {
        case BM_KEY_ESCAPE: *scan = 0x01U; break;
        case BM_KEY_1: *scan = 0x02U; break;
        case BM_KEY_2: *scan = 0x03U; break;
        case BM_KEY_3: *scan = 0x04U; break;
        case BM_KEY_4: *scan = 0x05U; break;
        case BM_KEY_5: *scan = 0x06U; break;
        case BM_KEY_6: *scan = 0x07U; break;
        case BM_KEY_7: *scan = 0x08U; break;
        case BM_KEY_8: *scan = 0x09U; break;
        case BM_KEY_9: *scan = 0x0aU; break;
        case BM_KEY_0: *scan = 0x0bU; break;
        case BM_KEY_MINUS: *scan = 0x0cU; break;
        case BM_KEY_EQUAL: *scan = 0x0dU; break;
        case BM_KEY_BACKSPACE: *scan = 0x0eU; break;
        case BM_KEY_TAB: *scan = 0x0fU; break;
        case BM_KEY_Q: *scan = 0x10U; break;
        case BM_KEY_W: *scan = 0x11U; break;
        case BM_KEY_E: *scan = 0x12U; break;
        case BM_KEY_R: *scan = 0x13U; break;
        case BM_KEY_T: *scan = 0x14U; break;
        case BM_KEY_Y: *scan = 0x15U; break;
        case BM_KEY_U: *scan = 0x16U; break;
        case BM_KEY_I: *scan = 0x17U; break;
        case BM_KEY_O: *scan = 0x18U; break;
        case BM_KEY_P: *scan = 0x19U; break;
        case BM_KEY_LEFT_BRACKET: *scan = 0x1aU; break;
        case BM_KEY_RIGHT_BRACKET: *scan = 0x1bU; break;
        case BM_KEY_ENTER: *scan = 0x1cU; break;
        case BM_KEY_LEFT_CONTROL: *scan = 0x1dU; break;
        case BM_KEY_A: *scan = 0x1eU; break;
        case BM_KEY_S: *scan = 0x1fU; break;
        case BM_KEY_D: *scan = 0x20U; break;
        case BM_KEY_F: *scan = 0x21U; break;
        case BM_KEY_G: *scan = 0x22U; break;
        case BM_KEY_H: *scan = 0x23U; break;
        case BM_KEY_J: *scan = 0x24U; break;
        case BM_KEY_K: *scan = 0x25U; break;
        case BM_KEY_L: *scan = 0x26U; break;
        case BM_KEY_SEMICOLON: *scan = 0x27U; break;
        case BM_KEY_APOSTROPHE: *scan = 0x28U; break;
        case BM_KEY_GRAVE: *scan = 0x29U; break;
        case BM_KEY_LEFT_SHIFT: *scan = 0x2aU; break;
        case BM_KEY_BACKSLASH: *scan = 0x2bU; break;
        case BM_KEY_Z: *scan = 0x2cU; break;
        case BM_KEY_X: *scan = 0x2dU; break;
        case BM_KEY_C: *scan = 0x2eU; break;
        case BM_KEY_V: *scan = 0x2fU; break;
        case BM_KEY_B: *scan = 0x30U; break;
        case BM_KEY_N: *scan = 0x31U; break;
        case BM_KEY_M: *scan = 0x32U; break;
        case BM_KEY_COMMA: *scan = 0x33U; break;
        case BM_KEY_PERIOD: *scan = 0x34U; break;
        case BM_KEY_SLASH: *scan = 0x35U; break;
        case BM_KEY_RIGHT_SHIFT: *scan = 0x36U; break;
        case BM_KEY_LEFT_ALT: *scan = 0x38U; break;
        case BM_KEY_SPACE: *scan = 0x39U; break;
        case BM_KEY_CAPS_LOCK: *scan = 0x3aU; break;
        case BM_KEY_F1: *scan = 0x3bU; break;
        case BM_KEY_F2: *scan = 0x3cU; break;
        case BM_KEY_F3: *scan = 0x3dU; break;
        case BM_KEY_F4: *scan = 0x3eU; break;
        case BM_KEY_F5: *scan = 0x3fU; break;
        case BM_KEY_F6: *scan = 0x40U; break;
        case BM_KEY_F7: *scan = 0x41U; break;
        case BM_KEY_F8: *scan = 0x42U; break;
        case BM_KEY_F9: *scan = 0x43U; break;
        case BM_KEY_F10: *scan = 0x44U; break;
        case BM_KEY_SCROLL_LOCK: *scan = 0x46U; break;
        case BM_KEY_NON_US_BACKSLASH: *scan = 0x56U; break;
        case BM_KEY_HOME: *scan = 0x47U; *extended = 1; break;
        case BM_KEY_UP: *scan = 0x48U; *extended = 1; break;
        case BM_KEY_PAGE_UP: *scan = 0x49U; *extended = 1; break;
        case BM_KEY_LEFT: *scan = 0x4bU; *extended = 1; break;
        case BM_KEY_RIGHT: *scan = 0x4dU; *extended = 1; break;
        case BM_KEY_END: *scan = 0x4fU; *extended = 1; break;
        case BM_KEY_DOWN: *scan = 0x50U; *extended = 1; break;
        case BM_KEY_PAGE_DOWN: *scan = 0x51U; *extended = 1; break;
        case BM_KEY_INSERT: *scan = 0x52U; *extended = 1; break;
        case BM_KEY_DELETE: *scan = 0x53U; *extended = 1; break;
        case BM_KEY_RIGHT_CONTROL: *scan = 0x1dU; *extended = 1; break;
        case BM_KEY_RIGHT_ALT: *scan = 0x38U; *extended = 1; break;
        default: return BM_STATUS_UNSUPPORTED;
    }
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_input(void *context, const bm_input_event_t *event)
{
    bm_pcs86_machine_t *machine = context;
    uint8_t scan;
    int extended;
    bm_status_t status;

    if ((machine == NULL) || (event == NULL) ||
        (event->kind != BM_INPUT_KEY))
        return BM_STATUS_INVALID_ARGUMENT;
    status = pcs86_key_to_set1(event->key, &scan, &extended);
    if (status != BM_STATUS_OK)
        return status;
    if (pcs86_queue_free(machine->scan_queue_start,
                         machine->scan_queue_end, 0x3fU) <
        (extended ? 2U : 1U))
        return BM_STATUS_CAPACITY_EXCEEDED;
    if (extended) {
        status = pcs86_scan_queue_push(machine, 0xe0U);
        if (status != BM_STATUS_OK)
            return status;
    }
    return pcs86_scan_queue_push(machine,
                                 event->pressed ? scan : (uint8_t) (scan | 0x80U));
}

static bm_status_t
pcs86_unpopulated_option_rom_access(void *context, bm_bus_transaction_t *transaction)
{
    uint32_t index;

    (void) context;
    if ((transaction == NULL) || (transaction->size == 0) ||
        (transaction->size > sizeof(transaction->value)))
        return BM_STATUS_INVALID_ARGUMENT;
    if (transaction->operation == BM_BUS_WRITE)
        return BM_STATUS_OK;
    if ((transaction->operation != BM_BUS_READ) &&
        (transaction->operation != BM_BUS_FETCH))
        return BM_STATUS_INVALID_ARGUMENT;

    transaction->value = 0;
    for (index = 0; index < transaction->size; ++index)
        transaction->value |= UINT64_C(0xff) << (index * 8U);
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_diagnostic_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ) {
        /* The inherited PCS 86 maps A0h-AEh as a write-only NMI-mask
         * aperture. Reads therefore see the PC open-bus value. */
        transaction->value = 0xffU;
    } else {
        uint8_t value = (uint8_t) transaction->value;
        machine->nmi_mask = value & 0x80U;
        if (transaction->address == 0x00a0U)
            machine->diagnostic_port = value;
    }
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_memory_control_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;

    if ((transaction->size != 1) || (transaction->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;

    /*
     * BIOS 1.09 writes 40h here while enabling the upper conventional-memory
     * path, between accesses to the board registers at 6Ch, 6Bh and 6Fh. No
     * surviving PCS 86 documentation currently defines the individual bits.
     * Keep the write explicit and observable without assigning guessed AT
     * CMOS/NMI side effects to this XT-class machine.
     */
    machine->memory_control_latch = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_video_arbitration_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;

    if ((transaction->size != 1) || (transaction->operation != BM_BUS_WRITE))
        return BM_STATUS_UNSUPPORTED;

    /*
     * BIOS 1.09 brackets writes to 102h with writes to 46E8h while choosing
     * between internal and secondary video. The values are retained for
     * deterministic inspection, but no undocumented arbitration side effects
     * are assigned until the portable video device is connected.
     */
    if (transaction->address == 0x46e8U)
        machine->video_setup_latch = (uint8_t) transaction->value;
    else
        machine->video_select_latch = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_ems_selector_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    size_t window;

    if ((transaction->size != 1U) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;

    window = (size_t) (transaction->address - 0x8400U);
    if (transaction->operation == BM_BUS_READ)
        transaction->value = machine->ems_page_selector[window];
    else
        machine->ems_page_selector[window] = (uint8_t) transaction->value;
    return BM_STATUS_OK;
}

static void
pcs86_pic_output(void *context, int asserted)
{
    bm_pcs86_machine_t *machine = context;
    if (machine->cpu_ready)
        (void) bm_engine_signal_cpu(machine->engine, machine->cpu_id, 0, asserted);
}

static bm_status_t
pcs86_interrupt_acknowledge(void *context, uint8_t *vector)
{
    bm_pcs86_machine_t *machine = context;
    return bm_pic8259_acknowledge(machine->pic, vector);
}

static void
pcs86_io_observer(void *context, const bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    if ((machine->io_trace != NULL) && (transaction->space == BM_ADDRESS_IO) &&
        (transaction->size == 1)) {
        bm_pcs86_io_trace_t trace = {
            transaction->operation,
            (uint16_t) transaction->address,
            (uint8_t) transaction->value
        };
        machine->io_trace(machine->io_trace_context, &trace);
    }
}

static void
pcs86_pit_output(void *context, unsigned int channel, int output)
{
    bm_pcs86_machine_t *machine = context;
    if (channel == 0)
        (void) bm_pic8259_set_irq(machine->pic, 0, output);
}

static bm_status_t
pcs86_board_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    uint16_t port = (uint16_t) transaction->address;
    uint8_t value = (uint8_t) transaction->value;
    if ((transaction->size != 1) || (transaction->operation == BM_BUS_FETCH))
        return BM_STATUS_UNSUPPORTED;
    if (transaction->operation == BM_BUS_READ) {
        switch (port) {
            case 0x0060:
                value = machine->keyboard_data_latch;
                (void) bm_pic8259_set_irq(machine->pic, 1, 0);
                if (machine->scan_queue_start != machine->scan_queue_end) {
                    value = machine->scan_queue[machine->scan_queue_start];
                    machine->keyboard_data_latch = value;
                    machine->scan_queue_start =
                        (uint8_t) ((machine->scan_queue_start + 1U) & 0x3fU);
                    if (machine->scan_queue_start != machine->scan_queue_end)
                        (void) bm_pic8259_set_irq(machine->pic, 1, 1);
                }
                break;
            case 0x0061:
                value = machine->port61;
                break;
            case 0x0062:
                value = machine->memory_blocks >= 10 ? 0xc0U : 0;
                break;
            case 0x0063:
                value = 0x08U;
                break;
            case 0x0064:
                value = machine->glue[4] & 0x8fU;
                if (machine->ems_size == (size_t) BM_PCS86_EMS_384_KIB * 1024U)
                    value |= 0x20U;
                else if (machine->ems_size ==
                         (size_t) BM_PCS86_EMS_1920_KIB * 1024U)
                    value |= 0x40U;
                break;
            case 0x0065:
                value = machine->control;
                break;
            case 0x0066:
            case 0x0069:
                value = machine->ps2[port - 0x0066U];
                break;
            case 0x0067:
            case 0x0068:
                value = pcs86_ps2_queue_pop(machine,
                                             (unsigned int) (port - 0x0067U),
                                             machine->ps2[port - 0x0066U]);
                break;
            case 0x006a:
                value = 0;
                if (machine->ps2_queue_start[0] != machine->ps2_queue_end[0])
                    value |= 0x20U;
                if (machine->ps2_queue_start[1] != machine->ps2_queue_end[1])
                    value |= 0x04U;
                break;
            case 0x006b:
            case 0x006c:
            case 0x006f:
                value = machine->glue[port & 0x0fU];
                break;
            default:
                value = 0xffU;
                break;
        }
        transaction->value = value;
        return BM_STATUS_OK;
    }
    switch (port) {
        case 0x0061:
            machine->port61 = value;
            return bm_pit8253_set_gate(machine->pit, 2, value & 1U);
        case 0x0064:
        case 0x006c:
        case 0x006f:
            machine->glue[port & 0x0fU] = value;
            break;
        case 0x0065:
            if (((machine->control ^ value) & 0x02U) && ((value & 0x02U) == 0))
                (void) bm_pic8259_set_irq(machine->pic, 7U, 0);
            if (((machine->control ^ value) & 0x10U) && ((value & 0x10U) == 0))
                (void) bm_pic8259_set_irq(machine->pic, 4U, 0);
            if (((machine->control ^ value) & 0x01U) != 0U)
                bm_xta_set_enabled(machine->xta, (value & 0x01U) != 0U);
            machine->control = value;
            break;
        case 0x0066:
            machine->ps2[0] = (value & 0xfbU) | (machine->ps2[0] & 0x04U);
            break;
        case 0x0067:
        case 0x0068:
            machine->ps2[port - 0x0066U] = value;
            return pcs86_ps2_command(machine,
                                     (unsigned int) (port - 0x0067U), value);
        case 0x0069:
        case 0x006a:
            machine->ps2[port - 0x0066U] = value;
            break;
        case 0x006b:
            machine->glue[11] = value & 0xfeU;
            if (((value & 1U) != 0) && (machine->memory_blocks < 10))
                ++machine->memory_blocks;
            break;
        default:
            break;
    }
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_jumpers_access(void *context, bm_bus_transaction_t *transaction)
{
    bm_pcs86_machine_t *machine = context;
    if ((transaction->size != 1) || (transaction->operation != BM_BUS_READ))
        return BM_STATUS_UNSUPPORTED;
    transaction->value = machine->jumpers;
    return BM_STATUS_OK;
}

static const bm_pcs86_firmware_identity_t expected_firmware[] = {
    {
        "even",
        "bios-even-109",
        BM_PCS86_FIRMWARE_HALF_SIZE,
        "c92a79509def8aee30d76700a5ce1c7b6716d2375a64075c9669ca376fde4eb8"
    },
    {
        "odd",
        "bios-odd-109",
        BM_PCS86_FIRMWARE_HALF_SIZE,
        "82f8363ea7cde1fb8abe50fd76c7381831b7b89539d3dc71cbe4305fb1568d40"
    }
};

static int
ascii_equal_case_insensitive(const char *left, const char *right)
{
    if ((left == NULL) || (right == NULL))
        return 0;
    while ((*left != '\0') && (*right != '\0')) {
        if (tolower((unsigned char) *left) != tolower((unsigned char) *right))
            return 0;
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static bm_status_t
validate_blob(const bm_blob_view_t *blob, const bm_pcs86_firmware_identity_t *identity)
{
    if ((blob->data == NULL) || (blob->size != identity->size))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((blob->sha256 != NULL) &&
        !ascii_equal_case_insensitive(blob->sha256, identity->sha256))
        return BM_STATUS_INVALID_ARGUMENT;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_validate(const bm_configuration_view_t *configuration)
{
    const bm_pcs86_config_t *config;
    bm_status_t status;
    size_t index;

    if ((configuration == NULL) || (configuration->type == NULL) ||
        (strcmp(configuration->type, BM_PCS86_CONFIG_TYPE) != 0) ||
        (configuration->version != BM_PCS86_CONFIG_VERSION) ||
        (configuration->size != sizeof(bm_pcs86_config_t)) ||
        (configuration->data == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    config = configuration->data;
    status = validate_blob(&config->firmware_even, &expected_firmware[0]);
    if (status == BM_STATUS_OK)
        status = validate_blob(&config->firmware_odd, &expected_firmware[1]);
    for (index = 0U; (status == BM_STATUS_OK) && (index < 2U); ++index)
        status = bm_floppy_drive_config_validate(&config->floppy[index]);
    if ((status == BM_STATUS_OK) &&
        (config->ems_kib != BM_PCS86_EMS_NONE_KIB) &&
        (config->ems_kib != BM_PCS86_EMS_384_KIB) &&
        (config->ems_kib != BM_PCS86_EMS_1920_KIB))
        status = BM_STATUS_INVALID_ARGUMENT;
    if ((status == BM_STATUS_OK) && config->hard_disk.present) {
        uint64_t required_blocks =
            (uint64_t) config->hard_disk.geometry.cylinders *
            config->hard_disk.geometry.heads *
            config->hard_disk.geometry.sectors_per_track;
        if ((config->hard_disk.geometry.cylinders == 0U) ||
            (config->hard_disk.geometry.heads == 0U) ||
            (config->hard_disk.geometry.sectors_per_track == 0U) ||
            (bm_block_media_validate(&config->hard_disk.media) != BM_STATUS_OK) ||
            (config->hard_disk.media.block_size != 512U) ||
            (config->hard_disk.media.block_count < required_blocks))
            status = BM_STATUS_INVALID_ARGUMENT;
    }
    return status;
}

static uint8_t
pcs86_floppy_jumper_code(const bm_floppy_drive_config_t *config)
{
    const bm_floppy_geometry_t *geometry = &config->geometry;

    if (!config->installed)
        return 3U;
    if ((geometry->cylinders == 40U) && (geometry->heads == 2U) &&
        (geometry->sectors_per_track == 9U))
        return 0U; /* 360 KiB. */
    if ((geometry->cylinders == 80U) && (geometry->heads == 2U) &&
        (geometry->sectors_per_track == 15U))
        return 2U; /* 1.2 MiB. */
    if ((geometry->cylinders == 80U) && (geometry->heads == 2U) &&
        (geometry->sectors_per_track == 9U))
        return 1U; /* 720 KiB. */
    return 3U; /* 1.44 MiB, or the board's open/no-drive encoding. */
}

static void
pcs86_destroy(void *context)
{
    bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return;
    bm_fdc765_destroy(machine->fdc);
    bm_floppy_drive_destroy(machine->floppy[1]);
    bm_floppy_drive_destroy(machine->floppy[0]);
    bm_dma_page_registers_destroy(machine->dma_pages);
    bm_xta_destroy(machine->xta);
    bm_dma8237_destroy(machine->dma);
    bm_uart16450_destroy(machine->uart);
    bm_lpt_spp_destroy(machine->lpt);
    bm_mm58167_destroy(machine->rtc);
    bm_pvga1a_destroy(machine->video);
    bm_pit8253_destroy(machine->pit);
    bm_pic8259_destroy(machine->pic);
    bm_linear_memory_destroy(machine->rom);
    if (machine->ems_ram != NULL)
        machine->host.release(machine->host.context, machine->ems_ram);
    if (machine->conventional_ram != NULL)
        machine->host.release(machine->host.context, machine->conventional_ram);
    bm_bus_destroy(machine->bus);
    machine->host.release(machine->host.context, machine);
}

static void
pcs86_clock_event(bm_engine_t *engine, void *context)
{
    bm_pcs86_machine_t *machine = context;
    bm_tick_t now = bm_engine_now(engine);
    bm_tick_t elapsed = now - machine->last_clock_time;
    uint64_t pit_ticks;
    uint64_t rtc_microseconds;

    machine->last_clock_time = now;
    machine->pit_clock_remainder += elapsed * PCS86_PIT_TICKS_PER_SECOND;
    pit_ticks = machine->pit_clock_remainder / PCS86_SCHEDULER_TICKS_PER_SECOND;
    machine->pit_clock_remainder %= PCS86_SCHEDULER_TICKS_PER_SECOND;
    if (pit_ticks != 0U)
        (void) bm_pit8253_advance(machine->pit, (uint32_t) pit_ticks);

    machine->rtc_clock_remainder += elapsed * UINT64_C(1000000);
    rtc_microseconds = machine->rtc_clock_remainder / PCS86_SCHEDULER_TICKS_PER_SECOND;
    machine->rtc_clock_remainder %= PCS86_SCHEDULER_TICKS_PER_SECOND;
    if (rtc_microseconds != 0U)
        (void) bm_mm58167_advance_microseconds(machine->rtc, rtc_microseconds);

    if (now <= UINT64_MAX - PCS86_CLOCK_QUANTUM)
        (void) bm_engine_schedule_at(engine, now + PCS86_CLOCK_QUANTUM,
                                     pcs86_clock_event, machine);
}

static bm_status_t
pcs86_video_geometry(const void *context, bm_video_geometry_t *geometry)
{
    const bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_pvga1a_video_geometry(machine->video, geometry);
}

static bm_status_t
pcs86_video_render(const void *context, bm_tick_t emulated_time,
                   bm_video_framebuffer_t *framebuffer)
{
    const bm_pcs86_machine_t *machine = context;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;
    return bm_pvga1a_render(machine->video, emulated_time,
                            PCS86_SCHEDULER_TICKS_PER_SECOND, framebuffer);
}

static size_t
pcs86_storage_count(const void *context)
{
    return context != NULL ? 2U : 0U;
}

static bm_status_t
pcs86_storage_status(const void *context, size_t index,
                     bm_storage_device_status_t *status)
{
    const bm_pcs86_machine_t *machine = context;
    bm_floppy_drive_state_t drive_state;
    bm_fdc765_state_t fdc_state;

    if ((machine == NULL) || (status == NULL) || (index >= 2U))
        return BM_STATUS_INVALID_ARGUMENT;
    memset(status, 0, sizeof(*status));
    status->kind = BM_STORAGE_DEVICE_FLOPPY;
    status->unit = (uint32_t) index;
    if (machine->floppy[index] == NULL)
        return BM_STATUS_OK;
    if (bm_floppy_drive_state(machine->floppy[index], &drive_state) !=
        BM_STATUS_OK)
        return BM_STATUS_DEVICE_ERROR;
    status->installed = drive_state.installed;
    status->media_present = drive_state.media_present;
    status->write_protected = drive_state.write_protected;
    status->read_operations = drive_state.read_operations;
    status->write_operations = drive_state.write_operations;
    if ((machine->fdc != NULL) &&
        (bm_fdc765_state(machine->fdc, &fdc_state) == BM_STATUS_OK))
        status->motor_active =
            (fdc_state.digital_output & (uint8_t) (0x10U << index)) != 0U;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_keyboard_leds(const void *context, bm_keyboard_led_state_t *state)
{
    const bm_pcs86_machine_t *machine = context;
    if ((machine == NULL) || (state == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    state->indicators = machine->keyboard_leds;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_storage_media(void *context, bm_storage_device_kind_t kind,
                    uint32_t unit, const bm_storage_media_change_t *change)
{
    bm_pcs86_machine_t *machine = context;
    bm_floppy_geometry_t geometry;

    if ((machine == NULL) || (change == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if ((kind != BM_STORAGE_DEVICE_FLOPPY) || (unit >= 2U) ||
        (machine->floppy[unit] == NULL) ||
        !bm_floppy_drive_installed(machine->floppy[unit]))
        return BM_STATUS_UNSUPPORTED;
    if (!change->media_present)
        return bm_floppy_drive_replace_media(machine->floppy[unit], NULL,
                                             NULL, 0);
    if ((change->media.block_size != 512U) ||
        ((change->media.block_count != 1440U) &&
         (change->media.block_count != 2880U)))
        return BM_STATUS_INVALID_ARGUMENT;
    geometry = (bm_floppy_geometry_t) {
        80U, 2U,
        (uint8_t) (change->media.block_count == 1440U ? 9U : 18U), 512U
    };
    return bm_floppy_drive_replace_media(machine->floppy[unit], &geometry,
                                         &change->media,
                                         change->write_protected);
}

static bm_status_t
pcs86_create(bm_engine_t *engine,
             const bm_host_services_t *host,
             const bm_configuration_view_t *configuration,
             void **out_machine)
{
    const bm_pcs86_config_t *config;
    bm_pcs86_machine_t *machine;
    uint8_t *combined_rom = NULL;
    bm_cpu_t cpu;
    bm_status_t status;
    size_t index;

    if ((engine == NULL) || (out_machine == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    *out_machine = NULL;
    status = pcs86_validate(configuration);
    if (status != BM_STATUS_OK)
        return status;
    config = configuration->data;
    machine = host->allocate(host->context, sizeof(*machine));
    if (machine == NULL)
        return BM_STATUS_OUT_OF_MEMORY;
    memset(machine, 0, sizeof(*machine));
    machine->host = *host;
    machine->engine = engine;
    machine->control = 0x80U;
    machine->ps2[0] = 0x04U; /* The front-panel key lock is open. */
    machine->jumpers = (uint8_t) (
        0xf0U | pcs86_floppy_jumper_code(&config->floppy[0]) |
        (uint8_t) (pcs86_floppy_jumper_code(&config->floppy[1]) << 2U));
    if (config->hard_disk.present)
        machine->jumpers &= (uint8_t) ~0x80U;
    machine->io_trace = config->io_trace;
    machine->io_trace_context = config->io_trace_context;
    machine->ems_size = (size_t) config->ems_kib * 1024U;
    machine->ems_pages = (uint16_t) (machine->ems_size / PCS86_EMS_WINDOW_SIZE);

    status = bm_bus_create(host, 32, &machine->bus);
    if (status == BM_STATUS_OK)
        bm_bus_set_observer(machine->bus, pcs86_io_observer, machine);
    if (status == BM_STATUS_OK) {
        machine->conventional_ram = host->allocate(host->context, BM_PCS86_MEMORY_SIZE);
        if (machine->conventional_ram == NULL)
            status = BM_STATUS_OUT_OF_MEMORY;
        else
            memset(machine->conventional_ram, 0, BM_PCS86_MEMORY_SIZE);
    }
    if ((status == BM_STATUS_OK) && (machine->ems_size != 0U)) {
        machine->ems_ram = host->allocate(host->context, machine->ems_size);
        if (machine->ems_ram == NULL)
            status = BM_STATUS_OUT_OF_MEMORY;
        else
            memset(machine->ems_ram, 0, machine->ems_size);
    }
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_MEMORY, 0U,
                            BM_PCS86_MEMORY_SIZE - 1U,
                            pcs86_memory_access, machine);
    if (status == BM_STATUS_OK) {
        combined_rom = host->allocate(host->context, BM_PCS86_ROM_SIZE);
        if (combined_rom == NULL)
            status = BM_STATUS_OUT_OF_MEMORY;
    }
    if (status == BM_STATUS_OK) {
        bm_linear_memory_config_t rom_config;
        for (index = 0; index < BM_PCS86_FIRMWARE_HALF_SIZE; ++index) {
            combined_rom[index * 2U] = config->firmware_even.data[index];
            combined_rom[index * 2U + 1U] = config->firmware_odd.data[index];
        }
        rom_config = (bm_linear_memory_config_t) {
            BM_ADDRESS_MEMORY, BM_PCS86_ROM_BASE, BM_PCS86_ROM_SIZE, 1,
            combined_rom, BM_PCS86_ROM_SIZE
        };
        status = bm_linear_memory_create(host, machine->bus, &rom_config, &machine->rom);
    }
    if (combined_rom != NULL)
        host->release(host->context, combined_rom);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_MEMORY,
                            0x000c0000U, 0x000effffU,
                            pcs86_unpopulated_option_rom_access, machine);
    if (status == BM_STATUS_OK) {
        bm_dma8237_config_t dma_config = { 0x0000U };
        status = bm_dma8237_create(host, machine->bus, &dma_config, &machine->dma);
    }
    if (status == BM_STATUS_OK) {
        bm_dma_page_registers_config_t page_config = {
            0x0080U, 0x0fU, machine->dma
        };
        status = bm_dma_page_registers_create(host, machine->bus, &page_config,
                                              &machine->dma_pages);
    }
    if (status == BM_STATUS_OK) {
        bm_xta_config_t xta_config = {
            .io_base = 0x0320U,
            .option_switches = 0xffU,
            .dma_channel = 3U,
            .dma = machine->dma,
            .irq = pcs86_xta_irq,
            .irq_context = machine,
            .drive_present = config->hard_disk.present,
            .geometry = config->hard_disk.geometry,
            .media = config->hard_disk.media
        };
        status = bm_xta_create(host, machine->bus, &xta_config, &machine->xta);
    }
    if (status == BM_STATUS_OK)
        /* The inherited PCS 86 used the legacy I/O fabric's default open-bus
         * behaviour for unclaimed ports.  Its firmware resets four
         * conventional XTA base slots at 321h, 325h, 329h and 32Dh, while the
         * onboard controller claims only 320h-323h.  Preserve that board/bus
         * contract explicitly: the three absent slots ignore writes and read
         * as open bus. */
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0324U, 0x032fU,
                            pcs86_open_bus_access, machine);
    if (status == BM_STATUS_OK) {
        bm_pic8259_config_t pic_config = {
            0x0020U, pcs86_pic_output, machine
        };
        status = bm_pic8259_create(host, machine->bus, &pic_config, &machine->pic);
    }
    for (index = 0U; (status == BM_STATUS_OK) && (index < 2U); ++index) {
        if (config->floppy[index].installed)
            status = bm_floppy_drive_create(host, &config->floppy[index],
                                            &machine->floppy[index]);
    }
    if (status == BM_STATUS_OK) {
        bm_fdc765_config_t fdc_config;
        memset(&fdc_config, 0, sizeof(fdc_config));
        fdc_config.io_base = 0x03f0U;
        fdc_config.dma_channel = 2U;
        fdc_config.disk_change_active_low = 1;
        fdc_config.dma = machine->dma;
        fdc_config.drives[0] = machine->floppy[0];
        fdc_config.drives[1] = machine->floppy[1];
        fdc_config.irq = pcs86_fdc_irq;
        fdc_config.irq_context = machine;
        status = bm_fdc765_create(host, machine->bus, &fdc_config,
                                  &machine->fdc);
    }
    if (status == BM_STATUS_OK) {
        bm_pit8253_config_t pit_config = { 0x0040U, pcs86_pit_output, machine };
        status = bm_pit8253_create(host, machine->bus, &pit_config, &machine->pit);
    }
    if (status == BM_STATUS_OK) {
        bm_mm58167_config_t rtc_config = {
            0x00b0U, 0x00e0U, NULL, NULL, NULL, 0U
        };
        status = bm_mm58167_create(host, machine->bus, &rtc_config, &machine->rtc);
    }
    if (status == BM_STATUS_OK) {
        bm_lpt_spp_config_t lpt_config = { NULL, pcs86_lpt_irq, machine };
        status = bm_lpt_spp_create(host, &lpt_config, &machine->lpt);
    }
    if (status == BM_STATUS_OK) {
        bm_uart16450_config_t uart_config = { NULL, pcs86_uart_irq, machine };
        status = bm_uart16450_create(host, &uart_config, &machine->uart);
    }
    if (status == BM_STATUS_OK) {
        bm_pvga1a_config_t video_config = { BM_PVGA1A_VRAM_SIZE };
        status = bm_pvga1a_create(host, machine->bus, &video_config, &machine->video);
    }
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0060U, 0x006fU,
                            pcs86_board_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0100U, 0x0100U,
                            pcs86_jumpers_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x00a0U, 0x00aeU,
                            pcs86_diagnostic_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0070U, 0x0070U,
                            pcs86_memory_control_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0378U, 0x037aU,
                            pcs86_lpt_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0278U, 0x027aU,
                            pcs86_open_bus_access, machine);
    if (status == BM_STATUS_OK)
        /* IBM DOS rearms the AT shared IRQ chain through 2F2h-2F7h while
         * initializing its standard character devices. The PCS 86 is not an
         * AT and has no such latch, so these addresses are an explicitly
         * absent device: writes have no effect and reads see the open bus. */
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x02f2U, 0x02f7U,
                            pcs86_open_bus_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x03f8U, 0x03ffU,
                            pcs86_uart_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x02f8U, 0x02ffU,
                            pcs86_open_bus_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x8400U, 0x8403U,
                            pcs86_ems_selector_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x0102U, 0x0102U,
                            pcs86_video_arbitration_access, machine);
    if (status == BM_STATUS_OK)
        status = bm_bus_map(machine->bus, BM_ADDRESS_IO, 0x46e8U, 0x46e8U,
                            pcs86_video_arbitration_access, machine);
    if (status == BM_STATUS_OK) {
        bm_808x_config_t cpu_config = {
            .model = BM_808X_NEC_V30,
            .frequency_hz = 10000000U,
            .bus = machine->bus,
            .trace = config->trace,
            .trace_context = config->trace_context,
            .interrupt_ack = pcs86_interrupt_acknowledge,
            .interrupt_context = machine
        };
        status = bm_808x_create(host, &cpu_config, &cpu);
    }
    if (status == BM_STATUS_OK) {
        status = bm_engine_add_cpu(engine, &cpu, &machine->cpu_id);
        if (status != BM_STATUS_OK)
            cpu.ops.destroy(cpu.context);
        else {
            machine->cpu_ready = 1;
            if (bm_pic8259_pending(machine->pic))
                status = bm_engine_signal_cpu(engine, machine->cpu_id, 0, 1);
        }
    }
    if (status != BM_STATUS_OK) {
        pcs86_destroy(machine);
        return status;
    }
    *out_machine = machine;
    return BM_STATUS_OK;
}

static bm_status_t
pcs86_reset(void *context)
{
    bm_pcs86_machine_t *machine = context;
    bm_tick_t now;
    if (machine == NULL)
        return BM_STATUS_INVALID_ARGUMENT;

    bm_dma8237_reset(machine->dma);
    bm_dma_page_registers_reset(machine->dma_pages);
    bm_pic8259_reset(machine->pic);
    bm_fdc765_reset(machine->fdc);
    bm_pit8253_reset(machine->pit);
    bm_mm58167_reset(machine->rtc);
    bm_lpt_spp_reset(machine->lpt);
    bm_uart16450_reset(machine->uart);
    bm_xta_reset(machine->xta);
    bm_pvga1a_reset(machine->video);
    machine->port61 = 0U;
    machine->control = 0x80U;
    bm_xta_set_enabled(machine->xta, 0);
    machine->memory_blocks = 0U;
    memset(machine->glue, 0, sizeof(machine->glue));
    memset(machine->ps2, 0, sizeof(machine->ps2));
    memset(machine->ps2_queue, 0, sizeof(machine->ps2_queue));
    memset(machine->ps2_queue_start, 0, sizeof(machine->ps2_queue_start));
    memset(machine->ps2_queue_end, 0, sizeof(machine->ps2_queue_end));
    memset(machine->ps2_pending_command, 0,
           sizeof(machine->ps2_pending_command));
    machine->keyboard_leds = 0U;
    memset(machine->scan_queue, 0, sizeof(machine->scan_queue));
    machine->scan_queue_start = 0U;
    machine->scan_queue_end = 0U;
    machine->keyboard_data_latch = 0U;
    machine->ps2[0] = 0x04U;
    machine->nmi_mask = 0U;
    machine->diagnostic_port = 0U;
    machine->memory_control_latch = 0U;
    machine->video_setup_latch = 0U;
    machine->video_select_latch = 0U;
    memset(machine->ems_page_selector, 0, sizeof(machine->ems_page_selector));
    machine->pit_clock_remainder = 0U;
    machine->rtc_clock_remainder = 0U;
    now = bm_engine_now(machine->engine);
    machine->last_clock_time = now;
    return bm_engine_schedule_at(machine->engine, now + PCS86_CLOCK_QUANTUM,
                                 pcs86_clock_event, machine);
}

static bm_status_t
pcs86_inspect(const void *context, const char *name, uint64_t *value)
{
    const bm_pcs86_machine_t *machine = context;
    uint16_t count;
    int output;
    if ((machine == NULL) || (name == NULL) || (value == NULL))
        return BM_STATUS_INVALID_ARGUMENT;
    if (strcmp(name, "pit0_count") == 0) {
        if (bm_pit8253_count(machine->pit, 0U, &count) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = count;
    } else if (strcmp(name, "pit0_output") == 0) {
        if (bm_pit8253_output(machine->pit, 0U, &output) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = (uint64_t) output;
    } else if (strcmp(name, "pic_pending") == 0) {
        *value = (uint64_t) bm_pic8259_pending(machine->pic);
    } else if ((strcmp(name, "pic_mask") == 0) ||
               (strcmp(name, "pic_requests") == 0) ||
               (strcmp(name, "pic_in_service") == 0) ||
               (strcmp(name, "pic_lines") == 0)) {
        bm_pic8259_state_t state;
        if (bm_pic8259_state(machine->pic, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        if (strcmp(name, "pic_mask") == 0)
            *value = state.interrupt_mask;
        else if (strcmp(name, "pic_requests") == 0)
            *value = state.interrupt_requests;
        else if (strcmp(name, "pic_in_service") == 0)
            *value = state.in_service;
        else
            *value = state.input_lines;
    } else if ((strcmp(name, "fdc_dor") == 0) ||
               (strcmp(name, "fdc_msr") == 0) ||
               (strcmp(name, "fdc_irq") == 0)) {
        bm_fdc765_state_t state;
        if (bm_fdc765_state(machine->fdc, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        if (strcmp(name, "fdc_dor") == 0)
            *value = state.digital_output;
        else if (strcmp(name, "fdc_msr") == 0)
            *value = state.main_status;
        else
            *value = (uint64_t) state.interrupt_asserted;
    } else if (strcmp(name, "floppy0_cylinder") == 0) {
        if (machine->floppy[0] == NULL)
            return BM_STATUS_INVALID_STATE;
        *value = bm_floppy_drive_cylinder(machine->floppy[0]);
    } else if (strcmp(name, "keyboard_queue_depth") == 0) {
        *value = (uint8_t) ((machine->scan_queue_end -
                            machine->scan_queue_start) & 0x3fU);
    } else if (strcmp(name, "ems_kib") == 0) {
        *value = machine->ems_size / 1024U;
    } else if (strcmp(name, "ems_pages") == 0) {
        *value = machine->ems_pages;
    } else if ((strlen(name) == strlen("ems_selector0")) &&
               (strncmp(name, "ems_selector", strlen("ems_selector")) == 0) &&
               (name[strlen("ems_selector")] >= '0') &&
               (name[strlen("ems_selector")] <= '3')) {
        *value = machine->ems_page_selector[name[strlen("ems_selector")] - '0'];
    } else if ((strcmp(name, "lpt_data") == 0) ||
               (strcmp(name, "lpt_status") == 0) ||
               (strcmp(name, "lpt_control") == 0)) {
        bm_lpt_spp_state_t state;
        if (bm_lpt_spp_state(machine->lpt, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        if (strcmp(name, "lpt_data") == 0)
            *value = state.data;
        else if (strcmp(name, "lpt_status") == 0)
            *value = state.status;
        else
            *value = state.control;
    } else if (strcmp(name, "uart_line_status") == 0) {
        bm_uart16450_state_t state;
        if (bm_uart16450_state(machine->uart, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = state.line_status;
    } else if ((strcmp(name, "xta_status") == 0) ||
               (strcmp(name, "xta_sense") == 0) ||
               (strcmp(name, "xta_cylinder") == 0) ||
               (strcmp(name, "xta_enabled") == 0) ||
               (strcmp(name, "xta_irq") == 0)) {
        bm_xta_state_t state;
        if (bm_xta_state(machine->xta, &state) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        if (strcmp(name, "xta_status") == 0)
            *value = state.status;
        else if (strcmp(name, "xta_sense") == 0)
            *value = state.sense;
        else if (strcmp(name, "xta_cylinder") == 0)
            *value = state.cylinder;
        else if (strcmp(name, "xta_enabled") == 0)
            *value = (uint64_t) state.enabled;
        else
            *value = (uint64_t) state.interrupt_asserted;
    } else if ((strcmp(name, "video_crtc_cursor_start") == 0) ||
               (strcmp(name, "video_crtc_cursor_end") == 0) ||
               (strcmp(name, "video_crtc_cursor_high") == 0) ||
               (strcmp(name, "video_crtc_cursor_low") == 0) ||
               (strcmp(name, "video_crtc_start_high") == 0) ||
               (strcmp(name, "video_crtc_start_low") == 0) ||
               (strcmp(name, "video_crtc_max_scan_line") == 0) ||
               (strcmp(name, "video_crtc_offset") == 0) ||
               (strcmp(name, "video_pvga_pr3") == 0)) {
        uint8_t index;
        uint8_t register_value;
        bm_pvga1a_register_set_t register_set = BM_PVGA1A_CRTC;
        if (strcmp(name, "video_crtc_cursor_start") == 0)
            index = 0x0aU;
        else if (strcmp(name, "video_crtc_cursor_end") == 0)
            index = 0x0bU;
        else if (strcmp(name, "video_crtc_cursor_high") == 0)
            index = 0x0eU;
        else if (strcmp(name, "video_crtc_cursor_low") == 0)
            index = 0x0fU;
        else if (strcmp(name, "video_crtc_start_high") == 0)
            index = 0x0cU;
        else if (strcmp(name, "video_crtc_start_low") == 0)
            index = 0x0dU;
        else if (strcmp(name, "video_crtc_max_scan_line") == 0)
            index = 9U;
        else if (strcmp(name, "video_crtc_offset") == 0)
            index = 0x13U;
        else {
            register_set = BM_PVGA1A_GRAPHICS;
            index = 0x0dU;
        }
        if (bm_pvga1a_inspect_register(machine->video, register_set,
                                        index, &register_value) != BM_STATUS_OK)
            return BM_STATUS_DEVICE_ERROR;
        *value = register_value;
    } else {
        return BM_STATUS_INVALID_ARGUMENT;
    }
    return BM_STATUS_OK;
}

const bm_pcs86_firmware_identity_t *
bm_pcs86_expected_firmware(size_t *count)
{
    if (count != NULL)
        *count = sizeof(expected_firmware) / sizeof(expected_firmware[0]);
    return expected_firmware;
}

static const bm_machine_definition_t pcs86_definition = {
    .id = "olivetti-pcs86",
    .scheduler_ticks_per_second = PCS86_SCHEDULER_TICKS_PER_SECOND,
    .configuration = {
        BM_PCS86_CONFIG_TYPE,
        BM_PCS86_CONFIG_VERSION,
        sizeof(bm_pcs86_config_t)
    },
    .ops = {
        pcs86_validate,
        pcs86_create,
        pcs86_destroy,
        pcs86_video_geometry,
        pcs86_video_render,
        pcs86_reset,
        pcs86_inspect,
        pcs86_input,
        pcs86_storage_count,
        pcs86_storage_status,
        pcs86_storage_media,
        pcs86_keyboard_leds
    },
    .engine = { 1U, 10U }
};

const bm_machine_definition_t *
bm_pcs86_machine_definition(void)
{
    return &pcs86_definition;
}

bm_machine_config_t
bm_pcs86_machine_config(const bm_pcs86_config_t *configuration)
{
    bm_machine_config_t result = {
        .definition = &pcs86_definition,
        .configuration = {
            BM_PCS86_CONFIG_TYPE,
            BM_PCS86_CONFIG_VERSION,
            sizeof(*configuration),
            configuration
        }
    };
    return result;
}
