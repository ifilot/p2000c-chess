/* SPDX-License-Identifier: GPL-3.0-only */
/* cpu.c -- the computer player.
 *
 * A negamax alpha-beta search over iterative deepening, with a capture-only
 * quiescence search at the leaves (so exchanges are played out before a
 * position is judged), check extensions, MVV-LVA capture ordering and two
 * killer moves per ply. A position that repeats one of the game or of the
 * line being searched counts as a draw. The evaluation is the incremental material and
 * piece-square score from chess.c plus king placement (sheltered in the
 * middle game, central in the end game) and, when one side is far ahead in
 * the end game, a mop-up term that drives the lone king to the edge.
 *
 * Every level plays from a small opening book first. Level 1 looks one move
 * ahead (plus exchanges) and picks at random among the moves within 0.4
 * pawn of the best; level 2 looks two moves ahead; level 3 deepens while it
 * is in the first third of a 10-second budget (60 Hz BIOS clock), stopping
 * at the budget. Levels 2 and 3 choose at random among moves within a few
 * points of the best, so games vary. Without a clock a node budget stands
 * in for the time.
 */
#include "chess.h"
#include "cpu.h"
#include "clock.h"
#include "saver.h"

unsigned char cpu_level = 2;
unsigned int cpu_seed = 0x1234;
unsigned int cpu_nodes;
unsigned char cpu_depth;

#define INF      32000
#define MATE     31000
#define MAX_PLY  40
#define STACK_GUARD (MOVE_STACK - 220)      /* room for one more full move list */

static unsigned char ply;                   /* distance from the root */
static unsigned int sp;                     /* first free entry in moves[] */
static unsigned char stop;                  /* time or node budget exhausted */
static unsigned int start_ticks, budget_ticks, node_budget;
static move_t killer[MAX_PLY][2];

unsigned char random_byte(void)
{
    /* 16-bit xorshift (7, 9, 8) */
    cpu_seed ^= cpu_seed << 7;
    cpu_seed ^= cpu_seed >> 9;
    cpu_seed ^= cpu_seed << 8;
    return (unsigned char)cpu_seed;
}

/* --- evaluation ---------------------------------------------------------------- */

static const signed char KING_MID[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20,
};

static const signed char KING_END[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50,
};

#define ENDGAME_PHASE 8

static unsigned char centre_distance(unsigned char sq)
{
    unsigned char f = FILE_OF(sq), r = RANK_OF(sq);
    return (f < 4 ? 3 - f : f - 4) + (r < 4 ? 3 - r : r - 4);
}

static unsigned char distance(unsigned char a, unsigned char b)
{
    unsigned char df = FILE_OF(a) > FILE_OF(b) ? FILE_OF(a) - FILE_OF(b) : FILE_OF(b) - FILE_OF(a);
    unsigned char dr = RANK_OF(a) > RANK_OF(b) ? RANK_OF(a) - RANK_OF(b) : RANK_OF(b) - RANK_OF(a);
    return df + dr;
}

/* Score from the side to move's point of view. */
static int evaluate(void)
{
    int s = score;
    unsigned char wk = SQ64(king_sq[0]), bk = SQ64(king_sq[1]);
    if (phase > ENDGAME_PHASE)
        s += KING_MID[wk ^ 56] - KING_MID[bk];
    else {
        s += KING_END[wk ^ 56] - KING_END[bk];
        /* mop-up: the stronger side drives the other king to the edge */
        if (s > 250)
            s += 10 * centre_distance(king_sq[1]) + 4 * (14 - distance(king_sq[0], king_sq[1]));
        else if (s < -250)
            s -= 10 * centre_distance(king_sq[0]) + 4 * (14 - distance(king_sq[0], king_sq[1]));
    }
    return side == WHITE ? s : -s;
}

/* --- move ordering -------------------------------------------------------------- */

static unsigned char same(const move_t *a, const move_t *b)
{
    return a->from == b->from && a->to == b->to && a->flags == b->flags;
}

/* Once the captures are through, the killers of this ply (quiet moves that
 * refuted a sibling) go to the front of the rest, moves[i..end). */
static void killers_first(unsigned int i, unsigned int end)
{
    move_t *m, *first = &moves[i], *last = &moves[end], t;
    unsigned char n, from, to;
    for (n = 0; n < 2; n++) {
        from = killer[ply][n].from;
        if (from == NO_SQ)
            break;
        to = killer[ply][n].to;
        for (m = first; m != last; m++)
            if (m->to == to && m->from == from) {
                t = *first;
                *first = *m;
                *m = t;
                first++;
                break;
            }
    }
}

/* Moves the best remaining move to index i. */
static void pick(unsigned int i, unsigned int end)
{
    move_t *m = &moves[i], *best = m, *p = m + 1, t;
    unsigned char k = m->key;
    for (i++; i < end; i++, p++)
        if (p->key > k) {
            k = p->key;
            best = p;
        }
    if (best != m) {
        t = *m;
        *m = *best;
        *best = t;
    }
}

/* --- search ----------------------------------------------------------------------- */

static void count_node(void)
{
    cpu_nodes++;
    if ((cpu_nodes & 127) == 0) {
        key_watch();                        /* keys that come in while thinking (saver.c) */
        if (clock_available) {
            if ((unsigned int)(clock_ticks() - start_ticks) >= budget_ticks)
                stop = 1;
        } else if (cpu_nodes >= node_budget)
            stop = 1;
    }
}

static int quiesce(int alpha, int beta)
{
    int stand, s;
    unsigned int i, start = sp, end;
    unsigned char victim;

    count_node();
    stand = evaluate();
    if (stand >= beta)
        return stand;
    if (stand > alpha)
        alpha = stand;
    if (ply >= MAX_PLY - 1 || sp > STACK_GUARD)
        return alpha;
    end = gen_moves(start, 1);
    sp = end;
    for (i = start; i < end; i++) {
        pick(i, end);
        /* delta pruning: even winning the victim cannot lift the score to alpha */
        victim = (moves[i].flags & MF_EP) ? PAWN : TYPE(board[moves[i].to]);
        if (!(moves[i].flags & MF_PROMO) && stand + piece_value[victim] + 200 <= alpha)
            continue;
        mover_in_check = 1;                 /* not known here: test every capture */
        if (!make_move(&moves[i]))
            continue;
        ply++;
        s = -quiesce(-beta, -alpha);
        ply--;
        unmake_move();
        if (stop)
            break;
        if (s > alpha) {
            alpha = s;
            if (s >= beta)
                break;
        }
    }
    sp = start;
    return alpha;
}

static int search(unsigned char depth, int alpha, int beta)
{
    int s;
    unsigned int i, start = sp, end;
    unsigned char legal = 0, check, sorted;

    if (halfmove >= 100 || repetitions())
        return 0;                           /* draw by rule, or heading for one */
    check = in_check(side);
    if (check && ply < 12)
        depth++;                            /* look further when in check */
    if (depth == 0)
        return quiesce(alpha, beta);
    count_node();
    if (ply >= MAX_PLY - 1 || sp > STACK_GUARD)
        return evaluate();
    end = gen_moves(start, 0);
    sp = end;
    sorted = 0;
    for (i = start; i < end; i++) {
        if (!sorted) {
            pick(i, end);
            if (!moves[i].key) {            /* only quiet moves left: killers, then as generated */
                sorted = 1;
                killers_first(i, end);
            }
        }
        mover_in_check = check;
        if (!make_move(&moves[i]))
            continue;
        legal++;
        ply++;
        s = -search(depth - 1, -beta, -alpha);
        ply--;
        unmake_move();
        if (stop)
            break;
        if (s > alpha) {
            alpha = s;
            if (s >= beta) {
                if (!(moves[i].flags & MF_CAPTURE) && !same(&moves[i], &killer[ply][0])) {
                    killer[ply][1] = killer[ply][0];
                    killer[ply][0] = moves[i];
                }
                break;
            }
        }
    }
    sp = start;
    if (!legal && !stop)
        return check ? -MATE + ply : 0;
    return alpha;
}

/* --- opening book ------------------------------------------------------------------ */

/* Main lines in coordinate notation; the book follows any line whose start
 * matches the game so far, choosing at random among the continuations. */
static const char *const BOOK[] = {
    "e2e4e7e5g1f3b8c6f1b5a7a6b5a4g8f6e1g1f8e7",
    "e2e4e7e5g1f3b8c6f1c4f8c5c2c3g8f6d2d3d7d6",
    "e2e4e7e5g1f3b8c6f1c4g8f6d2d3f8e7e1g1e8g8",
    "e2e4e7e5g1f3b8c6d2d4e5d4f3d4g8f6b1c3f8b4",
    "e2e4e7e5g1f3g8f6f3e5d7d6e5f3f6e4d2d4d6d5",
    "e2e4e7e5b1c3g8f6g1f3b8c6f1b5f8b4",
    "e2e4c7c5g1f3d7d6d2d4c5d4f3d4g8f6b1c3a7a6",
    "e2e4c7c5g1f3b8c6d2d4c5d4f3d4g8f6b1c3e7e5",
    "e2e4c7c5g1f3e7e6d2d4c5d4f3d4b8c6b1c3d8c7",
    "e2e4c7c5b1c3b8c6g2g3g7g6f1g2f8g7d2d3d7d6",
    "e2e4e7e6d2d4d7d5b1c3g8f6c1g5f8e7e4e5f6d7",
    "e2e4e7e6d2d4d7d5b1d2g8f6e4e5f6d7f1d3c7c5",
    "e2e4c7c6d2d4d7d5b1c3d5e4c3e4c8f5e4g3f5g6",
    "e2e4d7d5e4d5d8d5b1c3d5a5d2d4g8f6g1f3c8f5",
    "d2d4d7d5c2c4e7e6b1c3g8f6c1g5f8e7e2e3e8g8",
    "d2d4d7d5c2c4c7c6g1f3g8f6b1c3d5c4a2a4c8f5",
    "d2d4g8f6c2c4e7e6b1c3f8b4e2e3e8g8f1d3d7d5",
    "d2d4g8f6c2c4e7e6g1f3b7b6g2g3c8b7f1g2f8e7",
    "d2d4g8f6c2c4g7g6b1c3f8g7e2e4d7d6g1f3e8g8",
    "d2d4d7d5g1f3g8f6c1f4e7e6e2e3c7c5c2c3b8c6",
    "c2c4e7e5b1c3g8f6g1f3b8c6g2g3d7d5c4d5f6d5",
    "c2c4g8f6b1c3e7e6e2e4d7d5e4e5d5d4",
    "g1f3d7d5d2d4g8f6c2c4e7e6b1c3f8e7c1f4e8g8",
};

#define BOOK_LINES (sizeof BOOK / sizeof BOOK[0])

static unsigned char book_square(const char *s)
{
    return SQ(s[0] - 'a', s[1] - '1');
}

/* A book move for the current game (hist[0..hist_len)), or 0. */
static unsigned char book_move(unsigned int legal, move_t *best)
{
    unsigned char line, found = 0, pick_n = 0;
    unsigned int k, i;
    const char *p;
    move_t choice[BOOK_LINES];

    for (line = 0; line < BOOK_LINES; line++) {
        p = BOOK[line];
        for (k = 0; k < hist_len; k++, p += 4) {
            if (!*p || book_square(p) != hist[k].move.from || book_square(p + 2) != hist[k].move.to)
                break;
        }
        if (k < hist_len || !*p)
            continue;
        for (i = 0; i < legal; i++)
            if (moves[i].from == book_square(p) && moves[i].to == book_square(p + 2)) {
                choice[found++] = moves[i];
                break;
            }
    }
    if (!found)
        return 0;
    pick_n = random_byte() % found;
    *best = choice[pick_n];
    return 1;
}

/* --- root ------------------------------------------------------------------------ */

void cpu_choose(move_t *best)
{
    static int root_score[220];
    unsigned int n, i, j;
    unsigned char depth, max_depth, margin;
    int s, alpha, best_score, t_score;
    move_t t, chosen;

    n = gen_legal(0);
    *best = moves[0];
    cpu_nodes = 0;
    cpu_depth = 0;
    if (n == 1)
        return;
    if (book_move(n, best))
        return;

    switch (cpu_level) {
    case 1:  max_depth = 1; margin = 40; budget_ticks = 600; node_budget = 2000; break;
    case 2:  max_depth = 2; margin = 8;  budget_ticks = 300; node_budget = 1500; break;
    default: max_depth = 8; margin = 4;  budget_ticks = 560; node_budget = 4000; break;
    }
    start_ticks = clock_ticks();
    stop = 0;
    ply = 0;
    sp = n;
    for (i = 0; i < MAX_PLY; i++)
        killer[i][0].from = killer[i][1].from = NO_SQ;
    for (i = 0; i < n; i++)
        root_score[i] = moves[i].key;
    chosen = moves[0];

    for (depth = 1; depth <= max_depth; depth++) {
        /* best first: order by the previous iteration's scores */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && root_score[j] > root_score[j - 1]; j--) {
                t = moves[j]; moves[j] = moves[j - 1]; moves[j - 1] = t;
                t_score = root_score[j]; root_score[j] = root_score[j - 1]; root_score[j - 1] = t_score;
            }
        best_score = -INF;
        for (i = 0; i < n; i++) {
            make_move(&moves[i]);           /* legal: gen_legal() filtered */
            if (repetitions())
                s = 0;                      /* a repetition heads for a draw */
            else {
                ply = 1;
                alpha = best_score > -INF + margin ? best_score - margin : -INF;
                s = -search(depth - 1, -INF, -alpha);
                ply = 0;
            }
            unmake_move();
            if (stop)
                break;
            root_score[i] = s;
            if (s > best_score)
                best_score = s;
        }
        if (stop && i == 0)
            break;                          /* nothing new: keep the last iteration */
        /* random choice among the moves within the margin of the best
         * (those searched in an interrupted iteration count too) */
        for (j = 0, s = 0; j < (stop ? i : n); j++)
            if (root_score[j] > best_score - margin)
                s++;
        s = random_byte() % s;
        for (j = 0; ; j++)
            if (root_score[j] > best_score - margin && s-- == 0)
                break;
        chosen = moves[j];
        cpu_depth = depth;
        if (stop || best_score > MATE - 100 || best_score < -MATE + 100)
            break;
        /* an iteration costs several times the previous one: start one
         * only in the first third of the budget (an unfinished one still
         * counts, its first move being the previous best) */
        if (clock_available && (unsigned int)(clock_ticks() - start_ticks) > budget_ticks / 3)
            break;
    }
    mover_in_check = 1;                     /* the default for the game's callers */
    *best = chosen;
}
