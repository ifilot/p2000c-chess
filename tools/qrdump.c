/* SPDX-License-Identifier: GPL-3.0-only */
/* qrdump.c -- native driver for tools/test_qr.py (compiled with src/chess.c
 * and src/qr.c).
 *
 *   qrdump text STRING       the QR matrix of STRING
 *   qrdump games N SEED      N random games: per ply the moves so far and the
 *                            FEN, every seventh position also its QR matrix
 *
 * Output lines: "M e2e4 e7e5 ..." (moves as from-to plus promotion letter),
 * "F <fen>", "Q" followed by QR_SIZE lines of 0 and 1 (1 = dark).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/chess.h"
#include "../src/qr.h"

static unsigned char work[QR_WORK];

static void dump_qr(const char *text)
{
    int r, c;
    qr_encode(text, (unsigned char)strlen(text), work);
    puts("Q");
    for (r = 0; r < QR_SIZE; r++) {
        for (c = 0; c < QR_SIZE; c++)
            putchar('0' + (work[r * QR_SIZE + c] & 1));
        putchar('\n');
    }
}

static unsigned long seed;

static unsigned int next_random(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned int)(seed >> 16) & 0x7FFF;
}

static void random_game(int number)
{
    static char record[MAX_HIST * 6 + 1];
    char fen[FEN_MAX + 1], *p = record;
    unsigned int n;
    move_t m;

    chess_init();
    record[0] = 0;
    for (;;) {
        chess_fen(fen);
        if (strlen(fen) > FEN_MAX) {
            fprintf(stderr, "FEN longer than FEN_MAX: %s\n", fen);
            exit(1);
        }
        printf("M%s\nF %s\n", record, fen);
        if ((hist_len + number) % 7 == 0)
            dump_qr(fen);
        n = gen_legal(0);
        if (n == 0 || halfmove >= 100 || hist_len >= MAX_HIST - 48)
            break;
        m = moves[next_random() % n];
        *p++ = ' ';
        *p++ = 'a' + FILE_OF(m.from);
        *p++ = '1' + RANK_OF(m.from);
        *p++ = 'a' + FILE_OF(m.to);
        *p++ = '1' + RANK_OF(m.to);
        if (m.flags & MF_PROMO)
            *p++ = " pnbrq"[m.flags & MF_PROMO];
        *p = 0;
        mover_in_check = 1;
        make_move(&m);
    }
}

int main(int argc, char **argv)
{
    int i, games;
    if (argc == 3 && !strcmp(argv[1], "text")) {
        dump_qr(argv[2]);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "games")) {
        games = atoi(argv[2]);
        seed = strtoul(argv[3], 0, 10);
        for (i = 0; i < games; i++)
            random_game(i);
        return 0;
    }
    fprintf(stderr, "usage: qrdump text STRING | qrdump games N SEED\n");
    return 2;
}
