// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// The companion's music theory (companion/lib/jam.js) against the firmware's: every chord in app/tests/chords.json
// (tools/dump_tables.c) named the same, the same notes; the factory jams through decode and encode unchanged; the
// progression text read back as written.   node app/tests/theory_test.mjs
import fs from "node:fs";
import * as J from "../../companion/lib/jam.js";

let fails = 0;
const tests = JSON.parse(fs.readFileSync(new URL("./chords.json", import.meta.url)));
for (const [key, mode, color, deg, qual, bass, , num, name, pcs] of tests) {
  const j = { key, mode, color }, c = { deg, qual, beats: 4, bass };
  const t = J.chordText(j, c), p = J.chordPcs(j, c);
  if (t.num !== num || t.name !== name || p.join() !== pcs.join()) {
    if (fails++ < 10) console.log(`FAIL key ${key} mode ${mode} color ${color} deg ${deg} qual ${qual} bass ${bass}: ` +
      `${t.num} ${t.name} [${p}] vs ${num} ${name} [${pcs}]`);
  }
}
const tables = JSON.parse(fs.readFileSync(new URL("../../companion/lib/tables.json", import.meta.url)));
for (const h of tables.factory) {
  const back = Buffer.from(J.encode(J.fromHex(h))).toString("hex");
  if (back !== h) { fails++; console.log("FAIL factory jam round trip", J.fromHex(h).name); }
}
const modes = JSON.parse(fs.readFileSync(new URL("./modes.json", import.meta.url)));
for (const [i, m, want] of modes) {
  const j = J.fromHex(tables.factory[i]);
  J.modeSet(j, m);
  const got = [...j.part[0].c, ...j.part[1].c].map((c) => c.qual);
  if (got.join() !== want.join()) { fails++; console.log("FAIL scale change", j.name, "to", J.MODES[m]); }
}
for (const s of ["1maj7 6m7 2m7 5sus4:2 5:2", "b7 4/3 #4m7b5 1M:8", "17:16 47:8 1m9/5"]) {
  const back = J.progText(J.parseProg(s, 4), 4);
  if (back !== s) { fails++; console.log(`FAIL text "${s}" -> "${back}"`); }
}
console.log(fails ? `${fails} FAILED` : `theory: ${tests.length} chords, ${tables.factory.length} factory jams, all as the firmware has them`);
process.exit(fails ? 1 : 0);
