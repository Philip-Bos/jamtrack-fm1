/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: genres and the generator. A genre is a set of likely choices: tempos, time signatures, progressions (in
 * Nashville numbers, with their scale: M major, m minor, d dorian, x mixolydian, l lydian), grooves, bass lines,
 * comping, synth lines, kits and sounds. GENERATE draws a new jam from one, every choice still the player's to
 * change. Part of engine.c. */

#include "jt_ids.h"
#define END 0xFF

typedef struct {
    const char *name;
    uint8_t bpm_lo, bpm_hi;
    uint8_t meters[3];
    uint8_t grooves[5], bass[5], comp[5], synth[5], kits[4], pb[4], pc[5], ps[5];
    uint8_t swing_lo, swing_hi, tape_lo, tape_hi, color_lo, color_hi, rev, space;
    const char *prog[12];
} genre_t;

static const genre_t GENRES[] = {
    {"INDIE POP", 96, 124, {JT_METER_44, END},
     {G_STRAIGHT, G_DISCO, G_MOTORIK, G_HALF, END}, {B_ROOT8, B_MELODIC, B_OCT, B_TRES, END},
     {C_STRUM, C_PIANO, C_EIGHTHS, C_STABS, END}, {SY_HOOK, SY_ARP8, SY_PAD, SY_COUNTER, END}, {K_VINTAGE, K_STUDIO, K_LM, END},
     {P_FINGER, P_PICK, P_MUTED, END}, {P_CLEAN, P_JANGLE, P_RHODES, P_POLY, END}, {P_SOFTLEAD, P_POLY, P_BELLS, P_PLUCK, END},
     45, 55, 10, 35, 1, 2, 30, 40,
     {"M:1 5 6m 4", "M:6m 4 1 5", "M:1 3m 4 4m", "M:1maj7 6m7 2m7 5", "M:4 1 5 6m", "x:1 b7 4 1", "M:1 5/3 6m 4",
      "M:2m7 5 1maj7 6m7", "M:4maj7 5 3m7 6m", "M:1 4 6m 5", "M:1 6m 4 5sus4", 0}},
    {"DREAM POP", 70, 100, {JT_METER_44, JT_METER_68, END},
     {G_DREAMY, G_HALF, G_BALLAD, G_BOSSA, END}, {B_SPARSE, B_MELODIC, B_WHOLE, B_TRES, END},
     {C_HOLD, C_ARP, C_STRUM, C_HALVES, END}, {SY_PAD, SY_COUNTER, SY_BELLS, SY_MELODY, END}, {K_VINTAGE, K_LOFI, K_BRUSH, END},
     {P_MUTED, P_FINGER, P_FRETLESS, END}, {P_CLEAN, P_RHODES, P_JUNO, P_WURLI, END}, {P_JUNO, P_SOFTLEAD, P_VIBES, P_FLUTE, END},
     48, 56, 30, 60, 2, 3, 50, 70,
     {"M:1maj7 4maj7", "M:1maj7 3m7 4maj7 4m6", "m:1m9 49", "d:1m7 47", "M:4maj7 3m7 2m7 1maj7", "M:1maj7 1m7 4maj7 4m7",
      "m:1m7 b6maj7 b3maj7 b7", "M:6m9 4maj7 1maj7 5sus4", "l:1maj7 2", "m:1m9 b7 b6maj7 5m7", 0}},
    {"BEDROOM", 70, 95, {JT_METER_44, JT_METER_34, END},
     {G_BOSSA, G_LOFI, G_DREAMY, G_MACHINE, END}, {B_MELODIC, B_SPARSE, B_TRES, B_FUNK, END},
     {C_BOSSA, C_PICK, C_HOLD, C_CHARL, END}, {SY_MELODY, SY_BELLS, SY_COUNTER, SY_OFF, END}, {K_LOFI, K_BOX, K_VINTAGE, END},
     {P_FINGER, P_MUTED, P_UPRIGHT, END}, {P_NYLON, P_RHODES, P_CLEAN, P_FELT, END}, {P_FLUTE, P_VIBES, P_SOFTLEAD, P_BELLS, END},
     52, 62, 40, 75, 2, 3, 35, 45,
     {"M:2m7 57 1maj7 6m7", "M:1maj7 2m7 3m7 4maj7", "M:4maj7 3m7 6m7 2m7", "M:1maj7 6m7 2m9 513", "m:1m7 4m7 b7 b3maj7",
      "M:1maj7 b7 4maj7 4m6", "M:6m7 2m7 5sus4 1maj7", "M:1maj9 4maj9", 0}},
    {"INDIE FUNK", 106, 124, {JT_METER_44, END},
     {G_DISCO, G_FUNK, G_MOTORIK, END}, {B_OCT, B_DISCO, B_FUNK, B_MOTOWN, END},
     {C_SCRATCH, C_STABS, C_OFFBEAT, C_EIGHTHS, END}, {SY_STABS, SY_ARP16, SY_HOOK, SY_PAD, END}, {K_VINTAGE, K_STUDIO, K_LM, END},
     {P_PICK, P_FINGER, P_SYNTHB, END}, {P_CLEAN, P_WURLI, P_RHODES, P_POLY, END}, {P_STRINGS, P_BRASS, P_POLY, P_PLUCK, END},
     50, 56, 10, 30, 2, 3, 22, 30,
     {"d:1m7 47", "M:1maj7 6m7 2m7 57", "m:1m7 4m7 b7 b3maj7", "M:2m7 3m7 4maj7 57", "d:1m7 2m7 b3maj7 47",
      "M:4maj7 3m7 2m7 1maj7", "m:1m7 b7 b6maj7 57", "d:1m9 49", 0}},
    {"INDIE ROCK", 110, 150, {JT_METER_44, END},
     {G_STRAIGHT, G_ROCK, G_MOTORIK, G_HALF, G_PUNK}, {B_ROOT8, B_MELODIC, B_16, END},
     {C_EIGHTHS, C_STRUM, C_HALVES, END}, {SY_COUNTER, SY_OFF, SY_ARP8, SY_PAD, END}, {K_ROOM, K_ROCK, K_STUDIO, END},
     {P_PICK, P_FINGER, END}, {P_JANGLE, P_CLEAN, P_ORGAN, END}, {P_SQUARE, P_STRINGS, P_ORGAN, END},
     50, 50, 15, 35, 0, 1, 30, 45,
     {"M:1 5 6m 4", "M:1 4 6m 5", "x:1 b7 4 1", "m:1m b6 b3 b7", "M:4 5 6m 6m", "M:1 3m 4 5", "M:6m 4 1 5",
      "m:1m b7 b6 b7", "M:1 1/3 4 4m", 0}},
    {"FOLK", 80, 124, {JT_METER_44, JT_METER_34, JT_METER_68},
     {G_STOMP, G_TRAIN, G_TOMS, G_WALTZ, G_BALLAD}, {B_TWO, B_ROOT8, B_MELODIC, B_WHOLE, END},
     {C_FOLK, C_PICK, C_BOOM, C_STRUM, END}, {SY_OFF, SY_COUNTER, SY_MELODY, END}, {K_CAJON, K_BRUSH, K_VINTAGE, END},
     {P_UPRIGHT, P_FINGER, END}, {P_ACOUSTIC, P_NYLON, P_CLEAN, END}, {P_FLUTE, P_STRINGS, P_SOFTLEAD, END},
     48, 54, 5, 25, 0, 1, 30, 45,
     {"M:1 4 1 5", "M:1 5 6m 4", "M:6m 4 1 5", "M:1 4 6m 5", "m:1m b7 b6 b7", "M:1 1/3 4 5", "M:4 1 5 5", "M:1 5/3 4 1", 0}},
    {"SOUL", 68, 96, {JT_METER_44, END},
     {G_BOOMBAP, G_LOFI, G_FUNK, G_BALLAD, END}, {B_MELODIC, B_TRES, B_FUNK, B_SPARSE, END},
     {C_HOLD, C_CHARL, C_PIANO, C_STABS, END}, {SY_COUNTER, SY_PAD, SY_MELODY, END}, {K_VINTAGE, K_LOFI, K_STUDIO, END},
     {P_FINGER, P_FRETLESS, P_SUB, END}, {P_RHODES, P_WURLI, P_ORGAN, END}, {P_STRINGS, P_SOFTLEAD, P_FLUTE, END},
     54, 62, 20, 45, 3, 3, 30, 40,
     {"M:4maj9 3m7 2m9 1maj9", "M:2m9 513 1maj9 1maj9", "d:1m9 2m7 b3maj7 2m7", "M:1maj9 6m9 2m9 57sus4",
      "m:1m9 4m9 b7 b3maj7", "M:4maj7 57 3m7 6m7", 0}},
    {"CITY POP", 96, 120, {JT_METER_44, END},
     {G_FUNK, G_DISCO, G_STRAIGHT, END}, {B_FUNK, B_OCT, B_MOTOWN, END},
     {C_STABS, C_PIANO, C_SCRATCH, END}, {SY_HOOK, SY_STABS, SY_ARP16, END}, {K_STUDIO, K_LM, END},
     {P_PICK, P_FINGER, P_SYNTHB, END}, {P_RHODES, P_CLEAN, P_POLY, END}, {P_BRASS, P_POLY, P_BELLS, END},
     50, 54, 10, 30, 3, 3, 28, 35,
     {"M:4maj7 57 3m7 6m7", "M:4maj7 3m7 2m7 1maj7", "M:2m7 57 3m7 6m7", "M:4maj7 57 3m7 6m 2m7 57 1maj7:8", 0}},
    {"ROCK", 100, 150, {JT_METER_44, END},
     {G_ROCK, G_STRAIGHT, G_PUNK, G_HALF, END}, {B_ROOT8, B_16, B_MELODIC, END},
     {C_EIGHTHS, C_STRUM, C_HALVES, END}, {SY_OFF, SY_PAD, END}, {K_ROCK, K_ROOM, END},
     {P_PICK, P_SYNTHB, END}, {P_JANGLE, P_ORGAN, P_CLEAN, END}, {P_ORGAN, P_STRINGS, END},
     50, 50, 10, 30, 0, 0, 30, 45,
     {"x:15 b75 45 15", "m:1m b6 b7 1m", "M:1 4 5 4", "m:15 b35 45 b65", "M:1 5 4 4", "x:1 b7 b6 b7", 0}},
    {"BLUES", 70, 120, {JT_METER_44, JT_METER_128, END},
     {G_SHUFFLE, G_JAZZ, G_BALLAD, END}, {B_BOOGIE, B_WALK, B_ROOT8, END},
     {C_QUARTERS, C_CHARL, C_HOLD, C_STABS, END}, {SY_OFF, SY_COUNTER, END}, {K_VINTAGE, K_BRUSH, K_ROOM, END},
     {P_UPRIGHT, P_FINGER, END}, {P_ORGAN, P_WURLI, P_CLEAN, P_RHODES, END}, {P_ORGAN, P_BRASS, END},
     60, 66, 10, 30, 2, 2, 30, 45,
     {"M:17:16 47:8 17:8 57:4 47:4 17:4 57:4", "M:17:4 47:4 17:8 47:8 17:8 57:4 47:4 17:4 57:4",
      "m:1m7:16 4m7:8 1m7:8 b67:4 57:4 1m7:4 57:4", "M:17:4 57:4 47:8 17:4 57:4 17:4 57:4", 0}},
    {"HIP HOP", 80, 96, {JT_METER_44, END},
     {G_BOOMBAP, G_LOFI, G_MACHINE, END}, {B_HIPHOP, B_SPARSE, B_WHOLE, END},
     {C_HOLD, C_STABS, C_CHARL, END}, {SY_MELODY, SY_BELLS, SY_COUNTER, END}, {K_LOFI, K_808, K_VINTAGE, END},
     {P_SUB, P_MOOG, P_FINGER, END}, {P_RHODES, P_FELT, P_STRINGS, END}, {P_VIBES, P_BELLS, P_FLUTE, END},
     56, 64, 30, 60, 2, 3, 25, 35,
     {"m:1m7 b6maj7", "m:1m9 4m9", "d:1m7 2m7", "M:2m9 57 1maj9 6m9", "m:1m b6 b3 b7", "m:1m7 b7 b6maj7 b6maj7", 0}},
    {"REGGAE", 68, 90, {JT_METER_44, END},
     {G_ONEDROP, END}, {B_DROP, B_SPARSE, END}, {C_OFFBEAT, END}, {SY_OFF, SY_PAD, END}, {K_STUDIO, K_VINTAGE, END},
     {P_SUB, P_FINGER, END}, {P_ORGAN, P_CLEAN, END}, {P_ORGAN, P_BRASS, END},
     52, 58, 15, 35, 0, 1, 35, 45,
     {"M:1 4", "M:1 5 6m 4", "m:1m b7", "M:1 4 5 4", "m:1m 4m", 0}},
    {"JAZZ", 100, 180, {JT_METER_44, JT_METER_34, END},
     {G_JAZZ, G_BOSSA, END}, {B_WALK, END}, {C_CHARL, C_HOLD, C_BOSSA, END}, {SY_OFF, SY_MELODY, END}, {K_BRUSH, END},
     {P_UPRIGHT, END}, {P_RHODES, P_FELT, P_ORGAN, END}, {P_VIBES, P_FLUTE, END},
     62, 68, 5, 20, 2, 3, 30, 45,
     {"M:2m7 57 1maj7:8", "M:1maj7 6m7 2m7 57", "M:3m7 67 2m7 57", "m:2m7b5 57 1m7:8", "M:1maj7:8 2m7 57", 0}},
    {"COUNTRY", 90, 130, {JT_METER_44, JT_METER_34, END},
     {G_TRAIN, G_STRAIGHT, G_WALTZ, END}, {B_TWO, END}, {C_BOOM, C_FOLK, END}, {SY_OFF, SY_COUNTER, END}, {K_BRUSH, K_STUDIO, END},
     {P_UPRIGHT, P_PICK, END}, {P_ACOUSTIC, P_CLEAN, END}, {P_SOFTLEAD, P_STRINGS, END},
     50, 54, 5, 20, 0, 0, 25, 35,
     {"M:1 4 1 5", "M:1:8 4:8 5:8 1:8", "M:1 1 4 4 1 5 1 1", "M:1 4 5 1", 0}},
    {"SYNTH POP", 100, 128, {JT_METER_44, END},
     {G_MACHINE, G_DISCO, G_STRAIGHT, G_MOTORIK, END}, {B_16, B_OCT, B_ROOT8, END},
     {C_HOLD, C_STABS, C_EIGHTHS, END}, {SY_ARP16, SY_HOOK, SY_PULSE, END}, {K_LM, K_808, K_BOX, END},
     {P_SYNTHB, P_MOOG, END}, {P_JUNO, P_POLY, P_STRINGS, END}, {P_PLUCK, P_SQUARE, P_BELLS, END},
     50, 50, 10, 30, 1, 2, 30, 50,
     {"m:1m b6 b3 b7", "M:6m 4 1 5", "m:1m b7 b6 b7", "M:4 5 6m 1/3", "m:1m 4m b6 b7", 0}},
    {"BALLAD", 60, 84, {JT_METER_44, JT_METER_68, JT_METER_34},
     {G_BALLAD, G_WALTZ, G_DREAMY, END}, {B_WHOLE, B_SPARSE, B_MELODIC, END},
     {C_ARP, C_HOLD, C_PIANO, C_PICK, END}, {SY_PAD, SY_COUNTER, END}, {K_BRUSH, K_STUDIO, K_VINTAGE, END},
     {P_FINGER, P_FRETLESS, P_SUB, END}, {P_FELT, P_RHODES, P_NYLON, END}, {P_STRINGS, P_JUNO, END},
     48, 52, 10, 30, 1, 2, 40, 60,
     {"M:1 5/3 6m 4", "M:1 3m 4 5", "M:6m 4 1 5", "M:1 6m 4 5", "M:4 5 3m 6m", "M:1 1/3 4 4m", 0}},
};
#define NGENRES (sizeof(GENRES) / sizeof(GENRES[0]))

static const char *const NAME_A[] = {"VELVET", "GOLDEN", "SLOW", "NEON", "PALE", "LATE", "QUIET", "SUNDAY", "BLUE",
                                     "SOFT", "WARM", "LOW", "HAZY", "AMBER", "STATIC", "LAZY", "SILVER", "CITY",
                                     "PAPER", "OCEAN", "NIGHT", "FADED", "SECOND", "COPPER"};
static const char *const NAME_B[] = {"HAZE", "TIDE", "SUMMER", "ROOMS", "LIGHTS", "DRIVE", "BLOOM", "SWIM", "TAPE",
                                     "MORNING", "STATION", "WAVES", "WINDOW", "GARDEN", "COAST", "FEVER", "MOTEL",
                                     "SIGNAL", "HOURS", "CANYON", "MIRROR", "RIVER", "SKY", "STEPS"};

static uint32_t g_rs;
static uint32_t grand(uint32_t n)        /* 0 .. n-1 */
{
    g_rs = g_rs * 1664525u + 1013904223u;
    return n ? (g_rs >> 8) % n : 0u;
}
static int pick(const uint8_t *list, int max)
{
    int n = 0;
    while (n < max && list[n] != END)
        n++;
    return n ? list[grand((uint32_t)n)] : 0;
}

static int prog_mode(const char *s)
{
    switch (s[0]) {
    case 'm': return JM_MINOR;
    case 'd': return JM_DORIAN;
    case 'x': return JM_MIXO;
    case 'l': return JM_LYDIAN;
    default: return JM_MAJOR;
    }
}

/* a progression from text into a part, its lengths written for 4/4 brought to the meter's beats */
static void prog_into(jt_part_t *p, const char *s, int meter)
{
    int i, bb = jt_bar_beats(meter);
    if (s[1] == ':')
        s += 2;
    if (jt_parse_prog(s, p, 4))
        jt_parse_prog("1 4 5 1", p, 4);
    for (i = 0; i < p->n; i++) {
        int b = p->c[i].beats * bb / 4;
        p->c[i].beats = (uint8_t)(b < 1 ? 1 : b > 16 ? 16 : b);
    }
}

static void name_cat(char *d, const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (d[n])
        n++;
    while (*s && n + 1u < max)
        d[n++] = *s++;
    d[n] = 0;
}

void jt_jam_default(jt_jam_t *j)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)j;
    for (i = 0; i < sizeof *j; i++)
        b[i] = 0;
    j->magic = JT_JAM_MAGIC;
    name_cat(j->name, "NEW JAM", JT_NAME);
    j->key = 0;
    j->mode = JM_MAJOR;
    j->meter = JT_METER_44;
    j->bpm10 = 1050;
    j->swing = 50;
    j->human = 40;
    j->fills = JF_ROUND;
    j->color = 1;
    j->chain = JC_HAND;
    j->kit = K_VINTAGE;
    j->groove = G_STRAIGHT;
    j->bstyle = B_MELODIC;
    j->cstyle = C_STRUM;
    j->sstyle = SY_COUNTER;
    j->patch[0] = P_FINGER;
    j->patch[1] = P_CLEAN;
    j->patch[2] = P_SOFTLEAD;
    j->level[JP_DRUMS] = 80;
    j->level[JP_BASS] = 78;
    j->level[JP_CHORDS] = 70;
    j->level[JP_SYNTH] = 55;
    for (i = 0; i < JP_N; i++) {
        j->tone[i] = 50;
        j->verb[i] = i == JP_BASS ? 5 : i == JP_DRUMS ? 18 : 35;
    }
    j->rev = 35;
    j->space = 40;
    j->tape = 20;
    j->glue = 35;
    j->width = 70;
    j->seed = 7;
    j->switch_at = JS_ROUND;
    jt_parse_prog("1 5 6m 4", &j->part[0], 4);
    jt_parse_prog("4 5 3m 6m", &j->part[1], 4);
    j->part[0].energy = 2;
    j->part[1].energy = 3;
    j->part[0].reps = j->part[1].reps = 2;
    j->part[1].var = 1;
}

void jt_jam_sanitize(jt_jam_t *j)
{
    uint32_t i, k;
    j->magic = JT_JAM_MAGIC;
    for (i = 0; i < JT_NAME; i++) {
        char c = j->name[i];
        if (!c)
            break;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '.' || c == '\''))
            j->name[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : ' ';
    }
    j->name[JT_NAME - 1u] = 0;
    j->key %= 12;
    j->mode %= JM_N;
    j->meter %= JT_METER_N;
    j->genre %= NGENRES;
    if (j->bpm10 < 400 || j->bpm10 > 2400)
        j->bpm10 = j->bpm10 < 400 ? 400 : 2400;
    if (j->swing > 100) j->swing = 100;
    if (j->human > 100) j->human = 100;
    j->fills %= JF_N;
    j->push = j->push ? 1 : 0;
    if (j->color > 3) j->color = 3;
    j->chain %= JC_N;
    j->kit %= NKITS;
    j->groove %= NGROOVES;
    j->bstyle %= NBSTYLES;
    j->cstyle %= NCSTYLES;
    j->sstyle %= SY_N;
    if (j->count > 2) j->count = 2;
    for (i = 0; i < 3; i++)
        j->patch[i] %= NPATCHES;
    j->mute &= 15;
    for (i = 0; i < JP_N; i++) {
        if (j->level[i] > 100) j->level[i] = 100;
        if (j->tone[i] > 100) j->tone[i] = 100;
        if (j->verb[i] > 100) j->verb[i] = 100;
        if (j->oct[i] < -2) j->oct[i] = -2;
        if (j->oct[i] > 2) j->oct[i] = 2;
    }
    if (j->rev > 100) j->rev = 100;
    if (j->space > 100) j->space = 100;
    if (j->tape > 100) j->tape = 100;
    if (j->glue > 100) j->glue = 100;
    if (j->width > 100) j->width = 100;
    j->switch_at %= JS_N;
    for (i = 0; i < sizeof j->rsv; i++)
        j->rsv[i] = 0;
    for (k = 0; k < 2; k++) {
        jt_part_t *p = &j->part[k];
        if (p->n < 1 || p->n > JT_NCHORD)
            p->n = p->n < 1 ? 1 : JT_NCHORD;
        if (p->energy > 4) p->energy = 4;
        if (p->reps < 1 || p->reps > 8) p->reps = p->reps < 1 ? 1 : 8;
        p->var = p->var ? 1 : 0;
        for (i = 0; i < JT_NCHORD; i++) {
            jt_chord_t *c = &p->c[i];
            if ((c->deg & 7) > 6 || ((c->deg >> 4) & 3) == 3 || (c->deg & 0xC8))
                c->deg = 0;
            if (c->qual >= JQ_N) c->qual = 0;
            if (c->beats < 1 || c->beats > 16) c->beats = 4;
            if (c->bass >= JB_N) c->bass = 0;
        }
    }
}

void jt_generate(jt_jam_t *j, uint32_t genre, uint32_t seed, int what)
{
    const genre_t *g = &GENRES[genre % NGENRES];
    int i, n = 0, a, b, tries;
    g_rs = seed * 2654435761u + 12345u;
    grand(1);
    if (what == JG_ALL) {
        jt_jam_default(j);
        j->name[0] = 0;
        name_cat(j->name, NAME_A[grand(sizeof NAME_A / sizeof NAME_A[0])], JT_NAME);
        name_cat(j->name, " ", JT_NAME);
        name_cat(j->name, NAME_B[grand(sizeof NAME_B / sizeof NAME_B[0])], JT_NAME);
        j->genre = (uint8_t)(genre % NGENRES);
        {                                /* the meter: mostly the genre's first */
            int m = 0;
            while (m < 3 && g->meters[m] != END)
                m++;
            i = (int)grand(10);
            j->meter = g->meters[m > 1 && i >= 7 ? (m > 2 && i >= 9 ? 2 : 1) : 0];
        }
        j->bpm10 = (uint16_t)(10 * (g->bpm_lo + (int)grand((uint32_t)(g->bpm_hi - g->bpm_lo + 1))));
        j->seed = (uint8_t)grand(256);
    }
    if (what == JG_ALL || what == JG_PROG) {
        static const uint8_t KEYS_MAJ[] = {0, 2, 4, 5, 7, 9, 10, 3, 8, 11, 1, 6}, KEYS_MIN[] = {4, 9, 2, 11, 6, 0, 7, 5, 1, 3, 10, 8};
        while (n < 12 && g->prog[n])
            n++;
        a = (int)grand((uint32_t)n);
        b = a;
        for (tries = 0; tries < 12 && (b == a || prog_mode(g->prog[b]) != prog_mode(g->prog[a])); tries++)
            b = (int)grand((uint32_t)n);
        j->mode = (uint8_t)prog_mode(g->prog[a]);
        prog_into(&j->part[0], g->prog[a], j->meter);
        prog_into(&j->part[1], g->prog[b], j->meter);
        if (b == a)                      /* one progression only: B is it from its fourth chord, or IV-V */
            prog_into(&j->part[1], "M:4 5 4 5", j->meter);
        j->key = (j->mode == JM_MAJOR || j->mode == JM_LYDIAN || j->mode == JM_MIXO ? KEYS_MAJ : KEYS_MIN)[grand(7)];
    }
    if (what == JG_ALL || what == JG_GROOVE) {
        j->groove = (uint8_t)pick(g->grooves, 5);
        j->bstyle = (uint8_t)pick(g->bass, 5);
        j->cstyle = (uint8_t)pick(g->comp, 5);
        j->sstyle = (uint8_t)pick(g->synth, 5);
        j->swing = (uint8_t)(g->swing_lo + grand((uint32_t)(g->swing_hi - g->swing_lo + 1)));
        if (GROOVES[j->groove].swing)    /* a swung groove: around its own */
            j->swing = (uint8_t)(48 + grand(6));
        j->push = (uint8_t)(grand(4) == 0);
        j->fills = (uint8_t)(grand(3) ? JF_ROUND : JF_8);
        j->part[0].energy = 2;
        j->part[1].energy = (uint8_t)(2 + grand(2));
        j->part[1].var = (uint8_t)(grand(3) != 0);
        j->part[0].reps = j->part[1].reps = 2;
        j->seed = (uint8_t)grand(256);
    }
    if (what == JG_ALL || what == JG_SOUND) {
        j->kit = (uint8_t)pick(g->kits, 4);
        j->patch[0] = (uint8_t)pick(g->pb, 4);
        j->patch[1] = (uint8_t)pick(g->pc, 5);
        j->patch[2] = (uint8_t)pick(g->ps, 5);
        if (j->patch[2] == j->patch[1] && j->sstyle != SY_OFF)
            j->patch[2] = (uint8_t)pick(g->ps, 5);
        j->tape = (uint8_t)(g->tape_lo + grand((uint32_t)(g->tape_hi - g->tape_lo + 1)));
        j->color = (uint8_t)(g->color_lo + grand((uint32_t)(g->color_hi - g->color_lo + 1)));
        j->rev = g->rev;
        j->space = g->space;
        j->level[JP_SYNTH] = (uint8_t)(j->sstyle == SY_PAD ? 48 : j->sstyle == SY_BELLS ? 50 : 58);
    }
    jt_jam_sanitize(j);
}
