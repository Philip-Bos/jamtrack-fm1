// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// JAMTRACK companion: the user kit. A sample for each drum: any audio file the browser can read, trimmed, levelled,
// brought to the drum's rate and format, heard exactly as the FM-1 will play it, then sent (the FM-1 pulls it and
// checks every sector before it plays it).
import { app, $, el, msg, progress, download } from "./app.js";
import { prepare, crc32, wav16 } from "./lib/efcodec.js";
import { encodeKit, decodeKit, maxFrames, sectorsFor } from "./lib/kitcodec.js";

// each drum's format and rate: the bright ones as mu-law (no hiss), the cymbals at 24 kHz for length
const PLAN = [
  { fmt: 0, rate: 22050 }, { fmt: 0, rate: 32000 }, { fmt: 0, rate: 32000 }, { fmt: 1, rate: 32000 }, { fmt: 1, rate: 32000 },
  { fmt: 1, rate: 24000 }, { fmt: 1, rate: 24000 }, { fmt: 0, rate: 22050 }, { fmt: 1, rate: 32000 },
];
let dir = null, ctx = null;

const audio = () => (ctx = ctx || new AudioContext());
function play(pcm, rate) {
  const c = audio(), b = c.createBuffer(1, pcm.length, rate), d = b.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) d[i] = pcm[i] / 32768;
  const s = c.createBufferSource();
  s.buffer = b;
  s.connect(c.destination);
  s.start();
}

async function refresh() {
  if (!app.connected) { dir = null; draw(); return; }
  try { dir = await app.jt.kitDir(); } catch (e) { msg("kit-msg", e.message, "err"); }
  draw();
}

async function readBack(s) {               // a drum's sample from the FM-1, as it plays it
  const bytes = sectorsFor(s.frames, s.fmt) * 3828;
  const pay = await app.jt.kitRead(s.slot, bytes, (b, t) => progress("kit-prog", b / t));
  progress("kit-prog", null);
  return decodeKit(pay, s.frames, s.fmt);
}

async function send(slot, file) {
  if (!app.connected) { msg("kit-msg", "Connect the FM-1 first.", "err"); return; }
  const drum = app.tables.drums[slot], plan = PLAN[slot];
  try {
    msg("kit-msg", `Reading ${file.name}…`);
    const buf = await audio().decodeAudioData(await file.arrayBuffer());
    const chans = Array.from({ length: buf.numberOfChannels }, (_, i) => buf.getChannelData(i));
    const pcm = prepare(chans, buf.sampleRate, { target: plan.rate, trim: true, normalize: true, fadeMs: 2,
                                                 maxSeconds: maxFrames(plan.fmt) / plan.rate });
    if (pcm.length < 256) throw new Error("too short (or silent)");
    const pay = encodeKit(pcm, plan.fmt), crc = crc32(pay);
    play(decodeKit(pay, pcm.length, plan.fmt), plan.rate);          // what the FM-1 will play
    const name = file.name.replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9 ._-]+/g, " ").trim().slice(0, 11) || drum;
    msg("kit-msg", `Sending ${name} to ${drum} (${(pcm.length / plan.rate).toFixed(2)} s, ${plan.fmt ? "mu-law" : "ADPCM"})…`);
    app.busy++;
    try {
      await app.jt.kitUpload(slot, pay, { frames: pcm.length, rate: plan.rate, name, crc, fmt: plan.fmt },
                             (b, t) => progress("kit-prog", b / t));
    } finally { app.busy--; progress("kit-prog", null); }
    msg("kit-msg", `${drum}: ${name} is on the FM-1. Choose the USER kit to hear it.`, "ok");
    refresh();
  } catch (e) { progress("kit-prog", null); msg("kit-msg", `${drum}: ${e.message}`, "err"); }
}

function draw() {
  const ul = $("kit-list"), drums = app.tables.drums;
  $("kit-free").textContent = dir ? `${dir.free} of 48 sectors free` : "";
  ul.replaceChildren(...drums.map((name, i) => {
    const s = dir ? dir.slots[i] : null, plan = PLAN[i];
    const input = el("input", { type: "file", accept: "audio/*,.wav,.aif,.aiff,.flac,.mp3", hidden: true,
                                onchange: (e) => e.target.files[0] && send(i, e.target.files[0]) });
    const meta = s && s.state === "ok" ? `${s.name} · ${(s.frames / s.rate).toFixed(2)} s · ${s.fmt ? "mu-law" : "ADPCM"}`
      : s && s.state === "damaged" ? "damaged: send it again" : `empty (plays the STUDIO kit's) · up to ${(maxFrames(plan.fmt) / plan.rate).toFixed(1)} s`;
    const li = el("li", { class: s && s.state === "ok" ? "ok" : "" },
      el("span", { class: "num" }, String(i + 1)),
      el("span", { class: "name" }, name, el("span", { class: "meta" }, meta)),
      el("span", { class: "acts" },
        el("label", { class: "btn" }, "Choose", input),
        s && s.state === "ok" && el("button", { type: "button", class: "ghost", onclick: async () => play(await readBack(s), s.rate) }, "Hear"),
        s && s.state === "ok" && el("button", { type: "button", class: "ghost", onclick: async () =>
          download(`${name.toLowerCase().replace(/ /g, "-")}-${s.name.toLowerCase()}.wav`, wav16(await readBack(s), s.rate), "audio/wav") }, "WAV"),
        s && s.state !== "empty" && el("button", { type: "button", class: "ghost danger", onclick: async () => {
          if (!confirm(`Take ${s.name || "the sample"} off the ${name}?`)) return;
          try { await app.jt.kitErase(i); refresh(); } catch (e) { msg("kit-msg", e.message, "err"); }
        } }, "Remove")));
    li.addEventListener("dragover", (e) => { e.preventDefault(); li.classList.add("drop"); });
    li.addEventListener("dragleave", () => li.classList.remove("drop"));
    li.addEventListener("drop", (e) => { e.preventDefault(); li.classList.remove("drop"); if (e.dataTransfer.files[0]) send(i, e.dataTransfer.files[0]); });
    return li;
  }));
}

export function init() {
  app.on("connected", refresh);
  app.on("disconnected", () => { dir = null; draw(); });
  app.on("tab", (t) => { if (t === "kit") refresh(); });
  draw();
}
