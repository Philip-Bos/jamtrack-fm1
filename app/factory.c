/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK FM-1: the factory jams. Each is a key, a tempo, two progressions in Nashville numbers (the same text the
 * screen shows: "1maj7 6m7 2m7 5sus4:2 5:2", lengths in beats, a bar when left out), and the band's styles and
 * sounds. The rest comes from jt_jam_default. Part of app.c. */
#include "jt_ids.h"

typedef struct {
    const char *name;
    uint8_t genre, key, mode, meter;
    uint16_t bpm;                        /* tenths */
    uint8_t groove, bstyle, cstyle, sstyle, kit, pb, pc, ps;
    uint8_t swing, tape, color, push, rev, space, synth_level, ea, eb;
    const char *a, *b;
} factory_t;

/* genres (gen.h's order) */
enum { GN_INDIE, GN_DREAM, GN_BEDROOM, GN_FUNK, GN_IROCK, GN_FOLK, GN_SOUL, GN_CITY, GN_ROCK, GN_BLUES, GN_HIPHOP,
       GN_REGGAE, GN_JAZZ, GN_COUNTRY, GN_SYNTH, GN_BALLAD };
enum { kC, kDb, kD, kEb, kE, kF, kGb, kG, kAb, kA, kBb, kB };

static const factory_t FACTORY[] = {
    /* ---- indie, dream pop, bedroom */
    {"GOLDEN HOUR", GN_INDIE, kD, JM_MAJOR, JT_METER_44, 1040, G_MOTORIK, B_MELODIC, C_STRUM, SY_HOOK, K_VINTAGE,
     P_FINGER, P_CLEAN, P_SOFTLEAD, 50, 30, 1, 0, 30, 40, 52, 2, 3,
     "1maj7 6m7 4maj7 5sus4:2 5:2", "4maj7 5 3m7 6m7"},
    {"NIGHT SWIM", GN_DREAM, kB, JM_DORIAN, JT_METER_44, 1080, G_DISCO, B_DISCO, C_HOLD, SY_COUNTER, K_VINTAGE,
     P_MUTED, P_RHODES, P_SOFTLEAD, 52, 45, 2, 0, 40, 55, 50, 1, 2,
     "1m7 47", "3maj7 2m7 1m7 47"},
    {"SUNDAY TAPE", GN_BEDROOM, kF, JM_MAJOR, JT_METER_44, 920, G_BOSSA, B_MELODIC, C_BOSSA, SY_MELODY, K_LOFI,
     P_FINGER, P_NYLON, P_FLUTE, 56, 55, 2, 0, 35, 45, 48, 2, 2,
     "2m7 57 1maj7 6m7", "4maj7 3m7 2m9 513"},
    {"PASTEL", GN_DREAM, kA, JM_MAJOR, JT_METER_44, 840, G_DREAMY, B_SPARSE, C_ARP, SY_PAD, K_VINTAGE,
     P_MUTED, P_CLEAN, P_JUNO, 50, 40, 2, 0, 55, 70, 42, 2, 3,
     "1maj7 3m7 4maj7 4m6", "6m9 4maj7 1maj7 5sus4"},
    {"AFTERGLOW", GN_DREAM, kB, JM_MINOR, JT_METER_44, 980, G_DREAMY, B_MELODIC, C_HOLD, SY_COUNTER, K_VINTAGE,
     P_MUTED, P_CLEAN, P_SOFTLEAD, 52, 50, 3, 0, 45, 60, 52, 2, 3,
     "1m9 49 1m9 49", "6maj7 7 3maj7 5m7"},
    {"VELVET HAZE", GN_DREAM, kD, JM_MAJOR, JT_METER_44, 760, G_HALF, B_SPARSE, C_STRUM, SY_BELLS, K_VINTAGE,
     P_FINGER, P_CLEAN, P_VIBES, 50, 45, 1, 0, 60, 70, 46, 1, 2,
     "1maj7:8 4maj7:8", "6m7 5 4maj7 4m6"},
    {"POLAROID", GN_BEDROOM, kC, JM_MAJOR, JT_METER_44, 960, G_HALF, B_MELODIC, C_HOLD, SY_MELODY, K_LOFI,
     P_FINGER, P_WURLI, P_VIBES, 54, 50, 1, 0, 35, 45, 50, 2, 3,
     "1maj7 1m7 4maj7 4m7", "6m7 2m7 5sus4 5"},
    {"CHERRY COLA", GN_BEDROOM, kG, JM_MAJOR, JT_METER_44, 880, G_MACHINE, B_MELODIC, C_STRUM, SY_MELODY, K_BOX,
     P_FINGER, P_CLEAN, P_SOFTLEAD, 50, 50, 1, 0, 35, 45, 50, 2, 3,
     "1maj7 3m7 6m7 4maj7", "2m7 3m7 4maj7 5sus4"},
    {"PAPER PLANES", GN_INDIE, kE, JM_MAJOR, JT_METER_44, 1160, G_STRAIGHT, B_MELODIC, C_PIANO, SY_HOOK, K_STUDIO,
     P_FINGER, P_RHODES, P_PLUCK, 50, 20, 1, 1, 30, 40, 52, 2, 3,
     "1 5/3 6m 4", "4 1/3 2m 5"},
    {"SLOW BLOOM", GN_BALLAD, kEb, JM_MAJOR, JT_METER_68, 780, G_BALLAD, B_WHOLE, C_ARP, SY_PAD, K_BRUSH,
     P_FINGER, P_FELT, P_STRINGS, 50, 25, 1, 0, 50, 65, 44, 2, 3,
     "1 3m 4 4m", "6m 4 1 5"},
    {"FERNWEH", GN_FOLK, kG, JM_MAJOR, JT_METER_34, 1320, G_WALTZ, B_TWO, C_PICK, SY_COUNTER, K_BRUSH,
     P_UPRIGHT, P_ACOUSTIC, P_FLUTE, 50, 15, 0, 0, 35, 45, 50, 2, 3,
     "1 5/3 6m 4", "4 1 5 5"},
    {"STATIC LOVE", GN_SYNTH, kA, JM_MINOR, JT_METER_44, 1120, G_MACHINE, B_16, C_HOLD, SY_ARP16, K_LM,
     P_SYNTHB, P_JUNO, P_PLUCK, 50, 20, 1, 0, 35, 55, 48, 2, 3,
     "1m 6 3 7", "4m 6 7 7"},
    {"ROOFTOP", GN_FUNK, kDb, JM_MINOR, JT_METER_44, 1200, G_DISCO, B_DISCO, C_OFFBEAT, SY_ARP16, K_LM,
     P_PICK, P_WURLI, P_BELLS, 50, 15, 2, 0, 25, 35, 46, 2, 3,
     "1m7 4m7", "6maj7 7 1m7:8"},
    {"MIRROR BALL", GN_FUNK, kE, JM_MINOR, JT_METER_44, 1180, G_DISCO, B_OCT, C_SCRATCH, SY_STABS, K_STUDIO,
     P_PICK, P_CLEAN, P_STRINGS, 52, 15, 2, 0, 22, 30, 50, 2, 3,
     "1m7 4m7 7 3maj7", "6maj7 7 1m7:8"},
    {"SLEEPWALK", GN_IROCK, kA, JM_MIXO, JT_METER_44, 1320, G_MOTORIK, B_ROOT8, C_EIGHTHS, SY_ARP8, K_VINTAGE,
     P_PICK, P_ORGAN, P_PLUCK, 50, 25, 0, 0, 30, 45, 48, 2, 3,
     "1 7 4 1", "4:8 1 7"},
    {"COASTLINE", GN_IROCK, kG, JM_MAJOR, JT_METER_44, 1280, G_STRAIGHT, B_ROOT8, C_EIGHTHS, SY_COUNTER, K_ROOM,
     P_PICK, P_JANGLE, P_SQUARE, 50, 20, 0, 0, 30, 45, 46, 2, 4,
     "1 5 6m 4", "4 5 3m 6m"},
    {"SECOND WIND", GN_IROCK, kE, JM_MINOR, JT_METER_44, 760, G_HALF, B_ROOT8, C_STRUM, SY_PAD, K_ROOM,
     P_PICK, P_JANGLE, P_STRINGS, 50, 20, 0, 0, 40, 60, 44, 2, 4,
     "1m 6 3 7", "4m 6 3 7"},
    {"SUNROOM", GN_FOLK, kC, JM_MAJOR, JT_METER_44, 1000, G_STOMP, B_TWO, C_FOLK, SY_OFF, K_CAJON,
     P_UPRIGHT, P_ACOUSTIC, P_FLUTE, 50, 10, 0, 0, 30, 40, 40, 2, 3,
     "1 5 6m 4", "4 1/3 2m 5"},
    {"GLASS ROOMS", GN_CITY, kF, JM_MAJOR, JT_METER_44, 1120, G_FUNK, B_FUNK, C_STABS, SY_HOOK, K_STUDIO,
     P_PICK, P_RHODES, P_BRASS, 52, 15, 3, 0, 28, 35, 48, 2, 3,
     "4maj7 57 3m7 6m7", "2m7 3m7 4maj7 57"},
    {"LAZY RIVER", GN_HIPHOP, kBb, JM_MAJOR, JT_METER_44, 820, G_LOFI, B_MELODIC, C_HOLD, SY_BELLS, K_LOFI,
     P_FINGER, P_RHODES, P_VIBES, 50, 70, 3, 0, 30, 40, 46, 2, 3,
     "2m9 57 1maj9 6m9", "4maj9 3m7 2m9 513"},
    {"MOTEL POOL", GN_DREAM, kAb, JM_MAJOR, JT_METER_44, 900, G_BOSSA, B_TRES, C_HOLD, SY_COUNTER, K_LOFI,
     P_FRETLESS, P_JUNO, P_FLUTE, 54, 60, 2, 0, 50, 60, 44, 2, 3,
     "1maj7 b7 4maj7 4m6", "6m7 2m7 5sus4 5"},
    {"PALE BLUE", GN_INDIE, kBb, JM_MAJOR, JT_METER_44, 1100, G_DISCO, B_OCT, C_STRUM, SY_PAD, K_VINTAGE,
     P_FINGER, P_JANGLE, P_JUNO, 50, 30, 1, 0, 35, 50, 44, 1, 3,
     "1 3m 6m 4", "2m 4 1/3 5"},
    {"TRESILLO", GN_INDIE, kE, JM_MINOR, JT_METER_44, 1000, G_STRAIGHT, B_TRES, C_TRES, SY_HOOK, K_STUDIO,
     P_FINGER, P_POLY, P_SOFTLEAD, 50, 25, 1, 1, 30, 40, 50, 2, 3,
     "1m 6 7 1m", "4m 1m 6 7"},
    /* ---- the rest of the world */
    {"TWELVE BAR", GN_BLUES, kE, JM_MAJOR, JT_METER_44, 1000, G_SHUFFLE, B_BOOGIE, C_QUARTERS, SY_OFF, K_VINTAGE,
     P_UPRIGHT, P_ORGAN, P_ORGAN, 50, 20, 2, 0, 30, 45, 40, 2, 3,
     "17:16 47:8 17:8 57:4 47:4 17:4 57:4", "47:8 17:8 57:4 47:4 17:8"},
    {"SLOW BURN", GN_BLUES, kA, JM_MINOR, JT_METER_128, 880, G_SHUFFLE, B_WALK, C_HOLD, SY_COUNTER, K_BRUSH,
     P_UPRIGHT, P_WURLI, P_ORGAN, 50, 20, 2, 0, 35, 50, 44, 2, 3,
     "1m7:16 4m7:8 1m7:8 67:4 57:4 1m7:4 57:4", "4m7:8 1m7:8 67:4 57:4 1m7:8"},
    {"BOOM BAP SUNDAY", GN_HIPHOP, kD, JM_MINOR, JT_METER_44, 880, G_BOOMBAP, B_HIPHOP, C_HOLD, SY_MELODY, K_LOFI,
     P_SUB, P_FELT, P_VIBES, 50, 55, 2, 0, 25, 35, 46, 2, 3,
     "1m7 6maj7", "4m7 5m7 6maj7 7"},
    {"ONE LOVE DROP", GN_REGGAE, kG, JM_MAJOR, JT_METER_44, 760, G_ONEDROP, B_DROP, C_OFFBEAT, SY_OFF, K_STUDIO,
     P_SUB, P_ORGAN, P_ORGAN, 54, 20, 0, 0, 35, 45, 40, 2, 3,
     "1 4", "5 4 1:8"},
    {"BLUE NOTES", GN_JAZZ, kBb, JM_MAJOR, JT_METER_44, 1400, G_JAZZ, B_WALK, C_CHARL, SY_OFF, K_BRUSH,
     P_UPRIGHT, P_RHODES, P_VIBES, 50, 10, 2, 0, 30, 45, 40, 2, 3,
     "2m7 57 1maj7:8", "1maj7 6m7 2m7 57"},
    {"BOSSA SUNSET", GN_JAZZ, kF, JM_MAJOR, JT_METER_44, 1240, G_BOSSA, B_TWO, C_BOSSA, SY_MELODY, K_BRUSH,
     P_UPRIGHT, P_NYLON, P_FLUTE, 50, 20, 2, 0, 30, 40, 46, 2, 2,
     "1maj7:8 2m7 57", "3m7 67 2m7 57"},
    {"TRAIN SONG", GN_COUNTRY, kA, JM_MAJOR, JT_METER_44, 1160, G_TRAIN, B_TWO, C_BOOM, SY_OFF, K_BRUSH,
     P_UPRIGHT, P_ACOUSTIC, P_SOFTLEAD, 50, 10, 0, 0, 25, 35, 40, 2, 3,
     "1 4 1 5", "4 1 5 1"},
    {"HIGHWAY", GN_ROCK, kD, JM_MIXO, JT_METER_44, 1400, G_ROCK, B_ROOT8, C_EIGHTHS, SY_OFF, K_ROCK,
     P_PICK, P_JANGLE, P_ORGAN, 50, 15, 0, 0, 30, 45, 40, 2, 4,
     "15 75 45 15", "45 45 15 75"},
    {"GARAGE", GN_ROCK, kA, JM_MAJOR, JT_METER_44, 1680, G_PUNK, B_ROOT8, C_EIGHTHS, SY_OFF, K_ROCK,
     P_PICK, P_JANGLE, P_ORGAN, 50, 20, 0, 0, 25, 35, 40, 3, 4,
     "15 45 55 45", "65 45 15 55"},
    {"NEO SOUL", GN_SOUL, kEb, JM_MAJOR, JT_METER_44, 840, G_BOOMBAP, B_MELODIC, C_CHARL, SY_COUNTER, K_VINTAGE,
     P_FRETLESS, P_RHODES, P_STRINGS, 50, 30, 3, 0, 30, 40, 46, 2, 3,
     "4maj9 3m7 2m9 1maj9", "2m9 513 1maj9 1maj9"},
    {"CAMPFIRE", GN_FOLK, kD, JM_MAJOR, JT_METER_68, 1100, G_TOMS, B_ROOT8, C_FOLK, SY_OFF, K_CAJON,
     P_UPRIGHT, P_ACOUSTIC, P_FLUTE, 50, 10, 0, 0, 30, 40, 40, 2, 3,
     "1 4 1 5", "6m 4 1 5"},
    {"LOW TIDE", GN_BALLAD, kC, JM_MAJOR, JT_METER_44, 720, G_BALLAD, B_WHOLE, C_ARP, SY_PAD, K_STUDIO,
     P_FINGER, P_FELT, P_STRINGS, 50, 15, 1, 0, 50, 65, 42, 2, 3,
     "1 5/3 6m 4", "4 5 3m 6m"},
    {"SEVENTH HEAVEN", GN_IROCK, kE, JM_MINOR, JT_METER_78, 1200, G_STRAIGHT, B_ROOT8, C_EIGHTHS, SY_ARP8, K_STUDIO,
     P_PICK, P_CLEAN, P_PLUCK, 50, 20, 0, 0, 30, 45, 46, 2, 3,
     "1m 7 6 7", "4m 6 3 7"},
    {"FIVE ACROSS", GN_INDIE, kD, JM_DORIAN, JT_METER_54, 1320, G_STRAIGHT, B_MELODIC, C_STRUM, SY_COUNTER, K_VINTAGE,
     P_FINGER, P_CLEAN, P_SOFTLEAD, 50, 30, 1, 0, 35, 45, 48, 2, 3,
     "1m7 47", "3maj7 47"},
    {"PULSE CITY", GN_SYNTH, kF, JM_MINOR, JT_METER_44, 1040, G_MACHINE, B_OCT, C_STABS, SY_PULSE, K_808,
     P_MOOG, P_POLY, P_SQUARE, 50, 15, 1, 0, 30, 50, 44, 2, 3,
     "1m 6 3 7", "6 7 1m:8"},
};
#define NFACTORY ((int)(sizeof(FACTORY) / sizeof(FACTORY[0])))

static void factory_jam(int i, jt_jam_t *j)
{
    const factory_t *f = &FACTORY[i % NFACTORY];
    int bb, k;
    jt_jam_default(j);
    j->name[0] = 0;
    text_cpy(j->name, f->name, JT_NAME);
    j->genre = f->genre;
    j->key = f->key;
    j->mode = f->mode;
    j->meter = f->meter;
    j->bpm10 = f->bpm;
    j->groove = f->groove;
    j->bstyle = f->bstyle;
    j->cstyle = f->cstyle;
    j->sstyle = f->sstyle;
    j->kit = f->kit;
    j->patch[0] = f->pb;
    j->patch[1] = f->pc;
    j->patch[2] = f->ps;
    j->swing = f->swing;
    j->tape = f->tape;
    j->color = f->color;
    j->push = f->push;
    j->rev = f->rev;
    j->space = f->space;
    j->level[JP_SYNTH] = f->synth_level;
    j->part[0].energy = f->ea;
    j->part[1].energy = f->eb;
    j->part[1].var = f->eb > f->ea;
    j->seed = (uint8_t)(i * 37 + 11);
    bb = jt_bar_beats(j->meter);
    if (jt_parse_prog(f->a, &j->part[0], bb))
        jt_parse_prog("1 4 5 1", &j->part[0], bb);
    if (jt_parse_prog(f->b, &j->part[1], bb))
        jt_parse_prog("4 5 1 1", &j->part[1], bb);
    if (j->meter == JT_METER_128)        /* (12/8's beats are dotted quarters, four to the bar, as written) */
        ;
    for (k = 0; k < 2; k++)
        if (j->meter != JT_METER_44 && j->meter != JT_METER_128) {   /* lengths written in beats of 4/4 bars */
            int c;
            for (c = 0; c < j->part[k].n; c++) {
                uint32_t b = j->part[k].c[c].beats;
                if (b != (uint32_t)bb && b % 4u == 0)
                    j->part[k].c[c].beats = (uint8_t)(b / 4u * (uint32_t)bb > 16u ? 16u : b / 4u * (uint32_t)bb);
            }
        }
    jt_jam_sanitize(j);
}
