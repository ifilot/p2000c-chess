#!/usr/bin/env python3
"""Run a CP/M program in the headless emulator and print its text screen.

Usage: python3 tools/run_headless.py PROGRAM.COM MARKER [EXTRA ACTIONS...]
The program is put on F: of a copy of the second disk image and started;
the run ends when MARKER appears on the screen (then one more million
cycles, so the rest of the line is printed).
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import EMULATOR, HD0, IPL, ROOT, make_image


def main():
    com, marker = Path(sys.argv[1]).resolve(), sys.argv[2]
    image = make_image(com, ROOT / "build")
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0), "--hard-disk-1", str(image),
           "--fast-storage", "--wait-cycles", "4000000000", "--wait-for", "A>",
           "--send", f"F:{com.stem.upper()}\\r", "--wait-for", marker, "--run", "1000000",
           *sys.argv[3:], "--output", "json"]
    state = json.loads(subprocess.run(cmd, capture_output=True, text=True).stdout)
    for row in state["screen"]:
        if row.strip():
            print(row.rstrip())
    print(f"({state['status']}, {state['cycles'] / 4e6:.1f} s at 4 MHz)")
    return 0 if state["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
