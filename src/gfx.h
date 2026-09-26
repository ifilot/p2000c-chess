/* SPDX-License-Identifier: GPL-3.0-only */
/* gfx.h -- loading the data file SCHAKEN.GFX. */
#ifndef GFX_H
#define GFX_H

#define GFX_CAPACITY (36 * 128)             /* room for the bitmaps (memory.asm) */

/* Finds SCHAKEN.GFX and reads the bitmaps into gfx[] and the title
 * picture's run-length data into the framebuffer; 0 if it cannot. */
extern unsigned char gfx_load(void);

#endif
