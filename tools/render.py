#!/usr/bin/env python3
"""Run build/SCHAKEN.COM in the headless P2000C emulator and save a PNG.

Usage: python3 tools/render.py [--splash | --start] [--wait-for TEXT] [--out FILE.png] [-- ACTIONS]

The PNG is a plain dump of the terminal's dot raster: graphics RAM merged
with the text plane (rendered with the character-ROM glyphs from the sibling
p2000c-emulator font sheet), one dot per 3x5 block, green on black, no CRT
effects. Both rasters are placed on the 640x288-dot text canvas the way the
terminal does it (the graphics raster is centred at the same dot pitch), so
text-mode and graphics-mode screenshots have the same size. Requires
the sibling p2000c-cpm-disk-tool checkout (headless emulator, dist/pro/
images) and the p2000c-emulator checkout (font sheet).
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT.parent / "p2000c-cpm-disk-tool"
EMULATOR = TOOL / "build/emulator/p2000c-mini"
IPL = TOOL / "tools/emulator/firmware/IPLDUMP.BIN"
HD0 = TOOL / "dist/pro/HD0_256.hda"
HD1 = TOOL / "dist/pro/HD1_256.hda"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

WIDTH, HEIGHT, BYTES_PER_LINE = 512, 252, 64
TEXT_RASTER = (640, 288)            # 80x8 by 24x12 dots; sets the dot pitch
FONT_SHEET = TOOL.parent / "p2000c-emulator/assets/font/P2000C font mini.png"

DOT_PITCH = (3, 5)                  # horizontal:vertical dot pitch on the 4:3 CRT
FOREGROUND = (51, 255, 51)          # plain phosphor green
BACKGROUND = (0, 0, 0)


def make_image(com: Path, out_dir: Path) -> Path:
    """Copy the F: image and drop the program (and SCHAKEN.GFX, when next to
    it) in its high partition (F:)."""
    image = out_dir / "hd1.hda"
    shutil.copy(HD1, image)
    gfx = com.with_suffix(".GFX")
    files = [str(com)] + ([str(gfx)] if gfx.is_file() else [])
    cli = ["python3", "-m", "p2000c_disk.cli", "put-many", str(image), *files,
           "--partition", "high", "--replace"]
    subprocess.run(cli, cwd=TOOL, env={"PYTHONPATH": "src", "PATH": "/usr/bin:/bin"},
                   check=True, capture_output=True)
    return image


# let the title picture finish, press a key, then start a level-1 game as White
TO_START = ["--run", "24000000", "--send", " ", "--wait-for", "Sterkte van de computer"]
SKIP_SPLASH = TO_START + ["--send", "1"]


def run(image: Path, out_dir: Path, wait_for: str, extra: list[str], splash: bool = False,
        start: bool = False) -> tuple[dict, bytes]:
    dump = out_dir / "graphics.bin"
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0),
           "--hard-disk-1", str(image), "--fast-storage",
           "--wait-for", "A>", "--send", "F:SCHAKEN\r",
           *([] if splash else TO_START if start else SKIP_SPLASH),
           "--wait-for", wait_for, *extra,
           "--dump-graphics", str(dump), "--output", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    state = json.loads(result.stdout)
    if result.returncode != 0:
        print(f"emulator exit {result.returncode}: {state.get('message')}", file=sys.stderr)
    return state, dump.read_bytes()


# --- raster: what the terminal board puts on the tube ------------------------

def raster_dots(state: dict, graphics: bytes) -> tuple[list[list[int]], int, int]:
    """Per-dot intensity code (0 off, 1 half, 2 normal, 3 bold) for the active raster.

    Mirrors DisplayWidget::rebuild_raster: in graphics modes the 64x21 text
    plane is merged into the 512x252 raster; a character dot on a lit high-res
    pixel goes dark, on medium-res it inverts both bit planes.
    """
    mode = state["graphics_mode"]
    sheet = Image.open(FONT_SHEET).convert("RGB")
    flat = "".join(state["screen"])
    if mode == "character":
        width, height, columns = TEXT_RASTER[0], TEXT_RASTER[1], 80
    else:
        width, height, columns = WIDTH, HEIGHT, 64
    rows = []
    for y in range(height):
        row = [0] * width
        gline = graphics[y * BYTES_PER_LINE:(y + 1) * BYTES_PER_LINE]
        for x in range(width):
            code = ord(flat[(y // 12) * columns + x // 8]) & 0xFF
            r, g, b = sheet.getpixel(((code & 15) * 12 + x % 8, (code >> 4) * 12 + y % 12))
            char_dot = g != 0
            if mode == "character":
                level = 2 if char_dot else 0
            elif mode == "high-512":
                lit = gline[x // 8] & (0x80 >> (x & 7))
                level = 0 if (lit and char_dot) else 2 if (lit or char_dot) else 0
            else:
                lx = x // 2
                byte = gline[lx // 4]
                hi, lo = bool(byte & (0x80 >> (lx & 3))), bool(byte & (0x08 >> (lx & 3)))
                level = (2 if hi else 0) | (1 if lo else 0)   # 0 bg, 1 half, 2 normal, 3 bold
                if char_dot:
                    level = [3, 0, 1, 2][level]
            row[x] = level
        rows.append(row)
    return rows, width, height


def compose(state: dict, graphics: bytes) -> Image.Image:
    """Plain render: one raster dot -> a 3x5 green block (the CRT dot pitch)."""
    dots, rw, rh = raster_dots(state, graphics)
    raster = Image.new("L", (rw, rh), 0)
    raster.putdata([255 if level else 0 for row in dots for level in row])
    canvas = Image.new("L", TEXT_RASTER, 0)
    canvas.paste(raster, ((TEXT_RASTER[0] - rw) // 2, (TEXT_RASTER[1] - rh) // 2))
    canvas = canvas.resize((TEXT_RASTER[0] * DOT_PITCH[0], TEXT_RASTER[1] * DOT_PITCH[1]), Image.NEAREST)
    return Image.merge("RGB", [canvas.point(lambda v, c=c: c if v else b) for c, b in zip(FOREGROUND, BACKGROUND)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wait-for", default="Wit aan zet")
    parser.add_argument("--out", type=Path, default=ROOT / "build/board.png")
    parser.add_argument("--com", type=Path, default=ROOT / "build/SCHAKEN.COM",
                        help="CP/M binary built by make build")
    parser.add_argument("--splash", action="store_true", help="stay on the title picture instead of skipping it")
    parser.add_argument("--start", action="store_true", help="stop at the start screen")
    parser.add_argument("extra", nargs="*", help="additional emulator actions")
    args = parser.parse_args()
    out_dir = ROOT / "build"
    out_dir.mkdir(exist_ok=True)
    com = args.com.resolve()
    image = make_image(com, out_dir)
    state, graphics = run(image, out_dir, args.wait_for, args.extra, args.splash, args.start)
    lit = sum(bin(b).count("1") for b in graphics)
    print(f"status={state['status']} mode={state['graphics_mode']} "
          f"cycles={state['cycles']:,} lit_pixels={lit} com={com.stat().st_size}B")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    compose(state, graphics).save(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
