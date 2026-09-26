#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Show the private PCS286 portable boot probe in a live diagnostic window."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--cmos", required=True, type=Path)
    parser.add_argument("--floppy", required=True, type=Path)
    parser.add_argument("--steps", type=int, default=16_500_000)
    parser.add_argument("--live-every", type=int, default=100_000)
    args = parser.parse_args()

    for path in (args.probe, args.bios, args.cmos, args.floppy):
        if not path.is_file():
            parser.error(f"file does not exist: {path}")

    frame_dir = Path(tempfile.mkdtemp(prefix="blumach-pcs286-live-"))
    command = [
        str(args.probe), "--bios", str(args.bios), "--ram-mib", "1",
        "--steps", str(args.steps), "--peripheral-ns", "1000",
        "--io-holes", "ff", "--rtc-divider", "classic",
        "--cmos-file", str(args.cmos), "--video", "pvga1a",
        "--floppy", str(args.floppy), "--live-dir", str(frame_dir),
        "--live-every", str(args.live_every),
    ]
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", bufsize=1,
    )
    events: queue.Queue[tuple[str, object]] = queue.Queue()

    def read_stdout() -> None:
        assert process.stdout is not None
        with process.stdout:
            for line in process.stdout:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") == "live_frame":
                    events.put(("frame", event))
                elif event.get("event") == "summary":
                    events.put(("summary", event))
        events.put(("exit", process.wait()))

    def read_stderr() -> None:
        assert process.stderr is not None
        with process.stderr:
            for line in process.stderr:
                events.put(("error", line.rstrip()))

    threading.Thread(target=read_stdout, daemon=True).start()
    threading.Thread(target=read_stderr, daemon=True).start()

    root = tk.Tk()
    root.title("Olivetti PCS 286 — BluMach portable diagnostic")
    root.configure(background="black")
    image_label = tk.Label(root, background="black")
    image_label.pack(fill="both", expand=True)
    status = tk.StringVar(value="Arrancando la BIOS en el motor portable…")
    tk.Label(root, textvariable=status, anchor="w", background="#202020",
             foreground="white", padx=8, pady=5).pack(fill="x")
    root.geometry("900x550")
    current_image: tk.PhotoImage | None = None
    last_path: Path | None = None

    def close() -> None:
        if process.poll() is None:
            process.terminate()
        root.destroy()

    def poll() -> None:
        nonlocal current_image, last_path
        try:
            while True:
                kind, payload = events.get_nowait()
                if kind == "frame":
                    event = payload
                    assert isinstance(event, dict)
                    path = frame_dir / f"frame-{int(event['step']):020d}.ppm"
                    current_image = tk.PhotoImage(file=str(path))
                    image_label.configure(image=current_image)
                    status.set(
                        f"Motor portable · paso {int(event['step']):,} / {args.steps:,}"
                    )
                    if last_path is not None:
                        last_path.unlink(missing_ok=True)
                    last_path = path
                elif kind == "summary":
                    event = payload
                    assert isinstance(event, dict)
                    status.set(
                        f"Detenido: {event.get('reason')} · "
                        f"pasos {int(event.get('steps', 0)):,} · "
                        f"estado {event.get('status')}"
                    )
                elif kind == "error":
                    status.set(f"Error del probe: {payload}")
                elif kind == "exit" and payload not in (0, 2):
                    status.set(f"El probe terminó con código {payload}")
        except queue.Empty:
            pass
        if root.winfo_exists():
            root.after(100, poll)

    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, poll)
    try:
        root.mainloop()
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(frame_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
