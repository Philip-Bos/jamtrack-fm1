// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// JAMTRACK companion: the library. The FM-1's 32 places, the jams kept in this browser (and as files), the factory's.
import { app, $, el, msg, download, setJam, transport, pullJam, OP, J } from "./app.js";

const LOCAL = "jt.library";
const local = () => { try { return JSON.parse(localStorage.getItem(LOCAL)) || []; } catch { return []; } };
const saveLocal = (list) => { try { localStorage.setItem(LOCAL, JSON.stringify(list)); } catch (e) { msg("lib-msg", "This browser would not keep it: " + e.message, "err"); } };
const fileName = (j) => `${(j.name || "jam").toLowerCase().replace(/[^a-z0-9]+/g, "-")}.jam.json`;
const desc = (j) => `${J.keyText(j)} · ${app.tables.meters[j.meter]} · ${(j.bpm10 / 10).toFixed(0)} BPM · ${app.tables.grooves[j.groove] || ""}`;

let user = [];

async function refresh() {
  if (!app.connected) { user = []; draw(); return; }
  try { user = await app.jt.list(); } catch (e) { msg("lib-msg", e.message, "err"); }
  draw();
}

async function loadOnFm1(slot, j) {
  if (app.connected) {
    if (await transport(OP.LOAD, slot)) { msg("lib-msg", `${j ? j.name : "Jam"} loads at the next bar (or now, when stopped).`, "ok"); setTimeout(() => pullJam("load"), 400); }
  } else if (j) { setJam(j); msg("lib-msg", `${j.name} is in the editor.`, "ok"); }
}

function draw() {
  const st = app.status, cur = st ? st.slot : -1;
  const ul = $("lib-user");
  ul.replaceChildren();
  if (!app.connected) ul.append(el("li", { class: "muted" }, "Connect the FM-1 to see its jams."));
  for (const u of user) {
    const row = el("li", { class: u.slot === cur ? "cur" : "" },
      el("span", { class: "num" }, `U${String(u.slot + 1).padStart(2, "0")}`),
      el("span", { class: "name" }, u.used ? u.name : el("i", { class: "muted" }, "empty")),
      el("span", { class: "acts" },
        u.used && el("button", { type: "button", onclick: () => loadOnFm1(u.slot) }, "Load"),
        el("button", { type: "button", class: "ghost", onclick: async () => {
          if (u.used && !confirm(`Replace ${u.name} in U${u.slot + 1} with the jam playing?`)) return;
          try { await app.jt.slotOp(2, u.slot, ""); msg("lib-msg", `Saved in U${u.slot + 1}${app.status?.playing ? " (written when the band stops)" : ""}.`, "ok"); refresh(); }
          catch (e) { msg("lib-msg", e.message, "err"); }
        } }, "Save here"),
        u.used && el("button", { type: "button", class: "ghost", onclick: async () => {
          const r = await app.jt.jamGet(u.slot); download(fileName(J.decode(r.bytes)), J.toFile(J.decode(r.bytes)), "application/json");
        } }, "Export"),
        u.used && el("button", { type: "button", class: "ghost", onclick: async () => {
          const n = prompt("A new name (A-Z 0-9, 15 letters)", u.name);
          if (!n) return;
          try { await app.jt.slotOp(1, u.slot, n.toUpperCase().replace(/[^A-Z0-9 .'-]/g, "").slice(0, 15)); refresh(); } catch (e) { msg("lib-msg", e.message, "err"); }
        } }, "Rename"),
        u.used && el("button", { type: "button", class: "ghost danger", onclick: async () => {
          if (!confirm(`Delete ${u.name} from U${u.slot + 1}?`)) return;
          try { await app.jt.slotOp(0, u.slot); refresh(); } catch (e) { msg("lib-msg", e.message, "err"); }
        } }, "Delete")));
    ul.append(row);
  }
  const ll = $("lib-local");
  ll.replaceChildren();
  const list = local();
  if (!list.length) ll.append(el("li", { class: "muted" }, "Nothing kept here yet: Keep the jam playing here, or import jam files."));
  list.forEach((j, i) => ll.append(el("li", {},
    el("span", { class: "name" }, j.name, el("span", { class: "meta" }, desc(j))),
    el("span", { class: "acts" },
      el("button", { type: "button", onclick: async () => {
        setJam(j);
        if (app.connected) { await app.jt.jamPut(0xFF, J.encode(j)); msg("lib-msg", `${j.name} is playing on the FM-1 (not saved there yet: Save here).`, "ok"); }
      } }, "Open"),
      el("button", { type: "button", class: "ghost", onclick: () => download(fileName(j), J.toFile(j), "application/json") }, "Export"),
      el("button", { type: "button", class: "ghost danger", onclick: () => { const l = local(); l.splice(i, 1); saveLocal(l); draw(); } }, "Remove")))));
  const fl = $("lib-factory");
  fl.replaceChildren(...app.tables.factory.map((h, i) => {
    const j = J.fromHex(h);
    return el("li", {}, el("span", { class: "num" }, `F${String(i + 1).padStart(2, "0")}`),
      el("span", { class: "name" }, j.name, el("span", { class: "meta" }, `${app.tables.genres[j.genre]} · ${desc(j)}`)),
      el("span", { class: "acts" }, el("button", { type: "button", onclick: () => loadOnFm1(0x80 + i, j) }, app.connected ? "Load" : "Open")));
  }));
}

export function init() {
  $("lib-refresh").addEventListener("click", refresh);
  $("lib-keep").addEventListener("click", () => {
    const l = local();
    l.unshift(J.clone(app.jam));
    saveLocal(l);
    draw();
    msg("lib-msg", `${app.jam.name} kept in this browser.`, "ok");
  });
  $("lib-import").addEventListener("change", async (e) => {
    const l = local();
    let n = 0;
    for (const f of e.target.files) {
      try {
        const o = JSON.parse(await f.text());
        if (o.jamtrackLibrary) for (const t of o.jams) { l.push(J.fromFile(JSON.stringify(t))); n++; }
        else { l.push(J.fromFile(JSON.stringify(o))); n++; }
      } catch (err) { msg("lib-msg", `${f.name}: ${err.message}`, "err"); }
    }
    saveLocal(l);
    draw();
    if (n) msg("lib-msg", `${n} jam${n > 1 ? "s" : ""} imported.`, "ok");
    e.target.value = "";
  });
  $("lib-backup").addEventListener("click", async () => {
    if (!app.connected) { msg("lib-msg", "Connect the FM-1 first.", "err"); return; }
    const jams = [];
    for (const u of user.filter((x) => x.used)) jams.push(JSON.parse(J.toFile(J.decode((await app.jt.jamGet(u.slot)).bytes))));
    download(`jamtrack-backup-${new Date().toISOString().slice(0, 10)}.json`, JSON.stringify({ jamtrackLibrary: 1, jams }, null, 1), "application/json");
    msg("lib-msg", `${jams.length} jams in the file. Import it here (or in any browser) to have them back.`, "ok");
  });
  app.on("connected", refresh);
  app.on("disconnected", () => { user = []; draw(); });
  app.on("tab", (t) => { if (t === "library") refresh(); });
  app.on("slot", () => { if (!$("tab-library").hidden) draw(); });
  draw();
}
