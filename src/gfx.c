/* SPDX-License-Identifier: GPL-3.0-only */
/* gfx.c -- loading the data file SCHAKEN.GFX.
 *
 * The bitmaps (pieces, markers, labels) and the title picture live in a
 * data file next to the program, which keeps SCHAKEN.COM small. The file is
 * src/sprites.bin followed by src/splash.rle, both whole 128-byte records;
 * the first part goes to gfx[], the second to the start of the framebuffer,
 * where screens.c unpacks it in place.
 *
 * CP/M 2.2 does not tell a program which drive it was loaded from, so the
 * file is looked for on the current drive first and then on every drive
 * CP/M has logged in (which includes the one SCHAKEN.COM came from); a
 * drive that was never used, such as an empty floppy drive, is never
 * touched.
 */
#include <string.h>
#include "video.h"
#include "gfx.h"
#include "sprites.h"
#include "splash.h"

#define BDOS_OPEN         15
#define BDOS_READ         20
#define BDOS_SET_DMA      26
#define BDOS_LOGIN_VECTOR 24
#define DEFAULT_DMA       0x80

/* compile-time check: the bitmaps fit the space memory.asm leaves them */
typedef char gfx_fits[GFX_RECORDS * 128 <= GFX_CAPACITY ? 1 : -1];

static unsigned char fcb[36];

/* Opens SCHAKEN.GFX on a drive (0 current, 1 A:, 2 B:, ...). */
static unsigned char open_on(unsigned char drive)
{
    memset(fcb, 0, sizeof fcb);
    fcb[0] = drive;
    memcpy(fcb + 1, "SCHAKEN GFX", 11);
    return (unsigned char)bdos(BDOS_OPEN, (unsigned int)fcb) != 0xFF;
}

static unsigned char read_records(unsigned char *to, unsigned char count)
{
    while (count--) {
        bdos(BDOS_SET_DMA, (unsigned int)to);
        if ((unsigned char)bdos(BDOS_READ, (unsigned int)fcb))
            return 0;
        to += 128;
    }
    return 1;
}

unsigned char gfx_load(void)
{
    unsigned char drive, found = open_on(0), ok;
    unsigned int logged = bdos(BDOS_LOGIN_VECTOR, 0), bit = 1;
    for (drive = 1; !found && drive <= 16; drive++, bit <<= 1)
        if (logged & bit)
            found = open_on(drive);
    if (!found)
        return 0;
    ok = read_records(gfx, GFX_RECORDS) && read_records(framebuffer, SPLASH_RECORDS);
    bdos(BDOS_SET_DMA, DEFAULT_DMA);
    return ok;
}
