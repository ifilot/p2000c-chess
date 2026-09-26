/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.c -- the text-mode screens, the title picture and the FEN page.
 *
 * The start and help screens use the plain 80x24 text mode (instant); the
 * title picture is a 512x252 bitmap whose run-length data gfx.c reads from
 * SCHAKEN.GFX into the start of the framebuffer. It is unpacked in place
 * once, backwards, and uploaded sparsely. The FEN page shows the position
 * as text and as a QR code, which goes to the terminal line by line
 * without passing through the framebuffer, so the board picture there
 * stays intact for the way back.
 */
#include <string.h>
#include "video.h"
#include "chess.h"
#include "cpu.h"
#include "game.h"
#include "screen.h"
#include "screens.h"
#include "saver.h"
#include "splash.h"
#include "qr.h"
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
    "  F                     stelling als FEN-tekst en QR-code (om te analyseren)",
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

/* --- the position as FEN and QR code ---------------------------------------- */

#define QR_QUIET   4                        /* light modules round the code */
#define QR_DOTS    6                        /* a module is 6 dots by 4 lines: square on the CRT */
#define QR_LINES   4
#define QR_MODULES (QR_SIZE + 2 * QR_QUIET)
#define QR_BYTES   ((QR_MODULES * QR_DOTS + 7) / 8)
#define QR_TOP     ((FB_LINES - QR_MODULES * QR_LINES) / 2)
#define QR_COLUMN  1                        /* byte column of the left edge */
#define FEN_COL    36                       /* the text, right of the code */
#define FEN_WIDTH  28

/* The QR matrix, a line of dots and the FEN go in the move stack above the
 * player's legal moves (never more than 218); the search is idle meanwhile. */
#define QR_BUFFER  ((unsigned char *)(moves + 256))
#define QR_ROW     (QR_BUFFER + QR_WORK)
#define FEN_TEXT   ((char *)QR_ROW + QR_BYTES)
typedef char qr_buffer_fits[256 * 4 + QR_WORK + QR_BYTES + FEN_MAX + 1 <= MOVE_STACK * 4 ? 1 : -1];

/* The FEN over several lines, broken after a '/' or a space where it can. */
static void put_fen(void)
{
    const char *p = FEN_TEXT;
    unsigned char row = 5, n, cut;
    while (*p) {
        n = (unsigned char)strlen(p);
        cut = n;
        if (n > FEN_WIDTH) {
            cut = FEN_WIDTH;
            while (cut && p[cut - 1] != '/' && p[cut - 1] != ' ')
                cut--;
            if (!cut)
                cut = FEN_WIDTH;
        }
        con_at(ROWCOL(row++, FEN_COL));
        while (cut--)
            conout(*p++);
    }
}

/* Dark modules unlit, light ones and the quiet zone lit, as on paper. */
static void send_qr(void)
{
    unsigned char *line = QR_ROW, *out, bit, row, col, k, light;
    const unsigned char *modules;
    for (row = 0; row < QR_MODULES; row++) {
        modules = row >= QR_QUIET && row < QR_QUIET + QR_SIZE ? QR_BUFFER + (row - QR_QUIET) * QR_SIZE : 0;
        memset(line, 0, QR_BYTES);
        out = line;
        bit = 0x80;
        for (col = 0; col < QR_MODULES; col++) {
            light = !(modules && col >= QR_QUIET && col < QR_QUIET + QR_SIZE && (modules[col - QR_QUIET] & 1));
            for (k = 0; k < QR_DOTS; k++) {
                if (light)
                    *out |= bit;
                if (!(bit >>= 1)) {
                    bit = 0x80;
                    out++;
                }
            }
        }
        for (k = 0; k < QR_LINES; k++)
            video_send_row(line, COLROW(QR_COLUMN, QR_TOP + row * QR_LINES + k), QR_BYTES);
        key_watch();
    }
}

static void draw_fen_page(void)
{
    video_text();                           /* leaving graphics mode clears the picture */
    video_graphics();
    con_at(ROWCOL(3, FEN_COL));  con_puts("STELLING IN FEN-NOTATIE");
    put_fen();
    con_at(ROWCOL(11, FEN_COL)); con_puts("Deze tekst staat ook in de");
    con_at(ROWCOL(12, FEN_COL)); con_puts("QR-code: scan hem met een");
    con_at(ROWCOL(13, FEN_COL)); con_puts("telefoon en plak hem in een");
    con_at(ROWCOL(14, FEN_COL)); con_puts("schaakprogramma om de");
    con_at(ROWCOL(15, FEN_COL)); con_puts("stelling te analyseren.");
    con_at(ROWCOL(18, FEN_COL)); con_puts("Even geduld...");
    send_qr();
    con_at(ROWCOL(18, FEN_COL)); con_puts("Druk op een toets.");
}

/* The position as FEN and QR code; any key returns to the board. */
void fen_screen(void)
{
    qr_encode(FEN_TEXT, chess_fen(FEN_TEXT), QR_BUFFER);
    draw_fen_page();
    wait_key(draw_fen_page);
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

#define SPLASH_PROMPT "Druk op een toets om verder te gaan"

/* The prompt appears once the picture is complete, in the dark band under
 * the subtitle (text row 5, lines 60-71). */
static void draw_splash(void)
{
    video_graphics();
    flush_sparse();
    con_at(ROWCOL(5, (64 - (sizeof SPLASH_PROMPT - 1)) / 2));
    con_puts(SPLASH_PROMPT);
}

/* Title picture in graphics mode with a prompt; returns after any key. */
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
