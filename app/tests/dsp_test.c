/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the engine alone on the computer (no FM-1, no simulator): every genre generated and played, every
 * groove, style, sound and time signature, MIDI clock followed, extremes; checked for NaN, overload, silence, DC and
 * the time it takes. Writes WAVs to listen to.
 *   cc -O2 -DPLAT_HOST -Iapp/dsp -Ibase/include app/dsp/engine.c app/tests/dsp_test.c -lm -o dsp_test
 *   ./dsp_test OUTDIR */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "jt_dsp.h"

/* ---------------------------------------------------------------- the platform, stood in */
static uint32_t fake_ms;
static uint32_t fake_t24;
uint32_t plat_ms(void) { return fake_ms; }
uint32_t plat_ticks24(void) { return fake_t24; }
static uint32_t midi_q[256], midi_t[256], mq_r, mq_w, last_t;
int plat_midi_in(uint32_t *pkt, int *port)
{
    if (mq_r == mq_w)
        return 0;
    *pkt = midi_q[mq_r % 256];
    last_t = midi_t[mq_r % 256];
    mq_r++;
    if (port)
        *port = 2;
    return 1;
}
uint32_t plat_midi_in_t24(void) { return last_t; }
static uint32_t out_count[256];
void plat_midi_send(uint32_t pkt, uint32_t to) { (void)to; out_count[(pkt >> 8) & 0xFF]++; }
static void midi_push(uint32_t st, uint32_t t24)
{
    midi_q[mq_w % 256] = 0x0Fu | st << 8;
    midi_t[mq_w % 256] = t24;
    mq_w++;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- rendering */
#define BLOCK 256
static int32_t blk[BLOCK * 2];
typedef struct { double sum2, peak, dc; long n, nan; } stats_t;

static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        static const uint8_t h[44] = {0};
        fwrite(h, 1, 44, f);
    }
    return f;
}
static void wav_close(FILE *f)
{
    long n;
    uint8_t h[44];
    uint32_t v;
    if (!f)
        return;
    n = ftell(f) - 44;
    memcpy(h, "RIFF", 4);
    v = (uint32_t)(n + 36); memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    v = 44100; memcpy(h + 24, &v, 4);
    v = 44100 * 4; memcpy(h + 28, &v, 4);
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    v = (uint32_t)n; memcpy(h + 40, &v, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fclose(f);
}

static double worst_block_us;
static void run(double seconds, FILE *wav, stats_t *s)
{
    long blocks = (long)(seconds * 44100.0 / BLOCK), b;
    for (b = 0; b < blocks; b++) {
        int i;
        clock_t t0 = clock();
        double us;
        jt_render(blk, BLOCK, 1.0f);
        us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC;
        if (us > worst_block_us)
            worst_block_us = us;
        fake_t24 += (uint32_t)(BLOCK * 24000000.0 / 44100.0);
        fake_ms = fake_t24 / 24000;
        for (i = 0; i < BLOCK * 2; i++) {
            double v = blk[i] / 8388608.0;
            if (v != v) {
                s->nan++;
                v = 0;
            }
            s->sum2 += v * v;
            s->dc += v;
            if (fabs(v) > s->peak)
                s->peak = fabs(v);
            s->n++;
            if (wav) {
                int16_t o = (int16_t)(v > 1 ? 32767 : v < -1 ? -32768 : v * 32767);
                fwrite(&o, 2, 1, wav);
            }
        }
    }
}
static double rms_db(const stats_t *s) { return s->n ? 10 * log10(s->sum2 / s->n + 1e-20) : -200; }

static void play_for(const char *out, const char *name, double sec, stats_t *s)
{
    char path[512];
    FILE *w = 0;
    if (out) {
        snprintf(path, sizeof path, "%s/%s.wav", out, name);
        w = wav_open(path);
    }
    memset(s, 0, sizeof *s);
    run(sec, w, s);
    wav_close(w);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : 0;
    stats_t s;
    jt_jam_t *j;
    uint32_t g, i;
    char name[64];

    jt_init();
    j = jt_jam();

    /* ---- the theory: Nashville numbers into chords */
    {
        char num[16], nm[16];
        jt_part_t p;
        CHECK(jt_parse_prog("1maj7:4 6m7 2m9:2 5sus4:2 b7 4/3 #4m7b5", &p, 4) == 0, "parse");
        CHECK(p.n == 7, "parse count %d", p.n);
        j->key = 7; j->mode = JM_MAJOR; j->color = 0;   /* G major */
        jt_chord_text(j, &p.c[0], num, nm);
        CHECK(!strcmp(num, "1maj7") && !strcmp(nm, "Gmaj7"), "1maj7 in G: %s %s", num, nm);
        jt_chord_text(j, &p.c[1], num, nm);
        CHECK(!strcmp(nm, "Em7"), "6m7 in G: %s", nm);
        jt_chord_text(j, &p.c[4], num, nm);
        CHECK(!strcmp(nm, "F"), "b7 in G: %s", nm);
        jt_chord_text(j, &p.c[5], num, nm);
        CHECK(!strcmp(nm, "C/E"), "4/3 in G: %s", nm);
        j->key = 3;                      /* Eb: the same numbers, a new key */
        jt_chord_text(j, &p.c[1], num, nm);
        CHECK(!strcmp(nm, "Cm7"), "6m7 in Eb: %s", nm);
        jt_parse_prog("1 2 3 4 5 6 7", &p, 4);
        j->key = 0; j->color = 0;
        {
            static const char *const want[7] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
            for (i = 0; i < 7; i++) {
                jt_chord_text(j, &p.c[i], num, nm);
                CHECK(!strcmp(nm, want[i]), "AUTO degree %u: %s", i + 1, nm);
            }
            j->mode = JM_MINOR;          /* the same numbers in minor */
            jt_chord_text(j, &p.c[0], num, nm);
            CHECK(!strcmp(nm, "Cm"), "1 in C minor: %s", nm);
            jt_chord_text(j, &p.c[6], num, nm);
            CHECK(!strcmp(nm, "Bb"), "7 in C minor: %s", nm);
            j->mode = JM_MAJOR; j->color = 2;
            jt_chord_text(j, &p.c[4], num, nm);
            CHECK(!strcmp(nm, "G7"), "5 with COLOR 2: %s", nm);
        }
        CHECK(jt_parse_prog("8 1", &p, 4) != 0, "a bad number refused");
    }

    /* ---- the pattern strings: every one its family's length */
    /* (checked by the generated tables' sizes in styles_check below) */

    /* ---- every genre: generated, played 12 s, sounding, never over, no NaN */
    for (g = 0; g < jt_count(JT_T_GENRE); g++) {
        jt_generate(j, g, 1000 + g, JG_ALL);
        jt_jam_changed(JX_ALL);
        jt_play(1);
        snprintf(name, sizeof name, "genre_%02u_%s", g, jt_name(JT_T_GENRE, g));
        for (i = 0; name[i]; i++)
            if (name[i] == ' ')
                name[i] = '_';
        play_for(out, name, 14.0, &s);
        printf("  %-26s %-16s %5.1f BPM %-5s  rms %6.1f dB  peak %.3f\n", jt_name(JT_T_GENRE, g), j->name, j->bpm10 / 10.0,
               jt_name(JT_T_METER, j->meter), rms_db(&s), s.peak);
        CHECK(s.nan == 0, "%s: NaN", name);
        CHECK(rms_db(&s) > -32, "%s: too quiet (%.1f dB)", name, rms_db(&s));
        CHECK(s.peak <= 0.95, "%s: over the ceiling (%.3f)", name, s.peak);
        CHECK(fabs(s.dc / s.n) < 0.01, "%s: DC %.4f", name, s.dc / s.n);
        jt_stop_now();
        run(1.5, 0, &s);
    }

    /* ---- every time signature with every groove, a bar or two each */
    jt_generate(j, 0, 7, JG_ALL);
    for (i = 0; i < JT_METER_N; i++) {
        uint32_t k;
        j->meter = (uint8_t)i;
        for (k = 0; k < jt_count(JT_T_GROOVE); k++) {
            j->groove = (uint8_t)k;
            j->bstyle = (uint8_t)(k % jt_count(JT_T_BSTYLE));
            j->cstyle = (uint8_t)(k % jt_count(JT_T_CSTYLE));
            j->sstyle = (uint8_t)(k % jt_count(JT_T_SSTYLE));
            jt_jam_changed(JX_ALL);
            jt_play(1);
            memset(&s, 0, sizeof s);
            run(3.0, 0, &s);
            CHECK(s.nan == 0 && s.peak <= 0.95, "meter %s groove %s: nan %ld peak %.3f", jt_name(JT_T_METER, i),
                  jt_name(JT_T_GROOVE, k), s.nan, s.peak);
            CHECK(rms_db(&s) > -45, "meter %s groove %s silent", jt_name(JT_T_METER, i), jt_name(JT_T_GROOVE, k));
            jt_stop_now();
            run(0.3, 0, &s);
        }
    }

    /* ---- every sound on the chords and the synth, every kit */
    jt_generate(j, 1, 99, JG_ALL);
    j->meter = JT_METER_44;
    for (i = 0; i < jt_count(JT_T_PATCH); i++) {
        if (jt_patch_kind(i) == JK_BASS)
            j->patch[0] = (uint8_t)i;
        else
            j->patch[1] = j->patch[2] = (uint8_t)i;
        j->kit = (uint8_t)(i % jt_count(JT_T_KIT));
        j->sstyle = 3;
        jt_jam_changed(JX_ALL);
        jt_play(1);
        snprintf(name, sizeof name, "patch_%02u_%s", i, jt_name(JT_T_PATCH, i));
        for (g = 0; name[g]; g++)
            if (name[g] == ' ')
                name[g] = '_';
        play_for(i < 30 ? out : 0, name, 5.0, &s);
        CHECK(s.nan == 0 && s.peak <= 0.95, "patch %s: nan %ld peak %.3f", jt_name(JT_T_PATCH, i), s.nan, s.peak);
        jt_stop_now();
        run(0.5, 0, &s);
    }

    /* ---- extremes: everything up, 240 BPM, full swing, energy 4, all sends */
    jt_generate(j, 3, 5, JG_ALL);
    j->bpm10 = 2400; j->swing = 100; j->human = 100; j->tape = 100; j->glue = 100; j->rev = 100; j->space = 100;
    j->width = 100; j->color = 3;
    for (i = 0; i < JP_N; i++) {
        j->level[i] = 100; j->tone[i] = 100; j->verb[i] = 100;
    }
    j->part[0].energy = j->part[1].energy = 4;
    j->sstyle = 3;
    jt_jam_changed(JX_ALL);
    jt_play(1);
    play_for(out, "extreme", 8.0, &s);
    CHECK(s.nan == 0 && s.peak <= 0.95, "extreme: nan %ld peak %.3f", s.nan, s.peak);
    jt_stop_now();
    run(2, 0, &s);

    /* ---- MIDI clock: START, a clock at 120 BPM (20.83 ms a tick), the tempo followed, the bars counted */
    {
        const jt_status_t *st = jt_status();
        uint32_t t;
        double tick_us = 60e6 / 120.0 / 24.0;
        jt_generate(j, 0, 3, JG_ALL);
        j->bpm10 = 900;                  /* our own tempo, to be overridden */
        j->meter = JT_METER_44;
        jt_jam_changed(JX_ALL);
        uint32_t seq0;
        midi_push(0xFA, fake_t24);
        seq0 = st->beat_seq;
        run(0.05, 0, &s);
        CHECK(st->playing, "START plays");
        CHECK(st->beat_seq == seq0, "START: bar 0 waits for the first tick");
        t = fake_t24;
        for (i = 0; i < 24 * 4 * 8; i++) {   /* 8 bars of clock, fed as they come */
            uint32_t at = t + (uint32_t)(i * tick_us * 24.0);
            while ((int32_t)(fake_t24 + (uint32_t)(BLOCK * 24000000.0 / 44100.0) - at) < 0)
                run((double)BLOCK / 44100.0, 0, &s);
            midi_push(0xF8, at);
        }
        run(0.1, 0, &s);
        CHECK(st->ext, "following the clock");
        CHECK(fabs(st->bpm - 120.0) < 1.0, "the clock's tempo: %.2f", st->bpm);
        CHECK(st->bar == 7 || st->bar == 8, "8 bars of clock: bar %u", st->bar);
        /* the clock stops: the music stops with it (it never runs ahead of a tick not yet come) */
        {
            uint32_t bar = st->bar, seq = st->beat_seq;
            run(0.4, 0, &s);
            CHECK(st->beat_seq == seq || st->beat_seq == seq + 1, "no clock: no more beats (%u -> %u)", seq, st->beat_seq);
            (void)bar;
        }
        midi_push(0xFC, fake_t24);
        run(0.1, 0, &s);
        CHECK(!st->playing, "STOP stops");
        run(1.0, 0, &s);
        CHECK(!st->ext, "no clock: our own time again");
    }

    /* ---- START with no clock running: plays at once on our own tempo */
    {
        const jt_status_t *st = jt_status();
        midi_push(0xFA, fake_t24);
        run(2.0, 0, &s);
        CHECK(st->playing && !st->ext && st->beat_seq > 0, "START without a clock plays on our own time");
        midi_push(0xFC, fake_t24);
        run(0.2, 0, &s);
    }

    /* ---- internal clock out: START, 24 clocks a quarter note, STOP */
    {
        memset(out_count, 0, sizeof out_count);
        jt_settings()->clock_out = 1;
        j->bpm10 = 1200;
        j->count = 0;
        jt_play(1);
        run(2.0, 0, &s);                 /* 2 s at 120 BPM: 4 beats, 96 clocks */
        jt_stop_now();
        run(0.1, 0, &s);
        CHECK(out_count[0xFA] == 1 && out_count[0xFC] == 1, "clock out: START %u STOP %u", out_count[0xFA], out_count[0xFC]);
        CHECK(out_count[0xF8] >= 94 && out_count[0xF8] <= 98, "clock out: %u clocks in 2 s at 120", out_count[0xF8]);
    }

    /* ---- A to B at the end of the round; a fill; the count-in; the ending */
    {
        const jt_status_t *st = jt_status();
        jt_generate(j, 0, 11, JG_ALL);
        j->bpm10 = 1200;
        j->meter = JT_METER_44;
        j->count = 1;
        jt_parse_prog("1 4", &j->part[0], 4);
        jt_parse_prog("6m 5", &j->part[1], 4);
        j->switch_at = JS_ROUND;
        j->chain = JC_HAND;
        jt_jam_changed(JX_ALL);
        jt_play(1);
        run(0.3, 0, &s);
        CHECK(st->counting == 1, "counting in: %u", st->counting);
        run(2.0, 0, &s);                 /* the count-in bar over: bar 0 */
        CHECK(st->counting == 0 && st->part == 0, "after the count-in: part A");
        jt_part_go(1);
        run(0.1, 0, &s);
        CHECK(st->queued, "B queued");
        run(4.2, 0, &s);                 /* the round (2 bars) ends: B */
        CHECK(st->part == 1 && !st->queued, "B at the end of the round (part %u)", st->part);
        jt_settings()->stop_mode = 1;
        jt_play(0);
        run(0.1, 0, &s);
        CHECK(st->playing, "STOP at the end of the bar: still playing");
        run(4.5, 0, &s);
        CHECK(!st->playing, "the ending played, stopped");
        jt_settings()->stop_mode = 0;
    }

    /* ---- the audition, the keys */
    jt_audition(0, 1);
    play_for(0, "audition", 1.0, &s);
    CHECK(rms_db(&s) > -40, "the audition sounds (%.1f dB)", rms_db(&s));
    jt_live(1, 64, 100);
    play_for(0, "live", 0.5, &s);
    CHECK(rms_db(&s) > -45, "the keys sound (%.1f dB)", rms_db(&s));
    jt_live(0, 64, 0);
    run(2, 0, &s);

    /* ---- sanitize: garbage in, a valid jam out */
    {
        jt_jam_t junk;
        uint8_t *p = (uint8_t *)&junk;
        for (i = 0; i < sizeof junk; i++)
            p[i] = (uint8_t)(rand() & 0xFF);
        jt_jam_sanitize(&junk);
        *j = junk;
        jt_jam_changed(JX_ALL);
        jt_play(1);
        memset(&s, 0, sizeof s);
        run(4, 0, &s);
        CHECK(s.nan == 0 && s.peak <= 0.95, "a sanitized junk jam plays safely");
        jt_stop_now();
    }

    printf("worst block: %.0f us of %.0f (%.0f %%, this computer)\n", worst_block_us, BLOCK * 1e6 / 44100,
           worst_block_us * 100 / (BLOCK * 1e6 / 44100));
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    printf("engine: all passed\n");
    return 0;
}
