/* SPDX-License-Identifier: GPL-3.0-only */
/* RAM mailbox for matches of the real Z80 engine, with the BIOS clock. */
#include <stdio.h>
#include "../src/chess.h"
#include "../src/cpu.h"
#include "../src/clock.h"

volatile unsigned char bridge_cmd;
unsigned char bridge_ok;
unsigned int bridge_count;
move_t bridge_move;
char bridge_fen[100];

void key_watch(void) {}

int main(void)
{
    unsigned char command;
    clock_probe();
    chess_init();
    bridge_cmd = 0;
    printf("ENGINE READY\n");
    for (;;) {
        command = bridge_cmd;
        if (!command)
            continue;
        bridge_ok = 1;
        if (command == 1)
            chess_init();
        else if (command == 2) {
            bridge_count = gen_legal(0);
            if (bridge_count)
                cpu_choose(&bridge_move);
            else
                bridge_ok = 0;
        } else if (command == 3) {
            mover_in_check = 1;
            bridge_ok = make_move(&bridge_move);
        } else if (command == 4)
            bridge_count = gen_legal(0);
        else
            bridge_ok = 0;
        chess_fen(bridge_fen);
        bridge_cmd = 0;                     /* publish the completed response */
    }
}
