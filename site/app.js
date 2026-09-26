// SPDX-License-Identifier: GPL-3.0-only
// app.js -- the page the FEN page's QR code opens: the position from the
// address (#<FEN with '_' for spaces>), drawn with the game's own bitmaps,
// its details in a panel like the game's, and the FEN to copy or analyse.

import { composeBoard, lit, BOARD_BYTES, FB_LINES, DOT_PITCH } from "./board.js";
import { fenFromHash, fenToHash, parseFen, START_FEN, FenError } from "./fen.js";
import { GFX_OFFSETS, GFX_BASE64 } from "./sprites.js";

const PHOSPHOR = [51, 255, 51];             // the plain phosphor green of tools/render.py

const TEXT = {
  nl: {
    title: "Schaken – stelling",
    toMove: { w: "Wit aan zet", b: "Zwart aan zet" },
    move: "Zet",
    castling: "Rokade",
    ep: "En passant",
    fifty: "50 zetten",
    fiftyHint: "Halve zetten sinds de laatste slag of pionzet; bij 100 is het remise.",
    none: "-",
    fenLabel: "Stelling in FEN-notatie",
    copy: "Kopieer",
    copied: "Gekopieerd",
    flip: "Draai het bord",
    analyse: "Analyseer op lichess.org",
    noPosition: "Er staat geen stelling in het adres, dus dit is de beginstelling. Druk in het spel op F en scan de QR-code, of plak hieronder een FEN.",
    invalidHash: "De stelling in het adres is geen geldige FEN; hier staat de beginstelling.",
    invalid: "Geen geldige FEN: ",
    errors: {
      fields: d => `een FEN heeft zes velden, deze heeft er ${d}`,
      ranks: d => `de opstelling moet acht rijen hebben, niet ${d}`,
      piece: d => `onbekend teken '${d}'`,
      rank: d => `rij ${d} heeft niet precies acht velden`,
      kings: () => "elke kleur moet precies één koning hebben",
      side: d => `aan zet moet w of b zijn, niet '${d}'`,
      castling: d => `ongeldige rokade '${d}'`,
      ep: d => `ongeldig en-passantveld '${d}'`,
      numbers: () => "de laatste twee velden moeten getallen zijn",
    },
    names: { K: "koning", Q: "dame", R: "toren", B: "loper", N: "paard", P: "pion" },
    sides: { w: "Wit", b: "Zwart" },
    boardLabel: "Schaakbord",
    about: 'Deze pagina hoort bij <a href="https://github.com/ifilot/p2000c-chess">Schaken</a>, een schaakprogramma voor de Philips P2000C, de draagbare CP/M-computer van Philips uit het begin van de jaren tachtig. Druk in het spel op <kbd>F</kbd> en de QR-code op het scherm opent deze pagina met de stelling uit je partij. Het bord is getekend met dezelfde bitmaps als op de groene monitor, stip voor stip.',
    source: "Broncode op GitHub",
  },
  en: {
    title: "Schaken – position",
    toMove: { w: "White to move", b: "Black to move" },
    move: "Move",
    castling: "Castling",
    ep: "En passant",
    fifty: "50 moves",
    fiftyHint: "Half-moves since the last capture or pawn move; at 100 the game is drawn.",
    none: "-",
    fenLabel: "Position in FEN notation",
    copy: "Copy",
    copied: "Copied",
    flip: "Flip the board",
    analyse: "Analyse on lichess.org",
    noPosition: "There is no position in the address, so this is the starting position. Press F in the game and scan the QR code, or paste a FEN below.",
    invalidHash: "The position in the address is not a valid FEN; this is the starting position.",
    invalid: "Not a valid FEN: ",
    errors: {
      fields: d => `a FEN has six fields, this one has ${d}`,
      ranks: d => `the placement needs eight ranks, not ${d}`,
      piece: d => `unknown character '${d}'`,
      rank: d => `rank ${d} does not have exactly eight squares`,
      kings: () => "each side needs exactly one king",
      side: d => `the side to move must be w or b, not '${d}'`,
      castling: d => `invalid castling rights '${d}'`,
      ep: d => `invalid en passant square '${d}'`,
      numbers: () => "the last two fields must be numbers",
    },
    names: { K: "king", Q: "queen", R: "rook", B: "bishop", N: "knight", P: "pawn" },
    sides: { w: "White", b: "Black" },
    boardLabel: "Chess board",
    about: 'This page belongs to <a href="https://github.com/ifilot/p2000c-chess">Schaken</a>, a chess program for the Philips P2000C, a portable CP/M computer from the early 1980s. Press <kbd>F</kbd> in the game and the QR code on the screen opens this page with the position from your game. The board is drawn with the same bitmaps as on the green monitor, dot for dot.',
    source: "Source code on GitHub",
  },
};

const $ = id => document.getElementById(id);
const gfx = Uint8Array.from(atob(GFX_BASE64), c => c.charCodeAt(0));

const state = {
  lang: initialLanguage(),
  turned: false,
  position: null,
  notice: "",                               // key of TEXT for the notice, or ""
  error: null,                              // FenError from the input field
};

function initialLanguage() {
  try {
    const saved = localStorage.getItem("schaken-lang");
    if (saved in TEXT)
      return saved;
  } catch {
    // no storage: fall through to the browser's language
  }
  return (navigator.languages || [navigator.language || ""]).some(l => /^nl\b/i.test(l)) ? "nl" : "en";
}

function drawBoard() {
  const fb = composeBoard(gfx, GFX_OFFSETS, state.position.squares, state.turned);
  const width = BOARD_BYTES * 8;
  const dots = new ImageData(width, FB_LINES);
  for (let line = 0; line < FB_LINES; line++)
    for (let x = 0; x < width; x++)
      if (lit(fb, x, line)) {
        const i = (line * width + x) * 4;
        dots.data.set(PHOSPHOR, i);
        dots.data[i + 3] = 255;
      }
  const raster = document.createElement("canvas");
  raster.width = width;
  raster.height = FB_LINES;
  raster.getContext("2d").putImageData(dots, 0, 0);
  const canvas = $("board");
  canvas.width = width * DOT_PITCH[0];
  canvas.height = FB_LINES * DOT_PITCH[1];
  const ctx = canvas.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(raster, 0, 0, canvas.width, canvas.height);
}

// The pieces in words, for screen readers.
function describe(t) {
  const order = "KQRBNP";
  const parts = ["w", "b"].map(side => {
    const found = [];
    state.position.squares.forEach((p, sq) => {
      if (p && (p === p.toUpperCase()) === (side === "w"))
        found.push([order.indexOf(p.toUpperCase()), `${t.names[p.toUpperCase()]} ${"abcdefgh"[sq % 8]}${(sq >> 3) + 1}`]);
    });
    found.sort((a, b) => a[0] - b[0]);
    return `${t.sides[side]}: ${found.map(f => f[1]).join(", ")}`;
  });
  return `${t.boardLabel}. ${t.toMove[state.position.white ? "w" : "b"]}. ${parts.join(". ")}.`;
}

function render() {
  const t = TEXT[state.lang], p = state.position;
  document.documentElement.lang = state.lang;
  document.title = t.title;
  for (const el of document.querySelectorAll("[data-t]"))
    el.innerHTML = t[el.dataset.t];
  for (const el of document.querySelectorAll("[data-lang]"))
    el.setAttribute("aria-pressed", String(el.dataset.lang === state.lang));

  $("to-move").textContent = t.toMove[p.white ? "w" : "b"];
  $("move-number").textContent = p.fullmove;
  $("castling").textContent = p.castling || t.none;
  $("ep").textContent = p.ep || t.none;
  $("fifty").textContent = `${p.halfmove}/100`;
  $("fifty-label").title = t.fiftyHint;
  $("board").setAttribute("aria-label", describe(t));

  const lichess = new URL("https://lichess.org/analysis/standard/" + p.fen.replace(/ /g, "_"));
  if (state.turned)
    lichess.searchParams.set("color", "black");
  $("analyse").href = lichess.href;

  $("notice").hidden = !state.notice;
  $("notice").textContent = state.notice ? t[state.notice] : "";
  $("error").hidden = !state.error;
  $("error").textContent = state.error
    ? t.invalid + t.errors[state.error.code](state.error.detail) : "";
  drawBoard();
}

// The position in the address, or the starting position with a notice.
function loadFromHash() {
  const text = fenFromHash(location.hash);
  state.error = null;
  state.notice = "";
  if (!text) {
    state.position = parseFen(START_FEN);
    state.notice = "noPosition";
  } else {
    try {
      state.position = parseFen(text);
    } catch (e) {
      if (!(e instanceof FenError))
        throw e;
      state.position = parseFen(START_FEN);
      state.notice = "invalidHash";
      state.error = e;
    }
  }
  $("fen").value = text || state.position.fen;
  render();
}

$("fen").addEventListener("input", () => {
  try {
    state.position = parseFen($("fen").value);
    state.error = null;
    state.notice = "";
    history.replaceState(null, "", fenToHash(state.position.fen));
  } catch (e) {
    if (!(e instanceof FenError))
      throw e;
    state.error = e;
  }
  render();
});

$("copy").addEventListener("click", async () => {
  const button = $("copy"), t = TEXT[state.lang];
  try {
    await navigator.clipboard.writeText(state.position.fen);
  } catch {
    $("fen").select();
    document.execCommand("copy");
  }
  button.textContent = t.copied;
  setTimeout(() => { button.textContent = TEXT[state.lang].copy; }, 1500);
});

$("flip").addEventListener("click", () => {
  state.turned = !state.turned;
  render();
});

for (const button of document.querySelectorAll("[data-lang]"))
  button.addEventListener("click", () => {
    state.lang = button.dataset.lang;
    try {
      localStorage.setItem("schaken-lang", state.lang);
    } catch {
      // not remembered, still switched
    }
    render();
  });

window.addEventListener("hashchange", loadFromHash);
loadFromHash();
