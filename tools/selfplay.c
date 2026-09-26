/* SPDX-License-Identifier: GPL-3.0-only */
/* selfplay.c -- engine games on the host, compiled with src/chess.c and src/cpu.c.
 *
 * Without the P2000C clock the search runs on its node budgets, which makes
 * games reproducible for a given seed. Prints each game in coordinate
 * notation with its result; `make selfplay` plays a few games per level
 * pairing. Usage: selfplay [white-level black-level games seed]
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/chess.h"
#include "../src/cpu.h"
#include "../src/clock.h"

unsigned char clock_available = 0;
unsigned int clock_ticks(void) { return 0; }
void key_watch(void) {}

static const char *result(void)
{
    unsigned int n = gen_legal(0);
    if (n == 0)
        return in_check(side) ? (side == WHITE ? "0-1 (mat)" : "1-0 (mat)") : "1/2 (pat)";
    if (halfmove >= 100)
        return "1/2 (50 zetten)";
    if (repetitions() >= 2)
        return "1/2 (herhaling)";
    if (insufficient_material())
        return "1/2 (materiaal)";
    if (hist_len >= GAME_MAX)
        return "1/2 (lengte)";
    return 0;
}

int main(int argc, char **argv)
{
    int wl = argc > 1 ? atoi(argv[1]) : 2, bl = argc > 2 ? atoi(argv[2]) : 3;
    int games = argc > 3 ? atoi(argv[3]) : 1, g;
    unsigned long total_nodes = 0, searches = 0;
    cpu_seed = argc > 4 ? (unsigned int)atoi(argv[4]) : 1234;
    for (g = 0; g < games; g++) {
        const char *r;
        move_t m;
        chess_init();
        while (!(r = result())) {
            cpu_level = side == WHITE ? wl : bl;
            cpu_choose(&m);
            if (!make_move(&m)) {
                printf("ILLEGAL MOVE\n");
                return 1;
            }
            if (cpu_nodes) {
                total_nodes += cpu_nodes;
                searches++;
            }
            printf("%c%c%c%c%s ", 'a' + FILE_OF(m.from), '1' + RANK_OF(m.from), 'a' + FILE_OF(m.to),
                   '1' + RANK_OF(m.to), (m.flags & MF_PROMO) ? "=" : "");
        }
        printf("\ngame %d: %s after %u plies\n", g + 1, r, hist_len);
    }
    printf("average %lu nodes per search\n", searches ? total_nodes / searches : 0);
    return 0;
}
