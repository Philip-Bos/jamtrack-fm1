// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// JAMTRACK companion: the connection, the tabs, the jam being edited and what the pages share. The jam is the FM-1's
// when one is connected (edits go to it as you make them, and its panel's come back), else a jam in this browser.
import { Link, JamTrack, midiTransport, OP } from "./lib/jtproto.js";
import { pairs, usualName } from "./vendor/fm1-base/fm1ports.js";
import * as J from "./lib/jam.js";
import * as editor from "./editor.js";
import * as library from "./library.js";
import * as kit from "./kit.js";
import * as screen from "./screen.js";

export const $ = (id) => document.getElementById(id);
export const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) e.setAttribute(k, v === true ? "" : v);
  }
  for (const k of kids.flat()) if (k != null) e.append(k.nodeType ? k : String(k));
  return e;
};
export function msg(id, text, kind = "") { const m = $(id); if (m) { m.textContent = text; m.className = "msg " + kind; } }
export function progress(id, frac) {
  const p = $(id);
  p.hidden = frac == null;
  if (frac != null) p.firstElementChild.style.width = `${Math.round(Math.max(0, Math.min(1, frac)) * 100)}%`;
}
export function download(name, data, type = "application/octet-stream") {
  const a = el("a", { href: URL.createObjectURL(new Blob([data], { type })), download: name });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// ---------------------------------------------------------------- shared state and events
export const app = {
  access: null, link: null, jt: null, port: "", busy: 0, wanted: false,
  tables: null, jam: null, status: null,
  get info() { return this.jt ? this.jt.info : null; },
  get connected() { return !!this.jt && !!this.info; },
  fns: new Map(),
  on(ev, fn) { if (!this.fns.has(ev)) this.fns.set(ev, new Set()); this.fns.get(ev).add(fn); },
  emit(ev, ...a) { for (const fn of this.fns.get(ev) || []) try { fn(...a); } catch (e) { console.error(e); } },
};

function pill(text, kind) { const s = $("status"); s.textContent = text; s.className = "pill " + kind; }

export async function midiAccess() {
  if (app.access) return app.access;
  if (!navigator.requestMIDIAccess) throw new Error("This browser has no Web MIDI: use Chrome, Edge or Opera on a computer.");
  if (!isSecureContext) throw new Error("Web MIDI needs a secure page: open this from https or from localhost (companion/serve.py).");
  app.access = await navigator.requestMIDIAccess({ sysex: true });
  app.access.onstatechange = (e) => {
    if (e.port.state === "disconnected" && app.port && e.port.name === app.port) lost();
    else if (e.port.state === "connected" && app.wanted && !app.connected && !app.busy) setTimeout(() => connect(true), 600);
  };
  return app.access;
}

function lost() {
  if (!app.link) return;
  try { app.link.close(); } catch { /* gone */ }
  app.link = app.jt = null;
  app.port = "";
  pill("disconnected", "off");
  $("connect").hidden = false;
  app.emit("disconnected");
}

// the FM-1 running JAMTRACK: a port with an FM-1's name (or JAMTRACK's) that answers JAMTRACK's HELLO
export async function connect(quiet = false) {
  if (app.connected || app.busy) return app.connected;
  app.wanted = true;
  try {
    const access = await midiAccess();
    pill("looking…", "off");
    for (const p of pairs(access)) {
      if (!usualName(p.name) && !/jamtrack|felucca/i.test(p.name)) continue;
      if (/update|^ota|uboot/i.test(p.name)) continue;
      try { await p.input.open(); await p.output.open(); } catch { continue; }
      const link = new Link(midiTransport(p.input, p.output)), jt = new JamTrack(link);
      const info = await jt.hello();
      if (!info) { link.close(); continue; }
      app.link = link; app.jt = jt; app.port = p.name;
      pill(`JAMTRACK ${info.version}`, "on");
      $("connect").hidden = true;
      jt.onNotify(() => pullJam("panel"));
      await pullJam("connect");
      app.emit("connected", info);
      return true;
    }
    pill("not found", "warn");
    if (!quiet) msg("jam-msg", "No FM-1 running JAMTRACK answered. Check the cable, close other apps using it (M-UPGRADE, a DAW), or install JAMTRACK.", "err");
  } catch (e) {
    pill("no MIDI", "warn");
    if (!quiet) msg("jam-msg", e.message, "err");
  }
  return false;
}

// ---------------------------------------------------------------- the jam
let pushTimer = null, lastEdit = 0;
export async function pullJam(why) {
  if (!app.connected) return;
  if (why === "panel" && Date.now() - lastEdit < 700) return;   // (our own edit coming back)
  try {
    const r = await app.jt.jamGet(0xFF);
    app.jam = J.decode(r.bytes);
    app.emit("jam", why);
  } catch (e) { msg("jam-msg", e.message, "err"); }
}
// the jam changed here: on screen at once, to the FM-1 a moment later (a knob dragged sends a few, not hundreds)
export function edited(redraw = false) {
  lastEdit = Date.now();
  try { localStorage.setItem("jt.jam", JSON.stringify(app.jam)); } catch { /* private window */ }
  if (redraw) app.emit("jam", "edit");
  if (!app.connected) return;
  clearTimeout(pushTimer);
  pushTimer = setTimeout(async () => {
    try { await app.jt.jamPut(0xFF, J.encode(app.jam)); } catch (e) { msg("jam-msg", e.message, "err"); }
  }, 120);
}
export function setJam(j, why = "load") { app.jam = J.clone(j); edited(false); app.emit("jam", why); }

export async function transport(op, a = 0, b = 0) {
  if (!app.connected) { msg("jam-msg", "Connect the FM-1 to play.", "err"); return false; }
  try { await app.jt.transport(op, a, b); return true; } catch (e) { msg("jam-msg", e.message, "err"); return false; }
}

// ---------------------------------------------------------------- tabs, status
function tab(name) {
  for (const b of document.querySelectorAll(".tabs button")) b.setAttribute("aria-selected", String(b.dataset.tab === name));
  for (const s of document.querySelectorAll("main > .tab")) s.hidden = s.id !== "tab-" + name;
  try { localStorage.setItem("jt.tab", name); } catch { /* */ }
  app.emit("tab", name);
}

async function poll() {
  if (app.connected && !app.link.busy && !$("tab-jam").hidden) {
    const s = await app.jt.status();
    if (s) {
      const changed = !app.status || s.slot !== app.status.slot || s.dirty !== app.status.dirty;
      app.status = s;
      app.emit("status", s);
      if (changed) app.emit("slot", s);
    }
  }
  setTimeout(poll, 250);
}

async function init() {
  app.tables = await (await fetch("lib/tables.json")).json();
  try { app.jam = JSON.parse(localStorage.getItem("jt.jam")); } catch { app.jam = null; }
  if (!app.jam || !app.jam.part) app.jam = J.fromHex(app.tables.factory[0]);
  for (const b of document.querySelectorAll(".tabs button")) b.addEventListener("click", () => tab(b.dataset.tab));
  $("connect").addEventListener("click", () => connect(false));
  editor.init();
  library.init();
  kit.init();
  screen.init();
  let t = "jam";
  try { t = localStorage.getItem("jt.tab") || "jam"; } catch { /* */ }
  const h = location.hash.slice(1);
  if ($("tab-" + h)) t = h;            // a link to a tab: #library, #kit, #guide
  tab(t);
  app.emit("jam", "start");
  poll();
  if (navigator.requestMIDIAccess && isSecureContext) connect(true);
}
init();

export { OP, J };
