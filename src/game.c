/* SPDX-License-Identifier: GPL-3.0-only */
/* game.c -- game state, the player's cursor interface, the computer's turn,
 * take-backs and the demo game.
 *
 * The player moves a cursor over the board. Resting on one of their own
 * pieces, the squares it can go to are marked at once (a dot, or corner
 * wedges on a piece it can take); RETURN picks the piece up (a frame) and
 * RETURN on a marked square plays the move. RETURN on the piece again or
 * ESC puts it down, RETURN on another own piece picks that one up instead.
 * Only legal moves are ever marked, so castling is simply the king's move
 * two squares and en passant the pawn's diagonal step. A promoting pawn
 * asks for the piece in the panel.
 *
 * Keys typed faster than the screen can follow are all applied before the
 * board is brought up to date, so the cursor never lags behind; a key that
 * arrives twice while the screen is being drawn counts once (saver.c). The
 * status line is always written last after every change, so it doubles as
 * a display-complete marker for the tests.
 */
#include <string.h>
#include "video.h"
#include "chess.h"
#include "cpu.h"
#include "game.h"
#include "screen.h"
#include "panel.h"
#include "screens.h"
#include "saver.h"
#include "clock.h"

unsigned char human = WHITE;
unsigned char flipped;
unsigned char cursor = NO_SQ;
unsigned char marks[64];
unsigned char game_over;
unsigned char demo;

static unsigned char selected = NO_SQ;      /* the picked-up piece's square */
static unsigned int legal_end;              /* the player's legal moves: moves[0..legal_end) */

#define DEMO_WHITE_LEVEL 2
#define DEMO_BLACK_LEVEL 3
#define DEMO_PAUSE       6000               /* pause_or_key() iterations, about a second at 4 MHz */

/* Cursor keys. The P2000C keyboard's cursor quadrant emits the WordStar
 * diamond (^S ^D ^E ^X, as P2EDIT and SuperCalc expect); the graphical
 * emulator sends the terminal's own cursor-control bytes instead, so both
 * sets are accepted. */
#define KEY_LEFT   0x13                     /* ^S */
#define KEY_RIGHT  0x04                     /* ^D */
#define KEY_UP     0x05                     /* ^E */
#define KEY_DOWN   0x18                     /* ^X */
#define KEY_LEFT2  0x15
#define KEY_RIGHT2 0x06
#define KEY_UP2    0x1A
#define KEY_DOWN2  0x0A
#define KEY_CR     0x0D
#define KEY_BS     0x08
#define KEY_ESC    0x1B
#define KEY_DEL    0x7F
#define BEL        0x07

/* --- state ------------------------------------------------------------------------ */

/* Idle hook while waiting for the player: keeps the clock display current. */
static void tick_clock(void)
{
    if (clock_update())
        show_clock();
}

/* Decides whether the game is over after a move (or a take-back); for the
 * side to move, leaves its legal moves in moves[0..legal_end). */
static void check_end(void)
{
    legal_end = gen_legal(0);
    game_over = 0;
    if (legal_end == 0)
        game_over = in_check(side) ? OVER_MATE : OVER_STALEMATE;
    else if (halfmove >= 100)
        game_over = OVER_FIFTY;
    else if (repetitions() >= 2)
        game_over = OVER_REPEAT;
    else if (insufficient_material())
        game_over = OVER_MATERIAL;
    else if (hist_len >= GAME_MAX)
        game_over = OVER_LENGTH;
    if (game_over)
        clock_freeze();
}

/* Is it the player's turn to pick a move? */
static unsigned char players_turn(void)
{
    return !game_over && !demo && side == human;
}

/* Target and selection marks for the piece picked up, or else the own piece under the cursor. */
static void update_marks(void)
{
    unsigned int i;
    unsigned char from = selected;
    memset(marks, 0, sizeof marks);
    if (!players_turn())
        return;
    if (from == NO_SQ && cursor != NO_SQ && (board[cursor] & human))
        from = cursor;
    if (from == NO_SQ)
        return;
    for (i = 0; i < legal_end; i++)
        if (moves[i].from == from)
            marks[SQ64(moves[i].to)] |= MARK_TARGET;
    if (selected != NO_SQ)
        marks[SQ64(selected)] |= MARK_SELECT;
}

/* Board and markers up to date. */
static void refresh_board(void)
{
    update_marks();
    sync_cells();
}

/* The status line after a move: the result, or whose turn it is. */
static void announce(void)
{
    if (game_over)
        announce_result();
    else
        announce_turn();
}

/* Plays a legal move for the side to move, records it and shows it. */
static void do_move(const move_t *m)
{
    unsigned int k = hist_len;
    unsigned char type = TYPE(board[m->from]);
    make_move(m);
    check_end();
    ply_note[k] = type | (game_over == OVER_MATE ? NOTE_MATE : in_check(side) ? NOTE_CHECK : 0);
    selected = NO_SQ;
    refresh_board();
    show_move_number();
    show_moves();
    clock_update();                         /* (a no-op once frozen) the final time shows too */
    show_clock();
    show_note(game_over ? "" : (ply_note[k] & NOTE_CHECK) ? "Schaak!" : "");
}

/* The computer moves for the side to move. */
static void cpu_move(void)
{
    move_t m;
    show_thinking();
    cpu_choose(&m);
    do_move(&m);
#ifdef SEARCH_STATS
    {
        static char stats[15];
        unsigned int n = cpu_nodes;
        unsigned char i = 14;
        stats[i] = 0;
        do { stats[--i] = '0' + n % 10; n /= 10; } while (n && i > 3);
        stats[--i] = ' ';
        stats[--i] = '0' + cpu_depth;
        stats[--i] = 'd';
        show_note(stats + i);
    }
#endif
}

/* The computer's reply (if it is its turn), then the status line. */
static void cpu_turn(void)
{
    if (!game_over && side != human)
        cpu_move();
    announce();
}

/* --- the player's move ---------------------------------------------------------------- */

/* Nonzero when the piece on sq has a legal move. */
static unsigned char can_move(unsigned char sq)
{
    unsigned int i;
    for (i = 0; i < legal_end; i++)
        if (moves[i].from == sq)
            return 1;
    return 0;
}

static void redraw_promotion(void)
{
    redraw_game_screen();
    show_status("Promoveer tot:");
}

/* Asks for the promotion piece: D T L P, RETURN for a queen, ESC to cancel. */
static unsigned char ask_promotion(void)
{
    unsigned char key;
    show_note("D T L P  RET=D");
    show_status("Promoveer tot:");
    for (;;) {
        key = wait_key_idle(redraw_promotion, tick_clock);
        if (key >= 'a' && key <= 'z')
            key -= 'a' - 'A';
        switch (key) {
        case KEY_CR: case ' ': case 'D': key = QUEEN;  break;
        case 'T': key = ROOK;   break;
        case 'L': key = BISHOP; break;
        case 'P': key = KNIGHT; break;
        case KEY_ESC:           key = 0; break;
        default:
            conout(BEL);
            continue;
        }
        show_note("");
        announce_turn();
        return key;
    }
}

/* Plays the picked-up piece to the cursor; returns 0 if that is no legal move. */
static unsigned char play_selected(void)
{
    unsigned int i;
    unsigned char promo = 0;
    move_t m;
    for (i = 0; i < legal_end; i++) {
        if (moves[i].from != selected || moves[i].to != cursor)
            continue;
        if (moves[i].flags & MF_PROMO) {
            if (!promo && !(promo = ask_promotion()))
                return 1;                   /* cancelled: the piece stays picked up */
            if ((moves[i].flags & MF_PROMO) != promo)
                continue;
        }
        m = moves[i];                       /* moves[] is about to be reused */
        show_status("Even geduld...");
        do_move(&m);
        cpu_turn();
        refresh_board();
        return 1;
    }
    return 0;
}

/* RETURN or space: pick up, put down, or move. */
static void action(void)
{
    if (!players_turn()) {
        conout(BEL);
        return;
    }
    if (board[cursor] & human) {
        if (cursor == selected) {
            selected = NO_SQ;               /* put it down again */
            show_note("");
        }
        else if (can_move(cursor)) {
            selected = cursor;
            show_note("Kies het veld");
        } else {
            show_note("Geen zetten");
            conout(BEL);
        }
        return;
    }
    if (selected == NO_SQ || !play_selected())
        conout(BEL);
}

static void move_cursor(signed char right, signed char down)
{
    signed char file = FILE_OF(cursor), rank = RANK_OF(cursor);
    if (flipped) {
        file -= right;
        rank += down;
    } else {
        file += right;
        rank -= down;
    }
    if (file < 0 || file > 7 || rank < 0 || rank > 7)
        return;
    cursor = SQ(file, rank);
}

/* Takes back the player's last move and the computer's reply. */
static void take_back(void)
{
    unsigned char mover;
    unsigned int k;
    for (k = hist_len; k > 0; k--)          /* a move of the player's to take back? */
        if (((k - 1) & 1 ? BLACK : WHITE) == human)
            break;
    if (demo || k == 0) {
        conout(BEL);
        return;
    }
    do {
        mover = (hist_len - 1) & 1 ? BLACK : WHITE;
        unmake_move();
    } while (mover != human);
    clock_resume();
    check_end();
    selected = NO_SQ;
    refresh_board();
    show_move_number();
    show_moves();
    show_note("Teruggenomen");
    announce();
}

/* --- screens --------------------------------------------------------------------------- */

void redraw_game_screen(void)
{
    video_graphics();
    draw_panel();
    flush_frame();
    if (demo && !game_over)
        show_status("Demo gestopt");
    else
        announce();
}

static void new_game(void)
{
    chess_init();
    clock_reset();
    flipped = !demo && human == BLACK;
    selected = NO_SQ;
    cursor = demo ? NO_SQ : SQ(4, human == WHITE ? 1 : 6);    /* on the king's pawn */
    show_note("");
    check_end();
    update_marks();
    draw_board();
    video_graphics();
    draw_panel();
    flush_frame();
}

static void redraw_quit(void)
{
    redraw_game_screen();
    show_status("Stoppen? (J/N)");
}

/* "Stoppen? (J/N)": returns 1 when the player confirms. */
static unsigned char confirm_quit(void)
{
    unsigned char key;
    show_status("Stoppen? (J/N)");
    key = wait_key_idle(redraw_quit, tick_clock);
    if (key == 'j' || key == 'J' || key == 'y' || key == 'Y')
        return 1;
    announce();
    return 0;
}

/* Applies one key; returns 1 (new game), 0 (quit) or 2 (carry on). */
static unsigned char handle_key(unsigned char key)
{
    cpu_seed ^= entropy();                  /* the player's timing makes the games differ */
    if (key >= 'A' && key <= 'Z')
        key += 'a' - 'A';
    switch (key) {
    case KEY_LEFT:  case KEY_LEFT2:  case 'a': move_cursor(-1, 0); break;
    case KEY_RIGHT: case KEY_RIGHT2: case 'd': move_cursor(1, 0);  break;
    case KEY_UP:    case KEY_UP2:    case 'w': move_cursor(0, -1); break;
    case KEY_DOWN:  case KEY_DOWN2:  case 's': move_cursor(0, 1);  break;
    case KEY_CR:    case ' ':
        refresh_board();                    /* show the state being acted on */
        action();
        break;
    case KEY_ESC:
        if (selected != NO_SQ) {
            selected = NO_SQ;
            show_note("");
        }
        break;
    case 't': case KEY_BS: case KEY_DEL:
        take_back();
        break;
    case 'h':
        help_screen();
        break;
    case 'f':
        fen_screen();
        break;
    case 'n':
        return 1;
    case 'q':
        if (confirm_quit())
            return 0;
        break;
    }
    return 2;
}

/* One game; returns 1 to go back to the start screen, 0 to leave the program. */
unsigned char play(void)
{
    unsigned char result, key;

    demo = 0;
    new_game();
    if (side != human) {
        cpu_move();
        refresh_board();
    }
    announce();
    for (;;) {
        result = handle_key(wait_key_idle(redraw_game_screen, tick_clock));
        while (result == 2 && (key = next_key()) != 0)
            result = handle_key(key);
        if (result != 2)
            return result;
        refresh_board();
    }
}

/* --- demo: computer against computer ------------------------------------------------ */

/* Waits roughly a second (at 4 MHz); a key pressed meanwhile is consumed and reported. */
static unsigned char pause_or_key(void)
{
    unsigned int i;
    for (i = 0; i < DEMO_PAUSE; i++) {
        if (conready()) {
            key_taken(conin());
            return 1;
        }
        if ((i & 511) == 0)
            tick_clock();
    }
    return 0;
}

void demo_game(void)
{
    demo = 1;
    new_game();
    announce();
    while (!game_over) {
        cpu_level = side == WHITE ? DEMO_WHITE_LEVEL : DEMO_BLACK_LEVEL;
        cpu_move();
        announce();
        if (pause_or_key())
            break;
    }
    if (!game_over)
        show_status("Demo gestopt");
    wait_key_idle(redraw_game_screen, tick_clock);   /* the demo is over: the saver may run now */
    demo = 0;
}
