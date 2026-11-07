/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the engine's interface. A jam is a key, a time signature, a tempo, two parts (A and B) of up to 12 chords
 * written as Nashville numbers, and the four players: drums, bass, chords and synth, each with its style and sound.
 * The engine (engine.c, its own -O2 unit) turns the jam into music in the audio interrupt: the sequencer, the
 * players, the synth voices, the drum sample voices, the effects and the MIDI clock.
 *
 * Everything here is shared between the app (main loop), the engine (audio interrupt) and the companion app (the
 * jam's bytes on the wire, companion/lib/jam.js). The jam's layout is fixed: little-endian, no padding, JT_JAM_BYTES. */
#pragma once
#include <stdint.h>

#define JT_SR 44100
#define JT_PPQN 24u                      /* MIDI clock ticks a quarter note */
#define JT_STEP_TICKS 6u                 /* a step is a sixteenth */
#define JT_NCHORD 12u                    /* chords a part */
#define JT_NAME 16u

/* ---------------------------------------------------------------- the jam */
/* A chord: a degree of the key's scale (1..7, as the Nashville number), maybe flat or sharp, a quality (AUTO: the
 * scale's own, coloured by the jam's COLOR), a length in beats and the bass note under it. */
typedef struct {
    uint8_t deg;                         /* bits 0-2: degree 0..6 (the number less one); bits 4-5: 0 natural, 1 flat, 2 sharp */
    uint8_t qual;                        /* JQ_* */
    uint8_t beats;                       /* 1..16 */
    uint8_t bass;                        /* JB_*: the bass note: the root, the chord's 3rd, 5th or 7th, the key's 1 */
} jt_chord_t;

typedef struct {
    uint8_t n;                           /* chords used, 1..12 */
    uint8_t energy;                      /* 0..4: how busy the drums are (2 as written) */
    uint8_t reps;                        /* CHAIN AUTO: times round before the other part (1..8) */
    uint8_t var;                         /* the drums' alternative pattern (0 / 1) */
    jt_chord_t c[JT_NCHORD];
} jt_part_t;                             /* 52 bytes */

enum { JP_DRUMS, JP_BASS, JP_CHORDS, JP_SYNTH, JP_N };

typedef struct {
    uint32_t magic;                      /* JT_JAM_MAGIC */
    char name[JT_NAME];                  /* A-Z 0-9 space - . ' (the screen's capitals) */
    uint8_t key;                         /* 0..11: C .. B */
    uint8_t mode;                        /* JM_* */
    uint8_t meter;                       /* JT_METER_* */
    uint8_t genre;                       /* the genre it was made from (the generator's, and a label) */
    uint16_t bpm10;                      /* tempo in tenths of a quarter note a minute, 400..2400 */
    uint8_t swing;                       /* 0..100: 50 is the groove's own */
    uint8_t human;                       /* 0..100: timing and velocity looseness */
    uint8_t fills;                       /* JF_*: a fill every 4 / 8 / 16 bars, at the end of each round, never */
    uint8_t push;                        /* 0/1: the chords and bass change an eighth early, on the push */
    uint8_t color;                       /* 0..3: AUTO chords as triads .. ninths */
    uint8_t chain;                       /* JC_*: how A and B follow each other */
    uint8_t kit, groove, bstyle, cstyle, sstyle, count;   /* count: count-in bars before PLAY (0..2) */
    uint8_t patch[3];                    /* the bass's, the chords' and the synth's sound */
    uint8_t mute;                        /* bit JP_*: muted */
    uint8_t level[JP_N];                 /* 0..100 */
    uint8_t tone[JP_N];                  /* 0..100: darker .. brighter (50: the sound's own) */
    uint8_t verb[JP_N];                  /* 0..100: reverb send */
    int8_t oct[JP_N];                    /* -2..2: the bass's, chords' and synth's octave; [JP_DRUMS]: the kit's tuning (-2..2 semitones) */
    uint8_t rev;                         /* 0..100: the reverb's level */
    uint8_t space;                       /* 0..100: the reverb's size, room .. hall */
    uint8_t tape;                        /* 0..100: wow, flutter, warmth, saturation */
    uint8_t glue;                        /* 0..100: the drum bus's compression */
    uint8_t width;                       /* 0..100: stereo width */
    uint8_t seed;                        /* the patterns' variation: hooks, ghost notes, fills */
    uint8_t switch_at;                   /* JS_*: a part change at the next bar or at the end of the round */
    uint8_t rsv[23];                     /* room for what comes (zero) */
    jt_part_t part[2];
} jt_jam_t;

#define JT_JAM_MAGIC 0x314D414Au         /* "JAM1" */
#define JT_JAM_BYTES 192u
_Static_assert(sizeof(jt_jam_t) == JT_JAM_BYTES, "the jam's bytes are fixed (the wire, the store)");

/* scales */
enum { JM_MAJOR, JM_MINOR, JM_DORIAN, JM_MIXO, JM_LYDIAN, JM_PHRYG, JM_HARM, JM_N };
/* qualities */
enum { JQ_AUTO, JQ_MAJ, JQ_MIN, JQ_DIM, JQ_AUG, JQ_SUS2, JQ_SUS4, JQ_MAJ7, JQ_MIN7, JQ_DOM7, JQ_M7B5, JQ_ADD9, JQ_MAJ9,
       JQ_MIN9, JQ_DOM9, JQ_SIX, JQ_MIN6, JQ_7SUS4, JQ_MIN11, JQ_DIM7, JQ_MMAJ7, JQ_MADD9, JQ_DOM13, JQ_69, JQ_POW,
       JQ_N };
/* bass under the chord */
enum { JB_ROOT, JB_THIRD, JB_FIFTH, JB_SEVENTH, JB_TONIC, JB_N };
/* time signatures */
enum { JT_METER_44, JT_METER_34, JT_METER_68, JT_METER_128, JT_METER_24, JT_METER_54, JT_METER_78, JT_METER_N };
/* fills */
enum { JF_4, JF_8, JF_16, JF_ROUND, JF_OFF, JF_N };
/* chains */
enum { JC_HAND, JC_AUTO, JC_A, JC_N };
/* part switch */
enum { JS_ROUND, JS_BAR, JS_N };

/* ---------------------------------------------------------------- the engine's tables (names for the screens) */
uint32_t jt_count(int what);             /* how many of: JT_T_* */
const char *jt_name(int what, uint32_t i);
enum { JT_T_KIT, JT_T_GROOVE, JT_T_BSTYLE, JT_T_CSTYLE, JT_T_SSTYLE, JT_T_PATCH, JT_T_MODE, JT_T_METER, JT_T_QUAL,
       JT_T_GENRE, JT_T_N };
uint32_t jt_patch_kind(uint32_t i);      /* JK_*: what a patch is for */
enum { JK_BASS = 1, JK_POLY = 2, JK_LEAD = 4 };   /* (a lead is a poly sound made for the keys) */
#define JT_USER_KIT (jt_count(JT_T_KIT) - 1u)   /* the last kit: the user's, its samples from the flash */

/* ---------------------------------------------------------------- theory (for the screens too) */
void jt_chord_text(const jt_jam_t *j, const jt_chord_t *c, char *num, char *name);   /* "6m7", "Am7" (16 each) */
void jt_key_text(const jt_jam_t *j, char *t);                                        /* "Eb MAJOR" */
const char *jt_note_name(int pc, int flats);
int jt_key_flats(const jt_jam_t *j);
int jt_chord_pcs(const jt_jam_t *j, const jt_chord_t *c, int *pcs);   /* its notes' pitch classes, the root first -> count */
int jt_scale_pc(const jt_jam_t *j, int i);   /* the key's scale degree i (0..6) as a pitch class */
int jt_bar_beats(int meter);
int jt_beat_steps(int meter, int beat);  /* steps in a beat (4, or 6 in 6/8, 12/8, the 7/8's last) */
int jt_part_beats(const jt_part_t *p);
/* a progression in text ("1maj7:4 6m7:4 2m7 5sus4:2 5:2", lengths in beats, 4 when left out) into a part; 0 = parsed.
 * The degrees: 1..7, b/# before; qualities as the screen writes them; /3 /5 /7 /1 the bass */
int jt_parse_prog(const char *s, jt_part_t *p, int beats_default);
int jt_qual_parse(const char *s, int *len);

/* ---------------------------------------------------------------- the generator */
void jt_generate(jt_jam_t *j, uint32_t genre, uint32_t seed, int what);   /* a new jam in a genre */
enum { JG_ALL, JG_PROG, JG_GROOVE, JG_SOUND };
void jt_jam_default(jt_jam_t *j);
void jt_jam_sanitize(jt_jam_t *j);       /* every field inside its range (a jam from the flash or the wire) */

/* ---------------------------------------------------------------- the user kit's samples */
/* EFB1 blocks (adpcm.h): 256 samples, 4-byte header, 128 bytes of nibbles; `bps` blocks a run, each run at sec[i]
 * (the flash's sectors: 29 blocks each; the factory's: one run) */
#define JT_BLK_SAMPLES 256u
#define JT_BLK_BYTES 132u
typedef struct {
    const uint8_t *const *sec;
    uint32_t frames;
    uint16_t rate;
    uint16_t bps;
    uint16_t fmt;                        /* 0 EFB1 ADPCM (the user kit's, always), 1 8-bit mu-law, 256 bytes a block */
    uint16_t rsv;
} jt_sample_t;
enum { JD_KICK, JD_SNARE, JD_RIM, JD_HAT, JD_OPEN, JD_RIDE, JD_CRASH, JD_TOM, JD_PERC, JD_N };
void jt_user_sample(uint32_t voice, const jt_sample_t *s);   /* 0 / frames 0: the slot empty (the STUDIO kit's) */
void jt_user_release(void);              /* the user kit let go (before the flash under it changes); waits nothing */
void jt_user_resume(void);               /* and taken back (after jt_user_sample for each voice) */
int jt_user_idle(void);                  /* 1: no voice reads the user kit (after jt_user_release, a render later) */
const char *jt_drum_name(uint32_t voice);

/* ---------------------------------------------------------------- the engine */
typedef struct {
    uint8_t playing;                     /* 1 playing (or counting in) */
    uint8_t counting;                    /* the count-in's beats left */
    uint8_t part;                        /* 0 A, 1 B */
    uint8_t queued;                      /* 1: a change of part is waiting */
    uint8_t chord;                       /* the chord playing (index in the part) */
    uint8_t beat;                        /* in the bar, from 0 */
    uint8_t ext;                         /* following a MIDI clock */
    uint8_t ending;                      /* the ending is being played */
    uint16_t bar;                        /* bars since PLAY */
    uint16_t round;                      /* rounds of the part */
    float bpm;                           /* the tempo now (the clock's when following) */
    uint32_t beat_seq;                   /* counts every beat (the screen's flash) */
    float cpu;                           /* the render's share of its time, 0..1, smoothed */
    uint16_t peak_l, peak_r;             /* the output's peaks since the last read (0..1000) */
    uint8_t level[JP_N];                 /* each part's meter, 0..100 */
    uint8_t voices;                      /* synth voices sounding */
    uint8_t chord_step, chord_len;       /* steps of the chord playing so far, and its length in steps (a ring) */
} jt_status_t;

typedef struct {                         /* the settings the engine reads (the app keeps them) */
    uint8_t clock_in;                    /* 0 auto (follow when a clock comes), 1 internal (ignore the clock) */
    uint8_t clock_out;                   /* 1: MIDI clock, START and STOP out over USB while playing on our own time */
    uint8_t startstop_in;                /* 1: MIDI START / STOP / CONTINUE play and stop */
    uint8_t midi_ch;                     /* 0 omni, 1..16: the channel notes and CCs come in on */
    uint8_t live_patch;                  /* the keys' sound: any patch (a lead is made for it) */
    uint8_t stop_mode;                   /* 0 at once, 1 at the end of the bar with a last hit */
    uint8_t out_level;                   /* 50..100 % */
    uint8_t metronome;                   /* 1: a click on every beat (practice) */
    uint8_t live_level;                  /* 0..100: the keys' level, their own (the band's mutes and levels leave it) */
    uint8_t live_mono;                   /* 1: one note at a time, sliding from note to note (a lead) */
    uint8_t live_verb;                   /* 0..100: the keys' reverb */
    uint8_t rsv;
} jt_settings_t;
#define JT_SETTINGS_BYTES 12u

void jt_init(void);                      /* once: the tables, the voices, the effects (the pool's buffers) */
void jt_render(int32_t *lr, uint32_t n, float gain);   /* the audio interrupt: n stereo frames, 24-bit */
void jt_panic(void);                     /* everything quiet now */
jt_jam_t *jt_jam(void);                  /* the jam playing: the app edits it in place (byte by byte: safe) */
jt_settings_t *jt_settings(void);
const jt_status_t *jt_status(void);
void jt_peak_reset(void);               /* the status's peaks start again (they hold the highest since the last reset) */
void jt_jam_changed(uint32_t what);      /* the app changed these (JX_*): the engine picks them up at the next step */
enum { JX_PROG = 1, JX_SOUND = 2, JX_STYLE = 4, JX_ALL = 7 };

/* transport (any context but the render's own; acted on at the next render) */
void jt_play(int on);                    /* 1 start (with the count-in), 0 stop (at once, or as stop_mode says) */
void jt_stop_now(void);
void jt_part_go(int part);               /* -1 the other part; 0 A; 1 B: at the next bar or round (switch_at) */
void jt_fill(void);                      /* a fill now, into the next bar */
void jt_tap(void);                       /* tap tempo (the app calls it at each tap) */
void jt_mute(uint32_t part, int on);
void jt_restart(void);                   /* from the top of the part at the next bar */
void jt_audition(int part, int chord);   /* stopped: the chord heard on the chords' sound (an editor's cursor) */
void jt_live(int on, uint32_t note, uint32_t vel);   /* the keys: the live voice (the synth's sound) */
void jt_live_all_off(void);
float jt_tap_bpm(void);                  /* the last taps' tempo, 0 none */

/* MIDI in: the render reads the platform's queue itself (the clock wants it there); everything that is not real-time
 * is handed on to the main loop through here */
int jt_midi_take(uint32_t *pkt);         /* main loop: 1 = a packet (USB-MIDI, as plat_midi_in gives it) */
uint32_t jt_clock_seen(void);            /* ms (plat_ms) of the last clock tick, 0 never */

/* the host's tests and the diagnostics */
typedef struct {
    uint32_t t_render, t_voices, t_fx;   /* peak ticks (plat_ticks24) */
    uint32_t xrun_guard;                 /* voices shed to keep inside the time */
} jt_meter_t;
const jt_meter_t *jt_meter(void);
void jt_meter_reset(void);
_Static_assert(sizeof(jt_settings_t) == JT_SETTINGS_BYTES, "the settings on the wire");
