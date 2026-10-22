# Olivetti M10 engineering notes

## Outcome and evidence boundary

BluMach has an **experimental** M10: it runs the local 32 KB international
system ROM to the main menu and Microsoft BASIC, accepts typing through the
key matrix, keeps time with the calendar clock, and restarts warm from the
battery-backed RAM and clock saved in the machine folder. It is a board model
written from the service manual and primary data books, not a cycle-accurate
recreation of every bus phase.

As in the other notes, *documented* means manufacturer or data-book material,
*observed* means controlled execution of the local ROM, and *approximate*
names a deliberate substitute. Reaching the menu or BASIC validates the
guest-visible path only.

## Sources

- Olivetti, *M10 Manuale generale per l'assistenza*, 4100230 N (0), October
  1983: memory map, I/O decoding, LCD selection (6.14), keyboard matrix (6.8),
  buzzer (6.12), clock (6.15), language jumpers (6.16.3), power and reset
  (6.20, 6.21).
- Olivetti, *M10 Portable Computer Operations Guide*, 1983: keyboard layout,
  first-switch-on clock, BASIC sign-on and bytes free.
- Intel MCS-80/85 Family User's Manual and the OKI 1986 Microprocessor Data
  Book: MSM80C85A and MSM81C55.
- Hitachi 1983 LCD Driver LSI data book: HD44102CH.
- NEC 1983 Integrated Circuits for Consumer Use: uPD1990AC.
- MAME's kc85 driver was consulted only as a secondary source for the order of
  the ten LCD drivers on the panel. No MAME code is used.

Firmware stays local. It is not part of the repository or its tests.

## From probe to machine

The board was first exercised by a diagnostic tool,
`portable-engine-m10-firmware-probe`, which still exists for focused firmware
investigations. Its wiring moved into `systems/olivetti-m10`, with the chips
as separate components: the 8085 CPU, ten HD44102 drivers, the uPD1990AC and
the MSM81C55 ports and timer.

| Area | Model |
|---|---|
| Memory | ROM 0000h-7FFFh; RAM from FFFFh downwards in 8 KB steps. Empty addresses read FFh (assumption). |
| 81C55 | B0h-BFh, AD2-AD0 select the register. PA/PB drive the LCD chip selects, the key rows and the clock pins; PC reads the clock data and the language jumpers. Its TIMER IN is the CPU clock (2.4576 MHz, as in VirtualT and MAME). |
| E0h-EFh | Reads return the key matrix; writes latch the clock STB (bit 2) and the printer strobe (bit 1). |
| LCD | F0h-FFh, A0 selects data or instruction; chips 0-4 form the top half and 5-9 the bottom half, 50 columns each, of which the panel shows 240. |
| Clock | TP on RST 7.5; the firmware selects 256 Hz. Time advances in exact 32.768 kHz periods scheduled from the TP deadline. |
| Keyboard | Nine rows, one diode per key, so pressed keys never create phantom contacts. Host keys map by position to the USA-ASCII layout; CAPS LOCK latches mechanically. |
| Buzzer | PB5 AND (PB2 OR TIMER OUT), from Fig. 6-13. |
| Power | P.C.S. on PB4 switches the machine off (after 10 idle minutes, or low battery); a reset stands for the power switch. RAM survives. |

## Persistence

Two battery-backed states are saved: `ram`, an image of 8000h-FFFFh, and
`rtc`, the uPD1990AC counter, divider phase, latched commands and shift
register with the wall-clock second at which it was saved. When the machine
opens again, the clock advances by the time it spent closed, as the battery
kept it running. The portable launcher writes them to `state/ram.bin` and
`state/rtc.bin` inside the machine folder; the headless runner takes one
`--persistent-state role=path` per state.

The firmware decides cold or warm start from a RAM signature at F5F0h. A cold
start sets the clock to 01 Jan 00:00:00 from a ROM table, so the initial clock
option matters only when RAM is warm but no clock was saved. The chip keeps no
year: the firmware stores it in RAM and prints a fixed century of 19.

## Validation

- Menu and BASIC sign-on match the operations guide, with 29638 bytes free
  in the menu and 29382 in BASIC at 32 KB; 5062, 13254 and 21446 at 8, 16 and
  24 KB match the guide's table.
- TP period 3.906 ms (256 Hz), as the 4 ms of the service manual.
- Three sessions with the real ROM: a BASIC line and `DATE$` typed in the
  first, a warm second start still showing `10 Oct,1983` with the time
  running, and `LIST` returning the line in the third.
- After eleven idle minutes in the menu the machine switches itself off.
- `BEEP` toggles PB5 with PB2 high: 256 edges in about 60 ms.
- ROM-free contract tests cover the key matrix and jumpers, TP interrupts,
  RAM and clock persistence, both buzzer modes and the power-off latch.

## Limitations

- Timing is per instruction; device accesses see the start of the I/O machine
  cycle. HD44102 BUSY is about 9.3 us (approximate).
- The buzzer is observable through inspection only: the runtime has no audio
  output yet.
- Cassette, RS-232C UART, printer, bar-code reader, modem, the low-battery
  TRAP and the 81C55 RAM are not modelled.
- Bits of port C without a modelled source read high.
