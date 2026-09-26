/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.c -- the board picture.
 *
 * Everything on the board (frame, dithered light squares, pieces, markers,
 * coordinate glyphs) is composed in the 16 KiB framebuffer. A square is
 * built in layers: the background (the dither tile or black), then per
 * piece or marker its halo mask cleared and its image OR-ed in. Each square
 * keeps a record of the appearance last sent; when it changes, the square
 * is recomposed and only the lines that actually differ go over the
 * 19200-baud link, so moving the cursor or marking a target costs a few
 * short ESC r rows instead of the whole square.
 */
#include <string.h>
#include "video.h"
#include "chess.h"
#include "game.h"
#include "screen.h"
#include "sprites.h"

/* Appearance: the piece (type | colour, below 0x20) plus marker flags. */
#define SHOW_CURSOR 0x20
#define SHOW_SELECT 0x40
#define SHOW_TARGET 0x80
#define SHOW_NONE   0xFF                    /* never a real appearance: forces a redraw */

static unsigned char shown[64];             /* appearance last composed, per 0..63 square */
static const unsigned char tile_dark[CELL_BYTES * CELL_H];     /* all zero */
static unsigned char previous[CELL_BYTES * CELL_H];             /* a square before recomposing */

/* Screen column and row (0 = top) of a 0x88 square. */
static unsigned char screen_col(unsigned char sq)
{
    return flipped ? 7 - FILE_OF(sq) : FILE_OF(sq);
}

static unsigned char screen_row(unsigned char sq)
{
    return flipped ? RANK_OF(sq) : 7 - RANK_OF(sq);
}

static unsigned char cell_line(unsigned char sq)
{
    return BOARD_TOP + screen_row(sq) * CELL_H;
}

static unsigned char cell_byte(unsigned char sq)
{
    return LEFT_BYTE + screen_col(sq) * CELL_BYTES;
}

static unsigned char appearance(unsigned char sq)
{
    unsigned char look = board[sq], m = marks[SQ64(sq)];
    if (m & MARK_TARGET)
        look |= SHOW_TARGET;
    if (m & MARK_SELECT)
        look |= SHOW_SELECT;
    if (sq == cursor)
        look |= SHOW_CURSOR;
    return look;
}

static void layer(const unsigned char *image, const unsigned char *mask, unsigned int offset,
                  unsigned char rows)
{
    video_mask(mask, offset, WH(CELL_BYTES, rows));
    video_blit(image, offset, WH(CELL_BYTES, rows));
}

/* Composes one square in the framebuffer for its current appearance. */
static void compose(unsigned char sq, unsigned char look)
{
    unsigned char line = cell_line(sq), piece = look & 0x1F;
    unsigned int offset = line * FB_LINE + cell_byte(sq);

    if ((FILE_OF(sq) + RANK_OF(sq)) & 1)    /* a1 is dark */
        video_copy(GFX(GFX_TILE_LIGHT) + (line & 1) * CELL_BYTES, offset, WH(CELL_BYTES, CELL_H));
    else
        video_copy(tile_dark, offset, WH(CELL_BYTES, CELL_H));
    if (piece)
        layer(PIECE_IMAGE(COLOUR(piece) == BLACK, TYPE(piece)), PIECE_MASK(TYPE(piece)),
              offset + PIECE_TOP * FB_LINE, PIECE_ROWS);
    if (look & SHOW_TARGET) {
        if (piece)
            layer(GFX(GFX_MARK_CAPTURE), GFX(GFX_MARK_CAPTURE_MASK), offset + MARK_CAPTURE_TOP * FB_LINE, MARK_CAPTURE_ROWS);
        else
            layer(GFX(GFX_MARK_DOT), GFX(GFX_MARK_DOT_MASK), offset + MARK_DOT_TOP * FB_LINE, MARK_DOT_ROWS);
    }
    if (look & SHOW_SELECT)
        layer(GFX(GFX_MARK_SELECT), GFX(GFX_MARK_SELECT_MASK), offset + MARK_SELECT_TOP * FB_LINE, MARK_SELECT_ROWS);
    if (look & SHOW_CURSOR)
        layer(GFX(GFX_MARK_CURSOR), GFX(GFX_MARK_CURSOR_MASK), offset + MARK_CURSOR_TOP * FB_LINE, MARK_CURSOR_ROWS);
    shown[SQ64(sq)] = look;
}

/* Recomposes a square and sends the runs of lines that changed. */
static void update(unsigned char sq, unsigned char look)
{
    unsigned char line = cell_line(sq), col = cell_byte(sq), row, first;
    unsigned int offset = line * FB_LINE + col;
    const unsigned char *fb, *old;

    for (row = 0; row < CELL_H; row++)
        memcpy(previous + row * CELL_BYTES, framebuffer + offset + row * FB_LINE, CELL_BYTES);
    compose(sq, look);
    row = 0;
    while (row < CELL_H) {
        fb = framebuffer + offset + row * FB_LINE;
        old = previous + row * CELL_BYTES;
        if (!memcmp(fb, old, CELL_BYTES)) {
            row++;
            continue;
        }
        first = row;
        do {
            row++;
            fb += FB_LINE;
            old += CELL_BYTES;
        } while (row < CELL_H && memcmp(fb, old, CELL_BYTES));
        video_flush_rect(COLROW(col, line + first), WH(CELL_BYTES, row - first));
    }
}

void sync_cells(void)
{
    unsigned char i, sq, look;
    for (i = 0; i < 64; i++) {
        sq = SQ88(i);
        look = appearance(sq);
        if (look != shown[i])
            update(sq, look);
    }
}

/* One-dot frame round the board, touching the squares. */
static void draw_frame(void)
{
    unsigned int top = (BOARD_TOP - 1) * FB_LINE, bottom = (BOARD_TOP + BOARD_LINES) * FB_LINE;
    video_fill(top + LEFT_BYTE, 0xFF, 8 * CELL_BYTES);
    video_fill(bottom + LEFT_BYTE, 0xFF, 8 * CELL_BYTES);
    video_or_col(top, 0x01, BOARD_LINES + 2);                               /* x = 7 */
    video_or_col(top + LEFT_BYTE + 8 * CELL_BYTES, 0x80, BOARD_LINES + 2);  /* x = 392 */
}

/* Rank digits left of the frame, file letters below; turned with the board. */
static void draw_labels(void)
{
    unsigned char i;
    for (i = 0; i < 8; i++) {
        video_blit(RANK_GLYPH(flipped ? i : 7 - i),
                   (BOARD_TOP + i * CELL_H + 10) * FB_LINE, WH(1, 12));
        video_blit(FILE_GLYPH(flipped ? 7 - i : i),
                   LABEL_LINE * FB_LINE + LEFT_BYTE + 2 + i * CELL_BYTES, WH(2, 12));
    }
}

void draw_board(void)
{
    unsigned char i, sq;
    video_clear();
    draw_frame();
    draw_labels();
    for (i = 0; i < 64; i++) {
        sq = SQ88(i);
        compose(sq, appearance(sq));
    }
}

/* Uploads only the lit parts of the framebuffer: one ESC r per run of
 * non-zero bytes in a line (short gaps are bridged, a header costs 7 bytes). */
void flush_sparse(void)
{
    unsigned char line, first, last, x;
    const unsigned char *row;
    for (line = 0; line < FB_LINES; line++) {
        row = framebuffer + line * FB_LINE;
        x = 0;
        while (x < FB_LINE) {
            while (x < FB_LINE && row[x] == 0)
                x++;
            if (x == FB_LINE)
                break;
            first = last = x;
            while (x < FB_LINE) {                /* extend over gaps shorter than a header */
                if (row[x] != 0)
                    last = x;
                else if (x - last >= 7)
                    break;
                x++;
            }
            video_flush_rect(COLROW(first, line), WH(last - first + 1, 1));
        }
    }
}

void flush_frame(void)
{
    flush_sparse();
}
