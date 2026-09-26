#!/usr/bin/env python3
"""Open build/SCHAKEN.COM in the graphical P2000C emulator (WSLg / Linux desktop).

Builds a disposable copy of the F: disk image with SCHAKEN.COM on it, then
launches ../p2000c-emulator/build/p2000c (or P2000C_EMULATOR) with the pro
package's A: disk and that copy, auto-running F:SCHAKEN after the first CP/M
prompt. Qt settings go to a temporary XDG directory so the session does not
touch your normal emulator preferences; storage delays are off.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

from render import HD0, ROOT, TOOL, make_image


def find_emulator() -> Path:
    for candidate in (os.environ.get("P2000C_EMULATOR"),
                      str(TOOL.parent / "p2000c-emulator/build/p2000c"), "p2000c"):
        if not candidate:
            continue
        path = Path(candidate)
        if path.is_file():
            return path
        found = subprocess.run(["which", candidate], capture_output=True, text=True).stdout.strip()
        if found:
            return Path(found)
    sys.exit("graphical emulator not found: build ../p2000c-emulator or set P2000C_EMULATOR")


def main() -> int:
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        sys.exit("no graphical display (DISPLAY/WAYLAND_DISPLAY); use WSLg or a Linux desktop")
    com = ROOT / "build/SCHAKEN.COM"
    if not com.is_file():
        sys.exit("build/SCHAKEN.COM missing: run make build first")
    executable = find_emulator()
    with tempfile.TemporaryDirectory(prefix="schaken-run-") as temporary:
        work = Path(temporary)
        settings = work / "config/P2000C Emulator Project/P2000C Emulator.conf"
        settings.parent.mkdir(parents=True)
        settings.write_text("[machine]\ncopowerEnabled=false\nstorageDelays=false\nspeed=1\n")
        env = dict(os.environ, XDG_CONFIG_HOME=str(work / "config"),
                   XDG_DATA_HOME=str(work / "data"), XDG_CACHE_HOME=str(work / "cache"))
        image = make_image(com, work)
        command = [str(executable), "--hard-disk-0", str(HD0), "--hard-disk-1", str(image),
                   "--autorun", "F:SCHAKEN"]
        print("Opening the emulator; close its window to finish. Disk changes are discarded.",
              flush=True)
        with subprocess.Popen(command, env=env) as process:
            try:
                return process.wait()
            except KeyboardInterrupt:
                process.terminate()
                process.wait(timeout=5)
                return 130


if __name__ == "__main__":
    sys.exit(main())
