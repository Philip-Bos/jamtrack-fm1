/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK FM-1: the companion app's commands over USB, in the update's SysEx framing (felucca-bsp ota.c:
 * F0 pack7(00 59 cmd len24 body ~sum8(body)) F7), cmd 0x70..0x7F (fm1-base takes 0x11, 0x30, 0x41..0x44, 0x50..0x54).
 * The companion's side: companion/lib/jtproto.js. Every command is answered with a frame of the same cmd whose body
 * starts with a status byte (0 ok, else an error), except KIT UPLOAD, which ends the way the resource transfer does:
 * the FM-1 reads 0xC0000000 (done) or 0xCF000000 | code (failed) and the host acknowledges.
 *
 *   70 HELLO        -> "JTM1", protocol, firmware version (16), user slots, factory jams, kit slots, kit sectors, free
 *                      kit sectors, kit gen u32, jam bytes, current slot (0xFF none), playing
 *   71 TABLE        what (JT_T_*; 0x80 the factory jams, 0x81 the drums), first -> count, first, names (NUL ended)
 *   72 JAM GET      slot (0xFF the jam playing, 0..31 user, 0x40+ factory) -> used, slot, the jam (JT_JAM_BYTES)
 *   73 JAM PUT      slot (0xFF: the jam playing, at once; 0..31: stored in that slot), the jam
 *   74 LIST         -> per user slot: used, name (16)
 *   75 STATUS       -> playing, part, queued, chord, beat, counting, ext, ending, bar u16, bpm x10 u16, slot, dirty,
 *                      cpu %, peak L, peak R (0..250), levels (4), mutes, beat_seq u32
 *   76 TRANSPORT    op, a, b: 0 play, 1 stop (as STOP MODE says), 2 stop now, 3 part (a: 0 A, 1 B, 2 the other), 4 fill,
 *                   5 tap, 6 mute (a part, b on), 7 restart, 8 audition (a part, b chord), 9 generate (a genre, b what),
 *                   10 load (a slot as in JAM GET), 11 key on (a note, b velocity; b 0: off), 12 undo
 *   77 SLOT OP      op, slot, name (16): 0 delete, 1 rename, 2 the jam playing saved there (name: kept if empty)
 *   78 KIT DIR      -> free sectors, gen u32, then per drum: state (0 empty, 1 ok, 2 damaged), name (12), frames u32,
 *                      rate u16, format
 *   79 KIT UPLOAD   slot, rate u16, frames u32, crc u32, name (12), format (0 ADPCM, 1 mu-law; absent: 0): the FM-1 then pulls the payload with read requests
 *                   (address = byte offset in the payload, 512 at most), writes it, reports as above
 *   7A KIT ERASE    slot
 *   7B KIT READ     slot, offset u32, length u16 (<= 512) -> the payload's bytes (an export)
 *   7C SETTINGS     op (0 get, 1 put), then jt_settings_t (8 bytes) and the genre -> the same
 *   7D STATS        -> u32 each: render peak, voices peak (us), CPU %, xruns, frames, plat_ms
 *   7F (FM-1 -> host) the jam changed on the panel: what (JX_*), slot; sent while a host listens (it spoke within 20 s)
 * Part of app.c. */

#define JT_PROTO 2u                      /* 2: the factory jams on the wire from 0x40 (there are more than 127) */
enum { C_HELLO = 0x70, C_TABLE, C_JAM_GET, C_JAM_PUT, C_LIST, C_STATUS, C_TRANSPORT, C_SLOT_OP, C_KIT_DIR, C_KIT_UPLOAD,
       C_KIT_ERASE, C_KIT_READ, C_SETTINGS, C_STATS, C_NOTIFY = 0x7F };
enum { E_OK, E_ARGS = 1, E_STORE = 12, E_BUSY = 10 };
#define UP_OK_ADDR 0xC0000000u
#define UP_FAIL_ADDR 0xCF000000u

static uint32_t host_until;
static uint8_t px_wire[1100];

static uint32_t pk7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, w = 0, i;
    for (i = 0; i < n; i++) {
        acc |= (uint32_t)in[i] << nb;
        nb += 8u;
        while (nb >= 7u) {
            out[w++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7u;
        }
    }
    if (nb)
        out[w++] = (uint8_t)(acc & 0x7Fu);
    return w;
}

static void reply(uint32_t cmd, const uint8_t *body, uint32_t n)
{
    static uint8_t msg[912];
    uint32_t i, s = 0, w;
    if (n > 900u)
        return;
    msg[0] = 0;
    msg[1] = 0x59;
    msg[2] = (uint8_t)cmd;
    msg[3] = (uint8_t)n;
    msg[4] = (uint8_t)(n >> 8);
    msg[5] = 0;
    for (i = 0; i < n; i++) {
        msg[6u + i] = body[i];
        s += body[i];
    }
    msg[6u + n] = (uint8_t)~s;
    px_wire[0] = 0xF0;
    w = 1u + pk7(msg, 7u + n, px_wire + 1);
    px_wire[w++] = 0xF7;
    plat_sysex_send(px_wire, w);
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

/* the app's side (app.c) */
static int jam_get(int slot, jt_jam_t *out);
static int jam_put_user(int slot, const jt_jam_t *j);
static void jam_set_current(const jt_jam_t *j, int slot, int at_bar);
static int slot_delete(int slot);
static int slot_rename(int slot, const char *name);
static int slot_save_current(int slot, const char *name);
static void transport(uint32_t op, uint32_t a, uint32_t b);
static void upload_progress(uint32_t k, uint32_t n);
static int user_used(int slot, char *name);
static int cur_slot_wire(void);
static int slot_from_wire(int s);
static int session_dirty(void);
static uint32_t settings_genre(void);
static void settings_from_host(const uint8_t *b);

static void notify(uint32_t what)        /* a change on the panel, to a listening host */
{
    uint8_t b[2];
    if (!host_until || (int32_t)(plat_ms() - host_until) > 0)
        return;
    b[0] = (uint8_t)what;
    b[1] = (uint8_t)cur_slot_wire();
    reply(C_NOTIFY, b, 2);
}

static int kup_fill(uint32_t index, uint8_t *pay, void *ctx)
{
    uint32_t off = 0;
    (void)ctx;
    while (off < SEC_PAYLOAD) {
        uint32_t n = SEC_PAYLOAD - off > 512u ? 512u : SEC_PAYLOAD - off;
        if (plat_host_read(index * SEC_PAYLOAD + off, pay + off, n))
            return -1;
        off += n;
    }
    return 0;
}

static uint32_t name_list(int what, uint32_t first, uint8_t *o, uint32_t room)   /* names from first, NUL ended */
{
    uint32_t n = what == 0x80 ? (uint32_t)NFACTORY : what == 0x81 ? (uint32_t)JD_N : jt_count(what), i, w = 0;
    for (i = first; i < n; i++) {
        const char *s = what == 0x80 ? FACTORY[i].name : what == 0x81 ? jt_drum_name(i) : jt_name(what, i);
        uint32_t k = 0;
        while (s[k])
            k++;
        if (w + k + 1u > room)
            break;
        for (k = 0; s[k]; k++)
            o[w++] = (uint8_t)s[k];
        o[w++] = 0;
    }
    return w;
}

void app_usb_cmd(const uint8_t *d, uint32_t n)
{
    static uint8_t out[900];
    static jt_jam_t jbuf;
    uint32_t cmd = d[2], len = d[3] | (uint32_t)d[4] << 8 | (uint32_t)d[5] << 16, i, s = 0;
    const uint8_t *b = d + 6;
    if (cmd < C_HELLO || cmd > C_STATS || n < 7u + len)
        return;
    for (i = 0; i <= len; i++)
        s += d[6u + i];
    if ((s & 0xFFu) != 0xFFu)
        return;                          /* a damaged frame: no answer, the host asks again */
    host_until = plat_ms() + 20000u;
    switch (cmd) {
    case C_HELLO: {
        const char *v = APP_VERSION;
        out[0] = 0;
        out[1] = 'J'; out[2] = 'T'; out[3] = 'M'; out[4] = '1';
        out[5] = JT_PROTO;
        for (i = 0; i < 16u; i++)
            out[6u + i] = 0;
        for (i = 0; i < 15u && v[i]; i++)
            out[6u + i] = (uint8_t)v[i];
        out[22] = NUSER;
        out[23] = (uint8_t)NFACTORY;
        out[24] = KIT_SLOTS;
        out[25] = KIT_NSEC;
        out[26] = (uint8_t)kit_free_secs(-1);
        put32(out + 27, kdir.gen);
        put16(out + 31, JT_JAM_BYTES);
        out[33] = (uint8_t)cur_slot_wire();
        out[34] = jt_status()->playing;
        reply(cmd, out, 35);
        break;
    }
    case C_TABLE: {
        int what = len >= 1u ? b[0] : 0;
        uint32_t first = len >= 2u ? b[1] : 0, total = what == 0x80 ? (uint32_t)NFACTORY : what == 0x81 ? (uint32_t)JD_N
                                                                                                      : jt_count(what);
        out[0] = 0;
        out[1] = (uint8_t)total;
        out[2] = (uint8_t)first;
        reply(cmd, out, 3u + name_list(what, first, out + 3, sizeof out - 3u));
        break;
    }
    case C_JAM_GET: {
        int slot = len >= 1u ? b[0] : 0xFF, used = jam_get(slot_from_wire(slot), &jbuf);
        out[0] = used < 0 ? E_ARGS : E_OK;
        out[1] = (uint8_t)(used > 0);
        out[2] = (uint8_t)slot;
        for (i = 0; i < JT_JAM_BYTES; i++)
            out[3u + i] = ((const uint8_t *)&jbuf)[i];
        reply(cmd, out, used < 0 ? 1u : 3u + JT_JAM_BYTES);
        break;
    }
    case C_JAM_PUT: {
        int slot = len >= 1u ? b[0] : 0xFF;
        out[0] = E_ARGS;
        if (len >= 1u + JT_JAM_BYTES) {
            for (i = 0; i < JT_JAM_BYTES; i++)
                ((uint8_t *)&jbuf)[i] = b[1u + i];
            jt_jam_sanitize(&jbuf);
            if (slot == 0xFF) {
                jam_set_current(&jbuf, -2, 0);
                out[0] = E_OK;
            } else if (slot < NUSER)
                out[0] = (uint8_t)(jam_put_user(slot, &jbuf) ? E_STORE : E_OK);
        }
        reply(cmd, out, 1);
        break;
    }
    case C_LIST: {
        uint32_t o = 1;
        out[0] = 0;
        for (i = 0; i < NUSER; i++) {
            char nm[JT_NAME];
            uint32_t k;
            int used = user_used((int)i, nm);
            out[o++] = (uint8_t)used;
            for (k = 0; k < JT_NAME; k++)
                out[o++] = used ? (uint8_t)nm[k] : 0u;
        }
        reply(cmd, out, o);
        break;
    }
    case C_STATUS: {
        const jt_status_t *st = jt_status();
        const jt_jam_t *j = jt_jam();
        out[0] = 0;
        out[1] = st->playing;
        out[2] = st->part;
        out[3] = st->queued;
        out[4] = st->chord;
        out[5] = st->beat;
        out[6] = st->counting;
        out[7] = st->ext;
        out[8] = st->ending;
        put16(out + 9, st->bar);
        put16(out + 11, (uint32_t)(st->bpm * 10.0f + 0.5f));
        out[13] = (uint8_t)cur_slot_wire();
        out[14] = (uint8_t)session_dirty();
        out[15] = (uint8_t)(st->cpu * 100.0f);
        out[16] = (uint8_t)(st->peak_l / 4u);
        out[17] = (uint8_t)(st->peak_r / 4u);
        for (i = 0; i < JP_N; i++)
            out[18u + i] = st->level[i];
        out[22] = j->mute;
        put32(out + 23, st->beat_seq);
        out[27] = st->step;
        jt_peak_reset();                 /* (the peaks: since the last STATUS) */
        reply(cmd, out, 28);
        break;
    }
    case C_TRANSPORT:
        out[0] = len >= 1u ? E_OK : E_ARGS;
        if (len >= 1u)
            transport(b[0], len >= 2u ? b[1] : 0, len >= 3u ? b[2] : 0);
        reply(cmd, out, 1);
        break;
    case C_SLOT_OP: {
        char name[JT_NAME];
        int op = len >= 1u ? b[0] : 99, slot = len >= 2u ? b[1] : 99;
        name[0] = 0;
        if (len >= 2u + JT_NAME) {
            for (i = 0; i < JT_NAME; i++)
                name[i] = (char)b[2u + i];
            name[JT_NAME - 1u] = 0;
        }
        out[0] = E_ARGS;
        if (slot < NUSER) {
            if (op == 0)
                out[0] = (uint8_t)slot_delete(slot);
            else if (op == 1)
                out[0] = (uint8_t)slot_rename(slot, name);
            else if (op == 2)
                out[0] = (uint8_t)slot_save_current(slot, name);
        }
        reply(cmd, out, 1);
        break;
    }
    case C_KIT_DIR:
        out[0] = 0;
        out[1] = (uint8_t)kit_free_secs(-1);
        put32(out + 2, kdir.gen);
        for (i = 0; i < KIT_SLOTS; i++) {
            uint8_t *o = out + 6u + i * 20u;
            uint32_t k;
            o[0] = kslot_state[i];
            for (k = 0; k < NAME_N; k++)
                o[1u + k] = kdir.slot[i].used ? (uint8_t)kdir.slot[i].name[k] : 0;
            put32(o + 13, kdir.slot[i].used ? kdir.slot[i].frames : 0);
            put16(o + 17, kdir.slot[i].used ? kdir.slot[i].rate : 0);
            o[19] = kdir.slot[i].used ? kdir.slot[i].fmt : 0;
        }
        reply(cmd, out, 6u + KIT_SLOTS * 20u);
        break;
    case C_KIT_UPLOAD: {
        char name[NAME_N];
        int rc;
        if (len < 1u + 2u + 4u + 4u + NAME_N) {
            plat_host_read(UP_FAIL_ADDR | KIT_E_ARGS, out, 8);
            break;
        }
        for (i = 0; i < NAME_N; i++)
            name[i] = (char)b[11u + i];
        name_clean(name, NAME_N);
        plat_host_patience(3000u, 0);
        rc = kit_write(b[0], get32(b + 3), get16(b + 1), len > 23u ? b[23] : 0u, name, get32(b + 7), kup_fill, 0,
                       upload_progress);
        plat_host_read(rc ? UP_FAIL_ADDR | (uint32_t)rc : UP_OK_ADDR, out, 8);
        break;
    }
    case C_KIT_ERASE:
        out[0] = (uint8_t)(len >= 1u ? kit_erase(b[0]) : KIT_E_ARGS);
        reply(cmd, out, 1);
        break;
    case C_KIT_READ: {
        uint32_t sl = len >= 7u ? b[0] : KIT_SLOTS, off = get32(b + 1), cnt = get16(b + 5);
        out[0] = KIT_E_ARGS;
        if (sl < KIT_SLOTS && kslot_state[sl] == SLOT_OK && cnt <= 512u) {
            uint32_t total = kmap_count(kdir.slot[sl].map) * SEC_PAYLOAD, k = 0, secs[KIT_MAX_SECS], ns = 0;
            for (i = 0; i < KIT_NSEC && ns < KIT_MAX_SECS; i++)
                if (kmap_bit(kdir.slot[sl].map, i))
                    secs[ns++] = i;
            if (off <= total && cnt <= total - off) {
                for (k = 0; k < cnt; k++) {
                    uint32_t at = off + k;
                    out[1u + k] = plat_data(ksec_addr(secs[at / SEC_PAYLOAD]))[at % SEC_PAYLOAD];
                }
                out[0] = 0;
                reply(cmd, out, 1u + cnt);
                break;
            }
        }
        reply(cmd, out, 1);
        break;
    }
    case C_SETTINGS: {
        const uint8_t *p;
        if (len >= 1u + sizeof(jt_settings_t) + 1u && b[0] == 1u)
            settings_from_host(b + 1);
        p = (const uint8_t *)jt_settings();
        out[0] = 0;
        for (i = 0; i < sizeof(jt_settings_t); i++)
            out[1u + i] = p[i];
        out[1u + sizeof(jt_settings_t)] = (uint8_t)settings_genre();
        reply(cmd, out, 2u + sizeof(jt_settings_t));
        break;
    }
    case C_STATS: {
        const jt_meter_t *m = jt_meter();
        uint32_t v[6];
        v[0] = m->t_render / PLAT_TICKS_PER_US;
        v[1] = m->t_voices / PLAT_TICKS_PER_US;
        v[2] = (uint32_t)(jt_status()->cpu * 100.0f);
        v[3] = plat_xruns();
        v[4] = rt_frames;
        v[5] = plat_ms();
        jt_meter_reset();
        out[0] = 0;
        for (i = 0; i < 6u; i++)
            put32(out + 1u + 4u * i, v[i]);
        reply(cmd, out, 25);
        break;
    }
    }
}
