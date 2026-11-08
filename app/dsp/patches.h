/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the sounds. Four engines (voices.h):
 *   SUB    two oscillators, a sub, noise, a resonant low-pass with its envelope, vibrato, glide
 *          p: 0 wave A (0 saw, 1 square, 2 triangle, 3 sine), 1 wave B, 2 B's interval (semitones, fractions detune),
 *             3 B's level, 4 sub, 5 noise, 6 cutoff Hz, 7 resonance 0..1, 8 envelope to cutoff (octaves),
 *             9 its decay (s), 10 vibrato (semitones), 11 poles (1: 12 dB, 2: 24 dB)
 *   FM     a carrier and its modulator (electric pianos, bells, mallets), a tine's ping, bark
 *          p: 0 ratio, 1 index at the strike, 2 its decay (s), 3 index held, 4 tine ratio, 5 tine level, 6 tine decay,
 *             7 feedback, 8 bark (drive), 9 velocity to index
 *   KS     a plucked string (Karplus-Strong): guitars, basses, harp-like keys
 *          p: 0 pick brightness, 1 string damping, 2 ring time (s, at 110 Hz), 3 pick position, 4 body, 5 palm mute,
 *             6 pick click, 7 exciter (0 pick, 1 felt mallet)
 *   ORGAN  drawbars: 0 16', 1 8', 2 4', 3 2 2/3', 4 percussion, 5 its decay, 6 key click, 7 drive
 * a d s r: the amplitude's envelope (seconds; s a level). chorus, trem: the part's chorus and tremolo while this
 * sound plays (0..100). Part of engine.c. */

enum { E_SUB, E_FM, E_KS, E_ORGAN };

typedef struct {
    const char *name;
    uint8_t kind;                        /* JK_BASS or JK_POLY */
    uint8_t eng;
    uint8_t voicing;                     /* VO_* (chords) */
    uint8_t strum;                       /* ms between strings on a strum, 0: a keyboard */
    int8_t oct;                          /* its register against the part's */
    uint8_t chorus, trem;
    float a, d, s, r;
    float p[12];
    float gain;
} patch_t;

static const patch_t PATCHES[] = {
    /* ---- bass */
    {"FINGER", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .003f, 1.2f, .55f, .07f,
     {0, 2, 0, .55f, .35f, 0, 330, .12f, 2.2f, .22f, 0, 2}, 1.0f},
    {"MUTED", JK_BASS, E_KS, VO_CLOSE, 0, 0, 0, 0, .001f, 1.0f, 1.0f, .06f,
     {.22f, .55f, .55f, .18f, .5f, .85f, .2f, 0}, 1.25f},
    {"PICK", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .002f, .7f, .5f, .06f,
     {0, 0, 0.08f, .4f, .2f, 0, 650, .22f, 2.6f, .14f, 0, 2}, .9f},
    {"SYNTH", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .002f, .45f, .45f, .08f,
     {0, 1, -12, .45f, .2f, 0, 260, .5f, 3.6f, .28f, 0, 2}, .95f},
    {"SUB", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .006f, .3f, 1.0f, .12f,
     {3, 2, 0, .18f, 0, 0, 1800, 0, 0, .2f, 0, 1}, 1.25f},
    {"UPRIGHT", JK_BASS, E_KS, VO_CLOSE, 0, 0, 0, 0, .001f, 1.0f, 1.0f, .12f,
     {.32f, .42f, 1.3f, .22f, .7f, .25f, .35f, 0}, 1.2f},
    {"FRETLESS", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .012f, 1.5f, .7f, .12f,
     {2, 3, 0, .5f, .25f, 0, 820, .18f, 1.2f, .3f, .08f, 1}, 1.15f},
    {"MOOG", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .002f, .35f, .55f, .07f,
     {1, 0, .1f, .6f, .3f, 0, 200, .62f, 4.4f, .2f, 0, 2}, .9f},
    /* ---- keys, guitars, pads, leads */
    {"RHODES", JK_POLY, E_FM, VO_CLOSE, 0, 0, 10, 35, .001f, 3.8f, 0, .35f,
     {1, 1.5f, .55f, .22f, 14, .22f, .05f, 0, 0, .9f}, .9f},
    {"WURLI", JK_POLY, E_FM, VO_CLOSE, 0, 0, 5, 22, .001f, 2.6f, 0, .25f,
     {1, 2.4f, .32f, .55f, 7, .08f, .04f, .08f, .55f, .8f}, .85f},
    {"CLEAN GTR", JK_POLY, E_KS, VO_SPREAD, 9, 0, 60, 0, .001f, 1, 1, .09f,
     {.52f, .32f, 2.8f, .16f, .45f, 0, .15f, 0}, .75f},
    {"JANGLE", JK_POLY, E_KS, VO_SPREAD, 7, 0, 85, 0, .001f, 1, 1, .1f,
     {.82f, .14f, 3.4f, .1f, .3f, 0, .3f, 0}, .62f},
    {"NYLON", JK_POLY, E_KS, VO_SPREAD, 13, 0, 10, 0, .001f, 1, 1, .12f,
     {.28f, .48f, 2.4f, .24f, .65f, 0, .1f, 0}, .85f},
    {"ACOUSTIC", JK_POLY, E_KS, VO_SPREAD, 10, 0, 15, 0, .001f, 1, 1, .1f,
     {.7f, .22f, 3.2f, .12f, .8f, 0, .25f, 0}, .7f},
    {"ORGAN", JK_POLY, E_ORGAN, VO_CLOSE, 0, 0, 70, 0, .004f, .1f, 1, .06f,
     {.55f, 1, .55f, .35f, .45f, .25f, .25f, .25f}, .5f},
    {"JUNO PAD", JK_POLY, E_SUB, VO_OPEN, 0, 0, 90, 0, .45f, 1.2f, .85f, 1.1f,
     {0, 0, .12f, .7f, .25f, 0, 1500, .12f, .6f, 1.2f, 0, 1}, .42f},
    {"STRINGS", JK_POLY, E_SUB, VO_OPEN, 0, 0, 80, 0, .6f, 1, 1, 1.3f,
     {0, 0, .07f, .8f, 0, 0, 2400, .05f, .3f, 1, .04f, 1}, .38f},
    {"FELT KEYS", JK_POLY, E_KS, VO_CLOSE, 0, 0, 20, 0, .001f, 1, 1, .3f,
     {.18f, .55f, 2.6f, .5f, .2f, 0, 0, 1}, .9f},
    {"SOFT LEAD", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 25, 0, .025f, .4f, .85f, .3f,
     {2, 3, 12, .25f, 0, 0, 3200, .1f, .4f, .4f, .14f, 1}, .7f},
    {"SQUARE", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 15, 0, .004f, .3f, .7f, .2f,
     {1, 1, .08f, .35f, 0, 0, 2000, .25f, 1.2f, .25f, .1f, 1}, .5f},
    {"POLYSYNTH", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 55, 0, .004f, .55f, .4f, .4f,
     {0, 1, .1f, .45f, .15f, 0, 850, .28f, 2.6f, .35f, 0, 1}, .5f},
    {"BELLS", JK_POLY, E_FM, VO_CLOSE, 0, 1, 20, 0, .001f, 2.6f, 0, 1.2f,
     {3.5f, 2.2f, 1.1f, .3f, 0, 0, .1f, 0, 0, .5f}, .55f},
    {"VIBES", JK_POLY, E_FM, VO_CLOSE, 0, 0, 10, 45, .001f, 3, 0, .7f,
     {4, .7f, .35f, .05f, 10, .08f, .02f, 0, 0, .4f}, .8f},
    {"FLUTE", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 15, 0, .07f, .3f, .9f, .25f,
     {3, 2, 12, .2f, 0, .1f, 2600, .05f, .3f, .2f, .12f, 1}, .8f},
    {"BRASS", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 30, 0, .035f, .35f, .8f, .25f,
     {0, 0, .1f, .7f, 0, 0, 520, .15f, 2.2f, .28f, .04f, 1}, .45f},
    {"PLUCK", JK_POLY, E_SUB, VO_CLOSE, 0, 0, 40, 0, .001f, .28f, 0, .22f,
     {0, 1, 12.06f, .3f, 0, 0, 650, .3f, 3.6f, .13f, 0, 1}, .6f},
    /* ---- for the keys: leads to solo with (mono, sliding: GLO -> KEYS) */
    {"SOLO LEAD", JK_POLY | JK_LEAD, E_SUB, VO_CLOSE, 0, 0, 25, 0, .006f, .7f, .78f, .28f,
     {0, 0, .09f, .55f, .2f, 0, 1500, .28f, 1.4f, .45f, .2f, 2}, .55f},
    {"WHISTLE", JK_POLY | JK_LEAD, E_SUB, VO_CLOSE, 0, 0, 15, 0, .035f, .4f, .9f, .3f,
     {3, 2, 12, .12f, 0, .03f, 4000, .05f, .2f, .3f, .22f, 1}, .75f},
    {"BRIGHT LEAD", JK_POLY | JK_LEAD, E_SUB, VO_CLOSE, 0, 0, 35, 0, .004f, .5f, .7f, .25f,
     {1, 0, .07f, .5f, .15f, 0, 2600, .35f, 1.2f, .3f, .16f, 2}, .45f},
    /* ---- more (after the leads: the jams' numbers stay) */
    {"ACID", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .002f, .25f, .35f, .06f,
     {0, 0, 0, 0, .25f, 0, 240, .78f, 4.8f, .16f, 0, 2}, .8f},
    {"808 BASS", JK_BASS, E_SUB, VO_CLOSE, 0, 0, 0, 0, .002f, 1.6f, .0f, .25f,
     {3, 2, 0, .1f, 0, 0, 900, 0, 1.0f, .05f, 0, 1}, 1.35f},
    {"PIANO", JK_POLY, E_FM, VO_CLOSE, 0, 0, 0, 0, .001f, 3.2f, 0, .3f,
     {1, 2.2f, .9f, .25f, 2, .12f, .3f, .1f, 0, .9f}, .8f},
    {"MARIMBA", JK_POLY, E_FM, VO_CLOSE, 0, 0, 5, 0, .001f, .9f, 0, .4f,
     {4, 2.2f, .06f, 0, 10, .05f, .02f, 0, 0, .5f}, .9f},
    {"HARP", JK_POLY, E_KS, VO_OPEN, 6, 0, 25, 0, .001f, 1, 1, .3f,
     {.6f, .28f, 3.2f, .3f, .2f, 0, .05f, 0}, .8f},
    {"CLAV", JK_POLY, E_KS, VO_CLOSE, 0, 0, 30, 0, .001f, 1, 1, .05f,
     {.95f, .5f, .9f, .08f, .1f, .35f, .4f, 0}, .7f},
    {"GLASS PAD", JK_POLY, E_SUB, VO_OPEN, 0, 0, 95, 0, .8f, 1.5f, .8f, 1.6f,
     {2, 1, 12.08f, .35f, 0, 0, 3000, .1f, .4f, 1.5f, .03f, 1}, .45f},
    {"SAW LEAD", JK_POLY | JK_LEAD, E_SUB, VO_CLOSE, 0, 0, 40, 0, .004f, .4f, .75f, .25f,
     {0, 0, .12f, .6f, 0, 0, 2200, .3f, 1.0f, .3f, .18f, 2}, .42f},
};
#define NPATCHES (sizeof(PATCHES) / sizeof(PATCHES[0]))
