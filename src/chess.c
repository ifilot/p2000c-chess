/* SPDX-License-Identifier: GPL-3.0-only */
/* chess.c -- move generation, making and taking back moves, attack tests.
 *
 * Moves are generated pseudo-legally into one shared stack (moves[]), each
 * ply of the search appending above the previous one (on the P2000C by
 * rules.asm; the C generator below serves the native test tools); make_move() rejects a
 * move that leaves the own king in check. The material and piece-square
 * score is kept up to date incrementally, so the evaluation at the leaves
 * of the search costs next to nothing. Everything make_move() changes is
 * recorded in hist[], which doubles as the game record for take-backs.
 */
#include "chess.h"

unsigned char side;
unsigned char castle;
unsigned char ep;
unsigned char halfmove;
unsigned char king_sq[2];
unsigned char phase;
int score;
unsigned int hash;
unsigned char mover_in_check = 1;
unsigned int hist_len;

const int piece_value[7] = { 0, 100, 320, 330, 500, 900, 0 };
static const unsigned char PHASE[7] = { 0, 0, 1, 1, 2, 4, 0 };

static const signed char KNIGHT_D[8] = { 33, 31, 18, 14, -14, -18, -31, -33 };
static const signed char KING_D[8]   = { 17, 16, 15, 1, -1, -15, -16, -17 };
/* Bishop directions 0-3, rook directions 4-7, queen all eight. */
static const signed char SLIDE_D[8]  = { 17, 15, -15, -17, 16, 1, -1, -16 };

/* Piece-square tables from White's side, a8 first (as a board is printed);
 * Black reads them mirrored. Kings are scored by the search (cpu.c). */
static const signed char PST[5][64] = {
    {   0,  0,  0,  0,  0,  0,  0,  0,          /* pawn */
       50, 50, 50, 50, 50, 50, 50, 50,
       10, 10, 20, 30, 30, 20, 10, 10,
        5,  5, 10, 25, 25, 10,  5,  5,
        0,  0,  0, 20, 20,  0,  0,  0,
        5, -5,-10,  0,  0,-10, -5,  5,
        5, 10, 10,-20,-20, 10, 10,  5,
        0,  0,  0,  0,  0,  0,  0,  0 },
    { -50,-40,-30,-30,-30,-30,-40,-50,          /* knight */
      -40,-20,  0,  0,  0,  0,-20,-40,
      -30,  0, 10, 15, 15, 10,  0,-30,
      -30,  5, 15, 20, 20, 15,  5,-30,
      -30,  0, 15, 20, 20, 15,  0,-30,
      -30,  5, 10, 15, 15, 10,  5,-30,
      -40,-20,  0,  5,  5,  0,-20,-40,
      -50,-40,-30,-30,-30,-30,-40,-50 },
    { -20,-10,-10,-10,-10,-10,-10,-20,          /* bishop */
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5, 10, 10,  5,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0, 10, 10, 10, 10,  0,-10,
      -10, 10, 10, 10, 10, 10, 10,-10,
      -10,  5,  0,  0,  0,  0,  5,-10,
      -20,-10,-10,-10,-10,-10,-10,-20 },
    {   0,  0,  0,  0,  0,  0,  0,  0,          /* rook */
        5, 10, 10, 10, 10, 10, 10,  5,
       -5,  0,  0,  0,  0,  0,  0, -5,
       -5,  0,  0,  0,  0,  0,  0, -5,
       -5,  0,  0,  0,  0,  0,  0, -5,
       -5,  0,  0,  0,  0,  0,  0, -5,
       -5,  0,  0,  0,  0,  0,  0, -5,
        0,  0,  0,  5,  5,  0,  0,  0 },
    { -20,-10,-10, -5, -5,-10,-10,-20,          /* queen */
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5,  5,  5,  5,  0,-10,
       -5,  0,  5,  5,  5,  5,  0, -5,
        0,  0,  5,  5,  5,  5,  0, -5,
      -10,  5,  5,  5,  5,  5,  0,-10,
      -10,  0,  5,  0,  0,  0,  0,-10,
      -20,-10,-10, -5, -5,-10,-10,-20 },
};

/* The big arrays: on the P2000C they lie past the program (memory.asm). */
#ifdef __SDCC
extern int psq_tab[12][64];
extern unsigned int zobrist[12][64];
extern unsigned int zobrist_castle[16];
extern unsigned int zobrist_ep[8];
#else
undo_t hist[MAX_HIST];
move_t moves[MOVE_STACK];
int psq_tab[12][64];
unsigned int zobrist[12][64];
unsigned int zobrist_castle[16];
unsigned int zobrist_ep[8];
#endif

/* Value + square bonus with sign (White positive), per piece and 0..63
 * square; index 0-5 White pawn..king, 6-11 Black; kings count 0 here.
 * Built by chess_init(); rules.asm reads it. */
#define PIDX(p) (TYPE(p) - 1 + (((p) & BLACK) ? 6 : 0))
#define PSQ(p, sq) psq_tab[PIDX(p)][SQ64(sq)]

/* Zobrist keys: one per piece and square, castling rights, en-passant
 * file and side to move; the position key is the XOR of what applies. */
#define ZOBRIST_SIDE 0x5A3Cu                /* also in rules.asm */
#define ZKEY(p, sq) zobrist[PIDX(p)][SQ64(sq)]
#define ZEP(e) ((e) == NO_SQ ? 0 : zobrist_ep[FILE_OF(e)])


static const unsigned char BACK_RANK[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };

/* Nonzero when two squares share a rank, file or diagonal; index
 * a - b + 119 (0x88 differences are unique per offset). */
unsigned char aligned[239];

void chess_init(void)
{
    unsigned char i, sq, t;
    int v;
    unsigned int x = 0xACE1u;               /* fixed seed: keys are the same every run */
    signed char k;
    unsigned int *key = &zobrist[0][0];
    unsigned int n;

    for (n = 0; n < 12 * 64 + 16 + 8; n++) {
        x ^= x << 7;
        x ^= x >> 9;
        x ^= x << 8;
        if (n < 12 * 64)
            key[n] = x;
        else if (n < 12 * 64 + 16)
            zobrist_castle[n - 12 * 64] = x;
        else
            zobrist_ep[n - 12 * 64 - 16] = x;
    }

    for (sq = 0; sq < 128; sq++) {
        castle_keep[sq] = 0x0F;
        board[sq] = EMPTY;
    }
    for (i = 0; i < 64; i++)
        for (t = PAWN; t <= KING; t++) {
            v = t == KING ? 0 : piece_value[t];
            psq_tab[t - 1][i] = t == KING ? 0 : v + PST[t - 1][i ^ 56];
            psq_tab[t + 5][i] = t == KING ? 0 : -(v + PST[t - 1][i]);
        }
    for (i = 0; i < 239; i++)
        aligned[i] = 0;
    for (i = 0; i < 8; i++)
        for (k = 1; k < 8; k++)
            aligned[119 + k * SLIDE_D[i]] = 1;
    castle_keep[SQ(0, 0)] = (unsigned char)~CASTLE_WQ;
    castle_keep[SQ(4, 0)] = (unsigned char)~(CASTLE_WK | CASTLE_WQ);
    castle_keep[SQ(7, 0)] = (unsigned char)~CASTLE_WK;
    castle_keep[SQ(0, 7)] = (unsigned char)~CASTLE_BQ;
    castle_keep[SQ(4, 7)] = (unsigned char)~(CASTLE_BK | CASTLE_BQ);
    castle_keep[SQ(7, 7)] = (unsigned char)~CASTLE_BK;

    score = 0;
    phase = 0;
    hash = 0;
    for (i = 0; i < 8; i++) {
        board[SQ(i, 0)] = WHITE | BACK_RANK[i];
        board[SQ(i, 1)] = WHITE | PAWN;
        board[SQ(i, 6)] = BLACK | PAWN;
        board[SQ(i, 7)] = BLACK | BACK_RANK[i];
    }
    for (sq = 0; sq < 128; sq++) {
        if (OFFBOARD(sq) || !board[sq])
            continue;
        score += PSQ(board[sq], sq);
        phase += PHASE[TYPE(board[sq])];
        hash ^= ZKEY(board[sq], sq);
    }
    king_sq[0] = SQ(4, 0);
    king_sq[1] = SQ(4, 7);
    side = WHITE;
    castle = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    ep = NO_SQ;
    halfmove = 0;
    hist_len = 0;
    hash ^= zobrist_castle[castle];
}

/* Recomputes the key after the board was set up by other means (tests). */
void chess_rehash(void)
{
    unsigned char sq;
    hash = zobrist_castle[castle] ^ ZEP(ep) ^ (side == BLACK ? ZOBRIST_SIDE : 0);
    for (sq = 0; sq < 128; sq++)
        if (!OFFBOARD(sq) && board[sq])
            hash ^= ZKEY(board[sq], sq);
}

#ifndef __SDCC

unsigned char board[128];
unsigned char castle_keep[128];

/* --- attacks ------------------------------------------------------------------- */

unsigned char attacked(unsigned char sq, unsigned char by)
{
    unsigned char i, t, p, d;
    unsigned char bishop = by | BISHOP, rook = by | ROOK, queen = by | QUEEN;

    /* pawns: a White pawn attacks upward, so look one rank down */
    if (by == WHITE) {
        t = sq - 15;
        if (!OFFBOARD(t) && board[t] == (WHITE | PAWN))
            return 1;
        t = sq - 17;
        if (!OFFBOARD(t) && board[t] == (WHITE | PAWN))
            return 1;
    } else {
        t = sq + 15;
        if (!OFFBOARD(t) && board[t] == (BLACK | PAWN))
            return 1;
        t = sq + 17;
        if (!OFFBOARD(t) && board[t] == (BLACK | PAWN))
            return 1;
    }
    p = by | KNIGHT;
    for (i = 0; i < 8; i++) {
        t = sq + KNIGHT_D[i];
        if (!OFFBOARD(t) && board[t] == p)
            return 1;
    }
    p = by | KING;
    for (i = 0; i < 8; i++) {
        t = sq + KING_D[i];
        if (!OFFBOARD(t) && board[t] == p)
            return 1;
    }
    for (i = 0; i < 8; i++) {
        d = SLIDE_D[i];
        t = sq;
        for (;;) {
            t += d;
            if (OFFBOARD(t))
                break;
            p = board[t];
            if (!p)
                continue;
            if (p == queen || p == (i < 4 ? bishop : rook))
                return 1;
            break;
        }
    }
    return 0;
}

/* --- move generation ----------------------------------------------------------- */

static move_t *out;

static unsigned char attacker;             /* type of the piece generating moves */

/* Appends a move with its ordering key (the same keys as rules.asm). */
static void add(unsigned char from, unsigned char to, unsigned char flags)
{
    unsigned char key = 0;
    if (flags & MF_EP)
        key = 100 + 8 * PAWN - PAWN;
    else if (flags & MF_CAPTURE)
        key = 100 + 8 * TYPE(board[to]) - attacker;
    if ((flags & MF_PROMO) == QUEEN)
        key += 95;
    out->from = from;
    out->to = to;
    out->flags = flags;
    out->key = key;
    out++;
}

static void add_promotions(unsigned char from, unsigned char to, unsigned char flags,
                           unsigned char captures_only)
{
    add(from, to, flags | QUEEN);
    if (captures_only)
        return;
    add(from, to, flags | KNIGHT);
    add(from, to, flags | ROOK);
    add(from, to, flags | BISHOP);
}

unsigned int gen_moves(unsigned int start, unsigned char captures_only)
{
    unsigned char sq, t, p, q, i, first, last, d, rank;
    unsigned char me = side, them = OTHER(side);
    signed char fwd = me == WHITE ? 16 : -16;
    unsigned char start_rank = me == WHITE ? 1 : 6;
    unsigned char promo_rank = me == WHITE ? 6 : 1;

    out = moves + start;
    for (sq = 0; sq < 128; sq++) {
        if (sq & 8) {
            sq += 7;
            continue;
        }
        p = board[sq];
        if (!(p & me))
            continue;
        attacker = TYPE(p);
        switch (TYPE(p)) {
        case PAWN:
            rank = RANK_OF(sq);
            t = sq + fwd;
            if (!board[t]) {
                if (rank == promo_rank)
                    add_promotions(sq, t, 0, captures_only);
                else if (!captures_only) {
                    add(sq, t, 0);
                    if (rank == start_rank && !board[(unsigned char)(t + fwd)])
                        add(sq, t + fwd, MF_DOUBLE);
                }
            }
            for (i = 0; i < 2; i++) {
                t = sq + fwd + (i ? 1 : -1);
                if (OFFBOARD(t))
                    continue;
                if (board[t] & them) {
                    if (rank == promo_rank)
                        add_promotions(sq, t, MF_CAPTURE, captures_only);
                    else
                        add(sq, t, MF_CAPTURE);
                } else if (t == ep)
                    add(sq, t, MF_EP | MF_CAPTURE);
            }
            break;
        case KNIGHT:
        case KING:
            for (i = 0; i < 8; i++) {
                t = sq + (TYPE(p) == KNIGHT ? KNIGHT_D[i] : KING_D[i]);
                if (OFFBOARD(t))
                    continue;
                q = board[t];
                if (q & me)
                    continue;
                if (q)
                    add(sq, t, MF_CAPTURE);
                else if (!captures_only)
                    add(sq, t, 0);
            }
            if (TYPE(p) == KING && !captures_only && (castle & (me == WHITE ? 3 : 12))) {
                /* the king stands on its home square while it has rights */
                if ((castle & (me == WHITE ? CASTLE_WK : CASTLE_BK)) && !board[sq + 1] && !board[sq + 2]
                    && !attacked(sq, them) && !attacked(sq + 1, them))
                    add(sq, sq + 2, MF_CASTLE);
                if ((castle & (me == WHITE ? CASTLE_WQ : CASTLE_BQ)) && !board[sq - 1] && !board[sq - 2]
                    && !board[sq - 3] && !attacked(sq, them) && !attacked(sq - 1, them))
                    add(sq, sq - 2, MF_CASTLE);
            }
            break;
        default:
            first = TYPE(p) == ROOK ? 4 : 0;
            last = TYPE(p) == BISHOP ? 4 : 8;
            for (i = first; i < last; i++) {
                d = SLIDE_D[i];
                t = sq;
                for (;;) {
                    t += d;
                    if (OFFBOARD(t))
                        break;
                    q = board[t];
                    if (q) {
                        if (q & them)
                            add(sq, t, MF_CAPTURE);
                        break;
                    }
                    if (!captures_only)
                        add(sq, t, 0);
                }
            }
            break;
        }
    }
    return (unsigned int)(out - moves);
}

#endif /* !__SDCC */

/* --- making and taking back moves ------------------------------------------------ */

#ifndef __SDCC                              /* rules.asm has these for the Z80 */

unsigned char make_move(const move_t *m)
{
    undo_t *u = &hist[hist_len++];
    unsigned char from = m->from, to = m->to, flags = m->flags;
    unsigned char p = board[from], captured = board[to], me = side, t, rook;

    u->move = *m;
    u->castle = castle;
    u->ep = ep;
    u->halfmove = halfmove;
    u->score = score;
    u->phase = phase;
    u->hash = hash;

    halfmove++;
    score -= PSQ(p, from);
    hash ^= ZKEY(p, from) ^ ZEP(ep) ^ zobrist_castle[castle] ^ ZOBRIST_SIDE;
    if (flags & MF_EP) {
        t = me == WHITE ? to - 16 : to + 16;
        captured = board[t];
        board[t] = EMPTY;
        score -= PSQ(captured, t);
        hash ^= ZKEY(captured, t);
    } else if (captured) {
        score -= PSQ(captured, to);
        hash ^= ZKEY(captured, to);
        phase -= PHASE[TYPE(captured)];
        halfmove = 0;
    }
    u->captured = captured;
    board[from] = EMPTY;
    if (flags & MF_PROMO) {
        p = me | (flags & MF_PROMO);
        phase += PHASE[flags & MF_PROMO];
    }
    board[to] = p;
    score += PSQ(p, to);
    hash ^= ZKEY(p, to);

    ep = NO_SQ;
    switch (TYPE(p)) {
    case PAWN:
        halfmove = 0;
        if (flags & MF_DOUBLE) {
            ep = (from + to) >> 1;
            hash ^= zobrist_ep[FILE_OF(ep)];
        }
        break;
    case KING:
        king_sq[SIDE_INDEX(me)] = to;
        if (flags & MF_CASTLE) {
            /* g-file: rook h -> f; c-file: rook a -> d */
            if (FILE_OF(to) == 6) {
                t = to + 1;
                from = to - 1;              /* reused: the rook's destination */
            } else {
                t = to - 2;
                from = to + 1;
            }
            rook = board[t];
            board[t] = EMPTY;
            board[from] = rook;
            score += PSQ(rook, from) - PSQ(rook, t);
            hash ^= ZKEY(rook, from) ^ ZKEY(rook, t);
            from = m->from;
        }
        break;
    }
    castle &= castle_keep[from] & castle_keep[to];
    hash ^= zobrist_castle[castle];
    side = OTHER(me);

    t = king_sq[SIDE_INDEX(me)];
    if ((mover_in_check || t == to || (flags & MF_EP) || aligned[119 + t - m->from])
        && attacked(t, side)) {
        unmake_move();
        return 0;
    }
    return 1;
}

void unmake_move(void)
{
    undo_t *u = &hist[--hist_len];
    unsigned char from = u->move.from, to = u->move.to, flags = u->move.flags;
    unsigned char me = OTHER(side), p = board[to];

    side = me;
    if (flags & MF_PROMO)
        p = me | PAWN;
    board[from] = p;
    if (flags & MF_EP) {
        board[to] = EMPTY;
        board[me == WHITE ? to - 16 : to + 16] = u->captured;
    } else
        board[to] = u->captured;
    if (TYPE(p) == KING) {
        king_sq[SIDE_INDEX(me)] = from;
        if (flags & MF_CASTLE) {
            if (FILE_OF(to) == 6) {
                board[to + 1] = board[to - 1];
                board[to - 1] = EMPTY;
            } else {
                board[to - 2] = board[to + 1];
                board[to + 1] = EMPTY;
            }
        }
    }
    castle = u->castle;
    ep = u->ep;
    halfmove = u->halfmove;
    score = u->score;
    phase = u->phase;
    hash = u->hash;
}

#endif /* !__SDCC */

unsigned int gen_legal(unsigned int start)
{
    unsigned int end = gen_moves(start, 0), i, n = start;
    mover_in_check = in_check(side);
    for (i = start; i < end; i++) {
        if (!make_move(&moves[i]))
            continue;
        unmake_move();
        moves[n++] = moves[i];
    }
    return n;
}

/* --- positions ------------------------------------------------------------------- */

unsigned char repetitions(void)
{
    unsigned int k = hist_len, oldest = hist_len > halfmove ? hist_len - halfmove : 0;
    unsigned char count = 0;
    while (k >= oldest + 2) {
        k -= 2;
        if (hist[k].hash == hash)
            count++;
    }
    return count;
}

unsigned char insufficient_material(void)
{
    unsigned char sq, p, minors = 0, bishop_colours = 0;
    for (sq = 0; sq < 128; sq++) {
        if (sq & 8) {
            sq += 7;
            continue;
        }
        p = TYPE(board[sq]);
        if (p == PAWN || p == ROOK || p == QUEEN)
            return 0;
        if (p == KNIGHT || p == BISHOP) {
            minors++;
            if (p == BISHOP)
                bishop_colours |= ((FILE_OF(sq) + RANK_OF(sq)) & 1) ? 2 : 1;
            else
                bishop_colours |= 4;
        }
    }
    if (minors <= 1)
        return 1;
    /* only bishops, all on squares of one colour */
    return !(bishop_colours & 4) && bishop_colours != 3;
}
