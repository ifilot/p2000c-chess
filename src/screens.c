/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.c -- the text-mode screens and the title picture.
 *
 * The start and help screens use the plain 80x24 text mode (instant); the
 * title picture is a 512x252 bitmap whose run-length data gfx.c reads from
 * SCHAKEN.GFX into the start of the framebuffer. It is unpacked in place
 * once, backwards, and uploaded sparsely.
 */
#include "video.h"
#include "chess.h"
#include "cpu.h"
#include "game.h"
#include "screen.h"
#include "screens.h"
#include "saver.h"
#include "splash.h"
#include "version.h"

/* Character-ROM glyphs used by the text-mode screens. */
#define CH_BLOCK 0x9F                       /* full 8x12 block */
#define CH_H     0xD0                       /* box drawing: single lines */
#define CH_V     0xFA
#define CH_TL    0xA9
#define CH_TR    0xB9
#define CH_BL    0xAA
#define CH_BR    0xBA
#define ESC      27

/* --- help ------------------------------------------------------------------------ */

static const char *const HELP[] = {
    "SCHAKEN v" VERSION " voor de Philips P2000C" "                   gecompileerd " BUILD_DATE,
    REPO_URL,
    "",
    "BEDIENING",
    "  Pijltjes of W A S D   cursor verplaatsen over het bord",
    "  RETURN of spatie      stuk oppakken, dan zetten op een gemarkeerd veld:",
    "                        een stip is een vrij veld, hoekjes een stuk dat u slaat",
    "                        RETURN op het stuk zelf of ESC legt het weer neer",
    "  T of BS               zet terugnemen          H   dit hulpscherm",
    "  N                     nieuwe partij           Q   stoppen (met bevestiging)",
    "",
    "SPELREGELS",
    "  Wit begint. Rokeren: zet de koning twee velden opzij, de toren volgt vanzelf.",
    "  En passant: sla een pion die net twee velden zette alsof hij er een zette.",
    "  Promotie: een pion op de laatste rij wordt D(ame), T(oren), L(oper) of",
    "  P(aard); RETURN kiest de dame. Schaakmat wint. Remise bij pat, na 50 zetten",
    "  zonder slaan of pionzet, bij drie keer dezelfde stelling of als geen van",
    "  beiden nog mat kan zetten.",
    "",
    "NOTATIE  K koning  D dame  T toren  L loper  P paard, pionzetten zonder letter;",
    "         x slaat, + schaak, # mat, O-O en O-O-O rokade",
    "NIVEAUS  1 licht: een zet vooruit   2 normaal: twee zetten   3 zwaar: tot 10 s",
    "Druk op een toets om terug te keren.",
};

/* Clears the 80x24 text screen and hides the blinking cursor. */
void text_clear(void)
{
    con_at(ROWCOL(0, 0));
    conout(ESC); conout('k');
    conout(ESC); conout('c');
}

static void draw_help_page(void)
{
    unsigned char row;
    text_clear();
    for (row = 0; row < sizeof HELP / sizeof HELP[0]; row++) {
        con_at(ROWCOL(row, 0));
        con_puts(HELP[row]);
    }
}

static void help_page(void)
{
    draw_help_page();
    wait_key(draw_help_page);
}

/* Shows the rules from the game, then restores the board. Leaving graphics
 * mode clears the terminal's picture, but the framebuffer in RAM is intact,
 * so the return costs one flush_frame(). */
void help_screen(void)
{
    video_text();
    help_page();
    redraw_game_screen();
}

/* --- title picture ----------------------------------------------------------- */

/* Unpacks the title bitmap in place: the (count, value) pairs at the start
 * of the framebuffer are read from the last one backwards and their runs
 * written from the end of the framebuffer down (gen_splash.py checks that
 * the runs never reach a pair not yet read). Only possible once. */
static void unpack_splash(void)
{
    const unsigned char *in = framebuffer + SPLASH_RLE_SIZE;
    unsigned char *out = framebuffer + FB_LINE * FB_LINES;
    unsigned char n, value;
    while (in != framebuffer) {
        in -= 2;
        n = in[0];
        value = in[1];
        while (n--)
            *--out = value;
    }
}

static void draw_splash(void)
{
    video_graphics();
    flush_sparse();
}

/* Title picture in graphics mode; returns after any key. */
void splash_screen(void)
{
    unpack_splash();
    draw_splash();
    wait_key(draw_splash);
    video_text();
}

/* --- start screen -------------------------------------------------------------- */

/* SCHAKEN in a five-row block font, 34 pixels wide; every pixel becomes two
 * block characters, which is close to square on the CRT. */
static const char *const TITLE[5] = {
    " ###  ### #  #  ##  #  # #### #  #",
    "#    #    #  # #  # # #  #    ## #",
    " ##  #    #### #### ##   ###  # ##",
    "   # #    #  # #  # # #  #    #  #",
    "###   ### #  # #  # #  # #### #  #",
};

static void put_repeat(unsigned char ch, unsigned char count)
{
    while (count--)
        conout(ch);
}

/* A single-line box from (row, col), width and height in cells. */
static void draw_box(unsigned char row, unsigned char col, unsigned char width, unsigned char height)
{
    unsigned char r;
    con_at(ROWCOL(row, col));
    conout(CH_TL); put_repeat(CH_H, width - 2); conout(CH_TR);
    for (r = row + 1; r < row + height - 1; r++) {
        con_at(ROWCOL(r, col)); conout(CH_V);
        con_at(ROWCOL(r, col + width - 1)); conout(CH_V);
    }
    con_at(ROWCOL(row + height - 1, col));
    conout(CH_BL); put_repeat(CH_H, width - 2); conout(CH_BR);
}

static void show_colour(void)
{
    con_at(ROWCOL(12, 23)); con_puts(human == WHITE ? "[W] wit, u begint" : " W  wit, u begint");
    con_at(ROWCOL(12, 46)); con_puts(human == BLACK ? "[Z] zwart" : " Z  zwart");
}

static void draw_start_screen(void)
{
    unsigned char row;
    const char *pixel;

    text_clear();
    draw_box(0, 0, 80, 23);                  /* row 23 stays empty: writing its last cell scrolls */
    for (row = 0; row < 5; row++) {
        con_at(ROWCOL(2 + row, 6));
        for (pixel = TITLE[row]; *pixel; pixel++) {
            conout(*pixel == '#' ? CH_BLOCK : ' ');
            conout(*pixel == '#' ? CH_BLOCK : ' ');
        }
    }
    con_at(ROWCOL(8, 29));  con_puts("voor de Philips P2000C");
    con_at(ROWCOL(9, 14));  con_puts("versie " VERSION "   -   " REPO_URL);

    draw_box(11, 12, 56, 8);
    con_at(ROWCOL(12, 14)); con_puts("Kleur:");
    show_colour();
    con_at(ROWCOL(14, 14)); con_puts("Sterkte van de computer:");
    con_at(ROWCOL(15, 20)); con_puts("1  Licht     kijkt een zet vooruit");
    con_at(ROWCOL(16, 20)); con_puts("2  Normaal   kijkt twee zetten vooruit");
    con_at(ROWCOL(17, 20)); con_puts("3  Zwaar     denkt tot 10 seconden");

    con_at(ROWCOL(20, 6));  con_puts("W/Z: kleur   1, 2 of 3: spelen   D: demo   H: hulp   Q: naar CP/M");
}

/* Text-mode start screen; returns the chosen level (the colour is left in
 * `human`), START_DEMO for a demo game, or 0 to leave the program. */
unsigned char start_screen(void)
{
    unsigned char key;
    draw_start_screen();
    for (;;) {
        key = wait_key(draw_start_screen);
        cpu_seed ^= entropy();
        if (key >= 'a' && key <= 'z')
            key -= 'a' - 'A';
        if (key >= '1' && key <= '3')
            return key - '0';
        switch (key) {
        case 'W': human = WHITE; show_colour(); break;
        case 'Z': human = BLACK; show_colour(); break;
        case 'D': return START_DEMO;
        case 'Q': return 0;
        case 'H':
            help_page();
            draw_start_screen();
            break;
        }
    }
}
