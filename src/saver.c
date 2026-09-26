/* SPDX-License-Identifier: GPL-3.0-only */
/* saver.c -- CRT screen saver.
 *
 * Idle time is taken from the BIOS 60 Hz tick counter when clock_probe()
 * found one; otherwise it is estimated by counting keyboard polls, with
 * POLLS_PER_SECOND calibrated in the emulator for a bare polling loop (an
 * idle hook that does work per poll slows that estimate down, which is why
 * the tick counter is preferred).
 *
 * The saver itself lives in the 80x24 text mode with the cursor hidden, and
 * prints its caption with the quarter-bright attribute, so hardly any
 * phosphor is driven and never at one place for long.
 *
 * Every key the program takes goes through here, which also filters out
 * doubled keys. The terminal board finds keys through the video refresh
 * (the CRTC's light pen input) and tells a held key from a released one at
 * the vertical sync interrupts; while it is busy with picture data it can
 * take a key that is still held down for a new press, most of all in the
 * graphics mode. Such a copy comes while the finger is still on the key,
 * so the same key arriving within HOLD_TICKS (1/3 s, a normal press) of the
 * previous one is dropped. A key held longer repeats, about three times a
 * second. For the arrival times, key_watch() notes when a key comes in
 * while the program is drawing or thinking. Without the BIOS clock a copy
 * of the previous key is dropped if it was waiting when the program became
 * ready for the next key, or comes within ECHO_POLLS polls.
 *
 * Built with -DKEY_TRACE, every key is shown on the text plane (row 10,
 * column 50, the game's note row): its code, the ticks since the previous
 * key taken, and '-' when it was dropped.
 */
#include "video.h"
#include "saver.h"
#include "clock.h"

#define POLLS_PER_SECOND 20300              /* calibrated in the emulator: wait_key() polls per second at 4 MHz */
#define MOVE_SECONDS     3                  /* caption moves every few seconds */
#define ESC 27

static const char *const CAPTION[2] = { "S C H A K E N", "Philips P2000C" };

/* Quarter-bright attribute: intensity bits (6, 0) both zero; 40h is normal. */
static void set_brightness(unsigned char attribute)
{
    conout(ESC); conout('0'); conout(attribute);
}

static void caption_at(unsigned char row, unsigned char col)
{
    conout(12);                              /* form feed: clear screen, cursor home */
    con_at(ROWCOL(row, col));     con_puts(CAPTION[0]);
    con_at(ROWCOL(row + 1, col)); con_puts(CAPTION[1]);
}

/* Runs until a key is pressed; returns that key. */
static unsigned char screen_saver(void)
{
    unsigned char row = 5, col = 10;
    unsigned int polls;
    unsigned char seconds;

    video_text();
    conout(ESC); conout('c');
    set_brightness(0x00);
    for (;;) {
        caption_at(row, col);
        for (seconds = 0; seconds < MOVE_SECONDS; seconds++)
            for (polls = 0; polls < POLLS_PER_SECOND; polls++)
                if (conready()) {
                    set_brightness(0x40);
                    conout(12);
                    return conin();
                }
        row = (unsigned char)((row + 7) % 22);       /* a simple walk that covers the screen */
        col = (unsigned char)((col + 23) % 66);
    }
}

#define IDLE_EVERY 512                       /* polls between idle() calls and clock checks, ~25 ms */
#define SAVER_TICKS ((unsigned int)SAVER_SECONDS * CLOCK_TICKS_PER_SECOND)   /* 18000 < 65536 */
#define ECHO_POLLS (POLLS_PER_SECOND / 4)
#define HOLD_TICKS 20                        /* 1/3 s */

static unsigned char last_key;
static unsigned int last_arrival;            /* clock tick when last_key came in */
static unsigned char waiting;                /* key_watch() saw a key come in ... */
static unsigned int arrival;                 /* ... at this tick */

void key_watch(void)
{
    if (!waiting && conready()) {
        arrival = clock_ticks();
        waiting = 1;
    }
}

#ifdef KEY_TRACE
static void trace(unsigned char key, unsigned int ticks, unsigned char dropped)
{
    static const char HEX[] = "0123456789ABCDEF";
    char text[12], *p = text + 11;
    *p = 0;
    *--p = dropped ? '-' : ' ';
    *--p = ' ';
    if (ticks > 999)
        ticks = 999;
    do {
        *--p = '0' + ticks % 10;
        ticks /= 10;
    } while (ticks);
    *--p = '+';
    *--p = ' ';
    *--p = HEX[key & 15];
    *--p = HEX[key >> 4];
    con_at(ROWCOL(10, 50));
    con_puts(p);
    con_puts("   ");
}
#endif

/* Reads the waiting key; nonzero `fresh` says the program has been ready
 * for a while (without the clock: no copy of the previous key is dropped).
 * Returns the key, or 0 for a dropped copy. */
static unsigned char read_key(unsigned char fresh)
{
    unsigned int at = waiting ? arrival : clock_ticks();
    unsigned char key = conin(), copy;
    waiting = 0;
    copy = key == last_key &&
           (clock_available ? (unsigned int)(at - last_arrival) < HOLD_TICKS : !fresh);
#ifdef KEY_TRACE
    trace(key, at - last_arrival, copy);
#endif
    if (copy)
        return 0;
    last_key = key;
    last_arrival = at;
    return key;
}

unsigned char wait_key_idle(void (*redraw)(void), void (*idle)(void))
{
    unsigned int polls = 0, seconds = 0, quiet = 0, start = clock_ticks();
    unsigned char expired, key;
    for (;;) {
        if (conready()) {
            if ((key = read_key(quiet >= ECHO_POLLS)) != 0)
                return key;
            continue;
        }
        if (quiet < ECHO_POLLS)
            quiet++;
        expired = 0;
        if ((polls & (IDLE_EVERY - 1)) == 0) {
            if (idle)
                idle();
            if (clock_available && (unsigned int)(clock_ticks() - start) >= SAVER_TICKS)
                expired = 1;
        }
        if (++polls >= POLLS_PER_SECOND) {
            polls = 0;
            if (!clock_available && ++seconds >= SAVER_SECONDS)
                expired = 1;
        }
        if (expired) {
            key_taken(screen_saver());          /* the key only wakes the screen */
            redraw();
            start = clock_ticks();
            seconds = 0;
            quiet = 0;
        }
    }
}

unsigned char wait_key(void (*redraw)(void))
{
    return wait_key_idle(redraw, 0);
}

unsigned char next_key(void)
{
    unsigned char key;
    while (conready())
        if ((key = read_key(0)) != 0)
            return key;
    return 0;
}

void key_taken(unsigned char key)
{
    last_key = key;
    last_arrival = clock_ticks();
    waiting = 0;
}
