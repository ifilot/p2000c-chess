/* SPDX-License-Identifier: GPL-3.0-only */
/* saver.h -- waiting for a key with a CRT screen saver.
 *
 * Every place that waits for the player calls wait_key() instead of conin().
 * After SAVER_SECONDS without a key the screen is switched to text mode
 * (which blanks the picture) and a small, quarter-bright caption wanders
 * over the black screen; any key ends the saver, is consumed, and the
 * caller's redraw function restores the screen before waiting continues.
 * A key that arrives twice (see saver.c) is only reported once. */
#ifndef SAVER_H
#define SAVER_H

#define SAVER_SECONDS 300                   /* five minutes of inactivity */

extern unsigned char wait_key(void (*redraw)(void));

/* Like wait_key(), but calls idle() about 40 times a second while waiting
 * (the game uses it to keep its clock display current). idle may be 0. */
extern unsigned char wait_key_idle(void (*redraw)(void), void (*idle)(void));

/* A key already waiting, or 0, with the same filter for doubled keys. */
extern unsigned char next_key(void);

/* Tells the filter about a key read with conin() directly. */
extern void key_taken(unsigned char key);

/* Notes the arrival time of a key that comes in while the program is busy
 * (drawing, thinking); cheap, to be called every few tens of milliseconds. */
extern void key_watch(void);

#endif
