// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// The companion's protocol client (companion/lib/jtproto.js) against a real FM-1 running JAMTRACK, through
// tools/midibridge.py:   node app/tests/device_test.mjs     (it changes the playing jam back to what it was, and leaves
// the user kit's PERC empty)
import { spawn } from "node:child_process";
import { Link, JamTrack, OP } from "../../companion/lib/jtproto.js";
import * as J from "../../companion/lib/jam.js";
import { encodeKit } from "../../companion/lib/kitcodec.js";
import { crc32 } from "../../companion/lib/efcodec.js";
import fs from "node:fs";
const TABLES = JSON.parse(fs.readFileSync(new URL("../../companion/lib/tables.json", import.meta.url)));

const py = spawn(".venv/bin/python", ["tools/midibridge.py"], { stdio: ["pipe", "pipe", "inherit"] });
let onMsg = null, buf = "";
const ready = new Promise((r) => py.stdout.once("data", r));
py.stdout.on("data", (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf("\n")) >= 0) {
    const line = buf.slice(0, i); buf = buf.slice(i + 1);
    if (/^f0/.test(line) && onMsg) onMsg(Uint8Array.from(line.match(/../g).map((h) => parseInt(h, 16))));
  }
});
await ready;
const t = { send: (b) => py.stdin.write(Buffer.from(b).toString("hex") + "\n"), onMessage: (fn) => { onMsg = fn; }, close: () => py.kill() };
const jt = new JamTrack(new Link(t));
let fails = 0;
const ok = (c, what) => { console.log((c ? "  ok   " : "  FAIL ") + what); if (!c) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const h = await jt.hello();
ok(h && h.users === 32 && h.kitSlots === 9 && h.jamBytes === 192, `hello: ${JSON.stringify(h)}`);
const grooves = await jt.table(1), patches = await jt.table(5), factory = await jt.table(0x80);
ok(grooves.length === TABLES.grooves.length && patches.length === TABLES.patches.length && factory.length === TABLES.factory.length, `tables: ${grooves.length} grooves, ${patches.length} sounds, ${factory.length} factory jams`);
const was = await jt.jamGet(0xFF);
const j = J.decode(was.bytes);
ok(was.bytes.length === 192 && j.part[0].n >= 1, `the jam playing: ${j.name}, ${J.keyText(j)}`);
const j2 = J.clone(j);
j2.name = "DEVICE TEST"; j2.key = (j.key + 2) % 12;
j2.part[0].c = [...J.parseProg("1maj7 6m7 2m9 5sus4:2 5:2", 4), ...Array(7).fill({ deg: 0, qual: 0, beats: 4, bass: 0 })];
j2.part[0].n = 5;
await jt.jamPut(0xFF, J.encode(j2));
const back = J.decode((await jt.jamGet(0xFF)).bytes);
ok(back.name === "DEVICE TEST" && back.key === j2.key && J.progText(back.part[0].c.slice(0, 5)) === "1maj7 6m7 2m9 5sus4:2 5:2", "a jam sent comes back the same");
const list = await jt.list();
ok(list.length === 32, `list: ${list.filter((x) => x.used).length} of 32 used`);
await jt.transport(OP.PLAY);
await sleep(1500);
const s = await jt.status();
ok(s && s.playing && s.bpm > 30, `status while playing: ${JSON.stringify(s)}`);
await jt.transport(OP.STOP_NOW);
await jt.jamPut(0xFF, was.bytes);
const kd = await jt.kitDir();
ok(kd.slots.length === 9, `kit: ${kd.free} sectors free, ${kd.slots.filter((x) => x.state === "ok").length} drums set`);
for (const fmt of [1, 0]) {               // a shaker-ish burst into PERC, both formats, then off again
  const n = fmt ? 9000 : 12000, pcm = new Int16Array(n);
  for (let i = 0; i < n; i++) pcm[i] = Math.round(12000 * (Math.random() * 2 - 1) * Math.exp(-i / 1500));
  const pay = encodeKit(pcm, fmt);
  const t0 = Date.now();
  await jt.kitUpload(8, pay, { frames: n, rate: 32000, name: fmt ? "TEST MU" : "TEST ADPCM", crc: crc32(pay), fmt });
  const d = await jt.kitDir();
  ok(d.slots[8].state === "ok" && d.slots[8].fmt === fmt && d.slots[8].frames === n,
     `kit upload (${fmt ? "mu-law" : "ADPCM"}, ${pay.length} B) in ${Date.now() - t0} ms: ${JSON.stringify(d.slots[8])}`);
  const rb = await jt.kitRead(8, pay.length);
  ok(Buffer.compare(Buffer.from(rb), Buffer.from(pay)) === 0, "read back the same bytes");
}
await jt.kitErase(8);
ok((await jt.kitDir()).slots[8].state === "empty", "kit sample removed");
console.log(fails ? `${fails} FAILED` : "device: all passed");
t.close();
process.exit(fails ? 1 : 0);
