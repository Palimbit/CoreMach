# PCS286 functional DMA coordinator

The private PCS286 composition now connects the existing AT DMA request to the
existing AT bus and 80286 HOLD/HLDA path. This is the minimum functional owner
needed for floppy channel 2 transfers; it does not reproduce electrical DMA
timing or infer undocumented Headland glue.

At a board execution boundary the coordinator inspects the pending DMA channel,
claims the bus as DMA8 or DMA16 and asserts HOLD through the normal arbiter. It
does not grant the DMA until the CPU has produced HLDA. Each subsequent call
services at most one byte or word through the DMA component. When HRQ falls it
withdraws the DMA grant first and then releases the bus request. A held DREQ may
start another single-mode transfer only after the CPU has lowered the old HLDA;
the arbiter's fresh-grant rule therefore remains effective.

The coordinator retains its first host error. It rejects an unsupported pending
channel or a winner-width change before grant, and never turns a bus, memory or
device failure into a guest exception. The DMA component remains responsible
for address/count/page registers, terminal count, DACK and endpoint effects.
The bus remains responsible for LOCK, ownership and access exclusion. The CPU
remains the only source of HLDA.

An authored real-component test programs channel 2 for two single transfers,
keeps DREQ asserted across them, and verifies the two separate HOLD/HLDA/grant
cycles, stale-HLDA wait, memory bytes, nominal DMA clocks and final release.
The boot probe now uses the same owner and routes DMA memory transactions through
the granted AT bus. With no diskette attached the BIOS run produces no DREQ, so
that run establishes composition only; media boot remains a separate result.
