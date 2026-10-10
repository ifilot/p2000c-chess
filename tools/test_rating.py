#!/usr/bin/env python3
"""Validate rating fits against exact known Elo odds and censored samples."""
from match_stockfish import rating_summary
from rate_chess import summarize


def main():
    records = []
    # At 400 Elo disadvantage the expected score is 1/11; at 800 it is
    # 1/101. These exact synthetic samples must recover 600/1000/1400.
    for level, mode, wins, losses in ((1, "elo1400", 1, 100),
                                       (2, "elo1400", 1, 10),
                                       (3, "elo1400", 10, 10),
                                       (1, "level2", 1, 10),
                                       (1, "level3", 1, 100),
                                       (2, "level3", 1, 10)):
        records += [dict(level=level, mode=mode, result="win")] * wins
        records += [dict(level=level, mode=mode, result="loss")] * losses
    fitted = summarize(records)
    for row, expected in zip(fitted, (600, 1000, 1400)):
        assert row["estimated_elo"] == expected, row
        low, high = row["approximate_95_percent_interval"]
        assert low < expected < high, row
    print("anchored rating fit with games between levels: PASS")

    balanced = [dict(level=1, mode="elo1320", result="draw")] * 40
    assert rating_summary(balanced)[0]["estimated_elo"] == 1320
    print("draws count as half a point: PASS")

    losses = [dict(level=1, mode="elo1320", result="loss")] * 40
    row = rating_summary(losses)[0]
    assert row["estimated_elo"] is None
    assert 876 < row["one_sided_95_percent_bound"] < 877
    print("all losses yield an upper bound, not a finite rating: PASS")


if __name__ == "__main__":
    main()
