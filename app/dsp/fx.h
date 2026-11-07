/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the effects. A tilt EQ and a meter on each part, the chords' and synth's chorus and tremolo, the drum
 * bus's glue, a stereo reverb (four delay lines, two of them drifting, after two diffusers), TAPE (wow, flutter,
 * warmth, saturation) on everything, and a limiter. Part of engine.c. */

/* ---------------------------------------------------------------- reverb */
#define RV_LINES 4
#define RV_MAX 3000
#define RV_PRE 1200
static float rv_buf[RV_LINES][RV_MAX] PLAT_POOL;
static float rv_pre[RV_PRE] PLAT_POOL;
static float rv_ap[4][400] PLAT_POOL;
static const uint16_t RV_BASE[RV_LINES] = {1031, 1327, 1523, 1801};
static const uint16_t RV_APL[4] = {142, 379, 107, 277};
static struct {
    uint32_t w, pw, apw[4];
    uint16_t len[RV_LINES];
    float g[RV_LINES], lp[RV_LINES], damp, mod, size_now, t60;
    uint32_t pre;
} rv;

static void reverb_set(float space)      /* space 0..1: a room .. a hall */
{
    uint32_t i;
    float scale = 0.45f + space * 1.15f, t60 = 0.55f + space * space * 4.2f;
    for (i = 0; i < RV_LINES; i++) {
        uint32_t l = (uint32_t)((float)RV_BASE[i] * scale);
        rv.len[i] = (uint16_t)(l > RV_MAX - 20u ? RV_MAX - 20u : l);
        rv.g[i] = fm_powf(10.0f, -3.0f * (float)rv.len[i] / (t60 * (float)JT_SR));
    }
    rv.damp = 0.55f - space * 0.3f;      /* the loop's low-pass: rooms brighter, halls darker */
    rv.pre = (uint32_t)(220.0f + space * 900.0f);
    rv.size_now = space;
    rv.t60 = t60;
}

static inline __attribute__((always_inline)) float allpass(float *b, uint32_t n, uint32_t *w, float x, float g)
{
    float d = b[*w], y = d - g * x;
    b[*w] = x + g * y;
    if (++*w >= n)
        *w = 0;
    return y;
}

PLAT_RAM_CODE static void reverb_run(const float *inl, const float *inr, float *outl, float *outr, uint32_t len)
{
    uint32_t i, k;
    for (i = 0; i < len; i++) {
        float x = (inl[i] + inr[i]) * 0.5f, d[RV_LINES], s = 0, a, m;
        uint32_t pr = (rv.pw + RV_PRE - rv.pre) % RV_PRE;
        rv_pre[rv.pw] = x;
        rv.pw = (rv.pw + 1u) % RV_PRE;
        x = rv_pre[pr];
        x = allpass(rv_ap[0], RV_APL[0], &rv.apw[0], x, 0.7f);
        x = allpass(rv_ap[1], RV_APL[1], &rv.apw[1], x, 0.65f);
        rv.mod += 0.7f / (float)JT_SR;
        if (rv.mod >= 1.0f)
            rv.mod -= 1.0f;
        m = sine_t(rv.mod) * 9.0f;
        for (k = 0; k < RV_LINES; k++) {
            float dl = (float)rv.len[k] + (k == 0 ? m : k == 2 ? -m : 0.0f);
            int di = (int)dl;
            float fr = dl - (float)di;
            int r0 = (int)rv.w - di, r1;
            if (r0 < 0)
                r0 += RV_MAX;
            r1 = r0 ? r0 - 1 : RV_MAX - 1;
            a = rv_buf[k][r0] + (rv_buf[k][r1] - rv_buf[k][r0]) * fr;
            rv.lp[k] += (a - rv.lp[k]) * (1.0f - rv.damp);
            d[k] = rv.lp[k] * rv.g[k];
            s += d[k];
        }
        s *= 0.5f;                       /* Householder: d - 2/N * sum */
        rv_buf[0][rv.w] = rm_flush(d[0] - s + x);
        rv_buf[1][rv.w] = rm_flush(d[1] - s + x * 0.9f);
        rv_buf[2][rv.w] = rm_flush(d[2] - s);
        rv_buf[3][rv.w] = rm_flush(d[3] - s);
        if (++rv.w >= RV_MAX)
            rv.w = 0;
        a = d[0] + d[2] * 0.7f;
        m = d[1] + d[3] * 0.7f;
        outl[i] += allpass(rv_ap[2], RV_APL[2], &rv.apw[2], a, 0.5f);
        outr[i] += allpass(rv_ap[3], RV_APL[3], &rv.apw[3], m, 0.5f);
    }
}

/* ---------------------------------------------------------------- chorus */
#define CH_N 1024
static float ch_buf[CH_N] PLAT_POOL;
static uint32_t ch_w;
static float ch_ph;

PLAT_RAM_CODE static void chorus_run(const float *in, float *outl, float *outr, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        float a = sine_t(ch_ph), b = sine_t(wrap1(ch_ph + 0.25f)), dl, dr, fr;
        int di, r;
        ch_buf[ch_w] = in[i];
        ch_ph += 0.62f / (float)JT_SR;
        if (ch_ph >= 1.0f)
            ch_ph -= 1.0f;
        dl = 400.0f + 130.0f * a;
        dr = 400.0f + 130.0f * b;
        di = (int)dl;
        fr = dl - (float)di;
        r = (int)ch_w - di;              /* [0, CH_N), its older neighbour wrapped the same way */
        if (r < 0)
            r += CH_N;
        outl[i] += ch_buf[r] + (ch_buf[r ? r - 1 : CH_N - 1] - ch_buf[r]) * fr;
        di = (int)dr;
        fr = dr - (float)di;
        r = (int)ch_w - di;
        if (r < 0)
            r += CH_N;
        outr[i] += ch_buf[r] + (ch_buf[r ? r - 1 : CH_N - 1] - ch_buf[r]) * fr;
        if (++ch_w >= CH_N)
            ch_w = 0;
    }
}

/* ---------------------------------------------------------------- tape */
#define TP_N 512
#ifndef TP_WOW
#define TP_WOW 1.0f
#endif
static float tp_buf[2][TP_N] PLAT_POOL;
static struct {
    uint32_t w;
    float wow, flut, lpl, lpr, lp2l, lp2r, lol, lor;
} tp;

PLAT_RAM_CODE static void tape_run(float *l, float *r, uint32_t len, float amt)
{
    uint32_t i;
    /* the saturation only on the lows (below ~500 Hz): the warmth and the push are there, and a waveshaper fed hats
     * and cymbals at 44.1 kHz folds their harmonics back down as hiss and grit. The highs only roll off */
    float drive = 1.0f + amt * 1.4f, norm = 0.6f / rm_tanh(drive * 0.6f);
    float cut = 18000.0f - amt * 10000.0f, c = 1.0f - rm_exp2(-6.2831853f * 1.442695f * cut / (float)JT_SR);
    const float cl = 0.0688f;            /* 500 Hz */
    for (i = 0; i < len; i++) {
        float d, fr, a, b;
        int di, rr, r1;
        tp_buf[0][tp.w] = l[i];
        tp_buf[1][tp.w] = r[i];
        tp.wow += 0.55f / (float)JT_SR;
        if (tp.wow >= 1.0f)
            tp.wow -= 1.0f;
        tp.flut += 6.8f / (float)JT_SR;
        if (tp.flut >= 1.0f)
            tp.flut -= 1.0f;
        d = 80.0f + TP_WOW * amt * (48.0f * sine_t(tp.wow) + 2.6f * sine_t(tp.flut));
        di = (int)d;
        fr = d - (float)di;
        rr = (int)tp.w - di;             /* [0, TP_N), and its older neighbour wrapped the same way */
        if (rr < 0)
            rr += TP_N;
        r1 = rr ? rr - 1 : TP_N - 1;
        a = tp_buf[0][rr] + (tp_buf[0][r1] - tp_buf[0][rr]) * fr;
        b = tp_buf[1][rr] + (tp_buf[1][r1] - tp_buf[1][rr]) * fr;
        if (++tp.w >= TP_N)
            tp.w = 0;
        tp.lol += (a - tp.lol) * cl;
        tp.lor += (b - tp.lor) * cl;
        a += rm_tanh(tp.lol * drive) * norm - tp.lol;
        b += rm_tanh(tp.lor * drive) * norm - tp.lor;
        tp.lpl += (a - tp.lpl) * c;
        tp.lpr += (b - tp.lpr) * c;
        tp.lp2l += (tp.lpl - tp.lp2l) * c;
        tp.lp2r += (tp.lpr - tp.lp2r) * c;
        l[i] = tp.lp2l;
        r[i] = tp.lp2r;
    }
    tp.lol = rm_flush(tp.lol);
    tp.lor = rm_flush(tp.lor);
}

/* ---------------------------------------------------------------- the drum bus's glue */
static float glue_env = 0, glue_gain = 1, glue_target = 1;
PLAT_RAM_CODE static void glue_run(float *l, float *r, uint32_t len, float amt)
{
    uint32_t i;
    float th = 0.5f - amt * 0.36f, ratio = 1.0f + amt * 3.0f, make = 1.0f + amt * 0.9f, ex = 1.0f - 1.0f / ratio;
    const float at = 0.005652f, rl = 0.000162f;   /* 4 ms and 140 ms at 44.1 kHz */
    if (amt <= 0.001f)
        return;
    for (i = 0; i < len; i++) {
        float p = rm_max(rm_fabs(l[i]), rm_fabs(r[i]));
        glue_env += (p - glue_env) * (p > glue_env ? at : rl);
        if (!(i & 7u))                   /* the gain's curve every 8 samples */
            glue_target = glue_env > th ? rm_pow(th / glue_env, ex) : 1.0f;
        glue_gain += (glue_target - glue_gain) * 0.02f;
        l[i] *= glue_gain * make;
        r[i] *= glue_gain * make;
    }
}

/* ---------------------------------------------------------------- tilt EQ: around 1.2 kHz, the highs up or down */
static float tilt_l[JP_N + 1], tilt_r[JP_N + 1];
PLAT_RAM_CODE static void tilt_run(int p, float *l, float *r, uint32_t len, float hi)
{
    uint32_t i;
    const float c = 0.157f;              /* 1.2 kHz */
    float a = tilt_l[p], b = tilt_r[p];
    if (hi > 0.98f && hi < 1.02f)
        return;
    for (i = 0; i < len; i++) {
        a += (l[i] - a) * c;
        b += (r[i] - b) * c;
        l[i] = a + (l[i] - a) * hi;
        r[i] = b + (r[i] - b) * hi;
    }
    tilt_l[p] = rm_flush(a);
    tilt_r[p] = rm_flush(b);
}

/* ---------------------------------------------------------------- the limiter: never over the ceiling, back in 80 ms */
static float lim_g = 1;
PLAT_RAM_CODE static void limit_run(float *l, float *r, uint32_t len)
{
    uint32_t i;
    const float ceil = 0.94f, rel = 0.000283f;   /* 80 ms */
    for (i = 0; i < len; i++) {
        float p = rm_max(rm_fabs(l[i]), rm_fabs(r[i])), need = p * lim_g > ceil ? ceil / p : 1.0f;
        if (need < lim_g)
            lim_g = need;
        else
            lim_g += (1.0f - lim_g) * rel;
        l[i] *= lim_g;
        r[i] *= lim_g;
    }
}

static void fx_init(void)
{
    reverb_set(0.4f);
}

static void fx_panic(void)
{
    uint32_t i, k;
    for (k = 0; k < RV_LINES; k++)
        for (i = 0; i < RV_MAX; i++)
            rv_buf[k][i] = 0;
    for (i = 0; i < RV_PRE; i++)
        rv_pre[i] = 0;
    for (k = 0; k < 4; k++)
        for (i = 0; i < 400; i++)
            rv_ap[k][i] = 0;
    for (i = 0; i < CH_N; i++)
        ch_buf[i] = 0;
    for (i = 0; i < TP_N; i++)
        tp_buf[0][i] = tp_buf[1][i] = 0;
    for (k = 0; k < RV_LINES; k++)
        rv.lp[k] = 0;
}
