/* SPDX-License-Identifier: GPL-3.0-only */
/* cpu.h -- the computer player. */
#ifndef CPU_H
#define CPU_H

#include "chess.h"

extern unsigned char cpu_level;             /* 1-3 */
extern unsigned int cpu_seed;               /* random state, stirred by the game */
extern unsigned int cpu_nodes;              /* nodes searched for the last move */
extern unsigned char cpu_depth;             /* last completed search depth */

/* Chooses a move for `side`, which must have a legal move. */
extern void cpu_choose(move_t *best);

/* Longest game: the rest of hist[] is the search's. */
#define GAME_MAX (MAX_HIST - 48)           /* 300 plies; a longer game is drawn */

extern unsigned char random_byte(void);

#endif
