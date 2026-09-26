; SPDX-License-Identifier: GPL-3.0-only
; movegen.asm -- the hot paths of the chess rules in Z80 assembly: move
; generation and the attack test (C versions of both, used by the native
; test tools, are in chess.c and produce the same moves in the same order).
;
; The board is page aligned, so a square's contents are one `ld l,sq` away
; with H holding the page; the upper half of the page holds castle_keep.
; The page lies past the program's data (memory.asm).
; Every generated move carries an ordering key: captures 100 + 8 * victim -
; attacker (most valuable victim first, then least valuable attacker), a
; queen promotion 95 more, all other moves 0.
;
; No IY and no alternate registers (the BIOS interrupt handler may use
; them); IX is saved and restored, as sdcc expects.
;
; Calling convention (Z88DK, sdcc_iy): stack arguments are 16-bit words with
; the leftmost argument at SP+2 on entry; __z88dk_fastcall passes one word in
; HL; 8-bit results return in L, 16-bit ones in HL.

SECTION code_user

PUBLIC _gen_moves
PUBLIC _attacked_fc
PUBLIC _make_move
PUBLIC _unmake_move

EXTERN _side
EXTERN _ep
EXTERN _castle
EXTERN _moves
EXTERN _hist
EXTERN _hist_len
EXTERN _halfmove
EXTERN _phase
EXTERN _score
EXTERN _hash
EXTERN _king_sq
EXTERN _mover_in_check
EXTERN _psq_tab
EXTERN _zobrist
EXTERN _zobrist_castle
EXTERN _zobrist_ep
EXTERN _aligned

EXTERN _board                           ; page aligned, see memory.asm
EXTERN _castle_keep

defc PAWN   = 1
defc KNIGHT = 2
defc BISHOP = 3
defc ROOK   = 4
defc QUEEN  = 5
defc KING   = 6
defc WHITE  = 0x08

defc NO_SQ  = 0xFF
defc ZOBRIST_SIDE = 0x5A3C              ; as in chess.c

defc MF_EP      = 0x08
defc MF_CASTLE  = 0x10
defc MF_DOUBLE  = 0x20
defc MF_CAPTURE = 0x40

; --- move generation --------------------------------------------------------------

; unsigned int gen_moves(unsigned int start, unsigned char captures_only)
; Appends the pseudo-legal moves of `side` to moves[] from index start and
; returns the new end. Registers while generating: C = from square,
; H = board page with L = target square, DE = next free move, B = loop count
; or scratch.
_gen_moves:
        push ix
        ld hl,4
        add hl,sp
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = start
        inc hl
        ld a,(hl)
        ld (gm_caps),a
        ex de,hl
        add hl,hl
        add hl,hl
        ld de,_moves
        add hl,de
        ex de,hl                ; DE = &moves[start]
        ld a,(_side)
        ld (gm_me),a
        xor 0x18
        ld (gm_them),a
        xor 0x18
        cp WHITE
        jr nz,gm_black
        ld a,0x10               ; White pawns: forward +16, start rank 2, promote from rank 7
        ld (gm_fwd),a
        ld (gm_start),a
        ld a,0x60
        ld (gm_prom),a
        jr gm_go
gm_black:
        ld a,0xF0               ; Black pawns: forward -16, start rank 7, promote from rank 2
        ld (gm_fwd),a
        ld a,0x60
        ld (gm_start),a
        ld a,0x10
        ld (gm_prom),a
gm_go:
        ld hl,_board
        ld c,0
gm_next_piece:
        ld a,(gm_me)
        ld b,a                  ; B = own colour while scanning
gm_square:
        ld l,c
        ld a,(hl)
        and b
        jr z,gm_next
        ld a,(hl)
        and 7
        ld (gm_att),a
        dec a
        jp z,gm_pawn
        dec a
        jp z,gm_knight
        dec a
        jr z,gm_bishop
        dec a
        jr z,gm_rook
        dec a
        jr z,gm_queen
        jp gm_king
gm_next:
        inc c
        bit 3,c
        jr z,gm_square
        ld a,c
        add a,8                 ; skip the off-board half of the rank
        ld c,a
        bit 7,a
        jr z,gm_square
        jr gm_end
gm_piece_done:                  ; after a piece: B was used, reload it
        inc c
        bit 3,c
        jr z,gm_next_piece
        ld a,c
        add a,8
        ld c,a
        bit 7,a
        jr z,gm_next_piece
gm_end:
        ex de,hl                ; return (DE - moves) / 4
        ld de,_moves
        or a
        sbc hl,de
        srl h
        rr l
        srl h
        rr l
        pop ix
        ret

; sliding pieces: IX = directions, B = how many
gm_bishop:
        ld ix,dirs_diag
        ld b,4
        jr gm_slide
gm_rook:
        ld ix,dirs_orth
        ld b,4
        jr gm_slide
gm_queen:
        ld ix,dirs_diag         ; followed by dirs_orth
        ld b,8
gm_slide:
        ld a,b
        ld (gm_count),a
gm_dir:
        ld b,(ix+0)
        inc ix
        ld l,c
gm_ray:
        ld a,l
        add a,b
        ld l,a
        and 0x88
        jr nz,gm_dir_next
        ld a,(hl)
        or a
        jr nz,gm_ray_hit
        ld a,(gm_caps)
        or a
        jr nz,gm_ray
        call emit_quiet
        jr gm_ray
gm_ray_hit:
        ld a,(gm_them)
        and (hl)
        call nz,emit_capture
gm_dir_next:
        ld a,(gm_count)
        dec a
        ld (gm_count),a
        jr nz,gm_dir
        jp gm_piece_done

; knight and king: eight offsets each
gm_knight:
        ld ix,dirs_knight
        jr gm_leaper
gm_king:
        ld ix,dirs_king
gm_leaper:
        ld b,8
gm_leap:
        ld a,(ix+0)
        inc ix
        add a,c
        ld l,a
        and 0x88
        jr nz,gm_leap_next
        ld a,(hl)
        or a
        jr z,gm_leap_empty
        ld a,(gm_them)
        and (hl)
        call nz,emit_capture
        jr gm_leap_next
gm_leap_empty:
        ld a,(gm_caps)
        or a
        call z,emit_quiet
gm_leap_next:
        djnz gm_leap
        ld a,(gm_att)
        cp KING
        jp nz,gm_piece_done

; castling: the king stands on its home square while the side has rights
        ld a,(gm_caps)
        or a
        jp nz,gm_piece_done
        ld a,(gm_me)
        cp WHITE
        ld a,(_castle)
        jr z,gm_have_rights
        rrca                    ; Black's rights (4, 8) down to bits 0 and 1
        rrca
gm_have_rights:
        and 3
        jp z,gm_piece_done
        ld (gm_rights),a
        ld a,(gm_them)
        ld b,a
        call attacked_cb        ; not out of check
        jp nz,gm_piece_done
        ld a,(gm_rights)
        rrca
        jr nc,gm_castle_q
        ld l,c                  ; king side: f and g empty, f not attacked
        inc l
        ld a,(hl)
        inc l
        or (hl)
        jr nz,gm_castle_q
        push bc
        inc c
        call attacked_cb        ; (pop keeps its flags; dec would not)
        pop bc
        jr nz,gm_castle_q
        ld a,c
        add a,2
        ld l,a
        ld a,MF_CASTLE
        call emit_flags
gm_castle_q:
        ld a,(gm_rights)
        and 2
        jp z,gm_piece_done
        ld l,c                  ; queen side: d, c and b empty, d not attacked
        dec l
        ld a,(hl)
        dec l
        or (hl)
        dec l
        or (hl)
        jp nz,gm_piece_done
        push bc
        dec c
        call attacked_cb
        pop bc
        jp nz,gm_piece_done
        ld a,c
        sub 2
        ld l,a
        ld a,MF_CASTLE
        call emit_flags
        jp gm_piece_done

; pawns: the push (or promotions), the double step, two captures
gm_pawn:
        ld a,(gm_fwd)
        add a,c
        ld l,a                  ; one square forward: always on the board
        ld a,(hl)
        or a
        jr nz,gm_pawn_caps
        ld a,c
        and 0x70
        ld b,a                  ; B = rank of the pawn
        ld a,(gm_prom)
        cp b
        jr nz,gm_pawn_push
        xor a
        ld (gm_key),a
        call gm_promos          ; A = 0: plain promotions
        jr gm_pawn_caps
gm_pawn_push:
        ld a,(gm_caps)
        or a
        jr nz,gm_pawn_caps
        call emit_quiet
        ld a,(gm_start)
        cp b
        jr nz,gm_pawn_caps
        ld a,(gm_fwd)
        add a,l
        ld l,a
        ld a,(hl)
        or a
        jr nz,gm_pawn_caps
        ld a,MF_DOUBLE
        call emit_flags
gm_pawn_caps:
        ld a,(gm_fwd)
        add a,c
        dec a
        call gm_pawn_take
        ld a,(gm_fwd)
        add a,c
        inc a
        call gm_pawn_take
        jp gm_piece_done

; a pawn capture towards square A (a piece, or en passant)
gm_pawn_take:
        ld l,a
        and 0x88
        ret nz
        ld a,(gm_them)
        and (hl)
        jr z,gm_pawn_ep
        call capture_key
        ld (gm_key),a
        ld a,c
        and 0x70
        ld b,a
        ld a,(gm_prom)
        cp b
        ld a,MF_CAPTURE
        jr z,gm_promos
        jr emit_key
gm_pawn_ep:
        ld a,(_ep)
        cp l
        ret nz
        ld a,100 + 8 * PAWN - PAWN
        ld (gm_key),a
        ld a,MF_EP | MF_CAPTURE
        jr emit_key

; the four promotions to L (only the queen when captures_only);
; A = flags without the piece, gm_key = key without the queen bonus
gm_promos:
        ld b,a
        ld a,(gm_key)
        push af
        add a,95
        ld (gm_key),a
        ld a,b
        or QUEEN
        call emit_key
        pop af
        ld (gm_key),a
        ld a,(gm_caps)
        or a
        ret nz
        ld a,b
        or KNIGHT
        call emit_key
        ld a,b
        or ROOK
        call emit_key
        ld a,b
        or BISHOP
        jr emit_key

; --- emitting a move (from C to L) into (DE); B, C and HL are preserved --------------

emit_quiet:
        xor a
emit_flags:                     ; A = flags, key 0
        ex de,hl
        ld (hl),c
        inc hl
        ld (hl),e
        inc hl
        ld (hl),a
        inc hl
        ld (hl),0
        inc hl
        ex de,hl
        ret

emit_capture:                   ; a capture of the piece on L
        call capture_key
        ld (gm_key),a
        ld a,MF_CAPTURE
emit_key:                       ; A = flags, key in gm_key
        ex de,hl
        ld (hl),c
        inc hl
        ld (hl),e
        inc hl
        ld (hl),a
        inc hl
        ld a,(gm_key)
        ld (hl),a
        inc hl
        ex de,hl
        ret

; A = 100 + 8 * type of the piece on L - type of the moving piece
capture_key:
        ld a,(hl)
        and 7
        add a,a
        add a,a
        add a,a
        add a,100
        push hl
        ld hl,gm_att
        sub (hl)
        pop hl
        ret

; --- the attack test -----------------------------------------------------------------

; unsigned char attacked_fc(unsigned int sq_by) __z88dk_fastcall
; L = square, H = attacking colour; returns nonzero when attacked.
_attacked_fc:
        ld c,l
        ld b,h
        push ix
        call attacked_cb
        pop ix
        ld l,a
        ret

; C = square, B = attacking colour. Returns A = 1 and NZ when the square is
; attacked, A = 0 and Z otherwise; H = board page. Preserves BC and DE.
; Unrolled: every offset and ray direction is an immediate operand.
attacked_cb:
        push de
        ld hl,_board
        ld a,b
        or PAWN
        ld e,a                  ; E = the attacker's pawn
        ld a,b
        cp WHITE
        ld a,c
        jr nz,att_black_pawns
        sub 15                  ; a White pawn attacks from one rank below
        ld l,a
        and 0x88
        jr nz,att_wp
        ld a,(hl)
        cp e
        jp z,att_yes
att_wp:
        ld a,c
        sub 17
        jr att_last_pawn
att_black_pawns:
        add a,15
        ld l,a
        and 0x88
        jr nz,att_bp
        ld a,(hl)
        cp e
        jp z,att_yes
att_bp:
        ld a,c
        add a,17
att_last_pawn:
        ld l,a
        and 0x88
        jr nz,att_knights
        ld a,(hl)
        cp e
        jp z,att_yes
att_knights:
        ld a,b
        or KNIGHT
        ld e,a
        ld a,c
        add a,33
        ld l,a
        and 0x88
        jr nz,att_n_1
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_1:
        ld a,c
        add a,31
        ld l,a
        and 0x88
        jr nz,att_n_2
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_2:
        ld a,c
        add a,18
        ld l,a
        and 0x88
        jr nz,att_n_3
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_3:
        ld a,c
        add a,14
        ld l,a
        and 0x88
        jr nz,att_n_4
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_4:
        ld a,c
        add a,-14
        ld l,a
        and 0x88
        jr nz,att_n_5
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_5:
        ld a,c
        add a,-18
        ld l,a
        and 0x88
        jr nz,att_n_6
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_6:
        ld a,c
        add a,-31
        ld l,a
        and 0x88
        jr nz,att_n_7
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_7:
        ld a,c
        add a,-33
        ld l,a
        and 0x88
        jr nz,att_n_end
        ld a,(hl)
        cp e
        jp z,att_yes
att_n_end:
        ld a,b
        or KING
        ld e,a
        ld a,c
        add a,17
        ld l,a
        and 0x88
        jr nz,att_k_1
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_1:
        ld a,c
        add a,16
        ld l,a
        and 0x88
        jr nz,att_k_2
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_2:
        ld a,c
        add a,15
        ld l,a
        and 0x88
        jr nz,att_k_3
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_3:
        ld a,c
        add a,1
        ld l,a
        and 0x88
        jr nz,att_k_4
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_4:
        ld a,c
        add a,-1
        ld l,a
        and 0x88
        jr nz,att_k_5
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_5:
        ld a,c
        add a,-15
        ld l,a
        and 0x88
        jr nz,att_k_6
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_6:
        ld a,c
        add a,-16
        ld l,a
        and 0x88
        jr nz,att_k_7
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_7:
        ld a,c
        add a,-17
        ld l,a
        and 0x88
        jr nz,att_k_end
        ld a,(hl)
        cp e
        jp z,att_yes
att_k_end:
        ld a,b
        or QUEEN
        ld d,a                  ; D = the attacker's queen on every ray
        ld a,b
        or BISHOP
        ld e,a
        ld l,c
att_d_0_step:
        ld a,l
        add a,17
        ld l,a
        and 0x88
        jr nz,att_d_1
        ld a,(hl)
        or a
        jr z,att_d_0_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_d_1:
        ld l,c
att_d_1_step:
        ld a,l
        add a,15
        ld l,a
        and 0x88
        jr nz,att_d_2
        ld a,(hl)
        or a
        jr z,att_d_1_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_d_2:
        ld l,c
att_d_2_step:
        ld a,l
        add a,-15
        ld l,a
        and 0x88
        jr nz,att_d_3
        ld a,(hl)
        or a
        jr z,att_d_2_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_d_3:
        ld l,c
att_d_3_step:
        ld a,l
        add a,-17
        ld l,a
        and 0x88
        jr nz,att_d_end
        ld a,(hl)
        or a
        jr z,att_d_3_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_d_end:
        ld a,b
        or ROOK
        ld e,a
        ld l,c
att_o_0_step:
        ld a,l
        add a,16
        ld l,a
        and 0x88
        jr nz,att_o_1
        ld a,(hl)
        or a
        jr z,att_o_0_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_o_1:
        ld l,c
att_o_1_step:
        ld a,l
        add a,1
        ld l,a
        and 0x88
        jr nz,att_o_2
        ld a,(hl)
        or a
        jr z,att_o_1_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_o_2:
        ld l,c
att_o_2_step:
        ld a,l
        add a,-1
        ld l,a
        and 0x88
        jr nz,att_o_3
        ld a,(hl)
        or a
        jr z,att_o_2_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_o_3:
        ld l,c
att_o_3_step:
        ld a,l
        add a,-16
        ld l,a
        and 0x88
        jr nz,att_o_end
        ld a,(hl)
        or a
        jr z,att_o_3_step
        cp e
        jp z,att_yes
        cp d
        jp z,att_yes
att_o_end:
        pop de
        xor a
        ret
att_yes:
        pop de
        ld a,1
        or a
        ret

; --- making and taking back moves ----------------------------------------------------

; undo_t (chess.h): move (from, to, flags, key), captured, castle, ep,
; halfmove, phase, score (2), hash (2)
defc U_FROM     = 0
defc U_TO       = 1
defc U_FLAGS    = 2
defc U_KEY      = 3
defc U_CAPTURED = 4
defc U_CASTLE   = 5
defc U_EP       = 6
defc U_HALFMOVE = 7
defc U_PHASE    = 8
defc U_SCORE    = 9
defc U_HASH     = 11

; IX = &hist[HL] (13 bytes per entry)
hist_entry:
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        add hl,hl
        add hl,hl
        add hl,de
        ld de,_hist
        add hl,de
        push hl
        pop ix
        ret

; unsigned char make_move(const move_t *m) __z88dk_fastcall
; Plays a pseudo-legal move for `side` and records it in hist[]; returns 0
; (with the move taken back) if it leaves the mover's king attacked. The
; test is skipped when it cannot fail (see chess.h).
_make_move:
        push ix
        push hl
        ld hl,(_hist_len)
        call hist_entry
        ld hl,(_hist_len)
        inc hl
        ld (_hist_len),hl
        pop hl
        ld a,(hl)
        ld (ix+U_FROM),a
        ld (mm_from),a
        inc hl
        ld a,(hl)
        ld (ix+U_TO),a
        ld (mm_to),a
        inc hl
        ld a,(hl)
        ld (ix+U_FLAGS),a
        ld (mm_flags),a
        inc hl
        ld a,(hl)
        ld (ix+U_KEY),a
        ld a,(_castle)
        ld (ix+U_CASTLE),a
        ld a,(_ep)
        ld (ix+U_EP),a
        ld a,(_halfmove)
        ld (ix+U_HALFMOVE),a
        inc a
        ld (_halfmove),a
        ld a,(_phase)
        ld (ix+U_PHASE),a
        ld hl,(_score)
        ld (ix+U_SCORE),l
        ld (ix+U_SCORE+1),h
        ld hl,(_hash)
        ld (ix+U_HASH),l
        ld (ix+U_HASH+1),h
        ld de,ZOBRIST_SIDE      ; the other side to move,
        call xor_hash
        ld a,(_castle)          ; out with the old rights
        call hash_castle
        ld a,(_ep)              ; and the old en-passant file
        call hash_ep
        ld a,(_side)
        ld (mm_me),a

        ld hl,_board            ; the piece leaves its square
        ld a,(mm_from)
        ld l,a
        ld c,a
        ld a,(hl)
        ld (hl),0
        ld (mm_piece),a
        call remove_piece

        ld a,(mm_flags)         ; a capture
        and MF_EP
        jr z,mm_capture
        ld a,(mm_me)
        cp WHITE
        ld a,(mm_to)
        jr nz,mm_ep_black
        sub 16
        jr mm_ep
mm_ep_black:
        add a,16
mm_ep:
        ld hl,_board
        ld l,a
        ld c,a
        ld a,(hl)
        ld (hl),0
        ld (ix+U_CAPTURED),a
        call remove_piece
        jr mm_place
mm_capture:
        ld hl,_board
        ld a,(mm_to)
        ld l,a
        ld c,a
        ld a,(hl)
        ld (ix+U_CAPTURED),a
        or a
        jr z,mm_place
        push af
        call remove_piece
        pop af
        call phase_of
        ld a,(_phase)
        sub e
        ld (_phase),a
        xor a
        ld (_halfmove),a

mm_place:
        ld a,(mm_flags)         ; the piece arrives, promoted perhaps
        and 7
        jr z,mm_arrive
        call phase_of
        ld a,(_phase)
        add a,e
        ld (_phase),a
        ld a,(mm_flags)
        and 7
        ld hl,mm_me
        or (hl)
        ld (mm_piece),a
        xor a                   ; a pawn move: the fifty-move count restarts
        ld (_halfmove),a
mm_arrive:
        ld hl,_board
        ld a,(mm_to)
        ld l,a
        ld c,a
        ld a,(mm_piece)
        ld (hl),a
        call add_piece

        ld a,NO_SQ
        ld (_ep),a
        ld a,(mm_piece)
        and 7
        cp PAWN
        jr nz,mm_not_pawn
        xor a
        ld (_halfmove),a
        ld a,(mm_flags)
        and MF_DOUBLE
        jr z,mm_rights
        ld a,(mm_from)
        ld hl,mm_to
        add a,(hl)
        srl a
        ld (_ep),a
        call hash_ep
        jr mm_rights
mm_not_pawn:
        cp KING
        jr nz,mm_rights
        ld hl,_king_sq
        ld a,(mm_me)
        cp WHITE
        jr z,mm_king_sq
        inc hl
mm_king_sq:
        ld a,(mm_to)
        ld (hl),a
        ld a,(mm_flags)
        and MF_CASTLE
        jr z,mm_rights
        ld a,(mm_to)            ; the rook: h -> f, or a -> d
        ld b,a
        and 7
        cp 6
        ld a,b
        jr nz,mm_castle_long
        inc a
        ld c,a                  ; C = rook's square
        sub 2
        ld b,a                  ; B = its destination
        jr mm_rook
mm_castle_long:
        sub 2
        ld c,a
        add a,3
        ld b,a
mm_rook:
        ld hl,_board
        ld l,c
        ld a,(hl)
        ld (hl),0
        ld l,b
        ld (hl),a
        push bc
        push af
        call remove_piece
        pop af
        pop bc
        ld c,b
        call add_piece

mm_rights:
        ld hl,_castle_keep      ; rights lost by leaving or reaching a square
        ld a,(mm_from)
        or l
        ld l,a
        ld a,(_castle)
        and (hl)
        ld b,a
        ld a,(mm_to)
        or 0x80
        ld l,a
        ld a,b
        and (hl)
        ld (_castle),a
        call hash_castle
        ld a,(mm_me)
        xor 0x18
        ld (_side),a

        ld hl,_king_sq          ; C = the mover's king
        ld a,(mm_me)
        cp WHITE
        jr z,mm_own_king
        inc hl
mm_own_king:
        ld c,(hl)
        ld a,(_mover_in_check)
        or a
        jr nz,mm_test
        ld a,(mm_to)
        cp c
        jr z,mm_test
        ld a,(mm_flags)
        and MF_EP
        jr nz,mm_test
        ld a,(mm_from)          ; did the piece stand on a line through the king?
        ld b,a
        ld a,c
        sub b
        add a,119
        ld e,a
        ld d,0
        ld hl,_aligned
        add hl,de
        ld a,(hl)
        or a
        jr z,mm_legal
mm_test:
        ld a,(_side)
        ld b,a
        call attacked_cb
        jr z,mm_legal
        call _unmake_move
        pop ix
        ld l,0
        ret
mm_legal:
        pop ix
        ld l,1
        ret

; void unmake_move(void)
_unmake_move:
        push ix
        ld hl,(_hist_len)
        dec hl
        ld (_hist_len),hl
        call hist_entry
        ld a,(_side)
        xor 0x18
        ld (_side),a
        ld b,a                  ; B = the side that moved
        ld c,(ix+U_FROM)
        ld e,(ix+U_TO)
        ld hl,_board
        ld l,e
        ld d,(hl)               ; D = the piece
        ld a,(ix+U_FLAGS)
        and 7
        jr z,um_back
        ld a,b
        or PAWN
        ld d,a                  ; a promoted piece goes back as a pawn
um_back:
        ld l,c
        ld (hl),d
        ld l,e                  ; what was taken returns to the target square,
        ld a,(ix+U_FLAGS)
        and MF_EP
        jr z,um_captured
        ld (hl),0               ; or, en passant, beside it
        ld a,b
        cp WHITE
        ld a,e
        jr nz,um_ep_black
        sub 16
        jr um_ep
um_ep_black:
        add a,16
um_ep:
        ld l,a
um_captured:
        ld a,(ix+U_CAPTURED)
        ld (hl),a
        ld a,d
        and 7
        cp KING
        jr nz,um_state
        ld hl,_king_sq
        ld a,b
        cp WHITE
        jr z,um_king_sq
        inc hl
um_king_sq:
        ld (hl),c
        ld a,(ix+U_FLAGS)
        and MF_CASTLE
        jr z,um_state
        ld hl,_board
        ld a,e
        and 7
        cp 6
        ld a,e
        jr nz,um_castle_long
        dec a                   ; rook f -> h
        ld l,a
        ld d,(hl)
        ld (hl),0
        add a,2
        ld l,a
        ld (hl),d
        jr um_state
um_castle_long:
        inc a                   ; rook d -> a
        ld l,a
        ld d,(hl)
        ld (hl),0
        sub 3
        ld l,a
        ld (hl),d
um_state:
        ld a,(ix+U_CASTLE)
        ld (_castle),a
        ld a,(ix+U_EP)
        ld (_ep),a
        ld a,(ix+U_HALFMOVE)
        ld (_halfmove),a
        ld a,(ix+U_PHASE)
        ld (_phase),a
        ld l,(ix+U_SCORE)
        ld h,(ix+U_SCORE+1)
        ld (_score),hl
        ld l,(ix+U_HASH)
        ld h,(ix+U_HASH+1)
        ld (_hash),hl
        pop ix
        ret

; E = phase weight of the piece type in the low bits of A
phase_of:
        and 7
        ld e,a
        ld d,0
        ld hl,phase_weight
        add hl,de
        ld e,(hl)
        ret

; DE = offset of (piece A, square C) in psq_tab and zobrist:
; index (0-5 White pawn..king, 6-11 Black) * 128 + (0..63 square) * 2.
piece_offset:
        ld e,a
        and 7
        dec a
        bit 4,e
        jr z,po_white
        add a,6
po_white:
        rrca                    ; index / 2 in bits 0-6, index & 1 in bit 7
        ld d,a
        and 0x80
        ld e,a
        ld a,d
        and 0x7F
        ld d,a
        ld a,c                  ; (0..63 square) * 2 = rank * 16 + file * 2
        and 7
        add a,c
        or e
        ld e,a
        ret

; score -= value, hash ^= key of piece A on square C (C and B survive)
remove_piece:
        call piece_offset
        push de
        ld hl,_psq_tab
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(_score)
        or a
        sbc hl,de
        ld (_score),hl
        pop de
        jr xor_zobrist

; score += value, hash ^= key of piece A on square C
add_piece:
        call piece_offset
        push de
        ld hl,_psq_tab
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld hl,(_score)
        add hl,de
        ld (_score),hl
        pop de
xor_zobrist:
        ld hl,_zobrist
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
xor_hash:                       ; hash ^= DE
        ld hl,(_hash)
        ld a,l
        xor e
        ld l,a
        ld a,h
        xor d
        ld h,a
        ld (_hash),hl
        ret

; hash ^= key of the en-passant square A's file (nothing for NO_SQ)
hash_ep:
        cp NO_SQ
        ret z
        and 7
        add a,a
        ld e,a
        ld d,0
        ld hl,_zobrist_ep
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        jr xor_hash

; hash ^= key of castling rights A
hash_castle:
        add a,a
        ld e,a
        ld d,0
        ld hl,_zobrist_castle
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        jr xor_hash


SECTION rodata_user

phase_weight:
        defb 0, 0, 1, 1, 2, 4, 0

dirs_knight:
        defb 33, 31, 18, 14, -14, -18, -31, -33
dirs_king:
        defb 17, 16, 15, 1, -1, -15, -16, -17
dirs_diag:
        defb 17, 15, -15, -17
dirs_orth:
        defb 16, 1, -1, -16

SECTION bss_user

gm_caps:   defs 1
gm_me:     defs 1
gm_them:   defs 1
gm_fwd:    defs 1
gm_start:  defs 1
gm_prom:   defs 1
gm_att:    defs 1
gm_key:    defs 1
gm_count:  defs 1
gm_rights: defs 1
mm_from:   defs 1
mm_to:     defs 1
mm_flags:  defs 1
mm_piece:  defs 1
mm_me:     defs 1
