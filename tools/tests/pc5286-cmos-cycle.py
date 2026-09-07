"""Exercise real NVR storage and Qt CMOS clearing in separate processes.

Usage: python pc5286-cmos-cycle.py NVR_TEST_EXE QT_CLEAR_TEST_EXE
Only generated fixtures inside a fresh temporary directory are changed.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

def run(*args):
    return subprocess.run([str(a) for a in args], check=True,
                          capture_output=True, text=True).stdout.strip()

assert len(sys.argv) == 3
nvr_exe, qt_exe = (Path(a).resolve(strict=True) for a in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="pc5286-cmos-cycle-") as name:
    vm = Path(name).resolve()
    (vm / "nvr").mkdir()
    original = vm / "nvr/pc5286.nvr"
    expansion = vm / "nvr/expansion.nvr"
    expansion.write_bytes(bytes(range(128)))
    run(nvr_exe, vm, "seed")
    before = original.read_bytes()
    assert len(before) == 128
    run(nvr_exe, vm, "check")
    backup = Path(run(qt_exe, vm)).resolve(strict=True)
    assert backup.parent == original.parent and backup.name.startswith("pc5286.nvr.backup-")
    assert not original.exists() and backup.read_bytes() == before
    run(nvr_exe, vm, "fresh")
    assert not original.exists()  # A read does not overwrite the backup/state.
    assert expansion.read_bytes() == bytes(range(128))
    backup.rename(original)
    run(nvr_exe, vm, "check")
    assert original.read_bytes() == before
print("PC5286 CMOS integrated cycle: save, new-process load/reset, Qt clear, fresh NVR, exact restore, expansion isolation PASS")
