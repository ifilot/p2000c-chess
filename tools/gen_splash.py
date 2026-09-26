#!/usr/bin/env python3
"""Generate the title bitmap (512x252 monochrome) as RLE data in src/splash.rle.

splash.rle is the second part of the data file SCHAKEN.GFX, padded to whole
128-byte CP/M records. It takes no memory of its own: the program reads the
(count, value) pairs into the start of the framebuffer and unpacks them in
place, from the last pair backwards, writing from the end of the
framebuffer; this script checks that the write position never reaches a
pair not yet read.

Motif: a chessboard in perspective, its light squares dithered like the game
board, with a White king and queen and a Black knight and rook standing on
it -- the game's own Cburnett pieces (tools/pieces/), drawn large -- under
the title in the terminal's character-ROM glyphs, scaled up. Geometry is
worked out in physical units (a dot is 3 wide and 5 tall on the CRT), so
the board and the pieces keep their proportions on the tube. Also writes
build/splash.png, a preview rendered like the game screenshots.
"""
import io
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"
PIECE_SVG = ROOT / "tools/pieces/Chess_{}lt45.svg"
W, H = 512, 252
SX, SY = 3, 5                     # dot pitch


def blank():
    return [[0] * W for _ in range(H)]


def glyph_rows(sheet, code):
    return [[sheet.getpixel(((code & 15) * 12 + gx, (code >> 4) * 12 + gy))[1] != 0
             for gx in range(8)] for gy in range(12)]


def draw_text(dots, sheet, text, x, y, scale_x, scale_y, spacing=0):
    """Draws ROM glyphs into the dot image, scaled per axis; returns the end x."""
    for ch in text:
        rows = glyph_rows(sheet, ord(ch))
        for gy in range(12):
            for gx in range(8):
                if rows[gy][gx]:
                    for dy in range(scale_y):
                        for dx in range(scale_x):
                            px, py = x + gx * scale_x + dx, y + gy * scale_y + dy
                            if 0 <= px < W and 0 <= py < H:
                                dots[py][px] = 1
        x += 8 * scale_x + spacing
    return x


def text_width(text, scale_x, spacing=0):
    return len(text) * (8 * scale_x + spacing) - spacing


# --- the board in perspective ---------------------------------------------------

HORIZON = 12                       # line of the vanishing point
CX = 256                           # dot column of the vanishing point
NEAR_Z, FAR_Z = 1.0, 2.6           # depth of the near and far board edges
FOCAL_Y = 239.0                    # lines below the horizon at depth 1
HALF_WIDTH = 250.0                 # dots from the centre to the near corner (4 squares)


def project(u, v):
    """Board coordinates (u across 0..8, v away from the viewer 0..8) -> (x, line)."""
    z = NEAR_Z + (FAR_Z - NEAR_Z) * v / 8
    return CX + (u - 4) * HALF_WIDTH / 4 / z, HORIZON + FOCAL_Y / z


def draw_board(dots):
    for y in range(H):
        if y <= HORIZON:
            continue
        z = FOCAL_Y / (y - HORIZON)
        v = (z - NEAR_Z) / (FAR_Z - NEAR_Z) * 8
        if not 0 <= v < 8:
            continue
        for x in range(W):
            u = (x - CX) * z * 4 / HALF_WIDTH + 4
            if not 0 <= u < 8:
                continue
            light = (int(u) + int(v)) % 2 == 1
            # squares' edges: a dot's footprint in board units decides the line width
            du = z * 4 / HALF_WIDTH
            dv = (z * z / FOCAL_Y) * 8 / (FAR_Z - NEAR_Z)
            edge = min(u % 1, 1 - u % 1) < du * 0.5 or min(v % 1, 1 - v % 1) < dv * 0.5
            if u < du or u > 8 - du or v < dv or v > 8 - dv:
                dots[y][x] = 1                                  # outer frame
            elif light:
                dots[y][x] = int((x + y) % 2 == 0)
            elif edge:
                dots[y][x] = 0
    return dots


# --- pieces -----------------------------------------------------------------------

def piece_dots(piece, height_lines):
    """(white body, lines, silhouette) of a piece drawing height_lines tall, 3:5 aspect."""
    px_per_line = 10
    size = int(height_lines * 45 / 37.5 * px_per_line)       # the drawing spans ~37.5 of 45 units
    dot_px, line_px = px_per_line * SX // SY, px_per_line
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                          str(PIECE_SVG).format(piece)], capture_output=True, check=True).stdout
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    px = img.load()
    w, h = size // dot_px, size // line_px
    sil = [[0] * w for _ in range(h)]
    ink = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            a = k = n = 0
            for dy in range(line_px):
                for dx in range(dot_px):
                    r, g, b, al = px[x * dot_px + dx, y * line_px + dy]
                    n += 1
                    if al > 128:
                        a += 1
                        k += r < 128
            sil[y][x] = int(a >= n * 0.5)
            ink[y][x] = int(k >= n * 0.35)
    inner = [[int(all(0 <= y + dy < h and 0 <= x + dx < w and sil[y + dy][x + dx]
                      for dy in range(-2, 3) for dx in range(-3, 4))) for x in range(w)] for y in range(h)]
    body = [[int(sil[y][x] and not (ink[y][x] and inner[y][x])) for x in range(w)] for y in range(h)]
    return body, ink, sil


def stand(dots, piece, white, u, v, height_at_near):
    """Places a piece with its base centred on board square (u, v)."""
    x, y = project(u + 0.5, v + 0.35)
    z = NEAR_Z + (FAR_Z - NEAR_Z) * (v + 0.35) / 8
    body, ink, sil = piece_dots(piece, int(height_at_near / z))
    h, w = len(sil), len(sil[0])
    rows = [r for r in range(h) if any(sil[r])]
    bottom = rows[-1]
    left, top = int(x - w / 2), int(y - bottom)
    image = body if white else ink
    for r in range(h):
        for c in range(w):
            halo = any(0 <= r + dy < h and 0 <= c + dx < w and sil[r + dy][c + dx]
                       for dy in (-1, 0, 1) for dx in (-2, -1, 0, 1, 2))
            yy, xx = top + r, left + c
            if 0 <= yy < H and 0 <= xx < W:
                if halo:
                    dots[yy][xx] = 0
                if image[r][c]:
                    dots[yy][xx] = 1


def compose(sheet):
    dots = draw_board(blank())
    # back row first, so nearer pieces overlap farther ones
    stand(dots, "r", False, 6, 6, 90)
    stand(dots, "n", False, 1, 5, 90)
    stand(dots, "q", True, 5, 3, 90)
    stand(dots, "k", True, 3, 1, 90)
    # title: clear a band, then the letters
    title = "SCHAKEN"
    tw = text_width(title, 4, 8)
    draw_text(dots, sheet, title, (W - tw) // 2, 2, 4, 3, 8)
    sub = "voor de Philips P2000C"
    draw_text(dots, sheet, sub, (W - text_width(sub, 1)) // 2, 30, 1, 1)
    return dots


def rle(data):
    out = bytearray()
    i = 0
    while i < len(data):
        n = 1
        while i + n < len(data) and data[i + n] == data[i] and n < 255:
            n += 1
        out += bytes([n, data[i]])
        i += n
    return bytes(out)


def main():
    sheet = Image.open(FONT_SHEET).convert("RGB")
    dots = compose(sheet)
    raw = bytearray()
    for row in dots:
        for b in range(W // 8):
            v = 0
            for bit in range(8):
                v = (v << 1) | row[b * 8 + bit]
            raw.append(v)
    packed = rle(bytes(raw))
    out, size = len(raw), len(packed)
    for pair in range(size // 2 - 1, -1, -1):          # the decoder's order
        out -= packed[2 * pair]
        assert out >= 2 * pair, f"in-place unpacking would overwrite pair {pair}"
    records = (size + 127) // 128
    (ROOT / "src/splash.rle").write_bytes(packed + bytes(records * 128 - size))
    (ROOT / "src/splash.h").write_text(
        "/* Generated by tools/gen_splash.py -- do not edit. The title picture's\n"
        " * run-length data, the second part of SCHAKEN.GFX (src/splash.rle). */\n"
        "#ifndef SPLASH_H\n#define SPLASH_H\n\n"
        f"#define SPLASH_RLE_SIZE {size}\n#define SPLASH_RECORDS  {records}\n\n#endif\n")
    image = Image.new("L", (W, H))
    image.putdata([255 if v else 0 for row in dots for v in row])
    canvas = Image.new("L", (640, 288))
    canvas.paste(image, ((640 - W) // 2, (288 - H) // 2))
    canvas = canvas.resize((640 * SX, 288 * SY), Image.NEAREST)
    g = canvas.point(lambda v: 51 if v else 0)
    (ROOT / "build").mkdir(exist_ok=True)
    Image.merge("RGB", [g, canvas, g]).save(ROOT / "build/splash.png")
    print(f"wrote src/splash.rle ({len(packed)} bytes RLE), src/splash.h and build/splash.png")


if __name__ == "__main__":
    main()
