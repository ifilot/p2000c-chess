/* SPDX-License-Identifier: GPL-3.0-only */
/* Schaken voor de Philips P2000C -- chess against the computer, in Dutch.
 *
 * Title picture, start screen (colour, level 1-3, demo, help, quit), then
 * games until the player leaves. Modules:
 *   game.c    state, cursor interface, turns, demo    screen.c  board picture and uploads
 *   panel.c   text panel and move list               screens.c start/help screens, title
 *   saver.c   key waits with the CRT screen saver    clock.c   game clock (BIOS ticks)
 *   chess.c   rules: moves, legality, draws           cpu.c     computer player
 *   gfx.c     loads SCHAKEN.GFX (bitmaps, title)   memory.asm the big buffers, past the program
 *   rules.asm move generation, making moves       video.asm framebuffer primitives, ESC r, BIOS I/O
 *
 * The board uses the terminal's 512x252 high-resolution mode with the 64x21
 * text plane for the panel; the other screens use the 80x24 text mode.
 */
#include "video.h"
#include "cpu.h"
#include "game.h"
#include "screens.h"
#include "clock.h"
#include "gfx.h"

#define ESC 27

int main(void)
{
    unsigned char level;

    if (!gfx_load()) {
        con_puts("SCHAKEN.GFX niet gevonden.\r\n");
        return 1;
    }
    conout(ESC); conout('c');                /* no blinking text cursor */
    clock_probe();
    splash_screen();
    while ((level = start_screen()) != 0) {
        if (level == START_DEMO) {
            demo_game();
            video_text();
            continue;
        }
        cpu_level = level;
        level = play();
        video_text();
        if (!level)
            break;
    }
    text_clear();
    conout(ESC); conout('C');                /* CP/M gets its cursor back */
    return 0;
}
