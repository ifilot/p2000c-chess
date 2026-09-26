# Piece drawings

`Chess_?lt45.svg` are the standard chess pieces by Colin M.L. Burnett
("Cburnett"), from Wikimedia Commons
(<https://commons.wikimedia.org/wiki/Category:SVG_chess_pieces>), where they
are offered under the GFDL, the BSD licence and the GNU GPL; they are used
here under the GPL, like the rest of this project.

`tools/gen_sprites.py` renders them (at the CRT's 3:5 dot pitch) into the
bitmaps in `src/sprites.h`. Only the light set is needed: a White piece is
its silhouette with the inner lines cut out, a Black piece its lines alone.
