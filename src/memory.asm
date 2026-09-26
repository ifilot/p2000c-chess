; SPDX-License-Identifier: GPL-3.0-only
; memory.asm -- the big buffers, placed past the end of the program.
;
; z88dk writes uninitialised data into the .COM file as zeros, so buffers
; declared the usual way would make the file some 30 KB longer. These are
; instead given addresses in the free memory after the program's own data
; (__BSS_END_tail), up to __arena_end, below the stack (which starts at the
; BDOS entry and grows down; the Makefile checks the margin). Their contents
; start undefined: the program fills every one before use (chess_init(),
; the loader in gfx.c, the title picture and draw_board()).
;
; The sizes mirror chess.h (MOVE_STACK, MAX_HIST, sizeof(move_t) = 4,
; sizeof(undo_t) = 13) and gfx.h (GFX_CAPACITY).

SECTION code_user

EXTERN __BSS_END_tail

PUBLIC _board
PUBLIC _castle_keep
PUBLIC _framebuffer
PUBLIC _moves
PUBLIC _hist
PUBLIC _psq_tab
PUBLIC _zobrist
PUBLIC _zobrist_castle
PUBLIC _zobrist_ep
PUBLIC _gfx
PUBLIC __arena_end

; The board is page aligned (rules.asm reaches a square with `ld l,sq`);
; castle_keep fills the rest of its page. (z80asm's ALIGN only aligns within
; a module's share of a section, so the page is computed here instead.)
defc _board          = (__BSS_END_tail + 255) & 0xFF00
defc _castle_keep    = _board + 128
defc _framebuffer    = _board + 256            ; 512x252 dots: 16128 bytes
defc _moves          = _framebuffer + 16128    ; MOVE_STACK (900) moves of 4 bytes
defc _hist           = _moves + 900 * 4        ; MAX_HIST (348) records of 13 bytes
defc _psq_tab        = _hist + 348 * 13        ; int [12][64]
defc _zobrist        = _psq_tab + 12 * 64 * 2  ; unsigned int [12][64]
defc _zobrist_castle = _zobrist + 12 * 64 * 2  ; unsigned int [16]
defc _zobrist_ep     = _zobrist_castle + 32    ; unsigned int [8]
defc _gfx            = _zobrist_ep + 16        ; GFX_CAPACITY: the bitmaps from SCHAKEN.GFX
defc __arena_end     = _gfx + 36 * 128
