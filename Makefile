# Schaken (chess) for the Philips P2000C: C (Z88DK/sdcc) + Z80 assembly, CP/M target.
#
# The compiler runs in the z88dk/z88dk Docker image. Screenshots, tests and
# `make run` need the sibling p2000c-cpm-disk-tool checkout (headless emulator
# and dist/pro/ disk images) and, for the character-ROM font, p2000c-emulator.
# `make perft` and `make selfplay` build the rules and the engine natively (gcc).

VERSION    = 1.2.0
BUILD_DATE = $(shell date +%Y-%m-%d)

# sdcc's register-allocation effort. The hot paths are assembly (rules.asm,
# video.asm), and 200000 (about 5 minutes) measured no faster than 10000.
ALLOCS ?= 10000

# -SO2, not -SO3: in p2000c-battleship the level-3 peephole rules dropped
# stores and loads that were needed (seen in the listings).
# The game uses direct BDOS calls, so omit the CP/M CRT static stdio heap.
# Current Z88DK images otherwise reserve 1 KiB that pushes the arena past D800h.
ZCC      = docker run --rm --user $(shell id -u):$(shell id -g) -v "$(CURDIR)":/src -w /src z88dk/z88dk zcc
ZCCFLAGS = +cpm -vn -clib=sdcc_iy -O3 -SO2 --opt-code-speed --max-allocs-per-node$(ALLOCS) \
           -Ibuild -create-app -m -pragma-define:CLIB_STDIO_HEAP_SIZE=0 $(EXTRA)

SOURCES = src/main.c src/game.c src/screen.c src/panel.c src/screens.c src/saver.c src/clock.c \
          src/gfx.c src/chess.c src/cpu.c src/qr.c src/rules.asm src/video.asm src/memory.asm
HEADERS = src/video.h src/chess.h src/cpu.h src/game.h src/screen.h src/panel.h src/screens.h \
          src/saver.h src/clock.h src/gfx.h src/qr.h src/sprites.h src/splash.h src/version.h
COM     = build/SCHAKEN.COM
GFX     = build/SCHAKEN.GFX

# The buffers memory.asm places past the program must end this far below the
# BDOS entry (E406h on the 62K system), leaving the rest to the stack: 1.5 KiB.
# Measured in level-3 games: under 400 bytes; the search's MAX_PLY of 40
# bounds it at about 1.3 KiB.
ARENA_LIMIT = 0xDE00

# Deployment image for the SASI emulator (ZuluBlaster): a second-disk image
# with the standard split layout (E: low, F: high) built with the sibling
# disk tool's CLI and its split system tracks, holding only the game on F:.
# The former p2000c-cpm-disk-tool checkout is now part of the SASI-drive
# distribution. Prefer the legacy checkout when present, otherwise use it.
DISKTOOL      ?= $(firstword $(wildcard ../p2000c-cpm-disk-tool ../p2000c-zulublaster-sasi-drive))
P2000C_DISK   = PYTHONPATH=$(DISKTOOL)/src python3 -m p2000c_disk.cli
SYSTEM_TRACKS = $(DISKTOOL)/assets/boot/hdboot-split.trk
DEPLOY_IMAGE  = build/HD1_256.hda

HOSTCC = gcc
HOSTCFLAGS = -O2 -Wall

.PHONY: all build run screenshot test sprites deploy perft zperft selfplay qrtest site clean

all: build

build: $(COM) $(GFX)

$(COM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p build
	printf '#define VERSION "%s"\n#define BUILD_DATE "%s"\n' "$(VERSION)" "$(BUILD_DATE)" > build/build_info.h
	$(ZCC) $(ZCCFLAGS) $(SOURCES) -o build/schaken
	rm -f build/schaken build/schaken_CODE.bin
	@end=$$(sed -n 's/^__arena_end *= \$$\([0-9A-F]*\).*/\1/p' build/schaken.map); \
	 echo "program file $$(wc -c < $@) bytes, memory up to $$end"; \
	 if [ $$((0x$$end)) -gt $$(($(ARENA_LIMIT))) ]; then echo "memory ends past $(ARENA_LIMIT)"; rm -f $@; exit 1; fi

# The data file: the bitmaps, then the title picture (both whole CP/M records).
$(GFX): src/sprites.bin src/splash.rle
	mkdir -p build
	cat src/sprites.bin src/splash.rle > $@

# HD1_256.hda with SCHAKEN.COM and SCHAKEN.GFX on F: and nothing else. Copy
# it to the SD card in place of the distribution's HD1_256.hda.
deploy: build
	rm -f $(DEPLOY_IMAGE)
	$(P2000C_DISK) build $(DEPLOY_IMAGE) --layout split --system $(SYSTEM_TRACKS)
	$(P2000C_DISK) put-many $(DEPLOY_IMAGE) $(COM) $(GFX) --partition high
	$(P2000C_DISK) verify $(DEPLOY_IMAGE)
	$(P2000C_DISK) list $(DEPLOY_IMAGE)

# Regenerate the piece/marker bitmaps and the title picture, the two parts of
# SCHAKEN.GFX (need rsvg-convert and the p2000c-emulator font sheet).
sprites:
	python3 tools/gen_sprites.py
	python3 tools/gen_splash.py

# Move-generator check against the published perft counts (native build).
perft:
	mkdir -p build
	$(HOSTCC) $(HOSTCFLAGS) -o build/perft tools/perft.c src/chess.c
	build/perft

# The same check on the Z80 with rules.asm, in the headless emulator.
zperft:
	mkdir -p build
	$(ZCC) +cpm -vn -clib=sdcc_iy -O3 -SO2 -create-app -m tools/perft.c src/chess.c src/rules.asm src/memory.asm -o build/zperft
	rm -f build/zperft build/zperft_CODE.bin
	python3 tools/run_headless.py build/ZPERFT.COM "mismatches"

# Engine games on the host (node budgets instead of the clock).
selfplay:
	mkdir -p build
	$(HOSTCC) $(HOSTCFLAGS) -o build/selfplay tools/selfplay.c src/chess.c src/cpu.c
	build/selfplay 1 3 1 7
	build/selfplay 3 2 1 11
	build/selfplay 2 3 1 99

# FEN of random games against tools/chessmodel.py, QR codes through a
# decoder (the zxing-cpp Python module, if installed).
qrtest:
	mkdir -p build
	$(HOSTCC) $(HOSTCFLAGS) -o build/qrdump tools/qrdump.c src/chess.c src/qr.c
	python3 tools/test_qr.py

# The web page the QR code opens -> build/site/ (published by the Pages
# workflow); to look at it: python3 -m http.server -d build/site
site:
	python3 tools/gen_site.py

# Open the game in the graphical emulator.
run: build
	python3 tools/run.py

# Plain raster screenshot of the board -> build/board.png
# (add --wait-for/actions via tools/render.py for other moments)
screenshot: build
	python3 tools/render.py

# Games against the computer in the headless emulator.
test: build
	python3 tools/test_game.py

clean:
	rm -rf build
