#!/usr/bin/env python3
"""Combine Stockfish anchors and level matches into an Elo estimate.

python3 tools/rate_chess.py --input build/rating-1320.json build/rating-upper.json
The score model is approximate and conditional on Stockfish's configured
ratings and both engines' node limits. Its intervals exclude calibration
error, hardware differences and uncertainty in the Elo logistic model.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import subprocess
import tempfile
from pathlib import Path

from match_stockfish import Native, Z80, ROOT, match

K = math.log(10) / 400


class LevelOpponent:
    def __init__(self, native, seed):
        self.native, self.seed = native, seed

    def configure(self, mode):
        self.level = int(mode[5:])

    def choose(self, initial, played, nodes):
        n = self.native
        target_level, target_seed = n.var("cpu_level").value, n.var("cpu_seed", C.c_uint).value
        n.var("cpu_level").value = self.level
        n.var("cpu_seed", C.c_uint).value = self.seed
        move, _ = n.choose()
        self.seed = n.var("cpu_seed", C.c_uint).value
        n.var("cpu_level").value = target_level
        n.var("cpu_seed", C.c_uint).value = target_seed
        return move


def observations(games):
    grouped = {}
    for g in games:
        if not g["mode"].startswith(("elo", "level")):
            continue
        key = (g["level"] - 1,
               int(g["mode"][5:]) - 1 if g["mode"].startswith("level") else None,
               int(g["mode"][3:]) if g["mode"].startswith("elo") else None)
        points, count = grouped.get(key, (0, 0))
        grouped[key] = points + {"win": 1.0, "draw": 0.5, "loss": 0.0}[g["result"]], count + 1
    return [(*key, points, count) for key, (points, count) in grouped.items()]


def likelihood(ratings, games):
    total = 0
    for a, b, anchor, points, count in games:
        x = K * (ratings[a] - (anchor if b is None else ratings[b]))
        total += points * (-max(0, -x) - math.log1p(math.exp(-abs(x))))
        total += (count - points) * (-max(0, x) - math.log1p(math.exp(-abs(x))))
    return total


def fit(games, fixed=None, initial=None):
    ratings = list(initial or [900.0, 1200.0, 1500.0])
    if fixed:
        ratings[fixed[0]] = fixed[1]
    for _ in range(300):
        change = 0
        for level in range(3):
            if fixed and level == fixed[0]:
                continue
            lo, hi = -3000.0, 5000.0
            for _ in range(45):
                middle = (lo + hi) / 2
                derivative = 0
                for a, b, anchor, points, count in games:
                    if a != level and b != level:
                        continue
                    ra = middle if a == level else ratings[a]
                    rb = anchor if b is None else middle if b == level else ratings[b]
                    p = 1 / (1 + math.exp(-K * (ra - rb)))
                    derivative += (points - count * p) * (1 if a == level else -1)
                if derivative > 0:
                    lo = middle
                else:
                    hi = middle
            estimate = (lo + hi) / 2
            change = max(change, abs(estimate - ratings[level]))
            ratings[level] = estimate
        if change < 0.0001:
            break
    return ratings


def summarize(records):
    data = observations(records)
    ratings = fit(data)
    cutoff = likelihood(ratings, data) - 1.920729410347062
    summaries = []
    for level in range(3):
        bounds = []
        for left, right, lower in ((-3000.0, ratings[level], True), (ratings[level], 5000.0, False)):
            for _ in range(32):
                middle = (left + right) / 2
                fitted = fit(data, fixed=(level, middle), initial=ratings)
                if (likelihood(fitted, data) < cutoff) == lower:
                    left = middle
                else:
                    right = middle
            bounds.append(round((left + right) / 2))
        summaries.append(dict(level=level + 1, estimated_elo=round(ratings[level]),
                              approximate_95_percent_interval=bounds,
                              finite_estimate=-2999 < ratings[level] < 4999))
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, nargs="+", default=[])
    parser.add_argument("--games", type=int, default=20, help="self-games per pairing and colour")
    parser.add_argument("--seed", type=int, default=1234, help="first game seed")
    parser.add_argument("--self-results", type=Path, help="reuse completed level matches")
    parser.add_argument("--z80", action="store_true", help="use the 4 MHz Z80 engine for level matches")
    parser.add_argument("--engine-dir", type=Path, default=ROOT / "build", help="directory containing ZMATCH.COM and zmatch.map")
    parser.add_argument("--out", type=Path, default=ROOT / "build/chess-ratings.json")
    args = parser.parse_args()
    if args.games < 1:
        parser.error("--games must be positive")
    records = []
    anchors = []
    engine_mode = "z80-4mhz-bios-clock" if args.z80 else "native-node-budgets"
    binary_hash = hashlib.sha256((args.engine_dir / "ZMATCH.COM").read_bytes()).hexdigest() if args.z80 else None
    for path in args.input:
        source = json.loads(path.read_text())
        if source.get("engine_mode", "native-node-budgets") != engine_mode:
            parser.error(f"{path}: engine mode differs; use --z80 for Z80 data")
        if source.get("match_binary_sha256", binary_hash) != binary_hash:
            parser.error(f"{path}: engine binary differs")
        records.extend(g for g in source["results"] if g["mode"].startswith("elo"))
        anchors.append(dict(source=str(path), stockfish=source.get("stockfish"),
                            nodes_per_move=source.get("stockfish_nodes")))
    if args.self_results:
        source = json.loads(args.self_results.read_text())
        if source.get("engine_mode", "native-node-budgets") != engine_mode:
            parser.error(f"{args.self_results}: engine mode differs")
        if source.get("match_binary_sha256", binary_hash) != binary_hash:
            parser.error(f"{args.self_results}: engine binary differs")
        records.extend(g for g in source["results"] if g["mode"].startswith("level"))
    else:
        with tempfile.TemporaryDirectory(prefix="p2000c-level-rating-") as tmp:
            source, library = Path(tmp) / "clock.c", Path(tmp) / "engine.so"
            source.write_text("unsigned char clock_available=0;unsigned int clock_ticks(void){return 0;}void key_watch(void){}\n")
            subprocess.run(["gcc", "-O2", "-Wall", "-shared", "-fPIC", "-o", str(library),
                            str(ROOT / "src/chess.c"), str(ROOT / "src/cpu.c"), str(source)], check=True)
            native = Z80(tmp, args.engine_dir) if args.z80 else Native(library)
            try:
                for target, opponent in ((1, 2), (1, 3), (2, 3)):
                    for white in (True, False):
                        for game in range(args.games):
                            seed = args.seed + game * 997
                            records.append(match(native, LevelOpponent(native, seed ^ 0xBEEF), target,
                                                 white, f"level{opponent}", seed, 0))
                            args.out.parent.mkdir(parents=True, exist_ok=True)
                            args.out.write_text(json.dumps(dict(
                                engine_mode=engine_mode,
                                match_binary_sha256=binary_hash,
                                results=records), indent=2) + "\n")
            finally:
                if args.z80:
                    native.close()
    if not any(g["mode"].startswith("elo") for g in records):
        print("Saved level matches; supply Stockfish --input files to anchor the ratings.")
        return
    ratings = summarize(records)
    report = dict(model="Elo logistic expected score; draws count as half a point",
                  scale="Stockfish 16 UCI_Elo performance under fixed node limits; not a human rating",
                  uncertainty="Approximate 95% profile likelihood intervals, excluding calibration/hardware/model error",
                  engine_mode=engine_mode,
                  stockfish_anchors=anchors,
                  match_binary_sha256=binary_hash,
                  total_games=len(records),
                  game_limit="The app's 300-ply draw rule is used",
                  native_node_budgets=None if args.z80 else {"1": 2000, "2": 1500, "3": 4000},
                  ratings=ratings, results=records)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(ratings, indent=2))


if __name__ == "__main__":
    main()
