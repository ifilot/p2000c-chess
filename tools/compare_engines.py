#!/usr/bin/env python3
"""Compare two saved Z80 engines at the same level and BIOS time budget."""
import argparse
import hashlib
import json
import tempfile
from pathlib import Path

from chessmodel import Position
from match_stockfish import ROOT, Z80, match


class SavedOpponent:
    def __init__(self, engine):
        self.engine = engine
        self.seed = 1234

    def configure(self, mode):
        self.engine.init(Position(), int(mode[8:]), self.seed)
        self.played = 0
        self.stats = []

    def choose(self, initial, played, nodes):
        for move in played[self.played:]:
            self.engine.play(self.engine.legal()[move])
        self.played = len(played)
        move, stat = self.engine.choose()
        self.stats.append(stat)
        return move


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, default=ROOT / "build")
    parser.add_argument("--levels", type=int, nargs="+", default=[1, 2, 3], choices=[1, 2, 3])
    parser.add_argument("--games", type=int, default=10, help="games per level and colour")
    parser.add_argument("--seed", type=int, default=1234, help="first game seed")
    parser.add_argument("--out", type=Path, default=ROOT / "build/engine-comparison.json")
    args = parser.parse_args()
    if args.games < 1:
        parser.error("--games must be positive")
    results = []
    fingerprints = {name: hashlib.sha256((directory / "ZMATCH.COM").read_bytes()).hexdigest()
                    for name, directory in (("baseline", args.baseline), ("candidate", args.candidate))}
    with tempfile.TemporaryDirectory(prefix="p2000c-candidate-") as a, \
            tempfile.TemporaryDirectory(prefix="p2000c-baseline-") as b:
        candidate, baseline = Z80(a, args.candidate), Z80(b, args.baseline)
        opponent = SavedOpponent(baseline)
        try:
            for level in args.levels:
                for white in (True, False):
                    for game in range(args.games):
                        seed = args.seed + game * 997
                        opponent.seed = seed ^ 0xBEEF
                        results.append(match(candidate, opponent, level, white,
                                             f"baseline{level}", seed, 0))
                        results[-1]["baseline_searches"] = opponent.stats
                        args.out.parent.mkdir(parents=True, exist_ok=True)
                        args.out.write_text(json.dumps(dict(engine_mode="z80-4mhz-bios-clock",
                            binary_sha256=fingerprints, results=results), indent=2) + "\n")
        finally:
            candidate.close()
            baseline.close()
    for level in args.levels:
        games = [r for r in results if r["level"] == level]
        points = sum({"win": 1, "draw": .5, "loss": 0}[r["result"]] for r in games)
        print(f"Level {level}: candidate {points}/{len(games)} points against baseline")


if __name__ == "__main__":
    main()
