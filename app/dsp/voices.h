/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the synth voices: SUB, FM, KS and ORGAN (patches.h), each rendered a chunk at a time (32 samples at
 * most) into its part's bus, its slow parts (filter coefficients, glides, index decays) once a chunk. Part of
 * engine.c. */

#define NVOICE 14
#define V_BASS0 0                        /* the pools: bass 0..1, chords 2..7, synth 8..11, the keys 12..13 */
#define V_CHORD0 2
#define V_SYNTH0 8
#define V_LIVE0 12
#define KS_BASS 2048
#define KS_POLY 1024
#define SINE_N 2048

static float SINE[SINE_N + 1];
static float ks_mem[2 * KS_BASS + 12 * KS_POLY] PLAT_POOL;

static inline __attribute__((always_inline)) float sine_t(float ph)   /* ph in turns, [0, 1) */
{
    float x = ph * (float)SINE_N;
    int i = (int)x;
    float f = x - (float)i;
    i &= SINE_N - 1;
    return SINE[i] + (SINE[i + 1] - SINE[i]) * f;
}
static inline __attribute__((always_inline)) float wrap1(float p) { return p - (float)(int)p; }

static uint32_t rng_state = 0x1234567u;
static inline __attribute__((always_inline)) uint32_t rng(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}
static inline __attribute__((always_inline)) float rngf(void) { return (float)(rng() >> 8) * (1.0f / 8388608.0f) - 1.0f; }   /* -1..1 */
static inline __attribute__((always_inline)) float rng01(void) { return (float)(rng() >> 8) * (1.0f / 16777216.0f); }

static inline __attribute__((always_inline)) float polyblep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float note_hz(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

typedef struct {
    uint8_t on, part, note, stage;       /* stage: 0 attack, 1 decay / sustain, 3 release */
    const patch_t *pt;
    uint32_t gate, age;                  /* gate: samples left held (0xFFFFFFFF: until let go) */
    float vel, hz, hz_to, glide_k, env, att, dec_k, sus, rel_k, gain, pan_l, pan_r;
    float ph[3];
    float z1, z2, z3, z4, fe, fe_k, cut0, k;
    float idx, idx_k, idx_sus, tine, tine_k, fb;
    float *ks;
    uint16_t ks_cap, ks_w;
    float ks_d, ks_g, ks_c, ks_lp, ks_body, clk, ks_hp;
    float perc, perc_k;
    float vib;
} voice_t;

static voice_t V[NVOICE];
static uint32_t voice_age;
static float part_tone_oct[JP_N + 1];    /* the part's TONE as octaves on its filters ([JP_N]: the keys, 0) */

static void voices_init(void)
{
    uint32_t i;
    float *m = ks_mem;
    for (i = 0; i <= SINE_N; i++)
        SINE[i] = fm_sin_turns((float)i / (float)SINE_N);
    for (i = 0; i < NVOICE; i++) {
        V[i].ks = m;
        V[i].ks_cap = (uint16_t)(i < V_CHORD0 ? KS_BASS : KS_POLY);
        m += V[i].ks_cap;
    }
}

/* -60 dB in t seconds, a sample at a time */
static float decay_k(float t) { return t <= 0.0005f ? 0.0f : fm_expf(-6.9f / (t * (float)JT_SR)); }

static inline __attribute__((always_inline)) void voice_release(voice_t *v)
{
    if (v->on && v->stage != 3) {
        v->stage = 3;
        if (v->pt->eng == E_KS)          /* the string is damped: a hand on it */
            v->ks_g *= 0.985f;
    }
}

static void voice_kill(voice_t *v, float t)   /* a quick release (a mono line's last note, a steal) */
{
    if (!v->on)
        return;
    v->stage = 3;
    v->rel_k = decay_k(t);
    if (v->pt->eng == E_KS)
        v->ks_g *= 0.97f;
}

/* note on: the voice set up from its patch; tone (octaves) and the velocity colour the start */
static void voice_start(voice_t *v, const patch_t *pt, int part, int note, float vel, uint32_t gate, float pan)
{
    float hz;
    int n = note;
    v->pt = pt;
    v->part = (uint8_t)part;
    v->note = (uint8_t)note;
    v->vel = vel;
    v->gate = gate;
    v->age = ++voice_age;
    if (pt->eng == E_KS) {               /* the string must fit its delay line: an octave up if not */
        while (n < 127 && (float)JT_SR / note_hz((float)n) > (float)v->ks_cap - 4.0f)
            n += 12;
    }
    hz = note_hz((float)n);
    v->hz = v->hz_to = hz;
    v->glide_k = 0;
    v->stage = 0;
    v->env = 0;
    v->att = pt->a <= 0.0005f ? 1.0f : 1.0f / (pt->a * (float)JT_SR);
    v->dec_k = decay_k(pt->d);
    v->sus = pt->s;
    v->rel_k = decay_k(pt->r);
    v->gain = pt->gain * (0.25f + 0.75f * vel * vel);
    v->pan_l = 0.5f - pan * 0.5f;
    v->pan_r = 0.5f + pan * 0.5f;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->z1 = v->z2 = v->z3 = v->z4 = 0;
    v->vib = 0;
    switch (pt->eng) {
    case E_SUB:
        v->ph[1] = rng01();              /* the oscillators free-running against each other */
        v->ph[2] = rng01();
        v->fe = 1.0f;
        v->fe_k = decay_k(pt->p[9] * 1.4f);
        v->cut0 = pt->p[6] * fm_powf(hz / 261.6f, 0.5f) * (0.7f + 0.6f * vel);
        v->k = 2.0f - 1.95f * pt->p[7];
        break;
    case E_FM:
        v->idx = pt->p[1] * (1.0f - pt->p[9] + pt->p[9] * vel * 1.3f) * fm_exp2f(part_tone_oct[part] * 0.7f);
        v->idx_sus = pt->p[3] * fm_exp2f(part_tone_oct[part] * 0.7f);
        v->idx_k = decay_k(pt->p[2]);
        v->tine = pt->p[5] * (0.4f + vel);
        v->tine_k = decay_k(pt->p[6]);
        v->fb = 0;
        break;
    case E_KS: {
        uint32_t N, i, P;
        float d = (float)JT_SR / hz, b, lp = 0, c, t60, bright;
        bright = fm_clampf(pt->p[0] * (0.55f + 0.6f * vel) * fm_exp2f(part_tone_oct[part]), 0.03f, 0.98f);
        c = 1.0f - fm_clampf(pt->p[1] * (1.0f + pt->p[5]), 0.0f, 0.93f) * (1.0f - 0.25f * fm_clampf(part_tone_oct[part], 0, 1));
        v->ks_c = c;
        d -= (1.0f - c) / c;             /* the loop filter's delay, taken off the line */
        v->ks_d = d < 2.0f ? 2.0f : d;
        N = (uint32_t)v->ks_d + 1u;
        t60 = pt->p[2] * fm_powf(110.0f / hz, 0.45f) * (1.0f - 0.85f * pt->p[5]);
        v->ks_g = fm_powf(10.0f, -3.0f / (t60 * hz));
        if (v->ks_g > 0.99995f)
            v->ks_g = 0.99995f;
        P = (uint32_t)(pt->p[3] * (float)N);
        for (i = 0; i < v->ks_cap; i++)
            v->ks[i] = 0;
        b = bright * bright;
        for (i = 0; i < N; i++) {        /* the exciter: a pick's noise, or a felt mallet's soft bump */
            float x = pt->p[7] > 0.5f ? sine_t(0.5f * (float)i / (float)N) * (1.0f + 0.3f * rngf() * bright) : rngf();
            lp += (x - lp) * (0.08f + 0.92f * b);
            v->ks[i] = lp;
        }
        for (i = N; i-- > P;)            /* the pick's place: a comb */
            v->ks[i] -= 0.9f * v->ks[i - P];
        {                                /* no DC in the string */
            float m = 0;
            for (i = 0; i < N; i++)
                m += v->ks[i];
            m /= (float)N;
            for (i = 0; i < N; i++)
                v->ks[i] -= m;
        }
        v->ks_w = (uint16_t)N;
        v->ks_lp = 0;
        v->ks_body = 0;
        v->ks_hp = 0;
        v->clk = pt->p[6] * vel;
        v->gain *= 1.6f;
        break;
    }
    case E_ORGAN:
        v->perc = pt->p[4];
        v->perc_k = decay_k(pt->p[5]);
        v->clk = pt->p[6];
        break;
    }
    v->on = 1;
}

/* one voice, len samples, added into bl / br */
PLAT_RAM_CODE static void voice_render(voice_t *v, float *bl, float *br, uint32_t len)
{
    const patch_t *pt = v->pt;
    uint32_t i;
    float env = v->env, out, e[32];
    if (v->glide_k > 0.0f) {             /* glide (per chunk) */
        v->hz += (v->hz_to - v->hz) * (1.0f - rm_pow(v->glide_k, (float)len));
    }
    for (i = 0; i < len; i++) {
        /* the envelope */
        if (v->stage == 0) {
            env += v->att;
            if (env >= 1.0f) {
                env = 1.0f;
                v->stage = 1;
            }
        } else if (v->stage == 1)
            env = v->sus + (env - v->sus) * v->dec_k;
        else
            env *= v->rel_k;
        if (v->gate != 0xFFFFFFFFu && v->gate && !--v->gate && v->stage != 3)
            voice_release(v);
        e[i] = env;                      /* (the envelope, for the engine's loop below) */
    }
    v->env = env;
    /* the sound, times the envelope in e[] (then the sound itself in e[]) */
    switch (pt->eng) {
    case E_SUB: {
        float hz = v->hz, cut, g, a1, a2, a3, k = v->k, dt, inc0, inc1, inc2, vib = 0;
        int w0 = (int)(pt->p[0] + 0.5f), w1 = (int)(pt->p[1] + 0.5f), two = pt->p[11] > 1.5f;
        float mix1 = pt->p[3], sub = pt->p[4], noise = pt->p[5];
        float b1, b2, b3;
        if (pt->p[10] > 0.0f) {          /* vibrato after a moment */
            v->vib += (float)len * (1.0f / (float)JT_SR);
            vib = sine_t(wrap1(v->vib * 5.2f)) * pt->p[10] * rm_clamp(v->vib * 2.0f - 0.3f, 0, 1);
            hz *= rm_exp2(vib * (1.0f / 12.0f));
        }
        inc0 = hz * (1.0f / (float)JT_SR);
        inc1 = inc0 * rm_exp2(pt->p[2] * (1.0f / 12.0f));
        inc2 = inc0 * 0.5f;
        cut = v->cut0 * rm_exp2(pt->p[8] * v->fe + part_tone_oct[v->part]);
        v->fe *= rm_pow(v->fe_k, (float)len);
        cut = rm_clamp(cut, 30.0f, 16000.0f);
        g = rm_tan(3.14159265f * cut * (1.0f / (float)JT_SR));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        b1 = 1.0f / (1.0f + g * (g + 1.41f));
        b2 = g * b1;
        b3 = g * b2;
        dt = inc0;
        for (i = 0; i < len; i++) {
            float p0 = v->ph[0], p1 = v->ph[1], s0, s1, x, v1, v2, v3;
            s0 = w0 == 0 ? 2.0f * p0 - 1.0f - polyblep(p0, dt)
               : w0 == 1 ? (p0 < 0.5f ? 1.0f : -1.0f) + polyblep(p0, dt) - polyblep(wrap1(p0 + 0.5f), dt)
               : w0 == 2 ? 4.0f * rm_fabs(p0 - 0.5f) - 1.0f : sine_t(p0);
            s1 = w1 == 0 ? 2.0f * p1 - 1.0f - polyblep(p1, inc1)
               : w1 == 1 ? (p1 < 0.5f ? 1.0f : -1.0f) + polyblep(p1, inc1) - polyblep(wrap1(p1 + 0.5f), inc1)
               : w1 == 2 ? 4.0f * rm_fabs(p1 - 0.5f) - 1.0f : sine_t(p1);
            x = s0 * (1.0f - 0.5f * mix1) + s1 * mix1;
            if (sub > 0.0f)
                x += sub * sine_t(v->ph[2]) * 1.2f;
            if (noise > 0.0f)
                x += noise * rngf();
            v->ph[0] = wrap1(p0 + inc0);
            v->ph[1] = wrap1(p1 + inc1);
            v->ph[2] = wrap1(v->ph[2] + inc2);
            v3 = x - v->z2;              /* the state-variable filter (Cytomic's), low-pass */
            v1 = a1 * v->z1 + a2 * v3;
            v2 = v->z2 + a2 * v->z1 + a3 * v3;
            v->z1 = 2.0f * v1 - v->z1;
            v->z2 = 2.0f * v2 - v->z2;
            x = v2;
            if (two) {                   /* a second pole pair, no resonance: 24 dB */
                float u3 = x - v->z4, u1 = b1 * v->z3 + b2 * u3, u2 = v->z4 + b2 * v->z3 + b3 * u3;
                v->z3 = 2.0f * u1 - v->z3;
                v->z4 = 2.0f * u2 - v->z4;
                x = u2;
            }
            out = x * e[i] * v->gain;
            e[i] = out;
        }
        v->z1 = rm_flush(v->z1);
        v->z2 = rm_flush(v->z2);
        v->z3 = rm_flush(v->z3);
        v->z4 = rm_flush(v->z4);
        break;
    }
    case E_FM: {
        float inc = v->hz * (1.0f / (float)JT_SR), incm = inc * pt->p[0], inct = inc * pt->p[4];
        float idx = v->idx, isus = v->idx_sus, ik = rm_pow(v->idx_k, (float)len), tine = v->tine;
        float tk = rm_pow(v->tine_k, (float)len), fbk = pt->p[7], drive = pt->p[8];
        float di = (isus + (idx - isus) * ik - idx) / (float)len;
        for (i = 0; i < len; i++) {
            float m = sine_t(v->ph[1]) * idx, c;
            c = sine_t(wrap1(v->ph[0] + m * 0.159f + v->fb * fbk + 4.0f));
            v->fb = c;
            if (tine > 0.0001f)
                c += tine * sine_t(v->ph[2]);
            if (drive > 0.0f)
                c = rm_tanh(c * (1.0f + drive * 3.0f) * e[i]) / (1.0f + drive);
            else
                c *= e[i];
            v->ph[0] = wrap1(v->ph[0] + inc);
            v->ph[1] = wrap1(v->ph[1] + incm);
            v->ph[2] = wrap1(v->ph[2] + inct);
            idx += di;
            e[i] = c * v->gain;
        }
        v->idx = isus + (v->idx - isus) * ik;
        v->tine = tine * tk;
        break;
    }
    case E_KS: {
        float *b = v->ks, d = v->ks_d, g = v->ks_g, c = v->ks_c, lp = v->ks_lp, body = pt->p[4], bk = 0.045f;
        uint32_t cap = v->ks_cap, w = v->ks_w;
        int di = (int)d;
        float fr = d - (float)di;
        for (i = 0; i < len; i++) {
            int r = (int)w - di;
            float y0, y1, y, o;
            if (r < 0)
                r += (int)cap;
            y0 = b[r];
            y1 = b[r ? r - 1 : cap - 1];
            y = y0 + (y1 - y0) * fr;     /* the line read d samples back */
            lp += (y - lp) * c;          /* the string's losses */
            b[w] = rm_flush(lp * g);
            if (++w >= cap)
                w = 0;
            o = lp;
            v->ks_body += (o - v->ks_body) * bk;   /* the body: the low mids, warmer */
            o += body * v->ks_body * 1.5f;
            v->ks_hp += (o - v->ks_hp) * 0.004f;   /* no DC */
            o -= v->ks_hp;
            if (v->clk > 0.0005f) {      /* the pick's click */
                o += v->clk * rngf() * 0.5f;
                v->clk *= 0.94f;
            }
            e[i] = o * e[i] * v->gain;
        }
        v->ks_w = (uint16_t)w;
        v->ks_lp = rm_flush(lp);
        break;
    }
    case E_ORGAN: {
        float inc = v->hz * (1.0f / (float)JT_SR), l16 = pt->p[0], l8 = pt->p[1], l4 = pt->p[2], l3 = pt->p[3];
        float pk = v->perc_k, drive = pt->p[7];
        for (i = 0; i < len; i++) {
            float p = v->ph[0], x;
            x = l8 * sine_t(p) + l4 * sine_t(wrap1(p * 2.0f)) + l3 * sine_t(wrap1(p * 3.0f)) + l16 * sine_t(v->ph[1]);
            x += v->perc * sine_t(wrap1(p * 3.0f));
            v->perc *= pk;
            if (v->clk > 0.001f) {
                x += v->clk * rngf();
                v->clk *= 0.9f;
            }
            if (drive > 0.0f)
                x = rm_tanh(x * (1.0f + drive * 2.0f));
            v->ph[0] = wrap1(p + inc);
            v->ph[1] = wrap1(v->ph[1] + inc * 0.5f);
            e[i] = x * e[i] * v->gain * 0.4f;
        }
        break;
    }
    }
    for (i = 0; i < len; i++) {          /* into the bus */
        bl[i] += e[i] * v->pan_l;
        br[i] += e[i] * v->pan_r;
    }
    if (v->stage == 3 && v->env < 0.0003f)
        v->on = 0;
}

/* a free voice in [lo, hi): a silent one, else the oldest released, else the oldest */
static voice_t *voice_find(int lo, int hi)
{
    int i, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = lo; i < hi; i++)
        if (!V[i].on)
            return &V[i];
    for (i = lo; i < hi; i++)
        if (V[i].stage == 3 && V[i].age < oldest) {
            oldest = V[i].age;
            best = i;
        }
    if (best < 0)
        for (i = lo; i < hi; i++)
            if (V[i].age < oldest) {
                oldest = V[i].age;
                best = i;
            }
    return &V[best];
}
