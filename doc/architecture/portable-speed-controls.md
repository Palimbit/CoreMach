# Portable frontend speed controls

The status bar reports the rolling guest-time / wall-time ratio independently
of presentation FPS. 100% means one virtual second per host second, 50% half
real time and 200% twice real time. Samples span at least 500ms on the steady
host clock. Pause, guest reset and mode switches invalidate the sample, so an
idle pause is never included in the next result. Unknown clock rates report
no percentage. This compares against the runtime's declared clock, not against
physical hardware: PCS286 timing is still provisional.

Machine > Velocidad sin límite (benchmark) removes host pacing and the running
worker's 1ms wait. It does not alter guest CPU clocks, peripheral clocks, device
deadlines or instruction semantics. Work remains bounded to 5ms guest chunks;
commands are handled between chunks and video remains independently wall-paced.
The UI and emulator worker remain separate threads. This mode may occupy a full
host core. It is an interactive throughput indicator, not a standardized hardware
benchmark or an assurance that every guest workload behaves like physical silicon.

Each new session defaults to paced mode. Returning to paced mode resets the
host pacing origin and does not try to repay the time accumulated while unlimited.
Pausing in unlimited mode blocks the worker normally; resuming restores its chosen
mode. The display explicitly labels unlimited mode. No machine-ID conditional is
used: PCS86, M15 and PCS286 all use the same frontend control.

Tests exercise 50/100/200% ratios, unknown rates, guest reset and pause-like gaps;
a synthetic PCS86 reset-vector loop checks unlimited/paced transitions, pause,
resume, reset, shutdown and CMOS capture without proprietary firmware.

Paced mode now skips its 1ms host wait when the remaining pacing deficit is at
least 1ms. The query is read-only (does not consume the next slice); smaller
deficits still use the interruptible wait to avoid spinning. Commands and video
are still serviced between bounded slices. Tests cover ahead/on-time/behind,
reset, disabled pacing and a simulated fully loaded worker. This removes an
unnecessary throttle; it does not promise 100% throughput on a saturated host.
The previously observed 70% paced / 99% unlimited is a user observation, not a
calibration of physical PCS286 speed. Live improvement remains to be measured.
