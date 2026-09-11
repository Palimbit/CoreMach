# Runtime machine controls, v1

## Purpose

This is a small host-interface contract for controls that alter documented
emulated machine state while the machine is running.  It is not a replacement
for the configuration dialog, a generic collection of emulator shortcuts, or
a model-specific Qt conditional.

The machine/device supplies a static descriptor, reports its current value and
applies a requested value.  The Qt frontend only renders the descriptors
currently registered by the running platform.  Firmware keyboard actions and
the host menu therefore observe the same state.

## Initial controls

| Platform | Semantic ID | Kind | Evidence/state path |
| --- | --- | --- | --- |
| Toshiba T3200 | `display.output` | selector | documented Fn+Home/Fn+End; request is consumed by the original BIOS timer/KBC path |
| Toshiba T3200 | `display.lines` | selector | documented Fn+Down 350/400-line presentation |
| Toshiba T5100 | `display.output` | selector | documented Fn+Home/Fn+End notification and BIOS acknowledgement path |
| Toshiba T5100 | `display.lines` | selector | documented Fn+Down notification and BIOS acknowledgement path |

The menu disappears when no platform descriptor is registered, including after
a machine stops or a startup fails.

## Deliberate exclusions

* Secondary-monitor visibility is a host-window preference.  It does not claim
  to switch a physical display, and remains in the existing generic monitor UI.
* T5200's known Ctrl+Home panel restore is not yet exposed because the current
  implementation has no documented runtime panel-off operation to pair with
  it.
* Juko ST turbo is currently driven by guest I/O/TURBO.COM, not yet established
  as an independently host-operable front-panel control.
* Olivetti PCS 86 keylock is presently a startup/configuration condition.  It
  needs a persistence and runtime-behaviour decision before it can become a
  control.

Future kinds (toggle, momentary action, indicator and dial) must be added only
with an emulated state owner and documented behaviour.  Catalogue metadata may
later localize presentation and attach evidence, but must not become the source
of hardware state.
