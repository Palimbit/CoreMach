# Olivetti PCS 386SX

Implementation status as of 2026-08-26: partial, reaches the Phoenix 1.14
built-in Setup with a newly created CMOS image.

## Firmware and machine definition

The machine ID is `olivetti_pcs386sx`. Its two 64 KiB EPROM images are loaded
byte-interleaved at `E0000-FFFFF`. The first 24 KiB of the resulting image are
the embedded Olivetti OVC 1.06 video firmware; the onboard PVGA1A device must
therefore not map an unrelated option ROM at `C0000`.

The current model consists of:

- Intel 80386SX at 16 MHz;
- Headland HT18-B;
- 1-8 MiB RAM;
- Olivetti IOC02 and port 61h-63h glue;
- PC87310 as a temporary Super I/O approximation;
- onboard Paradise PVGA1A with 256 KiB;
- Olivetti-flavoured AT 8042 and 128-byte AT CMOS.

The driver installs a `60000-7FFFF` to `80000-9FFFF` RAM alias required by
the Phoenix memory-controller test. Headland CR0 bit 2 starts clear on the PCS
286 and PCS 386SX while retaining the existing default on other machines.

## CMOS defaults

A new CMOS image is seeded conservatively for the original 1 MiB
configuration: 640 KiB base memory, 384 KiB extended memory, one 1.44 MB
floppy, 80x25 colour video and no 80387. The Phoenix checksum covers registers
10h-2Dh and is stored big-endian in 2Eh/2Fh.

The Setup utility is expected after explicitly clearing CMOS. Persisted CMOS
and all supported RAM sizes still require certification.

## 8042 enable-keyboard handshake

Without a machine-specific response, Phoenix reports:

```text
I/O Controller Error : 3
Unrecoverable power-up error
```

Disassembly at `F000:37C6` shows the required transaction:

```text
mov al, AEh
call send_8042_command
wait_until_port_64_bit_0_is_set
in  al, 60h
cmp al, 3Bh
jne wait
```

The generic AT 8042 enables the keyboard on command AEh but does not provide
the byte expected by this Olivetti firmware. The PCS 386SX implementation
therefore queues one pending `3Bh` response on the first AEh of each POST. A
queue is required because Ctrl, Alt and Delete break codes may still occupy
OBF during a warm reset. The response is model-scoped and consumed once. It is
rearmed whenever the P2 reset output is asserted. This is not a periodic key
injector.

Command FEh is also treated as a real reset pulse when P2.0 is already low;
otherwise the generic edge-based path cannot observe another falling edge.

## Keyboard translation and warm reset

Phoenix leaves PCMODE and XLAT set together. Unlike the generic IBM AT case,
the Olivetti keyboard still supplies scan set 2 and expects the controller to
translate it to set 1. PCMODE therefore no longer suppresses XLAT for
`KBC_VEN_OLIVETTI`; F1 is converted from `05h` to `3Bh`, and F2 has been
verified in the built-in Setup.

IOC02 is a soft-reset device. Its reset callback originally restored the PCS
286 first-read workaround unconditionally. On the PCS 386SX this changed the
register 6Ah read-back protocol after Ctrl+Alt+Delete and caused
`I/O Controller Error : 2`. IOC02 reset now selects that workaround from the
active machine, preserving the PCS 386SX write/read semantics.

An automated Ctrl+F12 test (BluMach's Send Control+Alt+Delete action) now
completes the second POST: memory, parity, PIC, DMA, keyboard, clock/calendar,
protected mode and CMOS pass, followed by another MS-DOS 3.30a boot.

The service chord Left Shift+Ctrl+Alt+Delete takes a distinct Phoenix path.
It reads IOC02 register 6Ah immediately after reset, before writing it. The
reset value was incorrectly transformed from `04h` to `24h`, producing
`I/O Controller Error : 2`. A read-first transaction now exposes the ready
value `04h`; a write-first transaction retains the transformed read-back used
by normal POST. The corrected path completes POST and boots DOS. Physical
confirmation that holding Left Shift selects Setup remains pending.

Register 68h also selects the IOC02 function exposed at 6Ah. With its low
five bits clear, writes to 6Ah are not latched. Phoenix verifies this isolation
by selecting 00h, writing 55h and requiring a different value on read-back.
Treating 6Ah as an unconditional register left `AH=03h` at the end of the test
and was displayed as `I/O Controller Error : 2`; selector-aware writes leave
`AH=00h` and pass the detailed test.

The optional 80387SX is detected and passes POST. Existing CMOS images track
the emulator's physical FPU selection in equipment byte 14h and refresh the
Phoenix checksum. The onboard Paradise adapter must remain selected as
`internal`; choosing `none` produces a black 0 Hz display even though the
machine continues running.

## Relevant commits

- `58cfdcb7d` — initial PCS 386SX port;
- `ad8a8280e` — correct the 8042 FEh reset pulse;
- `e68fc4d5e` — emulate the PCS 386SX AEh/3Bh handshake.
- `776315855` — preserve XLAT for Olivetti PCMODE;
- `c9e04540b` — preserve KBC and IOC02 state across warm reset.
- this change — distinguish IOC02 read-first Setup entry from normal POST.

## Remaining validation

- complete Resident Diagnostics;
- validate 1, 2, 4 and 8 MiB configurations;
- validate date/time save and CMOS persistence;
- validate the complete interactive keyboard matrix and repeated warm resets;
- confirm Left Shift+Ctrl+Alt+Delete enters Setup from a physical keyboard;
- boot the 1.44 MB floppy and Olivetti Customer Utility 1.51;
- validate official 20, 40 and 100 MB hard-disk configurations;
- replace PC87310, IOC02 and the static RAM alias where better hardware
  evidence becomes available.
