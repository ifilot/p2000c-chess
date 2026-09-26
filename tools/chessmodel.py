#!/usr/bin/env python3
"""A small chess rules model for the emulator tests (no dependencies).

Squares are 0..63 (a1 = 0, h8 = 63); pieces are letters, upper case White
("PNBRQK"), lower case Black. Moves are (from, to, promotion letter or "").
Besides the legal moves it knows the game-end rules the program applies
(mate, stalemate, fifty moves, threefold repetition, insufficient
material) and the program's long notation with Dutch piece letters.
"""

KNIGHT = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]
KING = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
DIAG = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
ORTH = [(1, 0), (-1, 0), (0, 1), (0, -1)]
DUTCH = {"N": "P", "B": "L", "R": "T", "Q": "D", "K": "K"}
FROM_DUTCH = {v: k for k, v in DUTCH.items()}


def name(sq):
    return "abcdefgh"[sq % 8] + "12345678"[sq // 8]


def parse_square(text):
    return "abcdefgh".index(text[0]) + 8 * (int(text[1]) - 1)


class Position:
    def __init__(self):
        self.board = [""] * 64
        for f, p in enumerate("RNBQKBNR"):
            self.board[f] = p
            self.board[8 + f] = "P"
            self.board[48 + f] = "p"
            self.board[56 + f] = p.lower()
        self.white = True
        self.castling = set("KQkq")
        self.ep = None
        self.halfmove = 0
        self.history = [self.key()]

    def key(self):
        return (tuple(self.board), self.white, frozenset(self.castling), self.ep)

    def own(self, p):
        return p != "" and p.isupper() == self.white

    def enemy(self, p, white=None):
        white = self.white if white is None else white
        return p != "" and p.isupper() != white

    def attacked(self, sq, by_white):
        f, r = sq % 8, sq // 8

        def at(df, dr):
            ff, rr = f + df, r + dr
            return self.board[rr * 8 + ff] if 0 <= ff < 8 and 0 <= rr < 8 else None
        pawn_dir = -1 if by_white else 1
        for df in (-1, 1):
            if at(df, pawn_dir) == ("P" if by_white else "p"):
                return True
        for df, dr in KNIGHT:
            if at(df, dr) == ("N" if by_white else "n"):
                return True
        for df, dr in KING:
            if at(df, dr) == ("K" if by_white else "k"):
                return True
        for dirs, kinds in ((DIAG, "BQ"), (ORTH, "RQ")):
            for df, dr in dirs:
                ff, rr = f + df, r + dr
                while 0 <= ff < 8 and 0 <= rr < 8:
                    p = self.board[rr * 8 + ff]
                    if p:
                        if p.isupper() == by_white and p.upper() in kinds:
                            return True
                        break
                    ff, rr = ff + df, rr + dr
        return False

    def king(self, white):
        return self.board.index("K" if white else "k")

    def pseudo(self):
        out = []
        for sq, p in enumerate(self.board):
            if not self.own(p):
                continue
            f, r, kind = sq % 8, sq // 8, p.upper()
            if kind == "P":
                d = 1 if self.white else -1
                last = 7 if self.white else 0
                t = sq + 8 * d
                if 0 <= t < 64 and self.board[t] == "":
                    if t // 8 == last:
                        out += [(sq, t, x) for x in "QNRB"]
                    else:
                        out.append((sq, t, ""))
                        if r == (1 if self.white else 6) and self.board[t + 8 * d] == "":
                            out.append((sq, t + 8 * d, ""))
                for df in (-1, 1):
                    if 0 <= f + df < 8:
                        t = sq + 8 * d + df
                        if self.enemy(self.board[t]) or t == self.ep:
                            if t // 8 == last:
                                out += [(sq, t, x) for x in "QNRB"]
                            else:
                                out.append((sq, t, ""))
            elif kind in "NK":
                for df, dr in (KNIGHT if kind == "N" else KING):
                    ff, rr = f + df, r + dr
                    if 0 <= ff < 8 and 0 <= rr < 8 and not self.own(self.board[rr * 8 + ff]):
                        out.append((sq, rr * 8 + ff, ""))
                if kind == "K":
                    home = 4 if self.white else 60
                    them = not self.white
                    k, q = ("K", "Q") if self.white else ("k", "q")
                    if sq == home and not self.attacked(sq, them):
                        if k in self.castling and self.board[sq + 1] == self.board[sq + 2] == "" \
                                and not self.attacked(sq + 1, them):
                            out.append((sq, sq + 2, ""))
                        if q in self.castling and self.board[sq - 1] == self.board[sq - 2] == self.board[sq - 3] == "" \
                                and not self.attacked(sq - 1, them):
                            out.append((sq, sq - 2, ""))
            else:
                dirs = DIAG if kind == "B" else ORTH if kind == "R" else DIAG + ORTH
                for df, dr in dirs:
                    ff, rr = f + df, r + dr
                    while 0 <= ff < 8 and 0 <= rr < 8:
                        t = rr * 8 + ff
                        if self.own(self.board[t]):
                            break
                        out.append((sq, t, ""))
                        if self.board[t]:
                            break
                        ff, rr = ff + df, rr + dr
        return out

    def _apply(self, move):
        """Plays a move without legality checks; returns the undo record."""
        frm, to, promo = move
        saved = (self.board[:], self.white, set(self.castling), self.ep, self.halfmove)
        p = self.board[frm]
        captured = self.board[to]
        self.halfmove += 1
        if p.upper() == "P" and to == self.ep:
            self.board[to - 8 if self.white else to + 8] = ""
            captured = "p"
        if p.upper() == "K" and abs(to - frm) == 2:
            rook_from, rook_to = (frm + 3, frm + 1) if to > frm else (frm - 4, frm - 1)
            self.board[rook_to], self.board[rook_from] = self.board[rook_from], ""
        self.board[to] = (promo if self.white else promo.lower()) if promo else p
        self.board[frm] = ""
        if p.upper() == "P" or captured:
            self.halfmove = 0
        self.ep = (frm + to) // 2 if p.upper() == "P" and abs(to - frm) == 16 else None
        for sq, right in ((0, "Q"), (4, "KQ"), (7, "K"), (56, "q"), (60, "kq"), (63, "k")):
            if frm == sq or to == sq:
                self.castling -= set(right)
        self.white = not self.white
        return saved

    def _undo(self, saved):
        self.board, self.white, self.castling, self.ep, self.halfmove = saved

    def legal(self):
        out = []
        for m in self.pseudo():
            saved = self._apply(m)
            if not self.attacked(self.king(not self.white), self.white):
                out.append(m)
            self._undo(saved)
        return out

    def play(self, move):
        """Plays a legal move and returns its notation as the program shows it."""
        frm, to, promo = move
        p = self.board[frm]
        capture = self.board[to] != "" or (p.upper() == "P" and to == self.ep)
        if p.upper() == "K" and abs(to - frm) == 2:
            text = "O-O" if to > frm else "O-O-O"
        else:
            letter = "" if p.upper() == "P" else DUTCH[p.upper()]
            text = letter + name(frm) + ("x" if capture else "-") + name(to) + (DUTCH[promo] if promo else "")
        self._apply(move)
        self.history.append(self.key())
        check = self.attacked(self.king(self.white), not self.white)
        if check:
            text += "#" if not self.legal() else "+"
        return text

    def parse(self, text):
        """The legal move a line of the program's move list stands for."""
        text = text.rstrip("+#")
        for m in self.legal():
            probe = Position.__new__(Position)
            probe.__dict__ = {k: (v[:] if isinstance(v, list) else set(v) if isinstance(v, set) else v)
                              for k, v in self.__dict__.items()}
            if probe.play(m).rstrip("+#") == text:
                return m
        return None

    def result(self):
        """None while playing, else the program's status text."""
        if not self.legal():
            in_check = self.attacked(self.king(self.white), not self.white)
            return "Schaakmat!" if in_check else "Pat"
        if self.halfmove >= 100 or self.history.count(self.history[-1]) >= 3:
            return "Remise"
        pieces = [p.upper() for p in self.board if p and p.upper() != "K"]
        if not any(p in "PRQ" for p in pieces):
            if len(pieces) <= 1:
                return "Remise"
            bishops = {(sq % 8 + sq // 8) % 2 for sq, p in enumerate(self.board) if p.upper() == "B"}
            if all(p == "B" for p in pieces) and len(bishops) == 1:
                return "Remise"
        return None
