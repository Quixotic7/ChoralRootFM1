/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static int fail_bytes = -1;            /* power loss after any programmed byte */
static int erase_error, write_protected;
static uint32_t io_calls;

static int st_read(uint32_t off, void *dst, uint32_t n) { io_calls++; memcpy(dst, nor + off, n); return 0; }
static uint32_t erases, erase_at[64], erase_off[64], progs, sim_ms;   /* (the quiet writes: what was erased, when) */
static int erase_torn = -1;            /* power loss inside an erase: only this many bytes of the sector reach 0xFF */
static int st_erase(uint32_t off)
{
    io_calls++;
    if (erase_error) return -8;
    if (erases < 64u) { erase_at[erases] = sim_ms; erase_off[erases] = off; }
    erases++;
    if (erase_torn >= 0) { memset(nor + off, 0xFF, (size_t)erase_torn); return -9; }
    if (!write_protected) memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    io_calls++;
    progs++;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++) {
        if (fail_bytes == 0) return -9;
        if (fail_bytes > 0) fail_bytes--;
        if (!write_protected) nor[off + i] &= s[i];
    }
    return 0;
}
#define FELUCCA_CZ 1                   /* (ChoralRoot's map: the session and the favourites exist; storage.c without
                                        * core.h here) */
#define FELUCCA_QUAD 1
#define ST_AHEAD 1                     /* ChoralRoot's erase ahead (choralroot.c sets it) */
#include "../firmware/src/storage.c"

/* 74eae04's writer, word for word (0.22's first builds and everything before them: one erase a save): what an OLDER
 * firmware does to the flash this one leaves, and what it leaves for this one */
static int st_save_old(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];
    if ((rc = st_erase(base)) != 0)
        return rc;
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
    if ((rc = st_prog(base, &h, sizeof h)) != 0)
        return rc;
    {
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}

static int sector_blank(uint32_t off)
{
    uint32_t i;
    for (i = 0; i < 4096u; i++)
        if (nor[off + i] != 0xFFu)
            return 0;
    return 1;
}
static int loads(uint32_t obj, const char *want, uint32_t n)
{
    static char g[1024];
    return st_load(obj, g, sizeof g) == (int)n && !memcmp(g, want, n);
}

/* ---- the quiet writes' schedule (storage.c st_write_due / st_ahead_due), walked as cr_settings.c's poll walks it:
 * one object, a step every 10 ms. The script: marks (changes), inputs, and stretches in which something sounds ---- */
typedef struct { uint32_t at; char what; uint32_t len; } ev_t;   /* 'm' a mark (an input too), 'i' an input, 'n' a
                                                                  * note sounding for len ms (its key: an input) */
static uint32_t sim_writes, sim_write_at[16], sim_aheads;
static void sim(uint32_t obj, const ev_t *ev, uint32_t nev, uint32_t until, int boot_prep)
{
    uint32_t change = 0, loud = 0, input = 0, wrote = 0, loud_until = 0, k = 0, gen = 0;
    int dirty = 0;
    char payload[262];
    memset(nor, 0xFF, sizeof nor);                   /* the flash as an older firmware leaves it: both copies valid */
    memset(payload, 'x', sizeof payload);
    st_save(obj, payload, sizeof payload);
    st_save(obj, payload, 200);
    memset(st_ahead_ok, 0, sizeof st_ahead_ok);      /* a power-on */
    erases = progs = sim_writes = sim_aheads = 0;
    sim_ms = 0;
    if (boot_prep)
        st_prep(obj);
    for (sim_ms = 0; sim_ms <= until; sim_ms += 10u) {
        st_when_t w;
        for (; k < nev && ev[k].at <= sim_ms; k++) {
            input = sim_ms;
            if (ev[k].what == 'm') { dirty = 1; change = sim_ms; gen++; }
            if (ev[k].what == 'n') loud_until = sim_ms + ev[k].len;
        }
        if (sim_ms < loud_until)
            loud = sim_ms;
        w.now = sim_ms; w.change = change; w.loud = loud; w.input = input; w.wrote = wrote;
        w.busy = 0; w.ready = (uint8_t)(st_ahead_ix(obj) < 0 || st_ready(obj));   /* (as cr_settings.c crs_when) */
        if (dirty && st_write_due(&w)) {
            memset(payload, (int)('a' + gen), sizeof payload);
            if (st_save(obj, payload, sizeof payload) == 0) {
                dirty = 0;
                if (sim_writes < 16u) sim_write_at[sim_writes] = sim_ms;
                sim_writes++;
            }
            wrote = sim_ms;
        } else if (!dirty && st_ahead_due(&w)) {
            if (st_prep(obj) > 0)
                sim_aheads++;
            wrote = sim_ms;
        }
    }
}
static int erase_within(uint32_t t, uint32_t ms)     /* an erase started within ms of t */
{
    uint32_t i;
    for (i = 0; i < erases && i < 64u; i++)
        if (erase_at[i] + ms >= t && erase_at[i] <= t + ms)
            return 1;
    return 0;
}

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_CZBANK0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_CZBANK0, a, sizeof a) == 0);
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_CZBANK0, b, sizeof b) == 0);
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_CZBANK0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_CZBANK0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_CZBANK0, a, sizeof a) == 0);
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_CZBANK0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_CZBANK0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_CZBANK0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_CZBANK0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_CZBANK0 + 2, a, sizeof a);
    st_save(OBJ_CZBANK0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_CZBANK0 + 2, &h);
        nor[st_sector(OBJ_CZBANK0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_CZBANK0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_CZBANK0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_CZBANK0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_CZBANK0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_CZBANK0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_CZBANK0 + 2, got, sizeof got) < 0);
    bad += check("the CZ-1 banks at 0xA2000..0xB1FFF, the settings below 0xFF000",
                 st_sector(OBJ_SETTINGS, 1) + 4096 <= 0xFF000 && st_sector(OBJ_CZBANK0, 0) == 0xA2000u &&
                     st_sector(OBJ_CZBANK0 + 7, 1) + 4096 == 0xB2000u);
    {   /* 0.21's objects (Felucca's projects, the user preset banks, the CZ-1 and QUAD stores): numbers kept, not stored
         * (storage.c st_live, 3y231), the sound pool in every build since 3y232 */
        static const uint32_t X[] = {OBJ_X_PROJECT0, OBJ_X_PROJECT0 + 3, OBJ_X_UPRESET0, OBJ_X_UPRESET0 + 1,
                                     OBJ_X_CZSTORE1, OBJ_X_QUADSTORE};
        uint32_t i, calls = io_calls;
        int ok = 1;
        for (i = 0; i < sizeof X / sizeof X[0]; i++)
            ok &= !st_live(X[i]) && st_sector(X[i], 0) == ST_NOWHERE && st_sector(X[i], 1) == ST_NOWHERE &&
                  st_load(X[i], got, sizeof got) < 0 && st_save(X[i], a, sizeof a) < 0;
        bad += check("0.21's objects: no sector, load and save refused, no flash access", ok && io_calls == calls);
    }
    /* the FM6 patch bank: the two free sectors, 0x9F000 and 0xFE000 (after the settings) */
    bad += check("FM6 bank in the free sectors 0x9F000 / 0xFE000",
                 OBJ_FM6BANK == OBJ_X_UPRESET0 + 2 && st_sector(OBJ_FM6BANK, 0) == 0x9F000u &&
                     st_sector(OBJ_FM6BANK, 1) == 0xFE000u && st_sector(OBJ_SETTINGS, 1) + 4096 == 0xFE000u);
    {
        static uint8_t bank[3472], back[3472];
        uint32_t i;
        for (i = 0; i < sizeof bank; i++) bank[i] = (uint8_t)(i * 7u);
        bad += check("FM6 bank save / load (A then B)", st_save(OBJ_FM6BANK, bank, sizeof bank) == 0 &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof back && !memcmp(bank, back, sizeof bank) &&
                     (bank[0] ^= 1, st_save(OBJ_FM6BANK, bank, sizeof bank) == 0) &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof back && back[0] == bank[0]);
        {
            int erased = 1;
            for (i = 256u + sizeof bank; i < 4096u; i++) erased &= nor[0x9F000u + i] == 0xFFu && nor[0xFE000u + i] == 0xFFu;
            bad += check("FM6 bank: the sector tails stay erased", erased);
        }
    }
    {
        uint8_t copies[2 * ST_SECTOR];
        uint32_t off = st_sector(OBJ_CZBANK0, 0), cut;
        int ok = 1;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_CZBANK0, a, sizeof a);
        st_save(OBJ_CZBANK0, b, sizeof b);
        memcpy(copies, nor + off, sizeof copies);
        for (cut = 0; cut < sizeof a + sizeof(st_hdr_t); cut++) {
            memcpy(nor + off, copies, sizeof copies);
            fail_bytes = (int)cut;
            ok &= st_save(OBJ_CZBANK0, a, sizeof a) != 0;
            fail_bytes = -1;
            ok &= st_load(OBJ_CZBANK0, got, sizeof got) == (int)sizeof b && !memcmp(got, b, sizeof b);
        }
        bad += check("every payload/header byte cut keeps old data", ok);
        erase_error = 1;
        ok = st_save(OBJ_CZBANK0, a, sizeof a) != 0;
        erase_error = 0;
        bad += check("erase failure keeps old data", ok && st_load(OBJ_CZBANK0, got, sizeof got) == (int)sizeof b &&
                                                      !memcmp(got, b, sizeof b));
        write_protected = 1;
        ok = st_save(OBJ_CZBANK0, a, sizeof a) != 0;
        write_protected = 0;
        bad += check("write protection is a save error", ok && st_load(OBJ_CZBANK0, got, sizeof got) == (int)sizeof b &&
                                                            !memcmp(got, b, sizeof b));
    }
    {
        st_hdr_t h;
        uint32_t copy;
        for (copy = 0; copy < 2u; copy++) {
            uint32_t off = st_sector(OBJ_CZBANK0, copy);
            memcpy(&h, nor + off, sizeof h);
            h.seq = 0xFFFFFFFEu + copy;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + off, &h, sizeof h);
        }
        bad += check("save sequence wraps", st_save(OBJ_CZBANK0, a, sizeof a) == 0);
        n = st_load(OBJ_CZBANK0, got, sizeof got);
        bad += check("wrapped sequence loads new data", n == (int)sizeof a && !memcmp(got, a, sizeof a));
        bad += check("save after sequence wrap", st_save(OBJ_CZBANK0, b, sizeof b) == 0 &&
                                                st_load(OBJ_CZBANK0, got, sizeof got) == (int)sizeof b &&
                                                !memcmp(got, b, sizeof b));
    }
    {
        uint32_t calls = io_calls;
        bad += check("invalid object load does no flash access", st_load(OBJ_COUNT, got, sizeof got) < 0 && io_calls == calls);
        bad += check("invalid object save does no flash access", st_save(OBJ_COUNT, a, sizeof a) < 0 && io_calls == calls);
        bad += check("wrapped object rejected", st_save(0xFFFFFFFFu, a, sizeof a) < 0 && io_calls == calls);
        memset(got, 'X', sizeof got);
        bad += check("small destination rejects whole object", st_load(OBJ_CZBANK0, got, sizeof got - 1u) < 0 && got[0] == 'X');
        bad += check("oversized save rejected", st_save(OBJ_CZBANK0, a, ST_PAYLOAD_MAX + 1u) < 0);
    }
    /* ================= erase ahead (the quiet writes): the writer's states ================= */
    /* 3y52 (the owner's decision of 2026-10-14): the favourites and the session only; the settings below */
    for (n = 0; n < 2; n++) {
        const uint32_t AO = n ? OBJ_SESSION : OBJ_FAVS;
        uint32_t A = st_sector(AO, 0), B = st_sector(AO, 1), e0, cut, calls;
        uint8_t keep[ST_SECTOR];
        int ok;
        memset(nor, 0xFF, sizeof nor);
        memset(st_ahead_ok, 0, sizeof st_ahead_ok);
        erases = 0;
        bad += check("ahead: a new unit's first save programs only", st_save(AO, a, 262) == 0 && erases == 0 &&
                     loads(AO, a, 262) && sector_blank(B));
        bad += check("ahead: valid A + erased B reads A (a new unit's spare: ready without an erase)",
                     loads(AO, a, 262) && st_ready(AO));
        memset(st_ahead_ok, 0, sizeof st_ahead_ok); /* (a power-on) */
        bad += check("ahead: st_prep on a blank spare: no erase, ready", st_prep(AO) == 0 && erases == 0 && st_ready(AO));
        progs = 0;
        bad += check("ahead: the next save programs only (2 payload pages + the commit record)",
                     st_save(AO, b, 262) == 0 && erases == 0 && progs == 3 && loads(AO, b, 262) && !st_ready(AO));
        memcpy(keep, nor + B, sizeof keep);
        bad += check("ahead: valid A + valid B (newer): st_prep erases A, the stale one, and only A",
                     st_prep(AO) == 1 && erases == 1 && erase_off[0] == A && sector_blank(A) &&
                     !memcmp(keep, nor + B, sizeof keep) && loads(AO, b, 262) && st_ready(AO));
        bad += check("ahead: erased A + valid B reads B (an erased copy is not 'no object')", loads(AO, b, 262));
        bad += check("ahead: st_prep again: nothing to do", st_prep(AO) == 0 && erases == 1);
        /* a power cut at every byte of a program-only save: the valid copy stays in charge, the spare is found not
         * blank afterwards and the next save erases it first */
        ok = 1;
        for (cut = 0; cut < 262u + sizeof(st_hdr_t); cut++) {
            memset(nor + A, 0xFF, 4096);
            memcpy(nor + B, keep, sizeof keep);
            e0 = erases;
            fail_bytes = (int)cut;
            ok &= st_save(AO, a, 262) != 0 && erases == e0;
            fail_bytes = -1;
            ok &= loads(AO, b, 262) && !st_ready(AO);
            if (cut) {
                ok &= st_save(AO, a, 262) == 0 && erases == e0 + 1u && loads(AO, a, 262);
            } else {                                /* (cut before the first byte: the spare is still blank) */
                ok &= st_save(AO, a, 262) == 0 && erases == e0 && loads(AO, a, 262);
            }
        }
        bad += check("ahead: a program-only save cut at every byte keeps the old data; the next save erases", ok);
        /* an erase ahead cut by the power, at every 32nd byte of the stale copy's 856 (the model: the bytes before
         * the cut read 0xFF, the rest is as it was): the valid copy is untouched and the spare is not taken for
         * blank (st_prep does it again; a save without st_prep erases for itself). What no reader can tell: an erase
         * cut so late that every byte already reads 0xFF (the settings' document says what that may cost) */
        ok = 1;
        for (cut = 0; cut < 256u + 600u; cut += 32u) {
            memset(nor, 0xFF, sizeof nor);
            memset(st_ahead_ok, 0, sizeof st_ahead_ok);
            st_save(AO, a, 600);
            st_save(AO, b, 262);              /* B current, A stale and valid */
            memcpy(keep, nor + B, sizeof keep);
            erase_torn = (int)cut;
            ok &= st_prep(AO) < 0 && !st_ready(AO);
            erase_torn = -1;
            ok &= !memcmp(keep, nor + B, sizeof keep) && loads(AO, b, 262);
            e0 = erases;
            if (cut & 32u) {
                ok &= st_prep(AO) == 1 && erases == e0 + 1u && sector_blank(A) && st_ready(AO);
                ok &= st_save(AO, a, 262) == 0 && erases == e0 + 1u;
            } else {
                ok &= st_save(AO, a, 262) == 0 && erases == e0 + 1u;
            }
            ok &= loads(AO, a, 262);
            if (!ok) { printf("  (cut %u)\n", (unsigned)cut); break; }
        }
        bad += check("ahead: an erase cut at any point is done again; the valid copy never touched", ok);
        /* .. and one that only reached the sector's first page (the commit record and more read 0xFF, the rest does
         * not): blank is the WHOLE sector */
        memset(nor, 0xFF, sizeof nor);
        st_save(AO, a, 262);
        memset(nor + B, 0x00, 4096);
        memset(nor + B, 0xFF, 1024);
        e0 = erases;
        bad += check("ahead: a spare whose first pages alone read 0xFF is erased by the save",
                     st_save(AO, b, 262) == 0 && erases == e0 + 1u && loads(AO, b, 262) && nor[B + 4095] == 0xFF);
        /* no valid copy and rubbish in A (where the first save goes) */
        memset(nor, 0xFF, sizeof nor);
        memset(nor + A, 0x5A, 4096);
        e0 = erases;
        bad += check("ahead: no object, rubbish in A: st_prep erases A; the save programs only",
                     st_load(AO, got, sizeof got) < 0 && st_prep(AO) == 1 && sector_blank(A) &&
                     st_save(AO, a, 262) == 0 && erases == e0 + 1u && loads(AO, a, 262));
        /* flash errors */
        st_save(AO, b, 262);
        erase_error = 1;
        ok = st_prep(AO) < 0 && !st_ready(AO) && loads(AO, b, 262);
        erase_error = 0;
        write_protected = 1;
        ok &= st_prep(AO) == -7 && !st_ready(AO) && loads(AO, b, 262);
        write_protected = 0;
        bad += check("ahead: an erase error or a protected part: not ready, the data kept", ok);
        /* the other objects: as before, one erase a save, no erase ahead */
        memset(nor, 0xFF, sizeof nor);
        calls = io_calls;
        e0 = erases;
        bad += check("ahead: not for the stores (st_prep refuses without touching the flash; a save erases)",
                     st_prep(OBJ_CZBANK0) < 0 && st_prep(OBJ_FM6BANK) < 0 && io_calls == calls &&
                     st_save(OBJ_CZBANK0, a, sizeof a) == 0 && erases == e0 + 1u && !st_ready(OBJ_CZBANK0));
        bad += check("ahead: the favourites and the session are the list; the settings and the stores are not",
                     st_ahead_ix(OBJ_FAVS) >= 0 && st_ahead_ix(OBJ_SESSION) >= 0 && st_ahead_ix(OBJ_SETTINGS) < 0 && st_ahead_ix(OBJ_CZBANK0 + 3) < 0 && st_ahead_ix(OBJ_X_UPRESET0) < 0 &&
                     st_ahead_ix(OBJ_FM6BANK) < 0);
        /* an older firmware (74eae04's writer) on the flash this one leaves, and back */
        memset(nor, 0xFF, sizeof nor);
        memset(st_ahead_ok, 0, sizeof st_ahead_ok);
        st_save(AO, a, 262);
        st_save(AO, b, 262);
        st_prep(AO);                          /* valid B + erased A */
        ok = loads(AO, b, 262) && st_save_old(AO, a, 200) == 0 && loads(AO, a, 200);
        ok &= st_save_old(AO, b, 262) == 0 && loads(AO, b, 262);    /* both copies valid: the old state */
        e0 = erases;
        ok &= st_prep(AO) == 1 && erases == e0 + 1u && loads(AO, b, 262);
        ok &= st_save(AO, a, 262) == 0 && erases == e0 + 1u && loads(AO, a, 262);
        bad += check("ahead: 74eae04's writer reads and writes what this one leaves, and the reverse", ok);
    }
    /* ================= the settings object: NOT erased ahead (3y52) ================= */
    {
        uint32_t A = st_sector(OBJ_SETTINGS, 0), B = st_sector(OBJ_SETTINGS, 1), e0, cut, calls;
        int ok = 1;
        memset(nor, 0xFF, sizeof nor);
        memset(st_ahead_ok, 0, sizeof st_ahead_ok);
        erases = 0;
        calls = io_calls;
        bad += check("settings: st_prep refuses, touches nothing, never ready",
                     st_prep(OBJ_SETTINGS) < 0 && io_calls == calls && !st_ready(OBJ_SETTINGS));
        e0 = erases;
        ok &= st_save(OBJ_SETTINGS, a, 262) == 0 && erases == e0 + 1u && erase_off[e0] == A;
        ok &= st_save(OBJ_SETTINGS, b, 262) == 0 && erases == e0 + 2u && erase_off[e0 + 1u] == B;
        ok &= st_prep(OBJ_SETTINGS) < 0 && !sector_blank(A) && !sector_blank(B) && loads(OBJ_SETTINGS, b, 262);
        bad += check("settings: every save erases at its time (a blank copy too)", ok);
        /* the fallback: the previous copy stays until the next save; a rotten current copy falls back to it */
        nor[B + ST_PAYLOAD_OFF + 7] ^= 0x01;
        bad += check("settings: the previous copy kept: a rotten current one falls back", loads(OBJ_SETTINGS, a, 262));
        nor[B + ST_PAYLOAD_OFF + 7] ^= 0x01;
        /* the power-cut walk, Felucca's: a save cut at every byte keeps the current copy */
        {
            static uint8_t copies[2 * ST_SECTOR];
            memcpy(copies, nor + A, sizeof copies);
            ok = 1;
            for (cut = 0; cut < 200u + sizeof(st_hdr_t); cut++) {
                memcpy(nor + A, copies, sizeof copies);
                fail_bytes = (int)cut;
                ok &= st_save(OBJ_SETTINGS, a, 200) != 0;
                fail_bytes = -1;
                ok &= loads(OBJ_SETTINGS, b, 262);
            }
            bad += check("settings: a save cut at every byte keeps the current copy", ok);
        }
    }
    /* ================= the quiet writes: when ================= */
    {
        st_when_t w = {20000, 0, 0, 0, 0, 0, 1};
        int ok = st_write_due(&w) && !st_ahead_due(&w);
        w.ready = 0; ok &= st_write_due(&w) && st_ahead_due(&w);
        w.busy = 1; ok &= !st_write_due(&w) && !st_ahead_due(&w);
        w.busy = 0; w.change = 20000 - 1499; ok &= !st_write_due(&w);
        w.change = 20000 - 1500; w.ready = 1; ok &= st_write_due(&w);
        w.loud = 20000 - 999; ok &= !st_write_due(&w);
        w.loud = 20000 - 1000; ok &= st_write_due(&w);
        w.ready = 0; ok &= !st_write_due(&w);                    /* has to erase: 4 s without a sound .. */
        w.loud = 20000 - 4000; ok &= st_write_due(&w);
        w.input = 20000 - 3999; ok &= !st_write_due(&w);         /* .. and without input */
        w.input = 20000 - 4000; ok &= st_write_due(&w) && !st_ahead_due(&w);
        w.loud = w.input = 20000 - 9999; ok &= !st_ahead_due(&w);
        w.loud = w.input = 20000 - 10000; ok &= st_ahead_due(&w);
        w.wrote = 20000 - 249; ok &= !st_ahead_due(&w) && !st_write_due(&w);
        w.now = 5; w.change = w.loud = w.input = w.wrote = 0xFFFFF000u; ok &= st_write_due(&w);   /* (the clock wraps) */
        bad += check("when: st_write_due / st_ahead_due, each condition at its edge", ok);
    }
    {
        /* the unit's own case: a sound marked at 20.0 s, the next note 1.6 s later; the spare erased at power-on */
        static const ev_t one[] = {{20000, 'm', 0}, {21600, 'n', 800}};
        static const ev_t playing[] = {{20000, 'n', 3000}, {20500, 'm', 0}};
        static const ev_t many[] = {{20000, 'm', 0}, {20400, 'm', 0}, {21000, 'm', 0}, {22400, 'm', 0}, {23000, 'm', 0}};
        static const ev_t two[] = {{20000, 'm', 0}, {21600, 'n', 800}, {24000, 'm', 0}, {25000, 'n', 500}, {26000, 'n', 500},
                                   {28000, 'i', 0}, {30000, 'n', 400}};
        static const ev_t cancel[] = {{20000, 'm', 0}, {29900, 'i', 0}, {39800, 'i', 0}};
        static const ev_t early[] = {{2000, 'm', 0}, {3600, 'n', 800}};
        uint32_t A = st_sector(OBJ_FAVS, 0);
        sim(OBJ_FAVS, one, 2, 29000, 1);
        bad += check("when: power-on erases the stale copy, before anything plays", erases >= 1 && erase_at[0] == 0 && erase_off[0] == A);
        bad += check("when: a mark, a note 1.6 s later: written 1.5 s after the mark, program only",
                     sim_writes == 1 && sim_write_at[0] == 21500 && erases == 1 && !erase_within(21600, 2000));
        sim(OBJ_FAVS, one, 2, 40000, 1);
        bad += check("when: .. and the erase ahead 10 s after the note's end, the last input and sound",
                     erases == 2 && sim_aheads == 1 && erase_at[1] == 32390 && st_ready(OBJ_FAVS));
        sim(OBJ_FAVS, playing, 2, 30000, 1);
        bad += check("when: marked while a note sounds: not written until 1 s after it ends",
                     sim_writes == 1 && sim_write_at[0] == 23990 && erases == 1);
        sim(OBJ_FAVS, many, 5, 30000, 1);
        bad += check("when: five marks, each within 1.5 s of the one before: ONE write, 1.5 s after the last",
                     sim_writes == 1 && sim_write_at[0] == 24500 && progs == 3 && erases == 1);
        sim(OBJ_FAVS, two, 7, 60000, 1);
        bad += check("when: a second mark before the erase ahead waits for 4 s without sound or input, not for 1",
                     sim_writes == 2 && sim_write_at[0] == 21500 && sim_write_at[1] == 34390 && erases == 3 &&
                     erase_at[1] == 34390 && !erase_within(25000, 2000) && !erase_within(26000, 2000) &&
                     !erase_within(28000, 2000) && !erase_within(30000, 2000) && erase_at[2] == 40390 && sim_aheads == 1);
        sim(OBJ_FAVS, cancel, 3, 49790, 1);
        bad += check("when: an input 9.9 s into the idle puts the erase ahead off; twice",
                     sim_writes == 1 && erases == 1 && sim_aheads == 0 && !st_ready(OBJ_FAVS));
        sim(OBJ_FAVS, cancel, 3, 49800, 1);
        bad += check("when: .. and it runs 10 s after the last one", erases == 2 && erase_at[1] == 49800 && sim_aheads == 1);
        sim(OBJ_FAVS, early, 2, 9000, 0);
        bad += check("when: without the power-on erase the first mark's write waits for the idle too (and erases once)",
                     sim_writes == 1 && sim_write_at[0] == 8390 && erases == 1 && erase_at[0] == 8390 && !erase_within(3600, 2000));
        sim(OBJ_SESSION, one, 2, 40000, 1);
        bad += check("when: the session as the favourites: power-on erase, program-only write, the erase ahead",
                     sim_writes == 1 && sim_write_at[0] == 21500 && erases == 2 && erase_at[0] == 0 && erase_at[1] == 32390);
        /* the settings: not erased ahead, so the poll treats them as before the quiet writes (cr_settings.c crs_when:
         * ready = not one of the erase-ahead objects): written 1.5 s after the change, 1 s silent, erasing then;
         * no erase at power-on, none ahead */
        sim(OBJ_SETTINGS, one, 2, 40000, 1);
        bad += check("when: the settings: no power-on or idle erase; the write erases at its time (1.5 s, as before)",
                     sim_writes == 1 && sim_write_at[0] == 21500 && erases == 1 && erase_at[0] == 21500 &&
                     sim_aheads == 0);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
