// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// JAMTRACK companion: the jam editor. Every setting of the jam, the two progressions as chips and as text, live on the
// FM-1 as you change them; the chord playing lit as it plays.
import { app, $, el, msg, edited, transport, OP, J } from "./app.js";

const T = () => app.tables;
const barBeats = () => T().barBeats[app.jam.meter] || 4;
let sel = [0, 0];                         // the chord picked in each part

// ---------------------------------------------------------------- small builders
function select(label, options, get, set, opts = {}) {
  const s = el("select", { title: opts.title || "" });
  options.forEach((o, i) => { if (o != null) s.append(el("option", { value: i }, o)); });
  s.value = String(get());
  s.addEventListener("change", () => { set(+s.value); edited(!!opts.redraw); });
  return el("label", { class: "field" }, el("span", {}, label), s);
}
function range(label, min, max, get, set, fmt = (v) => v, step = 1) {
  const r = el("input", { type: "range", min, max, step }), out = el("output", {}, fmt(get()));
  r.value = String(get());
  r.addEventListener("input", () => { set(+r.value); out.textContent = fmt(+r.value); edited(false); });
  return el("label", { class: "field range" }, el("span", {}, label), r, out);
}
function check(label, get, set) {
  const c = el("input", { type: "checkbox" });
  c.checked = !!get();
  c.addEventListener("change", () => { set(c.checked ? 1 : 0); edited(false); });
  return el("label", { class: "field chk" }, c, el("span", {}, label));
}
const patchOpts = (kind) => T().patches.map((n, i) => (T().patchKind[i] & kind ? n : null));

// ---------------------------------------------------------------- the song
function song() {
  const j = app.jam;
  $("j-name").value = j.name;
  const fill = (id, opts, v) => { const s = $(id); s.replaceChildren(...opts.map((o, i) => el("option", { value: i }, o))); s.value = String(v); };
  fill("j-key", J.KEYS, j.key);
  fill("j-mode", T().modes, j.mode);
  fill("j-meter", T().meters, j.meter);
  $("j-bpm").value = String(j.bpm10 / 10);
}

// ---------------------------------------------------------------- a part: its chips, the chord picked, its text
function part(k) {
  const j = app.jam, p = j.part[k], box = document.querySelector(`.part[data-part="${k}"]`);
  const st = app.status, live = st && st.playing && st.part === k;
  if (sel[k] >= p.n) sel[k] = p.n - 1;
  const beats = p.c.slice(0, p.n).reduce((a, c) => a + c.beats, 0), bb = barBeats();
  const len = `${Math.floor(beats / bb)} bar${Math.floor(beats / bb) === 1 ? "" : "s"}${beats % bb ? ` + ${beats % bb}` : ""}`;
  const head = el("div", { class: "row between wrap gap" },
    el("h2", {}, `Part ${"AB"[k]} `, el("span", { class: "muted small" }, len)),
    el("div", { class: "row gap wrap" },
      range("Energy", 0, 4, () => p.energy, (v) => { p.energy = v; }),
      check("Other beat", () => p.var, (v) => { p.var = v; }),
      range("Rounds", 1, 8, () => p.reps, (v) => { p.reps = v; }),
      el("button", { type: "button", class: "ghost", onclick: () => transport(OP.PART, k) }, `Go to ${"AB"[k]}`)));
  const chips = el("div", { class: "chips" });
  p.c.slice(0, p.n).forEach((c, i) => {
    const t = J.chordText(j, c);
    const b = el("button", { type: "button", class: "chip" + (i === sel[k] ? " sel" : "") + (live && st.chord === i ? " live" : ""),
                             style: `flex-grow:${c.beats}`, title: `${c.beats} beats`, "data-i": i },
      el("b", {}, t.name), el("small", {}, t.num));
    b.addEventListener("click", () => { sel[k] = i; part(k); audition(k, i); });
    chips.append(b);
  });
  if (p.n < 12) chips.append(el("button", { type: "button", class: "chip add", title: "add a chord", onclick: () => {
    p.c.splice(sel[k] + 1, 0, { ...p.c[sel[k]] }); p.c.length = 12; p.n++; sel[k]++; edited(); part(k); } }, "+"));
  // the chord picked
  const c = p.c[sel[k]], semi = J.semiOfDeg(c.deg, j.mode);
  const nums = el("div", { class: "nums" });
  for (let d = 0; d < 7; d++) {         // the key's own chords, by name (the number small)
    const t = J.chordText(j, { deg: d, qual: 0, beats: 4, bass: 0 });
    const b = el("button", { type: "button", class: c.deg === d ? "on" : "" }, t.name, el("small", {}, t.num));
    b.addEventListener("click", () => { c.deg = d; c.qual = 0; edited(); part(k); audition(k, sel[k]); });
    nums.append(b);
  }
  const other = el("details", { class: "other" }, el("summary", {}, "Other chords"));
  const row = el("div", { class: "nums small" });
  for (let x = 0; x < 12; x++) {         // every root, for borrowed chords
    const d = J.degOfSemi(x, j.mode);
    const t = J.chordText(j, { deg: d, qual: c.qual, beats: 4, bass: 0 });
    const b = el("button", { type: "button", class: x === semi ? "on" : "" }, t.name, el("small", {}, J.degLabel(d)));
    b.addEventListener("click", () => { c.deg = d; edited(); part(k); audition(k, sel[k]); });
    row.append(b);
  }
  if ((c.deg >> 4) & 3) other.open = true;
  other.append(row);
  const ed = el("div", { class: "chord-ed" }, nums, other,
    el("div", { class: "row wrap gap" },
      select("Type", J.QUAL_MENU, () => c.qual, (v) => { c.qual = v; part(k); audition(k, sel[k]); }),
      range("Beats", 1, 16, () => c.beats, (v) => { c.beats = v; }),
      select("Bass", J.BASSES, () => c.bass, (v) => { c.bass = v; part(k); }),
      el("div", { class: "row gap" },
        el("button", { type: "button", class: "ghost", title: "earlier", onclick: () => move(k, -1) }, "◀"),
        el("button", { type: "button", class: "ghost", title: "later", onclick: () => move(k, 1) }, "▶"),
        el("button", { type: "button", class: "ghost", onclick: () => audition(k, sel[k]) }, "Hear it"),
        el("button", { type: "button", class: "ghost", disabled: p.n <= 1, onclick: () => {
          p.c.splice(sel[k], 1); p.c.push({ deg: 0, qual: 0, beats: 4, bass: 0 }); p.n--; edited(); part(k); } }, "Remove"))));
  // as text
  const txt = el("input", { class: "mono prog", spellcheck: "false", value: J.progText(p.c.slice(0, p.n), bb),
                            title: "Nashville numbers: 1maj7 6m7 4 5sus4:2 5:2 (lengths in beats; a bar when left out)" });
  const err = el("span", { class: "msg small" });
  const apply = () => {
    try {
      const cs = J.parseProg(txt.value, bb);
      p.c = [...cs, ...Array.from({ length: 12 - cs.length }, () => ({ deg: 0, qual: 0, beats: bb, bass: 0 }))];
      p.n = cs.length;
      err.textContent = "";
      edited();
      part(k);
    } catch (e) { err.textContent = e.message; err.className = "msg small err"; }
  };
  txt.addEventListener("keydown", (e) => { if (e.key === "Enter") apply(); });
  txt.addEventListener("blur", () => { if (txt.value !== J.progText(p.c.slice(0, p.n), bb)) apply(); });
  box.replaceChildren(head, chips, ed, el("div", { class: "row gap textrow" }, el("span", { class: "muted small" }, "As text"), txt, err));
}
function move(k, d) {
  const p = app.jam.part[k], i = sel[k], to = i + d;
  if (to < 0 || to >= p.n) return;
  [p.c[i], p.c[to]] = [p.c[to], p.c[i]];
  sel[k] = to;
  edited();
  part(k);
}
function audition(k, i) {
  if (app.connected && !(app.status && app.status.playing)) app.jt.transport(OP.AUDITION, k, i).catch(() => {});
}
// only the chips' lights, as the music plays (no rebuilding under the mouse)
function liveChips() {
  const st = app.status;
  for (const k of [0, 1])
    for (const b of document.querySelectorAll(`.part[data-part="${k}"] .chip[data-i]`))
      b.classList.toggle("live", !!st && st.playing && st.part === k && st.chord === +b.dataset.i);
  for (const k of [0, 1]) document.querySelector(`.part[data-part="${k}"]`).classList.toggle("playing", !!st && st.playing && st.part === k);
}

// ---------------------------------------------------------------- the band, the feel, the mix, the effects
function band() {
  const j = app.jam;
  $("band").replaceChildren(
    select("Groove", T().grooves, () => j.groove, (v) => { j.groove = v; }),
    select("Kit", T().kits, () => j.kit, (v) => { j.kit = v; }),
    select("Bass line", T().bass, () => j.bstyle, (v) => { j.bstyle = v; }),
    select("Bass sound", patchOpts(1), () => j.patch[0], (v) => { j.patch[0] = v; }),
    select("Chords", T().comp, () => j.cstyle, (v) => { j.cstyle = v; }),
    select("Chord sound", patchOpts(2), () => j.patch[1], (v) => { j.patch[1] = v; }),
    select("Synth line", T().synth, () => j.sstyle, (v) => { j.sstyle = v; lineEd(); }),
    select("Synth sound", patchOpts(2), () => j.patch[2], (v) => { j.patch[2] = v; }));
  $("feel").replaceChildren(
    range("Swing", 0, 100, () => j.swing, (v) => { j.swing = v; }, (v) => (v === 50 ? "50 (its own)" : v)),
    range("Human", 0, 100, () => j.human, (v) => { j.human = v; }),
    check("Changes on the push (an eighth early)", () => j.push, (v) => { j.push = v; }),
    select("Colour", J.COLORS, () => j.color, (v) => { j.color = v; }, { redraw: true }),
    select("Fills", J.FILLS, () => j.fills, (v) => { j.fills = v; }),
    select("A and B", J.CHAINS, () => j.chain, (v) => { j.chain = v; }),
    select("Change part at", J.SWITCHES, () => j.switchAt, (v) => { j.switchAt = v; }),
    select("Count-in", ["off", "1 bar", "2 bars"], () => j.count, (v) => { j.count = v; }),
    el("label", { class: "field" }, el("span", {}, "Variation"),
      el("div", { class: "row gap" }, Object.assign(el("input", { type: "number", min: 0, max: 255, value: j.seed, class: "seed",
        onchange: (e) => { j.seed = Math.max(0, Math.min(255, +e.target.value | 0)); edited(); } })),
        el("button", { type: "button", class: "ghost", title: "another hook, other ghost notes and fills",
          onclick: () => { j.seed = (Math.random() * 256) | 0; edited(true); } }, "🎲"))));
  const strips = J.PARTS.map((name, p) => el("div", { class: `strip p${p}` },
    el("div", { class: "row between" }, el("b", {}, name),
      el("button", { type: "button", class: "mute" + ((j.mute >> p) & 1 ? " on" : ""), onclick: (e) => {
        j.mute ^= 1 << p; e.target.classList.toggle("on"); edited(); if (app.connected) app.jt.transport(OP.MUTE, p, (j.mute >> p) & 1).catch(() => {});
      } }, "M")),
    el("div", { class: "meter" }, el("i", { id: `meter${p}` })),
    range("Level", 0, 100, () => j.level[p], (v) => { j.level[p] = v; }),
    range("Tone", 0, 100, () => j.tone[p], (v) => { j.tone[p] = v; }),
    range("Reverb", 0, 100, () => j.verb[p], (v) => { j.verb[p] = v; }),
    range(p ? "Octave" : "Tuning", -2, 2, () => j.oct[p], (v) => { j.oct[p] = v; }, (v) => (v > 0 ? "+" + v : v))));
  $("mix").replaceChildren(...strips);
  keys();
  $("fx").replaceChildren(
    range("Reverb", 0, 100, () => j.rev, (v) => { j.rev = v; }),
    range("Space (room … hall)", 0, 100, () => j.space, (v) => { j.space = v; }),
    range("Tape", 0, 100, () => j.tape, (v) => { j.tape = v; }),
    range("Glue (drums)", 0, 100, () => j.glue, (v) => { j.glue = v; }),
    range("Width", 0, 100, () => j.width, (v) => { j.width = v; }));
}

// the keys' own sound (the FM-1's settings, not the jam's)
let settings = null;
async function loadSettings() { if (app.connected) { try { settings = await app.jt.settings(); keys(); } catch { /* */ } } }
function keys() {
  const box = $("keys");
  if (!box) return;
  if (!settings) { box.replaceChildren(el("p", { class: "muted small" }, "Connect the FM-1 to set what its keys play.")); return; }
  const e = settings.eng, put = () => app.jt.settings(settings).catch((er) => msg("jam-msg", er.message, "err"));
  const sel = el("select", {});
  app.tables.patches.forEach((n, i) => { if (app.tables.patchKind[i] & 2) sel.append(el("option", { value: i }, n)); });
  sel.value = String(e[4]);
  sel.addEventListener("change", () => { e[4] = +sel.value; put(); });
  const rng = (label, idx) => {
    const r = el("input", { type: "range", min: 0, max: 100 }), o = el("output", {}, e[idx]);
    r.value = String(e[idx]);
    r.addEventListener("change", () => { e[idx] = +r.value; o.textContent = r.value; put(); });
    return el("label", { class: "field range" }, el("span", {}, label), r, o);
  };
  const mono = el("input", { type: "checkbox" });
  mono.checked = !!e[9];
  mono.addEventListener("change", () => { e[9] = mono.checked ? 1 : 0; put(); });
  box.replaceChildren(el("label", { class: "field" }, el("span", {}, "Sound"), sel), rng("Level", 8),
    el("label", { class: "field chk" }, mono, el("span", {}, "Mono: one note, sliding to the next (a lead)")), rng("Reverb", 10));
}

// ---------------------------------------------------------------- MY LINE: the synth's own steps, as a piano roll
const syUser = () => T().synth.indexOf("MY LINE");
const ROWS = J.LN_TOP;                    // rows 22 (top) .. 1
let drag = null;                          // {step, row}: a note being drawn out
function lineSet(fn, redrawBand = false) {
  const j = app.jam;
  if (!j.line) j.line = new Array(J.LINE).fill(0);
  fn(j);
  let sw = false;
  if (j.sstyle !== syUser()) { j.sstyle = syUser(); sw = true; }
  edited(false);
  if (sw || redrawBand) band();
  lineEd();
}
function lineEd() {
  const j = app.jam, box = $("line");
  if (!box) return;
  if (!j.line) j.line = new Array(J.LINE).fill(0);
  const len = J.lineLen(j), roll = el("div", { class: "roll" });
  roll.append(el("div", { class: "head" }));
  for (let i = 0; i < J.LINE; i++) roll.append(el("div", { class: "head", "data-step": i }, i % 4 === 0 ? String(i / 4 + 1) : ""));
  // where each step's note is, holds included
  const at = new Array(J.LINE).fill(null);
  for (let i = 0; i < len; i++) {
    const n = J.lineNote(j.line[i]);
    if (!n) continue;
    at[i] = { row: n, start: i, vel: J.lineVel(j.line[i]) };
    for (let k = i + 1; k < len && J.isHold(j.line[k]); k++) at[k] = { row: n, start: i, vel: J.lineVel(j.line[i]), hold: true };
  }
  for (let row = ROWS; row >= 1; row--) {
    const d = row - J.LN_ROOT, one = ((d % 7) + 7) % 7 === 0;
    roll.append(el("div", { class: "lab" + (one ? " one" : "") }, J.lineRowName(j, row)));
    for (let i = 0; i < J.LINE; i++) {
      const a = at[i] && at[i].row === row ? at[i] : null;
      const cls = ["c", i % 4 === 0 ? "beat" : "", one ? "one" : "", i >= len ? "out" : "",
                   a ? (a.hold ? "h" : "n") + " v" + a.vel : ""].join(" ");
      roll.append(el("div", { class: cls, "data-step": i, "data-row": row }));
    }
  }
  roll.addEventListener("contextmenu", (e) => e.preventDefault());
  roll.addEventListener("pointerdown", (e) => {
    const c = e.target.closest(".c");
    if (!c || c.classList.contains("out")) return;
    const i = +c.dataset.step, row = +c.dataset.row, a = at[i] && at[i].row === row ? at[i] : null;
    if (e.button === 2 || e.altKey) {     // how hard: normal, loud, accent, soft
      if (a) lineSet((jj) => { const v = jj.line[a.start]; jj.line[a.start] = (v & 0x3F) | ([2, 0, 3, 1][v >> 6] << 6); });
      return;
    }
    if (a && !a.hold) {                   // its start again: out, holds and all
      lineSet((jj) => { jj.line[i] = 0; for (let k = i + 1; k < J.LINE && J.isHold(jj.line[k]); k++) jj.line[k] = 0; });
      return;
    }
    if (a && a.hold) {                    // inside a held note: it ends here
      lineSet((jj) => { for (let k = i; k < J.LINE && J.isHold(jj.line[k]); k++) jj.line[k] = 0; });
      return;
    }
    lineSet((jj) => {                     // a new note (what was there goes; a note it cut short keeps its start)
      for (let k = i + 1; k < J.LINE && J.isHold(jj.line[k]); k++) jj.line[k] = 0;
      jj.line[i] = row | ((i % 4 === 0 ? 2 : 0) << 6);
    });
    drag = { step: i, row };
    roll.setPointerCapture?.(e.pointerId);
  });
  roll.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const c = document.elementFromPoint(e.clientX, e.clientY)?.closest(".roll .c");
    if (!c) return;
    const to = Math.min(+c.dataset.step, len - 1);
    if (to <= drag.step) return;
    const jj = app.jam;
    let changed = false;
    for (let k = drag.step + 1; k < J.LINE; k++) {
      const want = k <= to ? J.LN_HOLD : (J.isHold(jj.line[k]) ? 0 : jj.line[k]);
      if (jj.line[k] !== want) { jj.line[k] = want; changed = true; }
    }
    if (changed) { edited(false); lineEd(); }
  });
  const end = () => { drag = null; };
  roll.addEventListener("pointerup", end);
  roll.addEventListener("pointercancel", end);
  const mode = ((j.lineSet & J.LS_KEY) ? 1 : 0);
  const tools = el("div", { class: "row gap wrap line-tools" },
    j.sstyle === syUser() ? el("span", { class: "muted small" }, "The synth plays this line.")
      : el("button", { type: "button", onclick: () => lineSet(() => {}) }, "Play this line on the synth"),
    select("Length", Array.from({ length: 16 }, (_, k) => `${k + 1} step${k ? "s" : ""}`), () => len - 1,
      (v) => { j.lineSet = (j.lineSet & 0xF0) | (v + 1 === 16 ? 0 : v + 1); lineEd(); }),
    select("Follows", ["the chords", "the key"], () => mode,
      (v) => { j.lineSet = (j.lineSet & ~J.LS_KEY) | (v ? J.LS_KEY : 0); }),
    check("Slide note to note", () => j.lineSet & J.LS_SLIDE, (v) => { j.lineSet = (j.lineSet & ~J.LS_SLIDE) | (v ? J.LS_SLIDE : 0); }),
    el("button", { type: "button", class: "ghost", title: "every note a step of the scale higher", onclick: () => lineSet((jj) => {
      jj.line = jj.line.map((v) => (J.lineNote(v) && J.lineNote(v) < J.LN_TOP ? v + 1 : v)); }) }, "▲"),
    el("button", { type: "button", class: "ghost", title: "every note a step of the scale lower", onclick: () => lineSet((jj) => {
      jj.line = jj.line.map((v) => (J.lineNote(v) > 1 ? v - 1 : v)); }) }, "▼"),
    el("button", { type: "button", class: "ghost", title: "the line a step later", onclick: () => lineSet((jj) => {
      const l = J.lineLen(jj), a = jj.line.slice(0, l); a.unshift(a.pop()); if (J.isHold(a[0])) a[0] = 0; jj.line.splice(0, l, ...a); }) }, "→"),
    el("button", { type: "button", class: "ghost danger", onclick: () => lineSet((jj) => { jj.line = new Array(J.LINE).fill(0); }) }, "Clear"));
  box.replaceChildren(roll, tools);
  liveLine();
}
function liveLine() {                     // the playhead
  const st = app.status, j = app.jam, box = $("line");
  if (!box || !j) return;
  const on = !!st && st.playing && !st.counting && st.step >= 0 && j.sstyle === syUser();
  const ph = on ? st.step % J.lineLen(j) : -1;
  for (const c of box.querySelectorAll("[data-step]")) c.classList.toggle("play", +c.dataset.step === ph);
}

function all() {
  song();
  part(0);
  part(1);
  band();
  lineEd();
}

function live(s) {
  const t = s.counting ? `counting in` : s.playing ? `bar ${s.bar + 1} · beat ${s.beat + 1} · part ${"AB"[s.part]}${s.queued ? " → " + "AB"[s.part ^ 1] : ""}` : "stopped";
  $("live").textContent = `${t} · ${s.bpm.toFixed(1)} ${s.ext ? "EXT" : "BPM"}`;
  $("live").classList.toggle("ext", s.ext);
  for (let p = 0; p < 4; p++) { const m = $(`meter${p}`); if (m) m.style.width = `${s.levels[p]}%`; }
  liveChips();
  liveLine();
}

export function init() {
  $("j-name").addEventListener("input", (e) => { app.jam.name = e.target.value.toUpperCase().replace(/[^A-Z0-9 .'-]/g, ""); edited(); });
  $("j-key").addEventListener("change", (e) => { app.jam.key = +e.target.value; edited(); part(0); part(1); });
  $("j-mode").addEventListener("change", (e) => { J.modeSet(app.jam, +e.target.value); edited(); part(0); part(1); });
  $("j-meter").addEventListener("change", (e) => { app.jam.meter = +e.target.value; edited(); part(0); part(1); });
  $("j-bpm").addEventListener("change", (e) => { app.jam.bpm10 = Math.round(Math.max(40, Math.min(240, +e.target.value || 100)) * 10); edited(); });
  const g = $("g-genre");
  g.replaceChildren(...T().genres.map((n, i) => el("option", { value: i }, n)));
  try { g.value = localStorage.getItem("jt.genre") || "0"; } catch { /* */ }
  g.addEventListener("change", () => { try { localStorage.setItem("jt.genre", g.value); } catch { /* */ } });
  const gen = (what) => async () => {
    if (!app.connected) { msg("jam-msg", "GENERATE runs on the FM-1: connect it.", "err"); return; }
    if (await transport(OP.GENERATE, +g.value, what)) setTimeout(() => app.emit("pull"), 150);
  };
  $("g-all").addEventListener("click", gen(0));
  $("g-prog").addEventListener("click", gen(1));
  $("g-groove").addEventListener("click", gen(2));
  $("g-sound").addEventListener("click", gen(3));
  $("g-undo").addEventListener("click", async () => { if (await transport(OP.UNDO)) setTimeout(() => app.emit("pull"), 150); });
  $("t-play").addEventListener("click", () => transport(OP.PLAY));
  $("t-stop").addEventListener("click", () => transport(app.status && app.status.playing ? OP.STOP : OP.STOP_NOW));
  $("t-part").addEventListener("click", () => transport(OP.PART, 2));
  $("t-fill").addEventListener("click", () => transport(OP.FILL));
  $("t-tap").addEventListener("click", async () => { if (await transport(OP.TAP)) setTimeout(() => app.emit("pull"), 100); });
  $("t-top").addEventListener("click", () => transport(OP.RESTART));
  app.on("jam", (why) => { if (why !== "edit-quiet") all(); });
  app.on("pull", async () => { const { pullJam } = await import("./app.js"); pullJam("generate"); });
  app.on("status", live);
  app.on("disconnected", () => { $("live").textContent = "—"; settings = null; keys(); });
  app.on("connected", loadSettings);
}
