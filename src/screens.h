/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.h -- the text-mode screens (start, help) and the title picture. */
#ifndef SCREENS_H
#define SCREENS_H

#define START_DEMO 9                        /* start_screen() result for a demo game */

extern void text_clear(void);               /* clear the text screen, hide the cursor */
extern void splash_screen(void);            /* title picture in graphics mode; any key */
extern unsigned char start_screen(void);    /* level 1-3 (colour in `human`), START_DEMO, or 0 to quit */
extern void help_screen(void);              /* rules page from the game; restores the board */

#endif
