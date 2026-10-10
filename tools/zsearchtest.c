/* SPDX-License-Identifier: GPL-3.0-only */
/* Search regressions compiled with the actual Z80 rules and 16-bit ints.
 * Include the search to reach leaves after the check-extension horizon. */
#include <stdio.h>
#include <string.h>
#include "../src/cpu.c"

unsigned char clock_available = 0;
unsigned int clock_ticks(void) { return 0; }
void key_watch(void) {}

#define CHECK(c, label) do { if (!(c)) { printf("FAIL: %s\n", label); return 1; } } while (0)

static void setup(unsigned char wk, unsigned char piece, unsigned char sq)
{
    unsigned char i, p;
    chess_init();
    memset(board, 0, 128);
    board[wk] = WHITE | KING;
    board[sq] = WHITE | piece;
    board[SQ(7, 7)] = BLACK | KING;
    king_sq[0] = wk;
    king_sq[1] = SQ(7, 7);
    side = BLACK;
    castle = 0;
    ep = NO_SQ;
    score = 0;
    phase = piece == QUEEN ? 4 : 2;
    for (i = 0; i < 128; i++) {
        p = board[i];
        if (!OFFBOARD(i) && p)
            score += PIECE_SCORE(p, i);
    }
    chess_rehash();
}

static int probe(int alpha, int beta)
{
    ply = 12;
    sp = 0;
    stop = 0;
    cpu_nodes = 0;
    node_budget = 4000;
    return quiesce(alpha, beta);
}

int main(void)
{
    static const unsigned char repeat_line[8] = {
        SQ(0,7), SQ(1,7), SQ(2,0), SQ(3,0),
        SQ(1,7), SQ(2,7), SQ(3,0), SQ(2,0)
    };
    unsigned char i;
    unsigned char j;
    unsigned int x = 0xACE1u, signature = 0;
    unsigned int saved_hash, n;
    int saved_score, value, expected;
    move_t evasion;

    setup(SQ(6, 5), QUEEN, SQ(5, 6)); /* stalemate */
    CHECK(gen_legal(0) == 0 && !in_check(side), "stalemate setup");
    CHECK(probe(-32000, 32000) == 0, "stalemate at leaf");
    CHECK(probe(-2000, -1500) == 0, "stalemate before stand-pat cutoff");
    CHECK(probe(100, 200) == 0, "stalemate outside alpha window");

    setup(SQ(6, 5), QUEEN, SQ(6, 6)); /* mate */
    CHECK(gen_legal(0) == 0 && in_check(side), "mate setup");
    CHECK(probe(-32000, 32000) == -MATE + 12, "mate at leaf");
    CHECK(probe(-32000, -31999) == -MATE + 12, "mate before stand-pat cutoff");

    setup(SQ(5, 5), QUEEN, SQ(7, 1)); /* only h8-g8 evades check */
    n = gen_legal(0);
    CHECK(n == 1 && moves[0].to == SQ(6, 7), "quiet evasion setup");
    evasion = moves[0];
    saved_hash = hash;
    saved_score = score;
    value = probe(-32000, 32000);
    CHECK(cpu_nodes > 1, "quiet evasion searched");
    CHECK(hash == saved_hash && score == saved_score && hist_len == 0 && sp == 0 && side == BLACK,
          "state restored");
    CHECK(make_move(&evasion), "play quiet evasion");
    expected = -probe(-32000, 32000);
    unmake_move();
    CHECK(value == expected, "quiet evasion score");
    probe(-32000, -2000);
    CHECK(cpu_nodes > 1, "no stand-pat cutoff in check");
    halfmove = 100;
    CHECK(probe(-32000, 32000) == 0, "fifty-move draw");

    setup(SQ(2,0), QUEEN, SQ(4,3));
    board[SQ(7,7)] = EMPTY;
    board[SQ(0,7)] = BLACK | KING;
    king_sq[1] = SQ(0,7);
    chess_rehash();
    evasion.flags = evasion.key = 0;
    for (i = 0; i < 8; i += 2) {
        evasion.from = repeat_line[i]; evasion.to = repeat_line[i + 1];
        mover_in_check = 1;
        CHECK(make_move(&evasion), "repetition line legal");
    }
    CHECK(!repetitions(), "root not yet repeated");
    saved_hash = hash; saved_score = score;
    ply = 0; sp = 0; stop = 0; cpu_nodes = 0; node_budget = 65535;
    for (i = 0; i < MAX_PLY; i++)
        killer[i][0].from = killer[i][1].from = NO_SQ;
    CHECK(search(1, -100, 100) == 0, "quiet repetition draw");
    CHECK(hash == saved_hash && score == saved_score && hist_len == 4 && side == BLACK,
          "search restores state");

    setup(SQ(4,0), PAWN, SQ(0,5));
    saved_score = pawn_endgame();
    board[SQ(0,5)] = EMPTY; board[SQ(0,2)] = WHITE | PAWN;
    chess_rehash();
    CHECK(saved_score > pawn_endgame(), "advanced passed pawn bonus");
    board[SQ(0,2)] = BLACK | PAWN;
    chess_rehash();
    CHECK(saved_score == -pawn_endgame(), "pawn evaluation colour symmetry");
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 16; j++) {
            x ^= x << 7; x &= 0xFFFFu;
            x ^= x >> 9;
            x ^= x << 8; x &= 0xFFFFu;
            pawn_files[j] = (unsigned char)x;
        }
        signature = ((signature << 1) | (signature >> 15)) & 0xFFFFu;
        signature ^= (unsigned int)pawn_endgame() & 0xFFFFu;
    }
    printf("PAWN SIGNATURE %u\n", signature);
    printf("SEARCH PASS\n");
    return 0;
}
