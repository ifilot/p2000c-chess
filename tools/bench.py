#!/usr/bin/env python3
"""Benchmark: the computer's thinking time per level, in emulated seconds.

Plays 1.a3 (out of the opening book) and measures the Z80 cycles from the
moment the program starts thinking until Black's reply is on the screen.
Build with `make build EXTRA=-DSEARCH_STATS` to also see the depth reached
and the nodes searched (shown in the panel's note row). Run after make build.
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import EMULATOR, HD0, IPL, ROOT, TO_START, make_image

MHZ = 4.0


def run(level, actions):
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0), "--hard-disk-1", str(ROOT / "build/hd1.hda"),
           "--fast-storage", "--chunk-cycles", "2000", "--wait-for", "A>", "--send", "F:SCHAKEN\\r", *TO_START,
           "--send", str(level), "--wait-for", "Wit aan zet", "--send", "aAaA\\rw\\r",                          # a2-a3
           *actions, "--output", "json"]
    state = json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=900).stdout)
    assert state["status"] == "ok", state.get("message")
    flat = "".join(state["screen"])
    return state["cycles"], flat[10 * 64 + 50:11 * 64].strip()


def main():
    make_image(ROOT / "build/SCHAKEN.COM", ROOT / "build")
    for level in (1, 2, 3):
        start, _ = run(level, ["--wait-for", "denkt"])
        end, note = run(level, ["--wait-for", "denkt", "--wait-for", "aan zet"])
        print(f"level {level}: {(end - start) / MHZ / 1e6:5.1f} s   {note}")


if __name__ == "__main__":
    main()
