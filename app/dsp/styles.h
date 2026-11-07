/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the players' styles, written as step patterns. A bar is built from segments of three families, so every
 * style plays in every time signature:
 *   S  16 steps: a bar of 4/4 in sixteenths (2/4 is its first half)
 *   T  12 steps: a bar of 3/4 in sixteenths
 *   C  12 steps: a bar of 6/8 in sixteenths (two dotted-quarter beats, an eighth every two steps)
 * 5/4 is T then the second half of S; 7/8 is the first half of S then the first half of C; 12/8 is C twice.
 *
 * Drums, a row per instrument (K kick, S snare, R rim or clap, H closed hat, O open hat, Y ride, C crash, T toms,
 * P percussion):  . nothing   X accent   x hit   o soft   g ghost   ? a hit some of the time (more with ENERGY)
 *                 , a ghost some of the time.   Toms: h m l high, mid, low (H M L accented).
 * Bass:   R root (the chord's bass note)  5 fifth  8 octave up  3 the chord's third  7 its seventh  6 sixth
 *         L the root an octave down  A an approach to the next chord  P a passing note towards it
 *         d a dead note   - hold   . rest
 * Chords: X x a strum or chord (accented)  u an upstroke  s a short stab  b the voicing's bass note alone
 *         1..6 one voice (1 the lowest: fingerpicking, arpeggios)   - hold   . rest
 * Part of engine.c. */

enum { DR_K, DR_S, DR_R, DR_H, DR_O, DR_Y, DR_C, DR_T, DR_P, DR_N };
enum { FAM_S, FAM_T, FAM_C };
enum { FILL_TOMS, FILL_SNARE, FILL_LIGHT, FILL_DROP, FILL_DISCO };

typedef struct {
    const char *name;
    uint8_t swing8;                      /* swing on the eighths (1) or the sixteenths (0) */
    uint8_t swing;                       /* its own swing, 0..100 (50 straight... see seq.h: 0 straight) */
    uint8_t fill;                        /* FILL_* */
    uint8_t ride_top;                    /* 1: at full energy the hats move to the ride */
    const char *s[DR_N], *s2[DR_N], *t[DR_N], *c[DR_N];   /* S, S's alternative, T, C (0: derived) */
} groove_t;

#define R9(k, s, r, h, o, y, c, t, p) {k, s, r, h, o, y, c, t, p}
static const groove_t GROOVES[] = {
    {"STRAIGHT 8", 0, 0, FILL_TOMS, 1,
     R9("x.....x.x.....?.", "....x.......x...", 0, "X.x.x.x.X.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x.x.x...x.", "....x.......x.g.", 0, "X.x.x.x.X.x.x...", "..............x.", 0, 0, 0, 0),
     R9("x.......?...", "....x...x...", 0, "X.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x...?.......", "......x.....", 0, "X.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"DREAMY", 0, 0, FILL_LIGHT, 0,
     R9("x.........x.....", 0, "........x.......", "..o...o...o...o.", 0, 0, 0, 0, "ogogogogogogogog"),
     R9("x.....?...x.....", 0, "........x.....,.", "o.o.o.o.o.o.o.o.", 0, 0, 0, 0, "xgogxgogxgogxgog"),
     R9("x.....?.....", 0, "........x...", "..o...o...o.", 0, 0, 0, 0, "ogogogogogog"),
     R9("x.....?.....", 0, "......x.....", "o.o.o.o.o.o.", 0, 0, 0, 0, "ogogogogogog")},
    {"DISCO", 0, 0, FILL_DISCO, 0,
     R9("x...x...x...x...", "....x.......x...", 0, "xg.gxg.gxg.gxg.g", "..x...x...x...x.", 0, 0, 0, "....o.......o..."),
     R9("x...x...x...x...", "....x.......x.g.", "....x.......x...", "xgxgxgxgxgxgxgxg", "..x...x...x...x.", 0, 0, 0, "o.o.o.o.o.o.o.o."),
     R9("x...x...x...", "....x...x...", 0, "xg.gxg.gxg.g", "..x...x...x.", 0, 0, 0, 0),
     R9("x.....x.....", "......x.....", 0, "x.x.x.x.x.x.", "..x.....x...", 0, 0, 0, 0)},
    {"HALF TIME", 0, 0, FILL_TOMS, 1,
     R9("x......x..x.....", "........x.......", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x...x..x..", "........x.....g.", 0, "x.x.x.x.x.x.x...", "..............x.", 0, 0, 0, 0),
     R9("x.......x...", "........x...", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....?.....", "......x.....", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"BOSSA", 0, 0, FILL_LIGHT, 0,
     R9("o..oo..oo..oo..o", 0, "x..x..x...x..x..", "o.o.o.o.o.o.o.o.", 0, 0, 0, 0, "gogogogogogogogo"),
     R9("o..oo..oo..oo..o", 0, "x..x...x..x.x...", "o.o.o.o.o.o.o.o.", 0, 0, 0, 0, "gogogogogogogogo"),
     R9("o..oo..oo..o", 0, "x..x..x.....", "o.o.o.o.o.o.", 0, 0, 0, 0, "gogogogogogo"),
     R9("o....oo....o", 0, "x..x..x.....", "o.o.o.o.o.o.", 0, 0, 0, 0, "gogogogogogo")},
    {"SHUFFLE", 1, 66, FILL_SNARE, 1,
     R9("x.....x.x.......", "....x.......x...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x.x...?...", "....x...,...x..g", 0, "x.x.x.x.x.x.x.x.", "..............x.", 0, 0, 0, 0),
     R9("x.......x...", "....x.......", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x.....", "......x.....", 0, "x.xx.xx.xx.x", 0, 0, 0, 0, 0)},
    {"FUNK 16", 0, 15, FILL_SNARE, 0,
     R9("x..x..x...x..x..", "....x..g.g..x..g", 0, "Xgxgxgxgxgxgxgxg", 0, 0, 0, 0, 0),
     R9("x..x..x...x?.x..", "....x..g.g..x.gg", 0, "Xgxgxgx.Xgxgxgx.", ".......x.......x", 0, 0, 0, 0),
     R9("x..x..x...x.", "....x..g.g..", 0, "Xgxgxgxgxgxg", 0, 0, 0, 0, 0),
     R9("x..x...x....", "......x..g..", 0, "xgxgxgxgxgxg", 0, 0, 0, 0, 0)},
    {"BALLAD", 0, 0, FILL_LIGHT, 1,
     R9("x.......x.....?.", 0, "....x.......x...", "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.......x.x...?.", "....x.......x...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x...........", 0, "....x...x...", "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....?.....", 0, "......x.....", "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"ROCK", 0, 0, FILL_TOMS, 1,
     R9("x.....x.x.......", "....X.......X...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x.x.x.....", "....X.......X...", 0, 0, 0, "X.x.X.x.X.x.X.x.", 0, 0, 0),
     R9("x.......x...", "....X.......", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x...x.......", "......X.....", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"MOTORIK", 0, 0, FILL_SNARE, 0,
     R9("x.x.....x.x.....", "....x.......x...", 0, "xgxgxgxgxgxgxgxg", 0, 0, 0, 0, 0),
     R9("x.x.....x.x.....", "....x.......x...", 0, "xgxgxgxgxgxgxgxg", "......x.......x.", 0, 0, 0, "o.o.o.o.o.o.o.o."),
     R9("x.x.....x...", "....x.......", 0, "xgxgxgxgxgxg", 0, 0, 0, 0, 0),
     R9("x.x...x.....", "......x.....", 0, "xgxgxgxgxgxg", 0, 0, 0, 0, 0)},
    {"STOMP", 0, 0, FILL_TOMS, 0,
     R9("x...x...x...x...", 0, "....x.......x...", 0, 0, 0, 0, 0, "..x...x...x...x."),
     R9("x...x...x...x...", "....x.......x...", "....x.......x...", 0, 0, 0, 0, "l.l.l.l.l.l.l.l.", "..x...x...x...x."),
     R9("x...x...x...", 0, "....x...x...", 0, 0, 0, 0, 0, "..x...x...x."),
     R9("x.....x.....", 0, "......x.....", 0, 0, 0, 0, 0, "..x.x...x.x.")},
    {"TOMS", 0, 0, FILL_TOMS, 0,
     R9("x.......x.......", 0, "....x.......x...", 0, 0, 0, 0, "L.l.L.l.L.l.L.lm", "o.o.o.o.o.o.o.o."),
     R9("x.....x.x.......", "....x.......x...", 0, 0, 0, 0, 0, "L.lmL.l.L.lmL.lm", "xoxoxoxoxoxoxoxo"),
     R9("x.......x...", 0, "....x.......", 0, 0, 0, 0, "L.l.L.l.L.l.", "o.o.o.o.o.o."),
     R9("x.....x.....", 0, "......x.....", 0, 0, 0, 0, "L.l.l.L.l.l.", "o.o.o.o.o.o.")},
    {"TRAIN", 0, 10, FILL_SNARE, 0,
     R9("x.......x.......", "gogXgogogogXgogo", 0, 0, 0, 0, 0, 0, 0),
     R9("x.......x.......", "gogXgogogogXgogX", 0, "....x.......x...", 0, 0, 0, 0, 0),
     R9("x...........", "gogXgogXgogo", 0, 0, 0, 0, 0, 0, 0),
     R9("x.....x.....", "gogogoXogogo", 0, 0, 0, 0, 0, 0, 0)},
    {"BOOM BAP", 0, 58, FILL_DROP, 0,
     R9("x......x..x.....", "....x.......x...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x......x..x..?..", "....x.......x..,", 0, "x.x.x.x.x.x.x.x.", "......x.........", 0, 0, 0, 0),
     R9("x......x..x.", "....x.......", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x....x......", "......x.....", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"LO-FI", 0, 62, FILL_DROP, 0,
     R9("x.....x...x.....", "....o.......o...", 0, "o.o.o.o.o.o.o.o.", 0, 0, 0, 0, "....g.......g..."),
     R9("x.....x...x..?..", "....o..,....o...", 0, "o.ogo.o.o.ogo.o.", 0, 0, 0, 0, "....g.......g..."),
     R9("x.....x.....", "....o.......", 0, "o.o.o.o.o.o.", 0, 0, 0, 0, 0),
     R9("x.....?.....", "......o.....", 0, "o.o.o.o.o.o.", 0, 0, 0, 0, 0)},
    {"ONE DROP", 0, 20, FILL_LIGHT, 0,
     R9("........x.......", 0, "........x.......", "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("........x.......", 0, "........x.....,.", "x.x.x.x.x.x.x.x.", "......x.........", 0, 0, 0, "..o...o...o...o."),
     R9("........x...", 0, "........x...", "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("......x.....", 0, "......x.....", "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"JAZZ RIDE", 1, 66, FILL_LIGHT, 0,
     R9("g...g...g...g...", "......,.....,...", 0, "....o.......o...", 0, "x...x.x.x...x.x.", 0, 0, 0),
     R9("g...g...g...g...", "..,...,...,...,.", 0, "....o.......o...", 0, "x...x.x.x...x.x.", 0, 0, 0),
     R9("g...g...g...", "......,.....", 0, "....o...o...", 0, "x...x.x.x...", 0, 0, 0),
     R9("g.....g.....", "....,.....,.", 0, "......o.....", 0, "x.xx.xx.xx.x", 0, 0, 0)},
    {"MACHINE", 0, 0, FILL_DROP, 0,
     R9("x.....x.x.......", 0, "....x.......x...", "xgxgxgxgxgxgxgxg", 0, 0, 0, 0, "..x.......x....."),
     R9("x.....x.x.x.....", 0, "....x.......x...", "xgxgxgxgxgxgxgxg", "..............x.", 0, 0, 0, "..x...x...x....."),
     R9("x.......x...", 0, "....x.......", "xgxgxgxgxgxg", 0, 0, 0, 0, "..x.......x."),
     R9("x.....x.....", 0, "......x.....", "xgxgxgxgxgxg", 0, 0, 0, 0, "..x.....x...")},
    {"PUNK", 0, 0, FILL_TOMS, 1,
     R9("x...x...x...x...", "..x...x...x...x.", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.x.x...x.x.x...", "....X.......X...", 0, 0, 0, "X.x.X.x.X.x.X.x.", 0, 0, 0),
     R9("x...x...x...", "..x...x...x.", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.....x.....", "..x.x...x.x.", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
    {"WALTZ", 0, 0, FILL_LIGHT, 0,
     R9("x.......x.......", "....o...o...o...", 0, "x.x.x.x.x.x.x.x.", 0, 0, 0, 0, 0),
     R9("x.......x.......", "....x...x...x...", 0, 0, 0, "x.x.x.x.x.x.x.x.", 0, 0, 0),
     R9("x...........", "....o...o...", 0, "x...x...x...", 0, 0, 0, 0, "....g...g..."),
     R9("x.....x.....", "..o.o...o.o.", 0, "x.x.x.x.x.x.", 0, 0, 0, 0, 0)},
};
#define NGROOVES (sizeof(GROOVES) / sizeof(GROOVES[0]))

/* ---------------------------------------------------------------- bass */
typedef struct {
    const char *name;
    uint8_t gate;                        /* % of a note's written length it sounds */
    uint8_t approach;                    /* 1: the last note before a change of chord leans into the next one */
    uint8_t glide;                       /* 1: notes slide into each other (legato) */
    const char *s, *t, *c;
} bstyle_t;
static const bstyle_t BSTYLES[] = {
    {"ROOT 8THS", 75, 1, 0, "R.R.R.R.R.R.R.R.", "R.R.R.R.R.R.", "R.R.R.R.R.R."},
    {"MELODIC", 92, 1, 0, "R-----5-8---3-A-", "R-----5-8-A-", "R---5-8---A-"},
    {"OCTAVES", 50, 0, 0, "R.8.R.8.R.8.R.8.", "R.8.R.8.R.8.", "R.8.8.R.8.8."},
    {"SPARSE", 95, 0, 0, "R-----------.-5-", "R-------5---", "R-----------"},
    {"TRESILLO", 85, 1, 0, "R-----R-----5-A-", "R-----R---A-", "R-----5---A-"},
    {"FUNK 16", 45, 1, 0, "R.dR..8dR..R.5A.", "R.dR..8dR.A.", "R..R.8R..5A."},
    {"WALKING", 92, 1, 0, "R...P...P...A...", "R...P...A...", "R.....A....."},
    {"BOOGIE", 70, 0, 0, "R.3.5.6.7.6.5.3.", "R.3.5.6.5.3.", "R.3.5.6.5.3."},
    {"TWO BEAT", 60, 1, 0, "R.......5.....A.", "R...5...5...", "R.....5....."},
    {"WHOLE", 98, 0, 1, "R---------------", "R-----------", "R-----------"},
    {"HIP HOP", 90, 0, 1, "R------.R-R-----", "R------.R-R-", "R-----.R----"},
    {"ONE DROP", 70, 0, 0, "....R-R-..5-8-..", "....R-R-5-..", "..R-R-..5-8-"},
    {"MOTOWN", 60, 1, 0, "R-.8.5R-.R.8..A.", "R-.8.5R-.A..", "R-.8.5R-.A.."},
    {"16THS", 45, 0, 0, "RRRRRRRRRRRRRRRR", "RRRRRRRRRRRR", "RRRRRRRRRRRR"},
    {"PEDAL 1", 80, 0, 0, "L.L.L.L.L.L.L.L.", "L.L.L.L.L.L.", "L.L.L.L.L.L."},
    {"DISCO RUN", 55, 1, 0, "R.8.R.8.5.8.A.P.", "R.8.R.8.A.P.", "R.8.5.8.A.P."},
};
#define NBSTYLES (sizeof(BSTYLES) / sizeof(BSTYLES[0]))

/* ---------------------------------------------------------------- chords */
typedef struct {
    const char *name;
    uint8_t gate;                        /* % of a hit's written length */
    uint8_t tie;                         /* 1: a hit on the same chord as the last continues it (a pad, a held chord) */
    const char *s, *t, *c;
} cstyle_t;
static const cstyle_t CSTYLES[] = {
    {"HOLD", 100, 1, "x---------------", "x-----------", "x-----------"},
    {"HALVES", 96, 0, "x-------x-------", "x-------x---", "x-----x-----"},
    {"QUARTERS", 70, 0, "x-.-x-.-x-.-x-.-", "x-.-x-.-x-.-", "x-.-..x-.-.."},
    {"EIGHTHS", 70, 0, "x.x.x.x.x.x.x.x.", "x.x.x.x.x.x.", "x.x.x.x.x.x."},
    {"STRUM", 95, 0, "X-..x-u-..u-x-u-", "X-..x-u-x-u-", "X-..u-x-..u-"},
    {"FOLK STRUM", 95, 0, "X-u-x-u-X-u-x-u-", "X-u-x-u-x-u-", "X-u-x-X-u-x-"},
    {"OFFBEAT", 40, 0, "..s...s...s...s.", "..s...s...s.", "..s.s...s.s."},
    {"STABS", 35, 0, "s..s..s...s..s..", "s..s..s...s.", "s..s..s..s.."},
    {"BOSSA", 60, 0, "x..x..x...x..x..", "x..x..x...x.", "x..x..x...x."},
    {"SCRATCH", 30, 0, "s.us.us.s.us.usu", "s.us.us.s.us", "s.us.us.s.us"},
    {"FINGERPICK", 100, 0, "1.3.2.4.1.3.2.4.", "1.3.2.4.2.3.", "1.3.4.2.4.3."},
    {"ARPEGGIO", 100, 0, "1.2.3.4.5.4.3.2.", "1.2.3.4.3.2.", "1.2.3.4.3.2."},
    {"TRESILLO", 90, 0, "x-----x-----x---", "x-----x-----", "x-----x-----"},
    {"BOOM-CHICK", 60, 0, "b...x...b...x...", "b...x...x...", "b.....x.x..."},
    {"CHARLESTON", 50, 0, "x.....x.........", "x.....x.....", "x.....x....."},
    {"PIANO POP", 85, 0, "x-------..x-x---", "x-------x-x-", "x-----..x-x-"},
};
#define NCSTYLES (sizeof(CSTYLES) / sizeof(CSTYLES[0]))

/* ---------------------------------------------------------------- synth: the line above it all (seq.h makes it) */
#include "jt_ids.h"
static const char *const SSTYLE_NAME[SY_N] = {"OFF", "PAD", "ARP 8THS", "ARP 16THS", "HOOK", "MELODY", "COUNTER",
                                              "PULSE", "BELLS", "STABS"};
/* the hooks' rhythms: one bar of S (16 steps); 'x' a note, '-' hold, '.' rest; the pitch comes from the contour */
static const char *const HOOK_RHY[] = {
    "x.x.x---..x.x---", "x--.x-x-x---....", "..x-x-x-x---x-..", "x-x-..x-x-x-....",
    "x---x.x.x-------", "..x.x.x.x-..x-..", "x.xx-.x.x---....", "x-----x-x-x-x---",
};
#define NHOOK_RHY (sizeof(HOOK_RHY) / sizeof(HOOK_RHY[0]))

/* ---------------------------------------------------------------- fills: one beat of steps (4), the toms' pitches
 * h m l; the fill is these beats, last beats of the bar */
static const char *const FILL_BEAT[][2] = {   /* {snare row, tom row} */
    {"xxXx", 0}, {"x.xX", 0}, {"gxgX", 0}, {0, "hhmm"}, {0, "mmll"}, {"x...", ".mlL"}, {"x.x.", "..ll"},
    {"Xx..", "..mL"}, {"gggx", 0}, {0, "hmlL"},
};
