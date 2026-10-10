#!/usr/bin/env python3
"""Search regressions: terminal leaves, quiet check evasions and state restore."""
import ctypes as C
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from match_stockfish import Native, ROOT, position


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--z80-output", type=Path, help="compare the assembly pawn evaluator's random-mask signature")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="p2000c-search-test-") as tmp:
        source, library = Path(tmp) / "probe.c", Path(tmp) / "probe.so"
        # Include the implementation to exercise leaves beyond the normal
        # check-extension horizon without exporting a production test API.
        source.write_text(
            '#include "' + str(ROOT / "src/cpu.c") + '"\n'
            'unsigned char clock_available=0; unsigned int clock_ticks(void){return 0;}\n'
            'void key_watch(void){}\n'
            'int probe(int a,int b,unsigned char p){ply=p;sp=0;stop=0;'
            'cpu_nodes=0;node_budget=4000;return quiesce(a,b);}\n'
            'int probe_window(unsigned char d,int a,int b){unsigned char i;ply=0;sp=0;stop=0;'
            'cpu_nodes=0;node_budget=65535;for(i=0;i<MAX_PLY;i++)'
            'killer[i][0].from=killer[i][1].from=NO_SQ;'
            'return search(d,a,b);}\n'
            'int probe_pawns(void){return pawn_endgame();}\n')
        subprocess.run(["gcc", "-O2", "-Wall", "-shared", "-fPIC", "-o", str(library),
                        str(source), str(ROOT / "src/chess.c")], check=True)
        engine = Native(library)
        engine.lib.probe.argtypes = [C.c_int, C.c_int, C.c_ubyte]
        engine.lib.probe.restype = C.c_int

        def probe(fen, alpha=-32000, beta=32000):
            engine.init(position(fen), 3, 1234)
            before = engine.fen()
            pawns = bytes((C.c_ubyte * 16).in_dll(engine.lib, "pawn_files"))
            value = engine.lib.probe(alpha, beta, 12)
            assert engine.fen() == before, "search changed the position"
            assert bytes((C.c_ubyte * 16).in_dll(engine.lib, "pawn_files")) == pawns, "search changed pawn masks"
            assert engine.var("hist_len", C.c_uint).value == 0, "search leaked history"
            return value, engine.var("cpu_nodes", C.c_uint).value

        stalemate = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"
        assert position(stalemate).result() == "Pat"
        assert probe(stalemate)[0] == 0
        assert probe(stalemate, -2000, -1500)[0] == 0, "stand-pat cutoff hid stalemate"
        assert probe(stalemate, 100, 200)[0] == 0, "alpha hid stalemate"
        print("stalemate at a capture-search leaf: PASS")

        mate = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1"
        assert position(mate).result() == "Schaakmat!"
        assert probe(mate)[0] == -31000 + 12
        assert probe(mate, -32000, -31999)[0] == -31000 + 12
        print("checkmate beyond the check-extension horizon: PASS")

        quiet = "7k/8/5K2/8/8/8/7Q/8 b - - 0 1"
        p = position(quiet)
        legal = p.legal()
        assert len(legal) == 1 and p.board[legal[0][1]] == ""
        value, nodes = probe(quiet)
        assert nodes > 1, "quiet check evasion was not searched"
        assert probe(quiet, -32000, -2000)[1] > 1, "stand pat was used in check"
        p.play(p.legal()[0])
        expected, _ = probe(p.fen())
        assert value == -expected
        print("only legal response to check is quiet: PASS")

        capture_check = "7k/7p/6K1/8/8/8/8/7R w - - 0 1"
        p = position(capture_check)
        p.play(next(m for m in p.legal() if m[:2] == (7, 55)))
        assert p.attacked(p.king(p.white), not p.white)
        legal = p.legal()
        assert len(legal) == 1 and p.board[legal[0][1]] == ""
        assert probe(capture_check)[1] >= 3
        print("capture gives check followed by a quiet evasion: PASS")

        assert probe("7k/8/5K2/8/8/8/7Q/8 b - - 100 1")[0] == 0
        print("fifty-move draw inside capture search: PASS")

        # A quiet repetition must remain available to a losing side.
        p = position("k7/8/8/8/4Q3/8/8/2K5 b - - 0 1")
        engine.init(p, 3, 1234)
        for coordinate in ("a8b8", "c1d1", "b8c8", "d1c1"):
            engine.play(engine.legal()[coordinate])
        engine.lib.repetitions.restype = C.c_ubyte
        assert engine.lib.repetitions() == 0
        before = engine.fen()
        engine.lib.probe_window.argtypes = [C.c_ubyte, C.c_int, C.c_int]
        engine.lib.probe_window.restype = C.c_int
        assert engine.lib.probe_window(1, -100, 100) == 0, "quiet repetition missed"
        assert engine.fen() == before and engine.var("hist_len", C.c_uint).value == 4
        print("quiet repetition draw inside search: PASS")

        engine.lib.probe_pawns.restype = C.c_int
        def pawns(fen):
            engine.init(position(fen), 3, 1234)
            return engine.lib.probe_pawns()
        advanced = pawns("4k3/8/P7/8/8/8/8/4K3 w - - 0 1")
        assert advanced > pawns("4k3/8/8/8/8/P7/8/4K3 w - - 0 1")
        assert advanced > pawns("4k3/p7/P7/8/8/8/8/4K3 w - - 0 1")
        assert advanced > pawns("4k3/8/P7/P7/8/8/8/4K3 w - - 0 1")
        assert pawns("4k3/8/P7/1P6/8/8/8/4K3 w - - 0 1") > \
               pawns("4k3/8/P7/7P/8/8/8/4K3 w - - 0 1")
        assert advanced == -pawns("4k3/8/8/8/8/p7/8/4K3 b - - 0 1")
        print("passed, blocked, doubled and isolated pawn files; colour symmetry: PASS")
        masks = (C.c_ubyte * 16).in_dll(engine.lib, "pawn_files")
        x, signature = 0xACE1, 0
        for _ in range(100):
            for j in range(16):
                x = (x ^ (x << 7)) & 65535
                x ^= x >> 9
                x = (x ^ (x << 8)) & 65535
                masks[j] = x & 255
            signature = ((signature << 1) | (signature >> 15)) & 65535
            signature ^= engine.lib.probe_pawns() & 65535
        if args.z80_output:
            output = args.z80_output.read_text()
            found = re.search(r"PAWN SIGNATURE (\d+)", output)
            assert found and int(found[1]) == signature, (signature, output)
            assert "SEARCH PASS" in output
            print("assembly pawn evaluation equals native C on 100 random masks: PASS")


if __name__ == "__main__":
    main()
