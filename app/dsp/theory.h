/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK: music theory. Scales, Nashville numbers into chords, chord qualities, voicings that move as little as
 * they can from one chord to the next, and progressions in text. Part of engine.c. */

static const uint8_t SCALE[JM_N][7] = {
    {0, 2, 4, 5, 7, 9, 11},              /* major */
    {0, 2, 3, 5, 7, 8, 10},              /* natural minor */
    {0, 2, 3, 5, 7, 9, 10},              /* dorian */
    {0, 2, 4, 5, 7, 9, 10},              /* mixolydian */
    {0, 2, 4, 6, 7, 9, 11},              /* lydian */
    {0, 1, 3, 5, 7, 8, 10},              /* phrygian */
    {0, 2, 3, 5, 7, 8, 11},              /* harmonic minor */
};
static const char *const MODE_NAME[JM_N] = {"MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "LYDIAN", "PHRYGIAN", "HARM MINOR"};

typedef struct {
    const char *lab;                     /* as written after the number: "m7" */
    uint8_t n;
    int8_t iv[6];                        /* semitones above the root, low to high */
} qual_t;
static const qual_t QUAL[JQ_N] = {
    {"", 3, {0, 4, 7}},                  /* AUTO: resolved before use */
    {"", 3, {0, 4, 7}},
    {"m", 3, {0, 3, 7}},
    {"dim", 3, {0, 3, 6}},
    {"+", 3, {0, 4, 8}},
    {"sus2", 3, {0, 2, 7}},
    {"sus4", 3, {0, 5, 7}},
    {"maj7", 4, {0, 4, 7, 11}},
    {"m7", 4, {0, 3, 7, 10}},
    {"7", 4, {0, 4, 7, 10}},
    {"m7b5", 4, {0, 3, 6, 10}},
    {"add9", 4, {0, 4, 7, 14}},
    {"maj9", 5, {0, 4, 7, 11, 14}},
    {"m9", 5, {0, 3, 7, 10, 14}},
    {"9", 5, {0, 4, 7, 10, 14}},
    {"6", 4, {0, 4, 7, 9}},
    {"m6", 4, {0, 3, 7, 9}},
    {"7sus4", 4, {0, 5, 7, 10}},
    {"m11", 5, {0, 3, 7, 10, 17}},
    {"dim7", 4, {0, 3, 6, 9}},
    {"mM7", 4, {0, 3, 7, 11}},
    {"madd9", 4, {0, 3, 7, 14}},
    {"13", 5, {0, 4, 10, 14, 21}},
    {"6/9", 5, {0, 4, 7, 9, 14}},
    {"5", 2, {0, 7}},
};
static const char *const QUAL_MENU[JQ_N] = {"AUTO", "MAJ", "m", "dim", "aug", "sus2", "sus4", "maj7", "m7", "7",
                                            "m7b5", "add9", "maj9", "m9", "9", "6", "m6", "7sus4", "m11", "dim7",
                                            "mM7", "madd9", "13", "6/9", "5"};

static const char *const NOTE_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const NOTE_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

const char *jt_note_name(int pc, int flats) { return (flats ? NOTE_FLAT : NOTE_SHARP)[((pc % 12) + 12) % 12]; }

/* a key spelled with flats: F, Bb, Eb, Ab, Db, Gb majors and their relatives (by the major key with the same notes) */
int jt_key_flats(const jt_jam_t *j)
{
    static const int8_t REL[JM_N] = {0, 3, 10, 5, 7, 8, 3};   /* semitones up to the major with the same notes */
    int maj = (j->key + REL[j->mode < JM_N ? j->mode : 0]) % 12;
    return maj == 5 || maj == 10 || maj == 3 || maj == 8 || maj == 1 || maj == 6;
}

void jt_key_text(const jt_jam_t *j, char *t)
{
    const char *a = jt_note_name(j->key, jt_key_flats(j)), *b = MODE_NAME[j->mode < JM_N ? j->mode : 0];
    while (*a)
        *t++ = *a++;
    *t++ = ' ';
    while (*b)
        *t++ = *b++;
    *t = 0;
}

/* ---------------------------------------------------------------- chords */
typedef struct {
    int8_t root;                         /* pitch class 0..11 */
    int8_t bass;                         /* pitch class of the bass note */
    uint8_t q;                           /* the quality, resolved (never AUTO) */
    uint8_t n;
    int8_t iv[6];
} chord_t;

static int deg_index(uint8_t d) { int i = d & 7; return i > 6 ? 6 : i; }
static int deg_acc(uint8_t d) { int a = (d >> 4) & 3; return a == 1 ? -1 : a == 2 ? 1 : 0; }

/* the scale's own chord on degree i, with COLOR: 0 triads, 1 sevenths where they are soft (maj7, m7), 2 every
 * seventh, 3 ninths where they sit well */
static uint32_t auto_quality(int mode, int i, int acc, int color)
{
    const uint8_t *s = SCALE[mode < JM_N ? mode : 0];
    int third, fifth, sev;
    if (acc < 0)                         /* a borrowed flat chord: major (bVII, bVI, bIII, bII) */
        return color >= 2 ? JQ_MAJ7 : color == 1 && i != 4 ? JQ_MAJ7 : JQ_MAJ;
    if (acc > 0)                         /* a raised one: the leading-tone diminished (#iv) */
        return color >= 2 ? JQ_M7B5 : JQ_DIM;
    third = (s[(i + 2) % 7] - s[i] + 12) % 12;
    fifth = (s[(i + 4) % 7] - s[i] + 12) % 12;
    sev = (s[(i + 6) % 7] - s[i] + 12) % 12;
    if (fifth == 6)
        return color >= 1 ? JQ_M7B5 : JQ_DIM;
    if (fifth == 8)
        return JQ_AUG;
    if (third == 4) {                    /* major */
        if (sev == 11)
            return color >= 3 ? (i == 0 ? JQ_MAJ9 : JQ_MAJ7) : color >= 1 ? JQ_MAJ7 : JQ_MAJ;
        return color >= 3 ? JQ_DOM9 : color >= 2 ? JQ_DOM7 : JQ_MAJ;   /* dominant: a 7th only from COLOR 2 */
    }
    if (sev == 10) {                     /* minor seventh: m9 where the 9th is in the scale */
        int ninth = (s[(i + 1) % 7] - s[i] + 12) % 12;
        return color >= 3 && ninth == 2 ? JQ_MIN9 : color >= 1 ? JQ_MIN7 : JQ_MIN;
    }
    return color >= 2 ? JQ_MMAJ7 : JQ_MIN;
}

static void chord_resolve(const jt_jam_t *j, const jt_chord_t *c, chord_t *o)
{
    int mode = j->mode < JM_N ? j->mode : 0, i = deg_index(c->deg), acc = deg_acc(c->deg), k;
    uint32_t q = c->qual < JQ_N ? c->qual : 0;
    const qual_t *Q;
    if (q == JQ_AUTO)
        q = auto_quality(mode, i, acc, j->color);
    Q = &QUAL[q];
    o->root = (int8_t)(((int)j->key + SCALE[mode][i] + acc + 24) % 12);
    o->q = (uint8_t)q;
    o->n = Q->n;
    for (k = 0; k < 6; k++)
        o->iv[k] = k < Q->n ? Q->iv[k] : 0;
    switch (c->bass) {
    case JB_THIRD:                       /* the chord's third (or its 2nd / 4th when suspended) */
        o->bass = (int8_t)((o->root + (Q->n > 1 ? Q->iv[1] : 0)) % 12);
        break;
    case JB_FIFTH:
        o->bass = (int8_t)((o->root + 7) % 12);
        for (k = 0; k < Q->n; k++)
            if (Q->iv[k] == 6 || Q->iv[k] == 8)
                o->bass = (int8_t)((o->root + Q->iv[k]) % 12);
        break;
    case JB_SEVENTH: {                   /* its seventh, else a flat seventh */
        int s = 10;
        for (k = 0; k < Q->n; k++)
            if (Q->iv[k] == 9 && (q == JQ_DIM7) )
                s = 9;
            else if (Q->iv[k] == 10 || Q->iv[k] == 11)
                s = Q->iv[k];
        o->bass = (int8_t)((o->root + s) % 12);
        break;
    }
    case JB_TONIC:
        o->bass = (int8_t)j->key;
        break;
    default:
        o->bass = o->root;
    }
}

static int chord_has(const chord_t *c, int pc)   /* pc among the chord's notes */
{
    int k;
    for (k = 0; k < c->n; k++)
        if ((c->root + c->iv[k]) % 12 == ((pc % 12) + 12) % 12)
            return 1;
    return 0;
}

int jt_chord_pcs(const jt_jam_t *j, const jt_chord_t *c, int *pcs)
{
    chord_t r;
    int k;
    chord_resolve(j, c, &r);
    for (k = 0; k < r.n; k++)
        pcs[k] = (r.root + r.iv[k]) % 12;
    return r.n;
}
int jt_scale_pc(const jt_jam_t *j, int i) { return (j->key + SCALE[j->mode % JM_N][((i % 7) + 7) % 7]) % 12; }

static int text_put(char *d, const char *s)
{
    int n = 0;
    while (s[n]) {
        d[n] = s[n];
        n++;
    }
    d[n] = 0;
    return n;
}

void jt_chord_text(const jt_jam_t *j, const jt_chord_t *c, char *num, char *name)
{
    chord_t r;
    int acc = deg_acc(c->deg), n = 0, flats = jt_key_flats(j);
    chord_resolve(j, c, &r);
    if (num) {
        if (acc)
            num[n++] = acc < 0 ? 'b' : '#';
        num[n++] = (char)('1' + deg_index(c->deg));
        n += text_put(num + n, QUAL[r.q].lab);
        if (c->bass != JB_ROOT && c->bass < JB_N) {
            static const char *const B[JB_N] = {"", "/3", "/5", "/7", "/1"};
            n += text_put(num + n, B[c->bass]);
        }
        num[n] = 0;
    }
    if (name) {
        n = text_put(name, jt_note_name(r.root, flats || acc < 0));
        n += text_put(name + n, QUAL[r.q].lab);
        if (r.bass != r.root) {
            name[n++] = '/';
            n += text_put(name + n, jt_note_name(r.bass, flats || acc < 0));
        }
        name[n] = 0;
    }
}

/* ---------------------------------------------------------------- voicings
 * CLOSE: keys, four notes at most inside an octave and a bit, the inversion nearest the last chord's.
 * SPREAD: a guitar's shape: the root low, the fifth, then the rest above the octave, five or six strings.
 * OPEN: a pad's: root, fifth, tenth, the colours on top. */
enum { VO_CLOSE, VO_SPREAD, VO_OPEN };

static int vsort(int *v, int n)
{
    int i, k;
    for (i = 1; i < n; i++)
        for (k = i; k > 0 && v[k - 1] > v[k]; k--) {
            int t = v[k];
            v[k] = v[k - 1];
            v[k - 1] = t;
        }
    return n;
}

static int vuniq(int *v, int n)          /* sorted, the doubled notes once */
{
    int i, m = 0;
    for (i = 0; i < n; i++)
        if (!m || v[i] != v[m - 1])
            v[m++] = v[i];
    return m;
}

static int vdist(const int *a, int na, const int *b, int nb)   /* how far one voicing moves to the other */
{
    int i, d = 0;
    if (!na || !nb)
        return 0;
    for (i = 0; i < na; i++) {
        int k, best = 99;
        for (k = 0; k < nb; k++) {
            int e = a[i] - b[k];
            e = e < 0 ? -e : e;
            if (e < best)
                best = e;
        }
        d += best;
    }
    return d;
}

/* the chord's tones to voice, at most `max`: with more, the fifth goes, then the root (the bass has it) */
static int voice_tones(const chord_t *c, int max, int *t)
{
    int n = 0, k, drop5 = c->n > max, droproot = c->n - (drop5 ? 1 : 0) > max;
    for (k = 0; k < c->n; k++) {
        int iv = c->iv[k];
        if (drop5 && iv == 7 && c->n > 3)
            continue;
        if (droproot && iv == 0)
            continue;
        t[n++] = iv;
    }
    return n;
}

/* centre: the voicing's middle when there is no last chord; out: MIDI notes, low to high -> their count */
static int voice_chord(const chord_t *c, int style, const int *prev, int nprev, int centre, int *out)
{
    int t[6], nt, i, best_d = 1 << 30, n = 0, cand[6];
    if (style == VO_SPREAD) {
        int r, rb = 0, bd = 1 << 30;
        for (r = 40; r < 52; r++)        /* the root between E2 and D#3, nearest the last one's */
            if (r % 12 == c->root) {
                int d = nprev ? (r - prev[0] < 0 ? prev[0] - r : r - prev[0]) : (r - 45 < 0 ? 45 - r : r - 45);
                if (d < bd) {
                    bd = d;
                    rb = r;
                }
            }
        out[n++] = rb;
        if (c->n > 2 || c->iv[1] == 7) {
            int f = -1;
            for (i = 0; i < c->n; i++)
                if (c->iv[i] >= 6 && c->iv[i] <= 8)
                    f = c->iv[i];
            out[n++] = rb + (f > 0 ? f : c->iv[1]);
        }
        for (i = 1; i < c->n && n < 6; i++) {   /* the rest above the octave, in order */
            int iv = c->iv[i] % 12, note = rb + 12 + iv;
            if (iv >= 6 && iv <= 8 && n == 2)
                continue;
            if (c->iv[i] >= 12)
                note = rb + 12 + c->iv[i] - 12 + (c->iv[i] - 12 < 4 ? 12 : 0);
            out[n++] = note;
        }
        if (n < 5)
            out[n++] = rb + 12;          /* the octave */
        if (n < 6 && c->n <= 3)
            out[n++] = rb + 24;          /* and the root on top: a full strum */
        return vuniq(out, vsort(out, n));
    }
    if (style == VO_OPEN) {
        int r, rb = 0, bd = 1 << 30;
        for (r = 41; r < 53; r++)
            if (r % 12 == c->root) {
                int d = nprev ? (r - prev[0] < 0 ? prev[0] - r : r - prev[0]) : (r - 48 < 0 ? 48 - r : r - 48);
                if (d < bd) {
                    bd = d;
                    rb = r;
                }
            }
        out[n++] = rb;
        for (i = 0; i < c->n; i++)       /* the fifth (or what stands for it) near the root */
            if (c->iv[i] >= 5 && c->iv[i] <= 8) {
                out[n++] = rb + c->iv[i];
                break;
            }
        for (i = 1; i < c->n && n < 5; i++) {
            int iv = c->iv[i];
            if (iv >= 5 && iv <= 8 && iv != 6)
                continue;
            out[n++] = rb + 12 + (iv % 12);
        }
        return vuniq(out, vsort(out, n));
    }
    /* CLOSE: every inversion of the chord's tones at every octave near the centre; the one nearest the last voicing,
     * or the centre */
    nt = voice_tones(c, 4, t);
    for (i = 0; i < nt; i++) {           /* inversion i: tone i lowest */
        int o;
        for (o = -2; o <= 2; o++) {
            int k, base = 12 * (centre / 12 + o) + c->root, last = -100, d, m = 0;
            for (k = 0; k < nt; k++) {
                int note = base + t[(i + k) % nt] % 12;
                while (note <= last)
                    note += 12;
                cand[m++] = note;
                last = note;
            }
            if (cand[0] < centre - 9 || cand[m - 1] > centre + 16)
                continue;
            d = nprev ? vdist(cand, m, prev, nprev) * 4 : 0;
            {
                int mid = (cand[0] + cand[m - 1]) / 2 - centre;
                d += (mid < 0 ? -mid : mid) * (nprev ? 1 : 4);   /* keep near the centre: no drifting away */
            }
            if (d < best_d) {
                best_d = d;
                for (k = 0; k < m; k++)
                    out[k] = cand[k];
                n = m;
            }
        }
    }
    if (!n) {                            /* (cannot happen; a root position chord as a last resort) */
        for (i = 0; i < nt; i++)
            out[i] = 12 * (centre / 12) + c->root + t[i];
        n = nt;
    }
    return vsort(out, n);
}

/* ---------------------------------------------------------------- progressions in text */
int jt_qual_parse(const char *s, int *len)   /* the longest quality label at s; JQ_AUTO with len 0 when none */
{
    int q, best = JQ_AUTO, bl = 0;
    for (q = 1; q < JQ_N; q++) {
        const char *l = QUAL[q].lab;
        int k = 0;
        if (!l[0])
            continue;
        while (l[k] && s[k] == l[k])
            k++;
        if (!l[k] && k > bl) {
            bl = k;
            best = q;
        }
    }
    if (!bl && s[0] == 'M' && s[1] != 'A') {   /* "M": major, said plainly */
        *len = 1;
        return JQ_MAJ;
    }
    *len = bl;
    return best;
}

int jt_parse_prog(const char *s, jt_part_t *p, int beats_default)
{
    int n = 0;
    while (*s) {
        jt_chord_t c;
        int acc = 0, l;
        while (*s == ' ' || *s == '|' || *s == ',')
            s++;
        if (!*s)
            break;
        if (n >= (int)JT_NCHORD)
            return -1;
        if (*s == 'b') {
            acc = 1;
            s++;
        } else if (*s == '#') {
            acc = 2;
            s++;
        }
        if (*s < '1' || *s > '7')
            return -1;
        c.deg = (uint8_t)((*s - '1') | acc << 4);
        s++;
        c.qual = (uint8_t)jt_qual_parse(s, &l);
        s += l;
        c.bass = JB_ROOT;
        if (*s == '/') {
            s++;
            c.bass = *s == '3' ? JB_THIRD : *s == '5' ? JB_FIFTH : *s == '7' ? JB_SEVENTH : *s == '1' ? JB_TONIC : JB_ROOT;
            if (*s)
                s++;
        }
        c.beats = (uint8_t)beats_default;
        if (*s == ':') {
            int b = 0;
            s++;
            while (*s >= '0' && *s <= '9')
                b = b * 10 + *s++ - '0';
            c.beats = (uint8_t)(b < 1 ? 1 : b > 16 ? 16 : b);
        }
        if (*s && *s != ' ' && *s != '|' && *s != ',')
            return -1;
        p->c[n++] = c;
    }
    if (!n)
        return -1;
    p->n = (uint8_t)n;
    return 0;
}
