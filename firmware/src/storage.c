/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * ERASE AHEAD (ChoralRoot 0.22, the quiet writes: docs/SETTINGS.md "When flash is written"): an erase masks every
 * interrupt for one sector's erase time (17.8 ms measured on the unit: six audio halves nobody serves), a page
 * program for a fraction of one half. For the favourites and the session (st_ahead_ix; NOT the settings, whose
 * previous copy is the panel calibration's fallback: the owner's decision of 2026-10-14) the erase is therefore taken out of the save: st_prep erases the copy that
 * is NOT the current one at a moment nobody can hear (power-on, before the audio starts; later only after a long
 * idle: cr_settings.c), and st_save, finding that copy blank (every byte 0xFF: read, never assumed), programs only.
 * A copy that is not blank (no st_prep since the last save, an erase or a write cut by the power) is erased by
 * st_save as before. The on-flash format is unchanged and so is the rule above: only the copy that is not current
 * is ever erased or programmed, the commit record goes last. What changes is that the OLDER copy does not survive
 * until the next save: between an st_prep and the next save there is one copy, as on a unit saved once.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): the FM6 patch bank (fm6_bank.c) copy A 0x9F000, the eight
 * CZ-1 banks A..H 0xA2000..0xB1FFF (cz_bank.c, OBJ_CZBANK0 + k: an A/B pair each), the session 0xB4000 / 0xB5000
 * (cr_settings.c, OBJ_SESSION: where the unit was left, 0.18), the favourites 0xB6000 / 0xB7000 (cr_favs.c, OBJ_FAVS:
 * the marked sounds, 0.22), the sound pool 0xB8000..0xC7FFF (0.22: the sp_* section at the end, upool.c: 13 banks of 10
 * user slots floating over these 16 sectors), the loop slots 0xC8000..0xDBFFF (cr_ui.c CRL_FL_BASE), the settings A / B
 * 0xFC000 / 0xFD000, the FM6 patch bank copy B 0xFE000. SMK-37 (BOARD_SMK37): none of these places; every object, the
 * favourites, the pool's sectors and the loop slots have their own there (the SMK section at st_sector below; SMK37.md
 * "Storage").
 * FREE (HISTORY 3y231): 0x97000..0x9EFFF, 0xA0000..0xA1FFF, 0xB2000..0xB3FFF and 0xDC000..0xDFFFF held 0.21's sound
 * objects (the user banks, the VA / FM6 / CZ-1 tone / FM TONE stores; before them Felucca's projects and user presets).
 * 0.22 .. 0.29 copied them into the pool once at their first power-on; this firmware neither reads nor writes them (a
 * unit still on 0.21 goes through 0.29.2 first). Their object numbers stay reserved below: every record's header names
 * its object (st_hdr_t.type), so the live objects keep their numbers. */
enum { OBJ_SETTINGS, OBJ_X_PROJECT0, OBJ_X_UPRESET0 = OBJ_X_PROJECT0 + 4, OBJ_FM6BANK = OBJ_X_UPRESET0 + 2,
       OBJ_X_CZSTORE1, OBJ_CZBANK0, OBJ_X_QUADSTORE = OBJ_CZBANK0 + 8, OBJ_SESSION, OBJ_FAVS, OBJ_COUNT };
static inline int st_live(uint32_t obj)  /* the objects stored today */
{
    return obj < OBJ_COUNT && !(obj >= OBJ_X_PROJECT0 && obj < OBJ_FM6BANK) && obj != OBJ_X_CZSTORE1 &&
           obj != OBJ_X_QUADSTORE;
}
#define ST_NOWHERE 0xFFFFF000u         /* st_sector of an object not stored: in no FL_RANGE_OK window, at or past FL_KEYMAC_LO
                                        * (fm1_flash.h's FL_GUARD refuses an erase or a program there), no live object's */

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
#ifndef ST_READS_COUNTED
static uint32_t st_reads, st_read_bytes;       /* (a host test's own st_read: not counted) */
#endif
#ifndef ST_BLOCKED
#define ST_BLOCKED() 0                         /* (core.h: SAFE MODE, the objects neither loaded nor saved) */
#endif
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);
/* 3y48 (78 PEAK 46..49, totals since power-on, never cleared): who wrote last and why. The writer sets st_why
 * before its st_save (cr_settings.c crs_why for the automatic ones: st_why_loud, something sounded in the second
 * before; the guard should make st_auto_loud 0 always). Each st_save / loop slot write is one erase */
#define ST_WHY_OTHER 0u                         /* a sound saved, a store's own sector .. */
#define ST_WHY_AUTO_OPT 1u                      /* the Options poll (cr_settings_poll) */
#define ST_WHY_AUTO_FAV 2u                      /* the favourites poll (crf_poll) */
#define ST_WHY_GSAVE 3u                         /* the global save (SAVE held) or Reset Settings: cr_settings_commit */
#define ST_WHY_SAVE 4u                          /* cr_settings_save: the calibration, the update gesture */
#define ST_WHY_PUT 5u                           /* a backup's restore (PUT commit) */
#define ST_WHY_LOOP 6u                          /* a loop slot (cr_ui.c crl_fl_save / crl_fl_delete) */
#ifndef ST_NOW_MS
#define ST_NOW_MS() 0u                          /* (a host test without core.h's clock) */
#endif
static uint32_t st_why, st_why_loud;
static uint32_t st_last_why, st_last_obj, st_last_ms, st_auto_loud;
static void st_note(uint32_t obj, uint32_t why)   /* an erase of obj (0x100 + k: a loop slot) is about to start */
{
    st_last_why = why;
    st_last_obj = obj;
    st_last_ms = ST_NOW_MS() | 1u;              /* (0: none yet) */
    if (st_why_loud && (why == ST_WHY_AUTO_OPT || why == ST_WHY_AUTO_FAV))
        st_auto_loud++;
}

/* zlib CRC-32, 4 bits per step, continuing crc (0: a fresh one; st_crc32(a ++ b) == st_crc_run(st_crc32(a), b)) */
static uint32_t st_crc_run(uint32_t crc, const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return st_crc_run(0u, p, n); }

#if BOARD_SMK37
/* SMK-37 (SMK37.md "Storage"; genkigenki's layout of PR #14 for what 0.22 stored, kept so that a unit that ran it keeps
 * its data, and 0.24's favourites and sound pool added in what was free). The FM-1's places are the SMK's own areas
 * (its app area ends at 0x9CFFF, its settings area is the stock resources): every object moves, A/B neighbours as on
 * the FM-1. Only inside hal/fm1_flash_map.h's windows FL_DATA, FL_SMP and FL_TAIL (FL_STORE_OK), never in FL_OTA
 * (0xBB000..0xBFFFF, the update's staging), BTIF / USRTRIM (0xC0000..0xC1FFF), the stock resources (0xF4000..0xFDFFF)
 * or key_mac (0xFF000..).
 *   FL_DATA 0x9D000..0xBAFFF (the stock VM, 30 sectors)
 *     0x9D000..0xA8FFF  free (the place of 0.21's sound objects: nothing reads or writes it since HISTORY 3y231)
 *     0xA9000 / 0xAA000 OBJ_FM6BANK          0xAB000 / 0xAC000 OBJ_SETTINGS     0xAD000 / 0xAE000 OBJ_SESSION
 *     0xAF000           the store owner tag (st_owner_check: the same place in every ported firmware)
 *     0xB0000..0xB3FFF  free (the same)
 *     0xB4000 / 0xB5000 OBJ_FAVS (0.24 on the SMK)
 *     0xB6000..0xBAFFF  the sound pool's sectors 0..4 (sp_*, below)
 *   FL_SMP 0xC2000..0xE9FFF (USRFLASH, the stock's user area, 40 sectors: ChoralRoot has no samples)
 *     0xC2000..0xD1FFF  OBJ_CZBANK0..+7      0xD2000..0xE5FFF the loop slots (cr_ui.c CRL_FL_BASE, 2 x CRL_SLOTS)
 *     0xE6000..0xE9FFF  the sound pool's sectors 5..8
 *   0xEA000 (USRFLASH's last sector: the stock firmware's end marker, no data of ChoralRoot's)
 *   FL_TAIL 0xEB000..0xF1FFF (the free tail after it, 7 of its 9 sectors)
 *     0xEB000..0xF1FFF  the sound pool's sectors 9..15 (0xF2000..0xF3FFF stay free)
 * The pool's 16 floating sectors are three runs (sp_addr): 5 in FL_DATA, 4 at FL_SMP's end, 7 in FL_TAIL. */
#define ST_SMK_BASE 0x9D000u
#define ST_SMK_PAIRS 9u                        /* the pairs below the owner tag */
#define ST_SMK_SECTORS (2u * ST_SMK_PAIRS)
#define ST_SMK_HIGH 0xB0000u                   /* the pairs above it: CZ-1 store 2, QUAD store, the favourites */
#define ST_SMK_HIGH_SECTORS 6u
#define ST_SMK_CZB 0xC2000u                    /* the CZ-1 banks, in USRFLASH */
#define ST_SMK_CZB_SECTORS 16u
#define ST_SMK_LOOPS 0xD2000u                  /* the loop slots (cr_ui.c CRL_FL_BASE), in USRFLASH */
#define ST_SMK_LOOP_SECTORS 20u
#define ST_SMK_POOL0 0xB6000u                  /* the sound pool: sectors 0 .. ST_SMK_POOL0_N - 1 in FL_DATA */
#define ST_SMK_POOL0_N 5u
#define ST_SMK_POOL1 0xE6000u                  /* .. sectors 5..8 at the end of FL_SMP */
#define ST_SMK_POOL1_N 4u
#define ST_SMK_POOL2 0xEB000u                  /* .. sectors 9..15 in FL_TAIL */
#define ST_SMK_POOL2_N 7u
/* every place of ChoralRoot's data on the SMK (base, sectors): the owner check and Options > Flash Data clear these
 * (the owner tag is not one of them) */
static const uint32_t ST_SMK_REGIONS[][2] = {{ST_SMK_BASE, ST_SMK_SECTORS}, {ST_SMK_HIGH, ST_SMK_HIGH_SECTORS},
                                             {ST_SMK_CZB, ST_SMK_CZB_SECTORS}, {ST_SMK_LOOPS, ST_SMK_LOOP_SECTORS},
                                             {ST_SMK_POOL0, ST_SMK_POOL0_N}, {ST_SMK_POOL1, ST_SMK_POOL1_N},
                                             {ST_SMK_POOL2, ST_SMK_POOL2_N}};
#if FELUCCA_CZ && FELUCCA_QUAD
_Static_assert(OBJ_COUNT == 20u && OBJ_FAVS + 1u == OBJ_COUNT, "SMK store: a new object needs a place");
#endif
_Static_assert(ST_SMK_BASE == FL_DATA_LO && ST_SMK_BASE + ST_SMK_SECTORS * ST_SECTOR == 0xAF000u,
               "SMK store: the objects end at the owner tag");
_Static_assert(ST_SMK_HIGH >= 0xAF000u + ST_SECTOR && ST_SMK_HIGH + ST_SMK_HIGH_SECTORS * ST_SECTOR <= ST_SMK_POOL0,
               "SMK store: the pairs above the owner tag run into the pool");
_Static_assert(ST_SMK_POOL0 + ST_SMK_POOL0_N * ST_SECTOR <= FL_DATA_HI, "SMK store: the pool's first run past FL_DATA");
_Static_assert(ST_SMK_CZB >= FL_SMP_LO && ST_SMK_CZB + ST_SMK_CZB_SECTORS * ST_SECTOR <= ST_SMK_LOOPS &&
               ST_SMK_LOOPS + ST_SMK_LOOP_SECTORS * ST_SECTOR <= ST_SMK_POOL1, "SMK store: past the loops");
_Static_assert(ST_SMK_POOL1 + ST_SMK_POOL1_N * ST_SECTOR == FL_SMP_HI, "SMK store: the pool's second run past FL_SMP");
_Static_assert(ST_SMK_POOL2 == FL_TAIL_LO && ST_SMK_POOL2 + ST_SMK_POOL2_N * ST_SECTOR <= FL_TAIL_HI,
               "SMK store: the pool's third run outside FL_TAIL");
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1); a live object */
{
    copy &= 1u;
    if (!st_live(obj))
        return ST_NOWHERE;
    if (obj == OBJ_SESSION)
        return ST_SMK_BASE + (8u * 2u + copy) * ST_SECTOR;
    if (obj == OBJ_FAVS)
        return ST_SMK_HIGH + (4u + copy) * ST_SECTOR;
    if (obj >= OBJ_CZBANK0 && obj < OBJ_CZBANK0 + 8u)
        return ST_SMK_CZB + ((obj - OBJ_CZBANK0) * 2u + copy) * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return ST_SMK_BASE + (6u * 2u + copy) * ST_SECTOR;
    return ST_SMK_BASE + (7u * 2u + copy) * ST_SECTOR;   /* OBJ_SETTINGS */
}

/* The owner of the store. Several FM-1 firmwares are ported to the SMK-37 with the same board layer and this same
 * store area, each with its own object formats: a firmware must never parse another's data. The sector after the
 * objects (0xAF000, the same place in every port) holds "SMK37DATA" + the firmware family (FELUCCA_DATA_FAMILY,
 * build.py; ChoralRoot: CHORAL). Ours: load as ever. None (erased): data written before the tag existed, kept only
 * with FELUCCA_DATA_UNTAGGED_OK (0 here: the SMK build never had untagged data, so an erased tag with objects
 * present is another firmware's). Another family's: clear every place ChoralRoot keeps data in (the objects, the
 * CZ-1 banks, the loop slots, the favourites and the sound pool: st_smk_clear) and take the store. Calibration, BT pairing and key_mac are outside
 * them: never touched. Called first in persist_boot (cr_shim.c), before any store read; never in SAFE MODE. */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "CHORAL"
#endif
#ifndef FELUCCA_DATA_UNTAGGED_OK
#define FELUCCA_DATA_UNTAGGED_OK 0
#endif
#define ST_OWNER_OFF 0xAF000u
_Static_assert(ST_OWNER_OFF == ST_SMK_BASE + ST_SMK_SECTORS * ST_SECTOR, "SMK store owner: the sector after the objects");
static uint8_t st_owner_cleared;                /* (the console says so) */
#ifndef ST_FEED
#define ST_FEED() ((void)0)                     /* (the device: the watchdog, between erases) */
#endif
/* The stock firmware's mapping in USRFLASH (global settings at 0xC2000, magic A3 3A; 8 presets from 0xC3000 every
 * 0x5000, 0x29E3 bytes each, the rest erased; the end marker "usrdata\0" at 0xEAFF8, FL_STOCKTAG): the stock firmware
 * trusts the area while the marker is there and never rebuilds it, so after a return to stock ChoralRoot's banks and
 * loops would be its key, knob and fader mapping (every key one note). So: an intact stock mapping is kept (it reads
 * as empty banks and loops), and the marker is released (its sector erased: the stock firmware rebuilds its factory
 * mapping at its next start) before ChoralRoot's first erase or program in USRFLASH (storage_hw.c st_erase, st_prog)
 * and at boot when it stands over anything else (st_owner_check: ours, an older build's, another firmware's). */
static int st_stock_tagged(void)
{
    static const uint8_t TAG[8] = {'u', 's', 'r', 'd', 'a', 't', 'a', 0};
    uint8_t t[8];
    uint32_t i;
    if (st_read(FL_STOCKTAG_OFF, t, 8))
        return 0;
    for (i = 0; i < 8u; i++)
        if (t[i] != TAG[i])
            return 0;
    return 1;
}
static int st_stock_intact(void)                /* the marker over the stock layout */
{
    uint8_t b[8];
    uint32_t n, k, i, ff;
    if (!st_stock_tagged() || st_read(FL_SMP_LO, b, 2) || b[0] != 0xA3u || b[1] != 0x3Au)
        return 0;
    for (n = 0; n < 8u; n++)                    /* each preset: its head written, its last two sectors erased */
        for (k = 0; k < 3u; k++) {
            if (st_read(FL_SMP_LO + 0x1000u + n * 0x5000u + (k ? 0x2000u + k * 0x1000u : 0u), b, 8))
                return 0;
            for (ff = 1, i = 0; i < 8u; i++)
                ff &= b[i] == 0xFFu;
            if (ff != (k != 0u))
                return 0;
        }
    return 1;
}
static int st_stock_release(void)               /* erase the marker's sector, once: later calls find no marker. Not
                                                   * gone (a flash error): -10, and storage_hw.c refuses the USRFLASH
                                                   * write it was asked for, so the stock firmware never trusts a
                                                   * mapping ChoralRoot has written over */
{
    if (!st_stock_tagged())
        return 0;
    st_erase(FL_STOCKTAG_LO);
    return st_stock_tagged() ? -10 : 0;
}
static inline int st_in_usrflash(uint32_t off) { return off >= FL_SMP_LO && off < FL_SMP_HI; }
static void st_smk_clear(void)
{
    uint32_t r, k, keep = st_stock_intact();   /* the stock mapping: kept until ChoralRoot stores something there */
    for (r = 0; r < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; r++)
        for (k = 0; k < ST_SMK_REGIONS[r][1]; k++) {
            if (keep && st_in_usrflash(ST_SMK_REGIONS[r][0]))
                break;
            ST_FEED();
            st_erase(ST_SMK_REGIONS[r][0] + k * ST_SECTOR);
        }
}
static void st_owner_check(void)
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t tag[16], want[16];
    uint32_t i, erased = 1, ours = 1;
    for (i = 0; i < 16u; i++)
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    if (!st_stock_intact())
        st_stock_release();                     /* the marker over anything but the stock mapping: let it rebuild */
    if (st_read(ST_OWNER_OFF, tag, 16))
        return;
    for (i = 0; i < 16u; i++) {
        erased &= tag[i] == 0xFFu;
        ours &= tag[i] == want[i];
    }
    if (ours)
        return;
    if (!erased || !FELUCCA_DATA_UNTAGGED_OK) {     /* another firmware's store (or untagged data): clear it */
        st_smk_clear();
        st_owner_cleared = 1;
    }
    st_erase(ST_OWNER_OFF);
    st_prog(ST_OWNER_OFF, want, 16);
}
#else
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1); a live object */
{
    copy &= 1u;
    if (!st_live(obj))
        return ST_NOWHERE;
    if (obj == OBJ_FM6BANK)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_SESSION)
        return 0xB4000u + copy * ST_SECTOR;
    if (obj == OBJ_FAVS)
        return 0xB6000u + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0 && obj < OBJ_CZBANK0 + 8u)
        return 0xA2000u + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0xFC000u + copy * ST_SECTOR;                  /* OBJ_SETTINGS */
}
#endif /* BOARD_SMK37 */

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (!st_live(obj) || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (ST_BLOCKED() || !st_live(obj) || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* ---- erase ahead (see the top) ---- */
#ifndef ST_AHEAD
#define ST_AHEAD 0                                 /* (ChoralRoot's unit sets 1: choralroot.c. Off, every save erases
                                                    * its copy first, as Felucca's writer always did) */
#endif
static inline int st_ahead_ix(uint32_t obj)        /* the objects whose spare copy is erased ahead: 0.., else -1 */
{
    if (!ST_AHEAD)
        return -1;
    /* (0: was the settings. Not erased ahead, the owner's decision of 2026-10-14: the settings hold the panel
     * calibration, and their previous copy stays as the fallback until the next save, which erases at its time) */
#if FELUCCA_CZ && FELUCCA_QUAD
    if (obj == OBJ_SESSION)
        return 1;
    if (obj == OBJ_FAVS)
        return 2;
#endif
    return -1;
}
static uint8_t st_ahead_ok[4];                     /* [ix]: st_prep found or made the spare copy blank and nothing
                                                    * wrote the object since (this power-on). A hint for WHEN to
                                                    * write (st_ready): st_save reads the sector itself. [3]: st_prep
                                                    * runs at power-on (no audio yet: the emulator's flash model) */
static inline int st_ready(uint32_t obj)
{
    int ix = st_ahead_ix(obj);
    return ix >= 0 && st_ahead_ok[ix];
}
static int st_blank(uint32_t base)                 /* the whole sector reads 0xFF: 1 (st_buf is the scratch) */
{
    uint32_t off, i;
    for (off = 0; off < ST_SECTOR; off += 256u) {
        if (st_read(base + off, st_buf, 256u))
            return 0;
        for (i = 0; i < 256u; i++)
            if (st_buf[i] != 0xFFu)
                return 0;
    }
    return 1;
}
/* the copy a save of obj would write, made blank now: 0 it was blank already (no erase), 1 erased, < 0 not one of
 * the erase-ahead objects, SAFE MODE, or a flash error (the copy is then not marked ready: st_save erases it).
 * The copy erased is never the current one: st_current has just read the current copy's commit record and its
 * payload's CRC; with no valid copy at all it is copy A, where the first save goes */
static inline int st_prep(uint32_t obj)
{
    st_hdr_t h;
    int ix = st_ahead_ix(obj), rc;
    uint32_t base;
    if (ix < 0 || ST_BLOCKED())
        return -1;
    st_ahead_ok[ix] = 0;
    base = st_sector(obj, st_current(obj, &h) == 0 ? 1u : 0u);
    if (st_blank(base)) {
        st_ahead_ok[ix] = 1;
        return 0;
    }
    if ((rc = st_erase(base)) != 0)
        return rc;
    if (!st_blank(base))                           /* (a write-protected or failing part: not ready, and the caller
                                                    * does not try again and again) */
        return -7;
    st_ahead_ok[ix] = 1;
    return 1;
}

/* WHEN the unit may write by itself (cr_settings.c's poll asks; pure, so the host tests walk it: tests/storage_test.c).
 * All times are ms on one clock. An automatic WRITE of a changed object: nothing changed for ST_SETTLE_MS (so a run
 * of changes is one write), nothing sounded for ST_SILENT_MS, ST_GAP_MS since another write, not busy (a loop plays,
 * a restore); and then, when the spare copy is blank (program only: a few page programs, each masking the interrupts
 * for less than one audio half), at once; when it is not (the save has to erase: 17.8 ms masked), only after
 * ST_ERASE_IDLE_MS without a sound AND without input (a key, a button, a knob, a MIDI message), so that the hole
 * does not fall on the next note. An ERASE AHEAD (st_prep, nothing to write): after ST_AHEAD_IDLE_MS of the same. */
#define ST_SETTLE_MS 1500u
#define ST_SILENT_MS 1000u
#define ST_GAP_MS 250u
#define ST_ERASE_IDLE_MS 4000u
#define ST_AHEAD_IDLE_MS 10000u
typedef struct {
    uint32_t now, change, loud, input, wrote;      /* now; the last change, sound, input, flash write */
    uint8_t busy, ready;                           /* no write now at all; the spare copy is blank (st_ready) */
} st_when_t;
static inline int st_idle_for(const st_when_t *w, uint32_t ms)
{
    return (uint32_t)(w->now - w->loud) >= ms && (uint32_t)(w->now - w->input) >= ms;
}
static inline int st_write_due(const st_when_t *w)
{
    if (w->busy || (uint32_t)(w->now - w->change) < ST_SETTLE_MS || (uint32_t)(w->now - w->loud) < ST_SILENT_MS ||
        (uint32_t)(w->now - w->wrote) < ST_GAP_MS)
        return 0;
    return w->ready || st_idle_for(w, ST_ERASE_IDLE_MS);
}
static inline int st_ahead_due(const st_when_t *w)
{
    return !w->busy && !w->ready && (uint32_t)(w->now - w->wrote) >= ST_GAP_MS && st_idle_for(w, ST_AHEAD_IDLE_MS);
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc, ix, blank = 0;
    st_hdr_t h;
    if (ST_BLOCKED() || !st_live(obj) || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    if ((ix = st_ahead_ix(obj)) >= 0) {              /* erased ahead (st_prep), or never written: program only */
        st_ahead_ok[ix] = 0;
        blank = st_blank(base);
    }
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if (!blank) {                                    /* (3y48: only a save that erases is noted) */
        st_note(obj, st_why);
        if ((rc = st_erase(base)) != 0)
            return rc;
    }
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    if (ix >= 0 && cur < 0)                          /* the object's first save ever (copy A): the spare is copy B,
                                                      * blank on a new unit; after every other save it is the copy
                                                      * that was current, to be erased ahead */
        st_ahead_ok[ix] = (uint8_t)st_blank(st_sector(obj, 1u));
    return 0;
}

#if FELUCCA_CZ && FELUCCA_QUAD && ((defined(UP_POOL) && UP_POOL) || defined(SP_TEST))   /* (core.h UP_POOL; tests/pool_test.c) */
/* ---- THE SOUND POOL (ChoralRoot 0.22: docs/PRESETS.md "The pool"): FLOATING objects ----
 * 0xB8000..0xC7FFF (SMK-37: three runs, sp_addr), SP_NSEC sectors, hold SP_NBANK logical banks of SP_PER slots of SP_SLOT bytes (upool.c: a slot is
 * a user record and one engine's patch). A sector is a bank's copy when its st_hdr_t is valid: ST_MAGIC, type SP_TYPE,
 * slot = the logical bank, len ST_PAYLOAD_MAX, rsv[0] SP_VER, the header CRC; and its payload's CRC matches.
 * LOAD (sp_scan): per bank the valid copy with the newest seq wins (serial arithmetic, so seq wraps; equal seq,
 * which this writer never makes: the lower sector); a copy whose payload CRC fails is skipped for the next newest.
 * Every save takes seq = the newest seq of ANY valid pool header + 1, so seq orders all the pool's writes.
 * SAVE (sp_commit, sp_write): the new copy goes to the TARGET, the first sector after the newest one (cyclic) that holds no
 * bank's current copy (16 - 13 = at least 3 such sectors, so the target always exists and wear walks round all 16);
 * if it is not blank it is erased (the only erase; st_prep's rule: sp_prep erases it ahead), then the ten slots
 * are programmed from the current copy slot by slot through SP_SLOT bytes of st_buf (each in the two pieces that fall
 * in one page), the changed bytes substituted, and the commit record LAST, then everything read back. Until the header is programmed the old copy is the newest valid one;
 * after, the new one is; the old copy is left as it is until its sector becomes a target. A cut anywhere: the old
 * bank or the new one, never a mix. The retired objects' sectors (above) are other sectors */
#define SP_BASE 0xB8000u
#define SP_NSEC 16u
#define SP_NBANK 13u
#define SP_PER 10u
#define SP_SLOT 384u
#define SP_SLOTS (SP_NBANK * SP_PER)            /* 130: upool.c offers UPOOL_SLOTS of them */
#define SP_TYPE 0x4C50u                         /* "PL" (never an OBJ_*: st_head of an older firmware rejects it) */
#define SP_VER 1u
#define SP_NONE 0xFFu
_Static_assert(SP_PER * SP_SLOT == ST_PAYLOAD_MAX, "a pool bank fills one sector's payload");
_Static_assert(SP_NSEC >= SP_NBANK + 2u, "the pool needs a target besides the spare");
static uint8_t sp_cur[SP_NBANK];                /* the sector of bank b's current copy (SP_NONE: none) */
static uint8_t sp_last = SP_NONE;               /* the sector of the newest valid header */
static uint32_t sp_top;                         /* its seq */
static uint8_t sp_scanned, sp_spare_ok;         /* sp_scan ran; the target is known blank (sp_prep, cleared by a save) */
static uint32_t sp_erases, sp_progs;            /* (the host test's counters: erases and programs by the pool) */

static inline int sp_newer(uint32_t a, uint32_t b) { return a != b && a - b < 0x80000000u; }
#if BOARD_SMK37
_Static_assert(ST_SMK_POOL0_N + ST_SMK_POOL1_N + ST_SMK_POOL2_N == SP_NSEC, "SMK store: the pool's runs hold its sectors");
static uint32_t sp_addr(uint32_t s)             /* (SMK-37: three runs, ST_SMK_POOL0 / 1 / 2 above) */
{
    if (s < ST_SMK_POOL0_N)
        return ST_SMK_POOL0 + s * ST_SECTOR;
    s -= ST_SMK_POOL0_N;
    return s < ST_SMK_POOL1_N ? ST_SMK_POOL1 + s * ST_SECTOR : ST_SMK_POOL2 + (s - ST_SMK_POOL1_N) * ST_SECTOR;
}
#else
static uint32_t sp_addr(uint32_t s) { return SP_BASE + s * ST_SECTOR; }
#endif

static int sp_head(uint32_t s, st_hdr_t *h)    /* sector s holds a valid pool commit record: 0 */
{
    if (st_read(sp_addr(s), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != SP_TYPE || h->slot >= SP_NBANK || h->len != ST_PAYLOAD_MAX ||
        h->rsv[0] != SP_VER || h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}
static int sp_body(uint32_t s, uint32_t crc)   /* the payload's CRC is crc: 0 (st_buf's first 256 bytes: scratch) */
{
    uint32_t off, c = 0;
    for (off = 0; off < ST_PAYLOAD_MAX; off += 256u) {
        if (st_read(sp_addr(s) + ST_PAYLOAD_OFF + off, st_buf, 256u))
            return -1;
        c = st_crc_run(c, st_buf, 256u);
    }
    return c == crc ? 0 : -1;
}

#if BOARD_SMK37
/* 0.24-beta2-smk37's and 0.24-beta3-smk37's FL_TAIL began at 0xEA000, the stock marker's sector: their pool sector 9
 * was there, their 10..15 are this build's 9..14 as they are (a pool sector carries no number). A valid pool commit
 * record at 0xEA000 is moved to sector 15 (0xF1000, never written by those builds) when that is blank: the pages copied, the record's page last, read
 * back; then 0xEA000 is erased (FL_STOCKTAG, its one erase). A cut before the erase: the next scan finds the same
 * copy at 0xF1000 and erases 0xEA000. A cut during the copy leaves sector 15 written without its commit record (the
 * record's page goes last): such a sector is no pool copy (sp_scan skips it, sp_target erases it before use), so it is
 * erased and the copy made again; a sector 15 that holds a valid record of its own is left, and 0xEA000 with it. Once
 * a power-on (sp_scan). */
static uint8_t sp_old_done;
static void sp_old_move(void)
{
    st_hdr_t h, t;
    uint32_t to = sp_addr(SP_NSEC - 1u), off, same = 1;
    if (sp_old_done || ST_BLOCKED())
        return;
    sp_old_done = 1;
    if (st_read(FL_STOCKTAG_LO, &h, sizeof h) || h.magic != ST_MAGIC || h.type != SP_TYPE || h.slot >= SP_NBANK ||
        h.len != ST_PAYLOAD_MAX || h.rsv[0] != SP_VER || h.hcrc != st_crc32(&h, sizeof h - 4u))
        return;
    if (!st_blank(to) && sp_head(SP_NSEC - 1u, &t) && st_erase(to))   /* (a copy the power cut: again) */
        return;
    if (st_blank(to))
        for (off = ST_SECTOR; off; ) {          /* the record's page (0) last */
            off -= 256u;
            if (st_read(FL_STOCKTAG_LO + off, st_buf, 256u) || st_prog(to + off, st_buf, 256u))
                return;
        }
    for (off = 0; off < ST_SECTOR && same; off += 256u) {
        if (st_read(FL_STOCKTAG_LO + off, st_buf, 256u) || st_read(to + off, st_buf + 256u, 256u))
            return;
        same = !memcmp(st_buf, st_buf + 256u, 256u);
    }
    if (same)
        st_erase(FL_STOCKTAG_LO);
}
#endif
/* the 16 commit records -> sp_cur, sp_top, sp_last (power-on; at most 16 header reads and one payload read per bank,
 * more only when a payload fails its CRC) */
static __attribute__((unused)) void sp_scan(void)
{
    st_hdr_t h;
    uint32_t seq[SP_NSEC], crc[SP_NSEC], s, b;
    uint8_t bank[SP_NSEC];
#if BOARD_SMK37
    sp_old_move();
#endif
    sp_last = SP_NONE;
    sp_top = 0;
    sp_spare_ok = 0;
    for (s = 0; s < SP_NSEC; s++) {
        bank[s] = SP_NONE;
        if (ST_BLOCKED() || sp_head(s, &h))
            continue;
        bank[s] = (uint8_t)h.slot;
        seq[s] = h.seq;
        crc[s] = h.crc;
        if (sp_last == SP_NONE || sp_newer(h.seq, sp_top)) {
            sp_top = h.seq;
            sp_last = (uint8_t)s;
        }
    }
    for (b = 0; b < SP_NBANK; b++) {
        sp_cur[b] = SP_NONE;
        for (;;) {                                  /* the newest unrejected candidate; its payload checked */
            uint32_t best = SP_NONE;
            for (s = 0; s < SP_NSEC; s++)
                if (bank[s] == b && (best == SP_NONE || sp_newer(seq[s], seq[best])))
                    best = s;
            if (best == SP_NONE)
                break;
            if (sp_body(best, crc[best]) == 0) {
                sp_cur[b] = (uint8_t)best;
                break;
            }
            bank[best] = SP_NONE;
        }
    }
    sp_scanned = 1;
}
static inline void sp_ensure(void) { if (!sp_scanned) sp_scan(); }

static int sp_held(uint32_t s)                  /* sector s is some bank's current copy */
{
    uint32_t b;
    for (b = 0; b < SP_NBANK; b++)
        if (sp_cur[b] == s)
            return 1;
    return 0;
}
static uint32_t sp_target(void)                 /* where the next save goes (see the top) */
{
    uint32_t i, s = sp_last == SP_NONE ? SP_NSEC - 1u : sp_last;
    for (i = 0; i < SP_NSEC; i++) {
        s = (s + 1u) % SP_NSEC;
        if (!sp_held(s))
            return s;
    }
    return 0;                                   /* (cannot happen: SP_NSEC > SP_NBANK) */
}
static __attribute__((unused)) int sp_used(uint32_t b) { sp_ensure(); return b < SP_NBANK && sp_cur[b] != SP_NONE; }

/* n bytes at off of slot k's SP_SLOT -> dst; a slot of a bank never written reads 0: 1, read: 0, error -1.
 * NEVER from the audio ISR (st_read) */
static __attribute__((unused)) int sp_read(uint32_t k, uint32_t off, void *dst, uint32_t n)
{
    uint32_t b = k / SP_PER;
    if (ST_BLOCKED() || k >= SP_SLOTS || off + n > SP_SLOT)
        return -1;
    sp_ensure();
    if (sp_cur[b] == SP_NONE) {
        memset(dst, 0, n);
        return 1;
    }
    return st_read(sp_addr(sp_cur[b]) + ST_PAYLOAD_OFF + (k % SP_PER) * SP_SLOT + off, dst, n) ? -1 : 0;
}

/* the target made blank now (the quiet writes: at power-on, after a long idle): 0 it was, 1 erased, < 0 error */
static __attribute__((unused)) int sp_prep(void)
{
    uint32_t t;
    int rc;
    if (ST_BLOCKED())
        return -1;
    sp_ensure();
    sp_spare_ok = 0;
    t = sp_target();
    if (st_blank(sp_addr(t))) {
        sp_spare_ok = 1;
        return 0;
    }
    st_note(0x200u + t, st_why);
    sp_erases++;
    if ((rc = st_erase(sp_addr(t))) != 0)
        return rc;
    if (!st_blank(sp_addr(t)))
        return -7;
    sp_spare_ok = 1;
    return 1;
}
static inline int sp_ready(void) { return sp_scanned && sp_spare_ok; }

/* bank b written anew (see the top): slot j's SP_SLOT bytes are the current copy's (zeros: no copy yet), then
 * fill(j, slot) may change them in place (slot: st_buf, SP_SLOT bytes; fill must not call st_* functions that use
 * st_buf, st_read is fine; a non-zero return aborts, nothing committed). Each slot is programmed in the pieces that
 * fall in one page (two a slot), the commit record last. 0 ok, < 0 nothing changed (the old copy is current; -7:
 * written but not read back) */
static __attribute__((unused)) int sp_commit(uint32_t b, int (*fill)(uint32_t j, uint8_t *slot))
{
    st_hdr_t h;
    uint32_t t, old, j, crc = 0;
    int rc;
    if (ST_BLOCKED() || b >= SP_NBANK)
        return -1;
    sp_ensure();
    old = sp_cur[b];
    t = sp_target();
    sp_spare_ok = 0;
    sp_scanned = 0;                             /* (until the end: after any failure the next access scans) */
    if (!st_blank(sp_addr(t))) {
        st_note(0x200u + t, st_why);
        sp_erases++;
        if ((rc = st_erase(sp_addr(t))) != 0)
            return rc;
    }
    for (j = 0; j < SP_PER; j++) {
        uint32_t at = j * SP_SLOT, done = 0;
        if (old == SP_NONE)
            memset(st_buf, 0, SP_SLOT);
        else if (st_read(sp_addr(old) + ST_PAYLOAD_OFF + at, st_buf, SP_SLOT))
            return -1;
        if (fill && (rc = fill(j, st_buf)) != 0)
            return rc;
        crc = st_crc_run(crc, st_buf, SP_SLOT);
        while (done < SP_SLOT) {                /* the pieces of the slot that fall in one page */
            uint32_t a = at + done, n = 256u - (a & 255u);
            if (n > SP_SLOT - done)
                n = SP_SLOT - done;
            sp_progs++;
            if ((rc = st_prog(sp_addr(t) + ST_PAYLOAD_OFF + a, st_buf + done, n)) != 0)
                return rc;
            done += n;
        }
    }
    memset(&h, 0xFF, sizeof h);
    h.magic = ST_MAGIC;
    h.type = SP_TYPE;
    h.slot = (uint16_t)b;
    h.seq = sp_last == SP_NONE ? 1u : sp_top + 1u;
    h.len = ST_PAYLOAD_MAX;
    h.crc = crc;
    h.rsv[0] = SP_VER;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    sp_progs++;
    if ((rc = st_prog(sp_addr(t), &h, sizeof h)) != 0)   /* the commit record, last */
        return rc;
    {
        st_hdr_t chk;
        if (sp_head(t, &chk) || memcmp(&chk, &h, sizeof h) || sp_body(t, crc))
            return -7;
    }
    sp_cur[b] = (uint8_t)t;
    sp_top = h.seq;
    sp_last = (uint8_t)t;
    sp_scanned = 1;
    return 0;
}

/* slot k's bytes [off, off + n) = src (0: zeros), the rest of its bank as it is (sp_commit's result) */
static struct { uint32_t j, off, n; const uint8_t *src; } sp_sub;
static int sp_sub_fill(uint32_t j, uint8_t *slot)
{
    if (j == sp_sub.j) {
        if (sp_sub.src)
            memcpy(slot + sp_sub.off, sp_sub.src, sp_sub.n);
        else
            memset(slot + sp_sub.off, 0, sp_sub.n);
    }
    return 0;
}
static __attribute__((unused)) int sp_write(uint32_t k, uint32_t off, const void *src, uint32_t n)
{
    if (ST_BLOCKED() || k >= SP_SLOTS || off + n > SP_SLOT)
        return -1;
    sp_sub.j = k % SP_PER;
    sp_sub.off = off;
    sp_sub.n = n;
    sp_sub.src = src;
    return sp_commit(k / SP_PER, sp_sub_fill);
}
#endif
