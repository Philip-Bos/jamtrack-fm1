/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK FM-1: the user kit, one sample for each drum (kick, snare, rim, hat, open hat, ride, crash, tom, perc),
 * from the companion app. ECHOFIELD's sample bank, smaller: 48 sectors of 4 KiB in the data area (0x97000..0xC6FFF),
 * each holding one 3828-byte payload of EFB1 (29 ADPCM blocks) and a trailer; a directory (a store object, torn-write
 * safe) names each drum's sample and which sectors it has, in order. Any sector may belong to any sample: nothing
 * ever moves.
 *
 * Sector layout:
 *   0x000 .. 0xEF3   payload (29 blocks)
 *   0xEF4 .. 0xFDF   erased: bytes 0xF00..0xF07 stay erased, so the update loader never takes the sector for an update
 *                    record (plat.h, bsp/loader/ldr_core.c)
 *   0xFE0 .. 0xFEF   trailer: "JKS1", slot, index in the sample, 0, 0, CRC-32 of the payload, the directory's gen
 * Checks: at boot every sample's sectors (trailer and CRC) and its whole payload against the directory's CRC; one that
 * fails is DAMAGED and never played (the drum plays the STUDIO kit's sound instead). A sample is written into free
 * sectors, read back, and only then entered in the directory; the one it replaces leaves the directory first. Power
 * lost at any point: the directory names the old sample, or none, never a half-written one. Part of app.c. */

#define KIT_SLOTS JD_N
#define KIT_NSEC 48u
#define KIT_BASE PLAT_DATA_LO
#define KIT_MAGIC 0x314B544Au            /* "JTK1" */
#define KIT_VERSION 1u
#define KSEC_MAGIC 0x31534B4Au           /* "JKS1" */
#define SEC_BLOCKS 29u
#define SEC_PAYLOAD (SEC_BLOCKS * JT_BLK_BYTES)        /* 3828 */
#define SEC_SAMPLES (SEC_BLOCKS * JT_BLK_SAMPLES)      /* 7424 */
#define SEC_TRAILER 0xFE0u
#define KIT_MAX_SECS 16u                 /* ADPCM: 65 000 frames (the engine's most), 2 s at 32 kHz; mu-law: 57 344 */
#define MU_BLOCKS 14u                    /* mu-law: 14 blocks of 256 bytes a sector (3584 of the payload's 3828) */
#define KIT_MAPB ((KIT_NSEC + 7u) / 8u)

typedef struct {
    char name[NAME_N];
    uint32_t frames;
    uint16_t rate;
    uint8_t used, fmt;                   /* fmt: 0 EFB1 ADPCM, 1 8-bit mu-law (bright drums: no ADPCM hiss) */
    uint32_t crc;                        /* CRC-32 of the whole payload, sector after sector */
    uint8_t map[KIT_MAPB];               /* bit i: sector i is this sample's (in ascending order) */
    uint8_t pad[2];
} kit_slot_t;

typedef struct {
    uint32_t magic;
    uint16_t version, nslots;
    uint32_t gen;                        /* counts every change (the companion's cache) */
    kit_slot_t slot[KIT_SLOTS];
} kit_dir_t;

typedef struct {
    uint32_t magic;
    uint8_t slot, index, rsv[2];
    uint32_t crc, gen;
} ksec_trailer_t;

enum { SLOT_EMPTY, SLOT_OK, SLOT_DAMAGED };
enum { KIT_OK, KIT_E_ARGS = 1, KIT_E_XFER = 2, KIT_E_ROOM = 3, KIT_E_ERASE = 7, KIT_E_WRITE = 8, KIT_E_VERIFY = 9,
       KIT_E_BUSY = 10, KIT_E_CRC = 11, KIT_E_STORE = 12, KIT_E_FLASH = 13 };

_Static_assert(sizeof(kit_dir_t) <= PLAT_STORE_MAX, "the kit's directory fits one store object");
_Static_assert(SEC_PAYLOAD <= 0xEF4u, "the payload stays below the update loader's record bytes");
_Static_assert(KIT_BASE + KIT_NSEC * 0x1000u <= 0xD6000u, "the kit ends below the store objects");

static kit_dir_t kdir;
static uint8_t kslot_state[KIT_SLOTS];

static uint32_t ksec_addr(uint32_t i) { return KIT_BASE + i * 0x1000u; }
static int kmap_bit(const uint8_t *m, uint32_t i) { return (m[i >> 3] >> (i & 7u)) & 1; }
static uint32_t kmap_count(const uint8_t *m)
{
    uint32_t i, n = 0;
    for (i = 0; i < KIT_NSEC; i++)
        n += (uint32_t)kmap_bit(m, i);
    return n;
}
static uint32_t knsec_for(uint32_t frames, uint32_t fmt)
{
    uint32_t per = fmt ? MU_BLOCKS * JT_BLK_SAMPLES : SEC_SAMPLES;
    return (frames + per - 1u) / per;
}

/* ---- CRC-32 (zlib's) */
static uint32_t crc_t[256];
static void crc_init(void)
{
    uint32_t n, k, c;
    for (n = 0; n < 256u; n++) {
        for (c = n, k = 0; k < 8u; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, uint32_t n)
{
    c = ~c;
    while (n--)
        c = crc_t[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

/* ---- the directory */
static void kdir_empty(void)
{
    uint32_t i, k;
    kdir.magic = KIT_MAGIC;
    kdir.version = KIT_VERSION;
    kdir.nslots = KIT_SLOTS;
    for (i = 0; i < KIT_SLOTS; i++) {
        kdir.slot[i].used = 0;
        for (k = 0; k < KIT_MAPB; k++)
            kdir.slot[i].map[k] = 0;
    }
}

static int store_quiet(uint32_t obj, const void *p, uint32_t n);
static int kdir_save(void)
{
    kdir.gen++;
    return store_quiet(OBJ_KITDIR, &kdir, sizeof kdir) ? KIT_E_STORE : KIT_OK;
}

static int kslot_verify(uint32_t s)
{
    const kit_slot_t *e = &kdir.slot[s];
    uint32_t i, k = 0, c = 0;
    for (i = 0; i < KIT_NSEC; i++) {
        const uint8_t *p;
        ksec_trailer_t t;
        uint32_t b;
        if (!kmap_bit(e->map, i))
            continue;
        p = plat_data(ksec_addr(i));
        if (!p)
            return 0;
        for (b = 0; b < sizeof t; b++)
            ((uint8_t *)&t)[b] = p[SEC_TRAILER + b];
        if (t.magic != KSEC_MAGIC || t.slot != s || t.index != k || crc_upd(0, p, SEC_PAYLOAD) != t.crc)
            return 0;
        c = crc_upd(c, p, SEC_PAYLOAD);
        k++;
    }
    return c == e->crc;
}

/* the engine told which samples to play (the user kit's), after a change or at boot */
static const uint8_t *kit_runs[KIT_SLOTS][KIT_MAX_SECS];
static void kit_publish(void)
{
    uint32_t s;
    jt_user_release();
    for (s = 0; s < KIT_SLOTS; s++) {
        const kit_slot_t *e = &kdir.slot[s];
        jt_sample_t src;
        uint32_t i, k = 0;
        if (kslot_state[s] != SLOT_OK) {
            jt_user_sample(s, 0);
            continue;
        }
        for (i = 0; i < KIT_NSEC && k < KIT_MAX_SECS; i++)
            if (kmap_bit(e->map, i))
                kit_runs[s][k++] = plat_data(ksec_addr(i));
        src.sec = kit_runs[s];
        src.frames = e->frames;
        src.rate = e->rate;
        src.bps = (uint16_t)(e->fmt ? MU_BLOCKS : SEC_BLOCKS);
        src.fmt = e->fmt;
        src.rsv = 0;
        jt_user_sample(s, &src);
    }
    jt_user_resume();
}

static uint32_t kit_load(void)           /* boot: the directory loaded, every entry checked -> samples that play */
{
    uint8_t taken[KIT_MAPB] = {0};
    uint32_t i, k, ok = 0;
    crc_init();
    if (plat_store_load(OBJ_KITDIR, &kdir, sizeof kdir) != (int)sizeof kdir || kdir.magic != KIT_MAGIC ||
        kdir.version != KIT_VERSION || kdir.nslots != KIT_SLOTS)
        kdir_empty();
    for (i = 0; i < KIT_SLOTS; i++) {
        kit_slot_t *e = &kdir.slot[i];
        uint32_t n;
        int bad = 0;
        kslot_state[i] = SLOT_EMPTY;
        if (e->used != 1u) {
            e->used = 0;
            continue;
        }
        name_clean(e->name, NAME_N);
        n = kmap_count(e->map);
        if (e->fmt > 1u || e->frames < 256u || e->frames > 65000u || e->rate < 8000u || e->rate > 48000u || !n ||
            n != knsec_for(e->frames, e->fmt) ||
            n > KIT_MAX_SECS)
            bad = 1;
        for (k = 0; k < KIT_MAPB && !bad; k++)
            if (taken[k] & e->map[k])
                bad = 1;
        if (!bad)
            for (k = 0; k < KIT_MAPB; k++)
                taken[k] |= e->map[k];
        kslot_state[i] = !bad && kslot_verify(i) ? SLOT_OK : SLOT_DAMAGED;
        ok += kslot_state[i] == SLOT_OK;
    }
    kit_publish();
    return ok;
}

static uint32_t kit_free_secs(int except)
{
    uint32_t i, s, n = 0;
    for (i = 0; i < KIT_NSEC; i++) {
        int used = 0;
        for (s = 0; s < KIT_SLOTS; s++)
            if (kdir.slot[s].used && (int)s != except && kmap_bit(kdir.slot[s].map, i))
                used = 1;
        n += !used;
    }
    return n;
}

/* ---- writing: the user kit let go first (the engine reads it in the audio interrupt; an erase stops the audio
 * anyway), then given back */
static void kit_quiet(void)
{
    uint32_t i;
    jt_user_release();
    for (i = 0; i < 40u && !jt_user_idle(); i++)
        settle(2);
}

static int ksec_write(uint32_t i, const uint8_t *pay, uint32_t slot, uint32_t index)
{
    ksec_trailer_t t;
    uint32_t a = ksec_addr(i), b;
    const uint8_t *p;
    t.magic = KSEC_MAGIC;
    t.slot = (uint8_t)slot;
    t.index = (uint8_t)index;
    t.rsv[0] = t.rsv[1] = 0;
    t.crc = crc_upd(0, pay, SEC_PAYLOAD);
    t.gen = kdir.gen + 1u;
    if (plat_data_erase(a))
        return KIT_E_ERASE;
    if (plat_data_write(a, pay, SEC_PAYLOAD) || plat_data_write(a + SEC_TRAILER, &t, sizeof t))
        return KIT_E_WRITE;
    p = plat_data(a);
    if (!p || crc_upd(0, p, SEC_PAYLOAD) != t.crc)
        return KIT_E_VERIFY;
    for (b = 0; b < 8u; b++)
        if (p[0xF00u + b] != 0xFFu)
            return KIT_E_VERIFY;
    return KIT_OK;
}

typedef int (*kit_fill_fn)(uint32_t index, uint8_t *pay, void *ctx);
static uint8_t ksec_buf[SEC_PAYLOAD] PLAT_POOL;

static int kit_write(uint32_t s, uint32_t frames, uint32_t rate, uint32_t fmt, const char *name, uint32_t crc,
                     kit_fill_fn fill, void *ctx, void (*progress)(uint32_t, uint32_t))
{
    kit_slot_t e;
    uint32_t n = knsec_for(frames, fmt), i, k = 0, c = 0;
    int rc = KIT_OK;
    if (s >= KIT_SLOTS || fmt > 1u || frames < 256u || frames > 65000u || rate < 8000u || rate > 48000u || n > KIT_MAX_SECS)
        return KIT_E_ARGS;
    if (n > kit_free_secs((int)s))
        return KIT_E_ROOM;
    if (!plat_data(ksec_addr(0)))
        return KIT_E_FLASH;
    kit_quiet();
    if (kdir.slot[s].used) {
        kdir.slot[s].used = 0;
        kslot_state[s] = SLOT_EMPTY;
        if ((rc = kdir_save()) != KIT_OK)
            goto out;
    }
    for (i = 0; i < KIT_MAPB; i++)
        e.map[i] = 0;
    for (i = 0; i < KIT_NSEC && k < n; i++) {
        uint32_t t;
        int busy = 0;
        for (t = 0; t < KIT_SLOTS; t++)
            if (kdir.slot[t].used && kmap_bit(kdir.slot[t].map, i))
                busy = 1;
        if (busy)
            continue;
        if (fill(k, ksec_buf, ctx)) {
            rc = KIT_E_XFER;
            goto out;
        }
        if ((rc = ksec_write(i, ksec_buf, s, k)) != KIT_OK)
            goto out;
        c = crc_upd(c, ksec_buf, SEC_PAYLOAD);
        e.map[i >> 3] |= (uint8_t)(1u << (i & 7u));
        k++;
        if (progress)
            progress(k, n);
    }
    if (k != n) {
        rc = KIT_E_ROOM;
        goto out;
    }
    if (crc && c != crc) {
        rc = KIT_E_CRC;
        goto out;
    }
    text_cpy(e.name, name, NAME_N);
    name_clean(e.name, NAME_N);
    e.frames = frames;
    e.rate = (uint16_t)rate;
    e.used = 1;
    e.fmt = (uint8_t)fmt;
    e.pad[0] = e.pad[1] = 0;
    e.crc = c;
    kdir.slot[s] = e;
    if ((rc = kdir_save()) != KIT_OK) {
        kdir.slot[s].used = 0;
        goto out;
    }
    kslot_state[s] = kslot_verify(s) ? SLOT_OK : SLOT_DAMAGED;
    if (kslot_state[s] != SLOT_OK)
        rc = KIT_E_VERIFY;
out:
    kit_publish();
    return rc;
}

static int kit_erase(uint32_t s)
{
    int rc;
    if (s >= KIT_SLOTS)
        return KIT_E_ARGS;
    if (!kdir.slot[s].used)
        return KIT_OK;
    kit_quiet();
    kdir.slot[s].used = 0;
    kslot_state[s] = SLOT_EMPTY;
    rc = kdir_save();
    kit_publish();
    return rc;
}

static uint32_t kit_count_ok(void)
{
    uint32_t s, n = 0;
    for (s = 0; s < KIT_SLOTS; s++)
        n += kslot_state[s] == SLOT_OK;
    return n;
}
