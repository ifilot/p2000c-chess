#!/usr/bin/env python3
"""FEN and QR code check (native build of src/chess.c and src/qr.c).

Random games are played by tools/qrdump.c; every FEN it writes must equal
the one tools/chessmodel.py computes for the same moves, and the QR codes
(of some of those FENs and of texts of every length up to the capacity)
must decode to their text. The decoding needs the zxing-cpp Python module
(`pip install zxing-cpp`); without it only the FENs are checked. The codes
are drawn the way the game shows them: modules of 6 x 4 dots at the CRT's
3:5 dot pitch, light on a lit quiet zone. Run with `make qrtest`.
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chessmodel import Position, parse_square

ROOT = Path(__file__).resolve().parent.parent
QRDUMP = ROOT / "build/qrdump"
MODULE = (6 * 3, 4 * 5)                     # pixels per module: dots times the dot pitch
QUIET = 4

try:
    import zxingcpp
    from PIL import Image
except ImportError:
    zxingcpp = None


def parse(output):
    """(moves, fen, matrix or None) per position."""
    entries, lines, i = [], output.splitlines(), 0
    while i < len(lines):
        moves = lines[i][1:].split()
        fen = lines[i + 1][2:]
        i += 2
        matrix = None
        if i < len(lines) and lines[i] == "Q":
            matrix = lines[i + 1:i + 38]
            i += 38
        entries.append((moves, fen, matrix))
    return entries


def matrix_of(text):
    out = subprocess.run([str(QRDUMP), "text", text], capture_output=True, text=True, check=True).stdout
    return out.splitlines()[1:]


def decode(matrix):
    size = len(matrix) + 2 * QUIET
    image = Image.new("L", (size * MODULE[0], size * MODULE[1]), 255)
    for r, row in enumerate(matrix):
        for c, bit in enumerate(row):
            if bit == "1":
                x, y = (c + QUIET) * MODULE[0], (r + QUIET) * MODULE[1]
                image.paste(0, (x, y, x + MODULE[0], y + MODULE[1]))
    found = zxingcpp.read_barcodes(image)
    return found[0].text if found else None


def main():
    output = subprocess.run([str(QRDUMP), "games", "12", "2024"], capture_output=True, text=True,
                            check=True).stdout
    entries = parse(output)
    errors, codes, longest = 0, 0, 0
    for moves, fen, matrix in entries:
        pos = Position()
        for m in moves:
            pos.play((parse_square(m[:2]), parse_square(m[2:4]), m[4:].upper()))
        longest = max(longest, len(fen))
        if pos.fen() != fen:
            print(f"FEN {fen!r} != model {pos.fen()!r}")
            errors += 1
        if matrix and zxingcpp:
            codes += 1
            if decode(matrix) != fen:
                print(f"QR of {fen!r} decodes to {decode(matrix)!r}")
                errors += 1
    print(f"{len(entries)} positions, longest FEN {longest}, {codes} QR codes decoded")
    if zxingcpp:
        texts = ["x", "8/8/8/8/8/8/8/8 w - - 0 1"]
        texts += ["".join(chr(33 + (i * 7 + n) % 94) for i in range(n)) for n in range(1, 107, 5)]
        texts.append("K" * 106)
        for text in texts:
            if decode(matrix_of(text)) != text:
                print(f"QR of {text!r} decodes to {decode(matrix_of(text))!r}")
                errors += 1
        print(f"{len(texts)} texts of 1 to 106 characters decoded")
    else:
        print("zxing-cpp not installed: QR codes not decoded")
    print("PASS" if not errors else f"FAIL ({errors})")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
