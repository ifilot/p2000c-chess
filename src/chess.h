/* SPDX-License-Identifier: GPL-3.0-only */
/* chess.h -- the rules of chess on a 0x88 board.
 *
 * Squares are 0x88 indices (rank * 16 + file, a1 = 0x00, h8 = 0x77); an
 * index with a bit of 0x88 set lies off the board, which makes edge tests
 * a single AND. A piece is a type (1-6) OR-ed with its colour bit, so
 * `board[sq] & side` tests for an own piece. On the P2000C the move
 * generator, the attack test and making and taking back moves are Z80
 * assembly (rules.asm); chess.c has the same in C for the native test
 * tools (tools/perft.c, tools/selfplay.c).
 */
#ifndef CHESS_H
#define CHESS_H

#define EMPTY  0
#define PAWN   1
#define KNIGHT 2
#define BISHOP 3
#define ROOK   4
#define QUEEN  5
#define KING   6

#define WHITE  0x08
#define BLACK  0x10
#define TYPE(p)    ((p) & 7)
#define COLOUR(p)  ((p) & 0x18)
#define OTHER(s)   ((s) ^ 0x18)
#define SIDE_INDEX(s) ((s) >> 4)            /* WHITE 0, BLACK 1 */

#define SQ(file, rank) ((unsigned char)(((rank) << 4) | (file)))
#define FILE_OF(sq)    ((sq) & 7)
#define RANK_OF(sq)    ((sq) >> 4)
#define OFFBOARD(sq)   ((sq) & 0x88)
#define NO_SQ          0xFF
#define SQ64(sq)       ((unsigned char)(((sq) + ((sq) & 7)) >> 1))   /* 0x88 -> 0..63 */
#define SQ88(i)        ((unsigned char)((i) + ((i) & 0x38)))         /* 0..63 -> 0x88 */

#define CASTLE_WK 1
#define CASTLE_WQ 2
#define CASTLE_BK 4
#define CASTLE_BQ 8

/* Move flags; the low three bits hold the promotion piece type. */
#define MF_PROMO   0x07
#define MF_EP      0x08
#define MF_CASTLE  0x10
#define MF_DOUBLE  0x20
#define MF_CAPTURE 0x40

/* key orders the search: captures 100 + 8 * victim - attacker, a queen
 * promotion 95 more, other moves 0 (the search adds its killer moves). */
typedef struct {
    unsigned char from, to, flags, key;
} move_t;

/* What make_move() needs to take a move back (13 bytes; rules.asm knows
 * the layout). */
typedef struct {
    move_t move;
    unsigned char captured, castle, ep, halfmove, phase;
    int score;
    unsigned int hash;                      /* of the position before the move */
} undo_t;

#define MOVE_STACK 900                      /* generated moves for every ply of a search */
#define MAX_HIST   348                      /* game plies plus search plies */

extern unsigned char board[128];              /* page aligned in rules.asm */
extern unsigned char castle_keep[128];        /* rights kept by a move from or to a square */
extern unsigned char side;                  /* WHITE or BLACK to move */
extern unsigned char castle;                /* CASTLE_* rights */
extern unsigned char ep;                    /* en-passant target square or NO_SQ */
extern unsigned char halfmove;              /* plies since a capture or pawn move */
extern unsigned char king_sq[2];
extern unsigned char phase;                 /* non-pawn material: N,B 1, R 2, Q 4 */
extern int score;                           /* material + piece squares, White's view */
extern unsigned int hash;                   /* Zobrist key of the position */
extern unsigned int hist_len;               /* entries in hist */
extern undo_t hist[MAX_HIST];
extern move_t moves[MOVE_STACK];

extern const int piece_value[7];

/* Standard starting position. */
extern void chess_init(void);
/* Recomputes the hash after setting up a position directly (tests). */
extern void chess_rehash(void);

/* Pseudo-legal moves for `side` appended from index start; returns the new
 * end. With captures_only, only captures and queen promotions. */
extern unsigned int gen_moves(unsigned int start, unsigned char captures_only);

/* Nonzero when `by` attacks square sq. */
#ifdef __SDCC
extern unsigned char attacked_fc(unsigned int sq_by) __z88dk_fastcall;
#define attacked(sq, by) attacked_fc((unsigned int)(sq) | ((unsigned int)(by) << 8))
#else
extern unsigned char attacked(unsigned char sq, unsigned char by);
#endif
#define in_check(s) attacked(king_sq[SIDE_INDEX(s)], OTHER(s))

/* Plays a pseudo-legal move and records it; returns 0 (and takes it back)
 * if it leaves the mover's king in check. The (costly) test is skipped when
 * it cannot fail: the mover was not in check (mover_in_check, which the
 * caller must keep true unless it knows better), the piece is not the king,
 * the move not en passant, and the piece stood on no line through its king. */
extern unsigned char mover_in_check;
#ifdef __SDCC
extern unsigned char make_move(const move_t *m) __z88dk_fastcall;
#else
extern unsigned char make_move(const move_t *m);
#endif
extern void unmake_move(void);

/* Legal moves for `side` from index start; returns the new end. */
extern unsigned int gen_legal(unsigned int start);

/* How often the current position occurred before, with the same side to
 * move, since the last capture or pawn move (hashes in hist[]). */
extern unsigned char repetitions(void);

/* Neither side can mate: K v K, K+minor v K, K+B v K+B on one colour. */
extern unsigned char insufficient_material(void);

#endif
