// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
//
// The user kit's samples as the FM-1 keeps them (app/kit.c): sectors of a 3828-byte payload each, either EFB1 IMA
// ADPCM (efcodec.js: 29 blocks of 256 samples, 4 bits a sample: the most time, for kicks, snares, toms) or 8-bit
// mu-law (14 blocks of 256 bytes: no ADPCM hiss, for hats, cymbals and shakers, whose brightness ADPCM cannot follow).
import { encode, decode, SEC_PAYLOAD, SEC_SAMPLES } from "./efcodec.js";

export const MU_SEC = 14 * 256;          // mu-law samples a sector
export const MAX_SECS = 16, MAX_FRAMES = 65000;
export const perSector = (fmt) => (fmt ? MU_SEC : SEC_SAMPLES);
export const sectorsFor = (frames, fmt) => Math.ceil(frames / perSector(fmt));
export const maxFrames = (fmt) => Math.min(MAX_FRAMES, MAX_SECS * perSector(fmt));

export function mulaw(x) {               // G.711, as tools/encode_kits.mjs
  const BIAS = 0x84, CLIP = 32635;
  const s = x < 0 ? 0x80 : 0, m = Math.min(Math.abs(x), CLIP) + BIAS;
  let e = 7;
  for (let k = 0x4000; (m & k) === 0 && e > 0; k >>= 1) e--;
  return ~(s | (e << 4) | ((m >> (e + 3)) & 0x0F)) & 0xFF;
}
const MU = (() => {
  const t = new Int16Array(256);
  for (let i = 0; i < 256; i++) {
    const u = ~i & 0xFF, e = (u >> 4) & 7, m = u & 15, v = (((m << 3) + 0x84) << e) - 0x84;
    t[i] = u & 0x80 ? -v : v;
  }
  return t;
})();

// Int16Array -> the payload, whole sectors
export function encodeKit(pcm, fmt) {
  if (!fmt) return encode(pcm);
  const nsec = sectorsFor(pcm.length, 1), out = new Uint8Array(nsec * SEC_PAYLOAD);
  for (let i = 0; i < nsec * MU_SEC; i++) {
    const sec = Math.floor(i / MU_SEC), at = sec * SEC_PAYLOAD + (i % MU_SEC);
    out[at] = mulaw(i < pcm.length ? pcm[i] : 0);
  }
  return out;
}
// the payload -> Int16Array: what the FM-1 will play
export function decodeKit(payload, frames, fmt) {
  if (!fmt) return decode(payload, frames);
  const out = new Int16Array(frames);
  for (let i = 0; i < frames; i++) out[i] = MU[payload[Math.floor(i / MU_SEC) * SEC_PAYLOAD + (i % MU_SEC)]];
  return out;
}
