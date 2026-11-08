/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the engine, its own -O2 unit (fm1.toml o2). The render: MIDI in (the clock, START, STOP), the sequencer's
 * ticks and steps, the events, the voices, the parts' buses and the effects, in chunks of 32 samples at most, cut
 * where a tick or an event falls so each lands on its sample. */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "jt_dsp.h"
#include "adpcm.h"
#ifdef JT_DEBUG
#include <stdio.h>
#endif

#include "rmath.h"
#include "theory.h"
#include "styles.h"
#include "patches.h"
#include "voices.h"
#include "drums.h"
#include "fx.h"
#include "seq.h"
#include "gen.h"

#define CHUNK 32u
static uint8_t held_notes[8], nheld;    /* the keys held, for a mono lead's legato */
#define JP_LIVE JP_N                     /* the keys' own bus, after the band's four */

/* ---------------------------------------------------------------- tables, for the screens */
uint32_t jt_count(int what)
{
    switch (what) {
    case JT_T_KIT: return NKITS;
    case JT_T_GROOVE: return NGROOVES;
    case JT_T_BSTYLE: return NBSTYLES;
    case JT_T_CSTYLE: return NCSTYLES;
    case JT_T_SSTYLE: return SY_N;
    case JT_T_PATCH: return NPATCHES;
    case JT_T_MODE: return JM_N;
    case JT_T_METER: return JT_METER_N;
    case JT_T_QUAL: return JQ_N;
    case JT_T_GENRE: return NGENRES;
    }
    return 0;
}
const char *jt_name(int what, uint32_t i)
{
    if (i >= jt_count(what))
        return "";
    switch (what) {
    case JT_T_KIT: return KITS[i].name;
    case JT_T_GROOVE: return GROOVES[i].name;
    case JT_T_BSTYLE: return BSTYLES[i].name;
    case JT_T_CSTYLE: return CSTYLES[i].name;
    case JT_T_SSTYLE: return SSTYLE_NAME[i];
    case JT_T_PATCH: return PATCHES[i].name;
    case JT_T_MODE: return MODE_NAME[i];
    case JT_T_METER: return METERS[i].name;
    case JT_T_QUAL: return QUAL_MENU[i];
    case JT_T_GENRE: return GENRES[i].name;
    }
    return "";
}
uint32_t jt_patch_kind(uint32_t i) { return i < NPATCHES ? PATCHES[i].kind : 0; }

jt_jam_t *jt_jam(void) { return &jam; }
jt_settings_t *jt_settings(void) { return &settings; }
const jt_status_t *jt_status(void) { return &st; }
void jt_peak_reset(void) { st.peak_l = st.peak_r = 0; }

/* ---------------------------------------------------------------- commands from the main loop */
static volatile uint8_t c_play, c_stop, c_stop_now, c_fill, c_restart, c_panic, c_mutes_changed;
static volatile int8_t c_part = -2;
static volatile int16_t c_aud_part = -1, c_aud_chord = -1;
#define LIVE_Q 32
static volatile uint32_t live_q[LIVE_Q];
static volatile uint32_t live_w, live_r;
static volatile uint8_t c_live_off;

void jt_play(int on)
{
    if (on)
        c_play = 1;
    else
        c_stop = 1;
}
void jt_stop_now(void) { c_stop_now = 1; }
void jt_part_go(int part) { c_part = (int8_t)(part < 0 ? -1 : part & 1); }
void jt_fill(void) { c_fill = 1; }
void jt_restart(void) { c_restart = 1; }
void jt_mute(uint32_t part, int on)
{
    if (part >= JP_N)
        return;
    if (on)
        jam.mute |= (uint8_t)(1u << part);
    else
        jam.mute &= (uint8_t)~(1u << part);
    c_mutes_changed = 1;
}
void jt_audition(int part, int chord)
{
    c_aud_chord = (int16_t)chord;
    c_aud_part = (int16_t)part;
}
void jt_live(int on, uint32_t note, uint32_t vel)
{
    uint32_t w = live_w;
    if (w - live_r >= LIVE_Q)
        return;
    live_q[w % LIVE_Q] = (on ? (1u << 16) : 0) | (note & 0x7Fu) << 8 | (vel & 0x7Fu);
    live_w = w + 1u;
}
void jt_live_all_off(void) { c_live_off = 1; }
void jt_panic(void) { c_panic = 1; }

/* tap tempo: the main loop's taps (plat_ms) */
static uint32_t taps[5], ntaps;
static float tap_bpm;
void jt_tap(void)
{
    uint32_t now = plat_ms(), i;
    if (ntaps && now - taps[ntaps - 1u] > 2000u)
        ntaps = 0;
    if (ntaps == 5u) {
        for (i = 0; i < 4u; i++)
            taps[i] = taps[i + 1u];
        ntaps = 4;
    }
    taps[ntaps++] = now;
    if (ntaps >= 2u) {
        float ms = (float)(taps[ntaps - 1u] - taps[0]) / (float)(ntaps - 1u);
        if (ms > 230.0f && ms < 1600.0f) {
            tap_bpm = 60000.0f / ms;
            jam.bpm10 = (uint16_t)(tap_bpm * 10.0f + 0.5f);
        }
    }
}
float jt_tap_bpm(void) { return ntaps >= 2u ? tap_bpm : 0.0f; }

/* ---------------------------------------------------------------- MIDI: in, the clock, to the main loop */
#define MQ 64
static volatile uint32_t mq[MQ];
static volatile uint32_t mq_w, mq_r;
int jt_midi_take(uint32_t *pkt)
{
    uint32_t r = mq_r;
    if (r == mq_w)
        return 0;
    *pkt = mq[r % MQ];
    mq_r = r + 1u;
    return 1;
}
uint32_t jt_clock_seen(void) { return sq.clock_ms; }

/* the clock's time base: on the FM-1 each message's own stamp (fm1.toml midi_stamp) on the 24 MHz timer; in the host
 * simulator and the browser that timer is the computer's, which runs the FM-1 in bursts, so its milliseconds instead */
#ifdef PLAT_HOST
static uint32_t now24(void) { return plat_ms() * 24000u; }
static uint32_t msg24(void) { return plat_ms() * 24000u; }
#else
static uint32_t now24(void) { return plat_ticks24(); }
static uint32_t msg24(void) { return plat_midi_in_t24(); }
#endif
static uint32_t render_t24;              /* the time base at the last render */
static uint16_t clk_off[48];
static uint32_t nclk;
static uint8_t m_start, m_stop, m_cont;
static int32_t m_spp = -1;

static void midi_out_rt(uint32_t st_)
{
    if (settings.clock_out)
        plat_midi_send(0x0Fu | st_ << 8, TO_USB);
}

static void midi_read(uint32_t n)
{
    uint32_t pkt, t0 = render_t24, now = now24();
    int port;
    nclk = 0;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t s = (pkt >> 8) & 0xFFu;
        if (s == 0xF8u) {
            uint32_t t = msg24(), off;
            float d;
            if (settings.clock_in)
                continue;
            sq.clock_ms = plat_ms() | 1u;
            if (sq.last_rx_t) {          /* the clock's pace: smoothed, a jump taken after it repeats */
                float dt = (float)(t - sq.last_rx_t) * ((float)JT_SR / (1000000.0f * (float)PLAT_TICKS_PER_US));
                static uint8_t odd;
                if (dt > 20.0f && dt < 3000.0f) {
                    if (sq.spt <= 0.0f || (dt > sq.spt * 0.6f && dt < sq.spt * 1.6f)) {
                        sq.spt = sq.spt <= 0.0f ? dt : sq.spt + (dt - sq.spt) * 0.06f;
                        odd = 0;
                    } else if (++odd >= 4) {
                        sq.spt = dt;
                        odd = 0;
                    }
                }
            }
            sq.last_rx_t = t;
            d = (float)(t - t0) * ((float)JT_SR / (1000000.0f * (float)PLAT_TICKS_PER_US));
            off = t0 && d > 0.0f && d < (float)n ? (uint32_t)d : 0u;
            if ((int32_t)(t - t0) > (int32_t)(now - t0))
                off = 0;
            if (nclk < 48u)
                clk_off[nclk++] = (uint16_t)off;
        } else if (s == 0xFAu || s == 0xFBu || s == 0xFCu) {
            if (!settings.startstop_in)
                continue;
            if (s == 0xFAu)
                m_start = 1;
            else if (s == 0xFBu)
                m_cont = 1;
            else
                m_stop = 1;
        } else if (s == 0xF2u) {
            m_spp = (int32_t)(((pkt >> 16) & 0x7Fu) | ((pkt >> 24) & 0x7Fu) << 7);
        } else if (s >= 0xF8u) {
            continue;                    /* active sensing and the rest */
        } else {
            uint32_t w = mq_w;
            if (w - mq_r < MQ) {
                mq[w % MQ] = pkt;
                mq_w = w + 1u;
            }
        }
    }
    render_t24 = now;
    if (sq.clock_ms && (int32_t)(plat_ms() - sq.clock_ms) > 600) {   /* the clock stopped: our own time again */
        sq.clock_ms = 0;
        sq.last_rx_t = 0;
        sq.spt = 0;
        if (sq.ext) {
#ifdef JT_DEBUG
            printf("ext off: clock stopped ms %u last %u\n", plat_ms(), sq.clock_ms);
#endif
            sq.ext = 0;
            sq.cap = 1e9f;
        }
    }
}

/* ---------------------------------------------------------------- start, stop */
static float own_rate(void)
{
    float bpm = (float)jam.bpm10 * 0.1f;
    bpm = fm_clampf(bpm, 30.0f, 300.0f);
    return bpm * (float)JT_PPQN / 60.0f / (float)JT_SR;
}

static void release_part(int part)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (V[i].on && V[i].part == part)
            voice_release(&V[i]);
}

static void start_now(int count_in, int ext)
{
    int32_t bar_ticks = (int32_t)bar_steps(jam.meter % JT_METER_N) * (int32_t)JT_STEP_TICKS;
    seq_reset();
    sq.part = 0;
    part_switch(0);
    sq.ext = (uint8_t)ext;
    sq.pos = count_in ? -(float)(count_in * bar_ticks) : ext ? -0.5f : 0.0f;
    sq.next_tick = count_in ? (int32_t)sq.pos : 0;
    sq.cap = ext ? -0.5f : 1e9f;
    sq.rx = 0;
    sq.ext_wait = (uint8_t)ext;
    sq.want_stop = 0;
    sq.playing = 1;
    sq.out_tick = 0;
    nev = 0;
    sq.wait_until = now_s;
    if (!ext && settings.clock_out && !count_in)
        midi_out_rt(0xFAu);
}

static void stop_now(void)
{
    int i;
    if (sq.playing && !sq.ext && settings.clock_out)
        midi_out_rt(0xFCu);
    sq.playing = 0;
    sq.ending = 0;
    sq.ext_wait = 0;
    nev = 0;
    for (i = 0; i < NVOICE; i++)
        if (i < V_LIVE0)
            voice_release(&V[i]);
    st.counting = 0;
}

static void commands(void)
{
    if (c_panic) {
        int i;
        c_panic = 0;
        stop_now();
        for (i = 0; i < NVOICE; i++)
            V[i].on = 0;
        drums_panic();
        fx_panic();
    }
    /* MIDI's START, CONTINUE and STOP */
    if (m_start) {
        m_start = 0;
        /* wait for the clock's first tick (the downbeat); none within 90 ms: our own time from then */
        start_now(0, !settings.clock_in);
        sq.wait_until = now_s + (uint32_t)(sq.clock_ms ? 0.6f * (float)JT_SR : 0.09f * (float)JT_SR);
    }
    if (m_cont) {
        m_cont = 0;
        if (!sq.playing) {
            int clocked = !settings.clock_in;
            int32_t at = m_spp >= 0 ? m_spp * (int32_t)JT_STEP_TICKS : sq.next_tick;
            if (m_spp >= 0) {            /* the song position: the bar and step it says */
                start_now(0, clocked);
                {
                    int32_t bt = (int32_t)sq.bsteps * (int32_t)JT_STEP_TICKS;
                    int32_t bars = at / bt, k;
                    for (k = 0; k < bars; k++) {   /* the progression moved on as the bars went by */
                        sq.pstep = (sq.pstep + sq.bsteps) % sq.ptotal;
                        sq.bar++;
                        sq.part_bars++;
                    }
                    sq.next_tick = bars * bt;
                    sq.pos = (float)sq.next_tick;
                }
            } else {
                start_now(0, clocked);
            }
            if (clocked) {
                sq.cap = sq.pos - 0.5f;
                sq.rx = sq.next_tick;
                sq.ext_wait = 2;         /* (CONTINUE: the next tick goes on from here) */
                sq.wait_until = now_s + (uint32_t)(0.6f * (float)JT_SR);
            }
        }
        m_spp = -1;
    }
    if (m_stop) {
        m_stop = 0;
        stop_now();
    }
    if (c_play) {
        c_play = 0;
        if (!sq.playing)
            start_now(jam.count > 2 ? 2 : jam.count, 0);
    }
    if (c_stop_now) {
        c_stop_now = 0;
        stop_now();
    }
    if (c_stop) {
        c_stop = 0;
        if (sq.playing) {
            if (settings.stop_mode && !sq.ext && sq.pos >= 0.0f)
                sq.want_stop = 1;        /* to the end of the bar, then the last hit */
            else
                stop_now();
        }
    }
    if (c_part != -2) {
        int to = c_part < 0 ? sq.part ^ 1 : c_part;
        c_part = -2;
        if (!sq.playing) {
            sq.part = to;
            st.part = (uint8_t)to;
        } else if (sq.queued && sq.qpart == to)
            sq.queued = 0;               /* asked again: never mind */
        else if (to != sq.part) {
            sq.queued = 1;
            sq.qpart = (int8_t)to;
        } else
            sq.queued = 0;
        st.queued = sq.queued;
    }
    if (c_fill) {
        c_fill = 0;
        sq.want_fill = 1;
    }
    if (c_restart) {
        c_restart = 0;
        sq.want_restart = 1;
    }
    if (c_mutes_changed) {
        int p;
        c_mutes_changed = 0;
        for (p = JP_BASS; p < JP_N; p++)
            if (jam.mute & (1u << p))
                release_part(p);
    }
    if (c_live_off) {
        int i;
        c_live_off = 0;
        for (i = V_LIVE0; i < NVOICE; i++)
            voice_release(&V[i]);
        nheld = 0;
    }
    if (jam_changed_flags) {
        uint32_t f = jam_changed_flags;
        jam_changed_flags = 0;
        sq.changed |= f;
        if (f & JX_PROG)
            prog_layout();
        if (f & (JX_PROG | JX_SOUND))
            sq.comp_v_of = -1;
    }
    if (c_aud_part >= 0) {               /* the editor's chord, heard */
        int part = c_aud_part & 1, ci = c_aud_chord, k;
        c_aud_part = -1;
        if (!sq.playing && ci >= 0 && ci < (int)JT_NCHORD) {
            chord_t c;
            int v[6], nv, save = sq.part;
            const patch_t *pt = part_patch(JP_CHORDS);
            sq.part = part;
            chord_resolve(&jam, part_chord(ci), &c);
            nv = voice_chord(&c, pt->voicing, 0, 0, 62 + 12 * (jam.oct[JP_CHORDS] + pt->oct), v);
            for (k = V_CHORD0; k < V_SYNTH0; k++)
                voice_kill(&V[k], 0.03f);
            for (k = 0; k < nv; k++) {
                voice_t *vv = voice_find(V_CHORD0, V_SYNTH0);
                voice_start(vv, pt, JP_CHORDS, v[k] + (pt->voicing != VO_CLOSE ? 12 * (jam.oct[JP_CHORDS] + pt->oct) : 0), 0.7f,
                            (uint32_t)(0.9f * (float)JT_SR), (float)(k - nv / 2) * 0.15f);
            }
            {
                voice_t *vb = voice_find(V_BASS0, V_CHORD0);
                int bn = 28 + 5 + ((c.bass - 5 + 12) % 12) + 12 * jam.oct[JP_BASS];
                voice_kill(&V[V_BASS0], 0.03f);
                voice_kill(&V[V_BASS0 + 1], 0.03f);
                voice_start(vb, part_patch(JP_BASS), JP_BASS, bn, 0.7f, (uint32_t)(0.9f * (float)JT_SR), 0);
            }
            sq.part = save;
        }
    }
}

/* ---------------------------------------------------------------- events -> voices */
static float click_ph, click_env, click_hz;   /* the count-in's and metronome's click */
static float part_width(void) { return (float)jam.width * 0.01f * 1.3f; }

static void dispatch(const event_t *e)
{
    if (e->type == EV_DRUM) {
        const kit_t *kit = &KITS[jam.kit % NKITS];
        if (jam.mute & 1u)
            return;
        drum_hit(kit, e->note, (float)e->vel / 127.0f, (float)((int)e->part - 64), part_width(),
                 ((float)jam.tone[JP_DRUMS] - 50.0f) * 0.02f);
    } else if (e->type == EV_NOTE) {
        int part = e->part, lo, hi, i;
        const patch_t *pt = part_patch(part);
        voice_t *v;
        if (jam.mute & (1u << part))
            return;
        lo = part == JP_BASS ? V_BASS0 : part == JP_CHORDS ? V_CHORD0 : V_SYNTH0;
        hi = part == JP_BASS ? V_CHORD0 : part == JP_CHORDS ? V_SYNTH0 : V_LIVE0;
        if (e->tie)                      /* the same note still held: it goes on */
            for (i = lo; i < hi; i++)
                if (V[i].on && V[i].stage != 3 && V[i].note == e->note && V[i].pt == pt) {
                    V[i].gate = e->gate;
                    return;
                }
        if (part == JP_BASS) {           /* the bass is one line: the last note makes way (or slides on) */
            const bstyle_t *b = &BSTYLES[jam.bstyle % NBSTYLES];
            for (i = lo; i < hi; i++)
                if (V[i].on && V[i].stage != 3) {
                    if (b->glide && V[i].pt == pt && pt->eng != E_KS) {
                        V[i].hz_to = note_hz((float)e->note);
                        V[i].glide_k = decay_k(0.07f);
                        V[i].note = e->note;
                        V[i].gate = e->gate;
                        return;
                    }
                    voice_kill(&V[i], 0.018f);
                }
        }
        if (part == JP_CHORDS && pt->strum)   /* a string sounding again: the old note stops */
            for (i = lo; i < hi; i++)
                if (V[i].on && V[i].note == e->note)
                    voice_kill(&V[i], 0.01f);
        v = voice_find(lo, hi);
        if (v->on)
            voice_kill(v, 0.005f);
        voice_start(v, pt, part, e->note, (float)e->vel / 127.0f, e->gate, (float)e->pan * 0.01f * part_width());
    } else if (e->type == EV_CLICK) {
        float a = (float)e->vel / 127.0f;
        click_env = 0.55f * a;
        click_hz = a > 0.9f ? 1760.0f : 1320.0f;
        click_ph = 0;
    }
}

/* the keys: their own sound and bus. Mono (a lead): one voice, sliding to each new note, back to the last one still
 * held when a key is let go */
static void live_events(void)
{
    while (live_r != live_w) {
        uint32_t m = live_q[live_r % LIVE_Q], note = (m >> 8) & 0x7Fu, i;
        const patch_t *pt = &PATCHES[settings.live_patch % NPATCHES];
        float vel = (float)(m & 0x7Fu) / 127.0f;
        live_r++;
        if (m & (1u << 16)) {
            if (nheld < 8u)
                held_notes[nheld++] = (uint8_t)note;
            if (settings.live_mono)
                for (i = V_LIVE0; i < NVOICE; i++)
                    if (V[i].on && V[i].stage != 3 && V[i].part == JP_LIVE && V[i].pt == pt) {
                        V[i].hz_to = note_hz((float)note);
                        V[i].glide_k = decay_k(0.06f);
                        V[i].note = (uint8_t)note;
                        goto next;
                    }
            {
                voice_t *v = voice_find(V_LIVE0, NVOICE);
                if (v->on)
                    voice_kill(v, 0.005f);
                if (settings.live_mono)  /* (the other keys' voice: quiet) */
                    for (i = V_LIVE0; i < NVOICE; i++)
                        if (&V[i] != v && V[i].on)
                            voice_kill(&V[i], 0.02f);
                voice_start(v, pt, JP_LIVE, (int)note, vel, 0xFFFFFFFFu, 0);
            }
        } else {
            uint32_t k, w = 0;
            for (k = 0; k < nheld; k++)  /* off the held list */
                if (held_notes[k] != note)
                    held_notes[w++] = held_notes[k];
            nheld = (uint8_t)w;
            for (i = V_LIVE0; i < NVOICE; i++)
                if (V[i].on && V[i].note == note && V[i].stage != 3) {
                    if (settings.live_mono && nheld) {   /* back to the last key still held */
                        V[i].hz_to = note_hz((float)held_notes[nheld - 1u]);
                        V[i].glide_k = decay_k(0.06f);
                        V[i].note = held_notes[nheld - 1u];
                    } else
                        voice_release(&V[i]);
                }
        }
next:;
    }
}

/* ---------------------------------------------------------------- the render */
static float busl[JP_N + 1][CHUNK], busr[JP_N + 1][CHUNK];
static float lvl_now[JP_N + 1];
static uint8_t bus_used[JP_N + 1];      /* this chunk: something played into the bus */
static float meter_acc[JP_N + 1];
static float peak_l, peak_r;
static jt_meter_t meter;
static float space_now = -1;
static float trem_ph;

const jt_meter_t *jt_meter(void) { return &meter; }
void jt_meter_reset(void) { meter.t_render = meter.t_voices = meter.t_fx = 0; }

/* what the mix needs, worked out once a render (the mix itself runs from RAM: nothing from the flash in it) */
static struct {
    float lvl[JP_N + 1], hi[JP_N + 1], send[JP_N + 1], chs, chy, chl, trem, wet, width, tape, glue, gain;
} mx;

static void mix_prepare(float gain)
{
    static const float NORM[JP_N] = {0.95f, 0.75f, 0.62f, 0.55f};
    const patch_t *pc = part_patch(JP_CHORDS), *ps = part_patch(JP_SYNTH);
    int p;
    for (p = 0; p < JP_N; p++) {
        mx.lvl[p] = (jam.mute & (1u << p)) ? 0.0f : fm_powf((float)jam.level[p] * 0.01f, 1.5f) * NORM[p] * 1.4f;
        mx.hi[p] = fm_exp2f(((float)jam.tone[p] - 50.0f) * (p == JP_DRUMS ? 0.028f : 0.014f));
        mx.send[p] = (float)jam.verb[p] * 0.01f;
    }
    mx.send[JP_DRUMS] += (float)KITS[jam.kit % NKITS].room * 0.004f;
    mx.lvl[JP_LIVE] = fm_powf((float)settings.live_level * 0.01f, 1.5f) * 0.9f;
    mx.hi[JP_LIVE] = 1.0f;
    mx.send[JP_LIVE] = (float)settings.live_verb * 0.01f;
    mx.chl = (float)PATCHES[settings.live_patch % NPATCHES].chorus * 0.01f;
    mx.chs = (float)pc->chorus * 0.01f;
    mx.chy = (float)ps->chorus * 0.01f;
    mx.trem = (float)pc->trem * 0.01f;
    mx.wet = (float)jam.rev * 0.01f * 0.9f;
    mx.width = (float)jam.width * 0.01f * 1.4f;
    mx.tape = (float)jam.tape * 0.01f;
    mx.glue = (float)jam.glue * 0.01f;
    mx.gain = gain * 8300000.0f;
}

PLAT_RAM_CODE static void mix_chunk(int32_t *lr, uint32_t len)
{
    float revl[CHUNK], revr[CHUNK], chl[CHUNK], chr_[CHUNK], ml[CHUNK], mr[CHUNK], chin[CHUNK];
    uint32_t i, p;
    for (i = 0; i < len; i++)
        revl[i] = revr[i] = chl[i] = chr_[i] = ml[i] = mr[i] = chin[i] = 0;
    glue_run(busl[JP_DRUMS], busr[JP_DRUMS], len, mx.glue);
    if (mx.trem > 0.0f) {                /* tremolo: the electric piano's autopan, the vibes' */
        float d = mx.trem;
        for (i = 0; i < len; i++) {
            float s = sine_t(trem_ph);
            trem_ph = wrap1(trem_ph + 4.6f / (float)JT_SR);
            busl[JP_CHORDS][i] *= 1.0f - d * (0.5f + 0.5f * s);
            busr[JP_CHORDS][i] *= 1.0f - d * (0.5f - 0.5f * s);
        }
    }
    for (p = 0; p <= JP_LIVE; p++) {
        float target = mx.lvl[p], g = lvl_now[p], dg = (target - g) / (float)len, send = mx.send[p], pk = 0;
        if (!bus_used[p]) {              /* nothing on it: nothing to mix (its meter falls) */
            lvl_now[p] = target;
            meter_acc[p] *= 0.995f;
            continue;
        }
        tilt_run((int)p, busl[p], busr[p], len, mx.hi[p]);
        for (i = 0; i < len; i++) {
            float l = busl[p][i] * g, r = busr[p][i] * g;
            g += dg;
            ml[i] += l;
            mr[i] += r;
            revl[i] += l * send;
            revr[i] += r * send;
            if (p == JP_CHORDS)
                chin[i] += (l + r) * mx.chs;
            else if (p == JP_SYNTH)
                chin[i] += (l + r) * mx.chy;
            else if (p == JP_LIVE)
                chin[i] += (l + r) * mx.chl;
            l = rm_fabs(l) + rm_fabs(r);
            if (l > pk)
                pk = l;
        }
        lvl_now[p] = target;
        meter_acc[p] = rm_max(meter_acc[p] * 0.995f, pk);
    }
    if (mx.chs > 0.0f || mx.chy > 0.0f || mx.chl > 0.0f) {
        chorus_run(chin, chl, chr_, len);
        for (i = 0; i < len; i++) {
            ml[i] += chl[i] * 0.45f;
            mr[i] += chr_[i] * 0.45f;
        }
    }
    {
        float rl[CHUNK], rr[CHUNK], wet = mx.wet;
        for (i = 0; i < len; i++)
            rl[i] = rr[i] = 0;
        reverb_run(revl, revr, rl, rr, len);
        for (i = 0; i < len; i++) {
            ml[i] += rl[i] * wet;
            mr[i] += rr[i] * wet;
        }
    }
    if (mx.width < 0.98f || mx.width > 1.02f)   /* width: the sides scaled */
        for (i = 0; i < len; i++) {
            float m = (ml[i] + mr[i]) * 0.5f, s = (ml[i] - mr[i]) * 0.5f * mx.width;
            ml[i] = m + s;
            mr[i] = m - s;
        }
    if (click_env > 0.0005f)             /* the count-in's and metronome's click */
        for (i = 0; i < len; i++) {
            float c = sine_t(click_ph) * click_env;
            click_ph = wrap1(click_ph + click_hz / (float)JT_SR);
            click_env *= 0.9975f;
            ml[i] += c;
            mr[i] += c;
        }
    if (mx.tape > 0.0f)
        tape_run(ml, mr, len, mx.tape);
    limit_run(ml, mr, len);
    for (i = 0; i < len; i++) {
        float l = ml[i], r = mr[i];
        if (rm_fabs(l) > peak_l)
            peak_l = rm_fabs(l);
        if (rm_fabs(r) > peak_r)
            peak_r = rm_fabs(r);
        lr[2u * i] = (int32_t)(l * mx.gain);
        lr[2u * i + 1u] = (int32_t)(r * mx.gain);
    }
}

void jt_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t pos = 0, ci = 0, t_start = plat_ticks24(), tv = 0;
    float space = (float)jam.space * 0.01f;
    commands();
    midi_read(n);
    live_events();
    mix_prepare(gain);
    if (fm_fabsf(space - space_now) > 0.01f) {
        reverb_set(space);
        space_now = space;
    }
    {
        int p;
        for (p = 0; p < JP_N; p++)
            part_tone_oct[p] = ((float)jam.tone[p] - 50.0f) * 0.02f;
    }
    if (sq.spt > 0.0f && sq.clock_ms && !settings.clock_in)
        sq.rate = 1.0f / sq.spt;         /* a clock comes in: its tempo, even when it never said START */
    else
        sq.rate = own_rate();
    if (sq.ext_wait && sq.playing && (int32_t)(now_s - sq.wait_until) > 0 && !nclk) {
        int32_t at = sq.ext_wait == 2 ? sq.next_tick : 0;   /* no clock came: on our own time from here */
#ifdef JT_DEBUG
        printf("ext off: no clock came (wait %u)\n", sq.ext_wait);
#endif
        sq.ext_wait = 0;
        sq.ext = 0;
        sq.cap = 1e9f;
        sq.pos = (float)at;
        sq.next_tick = at;
        sq.rate = own_rate();
        if (settings.clock_out)
            midi_out_rt(at ? 0xFBu : 0xFAu);
    }
    while (pos < n) {
        uint32_t len = n - pos > CHUNK ? CHUNK : n - pos, i, p;
        uint32_t t0;
        /* a clock tick that came in at this point */
        while (ci < nclk && clk_off[ci] <= pos) {
            ci++;
            if (sq.ext_wait) {           /* the first tick after START: the downbeat (after CONTINUE: where it was) */
                sq.ext_wait = 0;
                sq.ext = 1;
                sq.pos = (float)sq.rx;
                sq.next_tick = sq.rx;
                if (sq.spt > 0.0f)
                    sq.rate = 1.0f / sq.spt;
            }
            if (sq.ext && sq.playing) {
                sq.cap = (float)sq.rx + 0.999f;
                if (sq.pos < (float)sq.rx)
                    sq.pos = (float)sq.rx;
                sq.rx++;
            }
        }
        if (ci < nclk && clk_off[ci] - pos < len)
            len = clk_off[ci] - pos;
        if (sq.playing) {
            while (sq.playing && !sq.ext_wait && (float)sq.next_tick <= sq.pos) {
                int32_t t = sq.next_tick++;
                tick(t);
                if (!sq.ext && settings.clock_out && t >= 0) {
                    if (t == 0 && jam.count)
                        midi_out_rt(0xFAu);
                    midi_out_rt(0xF8u);
                }
            }
            if (sq.playing) {
                float to = (float)sq.next_tick - sq.pos;
                if (!sq.ext || (float)sq.next_tick <= sq.cap) {
                    uint32_t k = (uint32_t)(to / (sq.rate > 1e-6f ? sq.rate : 1e-6f)) + 1u;
                    if (k < len)
                        len = k;
                }
            }
        }
        /* events due in this chunk start it; the next one cuts it */
        for (i = 0; i < nev;) {
            if ((int32_t)(EV[i].due - now_s) <= 0) {
                event_t e = EV[i];
                EV[i] = EV[--nev];
                dispatch(&e);
            } else {
                uint32_t d = EV[i].due - now_s;
                if (d < len)
                    len = d;
                i++;
            }
        }
        if (!len)
            len = 1;
        for (p = 0; p <= JP_LIVE; p++) {
            bus_used[p] = 0;
            for (i = 0; i < len; i++)
                busl[p][i] = busr[p][i] = 0;
        }
        t0 = plat_ticks24();
        PLAT_FAR(drums_render)(busl[JP_DRUMS], busr[JP_DRUMS], len);
        bus_used[JP_DRUMS] = 1;          /* (the drum bus: always, for the glue's and tilt's state) */
        {
            int nvon = 0;
            for (i = 0; i < NVOICE; i++)
                if (V[i].on) {
                    int pp = V[i].part <= JP_LIVE ? V[i].part : JP_SYNTH;
                    PLAT_FAR(voice_render)(&V[i], busl[pp], busr[pp], len);
                    bus_used[pp] = 1;
                    nvon++;
                }
            st.voices = (uint8_t)nvon;
        }
        tv += plat_ticks24() - t0;
        PLAT_FAR(mix_chunk)(lr + 2u * pos, len);
        if (sq.playing) {
            sq.pos += sq.rate * (float)len;
            if (sq.ext && sq.pos > sq.cap)
                sq.pos = sq.cap;
        }
        now_s += len;
        pos += len;
    }
    /* the status for the screens */
    st.playing = sq.playing;
    st.ext = sq.ext;
    st.ending = sq.ending;
    st.bpm = sq.ext && sq.spt > 0.0f ? 60.0f * (float)JT_SR / (sq.spt * (float)JT_PPQN)
           : sq.clock_ms && sq.spt > 0.0f ? 60.0f * (float)JT_SR / (sq.spt * (float)JT_PPQN) : (float)jam.bpm10 * 0.1f;
    {
        int p;
        for (p = 0; p < JP_N; p++)
            st.level[p] = (uint8_t)(fm_minf(meter_acc[p], 1.0f) * 100.0f);
    }
    if ((uint32_t)(peak_l * 1000.0f) > st.peak_l)
        st.peak_l = (uint16_t)fm_minf(peak_l * 1000.0f, 1000.0f);
    if ((uint32_t)(peak_r * 1000.0f) > st.peak_r)
        st.peak_r = (uint16_t)fm_minf(peak_r * 1000.0f, 1000.0f);
    peak_l = peak_r = 0;
    {
        uint32_t t = plat_ticks24() - t_start;
        float budget = (float)n * (1000000.0f * (float)PLAT_TICKS_PER_US / (float)JT_SR);
        st.cpu += ((float)t / budget - st.cpu) * 0.05f;
        if ((float)t > 0.8f * budget) {  /* running hot: the oldest voice already letting go stops now (a tail) */
            int k, best = -1;
            uint32_t oldest = 0xFFFFFFFFu;
            for (k = 0; k < V_LIVE0; k++)
                if (V[k].on && V[k].stage == 3 && V[k].age < oldest) {
                    oldest = V[k].age;
                    best = k;
                }
            if (best >= 0) {
                voice_kill(&V[best], 0.004f);
                meter.xrun_guard++;
            }
        }
        if (t > meter.t_render)
            meter.t_render = t;
        if (tv > meter.t_voices)
            meter.t_voices = tv;
    }
}

void jt_init(void)
{
    voices_init();
    drums_init();
    fx_init();
    jt_jam_default(&jam);
    sq.comp_v_of = -1;
    sq.cap = 1e9f;
    seq_reset();
}

_Static_assert(NGROOVES == G_ROCKERS + 1, "jt_ids.h: the grooves");
_Static_assert(NBSTYLES == B_LOCKED + 1, "jt_ids.h: the bass lines");
_Static_assert(NCSTYLES == C_BLOCKS + 1, "jt_ids.h: the comping");
_Static_assert(NPATCHES == P_SAWLEAD + 1, "jt_ids.h: the sounds");
_Static_assert(NKITS == K_USER + 1, "jt_ids.h: the kits");
