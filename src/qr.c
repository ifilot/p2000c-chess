/* SPDX-License-Identifier: GPL-3.0-only */
/* qr.c -- QR code encoder (ISO/IEC 18004), cut down to one symbol.
 *
 * Version 5 (37 x 37 modules) at error correction level L has a single
 * Reed-Solomon block of 108 data and 26 error correction codewords, so
 * there is no interleaving, and no version information (only from version
 * 7 on). The text goes in byte mode. The mask is fixed (pattern 0, a
 * checkerboard) instead of chosen by the penalty rules, which only improve
 * the odds for poor scanners; tools/test_qr.py decodes the result.
 *
 * The matrix is one byte per module: bit 0 dark, bit 1 part of a function
 * pattern (finders, timing, alignment, format), which the data skips.
 */
#include <string.h>
#include "qr.h"

#define DATA_CODEWORDS 108
#define ECC_CODEWORDS  26
#define DARK           1
#define FUNCTION       2
#define LEVEL_L        1                    /* format bits of error correction level L */
#define MASK_PATTERN   0                    /* (row + col) even: module inverted */

static unsigned char *matrix;

static void set(unsigned char row, unsigned char col, unsigned char dark)
{
    matrix[row * QR_SIZE + col] = FUNCTION | dark;
}

/* Product in GF(256) modulo x^8 + x^4 + x^3 + x^2 + 1. */
static unsigned char gf_mul(unsigned char x, unsigned char y)
{
    unsigned char z = 0;
    while (y) {
        if (y & 1)
            z ^= x;
        x = (x & 0x80) ? (unsigned char)((x << 1) ^ 0x1D) : (unsigned char)(x << 1);
        y >>= 1;
    }
    return z;
}

/* A square of rings around (row, col): dark where the ring's distance is
 * not in `light` (a bit per distance); cut off at the symbol's edge. */
static void rings(unsigned char row, unsigned char col, unsigned char radius, unsigned char light)
{
    unsigned char r, c, dr, dc, end = 2 * radius + 1, i, j;
    for (i = 0; i < end; i++)
        for (j = 0; j < end; j++) {
            r = row - radius + i;           /* off the symbol when it wraps below 0 */
            c = col - radius + j;
            if (r >= QR_SIZE || c >= QR_SIZE)
                continue;
            dr = i > radius ? i - radius : radius - i;
            dc = j > radius ? j - radius : radius - j;
            set(r, c, !((light >> (dr > dc ? dr : dc)) & 1));
        }
}

static void function_patterns(void)
{
    unsigned char i;
    unsigned int data = (LEVEL_L << 3) | MASK_PATTERN, bits;

    for (i = 0; i < QR_SIZE; i++) {         /* timing lines */
        set(6, i, !(i & 1));
        set(i, 6, !(i & 1));
    }
    rings(3, 3, 4, 0x14);                   /* finders with their light separators */
    rings(3, QR_SIZE - 4, 4, 0x14);
    rings(QR_SIZE - 4, 3, 4, 0x14);
    rings(30, 30, 2, 0x02);                 /* the one alignment pattern */

    /* format: 5 bits, a BCH(15,5) remainder, XOR 5412h; stored twice */
    bits = data;
    for (i = 0; i < 10; i++)
        bits = (bits << 1) ^ ((bits >> 9) * 0x537);
    bits = ((data << 10) | bits) ^ 0x5412;
    for (i = 0; i < 15; i++, bits >>= 1) {
        if (i < 6)
            set(i, 8, bits & 1);
        else if (i < 8)
            set(i + 1, 8, bits & 1);
        else
            set(8, i == 8 ? 7 : 14 - i, bits & 1);
        if (i < 8)
            set(8, QR_SIZE - 1 - i, bits & 1);
        else
            set(QR_SIZE - 15 + i, 8, bits & 1);
    }
    set(QR_SIZE - 8, 8, 1);                 /* the dark module */
}

/* Byte-mode segment, terminator and padding, then the error correction. */
static void codewords(const char *text, unsigned char len, unsigned char *cw)
{
    unsigned char *ecc = cw + DATA_CODEWORDS, *divisor = ecc + ECC_CODEWORDS;
    unsigned char i, j, prev, next, factor, root;

    /* mode 0100, count, the bytes, terminator 0000: whole nibbles */
    cw[0] = 0x40 | (len >> 4);
    prev = len;
    for (i = 0; i <= len; i++) {
        next = i < len ? text[i] : 0;
        cw[i + 1] = (unsigned char)(prev << 4) | (next >> 4);
        prev = next;
    }
    for (i = len + 2; i < DATA_CODEWORDS; i++)
        cw[i] = (i - len) & 1 ? 0x11 : 0xEC;

    /* generator: the product of (x - 2^k) for k < ECC_CODEWORDS */
    memset(divisor, 0, ECC_CODEWORDS);
    divisor[ECC_CODEWORDS - 1] = 1;
    root = 1;
    for (i = 0; i < ECC_CODEWORDS; i++) {
        for (j = 0; j < ECC_CODEWORDS; j++) {
            divisor[j] = gf_mul(divisor[j], root);
            if (j + 1 < ECC_CODEWORDS)
                divisor[j] ^= divisor[j + 1];
        }
        root = gf_mul(root, 2);
    }
    /* remainder of the data polynomial */
    memset(ecc, 0, ECC_CODEWORDS);
    for (i = 0; i < DATA_CODEWORDS; i++) {
        factor = cw[i] ^ ecc[0];
        memmove(ecc, ecc + 1, ECC_CODEWORDS - 1);
        ecc[ECC_CODEWORDS - 1] = 0;
        for (j = 0; j < ECC_CODEWORDS; j++)
            ecc[j] ^= gf_mul(divisor[j], factor);
    }
}

void qr_encode(const char *text, unsigned char len, unsigned char *work)
{
    unsigned char *cw = work + QR_SIZE * QR_SIZE, *m;
    signed char right;
    unsigned char vert, row, col, upward, j;
    unsigned int bit = 0;

    if (len > QR_MAX_TEXT)
        len = QR_MAX_TEXT;
    matrix = work;
    memset(work, 0, QR_SIZE * QR_SIZE);
    function_patterns();
    codewords(text, len, cw);

    /* two-column strips from the right, alternately up and down, the
     * vertical timing line skipped; each module masked as it is placed */
    for (right = QR_SIZE - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        upward = !((right + 1) & 2);
        for (vert = 0; vert < QR_SIZE; vert++) {
            row = upward ? QR_SIZE - 1 - vert : vert;
            for (j = 0; j < 2; j++) {
                col = right - j;
                m = matrix + row * QR_SIZE + col;
                if (*m & FUNCTION)
                    continue;
                if (bit < (DATA_CODEWORDS + ECC_CODEWORDS) * 8) {
                    if (cw[bit >> 3] & (0x80 >> (bit & 7)))
                        *m = DARK;
                    bit++;
                }
                if (!((row + col) & 1))
                    *m ^= DARK;
            }
        }
    }
}
