/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the drums. Samples in EFB1 (IMA ADPCM blocks, adpcm.h): the factory's in the firmware (kits_data.h, made
 * by tools/make_kits.py), the user kit's in the flash (the app hands them over: jt_user_sample). Ten voices, each
 * decoding its sample a block at a time and reading it at its own rate and pitch; the open hat is choked by the
 * closed one. Soft hits are darker. Part of engine.c. */

#include "kits_data.h"

typedef struct {
    const char *name;
    uint8_t smp[JD_N];                   /* SMP_* for each drum voice */
    int8_t tune[JD_N];                   /* semitones */
    int8_t db[JD_N];                     /* level, dB */
    uint8_t dark;                        /* 0..100: the whole kit darker (a lo-fi kit) */
    uint8_t room;                        /* 0..100: its reverb send, on top of the part's */
} kit_t;

static const kit_t KITS[] = {
    {"VINTAGE", {SMP_KICK_VINTAGE, SMP_SNARE_VINTAGE, SMP_STICK, SMP_HAT_TIGHT, SMP_OPEN_A, SMP_RIDE_JAZZ, SMP_CRASH_DARK,
                 SMP_TOM_VINTAGE, SMP_SHAKER}, {0}, {0, 0, -2, -4, -6, -5, -6, -1, -8}, 10, 10},
    {"STUDIO", {SMP_KICK_STUDIO, SMP_SNARE_STUDIO, SMP_STICK, SMP_HAT_TIGHT, SMP_OPEN_A, SMP_RIDE_ROCK, SMP_CRASH_A,
                SMP_TOM_STUDIO, SMP_TAMB}, {0}, {0, 0, -2, -4, -6, -5, -5, -1, -9}, 0, 20},
    {"ROOM", {SMP_KICK_ROOM, SMP_SNARE_ROOM, SMP_STICK, SMP_HAT_LOOSE, SMP_OPEN_A, SMP_RIDE_ROCK, SMP_CRASH_A,
              SMP_TOM_ROOM, SMP_TAMB}, {0}, {0, 0, -2, -4, -5, -5, -4, 0, -9}, 0, 45},
    {"BRUSHES", {SMP_KICK_SOFT, SMP_SNARE_BRUSH, SMP_STICK, SMP_HAT_LOOSE, SMP_OPEN_A, SMP_RIDE_JAZZ, SMP_CRASH_DARK,
                 SMP_TOM_VINTAGE, SMP_SWISH}, {0, 0, 0, -1, -1, 0, 0, 2, 0}, {-1, 0, -3, -7, -8, -3, -7, -3, -4}, 15, 25},
    {"LO-FI", {SMP_KICK_LOFI, SMP_SNARE_LOFI, SMP_SNAP, SMP_HAT_TIGHT, SMP_OPEN_A, SMP_RIDE_JAZZ, SMP_CRASH_DARK,
               SMP_TOM_VINTAGE, SMP_SHAKER}, {0, 0, 0, -2, -2, -2, -2, 0, 0}, {0, 0, -3, -6, -8, -7, -8, -2, -9}, 60, 15},
    {"ROCK", {SMP_KICK_ROCK, SMP_SNARE_ROCK, SMP_STICK, SMP_HAT_LOOSE, SMP_OPEN_A, SMP_RIDE_ROCK, SMP_CRASH_A,
              SMP_TOM_ROOM, SMP_TAMB}, {0}, {0, 0, -2, -4, -5, -4, -3, 0, -9}, 0, 35},
    {"CAJON", {SMP_KICK_CAJON, SMP_SNARE_CAJON, SMP_SNAP, SMP_SHAKER, SMP_TAMB, SMP_RIDE_JAZZ, SMP_CRASH_DARK,
               SMP_CONGA, SMP_CABASA}, {0}, {0, 0, -3, -6, -6, -7, -8, -2, -8}, 5, 20},
    {"LM-80", {SMP_KICK_LM, SMP_SNARE_LM, SMP_CLAP, SMP_HAT_LM, SMP_OPEN_LM, SMP_RIDE_ROCK, SMP_CRASH_A, SMP_TOM_LM,
               SMP_CABASA}, {0}, {0, 0, -2, -4, -6, -5, -5, -1, -7}, 0, 20},
    {"RHYTHM BOX", {SMP_KICK_BOX, SMP_SNARE_BOX, SMP_RIM_BOX, SMP_HAT_BOX, SMP_OPEN_LM, SMP_RIDE_JAZZ, SMP_CRASH_DARK,
                    SMP_CONGA, SMP_GUIRO}, {0}, {0, -1, -3, -5, -7, -7, -8, -3, -6}, 10, 15},
    {"808", {SMP_KICK_808, SMP_SNARE_808, SMP_CLAP, SMP_HAT_808, SMP_OPEN_LM, SMP_RIDE_ROCK, SMP_CRASH_A, SMP_TOM_808,
             SMP_COWBELL}, {0}, {0, -1, -2, -5, -6, -6, -6, -1, -8}, 0, 10},
    {"USER", {SMP_KICK_STUDIO, SMP_SNARE_STUDIO, SMP_STICK, SMP_HAT_TIGHT, SMP_OPEN_A, SMP_RIDE_ROCK, SMP_CRASH_A,
              SMP_TOM_STUDIO, SMP_TAMB}, {0}, {0, 0, -2, -4, -6, -5, -5, -1, -9}, 0, 20},
};
#define NKITS (sizeof(KITS) / sizeof(KITS[0]))
static const char *const DRUM_NAME[JD_N] = {"KICK", "SNARE", "RIM", "HAT", "OPEN HAT", "RIDE", "CRASH", "TOM", "PERC"};
const char *jt_drum_name(uint32_t v) { return v < JD_N ? DRUM_NAME[v] : ""; }

/* where each drum sits (width scales it), -1 left .. 1 right, as heard from the front */
static const float DRUM_PAN[JD_N] = {0, 0.04f, 0.04f, 0.28f, 0.28f, -0.32f, -0.4f, 0, 0.42f};

/* the factory's samples as sources (one run of blocks each) */
static const uint8_t *smp_run[SMP_N];
static int16_t MULAW[256];
static jt_sample_t smp_src[SMP_N];
static jt_sample_t user_src[JD_N];
static const uint8_t *user_runs[JD_N][32];   /* (16 sectors at most: kit.c KIT_MAX_SECS) */
static volatile uint8_t user_hold;       /* 1: the user kit let go (its flash is changing) */

void jt_user_sample(uint32_t voice, const jt_sample_t *s)
{
    uint32_t i;
    if (voice >= JD_N)
        return;
    user_src[voice].frames = 0;
    if (!s || !s->frames || !s->sec || !s->rate)
        return;
    for (i = 0; i < 32u && i * s->bps * JT_BLK_SAMPLES < s->frames; i++)
        user_runs[voice][i] = s->sec[i];
    user_src[voice].sec = user_runs[voice];
    user_src[voice].rate = s->rate;
    user_src[voice].bps = s->bps;
    user_src[voice].fmt = s->fmt;
    user_src[voice].frames = s->frames < i * s->bps * JT_BLK_SAMPLES ? s->frames : i * s->bps * JT_BLK_SAMPLES;
    if (user_src[voice].frames > 65000u)    /* (Q16 positions) */
        user_src[voice].frames = 65000u;
}

#define NDRUMV 10
typedef struct {
    const jt_sample_t *src;
    uint8_t on, voice, user;
    uint32_t blk;                        /* the block decoded into buf */
    int16_t buf[JT_BLK_SAMPLES + 1];
    uint32_t pos;                        /* Q16 frames from the sample's start */
    uint32_t inc;                        /* Q16 per output sample */
    float gl, gr, lp, lpc, choke;
} dvoice_t;
static dvoice_t DV[NDRUMV];
static uint32_t dv_next;

void jt_user_release(void) { user_hold = 1; }
void jt_user_resume(void) { user_hold = 0; }
int jt_user_idle(void)
{
    uint32_t i;
    for (i = 0; i < NDRUMV; i++)
        if (DV[i].on && DV[i].user)
            return 0;
    return 1;
}

static void drums_init(void)
{
    uint32_t i;
    for (i = 0; i < SMP_N; i++) {
        smp_run[i] = SMP_DATA + SMP_INFO[i].off;
        smp_src[i].sec = &smp_run[i];
        smp_src[i].frames = SMP_INFO[i].frames;
        smp_src[i].rate = SMP_INFO[i].rate;
        smp_src[i].bps = 0xFFFFu;
        smp_src[i].fmt = SMP_INFO[i].fmt;
    }
    for (i = 0; i < 256u; i++) {          /* G.711 mu-law */
        uint32_t u = ~i & 0xFFu, e = (u >> 4) & 7u, m = u & 15u;
        int32_t v = (int32_t)(((m << 3) + 0x84u) << e) - 0x84;
        MULAW[i] = (int16_t)((u & 0x80u) ? -v : v);
    }
}

static inline __attribute__((always_inline)) const uint8_t *blk_addr(const jt_sample_t *s, uint32_t b)
{
    return s->sec[b / s->bps] + (b % s->bps) * (s->fmt ? JT_BLK_SAMPLES : JT_BLK_BYTES);
}

static inline __attribute__((always_inline)) void dv_decode(dvoice_t *d, uint32_t b)
{
    const jt_sample_t *s = d->src;
    uint32_t nblk = (s->frames + JT_BLK_SAMPLES - 1u) / JT_BLK_SAMPLES;
    d->blk = b;
    if (b < nblk) {
        if (s->fmt) {
            const uint8_t *p = blk_addr(s, b);
            uint32_t i;
            for (i = 0; i < JT_BLK_SAMPLES; i++)
                d->buf[i] = MULAW[p[i]];
            d->buf[JT_BLK_SAMPLES] = b + 1u < nblk ? MULAW[blk_addr(s, b + 1u)[0]] : 0;
        } else {
            ef_ima_block(blk_addr(s, b), d->buf);
            d->buf[JT_BLK_SAMPLES] = b + 1u < nblk ? ef_ima_first(blk_addr(s, b + 1u)) : 0;
        }
    }
}

/* a hit: voice (JD_*), velocity 0..1, pitch in semitones, the kit's darkness and the part's width */
static void drum_hit(const kit_t *kit, uint32_t voice, float vel, float semis, float width, float tone_oct)
{
    const jt_sample_t *s = 0;
    dvoice_t *d;
    uint32_t i, user = 0;
    float g, pan, cut;
    if (kit == &KITS[NKITS - 1u] && user_src[voice].frames && !user_hold) {
        s = &user_src[voice];
        user = 1;
    } else
        s = &smp_src[kit->smp[voice]];
    if (!s->frames)
        return;
    if (voice == JD_HAT)                 /* the closed hat chokes the open one */
        for (i = 0; i < NDRUMV; i++)
            if (DV[i].on && DV[i].voice == JD_OPEN)
                DV[i].choke = 0.9935f;
    d = 0;
    for (i = 0; i < NDRUMV; i++)         /* the same drum again: its last hit fades (no pile-up), a free voice */
        if (DV[i].on && DV[i].voice == voice && voice != JD_CRASH && voice != JD_RIDE && voice != JD_TOM && !DV[i].choke)
            DV[i].choke = 0.995f;
    for (i = 0; i < NDRUMV && !d; i++)
        if (!DV[(dv_next + i) % NDRUMV].on)
            d = &DV[(dv_next + i) % NDRUMV];
    if (!d)
        d = &DV[dv_next % NDRUMV];
    dv_next++;
    semis += (float)kit->tune[voice];
    d->src = s;
    d->voice = (uint8_t)voice;
    d->user = (uint8_t)user;
    d->pos = 0;
    d->inc = (uint32_t)((float)s->rate / (float)JT_SR * fm_exp2f(semis * (1.0f / 12.0f)) * 65536.0f);
    g = fm_db2lin((float)kit->db[voice]) * (0.12f + 0.88f * vel * vel);
    pan = DRUM_PAN[voice] * width;
    if (voice == JD_TOM)
        pan = fm_clampf(-semis * 0.06f, -0.4f, 0.4f) * width;
    d->gl = g * (0.5f - pan * 0.5f) * 1.41f;
    d->gr = g * (0.5f + pan * 0.5f) * 1.41f;
    cut = 2500.0f + 15000.0f * vel;      /* soft hits darker; a dark kit darker still */
    cut *= fm_exp2f(-(float)kit->dark * 0.03f + tone_oct);
    cut = fm_clampf(cut, 300.0f, 20000.0f);
    d->lpc = 1.0f - fm_expf(-6.2831853f * cut / (float)JT_SR);
    d->lp = 0;
    d->choke = 0;
    dv_decode(d, 0);
    d->on = 1;
}

PLAT_RAM_CODE static void drums_render(float *bl, float *br, uint32_t len)
{
    uint32_t k, i;
    for (k = 0; k < NDRUMV; k++) {
        dvoice_t *d = &DV[k];
        const jt_sample_t *s;
        float gl, gr, lp, lpc, ch;
        if (!d->on)
            continue;
        if (d->user && user_hold) {      /* its flash is going away: quiet now */
            d->on = 0;
            continue;
        }
        s = d->src;
        gl = d->gl;
        gr = d->gr;
        lp = d->lp;
        lpc = d->lpc;
        ch = d->choke;
        for (i = 0; i < len; i++) {
            uint32_t f = d->pos >> 16, b = f / JT_BLK_SAMPLES, o = f % JT_BLK_SAMPLES;
            float x, fr;
            if (f + 1u >= s->frames) {
                d->on = 0;
                break;
            }
            if (b != d->blk)
                dv_decode(d, b);
            fr = (float)(d->pos & 0xFFFFu) * (1.0f / 65536.0f);
            x = (float)d->buf[o] + (float)(d->buf[o + 1u] - d->buf[o]) * fr;
            x *= 1.0f / 32768.0f;
            lp += (x - lp) * lpc;
            if (ch > 0.0f) {
                gl *= ch;
                gr *= ch;
                if (gl + gr < 0.0005f) {
                    d->on = 0;
                    break;
                }
            }
            bl[i] += lp * gl;
            br[i] += lp * gr;
            d->pos += d->inc;
        }
        d->gl = gl;
        d->gr = gr;
        d->lp = rm_flush(lp);
    }
}

static void drums_panic(void)
{
    uint32_t i;
    for (i = 0; i < NDRUMV; i++)
        DV[i].on = 0;
}
