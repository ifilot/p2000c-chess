/* SPDX-License-Identifier: GPL-3.0-only */
/* qr.h -- a QR code for a short text (the FEN of the position).
 *
 * One fixed symbol: version 6 (41 x 41 modules), error correction level L,
 * byte mode, which holds up to 134 bytes, enough for the web page's address
 * followed by any FEN (FEN_MAX). */
#ifndef QR_H
#define QR_H

#define QR_SIZE     41                      /* modules per side */
#define QR_MAX_TEXT 134
#define QR_WORK     (QR_SIZE * QR_SIZE + 190)   /* the matrix, codewords, generator */

/* What the FEN page encodes: this address, then the FEN with '_' for each
 * space. The web page (site/ in the repository) draws the position. */
#define FEN_PAGE_URL "https://ifilot.github.io/p2000c-chess/#"

/* Encodes len bytes of text into work: afterwards work[row * QR_SIZE + col]
 * has bit 0 set for a dark module. work needs QR_WORK bytes. */
extern void qr_encode(const char *text, unsigned char len, unsigned char *work);

#endif
