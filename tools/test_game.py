#!/usr/bin/env python3
"""Regression tests: games against the computer in the headless emulator.

The "human" moves with the cursor keys, as a player would: to the piece,
RETURN, to the target square, RETURN (and a letter for a promotion). Its
moves are chosen at random from the legal ones (seeded), preferring
castling, promotions and en passant so those paths get exercised. The
computer's replies are read from the move list in the panel, checked for
legality against tools/chessmodel.py and replayed there; every line of the
move list and the final verdict must agree with the model. As in the
Othello tests, the game is replayed from the start for every new computer
move (the emulator runs are deterministic). Run after `make build`.
"""
import json
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chessmodel import Position, name, parse_square
from render import EMULATOR, HD0, IPL, ROOT, make_image

PANEL = 50
ROW_STATUS, ROW_NOTE, ROW_LIST = 9, 10, 12
RESULTS = ("Schaakmat!", "Pat", "Remise")
PROMO_KEY = {"Q": "d", "R": "t", "B": "l", "N": "p"}


def emulate(actions, timeout=1800):
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0),
           "--hard-disk-1", str(ROOT / "build/hd1.hda"), "--fast-storage", "--chunk-cycles", "5000",
           "--wait-cycles", "80000000",
           "--wait-for", "A>", "--send", "F:SCHAKEN\\r", "--run", "24000000", "--send", " ",
           "--wait-for", "Sterkte van de computer", *actions, "--run", "400000", "--output", "json"]
    state = json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout)
    flat = "".join(state["screen"])
    rows = [flat[r * 64 + PANEL:(r + 1) * 64] for r in range(21)]
    return state, rows


def listed(rows):
    """The plies shown in the move list, as notation strings."""
    return [r.split()[-1] for r in rows[ROW_LIST:ROW_LIST + 8] if r.strip()]


class Game:
    def __init__(self, level, white, seed):
        self.pos = Position()
        self.white = white
        self.rng = random.Random(seed)
        self.cursor = parse_square("e2" if white else "e7")
        self.actions = (["--send", str(level)] if white else ["--send", "z", "--send", str(level)])
        self.actions += ["--wait-for", "Wit aan zet" if white else "Zwart aan zet"]
        self.notation = []                 # the whole game as the program writes it

    def screen_xy(self, sq):
        f, r = sq % 8, sq // 8
        return (f, 7 - r) if self.white else (7 - f, r)

    def keys_to(self, sq):
        (x0, y0), (x1, y1) = self.screen_xy(self.cursor), self.screen_xy(sq)
        self.cursor = sq
        return ("d" * (x1 - x0) + "a" * (x0 - x1) + "s" * (y1 - y0) + "w" * (y0 - y1))

    def choose(self):
        moves = self.pos.legal()
        special = [m for m in moves if m[2] or
                   (self.pos.board[m[0]].upper() == "K" and abs(m[1] - m[0]) == 2) or
                   (self.pos.board[m[0]].upper() == "P" and m[1] == self.pos.ep)]
        return self.rng.choice(special or moves)

    def human_move(self, move):
        keys = self.keys_to(move[0]) + "\\r" + self.keys_to(move[1]) + "\\r"
        if move[2]:
            keys += PROMO_KEY[move[2]]
        self.notation.append(self.pos.play(move))
        verdict = self.pos.result()
        self.actions += ["--send", keys]
        if verdict:
            self.actions += ["--wait-for", verdict]
        else:
            self.actions += ["--wait-for", "denkt"]
        return verdict

    def cpu_reply(self):
        """Runs the game so far, learns the computer's reply, checks the panel."""
        state, rows = emulate(self.actions + ["--wait-for", "aan zet"])
        finished = state["status"] == "timeout" and rows[ROW_STATUS].strip() in RESULTS
        if state["status"] != "ok" and not finished:       # a reply that ends the game: no "aan zet"
            return f"emulator: {state['status']} {state.get('message', '')}"
        shown = listed(rows)
        if not shown:
            return "empty move list"
        text = shown[-1]
        move = self.pos.parse(text)
        if move is None:
            return f"illegal or unreadable computer move {text!r} after {' '.join(self.notation)}"
        self.notation.append(self.pos.play(move))
        verdict = self.pos.result()
        self.actions += ["--wait-for", verdict if verdict else "aan zet"]
        expected = self.notation[-len(shown):]
        if shown != expected:
            return f"move list {shown} != {expected}"
        return None

    def check_panel(self, rows):
        shown = listed(rows)
        expected = self.notation[-len(shown):] if shown else []
        if shown != expected:
            return f"move list {shown} != {expected}"
        return None


def play_game(level, white, seed, human_moves):
    g = Game(level, white, seed)
    if not white:
        err = g.cpu_reply()
        if err:
            return err, g
    for _ in range(human_moves):
        if g.pos.result():
            break
        if g.human_move(g.choose()):
            break
        err = g.cpu_reply()
        if err:
            return err, g
    verdict = g.pos.result()
    state, rows = emulate(g.actions)
    if state["status"] != "ok":
        return f"final run: {state['status']} {state.get('message', '')}", g
    if verdict and rows[ROW_STATUS].strip() != verdict:
        return f"status {rows[ROW_STATUS].strip()!r}, model says {verdict!r}", g
    return g.check_panel(rows), g


def test_take_back():
    """Two moves each, T takes back the last pair, then the game goes on."""
    g = Game(1, True, 5)
    for _ in range(2):
        g.human_move(g.choose())
        err = g.cpu_reply()
        if err:
            return err
    g.actions += ["--send", "t", "--wait-for", "Teruggenomen"]
    state, rows = emulate(g.actions)
    if listed(rows) != g.notation[:2]:
        return f"after T the list shows {listed(rows)}, expected {g.notation[:2]}"
    # rebuild the model and the cursor bookkeeping at the taken-back position
    replay, g.pos = g.notation[:2], Position()
    g.notation = []
    for text in replay:
        g.notation.append(g.pos.play(g.pos.parse(text)))
    g.human_move(g.choose())
    return g.cpu_reply()


def main():
    make_image(ROOT / "build/SCHAKEN.COM", ROOT / "build")
    ok = True
    for level, white, seed, moves in ((1, True, 1, 40), (1, False, 2, 12), (2, True, 3, 8)):
        err, g = play_game(level, white, seed, moves)
        colour = "White" if white else "Black"
        print(f"level {level}, human {colour}: {len(g.notation)} plies, "
              f"{g.pos.result() or 'unfinished'}: {'ok' if not err else err}")
        print("   " + " ".join(g.notation))
        ok &= err is None
    err = test_take_back()
    print(f"take-back: {'ok' if not err else err}")
    ok &= err is None
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
