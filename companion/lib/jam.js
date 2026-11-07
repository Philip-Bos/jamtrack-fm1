// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
//
// JAMTRACK's jam on the host's side: the 192 bytes the FM-1 keeps (app/dsp/jt_dsp.h, jt_jam_t), as an object and
// back, and the music theory the screen uses to name chords (app/dsp/theory.h), so the companion shows "6m7 Bm7"
// exactly as the FM-1 does (app/tests/theory_test.mjs checks it against the firmware's own answers).

export const JAM_BYTES = 192, NCHORD = 12, NAME = 16, MAGIC = 0x314D414A;
export const PARTS = ["DRUMS", "BASS", "CHORDS", "SYNTH"];
export const FILLS = ["EVERY 4", "EVERY 8", "EVERY 16", "ROUND END", "NONE"];
export const CHAINS = ["BY HAND", "AUTO A/B", "A ONLY"];
export const SWITCHES = ["ROUND END", "NEXT BAR"];
export const BASSES = ["ROOT", "/3RD", "/5TH", "/7TH", "/KEY 1"];
export const COLORS = ["TRIADS", "SOFT 7THS", "7THS", "9THS"];

const SCALE = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 2, 4, 5, 7, 9, 10],
               [0, 2, 4, 6, 7, 9, 11], [0, 1, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 8, 11]];
export const MODES = ["MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "LYDIAN", "PHRYGIAN", "HARM MINOR"];
// qualities: [label after the number, intervals]
export const QUAL = [["", [0, 4, 7]], ["", [0, 4, 7]], ["m", [0, 3, 7]], ["dim", [0, 3, 6]], ["+", [0, 4, 8]],
  ["sus2", [0, 2, 7]], ["sus4", [0, 5, 7]], ["maj7", [0, 4, 7, 11]], ["m7", [0, 3, 7, 10]], ["7", [0, 4, 7, 10]],
  ["m7b5", [0, 3, 6, 10]], ["add9", [0, 4, 7, 14]], ["maj9", [0, 4, 7, 11, 14]], ["m9", [0, 3, 7, 10, 14]],
  ["9", [0, 4, 7, 10, 14]], ["6", [0, 4, 7, 9]], ["m6", [0, 3, 7, 9]], ["7sus4", [0, 5, 7, 10]],
  ["m11", [0, 3, 7, 10, 17]], ["dim7", [0, 3, 6, 9]], ["mM7", [0, 3, 7, 11]], ["madd9", [0, 3, 7, 14]],
  ["13", [0, 4, 10, 14, 21]], ["6/9", [0, 4, 7, 9, 14]], ["5", [0, 7]]];
export const QUAL_MENU = ["AUTO", "MAJ", "m", "dim", "aug", "sus2", "sus4", "maj7", "m7", "7", "m7b5", "add9", "maj9",
  "m9", "9", "6", "m6", "7sus4", "m11", "dim7", "mM7", "madd9", "13", "6/9", "5"];
const SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
export const KEYS = FLAT;
const mod12 = (v) => ((v % 12) + 12) % 12;

export const noteName = (pc, flats) => (flats ? FLAT : SHARP)[mod12(pc)];
export function keyFlats(j) {
  const REL = [0, 3, 10, 5, 7, 8, 3], maj = (j.key + REL[j.mode < 7 ? j.mode : 0]) % 12;
  return [5, 10, 3, 8, 1, 6].includes(maj);
}
export const keyText = (j) => `${noteName(j.key, keyFlats(j))} ${MODES[j.mode] || "MAJOR"}`;

const degIndex = (d) => Math.min(d & 7, 6);
const degAcc = (d) => { const a = (d >> 4) & 3; return a === 1 ? -1 : a === 2 ? 1 : 0; };

function autoQuality(mode, i, acc, color) {
  const s = SCALE[mode < 7 ? mode : 0];
  if (acc < 0) return color >= 2 ? 7 : color === 1 && i !== 4 ? 7 : 1;
  if (acc > 0) return color >= 2 ? 10 : 3;
  const third = mod12(s[(i + 2) % 7] - s[i]), fifth = mod12(s[(i + 4) % 7] - s[i]), sev = mod12(s[(i + 6) % 7] - s[i]);
  if (fifth === 6) return color >= 1 ? 10 : 3;
  if (fifth === 8) return 4;
  if (third === 4) {
    if (sev === 11) return color >= 3 ? (i === 0 ? 12 : 7) : color >= 1 ? 7 : 1;
    return color >= 3 ? 14 : color >= 2 ? 9 : 1;
  }
  if (sev === 10) {
    const ninth = mod12(s[(i + 1) % 7] - s[i]);
    return color >= 3 && ninth === 2 ? 13 : color >= 1 ? 8 : 2;
  }
  return color >= 2 ? 20 : 2;
}

// a chord resolved: {root, bass, q, iv}
export function resolve(j, c) {
  const mode = j.mode < 7 ? j.mode : 0, i = degIndex(c.deg), acc = degAcc(c.deg);
  let q = c.qual < 25 ? c.qual : 0;
  if (q === 0) q = autoQuality(mode, i, acc, j.color);
  const iv = QUAL[q][1], root = mod12(j.key + SCALE[mode][i] + acc);
  let bass = root;
  if (c.bass === 1) bass = mod12(root + (iv.length > 1 ? iv[1] : 0));
  else if (c.bass === 2) { bass = mod12(root + 7); for (const v of iv) if (v === 6 || v === 8) bass = mod12(root + v); }
  else if (c.bass === 3) { let s = 10; for (const v of iv) { if (v === 9 && q === 19) s = 9; else if (v === 10 || v === 11) s = v; } bass = mod12(root + s); }
  else if (c.bass === 4) bass = j.key;
  return { root, bass, q, iv };
}
export function chordPcs(j, c) { const r = resolve(j, c); return r.iv.map((v) => mod12(r.root + v)); }

// "6m7" and "Bm7", as the FM-1's screen writes them
export function chordText(j, c) {
  const r = resolve(j, c), acc = degAcc(c.deg), flats = keyFlats(j) || acc < 0;
  let num = (acc ? (acc < 0 ? "b" : "#") : "") + String(1 + degIndex(c.deg)) + QUAL[r.q][0];
  if (c.bass && c.bass < 5) num += ["", "/3", "/5", "/7", "/1"][c.bass];
  let name = noteName(r.root, flats) + QUAL[r.q][0];
  if (r.bass !== r.root) name += "/" + noteName(r.bass, flats);
  return { num, name };
}

// the 12 numbers a chord can have, spelled for the scale: its degrees plain, the rest flat (major) or sharp (minor)
export function degOfSemi(semi, mode) {
  const s = SCALE[mode % 7], minor = s[2] === 3;
  semi = mod12(semi);
  for (let i = 0; i < 7; i++) if (s[i] === semi) return i;
  if (minor) for (let i = 0; i < 7; i++) if ((s[i] + 1) % 12 === semi) return i | 2 << 4;
  for (let i = 0; i < 7; i++) if ((s[i] + 11) % 12 === semi) return i | 1 << 4;
  for (let i = 0; i < 7; i++) if ((s[i] + 1) % 12 === semi) return i | 2 << 4;
  return 0;
}
export const semiOfDeg = (deg, mode) => mod12(SCALE[mode % 7][degIndex(deg)] + degAcc(deg));
export const degLabel = (deg) => (degAcc(deg) ? (degAcc(deg) < 0 ? "b" : "#") : "") + String(1 + degIndex(deg));

// ---------------------------------------------------------------- progressions in text (jt_parse_prog's)
function qualParse(s) {
  let best = 0, bl = 0;
  for (let q = 1; q < 25; q++) {
    const l = QUAL[q][0];
    if (l && s.startsWith(l) && l.length > bl) { bl = l.length; best = q; }
  }
  if (!bl && s[0] === "M" && s[1] !== "A") return [1, 1];
  return [best, bl];
}
export function parseProg(text, beatsDefault = 4) {
  const out = [];
  for (const tok of text.trim().split(/[\s|,]+/).filter(Boolean)) {
    let s = tok, acc = 0;
    if (out.length >= NCHORD) throw new Error("12 chords at most");
    if (s[0] === "b") { acc = 1; s = s.slice(1); } else if (s[0] === "#") { acc = 2; s = s.slice(1); }
    if (!/^[1-7]/.test(s)) throw new Error(`"${tok}": a chord starts with a number 1 to 7`);
    const deg = (s.charCodeAt(0) - 49) | acc << 4;
    s = s.slice(1);
    const [qual, ql] = qualParse(s);
    s = s.slice(ql);
    let bass = 0;
    if (s[0] === "/") { bass = { 3: 1, 5: 2, 7: 3, 1: 4 }[s[1]] || 0; s = s.slice(2); }
    let beats = beatsDefault;
    if (s[0] === ":") { beats = Math.max(1, Math.min(16, parseInt(s.slice(1), 10) || 1)); s = s.replace(/^:\d+/, ""); }
    if (s) throw new Error(`"${tok}": cannot read "${s}"`);
    out.push({ deg, qual, beats, bass });
  }
  if (!out.length) throw new Error("no chords");
  return out;
}
export function progText(chords, beatsDefault = 4) {
  return chords.map((c) => {
    const num = degLabel(c.deg) + (c.qual ? (c.qual === 1 ? "M" : QUAL[c.qual][0]) : "") +
      (c.bass ? ["", "/3", "/5", "/7", "/1"][c.bass] : "");
    return c.beats === beatsDefault ? num : `${num}:${c.beats}`;
  }).join(" ");
}

// ---------------------------------------------------------------- the 192 bytes
const s8 = (v) => (v << 24) >> 24;
export function decode(b) {
  b = Uint8Array.from(b);
  if (b.length < JAM_BYTES) throw new Error("not a jam");
  const name = String.fromCharCode(...b.slice(4, 20)).replace(/\0[\s\S]*$/, "");
  const part = (o) => {
    const p = { n: b[o], energy: b[o + 1], reps: b[o + 2], var: b[o + 3], c: [] };
    for (let i = 0; i < NCHORD; i++) p.c.push({ deg: b[o + 4 + 4 * i], qual: b[o + 5 + 4 * i], beats: b[o + 6 + 4 * i], bass: b[o + 7 + 4 * i] });
    return p;
  };
  return {
    name, key: b[20], mode: b[21], meter: b[22], genre: b[23], bpm10: b[24] | (b[25] << 8), swing: b[26], human: b[27],
    fills: b[28], push: b[29], color: b[30], chain: b[31], kit: b[32], groove: b[33], bstyle: b[34], cstyle: b[35],
    sstyle: b[36], count: b[37], patch: [b[38], b[39], b[40]], mute: b[41], level: [...b.slice(42, 46)],
    tone: [...b.slice(46, 50)], verb: [...b.slice(50, 54)], oct: [...b.slice(54, 58)].map(s8), rev: b[58], space: b[59],
    tape: b[60], glue: b[61], width: b[62], seed: b[63], switchAt: b[64], part: [part(88), part(140)],
  };
}
export function encode(j) {
  const b = new Uint8Array(JAM_BYTES);
  b[0] = 0x4A; b[1] = 0x41; b[2] = 0x4D; b[3] = 0x31;
  const nm = (j.name || "").toUpperCase().replace(/[^A-Z0-9 .'-]/g, " ").slice(0, NAME - 1);
  for (let i = 0; i < nm.length; i++) b[4 + i] = nm.charCodeAt(i);
  const u8 = (o, v) => { b[o] = Math.max(0, Math.min(255, Math.round(v || 0))); };
  u8(20, j.key); u8(21, j.mode); u8(22, j.meter); u8(23, j.genre);
  const bpm = Math.max(400, Math.min(2400, Math.round(j.bpm10)));
  b[24] = bpm & 255; b[25] = bpm >> 8;
  u8(26, j.swing); u8(27, j.human); u8(28, j.fills); u8(29, j.push); u8(30, j.color); u8(31, j.chain); u8(32, j.kit);
  u8(33, j.groove); u8(34, j.bstyle); u8(35, j.cstyle); u8(36, j.sstyle); u8(37, j.count);
  for (let i = 0; i < 3; i++) u8(38 + i, j.patch[i]);
  u8(41, j.mute);
  for (let i = 0; i < 4; i++) { u8(42 + i, j.level[i]); u8(46 + i, j.tone[i]); u8(50 + i, j.verb[i]); b[54 + i] = j.oct[i] & 255; }
  u8(58, j.rev); u8(59, j.space); u8(60, j.tape); u8(61, j.glue); u8(62, j.width); u8(63, j.seed); u8(64, j.switchAt);
  j.part.forEach((p, k) => {
    const o = 88 + 52 * k;
    u8(o, p.n); u8(o + 1, p.energy); u8(o + 2, p.reps); u8(o + 3, p.var);
    for (let i = 0; i < NCHORD; i++) {
      const c = p.c[i] || { deg: 0, qual: 0, beats: 4, bass: 0 };
      u8(o + 4 + 4 * i, c.deg); u8(o + 5 + 4 * i, c.qual); u8(o + 6 + 4 * i, c.beats); u8(o + 7 + 4 * i, c.bass);
    }
  });
  return b;
}
export const fromHex = (h) => decode(Uint8Array.from(h.match(/../g).map((x) => parseInt(x, 16))));
export const clone = (j) => JSON.parse(JSON.stringify(j));

// a jam as a file people can keep and share (JSON, readable: the progressions as text)
export function toFile(j) {
  const bb = [4, 3, 2, 4, 2, 5, 3][j.meter] || 4;
  return JSON.stringify({ jamtrack: 1, ...clone(j), progA: progText(j.part[0].c.slice(0, j.part[0].n), bb),
                          progB: progText(j.part[1].c.slice(0, j.part[1].n), bb) }, null, 1);
}
export function fromFile(text) {
  const o = JSON.parse(text);
  if (!o.jamtrack || !o.part) throw new Error("not a JAMTRACK jam file");
  delete o.jamtrack; delete o.progA; delete o.progB;
  return decode(encode(o));             // through the bytes: every field in range as the FM-1 keeps it
}
