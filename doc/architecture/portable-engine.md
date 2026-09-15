# BluMach portable engine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: foundation for `0.2.0-dev`.

The portable engine is developed beside the inherited product and does not call
into its global state, configuration, device registry or user interfaces. The
two implementations can be enabled independently with
`BLUMACH_BUILD_LEGACY` and `BLUMACH_BUILD_ENGINE`.

## Dependency direction

```text
optional frontends
        |
        v
opaque runtime sessions
        |
        v
engine scheduler and CPU contract <--- reusable bus components
        |
        v
capability-based host services <--- null, Windows, POSIX, Circle, MorphOS
```

The engine and runtime are C11. They do not depend on Qt, SDL, native windowing,
host threads, C++ exceptions or a particular byte order. A frontend may use
those facilities without exposing them to an emulated component.

## Foundation invariants

- Every mutable value belongs to an engine, session, machine or component.
- A session has an explicit lifecycle and supports safe partial cleanup.
- Time uses integer ticks and same-time events use insertion order.
- CPU implementations receive a bounded budget and report consumed time.
- Buses model memory, I/O, program and data spaces independently.
- Debug access is an attribute of a transaction, not a second hidden bus.
- Host allocation, time and logging arrive through explicit capabilities.
- The null platform makes tests and GUI-free targets first-class builds.
- Every production file is covered exactly once by the provenance manifest.

The synthetic test machine has four instructions, two registers, memory, I/O,
halt, interrupt, a timer, event capture and register introspection. It is test
equipment and must never be exposed in the historical catalogue.

## Olivetti PCS 86 boundary

The canonical research identifies the PCS 86 processor as an NEC V30 at
10 MHz. The first real CPU target will therefore be an 808x-family interpreter
with the required V30 behaviour, not a target named or constrained as a pure
Intel 8088. No inherited CPU implementation is moved until its exact source
paths, commit, notices and adaptation method can be recorded.

The PCS 86 vertical slice starts only after this foundation is green. Its first
stage supplies firmware as a caller-owned blob, establishes reset execution and
memory transactions, and records instruction checkpoints without adding ROMs
or machine media to Git.
