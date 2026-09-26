// SPDX-License-Identifier: GPL-3.0-only
// board.js -- the board picture of Schaken, composed dot for dot as
// src/screen.c does it on the P2000C: a 512x252-dot framebuffer of 64-byte
// lines (MSB = leftmost dot), a one-dot frame, rank digits and file letters
// from the character ROM, the dithered light squares, and per piece its halo
// mask cleared and its bitmap OR-ed in. The bitmaps are the game's own
// src/sprites.bin, with the offsets from src/sprites.h (tools/gen_site.py
// puts both in sprites.js).

export const FB_LINE = 64;                  // bytes per framebuffer line
export const FB_LINES = 252;
export const DOT_PITCH = [3, 5];            // the CRT's dots are 3 wide by 5 high

const BOARD_TOP = 2;                        // first line of the top rank
const CELL_H = 29;
const CELL_BYTES = 6;
const LEFT_BYTE = 1;
const BOARD_LINES = 8 * CELL_H;
const LABEL_LINE = BOARD_TOP + BOARD_LINES + 3;
export const BOARD_BYTES = LEFT_BYTE + 8 * CELL_BYTES + 1;   // what the picture spans: labels to frame

const TYPES = "pnbrqk";                     // type order 1..6 as in chess.h

function blit(fb, gfx, src, offset, width, rows) {
  for (let r = 0; r < rows; r++)
    for (let b = 0; b < width; b++)
      fb[offset + r * FB_LINE + b] |= gfx[src + r * width + b];
}

function mask(fb, gfx, src, offset, width, rows) {
  for (let r = 0; r < rows; r++)
    for (let b = 0; b < width; b++)
      fb[offset + r * FB_LINE + b] &= ~gfx[src + r * width + b];
}

function copy(fb, gfx, src, offset, width, rows) {
  for (let r = 0; r < rows; r++)
    for (let b = 0; b < width; b++)
      fb[offset + r * FB_LINE + b] = gfx[src + r * width + b];
}

// The framebuffer for a position: squares[rank * 8 + file] (a1 = 0) holds a
// FEN piece letter or "". Turned, Black is at the bottom.
export function composeBoard(gfx, g, squares, turned) {
  const fb = new Uint8Array(FB_LINE * FB_LINES);

  // one-dot frame round the board, touching the squares
  const top = (BOARD_TOP - 1) * FB_LINE, bottom = (BOARD_TOP + BOARD_LINES) * FB_LINE;
  fb.fill(0xFF, top + LEFT_BYTE, top + LEFT_BYTE + 8 * CELL_BYTES);
  fb.fill(0xFF, bottom + LEFT_BYTE, bottom + LEFT_BYTE + 8 * CELL_BYTES);
  for (let line = 0; line < BOARD_LINES + 2; line++) {
    fb[top + line * FB_LINE] |= 0x01;
    fb[top + line * FB_LINE + LEFT_BYTE + 8 * CELL_BYTES] |= 0x80;
  }

  // rank digits left of the frame, file letters below
  for (let i = 0; i < 8; i++) {
    blit(fb, gfx, g.GFX_RANK_GLYPHS + (turned ? i : 7 - i) * 12,
         (BOARD_TOP + i * CELL_H + 10) * FB_LINE, 1, 12);
    blit(fb, gfx, g.GFX_FILE_GLYPHS + (turned ? 7 - i : i) * 24,
         LABEL_LINE * FB_LINE + LEFT_BYTE + 2 + i * CELL_BYTES, 2, 12);
  }

  for (let rank = 0; rank < 8; rank++)
    for (let file = 0; file < 8; file++) {
      const row = turned ? rank : 7 - rank, col = turned ? 7 - file : file;
      const line = BOARD_TOP + row * CELL_H;
      const offset = line * FB_LINE + LEFT_BYTE + col * CELL_BYTES;
      if ((file + rank) & 1)                // a1 is dark; the tile starts a row in on odd lines
        copy(fb, gfx, g.GFX_TILE_LIGHT + (line & 1) * CELL_BYTES, offset, CELL_BYTES, CELL_H);
      const piece = squares[rank * 8 + file];
      if (!piece)
        continue;
      const type = TYPES.indexOf(piece.toLowerCase());
      const black = piece === piece.toLowerCase();
      const at = offset + g.PIECE_TOP * FB_LINE;
      mask(fb, gfx, g.GFX_PIECE_MASK + type * g.PIECE_BYTES, at, CELL_BYTES, g.PIECE_ROWS);
      blit(fb, gfx, (black ? g.GFX_PIECE_BLACK : g.GFX_PIECE_WHITE) + type * g.PIECE_BYTES,
           at, CELL_BYTES, g.PIECE_ROWS);
    }
  return fb;
}

// Is the dot at (x, line) lit?
export function lit(fb, x, line) {
  return (fb[line * FB_LINE + (x >> 3)] & (0x80 >> (x & 7))) !== 0;
}
