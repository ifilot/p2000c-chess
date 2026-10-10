#!/usr/bin/env python3
"""Native or emulated Z80 matches against local Stockfish, without Python dependencies.

Usage: python3 tools/match_stockfish.py --games 2 --out build/stockfish-results.json
Compiles the C engine in a temporary directory. Checks every move, legal move
set and resulting FEN against chessmodel.py. Native mode uses node budgets;
--z80 uses make zmatch's real Z80 rules and BIOS time budgets. Coordinates and
starting FENs in the JSON allow games to be replayed in a UCI chess GUI.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from chessmodel import Position, name, parse_square

ROOT = Path(__file__).resolve().parent.parent


class Move(C.Structure):
    _fields_ = [(field, C.c_ubyte) for field in ("from_sq", "to_sq", "flags", "key")]


def coordinate(m):
    return name((m.from_sq >> 4) * 8 + (m.from_sq & 7)) + name(
        (m.to_sq >> 4) * 8 + (m.to_sq & 7)) + (" pnbrqk"[m.flags & 7].strip())


def model_coordinate(m):
    return name(m[0]) + name(m[1]) + m[2].lower()


def position(fen):
    p = Position()
    if fen is None:
        return p
    board, turn, rights, ep, halfmove, _ = fen.split()
    p.board = [""] * 64
    for rank, row in enumerate(board.split("/")):
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
            else:
                p.board[(7 - rank) * 8 + f] = ch
                f += 1
    p.white = turn == "w"
    p.castling = set(rights) - {"-"}
    p.ep = None if ep == "-" else parse_square(ep)
    p.halfmove = int(halfmove)
    p.history = [p.key()]
    return p


class Native:
    def __init__(self, library):
        self.lib = C.CDLL(str(library))
        self.lib.gen_legal.argtypes = [C.c_uint]
        self.lib.gen_legal.restype = C.c_uint
        self.lib.make_move.argtypes = [C.POINTER(Move)]
        self.lib.make_move.restype = C.c_ubyte
        self.lib.cpu_choose.argtypes = [C.POINTER(Move)]
        self.lib.chess_fen.argtypes = [C.c_char_p]

    def var(self, name, typ=C.c_ubyte):
        return typ.in_dll(self.lib, name)

    def init(self, p, level, seed):
        self.lib.chess_init()
        self.var("cpu_level").value = level
        self.var("cpu_seed", C.c_uint).value = seed
        board = (C.c_ubyte * 128).in_dll(self.lib, "board")
        kings = (C.c_ubyte * 2).in_dll(self.lib, "king_sq")
        pst = ((C.c_int * 64) * 5).in_dll(self.lib, "psq_tab")
        score = phase = 0
        for i in range(128):
            board[i] = 0
        for i, piece in enumerate(p.board):
            if piece:
                kind = " PNBRQK".index(piece.upper())
                board[(i // 8) * 16 + i % 8] = kind | (8 if piece.isupper() else 16)
                if kind != 6:
                    score += pst[kind - 1][i] if piece.isupper() else -pst[kind - 1][i ^ 56]
                phase += (0, 0, 1, 1, 2, 4, 0)[kind]
                if kind == 6:
                    kings[0 if piece.isupper() else 1] = (i // 8) * 16 + i % 8
        self.var("score", C.c_int).value = score
        self.var("phase").value = phase
        self.var("side").value = 8 if p.white else 16
        self.var("castle").value = sum(1 << i for i, r in enumerate("KQkq") if r in p.castling)
        self.var("ep").value = 255 if p.ep is None else (p.ep // 8) * 16 + p.ep % 8
        self.var("halfmove").value = p.halfmove
        self.lib.chess_rehash()

    def legal(self):
        n = self.lib.gen_legal(0)
        moves = (Move * 700).in_dll(self.lib, "moves")
        return {coordinate(m): Move.from_buffer_copy(m) for m in moves[:n]}

    def choose(self):
        m = Move()
        start = time.perf_counter()
        self.lib.cpu_choose(C.byref(m))
        return coordinate(m), {"seconds": time.perf_counter() - start,
                               "nodes": self.var("cpu_nodes", C.c_uint).value,
                               "depth": self.var("cpu_depth").value}

    def play(self, m):
        self.var("mover_in_check").value = 1
        assert self.lib.make_move(C.byref(m)), "Native make_move rejected a legal move"

    def fen(self):
        buf = C.create_string_buffer(100)
        self.lib.chess_fen(buf)
        return buf.value.decode()


class Stockfish:
    def __init__(self, path):
        self.proc = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        self.send("uci")
        self.until("uciok")
        self.name = self.engine_name
        self.send("setoption name Threads value 1")
        self.send("setoption name Hash value 16")

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def until(self, marker):
        while True:
            line = self.proc.stdout.readline().strip()
            if line.startswith("id name "):
                self.engine_name = line[8:]
            if line.startswith(marker):
                return line
            if self.proc.poll() is not None:
                raise RuntimeError("Stockfish exited unexpectedly")

    def configure(self, mode):
        limited = mode.startswith("elo")
        self.send("setoption name UCI_LimitStrength value " + ("true" if limited else "false"))
        self.send("setoption name UCI_Elo value " + (mode[3:] if limited else "1320"))
        self.send("setoption name Skill Level value " + ("0" if mode == "skill0" else "20"))
        self.send("ucinewgame")
        self.send("isready")
        self.until("readyok")

    def choose(self, initial, moves, nodes):
        self.send("position fen " + initial + (" moves " + " ".join(moves) if moves else ""))
        self.send(f"go nodes {nodes}")
        return self.until("bestmove").split()[1]

    def close(self):
        self.send("quit")
        self.proc.wait(timeout=10)


class Z80:
    """Persistent engine-only CP/M program, timed by the real emulated BIOS."""
    def __init__(self, temporary_directory, engine_dir=None):
        from render import HD0, IPL, make_image
        tmp = Path(temporary_directory)
        engine_dir = Path(engine_dir or ROOT / "build").resolve()
        # The emulator mounts writable media; all writes go to temporary copies.
        disk0 = tmp / "hd0.hda"
        shutil.copyfile(HD0, disk0)
        disk1 = make_image(engine_dir / "ZMATCH.COM", tmp)
        self.proc = subprocess.Popen([str(ROOT / "build/emulator-bridge"), str(IPL),
            str(disk0), str(disk1), str(engine_dir / "zmatch.map")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.variables = {"cpu_level": C.c_ubyte(2), "cpu_seed": C.c_uint(1234)}

    def var(self, name, typ=C.c_ubyte):
        return self.variables[name]

    def command(self, command):
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("Z80 emulator bridge exited unexpectedly")
        response = json.loads(line)
        assert response["ok"], (command, response)
        assert response["clock_available"], "Emulated BIOS timer is unavailable"
        self.current_fen = response["fen"]
        return response

    def init(self, p, level, seed):
        if p.fen() != Position().fen():
            raise ValueError("The Z80 mailbox starts from the standard position")
        self.var("cpu_level").value = level
        self.var("cpu_seed").value = seed & 65535
        self.command(f"1 {level} {seed & 65535}")

    def legal(self):
        response = self.command("4")
        return {coordinate(Move(*m)): Move(*m) for m in response["legal"]}

    def choose(self):
        start = time.perf_counter()
        response = self.command(f"2 {self.var('cpu_level').value} {self.var('cpu_seed').value}")
        if "cpu_seed" in response:
            self.var("cpu_seed").value = response["cpu_seed"]
        return coordinate(Move(*response["move"])), dict(seconds=time.perf_counter() - start,
            nodes=response["nodes"], depth=response["depth"], emulated_seconds=response["emulated_seconds"])

    def play(self, m):
        self.command(f"3 {m.from_sq} {m.to_sq} {m.flags} {m.key}")

    def fen(self):
        return self.current_fen

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        if self.proc.returncode:
            raise RuntimeError(f"Z80 bridge exit status {self.proc.returncode}")


def match(native, sf, level, white, mode, seed, nodes, fen=None):
    p = position(fen)
    native.init(p, level, seed)
    sf.configure(mode)
    initial = p.fen()
    played, stats = [], []
    while not p.result() and len(played) < 300:
        legal = native.legal()
        model_moves = {model_coordinate(m): m for m in p.legal()}
        assert legal.keys() == model_moves.keys(), (p.fen(), "legal move set mismatch")
        if p.white == white:
            move, stat = native.choose()
            stats.append(stat)
        else:
            move = sf.choose(initial, played, nodes)
        assert move in legal, (p.fen(), move, "illegal move")
        native.play(legal[move])
        p.play(model_moves[move])
        played.append(move)
        assert native.fen() == p.fen(), (native.fen(), p.fen())
    reason = p.result() or "300-ply limit"
    win = reason == "Schaakmat!" and p.white != white
    loss = reason == "Schaakmat!" and p.white == white
    result = "win" if win else "loss" if loss else "draw"
    searched = [s for s in stats if s["nodes"]]
    record = dict(level=level, color="white" if white else "black", mode=mode, seed=seed,
                  result=result, reason=reason, plies=len(played), initial_fen=initial,
                  moves=played, searches=stats, final_fen=p.fen())
    print(f"L{level} {record['color']:5} {mode:8}: {result:4} {len(played):3} plies; "
          f"mean depth {sum(s['depth'] for s in searched) / max(1, len(searched)):.2f}; {reason}", flush=True)
    return record


def rating_summary(results):
    """Fit expected score = logistic(Elo difference); profile-likelihood CI.

    A draw contributes half a point. This is an approximate score model,
    not a draw-aware engine-rating model or a human-rating calibration.
    """
    summaries = []
    for level in sorted({r["level"] for r in results}):
        games = [r for r in results if r["level"] == level and r["mode"].startswith("elo")]
        if not games:
            continue
        points = sum({"win": 1, "draw": 0.5, "loss": 0}[g["result"]] for g in games)
        def likelihood(rating):
            total = 0
            for game in games:
                x = (rating - int(game["mode"][3:])) * math.log(10) / 400
                # Stable log(sigmoid(x)) and log(1-sigmoid(x)).
                logp = -max(0, -x) - math.log1p(math.exp(-abs(x)))
                logq = -max(0, x) - math.log1p(math.exp(-abs(x)))
                score = {"win": 1, "draw": 0.5, "loss": 0}[game["result"]]
                total += score * logp + (1 - score) * logq
            return total
        if points in (0, len(games)):
            # The MLE is infinite; report a one-sided score bound instead.
            p = 1 - 0.05 ** (1 / len(games)) if points == 0 else 0.05 ** (1 / len(games))
            anchors = {int(g["mode"][3:]) for g in games}
            bound = (next(iter(anchors)) + 400 * math.log10(p / (1 - p))) if len(anchors) == 1 else None
            estimate, interval = None, None
        else:
            lo, hi = -3000.0, 5000.0
            for _ in range(80):
                middle = (lo + hi) / 2
                expected = sum(1 / (1 + 10 ** ((int(g["mode"][3:]) - middle) / 400)) for g in games)
                if expected < points:
                    lo = middle
                else:
                    hi = middle
            estimate = (lo + hi) / 2
            cutoff = likelihood(estimate) - 1.920729410347062
            bounds = []
            for left, right, lower in ((-3000.0, estimate, True), (estimate, 5000.0, False)):
                for _ in range(80):
                    middle = (left + right) / 2
                    if (likelihood(middle) < cutoff) == lower:
                        left = middle
                    else:
                        right = middle
                bounds.append(round((left + right) / 2))
            interval, bound = bounds, None
        counts = {outcome: sum(g["result"] == outcome for g in games) for outcome in ("win", "draw", "loss")}
        summaries.append(dict(level=level, games=len(games), points=points, **counts,
                              estimated_elo=round(estimate) if estimate is not None else None,
                              approximate_95_percent_interval=interval, one_sided_95_percent_bound=bound,
                              bound_type=("upper" if points == 0 else "lower") if bound is not None else None,
                              opponents=sorted({int(g["mode"][3:]) for g in games})))
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stockfish", default=shutil.which("stockfish") or "/usr/games/stockfish")
    parser.add_argument("--games", type=int, default=2, help="games per level/color/opponent")
    parser.add_argument("--seed", type=int, default=1234, help="first game seed; subsequent seeds add 997")
    parser.add_argument("--nodes", type=int, default=10000, help="Stockfish nodes per move")
    parser.add_argument("--elos", type=int, nargs="+", help="rating opponents (Stockfish 16: 1320-3190)")
    parser.add_argument("--levels", type=int, nargs="+", default=[1, 2, 3], choices=[1, 2, 3])
    parser.add_argument("--no-endgames", action="store_true")
    parser.add_argument("--z80", action="store_true", help="use make zmatch's real 4 MHz Z80 engine")
    parser.add_argument("--engine-dir", type=Path, default=ROOT / "build", help="directory containing ZMATCH.COM and zmatch.map")
    parser.add_argument("--out", type=Path, default=ROOT / "build/stockfish-results.json")
    args = parser.parse_args()
    if args.games < 1 or args.nodes < 1:
        parser.error("--games and --nodes must be positive")
    if args.elos and any(not 1320 <= elo <= 3190 for elo in args.elos):
        parser.error("Stockfish 16 Elo settings must be between 1320 and 3190")
    if args.z80 and not args.no_endgames:
        parser.error("--z80 requires --no-endgames (mailbox starts at the standard position)")
    binary_hash = hashlib.sha256((args.engine_dir / "ZMATCH.COM").read_bytes()).hexdigest() if args.z80 else None
    results = []
    with tempfile.TemporaryDirectory(prefix="p2000c-stockfish-") as tmp:
        stub = Path(tmp) / "clock.c"
        stub.write_text("unsigned char clock_available=0; unsigned int clock_ticks(void){return 0;} void key_watch(void){}\n")
        library = Path(tmp) / "engine.so"
        subprocess.run(["gcc", "-O2", "-Wall", "-shared", "-fPIC", "-o", str(library),
                        str(ROOT / "src/chess.c"), str(ROOT / "src/cpu.c"), str(stub)], check=True)
        native, sf = Z80(tmp, args.engine_dir) if args.z80 else Native(library), Stockfish(args.stockfish)
        try:
            for mode in ([f"elo{elo}" for elo in args.elos] if args.elos else ("full", "skill0", "elo1320")):
                for level in args.levels:
                    for white in (True, False):
                        for game in range(args.games):
                            results.append(match(native, sf, level, white, mode, args.seed + game * 997,
                                                 args.nodes))
                            # Keep completed games if a long tournament is interrupted.
                            args.out.parent.mkdir(parents=True, exist_ok=True)
                            args.out.write_text(json.dumps(dict(stockfish=sf.name, stockfish_path=args.stockfish,
                                stockfish_nodes=args.nodes, native_clock=False if not args.z80 else None,
                                match_binary_sha256=binary_hash,
                                engine_mode="z80-4mhz-bios-clock" if args.z80 else "native-node-budgets", results=results,
                                ratings=rating_summary(results)), indent=2) + "\n")
            # Winning material against full-strength defence: conversion tests.
            for fen in (() if args.no_endgames else ("7k/8/8/8/8/8/8/KQ6 w - - 0 1",
                        "7k/8/8/8/8/8/8/KR6 w - - 0 1",
                        "7k/8/8/8/8/4K3/4P3/8 w - - 0 1")):
                results.append(match(native, sf, 3, True, "full", 1234, args.nodes, fen))
        finally:
            sf.close()
            if args.z80:
                native.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(stockfish=sf.name, stockfish_path=args.stockfish,
        stockfish_nodes=args.nodes, native_clock=False if not args.z80 else None,
        match_binary_sha256=binary_hash,
        engine_mode="z80-4mhz-bios-clock" if args.z80 else "native-node-budgets", results=results,
        ratings=rating_summary(results)), indent=2) + "\n")
    print(json.dumps(rating_summary(results), indent=2))
    print(f"Saved {len(results)} games to {args.out}")


if __name__ == "__main__":
    main()
