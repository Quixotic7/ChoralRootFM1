/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the Felucca update loader (firmware/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc [SAME_STUB.fwsc OTHER_OLD.fwsc [OLD_STUB.fwsc]]
 *
 * With a NEW that carries ChoralRoot's boot stub and commit marker, the interruption model (docs/BOOT-SAFETY.md
 * "As built"): the install is cut after EVERY flash operation (each erase, each programmed page), the flash as it
 * then stands is given to the stub's own decision (firmware/stub/stub_core.h, the code the device runs) as the SPL
 * would after a power cycle, and the install is resumed from that state to a bit-exact end. Cases:
 *   OLD without a stub (the 0.16 / Felucca layout) -> NEW      the first install of a stub: its sector's own
 *                                                             erase and write is the one unprotected window
 *   SAME_STUB (NEW's stub, another body) -> NEW                every later install: no unprotected cut at all
 *   the dark unit of 2026-10-07 (OLD's sectors 0x4000..0x24FFF, then OTHER_OLD's: no stub, half and half) -> NEW
 *   OLD_STUB (an earlier stub, another body) -> NEW          a build that changes the stub (v2 -> v3): the old
 *                                                             stub guards up to the sector's erase, the new one
 *                                                             from its last page on; the window between is the same
 * A cut state is: B the stub boots it, R the stub rescues (the staged loader), O the old firmware still whole (no
 * stub yet, nothing it uses touched), U unprotected (no working stub in sector 0x4000 and not the whole old app).
 * The resource region (HISTORY 3y234, firmware/src/cr_res.h; the SMK-37's since 3y295): a NEW whose flash.bin runs on
 * to T_RES_HI (0x9F000; SMK 0xA9000) carries
 * it; every base starts with stale bytes there (0.21's objects, another firmware's data), so every cut leaves some of
 * it written, and B (the stub boots) is allowed only with the whole region in place. A NEW without it (an older
 * package: the downgrade) must leave the region as it was, byte for byte. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0
/* the board's places (-DBOARD_SMK37=1: the SMK-37's, on SMK-layout packages: PRCT 0x9D000, the staging at 0xBB000) */
#if BOARD_SMK37
#define T_APP_HI 0x9D000u                /* the end of the app area the loader writes (ldr_core.c LDR_APP_HI) */
#define T_RES_HI 0xA9000u                /* the resource region's end (LDR_RES_HI): 0x9D000 .. (3y295) */
#define T_REC_HI 0xC0000u                /* .. and of its record window (LDR_REC_HI) */
#define T_OTA 0xBB000u                   /* the staging area (ota.c OTA_AREA) */
#else
#define T_APP_HI 0x93000u
#define T_RES_HI 0x9F000u                /* the resource region's end (ldr_core.c LDR_RES_HI): 0x93000 .. */
#define T_REC_HI 0xFC000u
#define T_OTA 0xE0000u
#endif

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, bad_range, record_cleared, f0_asked;
static jmp_buf cut_jmp;                             /* the power fails after flash operation number cut_at */
static uint32_t ops, cut_at, oplog[8192];           /* each operation: its flash offset | 1 for an erase */

static void op_done(uint32_t what)
{
    if (ops < sizeof oplog / sizeof oplog[0]) oplog[ops] = what;
    ops++;
    if (cut_at && ops == cut_at) longjmp(cut_jmp, 1);
}

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n)      /* the staging area only (the fixture below) */
{
    if (off < T_OTA || off + n > T_OTA + 0x5000u) return -1;
    memcpy(nor + off, p, n);
    return 0;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/ota.c"

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= T_REC_HI || (off & 0xFFFu)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    erases++;
    memset(nor + off, 0xFF, 0x1000);
    op_done(off | 1u);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > T_RES_HI || ((off & 0xFFu) + n) > 256u) bad_range++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    op_done(off);
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static uint32_t flash_len(const uint8_t *lg)        /* flash.bin's length in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 12);
    }
    return 0;
}
static uint32_t t_end;                              /* what NEW writes: T_RES_HI with the region, else T_APP_HI */
static void stale_res(void)                         /* the region's sectors as a unit from 0.21 or another firmware */
{                                                   /* leaves them (not erased: the loader must write them over) */
    uint32_t i;
    for (i = T_APP_HI; i < T_RES_HI; i++) nor[i] = (uint8_t)(0xA5u ^ (i >> 4));
}

static int check(const char *what, int ok) { printf("%-56s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

/* ---- the interruption model ---- */
#include "../firmware/stub/stub_core.h"

static uint32_t chip_key;
static const uint8_t *new_fl;                       /* NEW's flash.bin */

static void xip(uint32_t off, void *dst, uint32_t n)        /* the app area as XIP shows it: decrypted */
{
    memcpy(dst, nor + off, n);
    ldr_sfc(dst, n, off - LDR_APP_LO, chip_key);
}

static int has_marker(const uint8_t *fl)            /* a flash.bin with the stub's marker */
{
    uint8_t m[32];
    memcpy(m, fl + STUB_MARK_OFF, 32);
    ldr_sfc(m, 32, STUB_MARK_OFF - LDR_APP_LO, chip_key);
    return ota_rd32(m) == STUB_MARK_MAGIC;
}

static uint32_t stub_says(stub_result_t *r)         /* the device's stub_decide on the flash as it stands */
{
    static uint32_t body[STUB_BODY_MAX / 4u];
    static stub_crc_t k;
    uint8_t m[32];
    stub_view_t v;
    xip(STUB_MARK_OFF, m, 32);
    if (stub_rd32(m) == STUB_MARK_MAGIC)            /* (the body is read only behind a marker: decrypt it then) */
        xip(STUB_BODY_OFF, body, STUB_BODY_MAX);
    v.mark = m;
    v.body = body;
    v.ldr = nor + STUB_LDR_OFF;                     /* stored plain */
    stub_decide(&v, &k, 0, 0, r);
    return r->decision;
}

static void restore(const uint8_t *base)
{
    memcpy(nor + LDR_APP_LO, base + LDR_APP_LO, LDR_APP_HI - LDR_APP_LO);
    stale_res();
    rx_full = 0;
}

/* base: the app area before the install; base_stub: it carries a stub, NEW's or an earlier one (then it must boot,
 * and its sector is a working stub as long as it is whole: the decision's code is the same); base_whole: without
 * a stub, it is a whole firmware (the SPL boots it); want_u: the unprotected cut points this case may have */
static int cut_case(const char *name, const uint8_t *base, int base_stub, int base_whole, uint32_t want_u)
{
    static uint8_t head[0x4000];
    uint32_t n, k, cnt[4] = {0, 0, 0, 0}, first_u = 0, last_u = 0, stub_done = 0, req, bad = 0, marker_free = 1, i;
    stub_result_t r;
    int rc;
    memcpy(head, nor, sizeof head);
    {   /* does the old firmware use the marker's sector? (0.16 does not: its app ends far below) */
        uint8_t sec[0x1000];
        memcpy(sec, base + STUB_MARK_SEC, sizeof sec);
        ldr_sfc(sec, sizeof sec, STUB_MARK_SEC - LDR_APP_LO, chip_key);
        for (i = 0; i < sizeof sec; i++) marker_free &= sec[i] == 0xFF;
    }
    restore(base);
    if (base_stub) bad += stub_says(&r) != STUB_BOOT;                 /* cut 0: the old build boots */
    ops = 0; cut_at = 0; requests = 0;
    rc = ldr_session();
    n = ops; req = requests;
    bad += rc != 0 || memcmp(nor + LDR_APP_LO, new_fl + LDR_APP_LO, t_end - LDR_APP_LO) != 0;
    bad += stub_says(&r) != STUB_BOOT || n > sizeof oplog / sizeof oplog[0];
    for (k = 0; k < n; k++)                                           /* the stub's sector whole from this op on */
        if (oplog[k] == LDR_STUB_SEC + 0xF00u) stub_done = k + 1u;
    if (n && oplog[0] != (LDR_MARK_SEC | 1u)) bad++;                  /* the first operation: the marker's erase */
    if (n && oplog[n - 1u] != LDR_MARK_SEC + 0xF00u) bad++;           /* the last: the marker's page */
    for (k = 1; k <= n; k++) {
        int st, stub_here;
        restore(base);
        ops = 0; cut_at = k;
        if (!setjmp(cut_jmp)) { ldr_session(); bad++; }               /* (it must be cut, not finish) */
        cut_at = 0;
        stub_here = !memcmp(nor + LDR_STUB_SEC, new_fl + LDR_STUB_SEC, 0x1000) ||
                    (base_stub && !memcmp(nor + LDR_STUB_SEC, base + LDR_STUB_SEC, 0x1000));
        if (stub_here) {
            uint32_t d = stub_says(&r);
            st = d == STUB_BOOT ? 3 : 2;
            bad += d == STUB_UBOOT;                                   /* (the staged loader is there: never) */
            if (d == STUB_BOOT)                                       /* boot: only the finished image */
                bad += k != n || memcmp(nor + LDR_APP_LO, new_fl + LDR_APP_LO, t_end - LDR_APP_LO) != 0;
        } else {
            int whole = base_whole && marker_free;                    /* the old app, but for a sector it never used */
            for (i = LDR_APP_LO; whole && i < LDR_APP_HI; i += 0x1000u)
                whole = i == LDR_MARK_SEC || !memcmp(nor + i, base + i, 0x1000);
            st = whole ? 0 : 1;
            if (st == 1) { if (!first_u) first_u = k; last_u = k; }
            bad += stub_done && k >= stub_done;                       /* no such state once the stub is written */
        }
        cnt[st]++;
        ops = 0;
        rc = ldr_session();                                           /* resume from the cut state */
        bad += rc != 0 || memcmp(nor + LDR_APP_LO, new_fl + LDR_APP_LO, t_end - LDR_APP_LO) != 0 ||
               memcmp(nor, head, sizeof head) != 0;
    }
    bad += cnt[1] != want_u || cnt[3] != (n ? 1u : 0u) || bad_range;
    if (cnt[1]) {                                                     /* the window: nothing but the stub's sector */
        for (k = first_u; k <= last_u; k++)                           /* (and the marker's erase before it) */
            bad += (oplog[k - 1u] & ~0xFFFu) != LDR_STUB_SEC && oplog[k - 1u] != (LDR_MARK_SEC | 1u);
    }
    printf("  %-34s %5u cut points: %4u rescue, %u boot (the end), %u old firmware whole, %2u UNPROTECTED",
           name, n, cnt[2], cnt[3], cnt[0], cnt[1]);
    if (cnt[1]) printf(" (cuts %u..%u)", first_u, last_u);
    printf("; %u requests  %s\n", req, bad ? "FAIL" : "ok");
    return bad != 0;
}

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo;
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc [SAME_STUB.fwsc OTHER_OLD.fwsc [OLD_STUB.fwsc]]\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    t_end = flash_len(logical) >= T_RES_HI ? T_RES_HI : T_APP_HI;
    printf("  NEW's flash.bin: %#x B (%s)\n", (unsigned)flash_len(logical),
           t_end > T_APP_HI ? "with the resource region" : "no resource region");
    /* device: OLD installed (its region too when it has one, else stale bytes there), a valid update record at 0xE4F00 */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, T_APP_HI);
    if (flash_len(old) >= T_RES_HI && T_RES_HI > T_APP_HI)
        memcpy(nor + T_APP_HI, old + ofo + T_APP_HI, T_RES_HI - T_APP_HI);
    else
        stale_res();
    memcpy(head, nor, sizeof head);
    {
        static uint8_t res_before[T_RES_HI - T_APP_HI + 1u];
        memcpy(res_before, nor + T_APP_HI, T_RES_HI - T_APP_HI);
    {
        uint8_t r[112] = {0};
        r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
        ota_wr16(r, ota_crc16(r + 2, 78, 0));
        memcpy(nor + T_OTA + 0x4F00u, r, sizeof r);
    }
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes", rc == 0);
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, logical + nfo + 0x4000, T_APP_HI - 0x4000));
    if (t_end > T_APP_HI)
        bad += check("resource region == the package's (T_APP_HI..T_RES_HI)",
                     !memcmp(nor + T_APP_HI, logical + nfo + T_APP_HI, T_RES_HI - T_APP_HI));
    else if (T_RES_HI > T_APP_HI)
        bad += check("no resource region in it: T_APP_HI..T_RES_HI as before (a downgrade)",
                     !memcmp(nor + T_APP_HI, res_before, T_RES_HI - T_APP_HI));
    }
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update record cleared (RAM + flash)", record_cleared && nor[T_OTA + 0x4F06u] == 0xFF);
    bad += check("no write outside the allowed windows", bad_range == 0);
    /* same package again: nothing to erase */
    erases = 0; requests = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
    /* a package for another chip key is refused before any erase */
    {
        uint8_t save = logical[nfo + 0x4000 + 5];
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
        memcpy(nor, old + ofo, T_APP_HI);
        stale_res();
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[nfo + 0x4000 + 5] = save;
    }
    /* the interruption model (see the top) */
    memcpy(nor, old + ofo, T_APP_HI);
    new_fl = logical + nfo;
    if (ldr_chip_key(&chip_key) || !has_marker(new_fl)) {
        printf("  (the new package has no boot stub marker: written in address order, no interruption model)\n");
    } else {
        uint32_t ota_off = 0, ota_len = 0, fo, fl, u_first;
        static uint8_t hdr[0x400], dark[T_APP_HI];
        size_t slen, xlen;
        memset(nor + T_APP_HI, 0xFF, sizeof nor - T_APP_HI);
        rx_full = 0;
        ota_ufw(hdr, &fo, &fl, &ota_off, &ota_len);                   /* the staged loader, as ota.c's step 1 leaves it */
        memcpy(nor + T_OTA + 0x20u, logical + ota_off + 0x20, ota_len - 0x20);
        ota_stage_head(ota_len - 0x20);
        bad += check("stub: the staged loader passes its check", stub_loader_check(nor + STUB_LDR_OFF) == STUB_LD_OK);
        {   /* the staged loader's life over an install: step 1's record beside it, the loader's write, its finish
             * (the record's sector erased), the app's first boot (ota.c ota_boot_cleanup): the loader still there */
            uint8_t r[112] = {0};
            stub_result_t sr;
            r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
            ota_wr16(r, ota_crc16(r + 2, 78, 0));
            memcpy(nor + T_OTA + 0x4F00u, r, sizeof r);
            cut_at = 0;
            rc = ldr_session();
            bad += check("install -> boot: the record gone, the staged loader kept and whole",
                         rc == 0 && nor[T_OTA + 0x4F06u] == 0xFF && ota_boot_cleanup() == 0 && ota_staged() &&
                         stub_loader_check(nor + STUB_LDR_OFF) == STUB_LD_OK);
            bad += check(".. the stub boots the app", stub_says(&sr) == STUB_BOOT);
            {
                static uint32_t body[4];
                static stub_crc_t k;
                uint8_t m[32];
                stub_view_t v;
                xip(STUB_MARK_OFF, m, 32);
                v.mark = m; v.body = body; v.ldr = nor + STUB_LDR_OFF;
                stub_decide(&v, &k, 1, 0, &sr);
                bad += check(".. with OCT- held: that loader, one attempt counted",
                             sr.decision == STUB_LOADER && sr.word == (STUB_RSC_MAGIC | 1u));
                stub_decide(&v, &k, 1, sr.word, &sr);
                bad += check(".. a second arrival with the key and no boot between: UBOOT", sr.decision == STUB_UBOOT);
            }
            memcpy(nor, old + ofo, T_APP_HI);
        }
        printf("interruption model: a power cut after every erase and every programmed page\n");
        /* 16 = the stub sector's erase and its first 15 pages; the marker's erase before them leaves a 0.16 app
         * whole (it does not use that sector); a firmware that does use it has one more */
        {
            uint8_t sec[0x1000];
            uint32_t i, free91 = 1;
            memcpy(sec, old + ofo + STUB_MARK_SEC, sizeof sec);
            ldr_sfc(sec, sizeof sec, STUB_MARK_SEC - LDR_APP_LO, chip_key);
            for (i = 0; i < sizeof sec; i++) free91 &= sec[i] == 0xFF;
            u_first = free91 ? 16u : 17u;
        }
        if (has_marker(old + ofo) && !memcmp(old + ofo + 0x4000, new_fl + 0x4000, 0x1000))
            bad += cut_case("OLD: same stub -> stub", old + ofo, 1, 0, 0);
        else
            bad += cut_case("no stub (0.16 layout) -> stub", old + ofo, 0, 1, u_first);
        if (argc >= 5) {
            uint8_t *same = load_logical(argv[3], &slen), *other = load_logical(argv[4], &xlen);
            uint32_t sfo = flash_off(same), xfo = flash_off(other);
            bad += check("SAME_STUB carries the new package's stub sector, byte for byte",
                         has_marker(same + sfo) && !memcmp(same + sfo + 0x4000, new_fl + 0x4000, 0x1000));
            bad += check(".. and another body", memcmp(same + sfo + 0x5000, new_fl + 0x5000, 0x8C000) != 0);
            bad += cut_case("same stub, another body -> stub", same + sfo, 1, 0, 0);
            /* the dark unit: 33 sectors of one firmware without a stub, then another's; nothing torn. No stub is
             * in it, so no code of ours runs: it stays dark until the hardware route revives it. Under the new
             * loader (once one runs on it) it is unprotected until the stub's sector is whole, then a rescue */
            memcpy(dark, other + xfo, sizeof dark);
            memcpy(dark + 0x4000, old + ofo + 0x4000, 0x21000);
            bad += check("the dark unit's state holds no stub", memcmp(dark + 0x4000, new_fl + 0x4000, 0x1000) != 0);
            bad += cut_case("the dark unit (33 new, then old)", dark, 0, 0, 17);
            /* the same accident with the new loader and a stub in place: cut where that install died */
            restore(same + sfo);
            ops = 0; cut_at = 1u + 33u * 17u;
            if (!setjmp(cut_jmp)) { ldr_session(); bad++; }
            cut_at = 0;
            {
                stub_result_t r;
                bad += check("the same cut (33 sectors in) with a stub in place: rescue, the loader",
                             stub_says(&r) == STUB_LOADER && r.mk != STUB_MK_OK);
            }
            if (argc >= 6) {                                          /* a stub change: the unit's version, the next in the package */
                size_t vlen;
                uint8_t *v1 = load_logical(argv[5], &vlen);
                uint32_t vfo = flash_off(v1);
                bad += check("OLD_STUB: a marker, another stub sector, another body",
                             has_marker(v1 + vfo) && memcmp(v1 + vfo + 0x4000, new_fl + 0x4000, 0x1000) != 0 &&
                             memcmp(v1 + vfo + 0x5000, new_fl + 0x5000, 0x8C000) != 0);
                bad += check(".. its directory (0x4000..0x411F) is the new package's: only the stub's bytes differ",
                             !memcmp(v1 + vfo + 0x4000, new_fl + 0x4000, 0x120));
                bad += cut_case("an earlier stub -> this stub", v1 + vfo, 1, 0, 16);
            }
        }
    }
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
