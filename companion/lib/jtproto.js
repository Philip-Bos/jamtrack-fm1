// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Philip Bos
//
// JAMTRACK FM-1's USB protocol, the host's side (the FM-1's: app/proto.c). Frames in the update's SysEx framing:
// F0 pack7(00 59 cmd len24 body ~sum8(body)) F7. One Link carries everything on the FM-1's port: these commands, the
// FM-1's read requests while a kit sample is sent (cmd 0x30, as the update loader's), its notices that the jam changed
// on the panel, and the screen mirror's frames (handed to whoever listens). The link is ECHOFIELD's (efproto.js).

export const CMD = { HELLO: 0x70, TABLE: 0x71, JAM_GET: 0x72, JAM_PUT: 0x73, LIST: 0x74, STATUS: 0x75, TRANSPORT: 0x76,
                     SLOT_OP: 0x77, KIT_DIR: 0x78, KIT_UPLOAD: 0x79, KIT_ERASE: 0x7A, KIT_READ: 0x7B, SETTINGS: 0x7C,
                     STATS: 0x7D, NOTIFY: 0x7F };
export const OP = { PLAY: 0, STOP: 1, STOP_NOW: 2, PART: 3, FILL: 4, TAP: 5, MUTE: 6, RESTART: 7, AUDITION: 8,
                    GENERATE: 9, LOAD: 10, KEY: 11, UNDO: 12 };
export const ERR = { 1: "bad request", 2: "transfer interrupted", 3: "not enough room in the user kit", 7: "flash erase failed",
                     8: "flash write failed", 9: "verification failed", 10: "busy", 11: "checksum mismatch",
                     12: "could not save", 13: "flash not available" };
export const SLOT_STATE = ["empty", "ok", "damaged"];
const UP_OK = 0xC0000000, UP_FAIL = 0xCF000000, MAXDATA = 512, NAME_N = 12, JAM = 192;

export function pack7(data) {
  const out = [];
  let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}
export function unpack7(s) {
  const out = [];
  let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}
export function frame(cmd, body = []) {
  let s = 0;
  for (const b of body) s += b;
  const n = body.length;
  return [0xF0, ...pack7([0x00, 0x59, cmd, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, ...body, ~s & 0xFF]), 0xF7];
}
export function parse(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59) return null;
  const n = u[3] | (u[4] << 8) | (u[5] << 16);
  if (u.length < 7 + n) return null;
  let s = 0;
  for (let i = 6; i <= 6 + n; i++) s += u[i];
  if ((s & 0xFF) !== 0xFF) return null;
  return { cmd: u[2], body: u.slice(6, 6 + n) };
}
export function readResponse(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}
function parseReadRequest(pkt) {
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

const le16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const str = (b, o, n) => String.fromCharCode(...b.slice(o, o + n)).replace(/\0[\s\S]*$/, "");
const nameN = (s, n) => { const a = new Array(n).fill(0); s = (s || "").toUpperCase(); for (let i = 0; i < Math.min(n - 1, s.length); i++) a[i] = s.charCodeAt(i) & 0x7F; return a; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class Link {
  constructor(transport) {
    this.t = transport;
    this.waiters = [];
    this.listeners = new Set();            // every SysEx frame (the mirror)
    this.notifyFns = new Set();            // the jam changed on the panel
    this.reqFns = null;                    // read requests, while an upload runs
    this.lost = false;
    this.chain = Promise.resolve();
    this.busy = false;
    transport.onMessage((data) => this.onData(data));
  }
  onData(data) {
    if (data[0] !== 0xF0) return;
    const pkt = Array.from(data);
    for (const fn of this.listeners) fn(pkt);
    const u = unpack7(pkt.slice(1, -1));
    if (u.length >= 3 && u[0] === 0 && u[1] === 0x59 && u[2] === 0x30) {
      const r = parseReadRequest(pkt);
      if (r && this.reqFns) this.reqFns(r);
      return;
    }
    const f = parse(pkt);
    if (!f) return;
    if (f.cmd === CMD.NOTIFY) { for (const fn of this.notifyFns) fn(f.body[0], f.body[1]); return; }
    const w = this.waiters.findIndex((x) => x.cmd === f.cmd);
    if (w >= 0) this.waiters.splice(w, 1)[0].resolve(f.body);
  }
  send(bytes) { try { this.t.send(bytes); return true; } catch { this.lost = true; return false; } }
  // one exchange at a time: the FM-1 holds one incoming frame
  exclusive(fn) {
    const run = this.chain.then(() => { this.busy = true; return fn(); }).finally(() => { this.busy = false; });
    this.chain = run.catch(() => {});
    return run;
  }
  ask(cmd, body = [], opts = {}) { return this.exclusive(() => this.askNow(cmd, body, opts)); }
  async askNow(cmd, body = [], { ms = 1500, tries = 3 } = {}) {
    for (let t = 0; t < tries; t++) {
      const p = new Promise((resolve) => this.waiters.push({ cmd, resolve }));
      if (!this.send(frame(cmd, body))) return null;
      const got = await Promise.race([p, sleep(ms).then(() => null)]);
      if (got) return got;
      this.waiters = this.waiters.filter((w) => w.cmd !== cmd);
    }
    return null;
  }
  close() { this.t.close(); }
}

// the FM-1 running JAMTRACK
export class JamTrack {
  constructor(link) { this.link = link; this.info = null; }

  async hello() {
    const b = await this.link.ask(CMD.HELLO, [], { ms: 800, tries: 2 });
    if (!b || b.length < 35 || str(b, 1, 4) !== "JTM1") return null;
    this.info = { proto: b[5], version: str(b, 6, 16), users: b[22], factory: b[23], kitSlots: b[24], kitSectors: b[25],
                  kitFree: b[26], kitGen: rd32(b, 27), jamBytes: rd16(b, 31), slot: b[33], playing: !!b[34] };
    return this.info;
  }
  async check(cmd, body, what) {
    const b = await this.link.ask(cmd, body);
    if (!b) throw new Error(`${what}: no answer from the FM-1`);
    if (b[0]) throw new Error(`${what}: ${ERR[b[0]] || "error " + b[0]}`);
    return b;
  }
  async table(what) {                    // a table's names, in as many frames as it takes
    const out = [];
    for (let first = 0; ;) {
      const b = await this.check(CMD.TABLE, [what, first], "table");
      const total = b[1];
      const names = str(b, 3, b.length - 3) ? String.fromCharCode(...b.slice(3)).split("\0").slice(0, -1) : [];
      out.push(...names);
      first += names.length;
      if (first >= total || !names.length) return out;
    }
  }
  // slot: 0xFF the jam playing, 0..31 user, 0x40 + i factory (0x80 + i before protocol 2) -> {used, bytes}
  async jamGet(slot = 0xFF) {
    const b = await this.check(CMD.JAM_GET, [slot], "read jam");
    return { used: !!b[1], bytes: Uint8Array.from(b.slice(3, 3 + JAM)) };
  }
  jamPut(slot, bytes) { return this.check(CMD.JAM_PUT, [slot, ...bytes], slot === 0xFF ? "send jam" : "store jam"); }
  async list() {
    const b = await this.check(CMD.LIST, [], "list");
    const out = [];
    for (let i = 0, o = 1; o + 17 <= b.length; i++, o += 17) out.push({ slot: i, used: !!b[o], name: str(b, o + 1, 16) });
    return out;
  }
  async status() {
    const b = await this.link.ask(CMD.STATUS, [], { ms: 600, tries: 1 });
    if (!b || b[0]) return null;
    return { playing: !!b[1], part: b[2], queued: !!b[3], chord: b[4], beat: b[5], counting: b[6], ext: !!b[7],
             ending: !!b[8], bar: rd16(b, 9), bpm: rd16(b, 11) / 10, slot: b[13], dirty: !!b[14], cpu: b[15],
             peak: [b[16] / 250, b[17] / 250], levels: b.slice(18, 22), mute: b[22], beatSeq: rd32(b, 23),
             step: b.length > 27 ? b[27] : -1 };
  }
  transport(op, a = 0, b = 0) { return this.check(CMD.TRANSPORT, [op, a, b], "transport"); }
  slotOp(op, slot, name = "") { return this.check(CMD.SLOT_OP, [op, slot, ...nameN(name, 16)], "slot"); }
  async kitDir() {
    const b = await this.check(CMD.KIT_DIR, [], "kit");
    const slots = [];
    for (let i = 0, o = 6; o + 20 <= b.length; i++, o += 20)
      slots.push({ slot: i, state: SLOT_STATE[b[o]] || "empty", name: str(b, o + 1, NAME_N), frames: rd32(b, o + 13),
                   rate: rd16(b, o + 17), fmt: b[o + 19] });
    return { free: b[1], gen: rd32(b, 2), slots };
  }
  kitErase(slot) { return this.check(CMD.KIT_ERASE, [slot], "delete"); }
  async kitRead(slot, bytes, onProgress) {
    const out = new Uint8Array(bytes);
    for (let off = 0; off < bytes; off += MAXDATA) {
      const n = Math.min(MAXDATA, bytes - off);
      const b = await this.link.ask(CMD.KIT_READ, [slot, ...le32(off), ...le16(n)]);
      if (!b || b[0] || b.length !== 1 + n) throw new Error("reading the sample failed");
      out.set(b.slice(1), off);
      onProgress?.(off + n, bytes);
    }
    return out;
  }
  // a sample into a drum: the payload (kitcodec.js: whole sectors, ADPCM or mu-law), its frames, rate and format, a name. The FM-1 pulls the
  // payload with read requests, writes and checks every sector, then enters it; it says so by reading UP_OK (or UP_FAIL)
  kitUpload(slot, payload, meta, onProgress) { return this.link.exclusive(() => this.kitUploadNow(slot, payload, meta, onProgress)); }
  async kitUploadNow(slot, payload, { frames, rate, name, crc, fmt = 0 }, onProgress) {
    const link = this.link;
    let last = Date.now(), done = null, sent = 0, asked = false;
    const finish = new Promise((resolve) => { done = resolve; });
    link.reqFns = (r) => {
      last = Date.now();
      asked = true;
      const top = (r.addr & 0xFF000000) >>> 0;
      if (top === UP_OK || top === UP_FAIL) {
        link.send(readResponse(r.addr, [...new TextEncoder().encode("success"), 0], r.fl));
        done(top === UP_OK ? 0 : (r.addr & 0xFFFF) || 99);
        return;
      }
      if (r.len > MAXDATA || r.addr + r.len > payload.length) { done(-1); return; }
      link.send(readResponse(r.addr, Array.from(payload.subarray(r.addr, r.addr + r.len)), r.fl));
      sent = Math.max(sent, r.addr + r.len);
      onProgress?.(sent, payload.length);
    };
    try {
      const head = frame(CMD.KIT_UPLOAD, [slot, ...le16(rate), ...le32(frames), ...le32(crc >>> 0), ...nameN(name, NAME_N), fmt]);
      link.send(head);
      const t0 = Date.now();
      let again = 0;
      const timer = (async () => {
        for (;;) {
          await sleep(250);
          if (link.lost) return -2;
          if (!asked && again < 3 && Date.now() - t0 > 1500 * (again + 1)) { again++; link.send(head); }
          if (Date.now() - last > 15000) return -3;
        }
      })();
      const rc = await Promise.race([finish, timer]);
      if (rc === 0) { await this.link.askNow(CMD.HELLO, [], { ms: 1000, tries: 2 }); return; }
      if (rc === -1) throw new Error("the FM-1 asked for data outside the sample (stopped)");
      if (rc === -2) throw new Error("the FM-1 was disconnected");
      if (rc === -3) throw new Error("the FM-1 stopped asking for data");
      throw new Error(`the FM-1 did not keep the sample: ${ERR[rc] || "error " + rc}`);
    } finally { link.reqFns = null; }
  }
  async settings(put = null) {
    const b = await this.check(CMD.SETTINGS, put ? [1, ...put.eng, put.genre] : [0], "settings");
    return { eng: b.slice(1, 13), genre: b[13] };
  }
  async stats() {
    const b = await this.check(CMD.STATS, [], "stats");
    const v = [0, 1, 2, 3, 4, 5].map((i) => rd32(b, 1 + 4 * i));
    return { renderUs: v[0], voicesUs: v[1], cpu: v[2], xruns: v[3] };
  }
  onNotify(fn) { this.link.notifyFns.add(fn); return () => this.link.notifyFns.delete(fn); }
}

export function midiTransport(input, output) {
  return {
    send: (b) => output.send(b),
    onMessage: (fn) => { input.onmidimessage = (ev) => fn(ev.data); },
    close: () => { input.onmidimessage = null; },
  };
}
