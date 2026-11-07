/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: the sequencer. Time is counted in MIDI clock ticks (24 a quarter note); a step is a sixteenth (6 ticks).
 * On our own time the tick position runs at the tempo; following a clock it runs at the clock's measured pace but
 * never past the next tick that has not come in yet, so every step lands on the tick it belongs to (a block after it
 * came in, the same for every tick) and a clock that speeds up, slows down or stops is followed at once.
 *
 * At every step the players look at the bar's patterns (styles.h) and the chord under the step, and put their notes
 * in the event queue, each with its own small delay: swing, the strum's strings, the human feel. Part of engine.c. */

/* ---------------------------------------------------------------- meters: beats (in steps) and pattern segments */
typedef struct {
    const char *name;
    uint8_t nbeats;
    uint8_t beat[6];                     /* steps in each beat */
    uint8_t nseg;
    struct { uint8_t fam, start, len; } seg[2];
} meter_t;
static const meter_t METERS[JT_METER_N] = {
    {"4/4", 4, {4, 4, 4, 4}, 1, {{FAM_S, 0, 16}}},
    {"3/4", 3, {4, 4, 4}, 1, {{FAM_T, 0, 12}}},
    {"6/8", 2, {6, 6}, 1, {{FAM_C, 0, 12}}},
    {"12/8", 4, {6, 6, 6, 6}, 2, {{FAM_C, 0, 12}, {FAM_C, 0, 12}}},
    {"2/4", 2, {4, 4}, 1, {{FAM_S, 0, 8}}},
    {"5/4", 5, {4, 4, 4, 4, 4}, 2, {{FAM_T, 0, 12}, {FAM_S, 8, 8}}},
    {"7/8", 3, {4, 4, 6}, 2, {{FAM_S, 0, 8}, {FAM_C, 0, 6}}},
};
#define MAX_BAR 24

int jt_bar_beats(int m) { return METERS[m < JT_METER_N ? m : 0].nbeats; }
int jt_beat_steps(int m, int b)
{
    const meter_t *M = &METERS[m < JT_METER_N ? m : 0];
    return M->beat[(b % M->nbeats + M->nbeats) % M->nbeats];
}
static int bar_steps(int m)
{
    const meter_t *M = &METERS[m < JT_METER_N ? m : 0];
    int i, n = 0;
    for (i = 0; i < M->nbeats; i++)
        n += M->beat[i];
    return n;
}
int jt_part_beats(const jt_part_t *p)
{
    int i, n = 0;
    for (i = 0; i < p->n && i < (int)JT_NCHORD; i++)
        n += p->c[i].beats;
    return n;
}

/* the character at bar step s of a three-family pattern set (0: none); for a family not written, S taken instead */
static char pat_at(int meter, const char *S, const char *T, const char *C, int s)
{
    const meter_t *M = &METERS[meter < JT_METER_N ? meter : 0];
    int k, at = s;
    for (k = 0; k < M->nseg; k++) {
        if (at < M->seg[k].len) {
            int fam = M->seg[k].fam, i = M->seg[k].start + at;
            const char *p = fam == FAM_S ? S : fam == FAM_T ? T : C;
            if (!p && S) {               /* derived from S: 3/4 is its first three beats, 6/8 its eighths spread */
                static const uint8_t C2S[12] = {0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15};
                p = S;
                if (fam == FAM_C)
                    i = C2S[i % 12];
            }
            if (!p)
                return 0;
            return p[i] ? p[i] : '.';
        }
        at -= M->seg[k].len;
    }
    return 0;
}

/* ---------------------------------------------------------------- events */
enum { EV_DRUM, EV_NOTE, EV_CLICK };
typedef struct {
    uint32_t due;                        /* sample time */
    uint32_t gate;                       /* samples held */
    uint8_t type, part, note, vel;       /* EV_DRUM: note is the drum voice, part the pitch + 64 (semitones) */
    int8_t pan;                          /* -100..100 */
    uint8_t tie;                         /* 1: continue a voice on this note if it is still held */
} event_t;
#define NEV 160
static event_t EV[NEV];
static uint32_t nev;
static uint32_t now_s;                   /* samples since start */

static void ev_push(const event_t *e)
{
    if (nev < NEV)
        EV[nev++] = *e;
}

/* ---------------------------------------------------------------- state */
static jt_jam_t jam;
static jt_settings_t settings = {0, 1, 1, 0, P_SOLO, 0, 90, 0, 75, 1, 25, 0};
static jt_status_t st;

static struct {
    uint8_t playing, ext, ending, end_bar, want_play, want_stop, want_fill, want_restart, ext_wait;
    int8_t want_part;
    float pos;                           /* tick position (from the start of bar 0; negative: counting in) */
    int32_t next_tick;                   /* the next whole tick to act on */
    float rate;                          /* ticks a sample */
    float cap;                           /* following a clock: the position never passes it */
    int32_t rx;                          /* clock ticks in since START */
    float spt;                           /* samples a tick (the clock's, measured) */
    uint32_t last_rx_t;                  /* plat_ticks24 of the last tick in */
    uint32_t clock_ms;                   /* plat_ms of the last tick in */
    uint32_t out_tick;
    uint32_t wait_until;                 /* START: the time the clock's first tick is waited for */
    /* the bar */
    int meter, bsteps, step, beat, beat_start[8];
    uint32_t bar;                        /* bars since PLAY */
    uint8_t drum[MAX_BAR][DR_N];
    uint8_t fill_bar, crash_next;
    /* the progression */
    int part, pstep, ptotal, cstart[JT_NCHORD + 1], nchord, round, part_bars;
    uint8_t queued;
    int8_t qpart;
    /* the players' memory */
    int bass_last, bass_chord, comp_chord, syn_chord, comp_prev[6], ncomp_prev, syn_last, arp_i;
    int comp_v[6], ncomp_v, comp_v_of;   /* the voicing for comp_v_of (a chord index + 16 * part) */
    uint32_t changed;
} sq;

static volatile uint32_t jam_changed_flags;
void jt_jam_changed(uint32_t what) { jam_changed_flags |= what; }

/* ---------------------------------------------------------------- the progression */
static void prog_layout(void)            /* each chord's first step in the part */
{
    const jt_part_t *p = &jam.part[sq.part & 1];
    int i, b = 0, s = 0, n = p->n < 1 ? 1 : p->n > (int)JT_NCHORD ? (int)JT_NCHORD : p->n;
    for (i = 0; i < n; i++) {
        int k, beats = p->c[i].beats < 1 ? 1 : p->c[i].beats > 16 ? 16 : p->c[i].beats;
        sq.cstart[i] = s;
        for (k = 0; k < beats; k++)
            s += jt_beat_steps(sq.meter, b++);
    }
    sq.cstart[n] = s;
    sq.nchord = n;
    sq.ptotal = s < 1 ? 1 : s;
    if (sq.pstep >= sq.ptotal)
        sq.pstep %= sq.ptotal;
}

static int chord_at(int pstep)           /* the chord index at a step of the part */
{
    int i;
    pstep %= sq.ptotal;
    if (pstep < 0)
        pstep += sq.ptotal;
    for (i = sq.nchord - 1; i > 0; i--)
        if (pstep >= sq.cstart[i])
            return i;
    return 0;
}
static int steps_to_change(int pstep)    /* steps until the next chord starts */
{
    int c = chord_at(pstep), e = sq.cstart[c + 1];
    return e - (pstep % sq.ptotal);
}

/* the chord a player plays at this step: with PUSH, a hit in the last eighth before a change takes the next chord */
static int chord_for_hit(int pstep, int *pushed)
{
    int c = chord_at(pstep);
    *pushed = 0;
    if (jam.push && steps_to_change(pstep) <= 2 && sq.nchord > 1) {
        *pushed = 1;
        return (c + 1) % sq.nchord;
    }
    return c;
}

static const jt_chord_t *part_chord(int i) { return &jam.part[sq.part & 1].c[i % (int)JT_NCHORD]; }

/* ---------------------------------------------------------------- timing helpers */
static float samples_per_step(void)
{
    float spt = sq.ext ? sq.spt : 1.0f / (sq.rate > 0.0f ? sq.rate : 0.001f);
    return spt * (float)JT_STEP_TICKS;
}

/* the swing's delay for a step (samples): on the offbeat eighths (swing8) or sixteenths, simple beats only */
static float swing_delay(int step_in_beat, int beat_len, const groove_t *g)
{
    float sw = (float)g->swing + ((float)jam.swing - 50.0f) * 1.3f, d, sps = samples_per_step();
    if (beat_len != 4)
        return 0.0f;
    sw = fm_clampf(sw, 0.0f, 75.0f) * 0.01f;    /* 0.66: triplets */
    d = sw * 0.5f;                       /* the offbeat's delay in units of the swung note */
    if (g->swing8) {                     /* positions in the beat 0, 1, 2, 3 -> warped */
        static const float W[4] = {0, 0.5f, 1.0f, 0.5f};
        return W[step_in_beat] * d * 2.0f * sps;
    }
    return (step_in_beat & 1) ? d * sps : 0.0f;
}

static float human_delay(float scale)    /* samples: a little early or late, around a small fixed delay */
{
    float h = (float)jam.human * 0.01f, d;
    d = 0.004f * (float)JT_SR + rngf() * h * scale * 0.007f * (float)JT_SR;
    return d > 0.0f ? d : 0.0f;          /* (never before the step: a sample time is unsigned) */
}

/* ---------------------------------------------------------------- the drums' bar */
static int drum_vel(char ch, int energy, uint32_t *rs)
{
    static const float GHOST[5] = {0, 0.6f, 1.0f, 1.15f, 1.3f};
    static const float PROB[5] = {0, 0.2f, 0.5f, 0.8f, 1.0f};
    static const int OFF[5] = {-22, -10, 0, 6, 10};
    int v;
    uint32_t r;
    *rs = *rs * 1664525u + 1013904223u;
    r = *rs >> 8;
    switch (ch) {
    case 'X': case 'H': case 'M': case 'L': v = 120; break;
    case 'x': case 'h': case 'm': case 'l': v = 96; break;
    case 'o': v = 68; break;
    case 'g': v = (int)(36.0f * GHOST[energy]); break;
    case '?': v = (float)(r & 0xFFFF) < PROB[energy] * 65536.0f ? 92 : 0; break;
    case ',': v = (float)(r & 0xFFFF) < PROB[energy] * 65536.0f ? (int)(36.0f * GHOST[energy]) : 0; break;
    default: return 0;
    }
    if (!v)
        return 0;
    v += OFF[energy];
    return v < 1 ? 1 : v > 127 ? 127 : v;
}

static char tom_pitch(char ch) { return ch == 'h' || ch == 'H' ? 'h' : ch == 'm' || ch == 'M' ? 'm' : 'l'; }

static void build_bar(void)
{
    const groove_t *g = &GROOVES[jam.groove % NGROOVES];
    const jt_part_t *p = &jam.part[sq.part & 1];
    int energy = p->energy > 4 ? 4 : p->energy, s, r, alt = p->var && g->s2[DR_K];
    uint32_t rs = 0x9E3779B9u * (uint32_t)(jam.seed + 1) + (uint32_t)(sq.part_bars % 4) * 7919u + (uint32_t)sq.part * 31u;
    for (s = 0; s < MAX_BAR; s++)
        for (r = 0; r < DR_N; r++)
            sq.drum[s][r] = 0;
    for (s = 0; s < sq.bsteps; s++) {
        int bi = 0, sb = s, on_beat;
        while (bi < 7 && sb >= jt_beat_steps(sq.meter, bi)) {
            sb -= jt_beat_steps(sq.meter, bi);
            bi++;
        }
        on_beat = sb == 0;
        for (r = 0; r < DR_N; r++) {
            const char *S = alt ? g->s2[r] : g->s[r];
            char ch = pat_at(sq.meter, S, g->t[r], g->c[r], s);
            int v, dst = r;
            if (!ch || ch == '.')
                continue;
            v = drum_vel(ch, energy, &rs);
            if (!v)
                continue;
            if (energy == 0) {           /* the barest: the kick, snare, rim and toms on the beats, nothing else */
                if (r == DR_O || r == DR_P || r == DR_C)
                    continue;
                if ((r == DR_H || r == DR_Y || r == DR_K) && !on_beat)
                    continue;
            } else if (energy == 1) {
                if (r == DR_P)
                    continue;
                if (r == DR_O)
                    dst = DR_H;
            } else if (energy == 4 && g->ride_top && (r == DR_H || r == DR_O))
                dst = DR_Y;
            if (r == DR_T)               /* the toms keep their pitch: h m l as 1 2 3 in the top bits, half the velocity below */
                v = ((v >> 1) & 0x3F) | (tom_pitch(ch) == 'h' ? 0x40 : tom_pitch(ch) == 'm' ? 0x80 : 0xC0);
            if (dst == DR_T)
                sq.drum[s][dst] = (uint8_t)v;
            else if (v > sq.drum[s][dst])
                sq.drum[s][dst] = (uint8_t)v;
        }
    }
    if (sq.fill_bar) {                   /* the fill: the last beat (two at full energy), its own rows */
        int nb = METERS[sq.meter].nbeats, beats = energy >= 3 && nb >= 3 ? 2 : 1, from = 0, b, k, kind;
        for (b = 0; b < nb - beats; b++)
            from += jt_beat_steps(sq.meter, b);
        kind = (int)((rs >> 3) % 10u);
        for (s = from; s < sq.bsteps; s++) {
            int bs = s - from, blen = jt_beat_steps(sq.meter, (s >= sq.bsteps - jt_beat_steps(sq.meter, nb - 1)) ? nb - 1 : nb - 2);
            int q = (bs % blen) * 4 / blen;   /* the step's place in a 4-step beat */
            int second = s >= sq.bsteps - jt_beat_steps(sq.meter, nb - 1);
            const char *sn, *tm;
            int fk = kind;
            if (beats == 2 && !second)
                fk = (kind + 3) % 10;
            if (g->fill == FILL_DROP) {  /* hip hop: the beat drops out */
                sq.drum[s][DR_K] = sq.drum[s][DR_H] = sq.drum[s][DR_O] = 0;
                continue;
            }
            if (g->fill == FILL_LIGHT)
                fk = second ? 1 : 8;
            else if (g->fill == FILL_SNARE || g->fill == FILL_DISCO)
                fk = fk % 3;             /* the snare's fills */
            else if (fk < 3)
                fk += 3;                 /* the toms' */
            sn = FILL_BEAT[fk][0];
            tm = FILL_BEAT[fk][1];
            for (k = 0; k < DR_N; k++)
                if (k != DR_K && k != DR_C && k != DR_P)
                    sq.drum[s][k] = 0;
            if (bs % (blen / 2 ? blen / 2 : 1))   /* the kick only on the fill's half beats */
                sq.drum[s][DR_K] = 0;
            if ((blen == 4 || (bs % blen) % 3 != 1)) {   /* (6-step beats: two of every three steps) */
                if (sn && sn[q] != '.') {
                    int v = drum_vel(sn[q], energy < 2 ? 2 : energy, &rs);
                    v = v * (60 + 40 * bs / (sq.bsteps - from)) / 100;   /* a crescendo */
                    if (g->fill == FILL_LIGHT)
                        v = v * 2 / 3;
                    sq.drum[s][DR_S] = (uint8_t)(v < 1 ? 1 : v);
                }
                if (tm && tm[q] != '.') {
                    int v = drum_vel(tm[q], 2, &rs);
                    sq.drum[s][DR_T] = (uint8_t)(((v >> 1) & 0x3F) | (tom_pitch(tm[q]) == 'h' ? 0x40 : tom_pitch(tm[q]) == 'm' ? 0x80 : 0xC0));
                }
            }
            if (g->fill == FILL_DISCO && q == 2)
                sq.drum[s][DR_O] = 90;
        }
    }
    if (sq.crash_next && energy >= 1) {  /* after a fill, a change of part, the start: the crash and the kick */
        sq.drum[0][DR_C] = (uint8_t)(energy >= 3 ? 118 : 96);
        if (!sq.drum[0][DR_K])
            sq.drum[0][DR_K] = 100;
    }
    sq.crash_next = 0;
}

/* ---------------------------------------------------------------- the players */
static const patch_t *part_patch(int part)
{
    uint32_t i = jam.patch[part - 1] % NPATCHES;
    return &PATCHES[i];
}

static void note_event(int part, int note, int vel, float delay, float gate_samples, int pan, int tie)
{
    event_t e;
    if (note < 12 || note > 115 || vel <= 0)
        return;
    e.type = EV_NOTE;
    e.part = (uint8_t)part;
    e.note = (uint8_t)note;
    e.vel = (uint8_t)(vel > 127 ? 127 : vel);
    e.due = now_s + (uint32_t)(delay < 0 ? 0 : delay);
    e.gate = (uint32_t)(gate_samples < 64.0f ? 64.0f : gate_samples);
    e.pan = (int8_t)pan;
    e.tie = (uint8_t)tie;
    ev_push(&e);
}

/* the length of a hit: from its step to the next character that is not a hold, in this bar */
static int hit_len(int meter, const char *S, const char *T, const char *C, int s, int bsteps)
{
    int k;
    for (k = s + 1; k < bsteps; k++) {
        char ch = pat_at(meter, S, T, C, k);
        if (ch != '-')
            return k - s;
    }
    return bsteps - s;
}

static int scale_tone(int deg)           /* the key's scale, any degree (negative too) -> semitones from the key */
{
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), i = deg - o * 7;
    return SCALE[jam.mode % JM_N][i] + 12 * o;
}

static int nearest(int pc, int to)       /* the note of pitch class pc nearest to `to` */
{
    int n = to - ((to - pc) % 12 + 12) % 12;
    return to - n > 6 ? n + 12 : n;
}

/* ---- bass */
static void play_bass(int s, float delay, int sib, int blen)
{
    const bstyle_t *b = &BSTYLES[jam.bstyle % NBSTYLES];
    char ch = pat_at(sq.meter, b->s, b->t, b->c, s);
    int pushed, ci, len, note, vel, lo, hi, base;
    chord_t c, nc;
    float gate;
    if (!ch || ch == '.')
        return;
    ci = chord_for_hit(sq.pstep, &pushed);
    if (ch == '-') {                     /* a hold over a change of chord: the new chord's root */
        if (ci == sq.bass_chord)
            return;
        ch = 'R';
    }
    chord_resolve(&jam, part_chord(ci), &c);
    chord_resolve(&jam, part_chord((ci + 1) % sq.nchord), &nc);
    len = hit_len(sq.meter, b->s, b->t, b->c, s, sq.bsteps);
    {                                    /* a note stops at its chord's end (a held note starts again there) */
        int to = steps_to_change(sq.pstep) + (pushed ? steps_to_change((sq.pstep + steps_to_change(sq.pstep)) % sq.ptotal) : 0);
        if (to < len)
            len = to;
    }
    base = 28 + 12 * (jam.oct[JP_BASS] < -1 ? -1 : jam.oct[JP_BASS] > 1 ? 1 : jam.oct[JP_BASS]) + part_patch(JP_BASS)->oct * 12;
    lo = base + 5;                       /* A1 .. G#2 (the root's home) */
    hi = lo + 11;
    {
        int r = nearest(c.bass, sq.bass_last ? sq.bass_last : lo + 5);
        while (r < lo)
            r += 12;
        while (r > hi)
            r -= 12;
        note = r;
    }
    /* an approach: the last note before a change of chord, leaning into the next root */
    if (b->approach && (ch == 'R' || ch == '5' || ch == '8') && sq.nchord > 1 && !pushed) {
        int to = steps_to_change(sq.pstep);
        if (to <= len && to <= 4 && to >= 1 && nc.bass != c.bass && s + to <= sq.bsteps)
            ch = 'A';
    }
    switch (ch) {
    case '5': {
        int f = 7, k;
        for (k = 0; k < c.n; k++)
            if (c.iv[k] == 6 || c.iv[k] == 8)
                f = c.iv[k];
        note = note + f > hi + 4 ? note + f - 12 : note + f;
        break;
    }
    case '8': note += 12; break;
    case 'L': note = note - 12 >= base ? note - 12 : note; break;
    case '3': note += c.n > 1 ? c.iv[1] % 12 : 4; break;
    case '7': {
        int k, sv = 10;
        for (k = 0; k < c.n; k++)
            if (c.iv[k] == 10 || c.iv[k] == 11)
                sv = c.iv[k];
        note += sv;
        break;
    }
    case '6': note += c.n > 1 && c.iv[1] == 3 ? 9 : 9; break;
    case 'A': case 'P': {
        int tgt = nearest(nc.bass, sq.bass_last ? sq.bass_last : note);
        while (tgt < lo - 2)
            tgt += 12;
        while (tgt > hi + 2)
            tgt -= 12;
        if (nc.bass == c.bass && sq.nchord > 1) {   /* no change ahead: a chord tone instead */
            note = (sq.step & 4) ? note + 7 : note + (c.n > 1 ? c.iv[1] % 12 : 4);
            if (note > hi + 3)
                note -= 12;
        } else if (ch == 'A')
            note = tgt + (tgt > (sq.bass_last ? sq.bass_last : note) ? -1 : 1);
        else {                           /* passing: a scale step from the last note towards the target */
            int last = sq.bass_last ? sq.bass_last : note, d = tgt > last ? 1 : -1, k, best = last;
            for (k = 1; k < 4; k++) {
                int cand = last + d * k, pc = ((cand - (int)jam.key) % 12 + 12) % 12, m;
                for (m = 0; m < 7; m++)
                    if (SCALE[jam.mode % JM_N][m] == pc)
                        break;
                if (m < 7) {
                    best = cand;
                    break;
                }
            }
            note = best == tgt ? best + d : best;
        }
        break;
    }
    case 'd': break;
    default: break;
    }
    vel = sib == 0 ? 108 : (sib * 2 == blen) ? 98 : 88;
    if (ch == 'd')
        vel = 42;
    if (ch == '8')
        vel -= 8;
    vel += (int)(rngf() * (float)jam.human * 0.12f);
    gate = (float)len * samples_per_step() * (float)b->gate * 0.01f;
    if (ch == 'd')
        gate = samples_per_step() * 0.25f;
    note_event(JP_BASS, note, vel, delay, gate, 0, b->gate >= 98 && ch == 'R' && ci == sq.bass_chord);
    sq.bass_last = note;
    sq.bass_chord = ci;
}

/* ---- chords */
static void comp_voicing(int ci)         /* the voicing for chord ci, led from the last one */
{
    const patch_t *pt = part_patch(JP_CHORDS);
    chord_t c;
    int k, centre = 62 + 12 * jam.oct[JP_CHORDS] + 12 * pt->oct, key = ci + 16 * sq.part;
    if (sq.comp_v_of == key && !(sq.changed & JX_SOUND))
        return;
    chord_resolve(&jam, part_chord(ci), &c);
    sq.ncomp_v = voice_chord(&c, pt->voicing, sq.comp_prev, sq.ncomp_prev, centre, sq.comp_v);
    if (pt->voicing != VO_CLOSE)         /* a guitar's or a pad's register follows the part's octave */
        for (k = 0; k < sq.ncomp_v; k++)
            sq.comp_v[k] += 12 * (jam.oct[JP_CHORDS] + pt->oct);
    for (k = 0; k < sq.ncomp_v; k++)
        sq.comp_prev[k] = sq.comp_v[k];
    sq.ncomp_prev = sq.ncomp_v;
    sq.comp_v_of = key;
}

static void play_chords(int s, float delay, int sib, int blen)
{
    const cstyle_t *cs = &CSTYLES[jam.cstyle % NCSTYLES];
    const patch_t *pt = part_patch(JP_CHORDS);
    char ch = pat_at(sq.meter, cs->s, cs->t, cs->c, s);
    int pushed, ci, len, k, vel, n;
    float gate, sps = samples_per_step();
    if (!ch || ch == '.')
        return;
    ci = chord_for_hit(sq.pstep, &pushed);
    if (ch == '-') {
        if (ci == sq.comp_chord)
            return;
        ch = 'x';
    }
    comp_voicing(ci);
    len = hit_len(sq.meter, cs->s, cs->t, cs->c, s, sq.bsteps);
    {
        int to = steps_to_change(sq.pstep) + (pushed ? 2 : 0);
        if (to < len && ch != 's')
            len = to;
    }
    n = sq.ncomp_v;
    gate = (float)len * sps * (float)cs->gate * 0.01f;
    vel = ch == 'X' ? 112 : ch == 'u' ? 72 : ch == 's' ? 96 : 90;
    if (sib == 0 && ch != 'u')
        vel += 6;
    vel += (int)(rngf() * (float)jam.human * 0.1f);
    if (ch >= '1' && ch <= '6') {        /* one voice: picked, ringing to the chord's end */
        int v = (ch - '1') % (n ? n : 1);
        int ring = steps_to_change(sq.pstep);
        if (n >= 5 && pt->voicing == VO_SPREAD)
            v = (ch - '1') < 1 ? 0 : (ch - '1') + 1 < n ? (ch - '1') + 1 : n - 1;   /* a guitar: the bass string, then the top strings */
        note_event(JP_CHORDS, sq.comp_v[v], 84 + (ch == '1' ? 8 : 0), delay, (float)(ring > 16 ? 16 : ring) * sps * 0.98f,
                   (int)((float)v / (float)(n > 1 ? n - 1 : 1) * 80.0f - 40.0f), 0);
    } else if (ch == 'b') {
        note_event(JP_CHORDS, sq.comp_v[0], 92, delay, gate, 0, 0);
    } else {
        int up = ch == 'u', first = 0, last = n, tie = cs->tie && ci == sq.comp_chord;
        float strum = (float)pt->strum * 0.001f * (float)JT_SR;
        if (up && n > 4)
            first = n - 4;               /* an upstroke: the top strings */
        if (!strum)
            strum = (float)jam.human * 0.0003f * (float)JT_SR * 0.1f;   /* keys: a hand's spread */
        for (k = first; k < last; k++) {
            int idx = up ? last - 1 - (k - first) : k;
            float d = delay + strum * (float)(k - first) * (1.0f + 0.25f * rngf());
            int pan = (int)(((float)idx / (float)(n > 1 ? n - 1 : 1) - 0.5f) * 70.0f);
            note_event(JP_CHORDS, sq.comp_v[idx], vel - (up ? 0 : (k - first) * 2), d, gate, pan, tie);
        }
    }
    sq.comp_chord = ci;
}

/* ---- synth */
static int syn_ladder(const chord_t *c, int centre, int k)   /* the chord's tones as a ladder: rung k from the centre */
{
    int t[6], n = 0, i, base, o;
    for (i = 0; i < c->n && n < 6; i++)
        t[n++] = (c->root + c->iv[i]) % 12;
    vsort(t, n);
    n = vuniq(t, n);
    base = centre - ((centre % 12) - t[0] + 12) % 12;   /* the lowest tone at or below the centre */
    o = k >= 0 ? k / n : -((-k + n - 1) / n);
    i = k - o * n;
    return base + 12 * o + ((t[i] - t[0] + 12) % 12);
}

static void play_synth(int s, float delay, int sib, int blen)
{
    int st_ = jam.sstyle % SY_N, pushed, ci = chord_for_hit(sq.pstep, &pushed), centre, k, to;
    chord_t c;
    float sps = samples_per_step();
    uint32_t h;
    if (st_ == SY_OFF)
        return;
    chord_resolve(&jam, part_chord(ci), &c);
    centre = 72 + 12 * jam.oct[JP_SYNTH] + 12 * part_patch(JP_SYNTH)->oct;
    to = steps_to_change(sq.pstep);
    h = (uint32_t)(jam.seed * 2654435761u) ^ (uint32_t)(sq.pstep * 40503u) ^ (uint32_t)(sq.part * 977u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    switch (st_) {
    case SY_PAD:
        if (ci != sq.syn_chord || s == 0) {
            int tie = ci == sq.syn_chord;
            int v[6], nv = voice_chord(&c, VO_CLOSE, 0, 0, centre, v);
            for (k = nv > 3 ? nv - 3 : 0; k < nv; k++)
                note_event(JP_SYNTH, v[k], 70, delay, (float)(to < sq.bsteps - s ? to : sq.bsteps - s) * sps * 0.99f,
                           (k - 1) * 25, tie);
        }
        break;
    case SY_ARP8: case SY_ARP16: {
        int unit = st_ == SY_ARP8 ? 2 : 1;
        if (sib % unit)
            break;
        if (ci != sq.syn_chord)
            sq.arp_i = 0;
        k = sq.arp_i++;
        if (st_ == SY_ARP16) {           /* up and down over two octaves of the chord */
            int span = 2 * (c.n > 4 ? 4 : c.n), m = k % (2 * span - 2);
            k = m < span ? m : 2 * span - 2 - m;
        } else
            k %= 2 * (c.n > 4 ? 4 : c.n);
        note_event(JP_SYNTH, syn_ladder(&c, centre - 7, k), sib == 0 ? 86 : 74, delay, (float)unit * sps * 0.8f,
                   (k & 1) ? 25 : -25, 0);
        break;
    }
    case SY_HOOK: case SY_MELODY: {
        /* a rhythm (one bar, or two for MELODY) and a contour from the seed: each note a rung of the chord's ladder on
         * the beats, a scale step between; the same seed, the same tune every round */
        int two = st_ == SY_MELODY, bar2 = two && (sq.part_bars & 1);
        const char *rhy = HOOK_RHY[((uint32_t)jam.seed + (bar2 ? 3u : 0u)) % NHOOK_RHY];
        int s16 = s * 16 / sq.bsteps, ni = 0, j;
        char ch;
        if (s * 16 % sq.bsteps)
            break;
        ch = rhy[s16];
        if (ch != 'x')
            break;
        for (j = 0; j < s16; j++)
            ni += rhy[j] == 'x';
        {
            uint32_t r = (uint32_t)jam.seed * 7u + (uint32_t)ni * 13u + (bar2 ? 101u : 0u);
            int rung = (int)((r * 2654435761u) >> 29) - 2;   /* -2 .. 5 */
            int l, note, len16 = 1;
            if (bar2 && ni >= 2)         /* the answer: falling to the root */
                rung = 2 - ni;
            note = syn_ladder(&c, centre, rung);
            if (sib != 0 && (r & 1)) {   /* off the beat: a scale step from it */
                int pc = ((note - (int)jam.key) % 12 + 12) % 12, m;
                for (m = 0; m < 7 && SCALE[jam.mode % JM_N][m] != pc; m++)
                    ;
                if (m < 7)
                    note += scale_tone(m + 1) - scale_tone(m);
            }
            for (l = s16 + 1; l < 16 && rhy[l] == '-'; l++)
                len16++;
            note_event(JP_SYNTH, note, sib == 0 ? 92 : 80, delay, (float)len16 * (float)sq.bsteps / 16.0f * sps * 0.9f, 10, 0);
        }
        break;
    }
    case SY_COUNTER:
        if (ci != sq.syn_chord) {        /* a long note on the chord's 3rd or 7th, whichever is nearest the last */
            int best = 0, bd = 99, cand;
            for (k = 1; k < c.n; k++) {
                int iv = c.iv[k] % 12, d;
                if (iv == 7 || iv == 0)
                    continue;
                cand = nearest((c.root + iv) % 12, sq.syn_last ? sq.syn_last : centre);
                d = cand - (sq.syn_last ? sq.syn_last : centre);
                d = d < 0 ? -d : d;
                if (d < bd) {
                    bd = d;
                    best = cand;
                }
            }
            if (best) {
                note_event(JP_SYNTH, best, 76, delay, (float)to * sps * 0.97f, -15, 0);
                sq.syn_last = best;
            }
        }
        break;
    case SY_PULSE:
        if (sib % 2)
            break;
        note_event(JP_SYNTH, nearest(c.root, centre - 12) + ((sq.step / 2) % 4 == 3 ? 7 : 0), sib == 0 ? 90 : 76, delay,
                   sps * 1.1f, 0, 0);
        break;
    case SY_BELLS:
        if (sib % 2 || (h & 0xFF) > 60)
            break;
        note_event(JP_SYNTH, syn_ladder(&c, centre + 5, (int)((h >> 8) % 5)), 60 + (int)((h >> 16) & 31), delay,
                   sps * 6.0f, (int)((h >> 20) % 120) - 60, 0);
        break;
    case SY_STABS:
        if (s % 8 == 4 || (s % 8 == 7 && (h & 1))) {
            int v[6], nv = voice_chord(&c, VO_CLOSE, 0, 0, centre, v);
            for (k = nv > 3 ? nv - 3 : 0; k < nv; k++)
                note_event(JP_SYNTH, v[k], 88, delay, sps * 1.2f, (k - 1) * 30, 0);
        }
        break;
    }
    sq.syn_chord = ci;
}

/* ---------------------------------------------------------------- a step */
static void drums_step(int s, int sib, int blen)
{
    const groove_t *g = &GROOVES[jam.groove % NGROOVES];
    float sw = swing_delay(sib, blen, g);
    int r;
    if (jam.mute & (1u << JP_DRUMS))
        return;
    for (r = 0; r < DR_N; r++) {
        int v = sq.drum[s][r], voice, pitch = 0;
        event_t e;
        if (!v)
            continue;
        voice = r == DR_K ? JD_KICK : r == DR_S ? JD_SNARE : r == DR_R ? JD_RIM : r == DR_H ? JD_HAT : r == DR_O ? JD_OPEN
              : r == DR_Y ? JD_RIDE : r == DR_C ? JD_CRASH : r == DR_T ? JD_TOM : JD_PERC;
        if (r == DR_T) {
            int p = v >> 6;
            pitch = p == 1 ? 5 : p == 2 ? 0 : -5;
            v = (v & 0x3F) * 2 + 1;
        }
        v += (int)(rngf() * (float)jam.human * 0.15f);
        e.type = EV_DRUM;
        e.note = (uint8_t)voice;
        e.part = (uint8_t)(64 + pitch + jam.oct[JP_DRUMS]);
        e.vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
        {
            float dd = sw + human_delay(r == DR_K || r == DR_S ? 0.5f : 1.0f);
            e.due = now_s + (uint32_t)(dd > 0.0f ? dd : 0.0f);
        }
        e.gate = 0;
        e.pan = 0;
        e.tie = 0;
        ev_push(&e);
    }
}

static void click_event(int accent, float delay)
{
    event_t e;
    e.type = EV_CLICK;
    e.vel = (uint8_t)(accent ? 127 : 90);
    e.due = now_s + (uint32_t)delay;
    e.note = e.part = 0;
    e.gate = 0;
    e.pan = 0;
    e.tie = 0;
    ev_push(&e);
}

static void part_switch(int to)
{
    sq.part = to & 1;
    sq.pstep = 0;
    sq.round = 0;
    sq.part_bars = 0;
    sq.queued = 0;
    sq.crash_next = 1;
    sq.comp_v_of = -1;
    prog_layout();
}

static void bar_start(void)
{
    const jt_part_t *p;
    int fills;
    sq.meter = jam.meter % JT_METER_N;
    sq.bsteps = bar_steps(sq.meter);
    if (sq.queued && jam.switch_at == JS_BAR)
        part_switch(sq.qpart);
    prog_layout();
    p = &jam.part[sq.part & 1];
    /* is this a fill bar? */
    sq.fill_bar = 0;
    fills = jam.fills;
    {
        int bars_round = (sq.ptotal + sq.bsteps - 1) / sq.bsteps, end_here = sq.pstep + sq.bsteps >= sq.ptotal;
        int nb = fills == JF_4 ? 4 : fills == JF_8 ? 8 : fills == JF_16 ? 16 : 0;
        if (nb && (sq.part_bars % nb) == nb - 1)
            sq.fill_bar = 1;
        if (fills == JF_ROUND && end_here && bars_round >= 2)
            sq.fill_bar = 1;
        if (fills != JF_OFF && sq.queued && (jam.switch_at == JS_BAR || end_here))
            sq.fill_bar = 1;
        if (jam.chain == JC_AUTO && end_here && sq.round + 1 >= (p->reps ? p->reps : 1) && fills != JF_OFF)
            sq.fill_bar = 1;
        if (sq.want_fill) {
            sq.fill_bar = 1;
            sq.want_fill = 0;
        }
        if (sq.ending)
            sq.fill_bar = 0;
    }
    build_bar();
    sq.changed = 0;
}

static void ending_hit(void)             /* the last bar's downbeat: crash, kick, the part's first chord rung out */
{
    float sps = samples_per_step(), hold = sps * 10.0f;
    event_t e;
    chord_t c;
    int v[6], nv, k;
    e.type = EV_DRUM;
    e.gate = 0;
    e.pan = 0;
    e.tie = 0;
    e.part = 64;
    e.due = now_s + (uint32_t)human_delay(0.3f);
    if (!(jam.mute & 1u)) {
        e.note = JD_CRASH;
        e.vel = 120;
        ev_push(&e);
        e.note = JD_KICK;
        e.vel = 118;
        ev_push(&e);
    }
    chord_resolve(&jam, part_chord(0), &c);
    if (!(jam.mute & (1u << JP_BASS)))
        note_event(JP_BASS, nearest(c.bass, sq.bass_last ? sq.bass_last : 36), 110, 0, hold * 1.6f, 0, 0);
    if (!(jam.mute & (1u << JP_CHORDS))) {
        sq.comp_v_of = -1;
        comp_voicing(0);
        for (k = 0; k < sq.ncomp_v; k++)
            note_event(JP_CHORDS, sq.comp_v[k], 100, (float)part_patch(JP_CHORDS)->strum * 0.001f * (float)JT_SR * (float)k,
                       hold * 1.6f, (k - sq.ncomp_v / 2) * 15, 0);
    }
    if (!(jam.mute & (1u << JP_SYNTH)) && jam.sstyle != SY_OFF) {
        nv = voice_chord(&c, VO_CLOSE, 0, 0, 72 + 12 * jam.oct[JP_SYNTH], v);
        note_event(JP_SYNTH, v[nv - 1], 80, 0, hold * 1.6f, 0, 0);
    }
}

static void step(void)
{
    int sib, blen, b, ss;
    if (sq.step == 0) {
        if (sq.ending == 2) {            /* the last hit has rung its bar: stopped */
            sq.playing = 0;
            sq.ending = 0;
            return;
        }
        if (sq.ending == 1) {
            sq.ending = 2;
            ending_hit();
            return;
        }
        bar_start();
    }
    if (sq.ending == 2)
        return;
    /* where the step is in its beat */
    for (b = 0, ss = sq.step; b < 7 && ss >= jt_beat_steps(sq.meter, b); b++)
        ss -= jt_beat_steps(sq.meter, b);
    sib = ss;
    blen = jt_beat_steps(sq.meter, b);
    sq.beat = b;
    if (sib == 0) {
        st.beat = (uint8_t)b;
        st.beat_seq++;
        if (settings.metronome)
            click_event(b == 0, human_delay(0));
    }
    {
        const groove_t *g = &GROOVES[jam.groove % NGROOVES];
        float d = swing_delay(sib, blen, g) + human_delay(0.6f);
        drums_step(sq.step, sib, blen);
        if (!(jam.mute & (1u << JP_BASS)))
            play_bass(sq.step, d, sib, blen);
        if (!(jam.mute & (1u << JP_CHORDS)))
            play_chords(sq.step, d, sib, blen);
        if (!(jam.mute & (1u << JP_SYNTH)))
            play_synth(sq.step, d, sib, blen);
    }
    st.chord = (uint8_t)chord_at(sq.pstep);
    {                                    /* where in its chord: the screen's ring */
        int c = st.chord, len = sq.cstart[c + 1] - sq.cstart[c];
        st.chord_step = (uint8_t)(sq.pstep - sq.cstart[c] < 255 ? sq.pstep - sq.cstart[c] : 255);
        st.chord_len = (uint8_t)(len < 255 ? len : 255);
    }
    /* on */
    if (++sq.pstep >= sq.ptotal) {       /* the end of a round */
        const jt_part_t *p = &jam.part[sq.part & 1];
        sq.pstep = 0;
        sq.round++;
        if (sq.queued && jam.switch_at == JS_ROUND)
            part_switch(sq.qpart);
        else if (jam.chain == JC_AUTO && sq.round >= (p->reps ? p->reps : 1))
            part_switch(sq.part ^ 1);
        else if (jam.chain == JC_A && sq.part)
            part_switch(0);
    }
    if (++sq.step >= sq.bsteps) {
        sq.step = 0;
        sq.bar++;
        sq.part_bars++;
        if (sq.fill_bar)
            sq.crash_next = 1;
        if (sq.want_stop)
            sq.ending = 1, sq.want_stop = 0;
        if (sq.want_restart) {
            sq.want_restart = 0;
            sq.pstep = 0;
            sq.round = 0;
            sq.part_bars = 0;
            sq.crash_next = 1;
        }
    }
    st.part = (uint8_t)sq.part;
    st.queued = sq.queued;
    st.bar = (uint16_t)sq.bar;
    st.round = (uint16_t)sq.round;
}

/* a tick: steps on every sixth; the count-in's clicks before bar 0 */
static void tick(int32_t t)
{
    if (t < 0) {                         /* counting in: a click on each beat of the count */
        int32_t bar_ticks = (int32_t)bar_steps(jam.meter % JT_METER_N) * (int32_t)JT_STEP_TICKS, into, b, acc = 0;
        into = ((t % bar_ticks) + bar_ticks) % bar_ticks;
        for (b = 0; b < jt_bar_beats(jam.meter); b++) {
            if (into == acc * (int32_t)JT_STEP_TICKS) {
                click_event(b == 0, 0);
                st.beat = (uint8_t)b;
                st.beat_seq++;
            }
            acc += jt_beat_steps(jam.meter, b);
        }
        st.counting = (uint8_t)((-t + bar_ticks - 1) / bar_ticks);
        return;
    }
    st.counting = 0;
    if (t % (int32_t)JT_STEP_TICKS == 0)
        step();
}

static void seq_reset(void)
{
    sq.step = 0;
    sq.bar = 0;
    sq.pstep = 0;
    sq.round = 0;
    sq.part_bars = 0;
    sq.queued = 0;
    sq.ending = 0;
    sq.crash_next = 1;
    sq.bass_last = 0;
    sq.bass_chord = sq.comp_chord = sq.syn_chord = -1;
    sq.ncomp_prev = 0;
    sq.comp_v_of = -1;
    sq.syn_last = 0;
    sq.arp_i = 0;
    sq.meter = jam.meter % JT_METER_N;
    sq.bsteps = bar_steps(sq.meter);
    prog_layout();
}
