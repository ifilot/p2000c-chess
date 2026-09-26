/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.h -- the status panel on the 64x21 text plane, right of the board
 * (text columns 50-63). */
#ifndef PANEL_H
#define PANEL_H

#define PANEL_COL   50
#define PANEL_WIDTH 14
#define ROW_LEVEL   5
#define ROW_MOVE    6
#define ROW_CLOCK   7
#define ROW_STATUS  9
#define ROW_NOTE    10
#define ROW_LIST    12                      /* the last four moves of each side */
#define LIST_LINES  8
#define ROW_KEYS    20                      /* bottom row: never written to its last column */

/* Per game ply: the piece that moved and whether it gave check or mate
 * (kept by game.c, read to write the move list). */
#define NOTE_CHECK 0x40
#define NOTE_MATE  0x80
extern unsigned char ply_note[];

extern const char *name_of(unsigned char colour);       /* "Wit" / "Zwart" */
extern void draw_panel(void);                          /* static texts, then the variable ones */
extern void show_move_number(void);
extern void show_clock(void);                          /* elapsed game time, if a clock exists */
extern void show_moves(void);                          /* the move list */
extern void show_note(const char *note);               /* note row, remembered */
extern void restore_note(void);                        /* after the help screen */
extern void show_status(const char *status);           /* status row, written last */
extern void show_thinking(void);                       /* "Zwart denkt..." */
extern void announce_turn(void);                       /* "Wit aan zet" */
extern void announce_result(void);                     /* "Schaakmat!" / "Remise" */

#endif
