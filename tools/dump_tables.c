/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* The engine's tables, the factory jams and chord names, as JSON for the companion (companion/lib/tables.json), so it
 * works without an FM-1 and its music theory is checked against the firmware's (app/tests/theory_test.mjs).
 *   cc -DPLAT_HOST -Iapp/dsp -Iapp -Ibase/include app/dsp/engine.c tools/dump_tables.c -lm -o build/dump_tables */
#include <stdio.h>
#include <string.h>
#include "jt_dsp.h"
uint32_t plat_ms(void) { return 0; }
uint32_t plat_ticks24(void) { return 0; }
int plat_midi_in(uint32_t *p, int *q) { (void)p; (void)q; return 0; }
uint32_t plat_midi_in_t24(void) { return 0; }
void plat_midi_send(uint32_t p, uint32_t t) { (void)p; (void)t; }
static void text_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
#include "factory.c"

static void names(const char *key, int what)
{
    uint32_t i;
    printf("  \"%s\": [", key);
    for (i = 0; i < jt_count(what); i++)
        printf("%s\"%s\"", i ? ", " : "", jt_name(what, i));
    printf("],\n");
}
int main(void)
{
    uint32_t i, k;
    static const char *const KEYS[JT_T_N] = {"kits", "grooves", "bass", "comp", "synth", "patches", "modes", "meters",
                                             "quals", "genres"};
    jt_init();
    printf("{\n");
    for (k = 0; k < JT_T_N; k++)
        names(KEYS[k], (int)k);
    printf("  \"patchKind\": [");
    for (i = 0; i < jt_count(JT_T_PATCH); i++)
        printf("%s%u", i ? ", " : "", jt_patch_kind(i));
    printf("],\n  \"drums\": [");
    for (i = 0; i < JD_N; i++)
        printf("%s\"%s\"", i ? ", " : "", jt_drum_name(i));
    printf("],\n  \"barBeats\": [");
    for (i = 0; i < JT_METER_N; i++)
        printf("%s%d", i ? ", " : "", jt_bar_beats((int)i));
    printf("],\n  \"factory\": [\n");
    for (i = 0; i < (uint32_t)NFACTORY; i++) {
        jt_jam_t j;
        const uint8_t *b = (const uint8_t *)&j;
        factory_jam((int)i, &j);
        printf("    \"");
        for (k = 0; k < sizeof j; k++)
            printf("%02x", b[k]);
        printf("\"%s\n", i + 1u < (uint32_t)NFACTORY ? "," : "");
    }
    printf("  ],\n  \"chordTests\": [\n");
    {                                    /* every degree, accidental, quality and bass in a few keys and scales */
        jt_jam_t j;
        int n = 0, key, mode, d, a, q, color, bass;
        jt_jam_default(&j);
        for (key = 0; key < 12; key += 5)
            for (mode = 0; mode < JM_N; mode++)
                for (color = 0; color < 4; color += 3)
                    for (d = 0; d < 7; d++)
                        for (a = 0; a < 3; a++)
                            for (q = 0; q < JQ_N; q += (q < 2 ? 1 : 3))
                                for (bass = 0; bass < JB_N; bass += 2) {
                                    jt_chord_t c = {(uint8_t)(d | a << 4), (uint8_t)q, 4, (uint8_t)bass};
                                    char num[16], nm[16];
                                    int pcs[6], np, m;
                                    j.key = (uint8_t)key;
                                    j.mode = (uint8_t)mode;
                                    j.color = (uint8_t)color;
                                    jt_chord_text(&j, &c, num, nm);
                                    np = jt_chord_pcs(&j, &c, pcs);
                                    printf("%s    [%d, %d, %d, %d, %d, %d, %d, \"%s\", \"%s\", [", n++ ? ",\n" : "", key, mode, color,
                                           c.deg, c.qual, c.bass, 0, num, nm);
                                    for (m = 0; m < np; m++)
                                        printf("%s%d", m ? ", " : "", pcs[m]);
                                    printf("]]");
                                }
    }
    printf("\n  ]\n}\n");
    return 0;
}
