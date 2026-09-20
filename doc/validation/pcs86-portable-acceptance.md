# Olivetti PCS 86 portable-product acceptance

This acceptance gate establishes whether the portable engine is usable as a
product for the first supported machine. It is deliberately narrower than a
claim of complete PCS 86 emulation: the gate covers deterministic firmware
execution, the documented boot path, local read-only floppy boot and the
desktop lifecycle.

No firmware or disk image is part of the repository. The operator supplies the
preserved local assets, which remain caller-owned and must be mounted
read-only.

## Automated profiles

Two profiles are kept separate because they answer different questions.

### Strict reference profile

The firmware probe disables expanded memory and runs 20 seconds of virtual
time. It is the short, exact comparison with the established engine baseline:

- status `BM_STATUS_OK` (`0`);
- 16,503,008 instructions;
- 11,977 successful I/O operations;
- every instruction boundary exact, with no ranged or unknown boundary;
- framebuffer CRC-32 `06bd8a15`.

### Default product profile

The headless frontend uses the same PCS 86 configuration as the desktop,
including 1,920 KiB of expanded memory. Its resident diagnostics therefore
take longer than the strict profile. At 60 seconds of virtual time it reaches
the MS-DOS 3.30a prompt with:

- status `BM_STATUS_OK` (`0`);
- 51,180,092 instructions;
- 152,359 successful I/O operations;
- 275 floppy reads and no floppy writes;
- framebuffer CRC-32 `e76fc1da`.

The difference at 20 seconds is expected: the default configuration is still
testing expanded memory, not stalled or slower because of an engine
regression.

Run both profiles on Windows after building the engine, headless frontend and
portable Qt frontend:

```powershell
powershell -ExecutionPolicy Bypass -File tools/validate-pcs86-portable.ps1 `
  -FirmwareEven <path-to-even-firmware> `
  -FirmwareOdd <path-to-odd-firmware> `
  -Floppy <path-to-system-diskette> `
  -BuildDirectory build/pcs86-acceptance
```

The script checks the expected asset sizes and SHA-256 values before running,
checks them again afterwards, and fails on any baseline mismatch. Use
`-KeepFrames` only when the generated local framebuffer captures are needed
for visual inspection.

## Desktop acceptance checklist

Launch the desktop with the same three local asset bindings and verify:

1. Resident diagnostics finish and the DOS prompt becomes visible.
2. The text cursor is visible and blinks at the prompt.
3. Typing `DIR` produces the expected make/break input and command output.
4. Pause freezes the guest and resume continues it without resetting.
5. Reset returns to POST and boots again.
6. Ejecting and reinserting the floppy updates the guest-visible medium.
7. Stop leaves the session stopped and permits a clean subsequent start.
8. Closing the window terminates without a hung worker or background process.
9. The firmware and disk image retain their original hashes.

This interactive checklist complements automated engine, frontend and Qt
tests. It does not replace them, and a visible DOS prompt alone is not enough
to accept lifecycle behaviour.

## Acceptance boundary

Passing this gate means the PCS 86 is the first usable vertical slice of the
portable product. It does not certify every V30 opcode, optional XTA hard disk,
all expansion combinations, cycle-perfect timing or every original peripheral.
Those remain independently testable follow-up work rather than hidden
conditions of this milestone.
