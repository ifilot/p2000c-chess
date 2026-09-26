# Changelog

All notable changes to Schaken for the Philips P2000C are recorded here. The
project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.1] - 2026-09-26

### Fixed

- Keys were still registered twice: the space bar picked a piece up and put
  it down again, a cursor key moved two squares. The terminal board, busy
  with picture data, takes a key that is still held down for a new press,
  also during short redraws, which the filter of 1.1.0 let through. Now the
  same key arriving within a third of a second of the previous one is
  ignored, whatever the program was doing; arrival times are noted while it
  draws or thinks. A key held longer repeats about three times a second.
- Built with `-DKEY_TRACE`, the panel shows every key with its timing, to
  check the filter on the real machine.

## [1.1.0] - 2026-09-26

### Added

- `F` shows the position as FEN text next to a QR code of the same text, to
  scan with a phone and paste into an analysis board; any key returns to the
  game.
- The title picture says "Druk op een toets om verder te gaan" once it has
  loaded.
- `make qrtest`: the FEN of random games against the rules model and the QR
  codes through a decoder (zxing-cpp); `make test` also checks the FEN page
  in the emulator, reading the QR code back from the screen.

### Fixed

- A key pressed while the screen was being drawn could arrive twice (a move
  played and the moved piece picked up again, two moves taken back). After
  a key that kept the program busy for a third of a second or more, the
  same key again within a quarter of a second of the program being ready
  is ignored.
- The promotion prompt and the quit question read their key through the same
  filter, and the screen saver can also start while they wait.
- A pawn promotion without a capture did not restart the fifty-move count,
  so a draw could be declared too early.

### Changed

- Memory: the stack reserve is based on measurement (under 400 bytes used,
  1.5 KiB kept), the move stack has 700 entries instead of 900 (self-play
  peak 351), and the bitmap buffer is exactly the size of the bitmaps.
- The panel's key hint reads "H hulp  F FEN"; the help screen lists `F`.

## [1.0.0]

### Added

- Chess against the computer in the 512x252 high-resolution graphics mode,
  with the Wikipedia (Cburnett) piece drawings, a Dutch interface, and a
  cursor that marks the legal moves of the piece under it.
- Full rules: castling, en passant, promotion to any piece, and draws by
  stalemate, the fifty-move rule, threefold repetition and insufficient
  material.
- Three levels of up to about 10 seconds per move, an opening book, a
  choice of colour (the board turns for Black), take-back, a move list,
  a game clock, a demo game, a help screen and a screen saver.
- `SCHAKEN.GFX` with the bitmaps and the title picture, found on the current
  drive or any drive in use.
- Perft checks natively and on the emulated Z80, self-play, emulator game
  tests, and a SASI deployment image.

[1.1.1]: https://github.com/ifilot/p2000c-chess/releases/tag/v1.1.1
[1.1.0]: https://github.com/ifilot/p2000c-chess/releases/tag/v1.1.0
[1.0.0]: https://github.com/ifilot/p2000c-chess/releases/tag/v1.0.0
