/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK FM-1: the app. The panel and the screen, the jams (factory, saved, the session), MIDI from a loop pedal or
 * a footswitch, the user kit and the companion app's commands, around the band (dsp/engine.c). Part of fm1-base's unity
 * build (device/unity.c, host/plat_host.c): gfx.c is in scope (cv_*, fonts, RGB).
 *
 * The panel:
 *   PLAY         start / stop (STOP MODE: at once, or at the end of the bar with a last hit)
 *   SEQ          part A / B (at the end of the round, or the next bar); held: a fill now
 *   REC          tap tempo; held: back to the top of the part at the next bar
 *   ARP          GENERATE: a new jam in the genre (on GROOVE pages the grooves only, on SOUND the sounds, on EDIT the
 *                progressions); held: a whole new jam.   OCT- + OCT+ together: undo
 *   HOME EDIT ENV LFO FX GLO   pages (again: the group's next page). Hold HOME: what the page does
 *   SEL          which part EDIT and the PART page work on (A / B); held + a key: the key of the jam
 *   SAVE         save the jam (SELECT or PRESETS picks the slot, SAVE again to write)
 *   K1..K4       the page's four settings (blue, green, white, orange)
 *   SELECT       EDIT: the chord; elsewhere the tempo      ALGORITHM  the genre GENERATE draws from
 *   PRESETS      browse the jams: the one you stop on loads (playing: at the next bar)
 *   keys         play along on the synth's sound (OCT- / OCT+: their octave); EDIT: the chord's number
 *   FX held      first four white keys: mute drums, bass, chords, synth; black keys: fill, part, restart, break */
#include "fastmath.h"
#include "adpcm.h"
#ifdef PLAT_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

static volatile uint32_t rt_frames;
static volatile float side_ceil = 0.141f;    /* the headphone amplifier's side limiter (ECHOFIELD 0.2.7): -17 dBFS */
static volatile float mute_target = 1.0f, mute_gain = 1.0f;

#define NAME_N 12u
#define NUSER 32
static void text_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void name_clean(char *s, uint32_t n)  /* the screen's characters only, ended */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!c)
            break;
        if (c >= 'a' && c <= 'z')
            s[i] = (char)(c - 32);
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '.' || c == '_' ||
                   c == '\''))
            s[i] = ' ';
    }
    s[n - 1u] = 0;
}
static void settle(uint32_t ms)              /* let the audio interrupt run a while (the device; the host has no time) */
{
#ifndef PLAT_HOST
    uint32_t t0 = plat_ms();
    while (plat_ms() - t0 < ms)
        plat_yield();
#else
    (void)ms;
#endif
}
/* a store save with the sound faded around it (an erase stops the audio for tens of ms: a gap, never a click) */
static int store_quiet(uint32_t obj, const void *p, uint32_t n)
{
    int rc;
    mute_target = 0.0f;
    settle(25);
    rc = plat_store_save(obj, p, n);
    mute_target = 1.0f;
    return rc;
}

#include "kit.c"
#include "factory.c"
#include "proto.c"

/* ================================================================ state */
#define SET_MAGIC 0x31535450u            /* "PTS1" */
typedef struct {
    uint32_t magic;
    jt_settings_t eng;
    uint8_t genre, octave;               /* GENERATE's genre; the keys' octave (+2) */
    uint8_t rsv[14];
} settings_t;
static settings_t set;

#define SES_MAGIC 0x31534A54u            /* "TJS1" */
typedef struct {
    uint32_t magic;
    int16_t slot;                        /* the jam's place: 0..31 user, 0x80+ factory, -1 none */
    uint8_t dirty, rsv;
    jt_jam_t jam;
} session_t;

#define BANK_MAGIC 0x31424A54u           /* "TJB1" */
typedef struct {
    uint32_t magic;
    uint8_t used[16];
    jt_jam_t jam[16];
} jbank_t;
_Static_assert(sizeof(jbank_t) <= PLAT_STORE_MAX, "a bank of 16 jams fits one store object");
static jbank_t jbank[2];

static int16_t cur_slot = -1;            /* the jam playing came from */
static uint8_t jam_dirty;                /* changed since it was loaded or saved */
static uint8_t ses_dirty, set_dirty, bank_pending[2];
static uint32_t changed_at, stopped_at;
static jt_jam_t undo_jam;
static uint8_t undo_ok;
static uint8_t backup_hold;

/* ================================================================ the jams */
static int user_used(int slot, char *name)
{
    const jbank_t *b = &jbank[slot / 16];
    int i = slot % 16;
    if (slot < 0 || slot >= NUSER || !b->used[i]) {
        if (name)
            name[0] = 0;
        return 0;
    }
    if (name)
        text_cpy(name, b->jam[i].name, JT_NAME);
    return 1;
}

static int jam_get(int slot, jt_jam_t *out)
{
    if (slot == 0xFF) {
        *out = *jt_jam();
        return 1;
    }
    if (slot >= 0x80 && slot - 0x80 < NFACTORY) {
        factory_jam(slot - 0x80, out);
        return 1;
    }
    if (slot >= 0 && slot < NUSER) {
        if (!user_used(slot, 0)) {
            jt_jam_default(out);
            return 0;
        }
        *out = jbank[slot / 16].jam[slot % 16];
        return 1;
    }
    return -1;
}

static void mark_changed(void)
{
    jam_dirty = 1;
    ses_dirty = 1;
    changed_at = plat_ms();
}

/* a new jam playing; slot: where from (-1 none, -2 keep the slot but changed: a companion's edit). at_bar: playing,
 * wait for the next bar (app_frame) */
static jt_jam_t pending_jam;
static uint8_t pending_on;
static int16_t pending_slot;
static uint32_t pending_seq;
static uint8_t ui_redraw_all;

static void jam_apply(const jt_jam_t *j, int slot)
{
    jt_jam_t *e = jt_jam();
    uint8_t mute = e->mute;
    if (slot != -2) {
        undo_jam = *e;
        undo_ok = 1;
    }
    *e = *j;
    if (slot == -2)
        e->mute = mute;                  /* the companion's edit keeps what the panel muted */
    jt_jam_changed(JX_ALL);
    jt_mute(0, e->mute & 1u);            /* (the engine releases the muted parts' voices) */
    if (slot == -2) {
        jam_dirty = 1;
    } else {
        cur_slot = (int16_t)slot;
        jam_dirty = 0;
    }
    ses_dirty = 1;
    changed_at = plat_ms();
    ui_redraw_all = 1;
}

static void jam_set_current(const jt_jam_t *j, int slot, int at_bar)
{
    if (at_bar && jt_status()->playing && !jt_status()->counting) {
        pending_jam = *j;
        pending_slot = (int16_t)slot;
        pending_seq = jt_status()->beat_seq;
        pending_on = 1;
        return;
    }
    jam_apply(j, slot);
}

static void session_save(void)
{
    static session_t s;
    s.magic = SES_MAGIC;
    s.slot = cur_slot;
    s.dirty = jam_dirty;
    s.rsv = 0;
    s.jam = *jt_jam();
    if (plat_store_save(OBJ_SESSION, &s, sizeof s) == 0)
        ses_dirty = 0;
}

static void session_load(void)
{
    static session_t s;
    if (plat_store_load(OBJ_SESSION, &s, sizeof s) == (int)sizeof s && s.magic == SES_MAGIC &&
        s.jam.magic == JT_JAM_MAGIC) {
        jt_jam_sanitize(&s.jam);
        *jt_jam() = s.jam;
        cur_slot = s.slot >= -1 && (s.slot < NUSER || (s.slot >= 0x80 && s.slot - 0x80 < NFACTORY)) ? s.slot : -1;
        jam_dirty = s.dirty ? 1 : 0;
    } else {
        factory_jam(0, jt_jam());        /* the first start: the first factory jam */
        cur_slot = 0x80;
        jam_dirty = 0;
    }
    jt_jam_changed(JX_ALL);
}

static void banks_load(void)
{
    int b, i;
    for (b = 0; b < 2; b++) {
        if (plat_store_load(OBJ_JAMS0 + (uint32_t)b, &jbank[b], sizeof jbank[b]) != (int)sizeof jbank[b] ||
            jbank[b].magic != BANK_MAGIC) {
            uint8_t *p = (uint8_t *)&jbank[b];
            uint32_t k;
            for (k = 0; k < sizeof jbank[b]; k++)
                p[k] = 0;
            jbank[b].magic = BANK_MAGIC;
        }
        for (i = 0; i < 16; i++) {
            if (jbank[b].used[i] != 1u)
                jbank[b].used[i] = 0;
            if (jbank[b].used[i])
                jt_jam_sanitize(&jbank[b].jam[i]);
        }
    }
}

static int bank_write_now(int b)
{
    int rc = store_quiet(OBJ_JAMS0 + (uint32_t)b, &jbank[b], sizeof jbank[b]);
    if (!rc)
        bank_pending[b] = 0;
    return rc;
}
/* a bank written now, or once the band stops (a write while playing is a gap in the sound) */
static int bank_commit(int b)
{
    if (jt_status()->playing) {
        bank_pending[b] = 1;
        return 0;
    }
    return bank_write_now(b) ? E_STORE : E_OK;
}

static int jam_put_user(int slot, const jt_jam_t *j)
{
    if (slot < 0 || slot >= NUSER)
        return E_ARGS;
    jbank[slot / 16].jam[slot % 16] = *j;
    jbank[slot / 16].used[slot % 16] = 1;
    return bank_commit(slot / 16);
}

static int slot_save_current(int slot, const char *name)
{
    jt_jam_t *j = jt_jam();
    int rc;
    if (name && name[0]) {
        text_cpy(j->name, name, JT_NAME);
        name_clean(j->name, JT_NAME);
    }
    rc = jam_put_user(slot, j);
    if (rc == E_OK) {
        cur_slot = (int16_t)slot;
        jam_dirty = 0;
        ses_dirty = 1;
        changed_at = plat_ms();
    }
    return rc;
}
static int slot_delete(int slot)
{
    if (slot < 0 || slot >= NUSER)
        return E_ARGS;
    jbank[slot / 16].used[slot % 16] = 0;
    if (cur_slot == slot)
        cur_slot = -1;
    return bank_commit(slot / 16);
}
static int slot_rename(int slot, const char *name)
{
    if (!user_used(slot, 0) || !name[0])
        return E_ARGS;
    text_cpy(jbank[slot / 16].jam[slot % 16].name, name, JT_NAME);
    name_clean(jbank[slot / 16].jam[slot % 16].name, JT_NAME);
    if (cur_slot == slot)
        text_cpy(jt_jam()->name, jbank[slot / 16].jam[slot % 16].name, JT_NAME);
    return bank_commit(slot / 16);
}
static int cur_slot_wire(void) { return cur_slot < 0 ? 0xFF : cur_slot; }
static int session_dirty(void) { return jam_dirty; }
static uint32_t settings_genre(void) { return set.genre; }

/* ================================================================ settings */
static void settings_defaults(void)
{
    jt_settings_t *e = &set.eng;
    set.magic = SET_MAGIC;
    e->clock_in = 0;
    e->clock_out = 1;
    e->startstop_in = 1;
    e->midi_ch = 0;
    e->live_patch = P_SOLO;
    e->live_level = 75;
    e->live_mono = 1;
    e->live_verb = 25;
    e->stop_mode = 0;
    e->out_level = 90;
    e->metronome = 0;
    set.genre = 0;
    set.octave = 2;
}
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
        settings_defaults();
    if (set.eng.out_level < 50 || set.eng.out_level > 100)
        set.eng.out_level = 90;
    if (set.eng.midi_ch > 16)
        set.eng.midi_ch = 0;
    set.genre %= jt_count(JT_T_GENRE) + 1u;
    if (set.octave > 4)
        set.octave = 2;
    set.eng.live_patch %= jt_count(JT_T_PATCH);
    if (set.eng.live_level > 100)
        set.eng.live_level = 75;
    set.eng.live_mono &= 1u;
    if (set.eng.live_verb > 100)
        set.eng.live_verb = 25;
    *jt_settings() = set.eng;
}
static void settings_changed(void)
{
    *jt_settings() = set.eng;
    set_dirty = 1;
    changed_at = plat_ms();
}
static void settings_from_host(const uint8_t *b)
{
    uint32_t i;
    for (i = 0; i < sizeof(jt_settings_t); i++)
        ((uint8_t *)&set.eng)[i] = b[i];
    set.genre = (uint8_t)(b[sizeof(jt_settings_t)] % (jt_count(JT_T_GENRE) + 1u));
    if (set.eng.out_level < 50 || set.eng.out_level > 100)
        set.eng.out_level = 90;
    if (set.eng.midi_ch > 16)
        set.eng.midi_ch = 0;
    settings_changed();
}

/* ================================================================ sound plumbing */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float g;
    rt_frames += n;
    mute_gain += (mute_target - mute_gain) * 0.5f;
    if (fm_fabsf(mute_target - mute_gain) < 0.002f)
        mute_gain = mute_target;
    g = (float)master_q12 * (1.0f / 4096.0f) * (float)set.eng.out_level * 0.01f * mute_gain;
    jt_render(lr, n, g);
    if (side_ceil < 1.0f) {              /* the side limiter: instant down, back up over ~100 ms */
        static float sg = 1.0f;
        const float thr = side_ceil * 8300000.0f, rel = 1.0f / (0.1f * 44100.0f);
        uint32_t i;
        for (i = 0; i < n; i++) {
            float l = (float)lr[2u * i], r = (float)lr[2u * i + 1u], m = (l + r) * 0.5f, sd = (l - r) * 0.5f;
            float a = fm_fabsf(sd), need = a > thr ? thr / a : 1.0f;
            if (need < sg)
                sg = need;
            else
                sg += (1.0f - sg) * rel;
            sd *= sg;
            lr[2u * i] = (int32_t)(m + sd);
            lr[2u * i + 1u] = (int32_t)(m - sd);
        }
    }
}

void app_panic(void) { jt_panic(); }

#define NGEN ((int)jt_count(JT_T_GENRE))  /* set.genre == NGEN: ALL */
static const char *genre_name(int g) { return g >= NGEN ? "ALL GENRES" : jt_name(JT_T_GENRE, (uint32_t)g); }

/* ================================================================ the UI's state */
enum { PG_HOME, PG_EDIT, PG_SOUNDS, PG_MIX, PG_TONE, PG_SENDS, PG_OCT, PG_STYLES, PG_FEEL, PG_PART, PG_FLOW, PG_FX,
       PG_SONG, PG_MIDI, PG_SYSTEM, PG_KEYS, PG_KIT, PG_N };
enum { GR_HOME, GR_EDIT, GR_SOUND, GR_GROOVE, GR_FX, GR_GLO };

/* the knobs' settings */
enum { PR_NONE, PR_TEMPO, PR_KEY, PR_MODE, PR_METER, PR_ENERGY, PR_TAPE, PR_KIT, PR_PBASS, PR_PCHORD, PR_PSYNTH,
       PR_LVL0, PR_LVL1, PR_LVL2, PR_LVL3, PR_TONE0, PR_TONE1, PR_TONE2, PR_TONE3, PR_VERB0, PR_VERB1, PR_VERB2, PR_VERB3,
       PR_DTUNE, PR_OCT1, PR_OCT2, PR_OCT3, PR_GROOVE, PR_BSTYLE, PR_CSTYLE, PR_SSTYLE, PR_SWING, PR_HUMAN, PR_PUSH,
       PR_COLOR, PR_VAR, PR_REPS, PR_SEED, PR_FILLS, PR_CHAIN, PR_SWITCH, PR_COUNT, PR_REV, PR_SPACE, PR_GLUE, PR_WIDTH,
       PR_CLKIN, PR_CLKOUT, PR_MIDICH, PR_SSIN, PR_STOPMODE, PR_KEYSND, PR_METRO, PR_OUTLVL, PR_CDEG, PR_CQUAL,
       PR_CBEATS, PR_CBASS, PR_GENRE, PR_KLEVEL, PR_KMONO, PR_KVERB, PR_N };

typedef struct {
    const char *title;
    uint8_t group;
    uint8_t k[4];
    const char *help[3];
} page_t;
static const page_t PAGES[PG_N] = {
    {"PLAY", GR_HOME, {PR_TEMPO, PR_KEY, PR_ENERGY, PR_TAPE},
     {"PLAY STARTS, SEQ SWITCHES A/B", "REC TAPS TEMPO. ARP: NEW JAM", "FX HELD + KEYS: MUTES, FILLS"}},
    {"EDIT", GR_EDIT, {PR_CDEG, PR_CQUAL, PR_CBEATS, PR_CBASS},
     {"SELECT PICKS THE CHORD", "KEYS SET ITS NUMBER (C = 1)", "OCT+ ADDS, OCT- REMOVES. SEL: A/B"}},
    {"SOUNDS", GR_SOUND, {PR_KIT, PR_PBASS, PR_PCHORD, PR_PSYNTH},
     {"THE DRUM KIT AND THE SOUNDS", "OF THE BASS, CHORDS AND SYNTH", "ARP: NEW SOUNDS FOR THIS JAM"}},
    {"MIX", GR_SOUND, {PR_LVL0, PR_LVL1, PR_LVL2, PR_LVL3},
     {"DRUMS, BASS, CHORDS, SYNTH", "LEVELS. FX HELD + WHITE KEYS", "1-4 MUTE THEM"}},
    {"TONE", GR_SOUND, {PR_TONE0, PR_TONE1, PR_TONE2, PR_TONE3},
     {"DARKER OR BRIGHTER,", "EACH PART", ""}},
    {"REVERB SENDS", GR_SOUND, {PR_VERB0, PR_VERB1, PR_VERB2, PR_VERB3},
     {"HOW MUCH OF EACH PART", "GOES TO THE REVERB", "(FX: ITS LEVEL AND SIZE)"}},
    {"OCTAVES", GR_SOUND, {PR_DTUNE, PR_OCT1, PR_OCT2, PR_OCT3},
     {"THE KIT'S TUNING, AND THE", "OCTAVE OF THE BASS, CHORDS", "AND SYNTH"}},
    {"STYLES", GR_GROOVE, {PR_GROOVE, PR_BSTYLE, PR_CSTYLE, PR_SSTYLE},
     {"HOW EACH PLAYER PLAYS:", "THE GROOVE, THE BASS LINE,", "THE COMPING, THE SYNTH LINE"}},
    {"FEEL", GR_GROOVE, {PR_SWING, PR_HUMAN, PR_PUSH, PR_COLOR},
     {"SWING (50: THE GROOVE'S OWN),", "LOOSENESS, CHANGES ON THE PUSH,", "CHORD COLOUR: TRIADS..9THS"}},
    {"PART", GR_GROOVE, {PR_ENERGY, PR_VAR, PR_REPS, PR_SEED},
     {"FOR THE PART SEL PICKS:", "ENERGY, THE OTHER BEAT, ROUNDS", "BEFORE AUTO CHAIN, VARIATION"}},
    {"FLOW", GR_GROOVE, {PR_FILLS, PR_CHAIN, PR_SWITCH, PR_COUNT},
     {"FILLS, HOW A AND B FOLLOW,", "WHEN A CHANGE OF PART COMES,", "BARS COUNTED IN"}},
    {"EFFECTS", GR_FX, {PR_REV, PR_SPACE, PR_TAPE, PR_GLUE},
     {"REVERB AND ITS SIZE, TAPE", "(WOW, WARMTH, SATURATION),", "GLUE ON THE DRUMS"}},
    {"SONG", GR_GLO, {PR_TEMPO, PR_KEY, PR_MODE, PR_METER},
     {"TEMPO, KEY, SCALE, TIME.", "THE NUMBERS STAY:", "CHANGE THE KEY, THE SONG MOVES"}},
    {"MIDI", GR_GLO, {PR_CLKIN, PR_CLKOUT, PR_MIDICH, PR_SSIN},
     {"CLOCK IN (AUTO FOLLOWS ONE)", "CLOCK OUT OVER USB, CHANNEL,", "START/STOP FROM MIDI"}},
    {"SYSTEM", GR_GLO, {PR_STOPMODE, PR_GENRE, PR_METRO, PR_OUTLVL},
     {"STOP AT ONCE OR AT THE BAR,", "THE GENRE, A CLICK,", "THE OUTPUT'S CEILING"}},
    {"KEYS", GR_GLO, {PR_KEYSND, PR_KLEVEL, PR_KMONO, PR_KVERB},
     {"WHAT THE KEYS PLAY, THEIR OWN", "LEVEL; MONO SLIDES NOTE TO NOTE.", "SEL HELD + SELECT: THE SOUND"}},
    {"USER KIT", GR_GLO, {PR_KIT, PR_DTUNE, PR_GLUE, PR_WIDTH},
     {"YOUR OWN SAMPLES, ONE A DRUM,", "FROM THE COMPANION APP.", "KIT: USER ON THE SOUNDS PAGE"}},
};

static struct {
    int page, last_page[6];
    int edit_part, cursor;
    uint32_t btn, keys;
    int octave;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t d_head, d_field, d_knobs;
    uint32_t sel_t, seq_t, rec_t, arp_t, home_t, save_t;
    uint8_t sel_used, seq_used, rec_used, arp_used, home_used, fx_used;
    int save_mode, save_slot;
    int browse, browse_on;
    uint32_t browse_t;
    int genre_on, genre_sel;
    uint32_t genre_t;
    uint32_t touched[4];
    uint32_t last_beat_seq;
    uint8_t live_held[NKEYS];
    uint8_t live_note[NKEYS];
    int upload_k, upload_n;
    uint8_t cc_last[128];
} ui;

static const uint16_t KNOB_RGB[4] = {RGB(70, 150, 255), RGB(90, 220, 120), RGB(235, 235, 240), RGB(255, 140, 50)};
static const uint16_t PART_RGB[JP_N] = {RGB(255, 120, 80), RGB(80, 170, 255), RGB(255, 196, 90), RGB(190, 135, 255)};
static const char *const PART_NAME[JP_N] = {"DRUMS", "BASS", "CHORDS", "SYNTH"};

void app_say(const char *a, const char *b)
{
    text_cpy(ui.msg[0], a ? a : "", 24);
    text_cpy(ui.msg[1], b ? b : "", 24);
    ui.msg_until = plat_ms() + 1300u;
    ui.d_field = 1;
    if (ann_on()) {
        char t[52];
        t[0] = 0;
        text_cpy(t, ui.msg[0], 24);
        if (ui.msg[1][0]) {
            uint32_t k = 0;
            while (t[k])
                k++;
            t[k++] = ' ';
            text_cpy(t + k, ui.msg[1], 24);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static jt_part_t *edit_part(void) { return &jt_jam()->part[ui.edit_part & 1]; }

/* ================================================================ the knobs' settings */
static const char *const ONOFF[2] = {"OFF", "ON"};
static const char *const FILL_NAME[JF_N] = {"EVERY 4", "EVERY 8", "EVERY 16", "ROUND END", "NONE"};
static const char *const CHAIN_NAME[JC_N] = {"BY HAND", "AUTO A/B", "A ONLY"};
static const char *const SWITCH_NAME[JS_N] = {"ROUND END", "NEXT BAR"};
static const char *const BASS_NAME[JB_N] = {"ROOT", "/3RD", "/5TH", "/7TH", "/KEY 1"};
static const char *const PR_LABEL[PR_N] = {"", "TEMPO", "KEY", "SCALE", "TIME", "ENERGY", "TAPE", "KIT", "BASS", "CHORDS",
                                           "SYNTH", "DRUMS", "BASS", "CHORDS", "SYNTH", "DRUMS", "BASS", "CHORDS", "SYNTH",
                                           "DRUMS", "BASS", "CHORDS", "SYNTH", "KIT TUNE", "BASS OCT", "CHORD OCT",
                                           "SYNTH OCT", "GROOVE", "BASS LINE", "CHORDS", "SYNTH LINE", "SWING", "HUMAN",
                                           "PUSH", "COLOR", "ALT BEAT", "ROUNDS", "VARIATION", "FILLS", "CHAIN", "SWITCH",
                                           "COUNT-IN", "REVERB", "SPACE", "GLUE", "WIDTH", "CLOCK IN", "CLOCK OUT",
                                           "CHANNEL", "START/STOP", "STOP", "KEYS SOUND", "CLICK", "OUTPUT", "CHORD",
                                           "TYPE", "BEATS", "BASS NOTE", "GENRE", "LEVEL", "MONO", "REVERB"};

/* the 12 numbers a chord may have, as the scale spells them: its own degrees plain, the others flat in a major scale and
 * sharp in a minor one */
static int deg_of_semi(int semi, int mode)
{
    static const uint8_t SC[JM_N][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
                                        {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 10},
                                        {0, 2, 3, 5, 7, 8, 11}};
    const uint8_t *s = SC[mode % JM_N];
    int i, minor = s[2] == 3;
    semi = ((semi % 12) + 12) % 12;
    for (i = 0; i < 7; i++)
        if (s[i] == semi)
            return i;
    if (minor) {
        for (i = 0; i < 7; i++)
            if ((s[i] + 1) % 12 == semi)
                return i | 2 << 4;       /* sharp */
    }
    for (i = 0; i < 7; i++)
        if ((s[i] + 11) % 12 == semi)
            return i | 1 << 4;           /* flat */
    for (i = 0; i < 7; i++)
        if ((s[i] + 1) % 12 == semi)
            return i | 2 << 4;
    return 0;
}
static int semi_of_deg(int deg, int mode)
{
    static const uint8_t SC[JM_N][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
                                        {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 10},
                                        {0, 2, 3, 5, 7, 8, 11}};
    int a = (deg >> 4) & 3;
    return (SC[mode % JM_N][(deg & 7) > 6 ? 6 : (deg & 7)] + (a == 1 ? -1 : a == 2 ? 1 : 0) + 12) % 12;
}

static int32_t accel(int e, uint32_t k)  /* faster turns go further */
{
    static uint32_t last[8];
    uint32_t now = plat_ms(), dt = now - last[k & 7u];
    last[k & 7u] = now;
    if (dt < 40u)
        return e * 4;
    if (dt < 90u)
        return e * 2;
    return e;
}

static uint8_t *pct_ptr(int id)
{
    jt_jam_t *j = jt_jam();
    if (id >= PR_LVL0 && id <= PR_LVL3)
        return &j->level[id - PR_LVL0];
    if (id >= PR_TONE0 && id <= PR_TONE3)
        return &j->tone[id - PR_TONE0];
    if (id >= PR_VERB0 && id <= PR_VERB3)
        return &j->verb[id - PR_VERB0];
    switch (id) {
    case PR_TAPE: return &j->tape;
    case PR_SWING: return &j->swing;
    case PR_HUMAN: return &j->human;
    case PR_REV: return &j->rev;
    case PR_SPACE: return &j->space;
    case PR_GLUE: return &j->glue;
    case PR_WIDTH: return &j->width;
    }
    return 0;
}

static uint32_t patch_step(uint32_t cur, int d, uint32_t kind)   /* the next sound of a kind */
{
    uint32_t n = jt_count(JT_T_PATCH), i = cur % n, k;
    for (k = 0; k < n; k++) {
        i = (i + n + (uint32_t)(d > 0 ? 1 : -1)) % n;
        if (jt_patch_kind(i) & kind)
            return i;
    }
    return cur;
}

static float param_norm(int id)          /* 0..1, for the dial */
{
    jt_jam_t *j = jt_jam();
    jt_part_t *p = edit_part();
    uint8_t *pp = pct_ptr(id);
    jt_chord_t *c = &p->c[ui.cursor < p->n ? ui.cursor : 0];
    if (pp)
        return (float)*pp * 0.01f;
    switch (id) {
    case PR_TEMPO: return ((float)j->bpm10 - 400.0f) / 2000.0f;
    case PR_KEY: return (float)j->key / 11.0f;
    case PR_MODE: return (float)j->mode / (float)(JM_N - 1);
    case PR_METER: return (float)j->meter / (float)(JT_METER_N - 1);
    case PR_ENERGY: return (float)p->energy / 4.0f;
    case PR_KIT: return (float)j->kit / (float)(jt_count(JT_T_KIT) - 1u);
    case PR_PBASS: case PR_PCHORD: case PR_PSYNTH:
        return (float)j->patch[id - PR_PBASS] / (float)(jt_count(JT_T_PATCH) - 1u);
    case PR_DTUNE: case PR_OCT1: case PR_OCT2: case PR_OCT3:
        return ((float)j->oct[id - PR_DTUNE] + 2.0f) / 4.0f;
    case PR_GROOVE: return (float)j->groove / (float)(jt_count(JT_T_GROOVE) - 1u);
    case PR_BSTYLE: return (float)j->bstyle / (float)(jt_count(JT_T_BSTYLE) - 1u);
    case PR_CSTYLE: return (float)j->cstyle / (float)(jt_count(JT_T_CSTYLE) - 1u);
    case PR_SSTYLE: return (float)j->sstyle / (float)(jt_count(JT_T_SSTYLE) - 1u);
    case PR_PUSH: return (float)j->push;
    case PR_COLOR: return (float)j->color / 3.0f;
    case PR_VAR: return (float)p->var;
    case PR_REPS: return (float)(p->reps - 1) / 7.0f;
    case PR_SEED: return (float)j->seed / 255.0f;
    case PR_FILLS: return (float)j->fills / (float)(JF_N - 1);
    case PR_CHAIN: return (float)j->chain / (float)(JC_N - 1);
    case PR_SWITCH: return (float)j->switch_at;
    case PR_COUNT: return (float)j->count / 2.0f;
    case PR_CLKIN: return set.eng.clock_in ? 1.0f : 0.0f;
    case PR_CLKOUT: return (float)set.eng.clock_out;
    case PR_MIDICH: return (float)set.eng.midi_ch / 16.0f;
    case PR_SSIN: return (float)set.eng.startstop_in;
    case PR_STOPMODE: return (float)set.eng.stop_mode;
    case PR_KEYSND: return (float)set.eng.live_patch / (float)(jt_count(JT_T_PATCH) - 1u);
    case PR_METRO: return (float)set.eng.metronome;
    case PR_OUTLVL: return ((float)set.eng.out_level - 50.0f) / 50.0f;
    case PR_KLEVEL: return (float)set.eng.live_level * 0.01f;
    case PR_KMONO: return (float)set.eng.live_mono;
    case PR_KVERB: return (float)set.eng.live_verb * 0.01f;
    case PR_CDEG: return (float)((c->deg & 7) > 6 ? 6 : (c->deg & 7)) / 6.0f;
    case PR_CQUAL: return (float)c->qual / (float)(JQ_N - 1);
    case PR_CBEATS: return (float)(c->beats - 1) / 15.0f;
    case PR_CBASS: return (float)c->bass / (float)(JB_N - 1);
    case PR_GENRE: return (float)set.genre / (float)jt_count(JT_T_GENRE);
    }
    return 0;
}

static void param_text(int id, char *t)
{
    jt_jam_t *j = jt_jam();
    jt_part_t *p = edit_part();
    uint8_t *pp = pct_ptr(id);
    jt_chord_t *c = &p->c[ui.cursor < p->n ? ui.cursor : 0];
    t[0] = 0;
    if (pp) {
        int_text(*pp, t);
        return;
    }
    switch (id) {
    case PR_TEMPO: {
        char d[4];
        int_text(j->bpm10 / 10, t);
        if (j->bpm10 % 10) {
            text_cat(t, ".");
            int_text(j->bpm10 % 10, d);
            text_cat(t, d);
        }
        break;
    }
    case PR_KEY: text_cpy(t, jt_note_name(j->key, jt_key_flats(j)), 8); break;
    case PR_MODE: text_cpy(t, jt_name(JT_T_MODE, j->mode), 12); break;
    case PR_METER: text_cpy(t, jt_name(JT_T_METER, j->meter), 8); break;
    case PR_ENERGY: int_text(p->energy, t); break;
    case PR_KIT: text_cpy(t, jt_name(JT_T_KIT, j->kit), 14); break;
    case PR_PBASS: case PR_PCHORD: case PR_PSYNTH: text_cpy(t, jt_name(JT_T_PATCH, j->patch[id - PR_PBASS]), 14); break;
    case PR_DTUNE: case PR_OCT1: case PR_OCT2: case PR_OCT3: {
        int v = j->oct[id - PR_DTUNE];
        if (v > 0)
            text_cpy(t, "+", 2);
        int_text(v, t + (v > 0));
        break;
    }
    case PR_GROOVE: text_cpy(t, jt_name(JT_T_GROOVE, j->groove), 14); break;
    case PR_BSTYLE: text_cpy(t, jt_name(JT_T_BSTYLE, j->bstyle), 14); break;
    case PR_CSTYLE: text_cpy(t, jt_name(JT_T_CSTYLE, j->cstyle), 14); break;
    case PR_SSTYLE: text_cpy(t, jt_name(JT_T_SSTYLE, j->sstyle), 14); break;
    case PR_PUSH: text_cpy(t, ONOFF[j->push & 1], 4); break;
    case PR_COLOR: {
        static const char *const C[4] = {"TRIADS", "SOFT 7THS", "7THS", "9THS"};
        text_cpy(t, C[j->color & 3], 12);
        break;
    }
    case PR_VAR: text_cpy(t, ONOFF[p->var & 1], 4); break;
    case PR_REPS: int_text(p->reps, t); break;
    case PR_SEED: int_text(j->seed, t); break;
    case PR_FILLS: text_cpy(t, FILL_NAME[j->fills % JF_N], 12); break;
    case PR_CHAIN: text_cpy(t, CHAIN_NAME[j->chain % JC_N], 12); break;
    case PR_SWITCH: text_cpy(t, SWITCH_NAME[j->switch_at % JS_N], 12); break;
    case PR_COUNT: if (!j->count) text_cpy(t, "OFF", 4); else { int_text(j->count, t); text_cat(t, j->count > 1 ? " BARS" : " BAR"); } break;
    case PR_CLKIN: text_cpy(t, set.eng.clock_in ? "IGNORE" : "FOLLOW", 8); break;
    case PR_CLKOUT: text_cpy(t, ONOFF[set.eng.clock_out & 1], 4); break;
    case PR_MIDICH: if (!set.eng.midi_ch) text_cpy(t, "ALL", 4); else int_text(set.eng.midi_ch, t); break;
    case PR_SSIN: text_cpy(t, ONOFF[set.eng.startstop_in & 1], 4); break;
    case PR_STOPMODE: text_cpy(t, set.eng.stop_mode ? "BAR END" : "AT ONCE", 8); break;
    case PR_KEYSND: text_cpy(t, jt_name(JT_T_PATCH, set.eng.live_patch), 14); break;
    case PR_METRO: text_cpy(t, ONOFF[set.eng.metronome & 1], 4); break;
    case PR_OUTLVL: int_text(set.eng.out_level, t); text_cat(t, "%"); break;
    case PR_KLEVEL: int_text(set.eng.live_level, t); break;
    case PR_KMONO: text_cpy(t, set.eng.live_mono ? "MONO" : "POLY", 6); break;
    case PR_KVERB: int_text(set.eng.live_verb, t); break;
    case PR_CDEG: jt_chord_text(j, c, 0, t); break;
    case PR_CQUAL: text_cpy(t, jt_name(JT_T_QUAL, c->qual), 8); break;
    case PR_CBEATS: int_text(c->beats, t); break;
    case PR_CBASS: text_cpy(t, BASS_NAME[c->bass % JB_N], 8); break;
    case PR_GENRE: text_cpy(t, genre_name(set.genre), 14); break;
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { return ((v % n) + n) % n; }

static void audition_cursor(void)
{
    if (!jt_status()->playing)
        jt_audition(ui.edit_part, ui.cursor);
}

static void param_turn(int id, int e, uint32_t knob)
{
    jt_jam_t *j = jt_jam();
    jt_part_t *p = edit_part();
    uint8_t *pp = pct_ptr(id);
    jt_chord_t *c = &p->c[ui.cursor < p->n ? ui.cursor : 0];
    int d = (int)accel(e, knob), settings = 0;
    uint32_t what = JX_SOUND;
    if (pp) {
        *pp = (uint8_t)clampi(*pp + d, 0, 100);
    } else
        switch (id) {
        case PR_TEMPO: j->bpm10 = (uint16_t)clampi(j->bpm10 + d * 10, 400, 2400); break;
        case PR_KEY: j->key = (uint8_t)wrapi(j->key + e, 12); what = JX_PROG; break;
        case PR_MODE: j->mode = (uint8_t)wrapi(j->mode + e, JM_N); what = JX_PROG; break;
        case PR_METER: j->meter = (uint8_t)wrapi(j->meter + e, JT_METER_N); what = JX_ALL; break;
        case PR_ENERGY: p->energy = (uint8_t)clampi(p->energy + e, 0, 4); break;
        case PR_KIT: j->kit = (uint8_t)wrapi(j->kit + e, (int)jt_count(JT_T_KIT)); break;
        case PR_PBASS: j->patch[0] = (uint8_t)patch_step(j->patch[0], e, JK_BASS); break;
        case PR_PCHORD: j->patch[1] = (uint8_t)patch_step(j->patch[1], e, JK_POLY); break;
        case PR_PSYNTH: j->patch[2] = (uint8_t)patch_step(j->patch[2], e, JK_POLY); break;
        case PR_DTUNE: case PR_OCT1: case PR_OCT2: case PR_OCT3:
            j->oct[id - PR_DTUNE] = (int8_t)clampi(j->oct[id - PR_DTUNE] + e, -2, 2);
            break;
        case PR_GROOVE: j->groove = (uint8_t)wrapi(j->groove + e, (int)jt_count(JT_T_GROOVE)); what = JX_STYLE; break;
        case PR_BSTYLE: j->bstyle = (uint8_t)wrapi(j->bstyle + e, (int)jt_count(JT_T_BSTYLE)); what = JX_STYLE; break;
        case PR_CSTYLE: j->cstyle = (uint8_t)wrapi(j->cstyle + e, (int)jt_count(JT_T_CSTYLE)); what = JX_STYLE; break;
        case PR_SSTYLE: j->sstyle = (uint8_t)wrapi(j->sstyle + e, (int)jt_count(JT_T_SSTYLE)); what = JX_STYLE; break;
        case PR_PUSH: j->push = e > 0; break;
        case PR_COLOR: j->color = (uint8_t)clampi(j->color + e, 0, 3); what = JX_PROG; break;
        case PR_VAR: p->var = e > 0; break;
        case PR_REPS: p->reps = (uint8_t)clampi(p->reps + e, 1, 8); break;
        case PR_SEED: j->seed = (uint8_t)(j->seed + d); break;
        case PR_FILLS: j->fills = (uint8_t)wrapi(j->fills + e, JF_N); break;
        case PR_CHAIN: j->chain = (uint8_t)wrapi(j->chain + e, JC_N); break;
        case PR_SWITCH: j->switch_at = (uint8_t)wrapi(j->switch_at + e, JS_N); break;
        case PR_COUNT: j->count = (uint8_t)clampi(j->count + e, 0, 2); break;
        case PR_CLKIN: set.eng.clock_in = e < 0 ? 0 : 1; settings = 1; break;
        case PR_CLKOUT: set.eng.clock_out = e > 0; settings = 1; break;
        case PR_MIDICH: set.eng.midi_ch = (uint8_t)clampi(set.eng.midi_ch + e, 0, 16); settings = 1; break;
        case PR_SSIN: set.eng.startstop_in = e > 0; settings = 1; break;
        case PR_STOPMODE: set.eng.stop_mode = e > 0; settings = 1; break;
        case PR_KEYSND: set.eng.live_patch = (uint8_t)patch_step(set.eng.live_patch, e, JK_POLY); settings = 1; break;
        case PR_METRO: set.eng.metronome = e > 0; settings = 1; break;
        case PR_KLEVEL: set.eng.live_level = (uint8_t)clampi(set.eng.live_level + d, 0, 100); settings = 1; break;
        case PR_KMONO: set.eng.live_mono = e > 0; settings = 1; break;
        case PR_KVERB: set.eng.live_verb = (uint8_t)clampi(set.eng.live_verb + d, 0, 100); settings = 1; break;
        case PR_OUTLVL: set.eng.out_level = (uint8_t)clampi(set.eng.out_level + d, 50, 100); settings = 1; break;
        case PR_GENRE: set.genre = (uint8_t)wrapi(set.genre + e, (int)jt_count(JT_T_GENRE) + 1); settings = 1; break;
        case PR_CDEG: {                  /* the key's own chords, in turn (a borrowed one: from the keys) */
            int dg = (c->deg & 7) > 6 ? 0 : (c->deg & 7);
            if ((c->deg >> 4) & 3)
                dg = e > 0 ? dg : dg + 1;   /* (from a borrowed chord: the nearest of the key's) */
            c->deg = (uint8_t)wrapi(dg + e, 7);
            c->qual = JQ_AUTO;
            what = JX_PROG;
            audition_cursor();
            break;
        }
        case PR_CQUAL: c->qual = (uint8_t)wrapi(c->qual + e, JQ_N); what = JX_PROG; audition_cursor(); break;
        case PR_CBEATS: c->beats = (uint8_t)clampi(c->beats + e, 1, 16); what = JX_PROG; break;
        case PR_CBASS: c->bass = (uint8_t)wrapi(c->bass + e, JB_N); what = JX_PROG; audition_cursor(); break;
        }
    if (settings)
        settings_changed();
    else {
        jt_jam_changed(what);
        mark_changed();
        notify(what);
    }
    ui.d_knobs = 1;
    ui.d_field = 1;
    ui.d_head = 1;
}

/* ================================================================ actions */
static void page_go(int page)
{
    ui.page = page;
    ui.last_page[PAGES[page].group] = page;
    ui.save_mode = 0;
    ui.d_head = ui.d_field = ui.d_knobs = 1;
    if (ann_on()) {
        char t[64] = "";
        text_cat(t, PAGES[page].title);
        ann_say(t, 0, ANN_NOW);
    }
}

static void group_button(int g)
{
    static const uint8_t ORDER[][6] = {{PG_HOME}, {PG_EDIT}, {PG_SOUNDS, PG_MIX, PG_TONE, PG_SENDS, PG_OCT},
                                       {PG_STYLES, PG_FEEL, PG_PART, PG_FLOW}, {PG_FX}, {PG_SONG, PG_MIDI, PG_SYSTEM, PG_KEYS, PG_KIT}};
    static const uint8_t NPG[6] = {1, 1, 5, 4, 1, 5};
    int i;
    if (PAGES[ui.page].group != g) {     /* a new group: the page last seen in it */
        int p = ui.last_page[g];
        page_go(PAGES[p].group == g ? p : ORDER[g][0]);
        return;
    }
    for (i = 0; i < NPG[g]; i++)
        if (ORDER[g][i] == ui.page)
            break;
    page_go(ORDER[g][(i + 1) % NPG[g]]);
}

static void generate(int what)
{
    jt_jam_t j = *jt_jam();
    static uint32_t salt;
    uint32_t seed = plat_ms() * 2654435761u + plat_ticks24() + ++salt * 40503u;
    int g = set.genre < jt_count(JT_T_GENRE) ? set.genre : jt_jam()->genre;
    jt_generate(&j, (uint32_t)g, seed, what);
    if (what != 0) {                     /* a part only: the name and the rest stay */
        j.mute = jt_jam()->mute;
    }
    jam_apply(&j, -1);
    jam_dirty = 1;
    if (what == JG_ALL)
        app_say(j.name, jt_name(JT_T_GENRE, (uint32_t)g));
    else
        app_say("GENERATED", what == JG_PROG ? "PROGRESSIONS" : what == JG_GROOVE ? "GROOVES" : "SOUNDS");
    notify(JX_ALL);
}

static void undo_do(void)
{
    jt_jam_t cur = *jt_jam();
    if (!undo_ok) {
        app_say("UNDO", "NOTHING YET");
        return;
    }
    jam_apply(&undo_jam, -1);
    undo_jam = cur;
    jam_dirty = 1;
    app_say("UNDO", jt_jam()->name);
    notify(JX_ALL);
}

static void mute_toggle(uint32_t part)
{
    int on = !(jt_jam()->mute >> part & 1u);
    jt_mute(part, on);
    app_say(PART_NAME[part], on ? "MUTED" : "ON");
    ui.d_field = 1;
}

static void play_toggle(void)
{
    if (jt_status()->playing) {
        jt_play(0);
        if (!set.eng.stop_mode)
            app_say("STOP", "");
    } else
        jt_play(1);
}

static void transport(uint32_t op, uint32_t a, uint32_t b)
{
    switch (op) {
    case 0: jt_play(1); break;
    case 1: jt_play(0); break;
    case 2: jt_stop_now(); break;
    case 3: jt_part_go(a >= 2u ? -1 : (int)a); break;
    case 4: jt_fill(); break;
    case 5: jt_tap(); ui.d_knobs = ui.d_head = 1; break;
    case 6: if (a < JP_N) jt_mute(a, b != 0); ui.d_field = 1; break;
    case 7: jt_restart(); break;
    case 8: jt_audition((int)a & 1, (int)b); break;
    case 9: set.genre = (uint8_t)(a % (jt_count(JT_T_GENRE) + 1u)); generate(b <= 3u ? (int)b : 0); break;
    case 10: {
        jt_jam_t j;
        if (jam_get((int)a, &j) > 0)
            jam_set_current(&j, (int)a, 1);
        break;
    }
    case 11:
        if (b)
            jt_live(1, a & 0x7Fu, b & 0x7Fu);
        else
            jt_live(0, a & 0x7Fu, 0);
        break;
    case 12: undo_do(); break;
    }
}

/* the jams by genre. ALGORITHM picks a genre (or ALL); PRESETS then browses that genre's jams: yours tagged with it,
 * then the factory's, then NEW: a jam GENERATE makes in it. Codes: 0..31 a user slot, 0x80 + i a factory jam, 0xFE new */
#define CODE_NEW 0xFE
static int abs_count(void) { return NUSER + NFACTORY; }          /* (program changes: every jam, in order) */
static int abs_slot(int i) { return i < NUSER ? i : 0x80 + (i - NUSER); }
static int code_genre(int code)
{
    if (code >= 0 && code < NUSER)
        return jbank[code / 16].jam[code % 16].genre;
    if (code >= 0x80 && code < 0x80 + NFACTORY)
        return FACTORY[code - 0x80].genre;
    return -1;
}
static int browse_list(int *codes)        /* the list for the genre -> its length */
{
    int i, n = 0, g = set.genre;
    for (i = 0; i < NUSER; i++)
        if (user_used(i, 0) && (g >= NGEN || code_genre(i) == g))
            codes[n++] = i;
    for (i = 0; i < NFACTORY; i++)
        if (g >= NGEN || FACTORY[i].genre == g)
            codes[n++] = 0x80 + i;
    if (g < NGEN)
        codes[n++] = CODE_NEW;
    return n;
}
static void browse_label(int code, char *t)
{
    char nm[JT_NAME];
    t[0] = 0;
    if (code == CODE_NEW) {
        text_cat(t, "+ NEW ");
        text_cat(t, genre_name(set.genre));
        text_cat(t, " JAM");
    } else if (code < NUSER) {
        t[0] = 'U';
        t[1] = (char)('0' + (code + 1) / 10);
        t[2] = (char)('0' + (code + 1) % 10);
        t[3] = ' ';
        t[4] = 0;
        text_cat(t, user_used(code, nm) ? nm : "---");
    } else {
        int f = code - 0x80 + 1;
        t[0] = 'F';
        t[1] = (char)('0' + f / 10);
        t[2] = (char)('0' + f % 10);
        t[3] = ' ';
        t[4] = 0;
        text_cat(t, FACTORY[code - 0x80].name);
    }
}
static int browse_codes[NUSER + 64], browse_n;
static void browse_turn(int e)
{
    int i;
    browse_n = browse_list(browse_codes);
    if (!ui.browse_on) {                 /* from the jam playing, when it is in the list */
        ui.browse = 0;
        for (i = 0; i < browse_n; i++)
            if (browse_codes[i] == cur_slot)
                ui.browse = i;
        if (browse_codes[ui.browse] != cur_slot)
            e = e > 0 ? 0 : -1;          /* (not in it: the first one, or the last) */
    }
    ui.browse = wrapi(ui.browse + (e > 0 ? 1 : e < 0 ? -1 : 0), browse_n);
    ui.browse_on = 1;
    ui.browse_t = plat_ms();
    ui.d_field = 1;
}
static void browse_load(void)
{
    jt_jam_t j;
    int code = browse_codes[wrapi(ui.browse, browse_n ? browse_n : 1)];
    ui.browse_on = 0;
    ui.d_field = 1;
    if (code == CODE_NEW) {
        generate(JG_ALL);
        return;
    }
    if (jam_get(code, &j) > 0) {
        jam_set_current(&j, code, 1);
        app_say(j.name, jt_status()->playing ? "AT THE NEXT BAR" : genre_name(j.genre));
        notify(JX_ALL);
    }
}
/* ALGORITHM: a genre, looked at for 2 s (OCT+ now, OCT- never mind); then its first jam plays (at the next bar) */
static void genre_turn(int e)
{
    if (!ui.genre_on)
        ui.genre_sel = set.genre;
    ui.genre_sel = wrapi(ui.genre_sel + e, NGEN + 1);
    ui.genre_on = 1;
    ui.genre_t = plat_ms();
    ui.browse_on = 0;
    ui.d_field = 1;
}
static void genre_confirm(void)
{
    ui.genre_on = 0;
    ui.d_field = ui.d_head = 1;
    if (ui.genre_sel == set.genre)
        return;
    set.genre = (uint8_t)ui.genre_sel;
    set_dirty = 1;
    changed_at = plat_ms();
    if (set.genre < NGEN && code_genre(cur_slot) != set.genre) {
        browse_n = browse_list(browse_codes);
        ui.browse = 0;
        browse_load();                   /* the genre's first jam (NEW when it has none) */
    } else
        app_say("GENRE", genre_name(set.genre));
}

/* SAVE: the slot picked, then SAVE again */
static void save_button(void)
{
    if (!ui.save_mode) {
        int s = cur_slot >= 0 && cur_slot < NUSER ? cur_slot : -1, i;
        if (s < 0)
            for (i = 0; i < NUSER; i++)
                if (!user_used(i, 0)) {
                    s = i;
                    break;
                }
        ui.save_mode = 1;
        ui.save_slot = s < 0 ? 0 : s;
        ui.d_field = ui.d_head = 1;
        return;
    }
    {
        int rc = slot_save_current(ui.save_slot, 0);
        char t[8] = "U";
        ui.save_mode = 0;
        t[1] = (char)('0' + (ui.save_slot + 1) / 10);
        t[2] = (char)('0' + (ui.save_slot + 1) % 10);
        t[3] = 0;
        if (rc)
            app_say("SAVE", "FAILED");
        else
            app_say(jt_status()->playing ? "SAVING AT STOP" : "SAVED", t);
        ui.d_head = 1;
        notify(0);
    }
}

/* the progression editor */
static void edit_insert(void)
{
    jt_part_t *p = edit_part();
    int i;
    if (p->n >= (int)JT_NCHORD) {
        app_say("EDIT", "12 CHORDS AT MOST");
        return;
    }
    for (i = p->n; i > ui.cursor + 1; i--)
        p->c[i] = p->c[i - 1];
    p->c[ui.cursor + 1 < (int)JT_NCHORD ? ui.cursor + 1 : ui.cursor] = p->c[ui.cursor];
    p->n++;
    ui.cursor = clampi(ui.cursor + 1, 0, p->n - 1);
    jt_jam_changed(JX_PROG);
    mark_changed();
    notify(JX_PROG);
    audition_cursor();
    ui.d_field = ui.d_knobs = 1;
}
static void edit_delete(void)
{
    jt_part_t *p = edit_part();
    int i;
    if (p->n <= 1) {
        app_say("EDIT", "ONE CHORD AT LEAST");
        return;
    }
    for (i = ui.cursor; i + 1 < p->n; i++)
        p->c[i] = p->c[i + 1];
    p->n--;
    ui.cursor = clampi(ui.cursor, 0, p->n - 1);
    jt_jam_changed(JX_PROG);
    mark_changed();
    notify(JX_PROG);
    ui.d_field = ui.d_knobs = 1;
}
static void edit_key(uint32_t k)         /* a key: the cursor's chord number, C as 1 */
{
    jt_part_t *p = edit_part();
    int note = 53 + (int)k;
    p->c[ui.cursor].deg = (uint8_t)deg_of_semi(note % 12, jt_jam()->mode);
    jt_jam_changed(JX_PROG);
    mark_changed();
    notify(JX_PROG);
    audition_cursor();
    ui.d_field = ui.d_knobs = 1;
}

/* ================================================================ MIDI from a loop pedal, a footswitch, a keyboard */
static void midi_cc(uint32_t cc, uint32_t v)
{
    int rise = v >= 64u && ui.cc_last[cc] < 64u;
    ui.cc_last[cc] = (uint8_t)v;
    switch (cc) {
    case 1: edit_part()->energy = (uint8_t)(v * 5u / 128u); jt_jam_changed(JX_SOUND); mark_changed(); ui.d_knobs = 1; break;
    case 20: case 21: case 22: case 23:
        jt_jam()->level[cc - 20u] = (uint8_t)(v * 100u / 127u);
        mark_changed();
        ui.d_knobs = 1;
        break;
    case 24: case 25: case 26: case 27: jt_mute(cc - 24u, v >= 64u); ui.d_field = 1; break;
    case 80: if (rise) play_toggle(); break;
    case 81: if (rise) jt_part_go(-1); break;
    case 82: if (rise) jt_fill(); break;
    case 83: if (rise) { jt_tap(); ui.d_head = ui.d_knobs = 1; } break;
    case 84: if (rise) jt_restart(); break;
    case 85: if (rise) { browse_turn(1); browse_load(); } break;
    case 86: if (rise) { browse_turn(-1); browse_load(); } break;
    case 87: if (rise) jt_play(0); break;
    case 88: case 89: case 90: case 91: if (rise) mute_toggle(cc - 88u); break;
    case 92: if (rise) jt_part_go(0); break;
    case 93: if (rise) jt_part_go(1); break;
    case 120: case 123: jt_live_all_off(); break;
    }
}

static void midi_main(void)
{
    uint32_t pkt;
    while (jt_midi_take(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ty = st & 0xF0u, ch = st & 15u;
        if (st < 0x80u || st >= 0xF0u)
            continue;
        if (set.eng.midi_ch && ch != set.eng.midi_ch - 1u)
            continue;
        if (ty == 0x90u && d2)
            jt_live(1, d1, d2);
        else if (ty == 0x80u || ty == 0x90u)
            jt_live(0, d1, 0);
        else if (ty == 0xB0u)
            midi_cc(d1, d2);
        else if (ty == 0xC0u) {
            jt_jam_t j;
            int i = (int)d1 < abs_count() ? (int)d1 : abs_count() - 1;
            if (jam_get(abs_slot(i), &j) > 0)
                jam_set_current(&j, abs_slot(i), 1);
        }
    }
}

/* ================================================================ input */
static int held(uint32_t b) { return (int)((ui.btn >> b) & 1u); }

static void fx_key(uint32_t k)           /* FX held + a key: the performance map */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (WK[i] == k) {
            mute_toggle(i);
            return;
        }
    for (i = 0; i < 11u; i++)
        if (BK[i] == k) {
            switch (i) {
            case 0: jt_fill(); app_say("FILL", ""); break;
            case 1: jt_part_go(-1); break;
            case 2: jt_restart(); app_say("FROM THE TOP", ""); break;
            case 3:                      /* a break: drums alone, then back (press again) */
                if ((jt_jam()->mute & 14u) == 14u) {
                    jt_mute(JP_BASS, 0); jt_mute(JP_CHORDS, 0); jt_mute(JP_SYNTH, 0);
                    app_say("BREAK", "OVER");
                } else {
                    jt_mute(JP_BASS, 1); jt_mute(JP_CHORDS, 1); jt_mute(JP_SYNTH, 1);
                    app_say("BREAK", "DRUMS ALONE");
                }
                break;
            case 4:                      /* the drums out, then back */
                mute_toggle(JP_DRUMS);
                break;
            default: break;
            }
            ui.d_field = 1;
            return;
        }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys, k, press, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    press = btn & ~ui.btn;
    /* ---- held buttons: SEL, SEQ, REC, ARP, HOME, FX */
    if (press & (1u << B_SEL)) { ui.sel_t = now; ui.sel_used = 0; }
    if (press & (1u << B_SEQ)) { ui.seq_t = now; ui.seq_used = 0; }
    if (press & (1u << B_REC)) { ui.rec_t = now; ui.rec_used = 0; jt_tap(); ui.d_head = ui.d_knobs = 1; }
    if (press & (1u << B_ARP)) { ui.arp_t = now; ui.arp_used = 0; }
    if (press & (1u << B_HOME)) { ui.home_t = now; ui.home_used = 0; }
    if (press & (1u << B_FX)) { ui.fx_used = 0; ui.d_field = 1; }
    if (held(B_SEQ) && !ui.seq_used && now - ui.seq_t > 500u) {
        ui.seq_used = 1;
        jt_fill();
        app_say("FILL", "");
    }
    if (held(B_REC) && !ui.rec_used && now - ui.rec_t > 800u) {
        ui.rec_used = 1;
        jt_restart();
        app_say("FROM THE TOP", "NEXT BAR");
    }
    if (held(B_ARP) && !ui.arp_used && now - ui.arp_t > 550u) {
        ui.arp_used = 1;
        generate(JG_ALL);
    }
    if (held(B_HOME) && !ui.home_used && now - ui.home_t > 500u) {
        ui.home_used = 1;
        ui.d_field = 1;
    }
    /* ---- releases */
    {
        uint32_t rel = ui.btn & ~btn;
        if ((rel & (1u << B_SEL)) && !ui.sel_used) {
            ui.edit_part ^= 1;
            ui.cursor = clampi(ui.cursor, 0, edit_part()->n - 1);
            app_say("EDITING PART", ui.edit_part ? "B" : "A");
            ui.d_field = ui.d_knobs = ui.d_head = 1;
        }
        if ((rel & (1u << B_SEQ)) && !ui.seq_used)
            jt_part_go(-1);
        if ((rel & (1u << B_ARP)) && !ui.arp_used) {
            int g = PAGES[ui.page].group;
            generate(g == GR_GROOVE ? JG_GROOVE : g == GR_SOUND ? JG_SOUND : g == GR_EDIT ? JG_PROG : JG_ALL);
        }
        if ((rel & (1u << B_HOME)) && !ui.home_used)
            group_button(GR_HOME);
        if (rel & (1u << B_HOME))
            ui.d_field = 1;
        if (rel & (1u << B_FX)) {
            if (!ui.fx_used)
                group_button(GR_FX);
            ui.d_field = 1;
        }
    }
    /* ---- presses */
    if (press & (1u << B_PLAY))
        play_toggle();
    if (press & (1u << B_EDIT))
        group_button(GR_EDIT);
    if (press & (1u << B_ENV))
        group_button(GR_SOUND);
    if (press & (1u << B_LFO))
        group_button(GR_GROOVE);
    if (press & (1u << B_GLO))
        group_button(GR_GLO);
    if (press & (1u << B_SAVE))
        save_button();
    if (ui.save_mode && (press & ~((1u << B_SAVE) | (1u << B_OCTDN) | (1u << B_OCTUP)))) {
        ui.save_mode = 0;                /* another button: never mind */
        ui.d_field = ui.d_head = 1;
    }
    if (ui.browse_on && (press & (1u << B_OCTUP))) {
        browse_load();
        press &= ~(1u << B_OCTUP);
    } else if (ui.browse_on && (press & (1u << B_OCTDN))) {
        ui.browse_on = 0;
        ui.d_field = 1;
        press &= ~(1u << B_OCTDN);
    } else if (ui.genre_on && (press & (1u << B_OCTUP))) {
        genre_confirm();
        press &= ~(1u << B_OCTUP);
    } else if (ui.genre_on && (press & (1u << B_OCTDN))) {
        ui.genre_on = 0;
        ui.d_field = 1;
        press &= ~(1u << B_OCTDN);
    }
    if ((btn & (3u << B_OCTDN)) == (3u << B_OCTDN) && (press & (3u << B_OCTDN))) {
        undo_do();
    } else if (press & (1u << B_OCTDN)) {
        if (ui.save_mode)
            ui.save_slot = wrapi(ui.save_slot - 1, NUSER), ui.d_field = 1;
        else if (ui.page == PG_EDIT)
            edit_delete();
        else {
            ui.octave = clampi(ui.octave - 1, -2, 2);
            set.octave = (uint8_t)(ui.octave + 2);
            app_say("KEYS OCTAVE", ui.octave < 0 ? (ui.octave == -1 ? "-1" : "-2") : ui.octave ? (ui.octave == 1 ? "+1" : "+2") : "0");
        }
    } else if (press & (1u << B_OCTUP)) {
        if (ui.save_mode)
            ui.save_slot = wrapi(ui.save_slot + 1, NUSER), ui.d_field = 1;
        else if (ui.page == PG_EDIT)
            edit_insert();
        else {
            ui.octave = clampi(ui.octave + 1, -2, 2);
            set.octave = (uint8_t)(ui.octave + 2);
            app_say("KEYS OCTAVE", ui.octave < 0 ? (ui.octave == -1 ? "-1" : "-2") : ui.octave ? (ui.octave == 1 ? "+1" : "+2") : "0");
        }
    }
    /* ---- the keys */
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            if (held(B_FX)) {
                ui.fx_used = 1;
                fx_key(k);
            } else if (held(B_SEL)) {
                ui.sel_used = 1;
                jt_jam()->key = (uint8_t)((53u + k) % 12u);
                jt_jam_changed(JX_PROG);
                mark_changed();
                notify(JX_PROG);
                {
                    char t[16];
                    jt_key_text(jt_jam(), t);
                    app_say("KEY", t);
                }
                ui.d_knobs = ui.d_head = 1;
            } else if (ui.page == PG_EDIT) {
                edit_key(k);
            } else {
                int note = clampi(53 + (int)k + 12 * ui.octave, 0, 127);
                ui.live_note[k] = (uint8_t)note;
                ui.live_held[k] = 1;
                jt_live(1, (uint32_t)note, touch_vel(k));
            }
        } else if (!(keys & bit) && (ui.keys & bit) && ui.live_held[k]) {
            ui.live_held[k] = 0;
            jt_live(0, ui.live_note[k], 0);
        }
    }
    /* ---- the encoders */
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            int id = PAGES[ui.page].k[k];
            if (id != PR_NONE)
                param_turn(id, (int)e, k);
            ui.touched[k] = now;
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (held(B_SEL)) {               /* SEL held + SELECT: the keys' sound */
            ui.sel_used = 1;
            jt_live_all_off();
            set.eng.live_patch = (uint8_t)patch_step(set.eng.live_patch, (int)e, JK_POLY);
            settings_changed();
            app_say("KEYS PLAY", jt_name(JT_T_PATCH, set.eng.live_patch));
            ui.d_knobs = 1;
        } else if (ui.save_mode) {
            ui.save_slot = wrapi(ui.save_slot + (int)e, NUSER);
            ui.d_field = 1;
        } else if (ui.page == PG_EDIT) {
            ui.cursor = wrapi(ui.cursor + (int)e, edit_part()->n);
            audition_cursor();
            ui.d_field = ui.d_knobs = 1;
        } else
            param_turn(PR_TEMPO, (int)e, 4);
    }
    if ((e = plat_enc(EN_ALGO)) != 0)
        genre_turn((int)e);
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.save_mode) {
            ui.save_slot = wrapi(ui.save_slot + (int)e, NUSER);
            ui.d_field = 1;
        } else
            browse_turn((int)e);
    }
    if (ui.browse_on && now - ui.browse_t > 2000u)   /* 2 s to look before it loads (OCT+ now, OCT- never mind) */
        browse_load();
    if (ui.genre_on && now - ui.genre_t > 2000u)
        genre_confirm();
    if (btn != ui.btn)
        ui.d_head = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* ================================================================ lights */
static void lights(void)
{
    const jt_status_t *st = jt_status();
    const jt_jam_t *j = jt_jam();
    uint32_t b = 0, dim = 0, keys = 0, dimk = 0, now = plat_ms();
    static uint32_t beat_seen, beat_at;
    static const uint8_t GRBTN[6] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_GLO};
    int k;
    if (st->beat_seq != beat_seen) {
        beat_seen = st->beat_seq;
        beat_at = now;
    }
    if (st->playing) {
        if (st->counting)
            b |= 1u << B_PLAY;           /* red: counting in */
        else
            b |= LED_PLAY_GREEN;
    }
    if (st->playing && now - beat_at < 90u)
        b |= 1u << B_REC;                /* the beat */
    if (st->part || (st->queued && (now / 150u) & 1u))
        b |= 1u << B_SEQ;
    if (jam_dirty || ui.save_mode)
        b |= 1u << B_SAVE;
    b |= 1u << GRBTN[PAGES[ui.page].group];
    if (held(B_FX))
        b |= 1u << B_FX;
    dim = 0x3FFFu & ~b;
    /* the keys: the chord's tones bright, the scale's dim (a map to play over); EDIT: the cursor's number */
    {
        const jt_part_t *p = &j->part[ui.page == PG_EDIT ? ui.edit_part : st->part];
        int ci = ui.page == PG_EDIT ? ui.cursor : st->chord;
        int tones[6], nt;
        if (ci >= p->n)
            ci = 0;
        nt = jt_chord_pcs(j, &p->c[ci], tones);
        for (k = 0; k < NKEYS; k++) {
            int pc = (53 + k) % 12, i, in_scale = 0;
            for (i = 0; i < nt; i++)
                if (tones[i] == pc)
                    keys |= 1u << k;
            for (i = 0; i < 7; i++)
                if (jt_scale_pc(j, i) == pc)
                    in_scale = 1;
            if (in_scale)
                dimk |= 1u << k;
        }
        if (!st->playing && ui.page != PG_EDIT)
            keys = 0;                    /* stopped: the scale only */
    }
    keys |= ui.keys;
    plat_lights(b, keys, dim, dimk & ~keys, 1);
}

/* ================================================================ drawing */
static void band(int32_t y0, int32_t h) { cv_begin(240, (uint32_t)h, C_BLACK); (void)y0; }
static void band_end(int32_t y0) { cv_blit(0, (uint32_t)y0); }
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(rx - text_w(f, s), y, f, s, c);
}

#define HEAD_H 20
#define FIELD_Y 20
#define FIELD_H 144
#define KNOB_Y 164
#define KNOB_H 76

static void slot_text(char *t)
{
    t[0] = 0;
    if (cur_slot >= 0 && cur_slot < NUSER) {
        t[0] = 'U';
        t[1] = (char)('0' + (cur_slot + 1) / 10);
        t[2] = (char)('0' + (cur_slot + 1) % 10);
        t[3] = 0;
    } else if (cur_slot >= 0x80) {
        t[0] = 'F';
        t[1] = (char)('0' + (cur_slot - 0x80 + 1) / 10);
        t[2] = (char)('0' + (cur_slot - 0x80 + 1) % 10);
        t[3] = 0;
    }
}

static void draw_head(void)
{
    const jt_status_t *st = jt_status();
    const jt_jam_t *j = jt_jam();
    char t[24], b[12];
    int32_t x;
    band(0, HEAD_H);
    slot_text(t);
    x = 4;
    if (t[0])
        x = cv_text(x, 2, &FONT_XS, t, C_GRAY) + 5;
    x = cv_text(x, 2, &FONT_XS, j->name, C_HI);
    if (jam_dirty)
        cv_rect(x + 3, 8, 4, 4, C_AMB);
    /* the tempo, EXT when following a clock */
    int_text((int32_t)(st->bpm + 0.5f), b);
    t[0] = 0;
    text_cat(t, b);
    x = 236 - text_w(&FONT_XS, t);
    cv_text(x, 2, &FONT_XS, t, st->ext ? RGB(120, 230, 160) : C_WHITE);
    x -= text_w(&FONT_T, st->ext ? "EXT" : "BPM") + 4;
    cv_text(x, 4, &FONT_T, st->ext ? "EXT" : "BPM", st->ext ? RGB(120, 230, 160) : C_DIM);
    cv_rect(0, HEAD_H - 1, 240, 1, C_LINE);
    band_end(0);
}

/* a chord chip: its number big, its name small */
static void chip(int32_t x, int32_t y, int32_t w, int32_t h, const jt_jam_t *j, const jt_chord_t *c, int on, int cursor,
                 float progress)
{
    char num[16], nm[16];
    uint16_t bg = on ? RGB(70, 52, 20) : C_LINE;
    jt_chord_text(j, c, num, nm);
    cv_rect(x, y, w, h, cursor ? C_HI : bg);
    if (cursor)
        cv_rect(x + 2, y + 2, w - 4, h - 4, on ? RGB(70, 52, 20) : C_BLACK);
    if (on && progress > 0.0f)
        cv_rect(x + 2, y + h - 4, (int32_t)((float)(w - 4) * progress), 2, C_HI);
    {                                    /* the chord's name first; its number small, under it when there is room */
        const felucca_font_t *f = w >= 50 && text_w(&FONT_S, nm) <= w - 4 ? &FONT_S : &FONT_XS;
        text_c(x + w / 2, y + (h >= 30 ? 2 : (h - 16) / 2), f, nm, on ? C_WHITE : C_HI);
        if (h >= 30)
            text_c(x + w / 2, y + h - 14, &FONT_T, num, on ? C_AMB : C_DIM);
    }
}

/* the ring: a circle around the chord's name that fills, clockwise from the top, as the chord plays. Drawn into the
 * band at screen row y0 (h rows): the part of it there */
static float atan2_turns(float y, float x)   /* the angle clockwise from the top, 0..1 */
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax > ay ? ay / (ax + 1e-9f) : ax / (ay + 1e-9f), r;
    r = (a * (0.785398f + 0.273f * (1.0f - a))) * 0.1591549f;   /* atan(a) / 2 pi, a in 0..1 (within 0.3 degrees) */
    if (ay > ax)
        r = 0.25f - r;
    /* r: from the x axis towards y, in the first quadrant; to clockwise from the top */
    if (x >= 0.0f && y < 0.0f)
        return 0.25f - r;
    if (x >= 0.0f)
        return 0.25f + r;
    if (y >= 0.0f)
        return 0.75f - r;
    return 0.75f + r;
}
static void ring(int32_t cx, int32_t cy, int32_t r1, int32_t r0, float frac, uint16_t on, uint16_t off, int32_t y0, int32_t h)
{
    int32_t x, y;
    for (y = cy - r1; y <= cy + r1; y++) {
        if (y < y0 || y >= y0 + h)
            continue;
        for (x = cx - r1; x <= cx + r1; x++) {
            float dx = (float)(x - cx) + 0.5f, dy = (float)(y - cy) + 0.5f, d = dx * dx + dy * dy;
            if (d > (float)(r1 * r1) || d < (float)(r0 * r0))
                continue;
            cv_rect(x, y - y0, 1, 1, atan2_turns(dy, dx) <= frac ? on : off);
        }
    }
}

#define RING_CX 62
#define RING_CY (FIELD_Y + 58)
static void draw_home_field(void)
{
    const jt_status_t *st = jt_status();
    const jt_jam_t *j = jt_jam();
    const jt_part_t *p = &j->part[st->part & 1];
    char t[32], num[16], nm[16], nnum[16], nnm[16];
    int ci = st->chord < p->n ? st->chord : 0, i, nb = jt_bar_beats(j->meter), y0;
    float frac = st->playing && st->chord_len ? ((float)st->chord_step + 1.0f) / (float)st->chord_len : 0.0f;
    jt_chord_text(j, &p->c[ci], num, nm);
    jt_chord_text(j, &p->c[(ci + 1) % p->n], nnum, nnm);
    for (y0 = FIELD_Y; y0 < FIELD_Y + 144; y0 += 72) {
        band(y0, 72);
        /* the ring and the chord in it: its name big, its number small */
        ring(RING_CX, RING_CY, 55, 49, frac, C_HI, C_LINE, y0, 72);
        if (st->counting) {
            int_text(st->counting, t);
            text_c(RING_CX, RING_CY - 16 - y0, &FONT_L, t, C_AMB);
            text_c(RING_CX, RING_CY + 18 - y0, &FONT_T, "COUNT-IN", C_GRAY);
        } else {
            const felucca_font_t *f = text_w(&FONT_L, nm) <= 90 ? &FONT_L : &FONT_M;
            text_c(RING_CX, RING_CY - (f == &FONT_L ? 18 : 13) - y0, f, nm, st->playing ? C_WHITE : C_HI);
            text_c(RING_CX, RING_CY + 18 - y0, &FONT_XS, num, C_GRAY);
        }
        /* beside it: the parts, the key, what comes next, the beat */
        for (i = 0; i < 2; i++) {
            int on = st->part == i, q = st->queued && st->part != i;
            uint16_t c = on ? C_HI : q ? (((plat_ms() / 150u) & 1u) ? C_AMB : C_DIM) : C_LINE;
            cv_rect(130 + i * 24, FIELD_Y + 4 - y0, 20, 18, c);
            text_c(140 + i * 24, FIELD_Y + 5 - y0, &FONT_S, i ? "B" : "A", on ? C_BLACK : C_GRAY);
        }
        jt_key_text(j, t);
        text_r(236, FIELD_Y + 26 - y0, &FONT_XS, t, C_HI);
        t[0] = 0;
        text_cat(t, set.genre < NGEN ? genre_name(set.genre) : genre_name(j->genre));
        text_cat(t, "  ");
        text_cat(t, jt_name(JT_T_METER, j->meter));
        text_r(236, FIELD_Y + 6 - y0, &FONT_T, t, C_GRAY);
        text_r(236, FIELD_Y + 46 - y0, &FONT_T, "NEXT", C_DIM);
        text_r(236, FIELD_Y + 58 - y0, &FONT_B, nnm, C_WHITE);
        for (i = 0; i < nb && i < 6; i++) {
            int on = st->playing && st->beat == i;
            cv_rect(132 + i * 17, FIELD_Y + 82 - y0, 13, 5, on ? (i == 0 ? C_WHITE : C_HI) : C_LINE);
        }
        for (i = 0; i < JP_N; i++) {         /* the parts: a meter each, grey when muted */
            int32_t x = 130 + (i % 2) * 54, yy = FIELD_Y + 96 + (i / 2) * 14 - y0;
            int muted = (j->mute >> i) & 1u;
            cv_text(x, yy - 3, &FONT_T, PART_NAME[i], muted ? C_DIM : PART_RGB[i]);
            if (!muted)
                cv_rect(x + 34, yy + 2, (st->level[i] * 16) / 100 + 1, 3, PART_RGB[i]);
        }
        {                                    /* the progression, by name */
            int n = p->n, w = (232 - (n - 1) * 2) / (n ? n : 1);
            for (i = 0; i < n; i++)
                chip(4 + i * (w + 2), FIELD_Y + 122 - y0, w, 20, j, &p->c[i], st->playing && i == ci, 0, 0);
        }
        band_end(y0);
    }
}

static void draw_edit_field(void)
{
    const jt_status_t *st = jt_status();
    const jt_jam_t *j = jt_jam();
    const jt_part_t *p = edit_part();
    char t[32];
    int i;
    band(FIELD_Y, 72);
    t[0] = 0;
    text_cat(t, ui.edit_part ? "PART B" : "PART A");
    cv_text(4, 2, &FONT_XS, t, C_HI);
    {
        int beats = jt_part_beats(p), bb = jt_bar_beats(j->meter);
        char b[8];
        t[0] = 0;
        int_text(beats / bb, b);
        text_cat(t, b);
        if (beats % bb) {
            text_cat(t, " BARS +");
            int_text(beats % bb, b);
            text_cat(t, b);
        } else
            text_cat(t, beats / bb == 1 ? " BAR" : " BARS");
        text_r(236, 2, &FONT_XS, t, C_GRAY);
    }
    for (i = 0; i < 12 && i < 8; i++) {
        int32_t x = 4 + (i % 4) * 58, y = 20 + (i / 4) * 26;
        if (i < p->n)
            chip(x, y, 56, 24, j, &p->c[i], st->playing && st->part == ui.edit_part && st->chord == i, i == ui.cursor, 0);
        else if (i == p->n)
            cv_text(x + 18, y + 4, &FONT_S, "+", C_DIM);
    }
    band_end(FIELD_Y);
    band(FIELD_Y + 72, 72);
    for (i = 8; i < 12; i++) {
        int32_t x = 4 + (i % 4) * 58, y = 0;
        if (i < p->n)
            chip(x, y, 56, 24, j, &p->c[i], st->playing && st->part == ui.edit_part && st->chord == i, i == ui.cursor, 0);
        else if (i == p->n)
            cv_text(x + 18, y + 4, &FONT_S, "+", C_DIM);
    }
    {                                    /* the cursor's chord, big */
        char num[16], nm[16];
        jt_chord_text(j, &p->c[ui.cursor < p->n ? ui.cursor : 0], num, nm);
        cv_text(6, 32, &FONT_M, nm, C_WHITE);
        text_r(236, 40, &FONT_XS, num, C_GRAY);
    }
    band_end(FIELD_Y + 72);
}

static void draw_param_field(void)
{
    const page_t *pg = &PAGES[ui.page];
    char t[24];
    int i;
    band(FIELD_Y, 72);
    cv_text(4, 4, &FONT_B, pg->title, C_WHITE);
    if (ui.page == PG_PART || ui.page == PG_EDIT)
        text_r(236, 6, &FONT_XS, ui.edit_part ? "PART B" : "PART A", C_HI);
    if (ui.page == PG_SYSTEM || ui.page == PG_MIDI) {
        int_text((int32_t)(jt_status()->cpu * 100.0f), t);
        text_cat(t, "% CPU");
        text_r(236, 6, &FONT_T, t, C_GRAY);
    }
    for (i = 0; i < 2; i++) {            /* the first two settings, big */
        int id = pg->k[i];
        cv_text(4 + i * 118, 28, &FONT_T, PR_LABEL[id], KNOB_RGB[i]);
        param_text(id, t);
        cv_text(4 + i * 118, 42, &FONT_B, t, C_WHITE);
    }
    band_end(FIELD_Y);
    band(FIELD_Y + 72, 72);
    for (i = 2; i < 4; i++) {
        int id = pg->k[i];
        cv_text(4 + (i - 2) * 118, 0, &FONT_T, PR_LABEL[id], KNOB_RGB[i]);
        param_text(id, t);
        cv_text(4 + (i - 2) * 118, 14, &FONT_B, t, C_WHITE);
    }
    if (ui.page == PG_KIT) {             /* the user kit's drums: set or not */
        uint32_t s;
        for (s = 0; s < KIT_SLOTS; s++) {
            int32_t x = 4 + (int32_t)s * 26;
            uint16_t c = kslot_state[s] == SLOT_OK ? C_HI : kslot_state[s] == SLOT_DAMAGED ? RGB(255, 80, 60) : C_LINE;
            cv_rect(x, 40, 24, 14, c);
            static const char *const AB[KIT_SLOTS] = {"KICK", "SN", "RIM", "HAT", "OH", "RIDE", "CR", "TOM", "PRC"};
            text_c(x + 12, 56, &FONT_T, AB[s], C_GRAY);
        }
    } else {                             /* the music, small: the chord and the beat */
        const jt_status_t *st = jt_status();
        const jt_jam_t *j = jt_jam();
        char num[16], nm[16];
        const jt_part_t *p = &j->part[st->part & 1];
        jt_chord_text(j, &p->c[st->chord < p->n ? st->chord : 0], num, nm);
        cv_rect(0, 44, 240, 1, C_LINE);
        cv_text(4, 50, &FONT_XS, st->part ? "B" : "A", C_HI);
        cv_text(20, 50, &FONT_XS, nm, st->playing ? C_WHITE : C_GRAY);
        text_r(236, 50, &FONT_XS, num, C_DIM);
    }
    band_end(FIELD_Y + 72);
}

static void draw_overlay(const char *title, const char *const *lines, int n, int hi)   /* a box over the field */
{
    int i;
    band(FIELD_Y, 72);
    cv_rect(0, 0, 240, 72, RGB(16, 16, 20));
    cv_text(8, 4, &FONT_B, title, C_HI);
    for (i = 0; i < n && i < 3; i++)
        cv_text(8, 26 + i * 15, &FONT_XS, lines[i], i == hi ? C_WHITE : C_GRAY);
    band_end(FIELD_Y);
    band(FIELD_Y + 72, 72);
    cv_rect(0, 0, 240, 72, RGB(16, 16, 20));
    for (i = 3; i < n && i < 7; i++)
        cv_text(8, (i - 3) * 15 + 2, &FONT_XS, lines[i], i == hi ? C_WHITE : C_GRAY);
    band_end(FIELD_Y + 72);
}

static void draw_field(void)
{
    uint32_t now = plat_ms();
    if (now < ui.msg_until) {            /* a message, big */
        band(FIELD_Y, 72);
        band_end(FIELD_Y);
        band(FIELD_Y + 72, 72);
        text_c(120, 0, &FONT_M, ui.msg[0], C_WHITE);
        text_c(120, 30, &FONT_B, ui.msg[1], C_HI);
        band_end(FIELD_Y + 72);
        return;
    }
    if (ui.save_mode) {
        char l[7][24];
        const char *ls[7];
        int i;
        for (i = 0; i < 7; i++) {
            int s = wrapi(ui.save_slot + i - 3, NUSER);
            browse_label(s, l[i]);   /* (user slots: their codes are their numbers) */
            ls[i] = l[i];
        }
        draw_overlay(user_used(ui.save_slot, 0) ? "SAVE: REPLACE?" : "SAVE TO", ls, 7, 3);
        return;
    }
    if (ui.browse_on) {
        char l[7][24];
        const char *ls[7];
        int i;
        char title[32] = "";
        for (i = 0; i < 7; i++) {
            int k = ui.browse + i - 3;
            l[i][0] = 0;
            if (browse_n > 6 || (k >= 0 && k < browse_n))
                browse_label(browse_codes[wrapi(k, browse_n)], l[i]);
            ls[i] = l[i];
        }
        text_cat(title, genre_name(set.genre));
        text_cat(title, "  (OCT+ NOW)");
        draw_overlay(title, ls, 7, 3);
        return;
    }
    if (ui.genre_on) {
        char l[7][20];
        const char *ls[7];
        int i;
        for (i = 0; i < 7; i++) {
            text_cpy(l[i], genre_name(wrapi(ui.genre_sel + i - 3, NGEN + 1)), 20);
            ls[i] = l[i];
        }
        draw_overlay("GENRE  (OCT+ NOW)", ls, 7, 3);
        return;
    }
    if (held(B_FX)) {
        static const char *const L[7] = {"WHITE 1-4: MUTE DRUMS BASS", "  CHORDS SYNTH", "BLACK 1: FILL   2: PART A/B",
                                         "3: FROM THE TOP", "4: BREAK (DRUMS ALONE)", "5: DRUMS OUT", "LET GO: EFFECTS PAGE"};
        draw_overlay("PERFORM", L, 7, -1);
        return;
    }
    if (held(B_HOME) && ui.home_used) {
        const char *L[3];
        int i;
        for (i = 0; i < 3; i++)
            L[i] = PAGES[ui.page].help[i];
        draw_overlay(PAGES[ui.page].title, L, 3, -1);
        return;
    }
    if (ui.upload_n) {
        char t[24], b[8];
        band(FIELD_Y, 72);
        band_end(FIELD_Y);
        band(FIELD_Y + 72, 72);
        text_c(120, 0, &FONT_B, "RECEIVING SAMPLE", C_WHITE);
        int_text(ui.upload_k, t);
        text_cat(t, " / ");
        int_text(ui.upload_n, b);
        text_cat(t, b);
        text_c(120, 24, &FONT_B, t, C_HI);
        band_end(FIELD_Y + 72);
        return;
    }
    if (ui.page == PG_HOME)
        draw_home_field();
    else if (ui.page == PG_EDIT)
        draw_edit_field();
    else
        draw_param_field();
}

static void draw_knobs(void)
{
    const page_t *pg = &PAGES[ui.page];
    int i;
    band(KNOB_Y, KNOB_H);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < 4; i++) {
        int id = pg->k[i];
        int32_t x = i * 60;
        char t[24];
        float v = param_norm(id);
        int lit = ui.touched[i] && plat_ms() - ui.touched[i] < 600u;
        if (id == PR_NONE)
            continue;
        text_c(x + 30, 6, &FONT_T, PR_LABEL[id], KNOB_RGB[i]);
        param_text(id, t);
        if (text_w(&FONT_XS, t) > 58)
            t[7] = 0;
        text_c(x + 30, 24, &FONT_XS, t, lit ? C_WHITE : C_HI);
        cv_rect(x + 6, 50, 48, 4, C_LINE);
        cv_rect(x + 6, 50, (int32_t)(48.0f * fm_clampf(v, 0, 1)), 4, KNOB_RGB[i]);
    }
    band_end(KNOB_Y);
}

void app_redraw(void) { ui.d_head = ui.d_field = ui.d_knobs = 1; }

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 80, C_BLACK);
    text_c(120, 10, &FONT_L, "JAMTRACK", RGB(255, 176, 64));
    text_c(120, 50, &FONT_XS, "A BACKING BAND", C_GRAY);
    cv_blit(0, 80);
}

static void upload_progress(uint32_t k, uint32_t n)
{
    ui.upload_k = (int)k;
    ui.upload_n = (int)n;
    draw_field();
    if (k >= n)
        ui.upload_n = 0;
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.page = PG_HOME;
    ui.octave = (int)set.octave - 2;
    ui.d_head = ui.d_field = ui.d_knobs = 1;
}

/* ================================================================ saving and the rest, once a frame */
static void housekeeping(void)
{
    const jt_status_t *st = jt_status();
    uint32_t now = plat_ms();
    if (st->playing)
        stopped_at = now;
    if (pending_on && (st->beat_seq != pending_seq) && st->beat == 0) {   /* the next bar: the browsed jam */
        pending_on = 0;
        jam_apply(&pending_jam, pending_slot);
        jt_restart();
        app_say(jt_jam()->name, "");
    }
    if (pending_on && !st->playing) {
        pending_on = 0;
        jam_apply(&pending_jam, pending_slot);
    }
    if (backup_hold || st->playing || now - stopped_at < 1500u)
        return;                          /* never a flash write while the band plays */
    if (bank_pending[0])
        bank_write_now(0);
    if (bank_pending[1])
        bank_write_now(1);
    if ((ses_dirty || set_dirty) && now - changed_at > 3000u) {
        if (set_dirty) {
            set.eng = *jt_settings();
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
                set_dirty = 0;
        }
        if (ses_dirty)
            session_save();
    }
}

void app_frame(void)
{
    static uint32_t last_seq, last_chord, last_part, last_play, last_msg, last_q, last_ext;
    static uint32_t last_bpm;
    const jt_status_t *st = jt_status();
    uint32_t now = plat_ms();
    midi_main();
    input();
    lights();
    housekeeping();
    ann_tick(now);
    if (ui_redraw_all) {
        ui_redraw_all = 0;
        ui.d_head = ui.d_field = ui.d_knobs = 1;
        ui.cursor = clampi(ui.cursor, 0, edit_part()->n - 1);
    }
    if (st->beat_seq != last_seq || st->chord != last_chord || st->part != last_part || st->playing != last_play ||
        st->queued != last_q) {
        last_seq = st->beat_seq;
        last_chord = st->chord;
        last_part = st->part;
        last_play = st->playing;
        last_q = st->queued;
        if (ui.page == PG_HOME || ui.page == PG_EDIT || !ui.d_field)
            ui.d_field = 1;
    }
    if (st->queued && ui.page == PG_HOME && (now / 150u) != last_msg)
        ui.d_field = 1;
    last_msg = now / 150u;
    if ((uint32_t)(st->bpm * 2.0f) != last_bpm || st->ext != last_ext) {
        last_bpm = (uint32_t)(st->bpm * 2.0f);
        last_ext = st->ext;
        ui.d_head = 1;
    }
    {
        static uint32_t msg_was;
        if (msg_was && now >= msg_was) {
            msg_was = 0;
            ui.d_field = 1;
        }
        if (now < ui.msg_until)
            msg_was = ui.msg_until;
    }
    {
        static uint32_t lit_until;
        int i;
        for (i = 0; i < 4; i++)
            if (ui.touched[i] && now - ui.touched[i] < 700u && now - ui.touched[i] > 600u)
                lit_until = 1;
        if (lit_until) {
            lit_until = 0;
            ui.d_knobs = 1;
        }
    }
    if (ui.d_head) {
        ui.d_head = 0;
        draw_head();
    }
    if (ui.d_field) {
        ui.d_field = 0;
        draw_field();
    }
    if (ui.d_knobs) {
        ui.d_knobs = 0;
        draw_knobs();
    }
}

/* ================================================================ boot */
void app_init(void)
{
    jt_init();
    settings_load();
    banks_load();
    session_load();
    kit_load();
    ui.edit_part = 0;
    ui.cursor = 0;
    stopped_at = plat_ms();
}

/* ================================================================ backup (fm1-base backup.c) */
int app_backup_playing(void) { return jt_status()->playing; }
void app_backup_flush(void)
{
    if (ses_dirty)
        session_save();
    if (set_dirty) {
        set.eng = *jt_settings();
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
    if (bank_pending[0])
        bank_write_now(0);
    if (bank_pending[1])
        bank_write_now(1);
}
void app_backup_hold(int on) { backup_hold = (uint8_t)(on != 0); }
void app_backup_restored(void)
{
    settings_load();
    banks_load();
    session_load();
    ui_redraw_all = 1;
}

/* ================================================================ the host simulator's tests */
#ifdef PLAT_HOST
static int host_fill(uint32_t index, uint8_t *pay, void *ctx)   /* a test sound for "kitwrite": a decaying tone */
{
    uint32_t i;
    ef_ima_t s = {0, 0};
    (void)ctx;
    for (i = 0; i < SEC_PAYLOAD / JT_BLK_BYTES; i++) {
        uint8_t *blk = pay + i * JT_BLK_BYTES;
        uint32_t j;
        blk[0] = (uint8_t)s.pred;
        blk[1] = (uint8_t)((uint32_t)s.pred >> 8);
        blk[2] = (uint8_t)s.index;
        blk[3] = 0;
        for (j = 0; j < JT_BLK_SAMPLES; j++) {
            uint32_t n = (index * (SEC_PAYLOAD / JT_BLK_BYTES) + i) * JT_BLK_SAMPLES + j;
            float t = (float)n / 22050.0f;
            int32_t x = (int32_t)(20000.0f * fm_sin_turns(fm_floorf(t * 180.0f * 1000.0f) / 1000.0f) * fm_expf(-t * 8.0f));
            uint32_t nib = ef_ima_encode(&s, x);
            if (j & 1u)
                blk[4u + j / 2u] = (uint8_t)((blk[4u + j / 2u] & 15u) | nib << 4);
            else
                blk[4u + j / 2u] = (uint8_t)nib;
        }
    }
    return 0;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c;
    (void)d;
    if (!strcmp(cmd, "kitwrite")) {      /* kitwrite SLOT FRAMES */
        int rc = kit_write((uint32_t)atoi(a), (uint32_t)atoi(b), 22050u, 0, "TEST", 0, host_fill, 0, 0);
        if (rc)
            printf("kitwrite: %d\n", rc);
        return 1;
    }
    if (!strcmp(cmd, "kitbreak")) {      /* kitbreak SLOT: a byte of its first sector changed */
        uint32_t s = (uint32_t)atoi(a), i;
        for (i = 0; i < KIT_NSEC; i++)
            if (kmap_bit(kdir.slot[s].map, i)) {
                uint8_t z = 0;
                plat_data_write(ksec_addr(i) + 100u, &z, 1);
                break;
            }
        return 1;
    }
    if (!strcmp(cmd, "generate")) {      /* generate GENRE */
        set.genre = (uint8_t)atoi(a);
        generate(JG_ALL);
        return 1;
    }
    if (!strcmp(cmd, "factory")) {       /* factory N: that factory jam */
        jt_jam_t j;
        factory_jam(atoi(a), &j);
        jam_set_current(&j, 0x80 + atoi(a), 0);
        return 1;
    }
    return 0;
}

int app_expect(const char *what, int *got)
{
    const jt_status_t *st = jt_status();
    const jt_jam_t *j = jt_jam();
    if (!strcmp(what, "playing")) *got = st->playing;
    else if (!strcmp(what, "part")) *got = st->part;
    else if (!strcmp(what, "queued")) *got = st->queued;
    else if (!strcmp(what, "bar")) *got = st->bar;
    else if (!strcmp(what, "chord")) *got = st->chord;
    else if (!strcmp(what, "ext")) *got = st->ext;
    else if (!strcmp(what, "bpm")) *got = (int)(st->bpm + 0.5f);
    else if (!strcmp(what, "bpm10")) *got = j->bpm10;
    else if (!strcmp(what, "key")) *got = j->key;
    else if (!strcmp(what, "mode")) *got = j->mode;
    else if (!strcmp(what, "slot")) *got = cur_slot;
    else if (!strcmp(what, "dirty")) *got = jam_dirty;
    else if (!strcmp(what, "sesdirty")) *got = ses_dirty;
    else if (!strcmp(what, "page")) *got = ui.page;
    else if (!strcmp(what, "mute")) *got = j->mute;
    else if (!strcmp(what, "energy")) *got = edit_part()->energy;
    else if (!strcmp(what, "nchords")) *got = edit_part()->n;
    else if (!strcmp(what, "cursor")) *got = ui.cursor;
    else if (!strcmp(what, "editpart")) *got = ui.edit_part;
    else if (!strcmp(what, "deg")) *got = edit_part()->c[ui.cursor].deg;
    else if (!strcmp(what, "genre")) *got = set.genre;
    else if (!strcmp(what, "kit_ok")) *got = (int)kit_count_ok();
    else if (!strncmp(what, "kitstate", 8)) *got = kslot_state[atoi(what + 8) % KIT_SLOTS];
    else if (!strcmp(what, "users")) {
        int i, n = 0;
        for (i = 0; i < NUSER; i++)
            n += user_used(i, 0);
        *got = n;
    } else if (!strcmp(what, "clockout")) *got = set.eng.clock_out;
    else if (!strcmp(what, "save_mode")) *got = ui.save_mode;
    else
        return 0;
    return 1;
}
#endif
