// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
// build/kits.json (tools/make_kits.py) -> app/dsp/kits_data.h: every factory drum sample in EFB1 blocks (IMA ADPCM,
// companion/lib/efcodec.js, the encoder the firmware's decoder matches bit for bit), one run of blocks each.
//   node tools/encode_kits.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { encode, BLK_SAMPLES, BLK_BYTES } from "../companion/lib/efcodec.js";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const kits = JSON.parse(fs.readFileSync(path.join(root, "build/kits.json"), "utf8"));
const names = Object.keys(kits);
// bright sounds (hats, cymbals, shakers) as 8-bit mu-law: IMA ADPCM's step cannot follow them and hisses (12-16 dB
// SNR); mu-law keeps about 38 dB at any level. A block is 256 bytes, the last one padded with silence
const MULAW = /RIDE|CRASH|HAT|OPEN|TAMB|SHAKER|CABASA|GUIRO|SWISH/;
function mulaw(x) {
  const BIAS = 0x84, CLIP = 32635;
  let s = x < 0 ? 0x80 : 0, m = Math.min(Math.abs(x), CLIP) + BIAS, e = 7;
  for (let k = 0x4000; (m & k) === 0 && e > 0; k >>= 1) e--;
  return ~(s | (e << 4) | ((m >> (e + 3)) & 0x0F)) & 0xFF;
}
const chunks = [], info = [];
let off = 0;
for (const name of names) {
  const buf = Buffer.from(kits[name].pcm, "base64");
  const pcm = new Int16Array(buf.buffer, buf.byteOffset, buf.length >> 1);
  const nblk = Math.ceil(pcm.length / BLK_SAMPLES), mu = MULAW.test(name);
  let payload;
  if (mu) {
    payload = new Uint8Array(nblk * BLK_SAMPLES).fill(mulaw(0));
    for (let i = 0; i < pcm.length; i++) payload[i] = mulaw(pcm[i]);
  } else
    payload = encode(pcm).subarray(0, nblk * BLK_BYTES);
  chunks.push(payload);
  info.push({ name, off, frames: pcm.length, rate: kits[name].rate, fmt: mu ? 1 : 0 });
  off += payload.length;
}
let h = `/* SPDX-License-Identifier: CC0-1.0 (the sounds) / MIT (the file)
 * Made by tools/make_kits.py and tools/encode_kits.mjs: do not edit. JAMTRACK's factory drum samples, synthesized,
 * in EFB1 blocks (adpcm.h) or 8-bit mu-law (the bright ones). ${names.length} samples, ${off} bytes. */
#pragma once
enum { ${names.map(n => "SMP_" + n).join(", ")}, SMP_N };
static const struct { uint32_t off, frames; uint16_t rate, fmt; } SMP_INFO[SMP_N] = {   /* fmt 0 EFB1 ADPCM, 1 mu-law */
${info.map(i => `    {${i.off}u, ${i.frames}u, ${i.rate}u, ${i.fmt}u},   /* ${i.name} */`).join("\n")}
};
static const uint8_t SMP_DATA[${off}] = {
`;
const all = Buffer.concat(chunks.map(c => Buffer.from(c)));
for (let i = 0; i < all.length; i += 24) h += "    " + Array.from(all.subarray(i, i + 24)).join(",") + ",\n";
h += "};\n";
fs.writeFileSync(path.join(root, "app/dsp/kits_data.h"), h);
console.log(`app/dsp/kits_data.h: ${names.length} samples, ${(off / 1024).toFixed(1)} KB`);
