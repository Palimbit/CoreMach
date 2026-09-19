# PCS 86 battery-backed RTC startup state (2026-09-19)

The Spanish OEM DOS loader conditionally installs its clock/calendar modules.
Static analysis of the preserved `IBMBIO.COM` shows that it calls BIOS
`INT 1Ah, AH=02h` twice and leaves its internal flag at zero when both `CX` and
`DX` are zero. This omits `0Fh` paragraphs of resident code. It is not a CPU
instruction failure and must not be repaired by reserving anonymous DOS memory.

An owner-run, read-only probe on a physical PCS 86 with BIOS 1.08 and no XTIDE
reported flag `0070:0A10=01`, List of Lists segment `029Eh` and first MCB
`09E6h`. A portable cold RTC previously reported flag zero, segment `028Fh`
and first MCB `09D7h`. The DOS system files used for both sides had identical
SHA-256 identities.

The MM58167's counter bytes are only part of the required battery-backed state.
BIOS 1.08 also validates and rewrites a weekday/checksum encoding stored in its
alarm RAM. Seeding only a nonzero time lets the first BIOS read succeed, then
makes subsequent reads fail after that rewrite. The frontend fallback therefore
supplies the complete 32-byte image: deterministic calendar counters plus a
coherent alarm-RAM encoding. The machine configuration passes the bytes through
to the existing component; neither the system nor the engine reads a wall clock,
path or host API.

The repeatable headless probe with BIOS 1.08 and a temporary derivative of the
canonical 720 KiB Customer disk then reported `0A10=01`, clock pointers
`0A1F=22D0` and `0A21=21F0`, List of Lists `029E:0026` and first MCB `09E6`.
Those key values match the physical observation. The derivative and framebuffer
used for validation remain outside Git; the canonical disk was opened read-only
and reported zero writes.

This change establishes valid deterministic startup state, not wall-clock
synchronization. A following frontend cut uses the runtime's opaque named-state
contract to retain the 32 bytes between runs. Qt stores them in its own host
settings; headless reads and writes only an explicitly supplied path. Both can
instead supply a zero image and discard it at shutdown to reproduce a depleted
battery and the firmware's `Calendar/Time Fail` path. Warm reset still retains
the live component state. This does not claim that the complete Customer
diagnostic now passes.
