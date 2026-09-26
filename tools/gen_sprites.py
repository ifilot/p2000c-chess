#!/usr/bin/env python3
"""Generate src/sprites.bin and .h: the chess pieces and board markers for the 512x252 mode.

Geometry: the terminal draws graphics at the text dot pitch, and the 9-inch
CRT gives dots a 3:5 horizontal-to-vertical pitch (p2000c-emulator,
docs/hardware.md), so a 48x29-dot square is (nearly) square on the tube.

The pieces are Colin Burnett's drawings from Wikimedia Commons (the SVGs in
tools/pieces/), rendered with rsvg-convert at the true 3:5 aspect and
reduced to dots. The screen emits light, so a White piece is its solid,
bright silhouette with the inner lines cut out, and a Black piece is those
lines alone, bright on a dark body: the two are each other's negative,
which keeps the details of both readable. Every piece and marker
comes with a mask, its shape grown by a dot or two: the mask is cleared
first, which gives everything a dark halo that keeps it readable on the
dithered light squares.

All bitmaps are row-major, MSB = leftmost dot, one cell (6 bytes) wide.
Coordinate labels reuse the terminal's own 8x12 character-ROM glyphs from
the font sheet in the sibling p2000c-emulator checkout. The bitmaps go to
src/sprites.bin, the first part of the data file SCHAKEN.GFX, with their
offsets in src/sprites.h. Also writes build/sprites.png, a preview of a
board built from these bitmaps.
"""
import io
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"
PIECE_SVG = ROOT / "tools/pieces/Chess_{}lt45.svg"

SX, SY = 3, 5                   # dot pitch
CELL_W, CELL_H = 48, 29         # square in dots (6 bytes wide)
PIECES = "PNBRQK"               # type order 1..6 as in chess.h
# The 45-unit SVG square is rendered at 1500 px: a dot is 30x50 px, so the
# drawing spans 50 dots by 30 lines, 1.11 dots or 0.67 lines per unit.
RENDER, DOT_PX, LINE_PX = 1500, 30, 50
SVG_W, SVG_H = RENDER // DOT_PX, RENDER // LINE_PX
SILHOUETTE = 0.5                # share of a dot the drawing must cover
INK = 0.4                       # share of a dot the black lines must cover


# --- bitmaps as lists of rows of 0/1 -------------------------------------------

def blank(w, h):
    return [[0] * w for _ in range(h)]


def grow(m, rx, ry):
    """Dilation with an elliptical rx-by-ry neighbourhood."""
    h, w = len(m), len(m[0])
    offsets = [(dx, dy) for dy in range(-ry, ry + 1) for dx in range(-rx, rx + 1)
               if (dx / (rx + 0.5)) ** 2 + (dy / (ry + 0.5)) ** 2 <= 1.0]
    return [[int(any(0 <= y + dy < h and 0 <= x + dx < w and m[y + dy][x + dx] for dx, dy in offsets))
             for x in range(w)] for y in range(h)]


def combine(a, b, op):
    return [[op(a[y][x], b[y][x]) for x in range(len(a[0]))] for y in range(len(a))]


# --- pieces --------------------------------------------------------------------

def render_svg(piece):
    """Coverage per dot of the drawing (alpha) and of its black lines."""
    png = subprocess.run(["rsvg-convert", "-w", str(RENDER), "-h", str(RENDER),
                          str(PIECE_SVG).format(piece.lower())],
                         capture_output=True, check=True).stdout
    px = Image.open(io.BytesIO(png)).convert("RGBA").load()
    cover, ink = blank(SVG_W, SVG_H), blank(SVG_W, SVG_H)
    samples = (DOT_PX // 2) * (LINE_PX // 2)
    for y in range(SVG_H):
        for x in range(SVG_W):
            a = k = 0
            for dy in range(0, LINE_PX, 2):
                for dx in range(0, DOT_PX, 2):
                    r, g, b, alpha = px[x * DOT_PX + dx, y * LINE_PX + dy]
                    if alpha > 128:
                        a += 1
                        k += r < 128
            cover[y][x] = a / samples
            ink[y][x] = k / samples
    return cover, ink


def erode(m, rx, ry):
    h, w = len(m), len(m[0])
    return [[int(all(0 <= y + dy < h and 0 <= x + dx < w and m[y + dy][x + dx]
                     for dy in range(-ry, ry + 1) for dx in range(-rx, rx + 1))) for x in range(w)]
            for y in range(h)]


def pieces():
    """(white image, black image, mask) per type, each SVG_W x SVG_H dots."""
    result = {}
    for p in PIECES:
        cover, ink = render_svg(p)
        silhouette = [[int(v >= SILHOUETTE) for v in row] for row in cover]
        lines = [[int(v >= INK) for v in row] for row in ink]
        # lines at least two dots inside the edge are cut out of the White body;
        # nearer the edge they would only fray its outline
        inner = erode(silhouette, 2, 1)
        white = combine(silhouette, combine(lines, inner, lambda a, b: a & b), lambda a, b: a & (1 - b))
        result[p] = (white, lines, grow(silhouette, 2, 1))
    return result


def place(set_):
    """Cuts the drawings to the cell width and the rows any piece uses (halo
    included) and centres that band vertically in the square."""
    rows = [y for y in range(SVG_H) if any(any(set_[p][2][y]) for p in PIECES)]
    top, bottom = rows[0], rows[-1] + 1
    left = (SVG_W - CELL_W) // 2
    cut = {p: tuple([row[left:left + CELL_W] for row in m[top:bottom]] for m in set_[p]) for p in PIECES}
    return cut, (CELL_H - (bottom - top)) // 2, bottom - top


# --- markers (full cell, CELL_W x CELL_H) --------------------------------------

def cursor():
    """Corner brackets: arms 11 dots x 6 lines, 3 dots / 2 lines thick."""
    m = blank(CELL_W, CELL_H)
    for y in range(CELL_H):
        for x in range(CELL_W):
            cx = x if x < CELL_W // 2 else CELL_W - 1 - x
            cy = y if y < CELL_H // 2 else CELL_H - 1 - y
            if (cy < 2 and cx < 11) or (cx < 3 and cy < 6):
                m[y][x] = 1
    return m


def selection():
    """A frame round the whole square, 2 dots / 1 line thick, one dot inside the edge."""
    m = blank(CELL_W, CELL_H)
    for y in range(1, CELL_H - 1):
        for x in range(1, CELL_W - 1):
            if y == 1 or y == CELL_H - 2 or x in (1, 2) or x in (CELL_W - 3, CELL_W - 2):
                m[y][x] = 1
    return m


def target_dot():
    """A round dot in the middle of an empty square: 10x6 dots, round on the tube."""
    m = blank(CELL_W, CELL_H)
    cx, cy = (CELL_W - 1) / 2, CELL_H / 2
    for y in range(CELL_H):
        for x in range(CELL_W):
            if ((x - cx) / 5.0) ** 2 + ((y - cy) / 3.0) ** 2 <= 1.0:
                m[y][x] = 1
    return m


def capture_corners():
    """Triangles in the four corners of a square holding a piece that can be taken."""
    m = blank(CELL_W, CELL_H)
    for y in range(CELL_H):
        for x in range(CELL_W):
            cx = x if x < CELL_W // 2 else CELL_W - 1 - x
            cy = y if y < CELL_H // 2 else CELL_H - 1 - y
            if cx / 9.0 + cy / 5.5 <= 1.0:
                m[y][x] = 1
    return m


def rows_bytes(m):
    out = []
    for row in m:
        bits = 0
        for v in row:
            bits = (bits << 1) | v
        out.append(bits.to_bytes(len(row) // 8, "big"))
    return out


# --- labels ----------------------------------------------------------------------

def rom_glyph(sheet, code):
    """8x12 glyph: sheet cell (code & 15, code >> 4), 12-px pitch, green = dot."""
    return [[int(sheet.getpixel(((code & 0x0F) * 12 + gx, (code >> 4) * 12 + gy))[1] != 0)
             for gx in range(8)] for gy in range(12)]


def label_rows(glyph, width, shift):
    """A glyph in a width-byte bitmap, moved right by shift dots (left if negative)."""
    out = []
    for row in glyph:
        bits = 0
        for v in row:
            bits = (bits << 1) | v
        bits <<= width * 8 - 8
        bits = bits >> shift if shift >= 0 else bits << -shift
        out.append((bits & ((1 << width * 8) - 1)).to_bytes(width, "big"))
    return out


# --- output ----------------------------------------------------------------------

def c_array(name, rows, comment):
    flat = b"".join(rows)
    lines = [", ".join(f"0x{b:02X}" for b in flat[i:i + 16]) for i in range(0, len(flat), 16)]
    body = ",\n    ".join(lines)
    return f"/* {comment} */\nstatic const unsigned char {name}[{len(flat)}] = {{\n    {body}\n}};\n"


def preview(set_, markers, piece_top):
    """A board with a few pieces and markers, rendered like the screenshots."""
    layout = ["rnbqkbnr", "pppp.ppp", "........", "....p...", "..B.P...", ".....N..", "PPPP.PPP", "RNBQK..R"]
    # the knight on f3 selected: its targets marked, the cursor on g5
    marks = {(5, 5): "select", (4, 3): "capture", (6, 3): "dot", (7, 4): "dot", (3, 4): "dot",
             (6, 7): "dot", (4, 6): "dot"}
    cursor_at = (6, 3)
    w, h = CELL_W * 8, CELL_H * 8
    dots = blank(w, h)
    for r in range(8):
        for c in range(8):
            light = (r + c) % 2 == 0
            cell = [[int(light and (c * CELL_W + x + r * CELL_H + y) % 2 == 0) for x in range(CELL_W)]
                    for y in range(CELL_H)]

            def put(img, mask, top):
                for y in range(len(img)):
                    for x in range(CELL_W):
                        if mask[y][x]:
                            cell[top + y][x] = 0
                        if img[y][x]:
                            cell[top + y][x] = 1
            ch = layout[r][c]
            if ch != ".":
                white, black, mask = set_[ch.upper()]
                put(white if ch.isupper() else black, mask, piece_top)
            if (c, r) in marks:
                put(*markers[marks[(c, r)]], 0)
            if (c, r) == cursor_at:
                put(*markers["cursor"], 0)
            for y in range(CELL_H):
                for x in range(CELL_W):
                    dots[r * CELL_H + y][c * CELL_W + x] = cell[y][x]
    image = Image.new("L", (w, h))
    image.putdata([255 if v else 0 for row in dots for v in row])
    image = image.resize((w * SX, h * SY), Image.NEAREST)
    g = image.point(lambda v: 51 if v else 0)
    return Image.merge("RGB", [g, image, g])


def main():
    """Packs every bitmap into src/sprites.bin (the first part of SCHAKEN.GFX,
    padded to whole 128-byte CP/M records) and writes their offsets in that
    block to src/sprites.h; the program loads the block into gfx[]."""
    sheet = Image.open(FONT_SHEET).convert("RGB")
    set_, piece_top, piece_rows = place(pieces())
    shapes = {"cursor": cursor(), "select": selection(), "dot": target_dot(), "capture": capture_corners()}
    markers = {k: (m, grow(m, 2, 1)) for k, m in shapes.items()}
    blob = bytearray()
    defines = []

    def put(name, rows, comment):
        defines.append(f"#define {name:<22} {len(blob):5d}   /* {comment} */")
        blob.extend(b"".join(rows))

    tile = [bytes([0xAA if y % 2 == 0 else 0x55] * (CELL_W // 8)) for y in range(CELL_H + 1)]
    put("GFX_TILE_LIGHT", tile, f"light-square dither, {CELL_H + 1} rows: start a row in on odd lines")
    piece_bytes = CELL_W // 8 * piece_rows
    put("GFX_PIECE_WHITE", [b"".join(rows_bytes(set_[p][0])) for p in PIECES], "White P N B R Q K")
    put("GFX_PIECE_BLACK", [b"".join(rows_bytes(set_[p][1])) for p in PIECES], "Black P N B R Q K")
    put("GFX_PIECE_MASK", [b"".join(rows_bytes(set_[p][2])) for p in PIECES], "halo masks P N B R Q K")
    marks = []
    for name, (img, mask) in markers.items():
        used = [y for y in range(CELL_H) if any(mask[y])]       # only the rows the marker touches
        top, bottom = used[0], used[-1] + 1
        marks += [f"#define MARK_{name.upper()}_TOP  {top}", f"#define MARK_{name.upper()}_ROWS {bottom - top}"]
        put(f"GFX_MARK_{name.upper()}", rows_bytes(img[top:bottom]), f"{name} marker")
        put(f"GFX_MARK_{name.upper()}_MASK", rows_bytes(mask[top:bottom]), f"{name} halo")
    put("GFX_FILE_GLYPHS", [b"".join(label_rows(rom_glyph(sheet, ord(c)), 2, 4)) for c in "abcdefgh"],
        "a-h, 2 bytes x 12 rows, moved right by 4 dots (centred under a square)")
    put("GFX_RANK_GLYPHS", [b"".join(label_rows(rom_glyph(sheet, ord(c)), 1, -1)) for c in "12345678"],
        "1-8, 1 byte x 12 rows, moved left by a dot (clear of the frame)")
    size = len(blob)
    blob.extend(bytes(-len(blob) % 128))
    (ROOT / "src/sprites.bin").write_bytes(blob)
    parts = ["/* Generated by tools/gen_sprites.py -- do not edit. Offsets of the bitmaps",
             " * in gfx[], which holds src/sprites.bin (the first part of SCHAKEN.GFX).",
             " * Bitmaps are one square (6 bytes) wide, row-major, MSB = leftmost dot. */",
             "#ifndef SPRITES_H", "#define SPRITES_H", "",
             f"#define GFX_SIZE    {size}",
             f"#define GFX_RECORDS {len(blob) // 128}                  /* 128-byte records in the file */",
             "",
             f"#define PIECE_TOP   {piece_top}                   /* first piece row inside a square */",
             f"#define PIECE_ROWS  {piece_rows}",
             f"#define PIECE_BYTES {piece_bytes}",
             *marks, "", *defines, "",
             "extern unsigned char gfx[];",
             "#define GFX(offset) (gfx + (offset))",
             "/* colour 0 White, 1 Black; type PAWN..KING */",
             "#define PIECE_IMAGE(black, type) GFX((black) ? GFX_PIECE_BLACK + ((type) - 1) * PIECE_BYTES \\",
             "                                             : GFX_PIECE_WHITE + ((type) - 1) * PIECE_BYTES)",
             "#define PIECE_MASK(type) GFX(GFX_PIECE_MASK + ((type) - 1) * PIECE_BYTES)",
             "#define FILE_GLYPH(i) GFX(GFX_FILE_GLYPHS + (i) * 24)",
             "#define RANK_GLYPH(i) GFX(GFX_RANK_GLYPHS + (i) * 12)",
             "", "#endif"]
    (ROOT / "src/sprites.h").write_text("\n".join(parts) + "\n")
    (ROOT / "build").mkdir(exist_ok=True)
    preview(set_, markers, piece_top).save(ROOT / "build/sprites.png")
    print(f"wrote src/sprites.bin ({size} bytes), src/sprites.h and build/sprites.png")


if __name__ == "__main__":
    main()
