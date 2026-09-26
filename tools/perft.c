/* SPDX-License-Identifier: GPL-3.0-only */
/* perft.c -- move-generator check, compiled natively with src/chess.c.
 *
 * Counts the leaf nodes of the legal move tree to a fixed depth for the
 * standard test positions and compares them with the published values
 * (chessprogramming.org, "Perft Results"). At every node the incrementally
 * kept Zobrist key is also checked against one computed from scratch.
 * Run with `make perft`; `make zperft` builds it for CP/M with the Z80
 * move generator (rules.asm), smaller depths, and runs it in the
 * headless emulator.
 */
#include <stdio.h>
#include <string.h>
#include "../src/chess.h"

static unsigned long hash_errors;
static unsigned char checking = 1;
extern int psq_tab[12][64];

/* The incrementally kept hash and score against ones computed from scratch. */
static void check_hash(void)
{
    unsigned int kept = hash;
    unsigned char sq, p;
    int s = 0;
    chess_rehash();
    for (sq = 0; sq < 128; sq++) {
        p = board[sq];
        if (!(sq & 0x88) && p)
            s += psq_tab[TYPE(p) - 1 + ((p & BLACK) ? 6 : 0)][(sq + (sq & 7)) >> 1];
    }
    if (hash != kept || s != score)
        hash_errors++;
    hash = kept;
}

static unsigned long perft(int depth)
{
    unsigned int start = 0, end, i;
    unsigned long n = 0;
    static unsigned int base[16];
    int ply = 16 - depth;
    unsigned char check;

    if (depth == 0)
        return 1;
    start = ply ? base[ply - 1] : 0;
    end = gen_moves(start, 0);
    base[ply] = end;
    check = in_check(side);
    for (i = start; i < end; i++) {
        mover_in_check = check;             /* exercises make_move()'s shortcut */
        if (!make_move(&moves[i]))
            continue;
        if (checking)
            check_hash();
        n += depth == 1 ? 1 : perft(depth - 1);
        unmake_move();
    }
    return n;
}

static int from_fen(const char *fen)
{
    int rank = 7, file = 0;
    const char *p;
    static const char NAMES[] = " pnbrqk";

    chess_init();
    memset(board, 0, 128);
    for (p = fen; *p && *p != ' '; p++) {
        if (*p == '/') {
            rank--;
            file = 0;
        } else if (*p >= '1' && *p <= '8')
            file += *p - '0';
        else {
            char lower = (char)(*p | 0x20);
            unsigned char type = (unsigned char)(strchr(NAMES, lower) - NAMES);
            unsigned char colour = (*p == lower) ? BLACK : WHITE;
            board[SQ(file, rank)] = colour | type;
            if (type == KING)
                king_sq[SIDE_INDEX(colour)] = SQ(file, rank);
            file++;
        }
    }
    side = p[1] == 'w' ? WHITE : BLACK;
    p += 3;
    castle = 0;
    for (; *p != ' '; p++) {
        if (*p == 'K') castle |= CASTLE_WK;
        if (*p == 'Q') castle |= CASTLE_WQ;
        if (*p == 'k') castle |= CASTLE_BK;
        if (*p == 'q') castle |= CASTLE_BQ;
    }
    p++;
    ep = *p == '-' ? NO_SQ : SQ(p[0] - 'a', p[1] - '1');
    chess_rehash();
    score = 0;
    for (rank = 0; rank < 128; rank++)
        if (!(rank & 0x88) && board[rank])
            score += psq_tab[TYPE(board[rank]) - 1 + ((board[rank] & BLACK) ? 6 : 0)][(rank + (rank & 7)) >> 1];
    return 0;
}

struct test {
    const char *name, *fen;
    int depth;
    unsigned long expected;
};

#ifdef __SDCC
static const struct test TESTS[] = {
    { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902UL },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862UL },
    { "position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238UL },
    { "position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467UL },
    { "position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 2, 1486UL },
    { "position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 2, 2079UL },
};
#else
static const struct test TESTS[] = {
    { "start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609UL },
    { "kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603UL },
    { "position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624UL },
    { "position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333UL },
    { "position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487UL },
    { "position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594UL },
};
#endif

int main(void)
{
    unsigned int i;
    int failures = 0;
    for (i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) {
        unsigned long n;
        if (i == 0)
            chess_init();
        else
            from_fen(TESTS[i].fen);
#ifdef __SDCC
        checking = TESTS[i].expected < 10000;   /* too slow for the Z80 on the big ones */
#endif
        n = perft(TESTS[i].depth);
        printf("%s depth %d: %lu expected %lu %s\n", TESTS[i].name, TESTS[i].depth, n,
               TESTS[i].expected, n == TESTS[i].expected ? "ok" : "FAIL");
        failures += n != TESTS[i].expected;
    }
    printf("hash or score mismatches: %lu\n", hash_errors);
    failures += hash_errors != 0;
    puts(failures ? "FAIL" : "PASS");
    return failures != 0;
}
