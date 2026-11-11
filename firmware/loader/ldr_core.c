/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   3. writes ONLY the app area [0x4000, 0x93000), sector by sector, skipping
 *      sectors that are already equal, and verifies each one; the flash head
 *      (SPL, isd_config) is never written;
 *   4. asks 0xF0000000 ("success"), then invalidates the update record (RAM
 *      and flash) so the SPL boots the new app, and resets.
 *
 * The order of 3 for a package with ChoralRoot's boot stub and commit marker
 * (firmware/stub/stub_core.h, docs/BOOT-SAFETY.md "As built"), so that a write
 * cut at any point leaves a unit the stub turns into a rescue:
 *   a. before the first write anywhere else, the marker's sector (0x91000) is
 *      erased: the body is uncommitted, the stub will not boot it;
 *   b. sector 0x4000 (the directory and the stub), only if it differs from the
 *      flash: the one window without a working stub. It is the same bytes in
 *      every build with the same stub, so normally nothing is written here;
 *   c. the body, 0x5000..0x90FFF and 0x92000..0x92FFF;
 *   d. the marker's sector last (its last page holds the marker).
 * A package without the marker is written in address order, as before.
 * The resource region (FM-1, HISTORY 3y234; SMK-37, 3y295; firmware/src/cr_res.h): a package whose flash.bin runs on
 * to LDR_RES_HI carries the region's sectors (0x93000..0x9EFFF; SMK 0x9D000..0xA8FFF) after the app area; they are
 * written in the same pass as the body (c),
 * so after the marker's sector went blank and before the marker: a cut install is uncommitted (the stub rescues, the
 * resumed install writes what differs) and a committed one has its region. An older package (flash.bin LDR_APP_HI
 * long) writes none and leaves whatever is there. The record sweep (4) never looks at the region's sectors.
 * The second resource region (FM-1, HISTORY 3y296; cr_res.h CR_RES2): the package's entry res2.bin (type
 * LDR_RES2_TYPE, exactly LDR_RES2_HI - LDR_RES2_LO bytes) holds flash 0xDC000..0xDFFFF; its sectors are written in the
 * same pass, after the first region's and before the marker, plain as they come. A package without the entry (any
 * before 3y296, the SMK-37's) writes none and leaves them; the record sweep skips them.
 * Power loss during 3 (the SPL does not start the loader from the flash copy
 * of the record on a cold boot: BOOT-SAFETY.md): the stub finds no marker and
 * hands over to the staged loader, or to the ROM's USB boot.
 *
 * Hooks from the platform (loader.c, ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_progress(done, total)
 * plus the ota.c hooks (frames, time, idle). */
#define LDR_APP_LO 0x4000u
#if BOARD_SMK37
/* SMK-37: app area up to the stock PRCT end (the stub, the body and the marker at the FM-1's places; the sectors
 * from 0x93000 up hold what the package holds there); records only in the VM, so the record scan can never erase
 * BTIF / USRTRIM (0xC0000..0xC1FFF, the key calibration) or anything above */
#define LDR_APP_HI 0x9D000u
#define LDR_REC_LO 0x9D000u
#define LDR_REC_HI 0xC0000u
_Static_assert(LDR_APP_HI <= LDR_REC_LO && LDR_REC_HI <= 0xC0000u && LDR_APP_LO >= 0x4000u,
               "SMK loader windows must stay below BTIF / USRTRIM and above the head");
#else
#define LDR_APP_HI 0x93000u
#define LDR_REC_LO 0x93000u                     /* update records live above the app ... */
#define LDR_REC_HI 0xFC000u                     /* ... and below Felucca's globals */
#endif
#if BOARD_SMK37
#define LDR_RES_HI 0xA9000u                     /* hal/fm1_flash_map.h FL_RES_HI: the region, 0x9D000 .. (3y295) */
#else
#define LDR_RES_HI 0x9F000u                     /* hal/fm1_flash_map.h FL_RES_HI: the region, 0x93000 .. */
#endif
_Static_assert(LDR_RES_HI > LDR_APP_HI && LDR_RES_HI <= LDR_REC_HI, "the resource region follows the app area");
#if !BOARD_SMK37
#define LDR_RES2_LO 0xDC000u                    /* hal/fm1_flash_map.h FL_RES2_LO .. FL_RES2_HI (3y296) */
#define LDR_RES2_HI 0xE0000u
#define LDR_RES2_TYPE 200u                      /* cr_res.h CR_RES2_TYPE, tools/fm1pkg_make.py RES2_TYPE */
_Static_assert(LDR_RES2_LO >= LDR_RES_HI && LDR_RES2_HI <= LDR_REC_HI, "the second region: in the record window, above");
#endif
#define LDR_STUB_SEC 0x4000u                    /* the directory and the boot stub */
#define LDR_MARK_SEC 0x91000u                   /* stub_core.h STUB_MARK_SEC, STUB_MARK_OFF */
#define LDR_MARK_OFF 0x91FE0u

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static void ldr_progress(uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
static int ldr_chip_key(uint32_t *key)
{
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

/* CRC-32 (zlib's), bit by bit: the marker's own 16 bytes only */
static uint32_t ldr_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i;
    while (n--) {
        c ^= *p++;
        for (i = 0; i < 8u; i++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

/* does the package carry the stub's commit marker? (the 512 bytes that hold it, decrypted in place) */
static int ldr_pkg_marker(uint32_t fl_off, uint8_t *b, uint32_t key)
{
    const uint8_t *m = b + (LDR_MARK_OFF & 0x1FFu);
    if (ota_read(fl_off + (LDR_MARK_OFF & ~0x1FFu), b, 512))
        return -7;
    ldr_sfc(b, 512, (LDR_MARK_OFF & ~0x1FFu) - LDR_APP_LO, key);
    return ota_rd32(m) == 0x4D425243u && ota_rd32(m + 4) == 1u && ota_rd32(m + 16) == ldr_crc32(m, 16);
}

/* the marker's sector erased, once, before the first write elsewhere: from here to the marker's own write the
 * stub refuses the body. Left alone when it is blank already (a resumed install) */
static int ldr_uncommit(uint8_t *cur, uint32_t *open)
{
    uint32_t i, tries;
    if (*open)
        return 0;
    for (tries = 0; tries < 3u; tries++) {
        if (ldr_fread(LDR_MARK_SEC, cur, 0x1000u))
            return -8;
        for (i = 0; i < 0x1000u && cur[i] == 0xFFu; i++)
            ;
        if (i == 0x1000u) {
            *open = 1;
            return 0;
        }
        if (tries < 2u && ldr_erase(LDR_MARK_SEC))
            return -9;
    }
    return -12;                                          /* it does not erase: nothing else was touched */
}

/* one sector: the package's (from package offset src) into sec first, then the flash's at s; erased, written and read
 * back (twice at most) only when they differ. mark: the package has a marker, so its sector goes blank before
 * anything else is written */
static int ldr_sector(uint32_t src, uint32_t s, uint8_t *sec, uint8_t *cur, uint32_t mark, uint32_t *open)
{
    uint32_t k, tries;
    int rc;
    for (k = 0; k < 0x1000u; k += 512u)
        if (ota_read(src + k, sec + k, 512))
            return -7;
    if (ldr_fread(s, cur, 0x1000u))
        return -8;
    if (ota_memeq(sec, cur, 0x1000u))
        return 0;
    if (mark && s != LDR_MARK_SEC && (rc = ldr_uncommit(cur, open)) != 0)
        return rc;
    for (tries = 0; tries < 2u; tries++) {
        if (ldr_erase(s))
            return -9;
        for (k = 0; k < 0x1000u; k += 256u)
            if (ldr_prog(s + k, sec + k, 256))
                return -10;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (ota_memeq(sec, cur, 0x1000u))
            return 0;
    }
    return -11;                                          /* verify failed twice */
}

/* the package's second region (res2.bin): its offset in the package, 0 = none. ota_ufw left the entries decrypted
 * in hdr; an entry of the type with another length is refused (the install stops before anything is written) */
static int ldr_res2(const uint8_t *hdr, uint32_t *off)
{
    uint32_t i, nent = ota_rd16(hdr + 8);
    *off = 0;
#ifdef LDR_RES2_TYPE
    for (i = 0; i < nent; i++) {
        const uint8_t *e = hdr + 0x40 + i * 0x50u;
        if (ota_rd16(e) != LDR_RES2_TYPE)
            continue;
        if (ota_rd32(e + 12) != LDR_RES2_HI - LDR_RES2_LO || !ota_rd32(e + 8))
            return -13;
        *off = ota_rd32(e + 8);
    }
#else
    (void)hdr; (void)i; (void)nent;
#endif
    return 0;
}

static int ldr_session(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000];
    uint32_t i, fl_off, fl_size, ota_off, ota_len, key, s, done = 0, open = 0, hi, r2_off, total;
    int rc, mark;
    /* 1. UFW header + entry list (ota.c) */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_size, &ota_off, &ota_len)) != 0)
        return rc;
    if ((rc = ldr_res2(hdr, &r2_off)) != 0)
        return rc;
    if (!fl_off || fl_size < LDR_APP_HI)
        return -3;
    /* 2. the package's app area must decrypt with this chip's key */
    if ((rc = ldr_chip_key(&key)) != 0)
        return -40 + rc;
    if (ota_read(fl_off + LDR_APP_LO, sec, 32))
        return -5;
    ldr_sfc(sec, 32, 0, key);
    if (ota_crc16(sec + 2, 30, 0) != ota_rd16(sec))
        return -6;                                       /* package for another chip key */
    /* 3. app area, sector by sector: the stub's sector first (it is the lowest), the resource region's after the body
     *    (when the package carries it), the marker's last */
    if ((mark = ldr_pkg_marker(fl_off, sec, key)) < 0)
        return mark;
    hi = fl_size >= LDR_RES_HI ? LDR_RES_HI : LDR_APP_HI;
    total = hi - LDR_APP_LO;
#ifdef LDR_RES2_TYPE
    if (r2_off)
        total += LDR_RES2_HI - LDR_RES2_LO;
#endif
    for (s = LDR_APP_LO; s < hi; s += 0x1000u) {
        if (mark && s == LDR_MARK_SEC)
            continue;
        if ((rc = ldr_sector(fl_off + s, s, sec, cur, (uint32_t)mark, &open)) != 0)
            return rc;
        done += 0x1000u;
        ldr_progress(done, total);
    }
#ifdef LDR_RES2_TYPE
    for (s = LDR_RES2_LO; r2_off && s < LDR_RES2_HI; s += 0x1000u) {   /* the second region: before the marker too */
        if ((rc = ldr_sector(r2_off + (s - LDR_RES2_LO), s, sec, cur, (uint32_t)mark, &open)) != 0)
            return rc;
        done += 0x1000u;
        ldr_progress(done, total);
    }
#endif
    if (mark) {                                          /* the commit: every other sector is written and verified */
        if ((rc = ldr_sector(fl_off + LDR_MARK_SEC, LDR_MARK_SEC, sec, cur, 1u, &open)) != 0)
            return rc;
        ldr_progress(total, total);
    }
    /* 4. finish: the host confirms, the record goes, the SPL boots the app */
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, sec, 8))
            break;
    ldr_record_clear();
    for (s = LDR_REC_HI; s > (hi > LDR_REC_LO ? hi : LDR_REC_LO); s -= 0x1000u) {   /* flash records: 4K boundary - 256;
                                                                                     * never in the region just written */
        uint8_t r[80];
#ifdef LDR_RES2_TYPE
        if (s > LDR_RES2_LO && s <= LDR_RES2_HI)         /* (never in the second region: its sectors are data) */
            continue;
#endif
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0))
            ldr_erase(s - 0x1000u);
    }
    return 0;
}
