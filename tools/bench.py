#!/usr/bin/env python3
"""Benchmark: the computer's thinking time per level, in emulated seconds.

Plays 1.a3 (out of the opening book) and measures the Z80 cycles from the
moment the program starts thinking until Black's reply is on the screen.
When build/schaken.map is present, reads depth and node counters directly
from emulated RAM; no SEARCH_STATS build is needed. Run after make build.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import EMULATOR, HD0, IPL, ROOT, TO_START, make_image

MHZ = 4.0


def run(level, actions):
    symbols = {}
    mapfile = ROOT / "build/schaken.map"
    if mapfile.exists():
        for symbol, address in re.findall(r"^(_cpu_nodes|_cpu_depth)\s+= \$([0-9A-F]+)",
                                          mapfile.read_text(), re.M):
            symbols[symbol] = int(address, 16)
    dumps = []
    for symbol, size in (("_cpu_nodes", 2), ("_cpu_depth", 1)):
        if symbol in symbols:
            dumps += ["--dump-memory", f"{symbols[symbol]}:{size}"]
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0), "--hard-disk-1", str(ROOT / "build/hd1.hda"),
           "--fast-storage", "--chunk-cycles", "2000", "--wait-for", "A>", "--send", "F:SCHAKEN\\r", *TO_START,
           "--send", str(level), "--wait-for", "Wit aan zet", "--send", "aAaA\\rw\\r",                          # a2-a3
           *actions, *dumps, "--output", "json"]
    state = json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=900).stdout)
    assert state["status"] == "ok", state.get("message")
    flat = "".join(state["screen"])
    note = flat[10 * 64 + 50:11 * 64].strip()
    if len(state.get("memory", [])) == 2:
        nodes, depth = [int.from_bytes(bytes.fromhex(d["bytes"]), "little") for d in state["memory"]]
        note = f"reported depth {depth}, {nodes} nodes"
    return state["cycles"], note


def main():
    make_image(ROOT / "build/SCHAKEN.COM", ROOT / "build")
    for level in (1, 2, 3):
        start, _ = run(level, ["--wait-for", "denkt"])
        end, note = run(level, ["--wait-for", "denkt", "--wait-for", "aan zet"])
        print(f"level {level}: {(end - start) / MHZ / 1e6:5.1f} s   {note}")


if __name__ == "__main__":
    main()
