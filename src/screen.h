/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.h -- the board picture: composition in the framebuffer and uploads.
 *
 * Geometry in dots: 48x29-dot squares (square on the CRT's 3:5 dot pitch),
 * the board's left edge on byte 1 (x = 8), a one-dot frame round it, rank
 * digits in byte 0 and file letters below. The text panel starts right of
 * the frame at text column 50. */
#ifndef SCREEN_H
#define SCREEN_H

#define BOARD_TOP   2                       /* first line of rank 8 (or 1 when turned) */
#define CELL_H      29
#define CELL_BYTES  6
#define LEFT_BYTE   1
#define BOARD_LINES (8 * CELL_H)            /* 232 */
#define LABEL_LINE  (BOARD_TOP + BOARD_LINES + 3)

/* Composes the whole picture (frame, labels, every square) in RAM. */
extern void draw_board(void);

/* Recomposes and re-sends every square whose appearance changed. */
extern void sync_cells(void);

/* Sends the composed picture after ESC 3 (the lit runs of every line). */
extern void flush_frame(void);

/* Sends only the lit runs of every framebuffer line (also the title picture). */
extern void flush_sparse(void);

#endif
