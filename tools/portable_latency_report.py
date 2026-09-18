# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarize opt-in Qt traces; paint submission is not photon/glyph latency."""
import argparse
import json
import math
import re
from pathlib import Path

ROW = re.compile(r"bm-latency (\S+) id=(\d+) us=(\d+) value=(\d+)")


def summary(values):
    values = sorted(values)
    if not values:
        return {"samples": 0}
    return {"samples": len(values), **{
        name: round(values[max(0, math.ceil(len(values) * p) - 1)], 3)
        for name, p in (("p50_ms", .5), ("p95_ms", .95), ("max_ms", 1))}}


def report(text):
    rows = [(s, int(i), int(t), int(v)) for s, i, t, v in ROW.findall(text)]
    rows.sort(key=lambda row: row[2])
    queued, dispatch, ready, painted = {}, {}, {}, {}
    renders, frames, ticks = [], [], []
    for stage, serial, when, value in rows:
        if stage == "input-queued": queued[serial] = when
        elif stage == "input-dispatch": dispatch[serial] = when
        elif stage == "frame-ready":
            ready[serial] = (when, value)
            frames.append(when)
        elif stage == "paint-submit" and serial:
            painted.setdefault(serial, when)
        elif stage == "render-us": renders.append(value / 1000)
        elif stage == "guest-ticks": ticks.append((when, value))
    delivered = [(dispatch[i] - t) / 1000 for i, t in queued.items() if i in dispatch]
    first_paint = []
    for serial, enqueued in queued.items():
        candidates = [painted[f] for f, (_, last_input) in ready.items()
                      if last_input >= serial and f in painted and painted[f] >= enqueued]
        if candidates:
            first_paint.append((min(candidates) - enqueued) / 1000)
    return {
        "enqueue_to_dispatch": summary(delivered),
        "enqueue_to_first_post_input_paint_submission": summary(first_paint),
        "render": summary(renders),
        "frame_interval": summary([(b-a)/1000 for a, b in zip(frames, frames[1:])]),
        "observed_ticks_per_wall_second": round(
            (ticks[-1][1]-ticks[0][1])*1e6/(ticks[-1][0]-ticks[0][0]), 1)
            if len(ticks) > 1 and ticks[-1][0] > ticks[0][0] else None,
        "limitations": "First submitted frame after input; not proof of glyph visibility or monitor scanout. Opt-in logging adds overhead."
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(report(args.log.read_text(encoding="utf-8", errors="replace")), indent=2))
