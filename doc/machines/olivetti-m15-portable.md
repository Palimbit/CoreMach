# Olivetti M15: portable-engine bring-up

This is an internal system target, not a selectable or bootable machine. It is
separate from the legacy `olivetti_m15` pilot and does not read legacy VM
configuration. The public M15 catalogue sheet still describes that pilot; it
must not imply that the new engine has reached parity.

## Implemented and tested

- A typed, session-owned M15 configuration with 256/512 KiB RAM, a caller-owned
  64 KiB firmware blob and two internal 720 KiB floppy-drive definitions.
- Intel 8088 interpreted execution at a nominal 4.77 MHz, ROM at F0000h,
  16 KiB writable video RAM at B8000h, PIC, PIT, DMA, and FDC.
- M15-specific startup display and memory switches at ports 60h/62h, Port B
  control, and the BIOS keyboard-probe command 05h/response 82h.
- Synthetic reset-vector and I/O probes for both RAM sizes, reset, and
  allocation-failure cleanup. These tests contain no proprietary firmware.

The Intel 8088 implementation is an approximation for the documented 80C88;
its CMOS-specific behavior and timings have not been verified. The PIT ratio
of CPU/3 is inferred from earlier BIOS diagnostics, not a measured board clock.

## Required before a usable M15

1. Port and test the Yamaha V6355D register and framebuffer behavior, keeping
   the four-level green LCD presentation separate from controller state.
2. Port and test the OKI MSM6242 RTC without using host-global state.
3. Complete live keyboard input, status and IRQ behavior; the current model
   only supports the BIOS identification probe.
4. Compare POST, interrupt and port traces with the validated legacy pilot
   using locally approved firmware. Do not package firmware or media.
5. Validate floppy boot and reset in the portable engine, then expose M15 via
   the runtime registry and optional frontends. Only then consider the
   catalogue entry available for the new engine.

The canonical custody and historical evidence remain under
`Z:\library\olivetti\m15`; the matching workspace library directory is a
working replica, not an additional source of truth.
