/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 8042 (AT keyboard controller) emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2023-2025 Miran Grca.
 *          Copyright 2023-2025 EngiNerd.
 *
 * BluMach modifications: rtzor, Project BluMach, 2026.
 */
/* Copyright 2026 BluMach contributors.
 * Derived from the classic controller's command/buffer paths and translation
 * table. IBM 5170 Technical Reference 6280070, pp.1-42..1-55 governs corrections.
 * Instance-owned byte boundary; excludes MCU/serial firmware behavior.
 * Explicit PCS286 profile reuses classic Olivetti 80/84 latch and CFh policies.
 */
#include "kbc8042_private.h"
#include <string.h>

enum { OBF=1, IBF=2, SYSTEM=4, COMMAND=8, UNLOCKED=16,
       DISABLED=16, PC_MODE=32, TRANSLATE=64 };


static const uint8_t nont_to_t[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static bm_status_t fail(bm_kbc8042_t *k, bm_status_t result)
{
    if (result == BM_STATUS_IDLE) result = BM_STATUS_INVALID_STATE;
    if (result != BM_STATUS_OK) k->s.failure = result;
    return result;
}
static bm_status_t line(bm_kbc8042_t *k, bm_kbc8042_line_fn fn,
                         void *context, int *old, int level, int force)
{
    if (!force && *old == level) return BM_STATUS_OK;
    *old = level;
    return fail(k, fn(context, level));
}
static bm_status_t publish(bm_kbc8042_t *k, int force)
{
    bm_status_t r;
    int inhibit = (k->s.command_byte & DISABLED) || !(k->s.output_port & 0x40U) ||
        (k->s.status & OBF) || k->s.output_remaining;
    r = line(k, k->config.a20, k->config.output_context, &k->s.a20,
             (k->s.output_port & 2U) != 0, force);
    if (r == BM_STATUS_OK)
        r = line(k, k->config.cpu_reset, k->config.output_context, &k->s.cpu_reset,
                 !(k->s.output_port & 1U), force);
    if (r == BM_STATUS_OK)
        r = line(k, k->config.keyboard_inhibit, k->config.keyboard_context,
                 &k->s.inhibited, inhibit != 0, force);
    if (r == BM_STATUS_OK)
        r = line(k, k->config.irq, k->config.output_context, &k->s.irq,
                 (k->s.command_byte & 1U) && (k->s.status & OBF), force);
    return r;
}
static void initial(bm_kbc8042_t *k)
{
    uint64_t cycles = k->s.cycles;
    memset(&k->s, 0, sizeof(k->s)); k->s.cycles = cycles;
    k->s.command_byte = DISABLED;
    k->s.auxiliary_enabled = 0;
    k->s.output_port = k->config.initial_output_port & 0xcfU;
    k->s.olivetti_p2 = k->config.initial_output_port;
    k->s.a20 = (k->s.output_port & 2U) != 0;
    k->s.inhibited = 1;
    k->input = k->pending_output = k->pulse_restore = 0;
    k->input_command = 0;
}
bm_status_t bm_kbc8042_create(const bm_host_services_t *host,
                              const bm_kbc8042_config_t *c, bm_kbc8042_t **out)
{
    bm_kbc8042_t *k;
    if (out) *out = NULL;
    if (!out || !c || bm_host_services_validate(host) != BM_STATUS_OK ||
        c->data_port == c->command_port || !c->irq || !c->a20 || !c->cpu_reset ||
        !c->keyboard_inhibit || !c->clock.cycles_per_second_numerator ||
        !c->clock.cycles_per_second_denominator || !c->input_cycles ||
        !c->output_cycles || !c->self_test_cycles || !c->pulse_cycles ||
        c->self_test_cycles > UINT64_MAX - c->input_cycles ||
        !(c->input_port & 0x80U) || (c->initial_output_port & 0xc1U) != 0xc1U ||
        (c->command_profile != BM_KBC8042_COMMANDS_AT &&
         c->command_profile != BM_KBC8042_COMMANDS_OLIVETTI_PCS286))
        return BM_STATUS_INVALID_ARGUMENT;
    k = host->allocate(host->context, sizeof(*k));
    if (!k) return BM_STATUS_OUT_OF_MEMORY;
    memset(k, 0, sizeof(*k)); k->host = *host; k->config = *c;
    initial(k); *out = k; return BM_STATUS_OK;
}
void bm_kbc8042_destroy(bm_kbc8042_t *k)
{
    if (k && !k->busy && !k->clock_link) k->host.release(k->host.context, k);
}
bm_status_t bm_kbc8042_reset(bm_kbc8042_t *k)
{
    bm_status_t r;
    if (!k) return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy) return BM_STATUS_INVALID_STATE;
    k->busy = 1; initial(k); r = publish(k, 1); k->busy = 0;
    return r;
}
static void reply(bm_kbc8042_t *k, uint8_t byte)
{
    k->pending_output = byte;
    k->s.output_remaining = k->config.output_cycles;
}
static uint8_t output_port(const bm_kbc8042_t *k)
{
    /* Data line idle high; logical inhibition drives the clock output low.
     * No intermediate serial waveform is represented at the byte boundary. */
    return (uint8_t)((k->s.output_port & (k->s.inhibited ? 0xbfU : 0xffU)) |
                     ((k->s.status & OBF) ? 0x10U : 0U) |
                     ((k->s.status & IBF) ? 0U : 0x20U));
}
static int response_command(const bm_kbc8042_t *k, uint8_t v)
{
    return (v >= 0x20 && v <= 0x3f) || v == 0xa4 || v == 0xaa || v == 0xc0 || v == 0xd0 ||
        (v == 0x80 && k->config.command_profile == BM_KBC8042_COMMANDS_OLIVETTI_PCS286);
}
static int supported_command(const bm_kbc8042_t *k, uint8_t v)
{
    return response_command(k, v) || (v >= 0x60 && v <= 0x7f) ||
        v == 0xad || v == 0xae || v == 0xd1 || v >= 0xf0 ||
        ((v == 0x84 || v == 0xa7 || v == 0xa8 || v == 0xcf || v == 0xd4) &&
         k->config.command_profile == BM_KBC8042_COMMANDS_OLIVETTI_PCS286);
}
static bm_status_t consume_input(bm_kbc8042_t *k)
{
    uint8_t v = k->input;
    if (!k->input_command && k->s.parameter == 0xd4 &&
        k->config.auxiliary_command && k->s.auxiliary_enabled) {
        bm_status_t r = k->config.auxiliary_command(k->config.auxiliary_context, v);
        if (r == BM_STATUS_IDLE) {
            k->s.input_remaining = k->config.input_cycles;
            return BM_STATUS_OK;
        }
        if (r != BM_STATUS_OK) return fail(k, r);
    }
    if (!k->input_command && !k->s.parameter) {
        bm_status_t r;
        /* Classic kbc_ibf_process enables the interface when consuming a
         * keyboard-bound byte. AD inhibits keyboard traffic, not the host's
         * input buffer. Parameters for 60/D1/84 do not take this path.
         * Release the byte link before delivery; retain any callback failure
         * and accepted enable effects without retrying the command. */
        k->s.command_byte &= (uint8_t)~DISABLED;
        k->s.output_port |= 0x40U;
        r = publish(k, 0);
        if (r != BM_STATUS_OK) return r;
        r = k->config.keyboard_command(k->config.keyboard_context, v);
        if (r == BM_STATUS_IDLE) {
            k->s.input_remaining = k->config.input_cycles;
            return BM_STATUS_OK; /* Endpoint accepted nothing; explicit backpressure. */
        }
        if (r != BM_STATUS_OK) return fail(k, r);
    }
    k->s.status &= (uint8_t)~IBF;
    if (k->input_command) {
        k->s.parameter = 0; /* Accepted command replaces an unfinished parameter. */
        if (v >= 0x20 && v <= 0x3f) {
            reply(k, v == 0x20 ? k->s.command_byte : k->s.internal_ram[v - 0x21]);
        } else if (v >= 0x60 && v <= 0x7f) {
            k->s.parameter = v;
        } else switch (v) {
            case 0x84: case 0xd1: case 0xd4: k->s.parameter = v; break;
            case 0x80: reply(k, k->s.olivetti_p2); break;
            case 0xa4:
                /* IBM AT command: report that no controller password is
                 * installed. Password loading/enforcement is not modeled. */
                reply(k, 0xf1); break;
            case 0xaa:
                k->s.command_byte |= SYSTEM | DISABLED;
                k->s.status |= SYSTEM; k->s.break_pending = 0;
                k->s.auxiliary_enabled = 0;
                reply(k, 0x55); break;
            case 0xa7: k->s.auxiliary_enabled = 0; break;
            case 0xa8: k->s.auxiliary_enabled = 1; break;
            case 0xad: k->s.command_byte |= DISABLED; break;
            case 0xae:
                k->s.command_byte &= (uint8_t)~DISABLED;
                k->s.output_port |= 0x40U; /* Enable releases the clock line. */
                break;
            case 0xc0: reply(k, k->config.input_port); break;
            case 0xd0: reply(k, output_port(k)); break;
            case 0xcf:
                /* Classic write_cmd_olivetti accepts CFh without a reply or
                 * output effect. Profile-gated at admission; no firmware-PC
                 * check, POST result, buffer flush or fabricated ACK. */
                break;
            default:
                k->pulse_restore = k->s.output_port & (uint8_t)~v & 15U;
                if (k->pulse_restore) {
                    k->s.output_port &= (uint8_t)~k->pulse_restore;
                    k->s.pulse_remaining = k->config.pulse_cycles;
                }
                break;
        }
    } else if (k->s.parameter >= 0x60 && k->s.parameter <= 0x7f) {
        if (k->s.parameter == 0x60) {
            if ((k->s.command_byte ^ v) & (TRANSLATE | PC_MODE)) k->s.break_pending = 0;
            k->s.command_byte = v;
            if (!(v & DISABLED)) k->s.output_port |= 0x40U;
            k->s.status = (uint8_t)((k->s.status & ~SYSTEM) | (v & SYSTEM));
        } else {
            k->s.internal_ram[k->s.parameter - 0x61] = v;
        }
        k->s.parameter = 0;
    } else if (k->s.parameter == 0xd1) {
        k->s.output_port = v & 0xcfU;
        k->s.olivetti_p2 = v;
        k->s.parameter = 0;
    } else if (k->s.parameter == 0x84) {
        /* Classic write_cmd_data_olivetti stores P2 without write_p2's
         * A20/reset callbacks. Keep raw vendor readback separate from driven
         * outputs so later buffer publications cannot apply it accidentally.
         * D0/pulses retain the documented AT output semantics. */
        k->s.olivetti_p2 = v;
        k->s.parameter = 0;
    } else if (k->s.parameter == 0xd4) {
        k->s.parameter = 0;
    }
    return BM_STATUS_OK;
}
bm_status_t bm_kbc8042_io(void *context, bm_bus_transaction_t *t)
{
    bm_kbc8042_t *k = context;
    bm_status_t r = BM_STATUS_OK;
    int command, debug;
    uint8_t v;
    if (!k || !t) return BM_STATUS_INVALID_ARGUMENT;
    if (t->space != BM_ADDRESS_IO) return BM_STATUS_UNMAPPED;
    if (t->operation < BM_BUS_READ || t->operation > BM_BUS_FETCH ||
        t->address > UINT16_MAX || t->alignment > 1U ||
        (t->attributes & ~(uint32_t)(BM_BUS_TRANSACTION_DEBUG | BM_BUS_TRANSACTION_LOCKED)) ||
        (t->endianness != BM_ENDIAN_LITTLE && t->endianness != BM_ENDIAN_BIG))
        return BM_STATUS_INVALID_ARGUMENT;
    if (t->size != 1 || t->operation == BM_BUS_FETCH) return BM_STATUS_UNSUPPORTED;
    if (t->address != k->config.data_port && t->address != k->config.command_port)
        return BM_STATUS_UNMAPPED;
    command = t->address == k->config.command_port;
    debug = (t->attributes & BM_BUS_TRANSACTION_DEBUG) != 0;
    if (debug) {
        if (t->operation != BM_BUS_READ) return BM_STATUS_UNSUPPORTED;
        t->value = command ? k->s.status : k->s.output_byte;
        return BM_STATUS_OK;
    }
    if (k->busy) return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK) return k->s.failure;
    v = (uint8_t)t->value;
    if (t->operation == BM_BUS_READ) {
        if (command) { t->value = k->s.status; return BM_STATUS_OK; }
        /* Classic AT port reads return the retained output latch even when
         * OBF is clear. Reading an empty register is not a pending host bus
         * transaction; do not manufacture a byte or consume a pending reply. */
        k->busy = 1; v = k->s.output_byte; k->s.status &= (uint8_t)~OBF;
        r = publish(k, 0);
        if (r == BM_STATUS_OK) t->value = v;
        k->busy = 0; return r;
    }
    if (k->s.status & IBF) return BM_STATUS_CAPACITY_EXCEEDED;
    if (command) {
        if (!supported_command(k, v)) return BM_STATUS_UNSUPPORTED;
        if (response_command(k, v) && ((k->s.status & OBF) || k->s.output_remaining))
            return BM_STATUS_CAPACITY_EXCEEDED;
        /* Pulse overlap has no qualified behavior; never extend/erase an edge. */
        if (k->s.pulse_remaining && (v >= 0xf0 || v == 0xd1)) return BM_STATUS_UNSUPPORTED;
    } else if (k->s.parameter == 0x60) {
        /* The command byte is RAM cell 20h. IBM says software should write
         * reserved bits 7 and 1 as zero, but does not define the host write as
         * rejected. Retain all eight bits for readback; only documented bits
         * drive modeled behavior. The PCS286 BIOS legitimately retains bit 1. */
    } else if (k->s.parameter >= 0x61 && k->s.parameter <= 0x7f) {
        /* Remaining internal RAM bytes accept all values without line effects. */
    } else if (k->s.parameter == 0xd1) {
        if (!(v & 0x80U) || k->s.pulse_remaining) return BM_STATUS_UNSUPPORTED;
    } else if (k->s.parameter == 0x84) {
        /* Raw vendor latch accepts all bytes, independent of keyboard state. */
    } else if (k->s.parameter == 0xd4) {
        /* The second-channel write is a controller parameter. Absence of an
         * attached endpoint is a real no-response condition, not an error. */
    } else {
        if (!k->config.keyboard_command) return BM_STATUS_UNSUPPORTED;
        /* The host input and controller output buffers are independent. A
         * full OBF may retain the keyboard's first ACK while the host queues
         * the following option byte in IBF. A response still moving toward
         * OBF occupies the modeled serial boundary and refuses overlap. */
        if (k->s.output_remaining) return BM_STATUS_CAPACITY_EXCEEDED;
    }
    k->input = v; k->input_command = command;
    k->s.input_remaining = k->config.input_cycles +
        (command && v == 0xaa ? k->config.self_test_cycles : 0U);
    k->s.status = (uint8_t)((k->s.status & ~COMMAND) | IBF | (command ? COMMAND : 0U));
    return BM_STATUS_OK;
}
bm_status_t bm_kbc8042_receive_keyboard(bm_kbc8042_t *k, uint8_t byte)
{
    bm_status_t r;
    int translate;
    if (!k) return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy) return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK) return k->s.failure;
    if ((k->s.command_byte & DISABLED) || !(k->s.output_port & 0x40U)) return BM_STATUS_IDLE;
    if ((k->s.status & (IBF | OBF)) || k->s.output_remaining)
        return BM_STATUS_CAPACITY_EXCEEDED;
    k->busy = 1;
    /* The classic Olivetti controller path keeps XLAT active when PCMODE and
     * XLAT are set together. The PCS286 BIOS writes CCB 64h/65h while its
     * keyboard still emits set 2 and later waits for set-1 F1 (3Bh). Keep the
     * IBM AT profile's PCMODE bypass unchanged. This is an explicit functional
     * profile policy, not a claim about the missing Mitsubishi mask ROM. */
    translate = (k->s.command_byte & TRANSLATE) &&
        (!(k->s.command_byte & PC_MODE) ||
         k->config.command_profile == BM_KBC8042_COMMANDS_OLIVETTI_PCS286);
    if (translate) {
        if (byte == 0xf0) { k->s.break_pending = 1; k->busy = 0; return BM_STATUS_OK; }
        byte = nont_to_t[byte];
        /* Inherited break/high-bit suppression, separately qualified in docs. */
        if (k->s.break_pending && (byte & 0x80U)) {
            k->s.break_pending = 0; k->busy = 0; return BM_STATUS_OK;
        }
        if (k->s.break_pending) byte |= 0x80U;
    }
    k->s.break_pending = 0; reply(k, byte);
    r = publish(k, 0); k->busy = 0; return r;
}
static uint64_t deadline(const bm_kbc8042_t *k)
{
    uint64_t n = k->s.input_remaining, v = k->s.output_remaining;
    if (v && (!n || v < n)) n = v;
    v = k->s.pulse_remaining;
    if (v && (!n || v < n)) n = v;
    return n;
}
void bm_kbc8042_elapse(bm_kbc8042_t *k, uint64_t step, bm_kbc_edge_t *edge)
{
    edge->input = k->s.input_remaining && k->s.input_remaining == step;
    edge->output = k->s.output_remaining && k->s.output_remaining == step;
    edge->pulse = k->s.pulse_remaining && k->s.pulse_remaining == step;
    if (k->s.input_remaining) k->s.input_remaining -= step;
    if (k->s.output_remaining) k->s.output_remaining -= step;
    if (k->s.pulse_remaining) k->s.pulse_remaining -= step;
    k->s.cycles += step;
}
bm_status_t bm_kbc8042_settle(bm_kbc8042_t *k, const bm_kbc_edge_t *edge)
{
    bm_status_t r = BM_STATUS_OK;
    /* Stable tie order: pulse release, input consumption, output publication. */
    if (edge->pulse) {
        k->s.output_port |= k->pulse_restore; k->pulse_restore = 0;
    }
    if (edge->input) r = consume_input(k);
    if (r == BM_STATUS_OK && edge->output) {
        k->s.output_byte = k->pending_output; k->s.status |= OBF | UNLOCKED;
    }
    if (r == BM_STATUS_OK && (edge->pulse || edge->input || edge->output)) r = publish(k, 0);
    return r;
}
bm_status_t bm_kbc8042_advance(bm_kbc8042_t *k, uint64_t cycles)
{
    bm_status_t r = BM_STATUS_OK;
    if (!k) return BM_STATUS_INVALID_ARGUMENT;
    if (k->busy) return BM_STATUS_INVALID_STATE;
    if (k->s.failure != BM_STATUS_OK) return k->s.failure;
    if (cycles > UINT64_MAX - k->s.cycles) return BM_STATUS_CAPACITY_EXCEEDED;
    k->busy = 1;
    while (cycles && r == BM_STATUS_OK) {
        uint64_t n = deadline(k), step = n && n < cycles ? n : cycles;
        bm_kbc_edge_t edge;
        bm_kbc8042_elapse(k, step, &edge); cycles -= step;
        r = bm_kbc8042_settle(k, &edge);
    }
    k->busy = 0; return r;
}
bm_status_t bm_kbc8042_next_deadline(const bm_kbc8042_t *k, uint64_t *cycles)
{
    if (!k || !cycles) return BM_STATUS_INVALID_ARGUMENT;
    if (k->s.failure != BM_STATUS_OK) return k->s.failure;
    *cycles = deadline(k); return *cycles ? BM_STATUS_OK : BM_STATUS_IDLE;
}
bm_status_t bm_kbc8042_state(const bm_kbc8042_t *k, bm_kbc8042_state_t *s)
{
    if (!k || !s) return BM_STATUS_INVALID_ARGUMENT;
    *s = k->s; return BM_STATUS_OK;
}
bm_status_t bm_kbc8042_inspect(const bm_kbc8042_t *k, uint8_t *status, uint8_t *ccb)
{
    if (!k || !status || !ccb) return BM_STATUS_INVALID_ARGUMENT;
    *status = k->s.status; *ccb = k->s.command_byte; return BM_STATUS_OK;
}
