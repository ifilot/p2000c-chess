// SPDX-License-Identifier: GPL-3.0-only
// fen.js -- reading a position in Forsyth-Edwards Notation.
//
// The game's QR code holds the page address followed by the FEN with '_'
// for each space (a URL has no spaces); fenFromHash() undoes that. parseFen()
// checks the form of the six fields, not whether the position is legal,
// and throws a FenError whose code names what is wrong.

export class FenError extends Error {
  constructor(code, detail) {
    super(code);
    this.code = code;
    this.detail = detail;
  }
}

export const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

export function fenFromHash(hash) {
  let text = hash.replace(/^#/, "");
  try {
    text = decodeURIComponent(text);
  } catch {
    // a stray '%': take the text as it is
  }
  return text.replace(/_/g, " ").trim().replace(/\s+/g, " ");
}

export function fenToHash(fen) {
  return "#" + fen.trim().replace(/\s+/g, "_");
}

export function parseFen(fen) {
  const fields = fen.trim().split(/\s+/);
  if (fields.length !== 6)
    throw new FenError("fields", fields.length);
  const [placement, side, castling, ep, halfmove, fullmove] = fields;

  const ranks = placement.split("/");
  if (ranks.length !== 8)
    throw new FenError("ranks", ranks.length);
  const squares = new Array(64).fill("");
  ranks.forEach((text, i) => {
    const rank = 7 - i;
    let file = 0;
    for (const c of text) {
      if (c >= "1" && c <= "8")
        file += Number(c);
      else if ("pnbrqkPNBRQK".includes(c)) {
        if (file < 8)
          squares[rank * 8 + file] = c;
        file++;
      } else
        throw new FenError("piece", c);
    }
    if (file !== 8)
      throw new FenError("rank", rank + 1);
  });
  for (const king of "Kk")
    if (squares.filter(p => p === king).length !== 1)
      throw new FenError("kings");

  if (side !== "w" && side !== "b")
    throw new FenError("side", side);
  if (!/^(-|K?Q?k?q?)$/.test(castling) || castling === "")
    throw new FenError("castling", castling);
  if (!/^(-|[a-h][36])$/.test(ep))
    throw new FenError("ep", ep);
  if (!/^\d+$/.test(halfmove) || !/^\d+$/.test(fullmove) || Number(fullmove) < 1)
    throw new FenError("numbers");

  return {
    squares,
    white: side === "w",
    castling: castling === "-" ? "" : castling,
    ep: ep === "-" ? "" : ep,
    halfmove: Number(halfmove),
    fullmove: Number(fullmove),
    fen: fields.join(" "),
  };
}
