/* SPDX-License-Identifier: GPL-3.0-only */
/* qr.h -- a QR code for a short text (the FEN of the position).
 *
 * One fixed symbol: version 5 (37 x 37 modules), error correction level L,
 * byte mode, which holds up to 106 bytes, enough for any FEN (FEN_MAX). */
#ifndef QR_H
#define QR_H

#define QR_SIZE     37                      /* modules per side */
#define QR_MAX_TEXT 106
#define QR_WORK     (QR_SIZE * QR_SIZE + 160)   /* the matrix, codewords, generator */

/* Encodes len bytes of text into work: afterwards work[row * QR_SIZE + col]
 * has bit 0 set for a dark module. work needs QR_WORK bytes. */
extern void qr_encode(const char *text, unsigned char len, unsigned char *work);

#endif
