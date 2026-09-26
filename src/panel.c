/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.c -- the text panel: title, players, clock, status, notes, move list.
 *
 * Every line is padded to the panel's 14 columns so a shorter text
 * overwrites a longer one; the bottom row is never written up to its last
 * column (that would scroll the text plane). The status line is written
 * last after every update, so it doubles as a display-complete marker for
 * the tests. Moves are listed in the long notation with the Dutch piece
 * letters (K koning, D dame, T toren, L loper, P paard): Pg1-f3, e4xd5, O-O.
 */
#include "video.h"
#include "chess.h"
#include "cpu.h"
#include "game.h"
#include "panel.h"
#include "clock.h"

#define CLOCK_MAX 35999u                    /* 9:59:59 */

unsigned char ply_note[GAME_MAX];

static const char PIECE_LETTER[7] = { 0, 0, 'P', 'L', 'T', 'D', 'K' };

static char line[PANEL_WIDTH + 1];

/* Writes text at a panel row, cut or padded to the panel width (one short
 * on the bottom row: writing the plane's last cell scrolls it). */
static void put_row(unsigned char row, const char *text)
{
    unsigned char n = row == ROW_KEYS ? 1 : 0;
    con_at(ROWCOL(row, PANEL_COL));
    while (*text && n < PANEL_WIDTH) {      /* longer would wrap into the board */
        conout(*text++);
        n++;
    }
    while (n++ < PANEL_WIDTH)
        conout(' ');
}

static char *put_number(char *p, unsigned int n, unsigned char width)
{
    char digits[5];
    unsigned char k = 0;
    do {
        digits[k++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (width > k) {
        *p++ = ' ';
        width--;
    }
    while (k)
        *p++ = digits[--k];
    return p;
}

static char *put_square(char *p, unsigned char sq)
{
    *p++ = 'a' + FILE_OF(sq);
    *p++ = '1' + RANK_OF(sq);
    return p;
}

const char *name_of(unsigned char colour)
{
    return colour == WHITE ? "Wit" : "Zwart";
}

void show_move_number(void)
{
    char *p = line;
    p = put_number(p, hist_len / 2 + 1, 3);
    *p = 0;
    con_at(ROWCOL(ROW_MOVE, PANEL_COL + 6));
    con_puts(line);
}

/* "Tijd  0:12:34": hours, minutes and seconds, capped at 9:59:59. */
void show_clock(void)
{
    unsigned int s = clock_seconds(), h, m;
    if (!clock_available)
        return;
    if (s > CLOCK_MAX)
        s = CLOCK_MAX;
    h = s / 3600;
    m = (s / 60) % 60;
    s %= 60;
    con_at(ROWCOL(ROW_CLOCK, PANEL_COL + 5));
    conout('0' + h); conout(':');
    conout('0' + m / 10); conout('0' + m % 10); conout(':');
    conout('0' + s / 10); conout('0' + s % 10);
}

/* One ply in long notation, White's with the move number: " 12. Pg1-f3+". */
static void format_ply(unsigned int k)
{
    const undo_t *u = &hist[k];
    unsigned char note = ply_note[k], type = note & 7;
    char *p = line;

    if (!(k & 1)) {
        p = put_number(p, k / 2 + 1, 3);
        *p++ = '.';
    } else {
        *p++ = ' ';
        *p++ = ' ';
        *p++ = ' ';
        *p++ = ' ';
    }
    *p++ = ' ';
    if (u->move.flags & MF_CASTLE) {
        *p++ = 'O'; *p++ = '-'; *p++ = 'O';
        if (FILE_OF(u->move.to) == 2) {
            *p++ = '-'; *p++ = 'O';
        }
    } else {
        if (PIECE_LETTER[type])
            *p++ = PIECE_LETTER[type];
        p = put_square(p, u->move.from);
        *p++ = u->captured ? 'x' : '-';
        p = put_square(p, u->move.to);
        if (u->move.flags & MF_PROMO)
            *p++ = PIECE_LETTER[u->move.flags & MF_PROMO];
    }
    if (note & NOTE_MATE)
        *p++ = '#';
    else if (note & NOTE_CHECK)
        *p++ = '+';
    *p = 0;
}

/* The last four moves of each side, a line per ply, White's first. */
void show_moves(void)
{
    unsigned int first = 0, k;
    unsigned char row;
    if (hist_len > LIST_LINES)
        first = (hist_len - LIST_LINES + 1) & ~1u;
    for (row = 0, k = first; row < LIST_LINES; row++, k++) {
        if (k < hist_len) {
            format_ply(k);
            put_row(ROW_LIST + row, line);
        } else
            put_row(ROW_LIST + row, "");
    }
}

/* The note row is remembered so the help screen can restore it. */
static const char *note_text = "";

void show_note(const char *note)
{
    note_text = note;
    put_row(ROW_NOTE, note);
}

void restore_note(void)
{
    put_row(ROW_NOTE, note_text);
}

void show_status(const char *status)
{
    put_row(ROW_STATUS, status);
}

static void status_with_name(const char *tail)
{
    char *p = line;
    const char *s = name_of(side);
    while (*s)
        *p++ = *s++;
    while (*tail)
        *p++ = *tail++;
    *p = 0;
    show_status(line);
}

void show_thinking(void)
{
    status_with_name(" denkt...");
}

void announce_turn(void)
{
    status_with_name(" aan zet");
}

void announce_result(void)
{
    switch (game_over) {
    case OVER_MATE:
        show_note(side == WHITE ? "Zwart wint" : "Wit wint");
        show_status("Schaakmat!");
        break;
    case OVER_STALEMATE:
        show_note("Remise");
        show_status("Pat");
        break;
    case OVER_FIFTY:
        show_note("50-zettenregel");
        show_status("Remise");
        break;
    case OVER_REPEAT:
        show_note("3x herhaling");
        show_status("Remise");
        break;
    case OVER_MATERIAL:
        show_note("Te weinig stuk");
        show_status("Remise");
        break;
    default:
        show_note("Partij te lang");
        show_status("Remise");
        break;
    }
}

/* Static texts, then the variable ones (the status line is left to the caller). */
void draw_panel(void)
{
    char *p;
    put_row(0, "S C H A K E N");
    put_row(1, "Philips P2000C");
    if (demo) {
        put_row(3, "Wit   niv. 2");
        put_row(4, "Zwart niv. 3");
        put_row(ROW_LEVEL, "DEMO");
        put_row(ROW_KEYS, "Toets: stop");
    } else {
        put_row(3, human == WHITE ? "Wit   u" : "Wit   P2000C");
        put_row(4, human == WHITE ? "Zwart P2000C" : "Zwart u");
        p = line;
        p[0] = 'N'; p[1] = 'i'; p[2] = 'v'; p[3] = 'e'; p[4] = 'a'; p[5] = 'u'; p[6] = ' ';
        p[7] = '0' + cpu_level;
        p[8] = 0;
        put_row(ROW_LEVEL, line);
        put_row(ROW_KEYS, "H hulp  F FEN");
    }
    put_row(ROW_MOVE, "Zet");
    put_row(ROW_CLOCK, clock_available ? "Tijd" : "");
    show_move_number();
    show_clock();
    show_moves();
    restore_note();
}
