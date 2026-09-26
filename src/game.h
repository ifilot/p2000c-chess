/* SPDX-License-Identifier: GPL-3.0-only */
/* game.h -- game state shared by the modules, and the game flow. */
#ifndef GAME_H
#define GAME_H

/* Square markers (marks[], indexed 0..63 = a1..h8) */
#define MARK_TARGET 0x01                    /* the previewed piece can go here */
#define MARK_SELECT 0x02                    /* the piece picked up */

extern unsigned char human;                 /* WHITE or BLACK: the player's colour */
extern unsigned char flipped;               /* Black at the bottom of the screen */
extern unsigned char cursor;                /* 0x88 square, or NO_SQ when hidden */
extern unsigned char marks[64];
extern unsigned char game_over;              /* 0 while playing, else OVER_* */

#define OVER_MATE      1
#define OVER_STALEMATE 2
#define OVER_FIFTY     3                    /* fifty moves without capture or pawn move */
#define OVER_REPEAT    4                    /* threefold repetition */
#define OVER_MATERIAL  5                    /* neither side can mate */
#define OVER_LENGTH    6                    /* the game record is full */
extern unsigned char demo;                  /* both colours played by the computer */

/* One game against the computer at cpu_level; returns 1 to go back to the
 * start screen (N), 0 to leave the program (Q confirmed). */
extern unsigned char play(void);

/* A demo game between two computer players; any key ends it. */
extern void demo_game(void);

/* Restores the game screen after a text-mode interlude (help, screen saver). */
extern void redraw_game_screen(void);

#endif
