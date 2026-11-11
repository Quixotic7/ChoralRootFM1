/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* ChoralRoot on the M-Vave SMK-37, the store owner tag (storage.c st_owner_check, build with -DBOARD_SMK37=1): a
 * simulated flash with another firmware's store, an untagged one and ChoralRoot's own; only ChoralRoot's data sectors
 * (ST_SMK_REGIONS) and the tag sector are ever erased, ours is kept, the staging area, the calibration and the rest
 * are never touched. The family is FELUCCA_DATA_FAMILY (build.py: CHORAL). 0.24: the regions take in the favourites
 * and the sound pool's runs (0xB4000..0xBAFFF, 0xE6000..0xE9FFF, 0xEB000..0xF1FFF); USRFLASH's last sector 0xEA000,
 * the free tail's rest (0xF2000..0xF3FFF), the stock resources and key_mac stay as they are. The stock firmware's
 * mapping in USRFLASH: kept while intact, its marker sector 0xEA000 erased (only ever erased) before ChoralRoot's
 * first write there and at boot over other data; 0.24-beta2/beta3-smk37's pool sector at 0xEA000 moved to 0xF1000. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#define SP_TEST 1                                  /* storage.c's sound pool: the old sector 9's move (sp_old_move) */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "CHORAL"
#endif

static uint8_t nor[0x100000];
static int bad_write;
static int tag_erases, usr_before_tag;
static int pool_progs, pool_last;                  /* programs into the pool's sector 15 (0xF1000), the last page's */
static int pool_erases;                            /* .. and its erases */
static int tag_erase_fails;                        /* the marker sector's erase fails (a flash error) */
static int in_regions(uint32_t s);
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !(in_regions(off) || off == 0xAF000u || off == 0xEA000u))
        bad_write++;
    if (off == 0xEA000u && tag_erase_fails)
        return -2;
    if (off == 0xEA000u)
        tag_erases++;
    else if (off == 0xF1000u)
        pool_erases++;
    else if (off >= 0xC2000u && off < 0xEA000u && nor[0xEAFF8] == 'u')
        usr_before_tag++;                          /* a USRFLASH erase while the stock marker is still there */
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if ((off & ~0xFFFu) == 0xF1000u && n <= 256u && !((off & 0xFFu) + n > 256u)) {
        pool_progs++;
        pool_last = (int)(off & 0xFFFu);
    } else if (off != 0xAF000u)
        bad_write++;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int in_regions(uint32_t s)
{
    uint32_t r;
    for (r = 0; r < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; r++)
        if (s >= ST_SMK_REGIONS[r][0] && s < ST_SMK_REGIONS[r][0] + ST_SMK_REGIONS[r][1] * ST_SECTOR)
            return 1;
    return 0;
}

static int bad;
static void check(const char *what, int ok) { printf("smk owner: %-78s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static void fill(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(nor + 0x9D000, 0xAA, 0xC000);           /* the resource region (3y295): the loader's, never touched */
    memset(nor + 0xA9000, 0x11, 0x6000);           /* objects below the tag */
    memset(nor + 0xB0000, 0x22, 0xB000);           /* the CZ-1 store's second half, the QUAD store, the favourites,
                                                    * the pool's first run */
    memset(nor + 0xBB000, 0x55, 0x5000);           /* the staging (the loader the stub hands over to): never */
    memset(nor + 0xC1000, 0x33, 0x1000);           /* USRTRIM: never touched */
    memset(nor + 0xC2000, 0x44, 0x28000);          /* the CZ-1 banks, the loop slots, the pool's second run */
    memset(nor + 0xEA000, 0x99, 0x1000);           /* USRFLASH's last sector (the stock marker's): not ChoralRoot's */
    memset(nor + 0xEB000, 0x44, 0x7000);           /* the pool's third run, in FL_TAIL */
    memset(nor + 0xF2000, 0x66, 0x2000);           /* the rest of the free tail: not ChoralRoot's, never touched */
    memset(nor + 0xF4000, 0x77, 0xA000);           /* USR, the stock resources: never touched */
    memset(nor + 0xFF000, 0x88, 0x1000);           /* key_mac: never touched */
}
static int all(uint32_t lo, uint32_t hi, uint8_t v)
{
    uint32_t i;
    for (i = lo; i < hi; i++)
        if (nor[i] != v)
            return 0;
    return 1;
}
static void stock(void)                            /* the stock mapping as V016 writes it */
{
    uint32_t n;
    memset(nor + 0xC2000, 0xFF, 0x29000);
    memset(nor + 0xC2000, 0x07, 0x86);
    nor[0xC2000] = 0xA3; nor[0xC2001] = 0x3A;
    for (n = 0; n < 8u; n++) {
        memset(nor + 0xC3000 + n * 0x5000, 0x01, 0x29E3);
        nor[0xC3000 + n * 0x5000 + 4] = 0x7F;
    }
    memcpy(nor + 0xEAFF8, "usrdata", 8);
}
static int is_stock(void)
{
    uint8_t keep[0x29000];
    int same;
    memcpy(keep, nor + 0xC2000, sizeof keep);
    stock();
    same = !memcmp(keep, nor + 0xC2000, sizeof keep);
    memcpy(nor + 0xC2000, keep, sizeof keep);
    return same;
}
static int store_erased(void)
{
    return all(0xA9000, 0xAF000, 0xFF) && all(0xB0000, 0xBB000, 0xFF) && all(0xC2000, 0xEA000, 0xFF) &&
           all(0xEB000, 0xF2000, 0xFF);
}
static int others_kept(void)
{
    return all(0x9D000, 0xA9000, 0xAA) && all(0xBB000, 0xC0000, 0x55) && all(0xC1000, 0xC2000, 0x33) && all(0xEA000, 0xEB000, 0x99) &&
           all(0xF2000, 0xF4000, 0x66) &&
           all(0xF4000, 0xFE000, 0x77) && all(0xFF000, 0x100000, 0x88);
}
static int tagged(void)
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t want[16];
    uint32_t i;
    for (i = 0; i < 16u; i++)                     /* (16 bytes: a 7-letter family fills the tag, no NUL) */
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    return !memcmp(nor + 0xAF000, want, 16) && all(0xAF010, 0xB0000, 0xFF);
}

int main(void)
{
    fill();
    memcpy(nor + 0xAF000, "SMK37DATAFELUCCA", 16);
    st_owner_check();
    check("another family's store (FELUCCA): every ChoralRoot data sector cleared, tagged ours",
          store_erased() && st_owner_cleared && tagged());
    check("... the resource region, the staging, USRTRIM, 0xEA000, the free tail's rest, USR and key_mac untouched", others_kept());
    fill();
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    st_owner_cleared = 0;
    st_owner_check();
    check("another family's store (SLOOP): cleared, tagged ours", store_erased() && st_owner_cleared && tagged() &&
          others_kept());
    fill();
    st_owner_check();                              /* tag the store ours (an untagged one: cleared first) */
    memset(nor + 0xA9000, 0x11, 0x6000);
    memset(nor + 0xC2000, 0x44, 0x28000);
    memset(nor + 0xEB000, 0x44, 0x7000);
    st_owner_cleared = 0;
    st_owner_check();
    check("our own store: kept as it is", all(0xA9000, 0xAF000, 0x11) && all(0x9D000, 0xA9000, 0xAA) && all(0xC2000, 0xEA000, 0x44) &&
          all(0xEB000, 0xF2000, 0x44) &&
          !st_owner_cleared && tagged());
    fill();
    st_owner_check();
    check("an untagged store (FELUCCA_DATA_UNTAGGED_OK 0): cleared, tagged", store_erased() && tagged() &&
          others_kept());
    /* the stock firmware's mapping in USRFLASH (01-hardware 2.3) */
    fill();
    stock();
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    tag_erases = usr_before_tag = 0;
    st_owner_cleared = 0;
    st_owner_check();
    check("owner change over the intact stock mapping: the store cleared, USRFLASH and its marker kept",
          all(0xA9000, 0xAF000, 0xFF) && all(0xB0000, 0xB4000, 0xFF) && is_stock() && all(0x9D000, 0xA9000, 0xAA) && tag_erases == 0 &&
          st_stock_tagged() && st_stock_intact() && tagged());
    st_owner_check();
    check("... and at the next boot (ours now)", is_stock() && tag_erases == 0);
    st_stock_release();                            /* ChoralRoot's first write into USRFLASH (storage_hw.c st_erase) */
    check("first write into USRFLASH: the marker sector erased, once", all(0xEA000, 0xEB000, 0xFF) && tag_erases == 1 &&
          !st_stock_tagged());
    st_stock_release();
    check("... and only once", tag_erases == 1);
    {
        static const uint32_t HIT[] = {0xC2000u, 0xC3000u, 0xC6000u, 0xDB000u, 0xE5000u, 0xEA000u};
        uint32_t h, ok = 1;
        for (h = 0; h < sizeof HIT / sizeof HIT[0]; h++) {
            stock();
            nor[HIT[h]] = 0x5A;                    /* one sector of the stock layout overwritten */
            if (HIT[h] == 0xC3000u)
                memset(nor + 0xC3000, 0xFF, 0x1000);   /* (an erased preset head) */
            ok &= !st_stock_intact();
        }
        check("the stock layout checked: its settings' magic, each preset's head and erased tail", ok);
    }
    fill();
    memset(nor + 0xEA000, 0xFF, 0x1000);
    memcpy(nor + 0xEAFF8, "usrdata", 8);           /* the marker over another firmware's data (0x44): broken stock */
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    tag_erases = usr_before_tag = 0;
    st_owner_check();
    check("owner change, marker over another firmware's data: marker released first, then the store cleared",
          store_erased() && all(0xEA000, 0xEB000, 0xFF) && tag_erases == 1 && usr_before_tag == 0 && tagged());
    stock();
    memset(nor + 0xD2000, 0x44, 0x5000);           /* an older ChoralRoot's loop slots over the stock presets */
    tag_erases = 0;
    st_owner_cleared = 0;
    st_owner_check();
    check("our own store, the marker over our data (an older build): marker released at boot, data kept",
          tag_erases == 1 && !st_stock_tagged() && all(0xD2000, 0xD7000, 0x44) && !st_owner_cleared && tagged());
    fill();
    memset(nor + 0xEA000, 0xFF, 0x1000);
    tag_erases = 0;
    st_owner_check();
    check("no marker: nothing more erased than before", store_erased() && tag_erases == 0);
    {   /* 0.24-beta2/beta3-smk37's pool sector 9 at 0xEA000 (FL_TAIL from there): moved to sector 15, 0xF1000 */
        st_hdr_t h;
        uint8_t old[0x1000];
        uint32_t i;
        fill();
        memset(nor + 0xEA000, 0xFF, 0x1000);
        memset(nor + 0xF1000, 0xFF, 0x1000);
        for (i = 0; i < ST_PAYLOAD_MAX; i++)
            nor[0xEA000 + ST_PAYLOAD_OFF + i] = (uint8_t)(i * 7u + 3u);
        memset(&h, 0, sizeof h);
        h.magic = ST_MAGIC; h.type = SP_TYPE; h.slot = 4; h.seq = 77; h.len = ST_PAYLOAD_MAX;
        h.crc = st_crc32(nor + 0xEA000 + ST_PAYLOAD_OFF, ST_PAYLOAD_MAX); h.rsv[0] = SP_VER;
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + 0xEA000, &h, sizeof h);
        memcpy(old, nor + 0xEA000, sizeof old);
        tag_erases = pool_progs = 0; pool_last = -1;
        sp_old_done = 0; sp_scanned = 0;
        sp_scan();
        check("the old pool sector 9 at 0xEA000: copied to sector 15 (0xF1000), its record's page last, 0xEA000 erased",
              !memcmp(nor + 0xF1000, old, sizeof old) && pool_progs == 16 && pool_last == 0 && tag_erases == 1 &&
              all(0xEA000, 0xEB000, 0xFF) && sp_cur[4] == SP_NSEC - 1u && sp_top == 77u);
        sp_scanned = 0;
        sp_scan();
        check("... once a power-on: the next scan reads nothing at 0xEA000 again", pool_progs == 16 && tag_erases == 1);
        memcpy(nor + 0xEA000, old, sizeof old);    /* a cut after the copy, before the erase */
        pool_progs = 0; tag_erases = 0;
        sp_old_done = 0; sp_scanned = 0;
        sp_scan();
        check("... cut before its erase: the next power-on erases 0xEA000, programs nothing",
              pool_progs == 0 && tag_erases == 1 && all(0xEA000, 0xEB000, 0xFF) && !memcmp(nor + 0xF1000, old, sizeof old));
        memcpy(nor + 0xEA000, old, sizeof old);    /* a cut during the copy: sector 15's pages written but its first
                                                    * (the commit record's), so no valid record there */
        memset(nor + 0xF1000, 0xFF, 0x1000);
        memcpy(nor + 0xF1000 + 0x800, old + 0x800, 0x800);
        pool_progs = 0; tag_erases = 0; pool_erases = 0; pool_last = -1;
        sp_old_done = 0; sp_scanned = 0;
        sp_scan();
        check("... cut during the copy: sector 15 erased, the copy again (record last), then 0xEA000 erased",
              pool_erases == 1 && pool_progs == 16 && pool_last == 0 && tag_erases == 1 && all(0xEA000, 0xEB000, 0xFF) &&
              !memcmp(nor + 0xF1000, old, sizeof old) && sp_cur[4] == SP_NSEC - 1u);
        {   /* sector 15 with a valid record of its own (another bank): nothing moved, nothing erased */
            st_hdr_t o = h;
            memcpy(nor + 0xEA000, old, sizeof old);
            memset(nor + 0xF1000, 0xFF, 0x1000);
            for (i = 0; i < ST_PAYLOAD_MAX; i++)
                nor[0xF1000 + ST_PAYLOAD_OFF + i] = (uint8_t)(i * 5u + 1u);
            o.slot = 6; o.seq = 90;
            o.crc = st_crc32(nor + 0xF1000 + ST_PAYLOAD_OFF, ST_PAYLOAD_MAX);
            o.hcrc = st_crc32(&o, sizeof o - 4u);
            memcpy(nor + 0xF1000, &o, sizeof o);
            pool_progs = 0; tag_erases = 0; pool_erases = 0;
            sp_old_done = 0; sp_scanned = 0;
            sp_scan();
            check("... sector 15 holding a valid record of its own: nothing written, nothing erased",
                  pool_progs == 0 && tag_erases == 0 && pool_erases == 0 && !memcmp(nor + 0xEA000, old, sizeof old) &&
                  sp_cur[6] == SP_NSEC - 1u);
        }
        stock();                                   /* the stock layout at 0xEA000 (its marker): no pool record */
        memset(nor + 0xF1000, 0xFF, 0x1000);
        pool_progs = 0; tag_erases = 0;
        sp_old_done = 0; sp_scanned = 0;
        sp_scan();
        check("... the stock marker's sector (no pool record): left as it is", pool_progs == 0 && tag_erases == 0 &&
              st_stock_tagged() && st_stock_intact());
    }
    stock();                                       /* the marker's erase fails: the release says so (storage_hw.c
                                                    * then refuses the USRFLASH write it was asked for) */
    tag_erase_fails = 1;
    check("a marker that cannot be erased: the release answers -10, the marker and the mapping as they were",
          st_stock_release() == -10 && st_stock_tagged() && st_stock_intact());
    tag_erase_fails = 0;
    check("... erasable again: released, 0", st_stock_release() == 0 && !st_stock_tagged() && st_stock_release() == 0);
    check("nothing written outside ChoralRoot's data sectors, the tag and the stock marker sector", bad_write == 0);
    printf("%s\n", bad ? "SMK OWNER TEST FAILED" : "smk owner test passed");
    return bad != 0;
}
