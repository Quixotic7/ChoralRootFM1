/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the backup / restore SysEx handler (firmware/src/cr_backup.c, web/EDITOR_PROTOCOL.md "ChoralRoot:
 * backup and restore"): the firmware as the emulator builds it (tools/emu/emu_firmware.h: tests/hostsim.c's sound
 * side, the ChoralRoot UI, storage.c on emu_hal_fw.h's RAM NOR), requests fed to crb_handle as the main loop would,
 * the replies taken at CRB_SEND.
 *   sh tests/run_cr_tests.sh
 * Checks: INFO; LIST of every object (settings, banks, FM6 bank, VA, FM6 patches, CZ-1 tones and banks, the QUAD
 * store (22), 10 loops; no sample slots) with sizes
 * and CRCs; GET of
 * all of them in 256-byte pieces; a restore of everything onto an erased flash (PUT) gives a LIST and bytes
 * identical to the backup and reloads the mirrors; a CRC error, a short object, a malformed record are refused
 * with nothing written; a playing loop answers busy (3) and the commit goes through once it stops; stale sessions
 * (the UI's buffer, a USB reset, 15 s); Felucca's objects: a PER4 settings record becomes PER5 with ChoralRoot's
 * defaults, the banks and the FM6 bank as they are (Felucca 1.0's 27-slot FM6 bank too, imported at the next boot),
 * ids 0 / 2..5 / 32..34 refused, the SMP_* commands unanswered; the Sounds (docs/SOUNDS.md): single slots imported,
 * renamed, deleted and replaced by another engine through whole-object PUTs, usable at once (name, engine, patch), a
 * bad blob / bank header refused with nothing written, LIST's sizes and CRCs; a pool bank as the device manager sends
 * one (3y78: a used FM TONE slot's 80-byte patch taken and loaded, a deleted slot's stale bytes written as zeros, a
 * wrong patch length refused); SCREEN (74: off / on); SELECT (76: ok, a bad
 * part / engine / position, busy) and the reload of a playing part after a PUT of its slot (its patch CRC); RESTART. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

/* the replies */
static uint8_t rx[4096];
static uint32_t rx_n, rx_count;
#define CRB_SEND(p, n) (memcpy(rx, (p), (n)), rx_n = (n), rx_count++)
/* 77 STUB (cr_backup.c crb_stub): the boot stub's version as the mailbox would say it, and the restart's request */
static unsigned t_stub_ver, t_stub_went;
#define CRB_STUB_VER() t_stub_ver
#define CRB_STUB_GO(w) (t_stub_went = (w))
/* the update gesture (cr_backup.c cr_update_poll): the screen it leaves on, the ROM's USB boot when no stub ran */
static const char *t_upd_screen;
static unsigned t_uboot_went;
#define CRB_UPDATE_SCREEN(t) (t_upd_screen = (t))
#define CRB_UBOOT_GO() (t_uboot_went++)
#include "../tools/emu/emu_firmware.h"
/* 0.22: the patches live in the sound pool (upool.c); the stores' readers of before, as the checks below use them */
#include "cr_pool_shim.h"
#include "../firmware/stub/stub_core.h"            /* the request words the stub knows */

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------- the wire --- */
static uint8_t tx[1024];
static uint32_t txn;
static void t_begin(uint32_t cmd) { tx[0] = 0x7D; tx[1] = 0x46; tx[2] = 0x4C; tx[3] = (uint8_t)cmd; txn = 4; }
static void t_b(uint32_t v) { tx[txn++] = (uint8_t)(v & 127u); }
static void t_u32(uint32_t v) { for (int i = 0; i < 5; i++) t_b(v >> (7 * i)); }
static void t_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++) m |= (uint32_t)(p[i] >> 7) << i;
        t_b(m);
        for (i = 0; i < k; i++) t_b(p[i]);
        p += k; n -= k;
    }
}
/* send; -> the reply's payload (after F0 7D 46 4C cmd), its length in *n; 0: no reply */
static const uint8_t *t_send(uint32_t *n)
{
    uint32_t before = rx_count;
    crb_handle(tx, txn);
    if (rx_count == before) { *n = 0; return 0; }
    if (rx_n < 6 || rx[0] != 0xF0 || rx[1] != 0x7D || rx[2] != 0x46 || rx[3] != 0x4C || rx[4] != tx[3] || rx[rx_n - 1] != 0xF7) {
        printf("  bad reply frame\n");
        *n = 0;
        return 0;
    }
    for (uint32_t i = 1; i + 1 < rx_n; i++)
        if (rx[i] > 127u) { printf("  8-bit byte in a reply\n"); fails++; }
    *n = rx_n - 6;
    return rx + 5;
}
static uint32_t r32(const uint8_t *p) { return p[0] | p[1] << 7 | p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28; }
static uint32_t unpack(const uint8_t *a, uint32_t na, uint8_t *out)
{
    uint32_t n = 0;
    while (na) {
        uint32_t m = *a++, k;
        na--;
        k = na > 7 ? 7 : na;
        for (uint32_t j = 0; j < k; j++, na--) out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}

/* ------------------------------------------------------------------ the objects --- */
typedef struct { uint8_t id; uint32_t size, crc; uint8_t *bytes; } obj_t;
static obj_t man[48];
static uint32_t nman;

static int t_list(void)                              /* -> rc; man[] filled */
{
    uint32_t n, i;
    const uint8_t *a;
    t_begin(65);
    a = t_send(&n);
    if (!a || n < 3 || a[0] != 1) return -1;
    if (a[1]) return a[1];
    nman = a[2];
    if (n != 3 + nman * 11) return -2;
    for (i = 0; i < nman; i++) {
        const uint8_t *p = a + 3 + i * 11;
        free(man[i].bytes);
        man[i].id = p[0];
        man[i].size = r32(p + 1);
        man[i].crc = r32(p + 6);
        man[i].bytes = 0;
    }
    return 0;
}
static int t_get(uint32_t id, uint32_t off, uint32_t count, uint8_t *out)   /* -> rc */
{
    uint32_t n;
    const uint8_t *a;
    t_begin(66); t_b(id); t_u32(off); t_b(count & 127); t_b(count >> 7);
    a = t_send(&n);
    if (!a || n < 9 || a[0] != id || r32(a + 2) != off) return -1;
    if (a[1]) return a[1];
    if ((a[7] | a[8] << 7) != (int)count || unpack(a + 9, n - 9, out) != count) return -2;
    return 0;
}
static int t_capture(void)                           /* LIST, then every object read and checked against its CRC */
{
    uint32_t i, off;
    if (t_list()) return -1;
    for (i = 0; i < nman; i++) {
        man[i].bytes = malloc(man[i].size + 1);
        for (off = 0; off < man[i].size; off += 256) {
            uint32_t k = man[i].size - off < 256 ? man[i].size - off : 256;
            if (t_get(man[i].id, off, k, man[i].bytes + off)) return -2;
        }
        if (st_crc32(man[i].bytes, man[i].size) != man[i].crc) return -3;
    }
    return 0;
}
static obj_t *find(obj_t *m, uint32_t n, uint32_t id)
{
    for (uint32_t i = 0; i < n; i++) if (m[i].id == id) return &m[i];
    return 0;
}

static int t_put_op(uint32_t op, uint32_t id)       /* -> rc of commit (2) / abort (3) */
{
    uint32_t n;
    const uint8_t *a;
    t_begin(67); t_b(op); t_b(id);
    a = t_send(&n);
    return a && n == 3 && a[0] == op && a[1] == id ? a[2] : -1;
}
static int t_put_begin(uint32_t id, uint32_t size, uint32_t crc)
{
    uint32_t n;
    const uint8_t *a;
    t_begin(67); t_b(0); t_b(id); t_u32(size); t_u32(crc);
    a = t_send(&n);
    return a && n == 3 && a[0] == 0 && a[1] == id ? a[2] : -1;
}
static int t_put_data(uint32_t id, uint32_t off, const uint8_t *p, uint32_t k)
{
    uint32_t n;
    const uint8_t *a;
    t_begin(67); t_b(1); t_b(id); t_u32(off); t_pack(p, k);
    a = t_send(&n);
    return a && n == 3 && a[0] == 1 && a[1] == id ? a[2] : -1;
}
static int t_put(uint32_t id, const uint8_t *p, uint32_t size)   /* begin, data, commit: the first nonzero rc */
{
    int rc = t_put_begin(id, size, st_crc32(p, size));
    for (uint32_t off = 0; !rc && off < size; off += 256)
        rc = t_put_data(id, off, p + off, size - off < 256 ? size - off : 256);
    return rc ? rc : t_put_op(2, id);
}
static int t_smp(uint32_t cmd, uint32_t slot)        /* SMP_BEGIN / SMP_ERASE: answered? (-1: no reply) */
{
    uint32_t n;
    const uint8_t *a;
    t_begin(cmd); t_b(slot);
    a = t_send(&n);
    return a ? (int)n : -1;
}

/* ------------------------------------------------------------------ the content --- */
static void power_on(void)                           /* persist_boot + the UI, on the flash as it is */
{
    up_boot();
    cr_settings_boot();
    cu_loop_scan();
}
static uint32_t make_loop(uint32_t k, uint32_t nev)   /* slot k: nev events; -> the record's length */
{
    static crl_data_t d;
    uint32_t i, n;
    memset(&d, 0, sizeof d);
    d.len = 4u * CRL_PPQN * 2u;
    d.nlayers = 1;
    d.nev = (uint16_t)nev;
    for (i = 0; i < nev; i++) {
        d.ev[i].t = (uint16_t)(i * 24u % d.len);
        d.ev[i].dur = 12;
        d.ev[i].root = (uint8_t)(48 + (i + k) % 24u);
        d.ev[i].vel = 100;
        d.ev[i].qx = 0;
    }
    n = cr_loop_pack(&d, cu_loop_buf, sizeof cu_loop_buf);
    crl_fl_save(k, cu_loop_buf, n);
    return n;
}

/* a version-3 FM TONE blob (the init patch in version 3's units) in a QUAD_BLOB buffer, zeros after its 80 bytes */
static void qb_v3(uint8_t *b)
{
    uint32_t i;
    memset(b, 0, QUAD_BLOB);
    b[0] = QUAD_MAGIC, b[1] = 3;
    for (i = 0; i < QP_NP5; i++)
        b[2 + i] = (uint8_t)(quad_range_v3(i).def - quad_range_v3(i).min);
}

int main(void)
{
    uint32_t n, i, k;
    const uint8_t *a;
    static obj_t ref[48];
    uint32_t nref;

    emu_flash_open();                                /* (no file: RAM only, erased) */
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    usb.config = 1;

    /* ---- INFO ---- */
    t_begin(1);
    a = t_send(&n);
    CHECK(a && n > 12 && !memcmp(a, "EMU", 4) && a[n - 6] == 0x42 && a[n - 4] == 3 && a[n - 3] == 0x43 && a[n - 1] == 3,
          "INFO: version, no engines, tags 42 01 03 and 43 01 03 (0.22: the sound pool's ids)");
    {   /* 3y66: the unit's ID. None known (the emulator reads no flash ID): no tag, the reply as before. Known: INFO
         * ends with 49 08 and its 8 hex digits, after the same bytes as before; the hosts of before read the version
         * to its NUL (fm1_install.py device_info, fm1backup.js deviceInfo) and editor.html Felucca's fields from the
         * start (NTRK, CHAIN_ROWS), which are where they were. cr_unit_id_set: the CRC-32 of the flash's 16 bytes
         * (zlib's: Python's zlib.crc32 of 03 14 25 .. F2 is 63E68B82), sixteen equal bytes no ID */
        uint8_t was[64], u[16];
        uint32_t wn = n < sizeof was ? n : 0, i, p, tags = 0, ok;
        char lab[sizeof ((cr_screen_t *)0)->label];
        cr_screen_t sc;
        memcpy(was, a, wn);
        ok = cr_unit_id[0] == 0;
        for (i = 0; i < 16u; i++)
            u[i] = (uint8_t)(i * 17u + 3u);
        cr_unit_id_set(u);
        ok &= !strcmp(cr_unit_id, "63E68B82");
        t_begin(1);
        a = t_send(&n);
        ok &= a && wn && n == wn + 10u && !memcmp(a, was, wn) && a[wn] == 0x49 && a[wn + 1] == 8 && !memcmp(a + wn + 2, "63E68B82", 8);
        ok &= a && !strcmp((const char *)a, "EMU");                   /* (the old hosts: the version to the NUL) */
        for (p = a ? 4u + 7u : n; p + 2u <= n && p + 2u + a[p + 1] <= n; p += 2u + a[p + 1])   /* the tags, in order */
            tags = tags << 8 | a[p];
        ok &= a && p == n && tags == 0x424349u && a[4 + 5] == 0 && a[4 + 6] == 0;   /* (NTRK 0, CHAIN_ROWS 0: editor.html) */
        memset(&sc, 0, sizeof sc);
        cu.opt_sel = O_VERSION;
        cu_options_screen(&sc);
        ok &= !strcmp(sc.label, "unit ID 63E68B82");
        memset(u, 0xFF, sizeof u);
        cr_unit_id_set(u);
        ok &= cr_unit_id[0] == 0;
        t_begin(1);
        a = t_send(&n);
        ok &= a && n == wn && !memcmp(a, was, wn);
        cu_options_screen(&sc);
        memcpy(lab, sc.label, sizeof lab);
        ok &= !strcmp(lab, "no unit ID");
        cu.opt_sel = 0;
        CHECK(ok, "INFO with the unit's ID (3y66): 49 08 '63E68B82' appended, the bytes before it as without, the "
                  "version to the NUL 'EMU', the tags 42 43 49 in order; Options > Version's line 'unit ID 63E68B82'; "
                  "16 x FF: no ID, no tag, 'no unit ID'");
    }
    {   /* Options > Version (HISTORY 3y64): the value line holds 23 characters (cr_screen.h value[24]); a version
         * longer drops its leading "ChoralRoot ", one that fits shows whole, one too long even then is cut. INFO
         * above keeps CR_VERSION whole */
        char d[sizeof ((cr_screen_t *)0)->value], o[sizeof d];
        uint32_t ok = sizeof d == 24u;
        cu_version_text(d, "ChoralRoot 0.22-beta1 1-core", sizeof d);   /* (28: the single-core beta) */
        ok &= !strcmp(d, "0.22-beta1 1-core");
        cu_version_text(d, "ChoralRoot 0.22", sizeof d);
        ok &= !strcmp(d, "ChoralRoot 0.22");
        cu_version_text(d, "ChoralRoot 0.22-beta1 1", sizeof d);        /* (23: fits exactly) */
        ok &= !strcmp(d, "ChoralRoot 0.22-beta1 1");
        cu_version_text(d, "ChoralRoot 0.22-beta12 1-core and more", sizeof d);
        ok &= !strcmp(d, "0.22-beta12 1-core and ");
        opt_text(O_VERSION, o, sizeof o);
        ok &= !strcmp(o, CR_VERSION);
        CHECK(ok, "Options > Version: 'ChoralRoot 0.22-beta1 1-core' (28) shows '0.22-beta1 1-core', 'ChoralRoot 0.22' "
                  "and a 23-character one whole, a longer one cut at 23; this build's '%s' as it is", CR_VERSION);
    }
    t_begin(2); t_b(0); t_b(0);
    CHECK(!t_send(&n), "an editor command (GET 2) gets no reply");

    /* ---- an empty device: every object listed, sizes 0 but the settings ---- */
    CHECK(t_list() == 0 && nman == 34, "LIST on a fresh flash: 34 objects (0.22: the pool's 13 banks for the legacy 8)");
    for (i = 0, k = 0; i < nman; i++) k += man[i].size != 0;
    CHECK(k <= 1, "fresh flash: nothing stored (the settings at most)");

    /* ---- content: settings, both banks, FM6, VA, three loops ---- */
    cs.bpm = 133;
    cs.tonic = 5;
    cr_settings_commit();                            /* (the global save: the tempo and the key are manual settings, 0.18) */
    {
        static up_bank_t b;
        static fm6_bank_t f;
        static va_store_t v;
        static fm6u_t fu[2];
        uint8_t blob[VA_BLOB], fb[FM6_BLOB], rec[FM6_PACKED];
        for (k = 0; k < 2; k++) {
            memset(&b, 0, sizeof b);
            b.magic = UP_BANK_MAGIC;
            b.rsize = sizeof(up_rec_t);
            b.nslot = UP_PER_BANK;
            b.r[3 + k].used = UP_USED;
            b.r[3 + k].ver = UP_VER;
            b.r[3 + k].engine = ENGI_VA;
            b.r[3 + k].np = P_COUNT;
            memcpy(b.r[3 + k].name, k ? "PAD TWO" : "PAD ONE", 7);
            for (i = 0; i < P_COUNT; i++) b.r[3 + k].packed[i] = (uint8_t)(64 + (int)(i % 9));
            CHECK(st_save(OBJ_UPRESET0 + k, &b, sizeof b) == 0, "setup: bank %u", (unsigned)k);
        }
        memset(&f, 0, sizeof f);
        f.magic = FM6_BANK_MAGIC; f.ver = FM6_BANK_VER; f.nslot = FM6_BANK_N; f.used = 5;
        memcpy(f.fn, FM6_FNDEF, FM6_NFN);
        for (i = 0; i < FM6_PACKED; i++) rec[i] = (uint8_t)(i & 127u);
        fm6_pack7(f.pk[0], rec, FM6_PACKED);
        for (i = 0; i < FM6_PACKED; i++) rec[i] = (uint8_t)((i * 3u) & 127u);
        fm6_pack7(f.pk[2], rec, FM6_PACKED);
        CHECK(fm6_bank_valid(&f) && st_save(OBJ_FM6BANK, &f, sizeof f) == 0, "setup: FM6 bank");
        trk[1].eng_req = ENGI_FM6;
        fm6_load_slot(1, 14);                        /* (F15: STRINGS) */
        fm6_blob_get(&trk[1], fb);
        CHECK(fm6_blob_ok(fb), "setup: an FM6 blob");
        for (k = 0; k < 2; k++) {
            memset(&fu[k], 0, sizeof fu[k]);
            fu[k].magic = FM6U_MAGIC; fu[k].ver = FM6U_VER; fu[k].nslot = FM6U_HALF; fu[k].first = (uint16_t)(k * FM6U_HALF);
            fu[k].blob = FM6_BLOB; fu[k].used = 1u << 3;
            memcpy(fu[k].b[3], fb, FM6_BLOB);
            CHECK(fm6u_valid(&fu[k], k) && st_save(OBJ_FM6STORE0 + k, &fu[k], sizeof fu[k]) == 0, "setup: FM6 store %u", (unsigned)k);
        }
        memset(&v, 0, sizeof v);
        v.magic = VA_STORE_MAGIC; v.ver = 3; v.nslot = UP_LEGACY; v.blob = VA_BLOB;
        trk[0].eng_req = ENGI_VA;
        va_blob_get(&trk[0], blob);
        CHECK(va_blob_ok(blob), "setup: a VA blob");
        memcpy(v.p[3], blob, VA_BLOB);
        memcpy(v.p[19], blob, VA_BLOB);
        v.used = 1u << 3 | 1u << 19;
        CHECK(va_store_valid(&v) && st_save(OBJ_VASTORE, &v, sizeof v) == 0, "setup: VA store");
        {                                            /* CZ-1: U04 / U20 hold D-1 PIANO 1, bank C saved with its name */
            static czu_t cu2[2];
            static cz_bank_t cb;
            for (k = 0; k < 2; k++) {
                memset(&cu2[k], 0, sizeof cu2[k]);
                cu2[k].magic = CZU_MAGIC; cu2[k].ver = CZU_VER; cu2[k].nslot = CZU_HALF; cu2[k].first = (uint16_t)(k * CZU_HALF);
                cu2[k].blob = CZ_BYTES; cu2[k].used = 1u << 3;
                memcpy(cu2[k].b[3], CZ_FACTORY[24], CZ_BYTES);
                CHECK(czu_valid(&cu2[k], k) && st_save(czu_obj(k), &cu2[k], sizeof cu2[k]) == 0, "setup: CZ-1 store %u", (unsigned)k);
            }
            cz_bank_default(&cb, 2);
            memcpy(cb.name, "MY CZ BANK", 10);
            CHECK(cz_bank_valid(&cb) && st_save(OBJ_CZBANK0 + 2, &cb, sizeof cb) == 0, "setup: CZ-1 bank C");
        }
        {                                            /* QUAD: U06 holds the BELL preset's patch */
            static quad_store_t qs;
            uint8_t qb[QUAD_BLOB];
            trk[0].eng_req = ENGI_QUAD;
            quad_blob_preset(&trk[0], 1);
            qb_v3(qb);   /* (the legacy store: an 80-byte version-3 blob, zeros after) */
            memset(&qs, 0, sizeof qs);
            qs.magic = QUAD_STORE_MAGIC; qs.ver = QUAD_STORE_VER; qs.nslot = UP_LEGACY; qs.blob = QUAD_BLOB_V3; qs.used = 1u << 5;
            memcpy(qs.p[5], qb, QUAD_BLOB_V3);
            CHECK(quad_blob_ok(qb) && quad_store_valid(&qs) && st_save(OBJ_QUADSTORE, &qs, sizeof qs) == 0, "setup: QUAD store");
        }
    }
    make_loop(0, 4);
    make_loop(4, 300);
    make_loop(9, CRL_MAX_EV);
    {   /* 0.22: the first power-on migrates the legacy slots into the pool and never writes the legacy sectors */
        uint32_t c0 = st_crc32(emu_flash + 0x97000, 0xB4000 - 0x97000), c1 = st_crc32(emu_flash + 0xDC000, 0x4000);
        uint8_t pa[VA_BLOB], pb[VA_BLOB];
        power_on();
        CHECK(c0 == st_crc32(emu_flash + 0x97000, 0xB4000 - 0x97000) && c1 == st_crc32(emu_flash + 0xDC000, 0x4000),
              "migration: the legacy sectors untouched (0x97000..0xB3FFF, 0xDC000..0xDFFFF)");
        trk[0].eng_req = ENGI_VA;
        va_blob_get(&trk[0], pb);
        CHECK(upool_boot_rc == 2 && up_used(3) && up_used(20) && up_rec(3)->engine == ENGI_VA &&
              up_patch_get(3, ENGI_VA, pa, VA_BLOB) == 0 && up_patch_get(20, ENGI_VA, pa, VA_BLOB) != 0 &&
              up_patch_get(3, ENGI_FM6, pa, FM6_BLOB) != 0, "migration: U04 (VA, its patch) and U21 (VA, no patch in the store at 21)");
        power_on();
        CHECK(upool_boot_rc == 0 && up_used(3) && up_used(20), "migration: once (the second power-on finds the pool)");
    }
    CHECK(cs.loop_used == (1u | 1u << 4 | 1u << 9), "setup: loops in slots 1, 5, 10");

    /* ---- backup: LIST, GET everything, CRCs ---- */
    CHECK(t_capture() == 0, "backup: LIST + GET of every object, each CRC as listed");
    {
        static const uint8_t ids[34] = {1, 8, 14, 15, 16, 17, 18, 19, 20, 21, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
                                        62, 23, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
        int same = nman == 34;
        for (i = 0; same && i < 34; i++) same = man[i].id == ids[i];
        CHECK(same, "LIST: ids 1 8 14..21 (the CZ-1 banks) 50..62 (the pool's banks, 0.22) 23 (the favourites, after the sounds) 40..49 in order");
    }
    CHECK(find(man, nman, 1)->size == sizeof(persist_t) && !find(man, nman, 6) && !find(man, nman, 9) && !find(man, nman, 22) &&
          find(man, nman, 8)->size == sizeof(fm6_bank_t) && find(man, nman, 50)->size == ST_PAYLOAD_MAX &&
          find(man, nman, 51)->size == ST_PAYLOAD_MAX && find(man, nman, 52)->size == ST_PAYLOAD_MAX &&
          find(man, nman, 53)->size == ST_PAYLOAD_MAX && find(man, nman, 54)->size == 0 && find(man, nman, 62)->size == 0 &&
          find(man, nman, 16)->size == sizeof(cz_bank_t) && find(man, nman, 14)->size == 0 &&
          find(man, nman, 40)->size > 16 && find(man, nman, 41)->size == 0 && find(man, nman, 49)->size == CRL_REC_HDR + 2u + 7u * CRL_MAX_EV,
          "LIST: the sizes (settings PER5, the FM6 bank, the pool's four migrated banks, loops; empty 0)");
    {
        const persist_t *p = (const persist_t *)find(man, nman, 1)->bytes;
        CHECK(p->magic == PERSIST_MAGIC && p->cr.bpm == 133 && p->cr.tonic == 5, "backup: the settings carry the last global save");
    }
    t_begin(66); t_b(50); t_u32(3700); t_b(200 & 127); t_b(200 >> 7);
    a = t_send(&n);
    CHECK(a && a[1] == 1, "GET past the end: rc 1 (%d %d)", a ? a[0] : -1, a ? a[1] : -1);
    t_begin(66); t_b(2); t_u32(0); t_b(16); t_b(0);
    a = t_send(&n);
    CHECK(a && a[1] == 1, "GET of id 2 (a Felucca project): rc 1");
    nref = nman;
    for (i = 0; i < nman; i++) { ref[i] = man[i]; man[i].bytes = 0; }

    /* ---- a USB reset makes the snapshot stale ---- */
    usb.resets++;
    t_begin(66); t_b(1); t_u32(0); t_b(16); t_b(0);
    a = t_send(&n);
    CHECK(a && a[1] == 5, "GET after a USB reset: rc 5 (LIST again)");

    /* ---- restore everything onto an erased flash ---- */
    memset(emu_flash, 0xFF, sizeof emu_flash);
    power_on();
    CHECK(t_list() == 0 && find(man, nman, 50)->size == 0 && find(man, nman, 58)->size == 0, "erased: the pool's banks empty");
    {
        int rc = 0;
        for (i = 0; i < nref && !rc; i++) {
            const obj_t *o = &ref[i];
            if (o->id == 1) continue;                /* the settings last, as the page does */
            rc = t_put(o->id, o->bytes, o->size);
            if (rc) printf("  restore id %u: rc %d\n", o->id, rc);
        }
        CHECK(!rc, "restore: every object accepted (PUT)");
        CHECK(!cr_restore_lock, "restore: no settings lock before the settings");
        {   /* the session (0.18) belongs to the settings it was written beside: a restored settings object empties it */
            static cr_session_t x;
            cr_session_defaults(&x);
            x.voicing = 5;
            cr_session_seal(&x);
            CHECK(st_save(OBJ_SESSION, &x, CSS_SIZE) == 0 && st_load(OBJ_SESSION, &x, sizeof x) == (int)CSS_SIZE,
                  "restore: a session in flash before the settings are restored");
            css_flash = cr_session_hash(&x);
            rc = t_put(1, find(ref, nref, 1)->bytes, find(ref, nref, 1)->size);
            CHECK(rc == 0 && st_load(OBJ_SESSION, &x, sizeof x) == 0 && css_flash == 0,
                  "restore: the settings committed: the session object is empty (the restart comes up on the settings alone)");
            CHECK(crs_obj_crc == st_crc32(&crs_rec, sizeof crs_rec), "restore: the session's binding follows the restored object");
        }
        CHECK(rc == 0 && cr_restore_lock && CR_SETTINGS_BUSY(), "restore: settings committed, the settings saves held until the restart");
    }
    {
        uint8_t pa[VA_BLOB];
        CHECK(up_used(3) && up_used(16 + 4) && cs.loop_used == (1u | 1u << 4 | 1u << 9) && fm6_bank.used == 5u &&
              up_patch_get(3, ENGI_VA, pa, VA_BLOB) == 0 && up_patch_get(20, ENGI_VA, pa, VA_BLOB) != 0,
              "restore: the pool's table and the mirrors reloaded (user sounds, loops, FM6 bank; U04's VA patch)");
    }
    CHECK(t_capture() == 0 && nman == nref, "restore: LIST + GET again");
    {
        int same = 1;
        for (i = 0; i < nref; i++) {
            const obj_t *o = find(man, nman, ref[i].id);
            if (!o || o->size != ref[i].size || o->crc != ref[i].crc || memcmp(o->bytes, ref[i].bytes, o->size)) {
                printf("  id %u differs (%u / %u bytes)\n", ref[i].id, o ? o->size : 0, ref[i].size);
                same = 0;
            }
        }
        CHECK(same, "restore: every object identical to the backup (list -> read -> write back -> identical)");
    }

    /* ---- refused: CRC, size, content; nothing written ---- */
    {
        obj_t *o = find(ref, nref, 50);              /* (0.22: the pool's first bank) */
        uint32_t erases = emu_stalls;
        uint8_t bad[4096];
        int rc = t_put_begin(50, o->size, o->crc ^ 1u);
        for (i = 0; !rc && i < o->size; i += 256) rc = t_put_data(50, i, o->bytes + i, o->size - i < 256 ? o->size - i : 256);
        CHECK(!rc && t_put_op(2, 50) == 2 && emu_stalls == erases, "a CRC error: commit rc 2, nothing erased");
        CHECK(t_put_begin(6, 100, 0) == 1 && t_put_begin(1, 4000, 0) == 1 && t_put_begin(9, 17, 0) == 1 &&
              t_put_begin(50, 3000, 0) == 1, "a wrong size: begin rc 1");
        memcpy(bad, o->bytes, o->size);
        bad[3 * SP_SLOT + 3] = 0;                    /* U04's record: np 0 */
        CHECK(t_put(50, bad, o->size) == 2 && emu_stalls == erases, "a pool bank with a bad record: rc 2, nothing written");
        memcpy(bad, o->bytes, o->size);
        bad[3 * SP_SLOT + UPP_HDR + 1] = 7;          /* U04's patch: a length no engine has */
        CHECK(t_put(50, bad, o->size) == 2 && emu_stalls == erases, "a pool bank with a bad patch header: rc 2");
        o = find(ref, nref, 40);
        memcpy(bad, o->bytes, o->size);
        bad[4] = 9;                                  /* the loop record's version */
        CHECK(t_put(40, bad, o->size) == 2, "a loop record of another version: rc 2");
        o = find(ref, nref, 8);
        memcpy(bad, o->bytes, o->size);
        bad[12] = 200;                               /* a function setting out of its range */
        CHECK(t_put(8, bad, o->size) == 2 && emu_stalls == erases && fm6_bank.used == 5u &&
              !memcmp(&fm6_bank, o->bytes, sizeof fm6_bank),
              "an FM6 bank with a bad function setting: rc 2, nothing written, the bank kept");
        CHECK(t_put_data(50, 0, bad, 16) == 5 && t_put_op(2, 50) == 5, "data / commit without a begin: rc 5");
        o = find(ref, nref, 50);
        CHECK(t_put_begin(50, o->size, o->crc) == 0 && t_put_data(50, 256, o->bytes, 256) == 1, "data out of order: rc 1");
        CHECK(t_put_op(3, 50) == 0 && t_put_op(2, 50) == 5, "abort ends the session");
    }

    /* ---- stale sessions ---- */
    {
        obj_t *o = find(ref, nref, 51);
        CHECK(t_put_begin(51, o->size, o->crc) == 0 && t_put_data(51, 0, o->bytes, 256) == 0, "stale: begin + data");
        cu_loop_gen++;                               /* the UI loaded a loop slot through cu_loop_buf */
        CHECK(t_put_data(51, 256, o->bytes + 256, 256) == 5, "stale: the UI reused the staging buffer -> rc 5");
        CHECK(t_put_begin(51, o->size, o->crc) == 0, "stale: begin again");
        fm1_ms += 16000u;
        CHECK(t_put_data(51, 0, o->bytes, 256) == 5, "stale: 15 s without a request -> rc 5");
        CHECK(t_put_begin(51, o->size, o->crc) == 0, "stale: begin again");
        usb.resets++;
        CHECK(t_put_data(51, 0, o->bytes, 256) == 5, "stale: a USB reset -> rc 5");
    }

    /* ---- busy: a playing loop ---- */
    {
        obj_t *o = find(ref, nref, 41 + 3);          /* (an empty slot: a begin of size 0 deletes) */
        obj_t *l = find(ref, nref, 40);
        uint32_t erases = emu_stalls;
        cr_snap.lstate = CRL_PLAYING;
        CHECK(t_put_begin(41, l->size, l->crc) == 3, "busy: a begin while a loop plays: rc 3");
        cr_snap.lstate = CRL_STOPPED;
        CHECK(t_put_begin(41, l->size, l->crc) == 0, "busy: the begin once it stopped");
        for (i = 0; i < l->size; i += 256) t_put_data(41, i, l->bytes + i, l->size - i < 256 ? l->size - i : 256);
        cr_snap.lstate = CRL_PLAYING;
        CHECK(t_put_op(2, 41) == 3 && emu_stalls == erases, "busy: a commit while it plays: rc 3, nothing written");
        cr_snap.lstate = CRL_STOPPED;
        CHECK(t_put_op(2, 41) == 0 && (cs.loop_used >> 1 & 1u), "busy: the same commit once it stopped (slot 2 now holds the loop)");
        CHECK(t_put(41, 0, 0) == 0 && !(cs.loop_used >> 1 & 1u), "a loop slot of size 0: deleted");
        (void)o;
    }

    /* ---- Felucca's objects ---- */
    {
        const persist_t *p0 = (const persist_t *)find(ref, nref, 1)->bytes;
        static uint8_t per4[sizeof(persist_t)];
        uint32_t n4 = sizeof(persist_t) - sizeof(cr_settings_t);
        persist_t *rd = (persist_t *)cu_loop_buf;
        memcpy(per4, p0, n4);
        per4[0] = 0x34;                              /* "PER4" */
        CHECK(t_put(1, per4, n4) == 0, "Felucca: a PER4 settings record is taken");
        CHECK(st_load(OBJ_SETTINGS, rd, sizeof *rd) == (int)sizeof *rd && rd->magic == PERSIST_MAGIC &&
              rd->panel.magic == PANEL_MAGIC && rd->cr.magic == CRS_MAGIC && rd->cr.bpm == 120,
              "Felucca: stored as PER5, Felucca's fields kept, ChoralRoot's block the defaults");
        CHECK(CRS_DUALCORE(&rd->cr), "Felucca: a PER4 record: Dual Core On (the default of ChoralRoot's block since 0.22)");
        {   /* a 0.14 / 0.15 backup's settings (ChoralRoot's block v11: bit 6 of view clear = On, the default there, set
             * = Off): Dual Core Off on 0.16 whatever the bit says, the view and its lock kept; a 0.16 one (v12, set =
             * On) keeps what it says (cr_settings_import, docs/SETTINGS.md) */
            static persist_t pv;
            uint32_t k, h, i;
            for (k = 0; k < 4u; k++) {               /* v11 bit clear, v11 bit set, v12 On, v12 Off */
                memcpy(&pv, p0, sizeof pv);
                pv.cr.view = (uint8_t)(CRS_VIEW_BYTE(2u, 1) | (k == 1u || k == 2u ? 0x40u : 0u));
                cr_settings_seal(&pv.cr);
                if (k < 2u) {
                    pv.cr.version = 11;
                    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((const uint8_t *)&pv.cr)[i]; h *= 16777619u; }
                    pv.cr.check = h;
                }
                CHECK(t_put(1, (const uint8_t *)&pv, sizeof pv) == 0 &&
                      st_load(OBJ_SETTINGS, rd, sizeof *rd) == (int)sizeof *rd && rd->cr.version == CRS_VERSION &&
                      CRS_DUALCORE(&rd->cr) == (k == 2u) && CRS_VIEW(&rd->cr) == 2u && CRS_VIEW_LOCK(&rd->cr),
                      "settings restored (%s, bit 6 %s): Dual Core %s, the view and lock kept", k < 2u ? "v11" : "v12",
                      k == 1u || k == 2u ? "set" : "clear", k == 2u ? "On" : "Off");
            }
        }
        CHECK(t_put_begin(0, 3584, 0) == 1 && t_put_begin(2, 3584, 0) == 1 && t_put_begin(5, 0, 0) == 1,
              "Felucca: ids 0 (the music) and 2..5 (projects) refused (rc 1)");
        CHECK(t_put_begin(32, 4096, 0) == 1 && t_put_begin(33, 0, 0) == 1 && t_put_begin(34, 0, 0) == 1,
              "Felucca: ids 32..34 (sample slots) refused (rc 1)");
        CHECK(t_smp(11, 0) == -1 && t_smp(14, 0) == -1 && t_smp(11, 2) == -1, "SMP_BEGIN / SMP_ERASE: no reply (no SAMPLE engine)");
        {   /* (the parts' PTCH slots as they are, around the PUT: a part on a bank patch, B3, forgets its slot, so
             * fm6_poll loads the restored patch; one on a factory patch keeps it. Felucca's tests/backup_test.c checked
             * this of editor_backup.c, which cannot stage today's bank) */
            uint8_t s0 = fm6_slot[0], s1 = fm6_slot[1];
            fm6_slot[0] = 3;
            fm6_slot[1] = (uint8_t)(FM6_NFAC + 2u);
            static up_bank_t lb;                     /* (0.22: a legacy bank 0 made of slots 1..16 as they are) */
            lb.magic = UP_BANK_MAGIC; lb.rsize = sizeof(up_rec_t); lb.nslot = UP_PER_BANK;
            for (i = 0; i < UP_PER_BANK; i++) sp_read(i, 0, &lb.r[i], sizeof lb.r[i]);
            CHECK(t_put(6, (const uint8_t *)&lb, sizeof lb) == 0 && t_put(8, find(ref, nref, 8)->bytes, sizeof(fm6_bank_t)) == 0,
                  "Felucca: user preset banks and the FM6 bank restore");
            CHECK(fm6_bank.used == 5u && !memcmp(&fm6_bank, find(ref, nref, 8)->bytes, sizeof fm6_bank) &&
                  fm6_slot[0] == 3u && fm6_slot[1] == 0xFFu,
                  "the FM6 bank restored: the mirror is the bank, a part on a bank patch reloads, one on a factory patch stays");
            fm6_slot[0] = s0;
            fm6_slot[1] = s1;
        }
        {   /* Felucca 1.0's FM6 bank (27 unpacked records, version 1): taken, imported at the next boot */
            static uint8_t fel[16 + 27 * 128];
            uint8_t v1[FP_SIZE + 1u], rec[FM6_PACKED];
            memset(fel, 0, sizeof fel);
            fel[0] = 0x46; fel[1] = 0x4D; fel[2] = 0x36; fel[3] = 0x42;   /* "FM6B" */
            fel[4] = 1; fel[6] = 27; fel[8] = 1u << 1;                   /* slot 2 used */
            fm6_unpack(FM6_FACTORY[3], v1);
            fm6_pack(v1, fel + 16 + 128);
            CHECK(t_put(8, fel, sizeof fel) == 0, "Felucca 1.0: its 27-slot FM6 bank restores");
            fm6_bank_boot();
            CHECK(fm6_bank.used == 1u << 1 && fm6_bank_get(1, rec) == 0 && !memcmp(rec, FM6_FACTORY[3], FM6_PACKED),
                  "Felucca 1.0: its FM6 bank imported (B2 = the record)");
        }
        CHECK(t_put(6, 0, 0) == 0 && !up_used(3), "a bank of size 0: emptied");
    }

    /* ---- sounds: single slots through PUT (docs/SOUNDS.md: export / import / rename / delete as whole objects) ---- */
    {
        static up_bank_t b0, b1, bad_b;
        static va_store_t vs, bad_v;
        static fm6u_t f0;
        static czu_t c1;
        static uint8_t got[4096];
        uint8_t vb[VA_BLOB], fb[FM6_BLOB], out[256];
        char nm[13];
        int rc;
        memset(emu_flash, 0xFF, sizeof emu_flash);   /* a fresh device */
        power_on();
        trk[0].eng_req = ENGI_VA;
        va_blob_get(&trk[0], vb);
        trk[1].eng_req = ENGI_FM6;
        fm6_load_slot(1, 3);
        fm6_blob_get(&trk[1], fb);
        CHECK(va_blob_ok(vb) && fm6_blob_ok(fb), "sounds: a VA blob and an FM6 blob");
        /* a: import U03 (VA "MY PAD") and U05 (FM6 "TINE 2"): the stores first, the bank last */
        memset(&b0, 0, sizeof b0);
        b0.magic = UP_BANK_MAGIC; b0.rsize = sizeof(up_rec_t); b0.nslot = UP_PER_BANK;
        b0.r[2].used = UP_USED; b0.r[2].ver = UP_VER; b0.r[2].engine = ENGI_VA; b0.r[2].np = P_COUNT;
        memcpy(b0.r[2].name, "MY PAD", 6);
        for (i = 0; i < P_COUNT; i++) b0.r[2].packed[i] = (uint8_t)(64 + (int)(i % 7));
        b0.r[4] = b0.r[2];
        b0.r[4].engine = ENGI_FM6;
        memset(b0.r[4].name, 0, 12);
        memcpy(b0.r[4].name, "TINE 2", 6);
        memset(&vs, 0, sizeof vs);
        vs.magic = VA_STORE_MAGIC; vs.ver = 3; vs.nslot = UP_LEGACY; vs.blob = VA_BLOB; vs.used = 1u << 2;
        memcpy(vs.p[2], vb, VA_BLOB);
        memset(&f0, 0, sizeof f0);
        f0.magic = FM6U_MAGIC; f0.ver = FM6U_VER; f0.nslot = FM6U_HALF; f0.first = 0; f0.blob = FM6_BLOB; f0.used = 1u << 4;
        memcpy(f0.b[4], fb, FM6_BLOB);
        CHECK(sizeof b0 == 3080 && sizeof vs == 3536 && sizeof f0 == 2064 && sizeof c1 == 2320, "sounds: the object sizes of docs/SOUNDS.md");
        CHECK(t_put(9, (uint8_t *)&vs, sizeof vs) == 0, "sounds: the VA store PUT");
        CHECK(t_put(10, (uint8_t *)&f0, sizeof f0) == 0, "sounds: FM6 store half 0 PUT");
        CHECK(t_put(6, (uint8_t *)&b0, sizeof b0) == 0, "sounds: bank 0 PUT");
        CHECK(up_used(2) && up_used(4), "sounds: U03 and U05 used at once (no restart)");
        up_name(2, nm);
        CHECK(!strcmp(nm, "MY PAD"), "sounds: U03's name (%s)", nm);
        up_name(4, nm);
        CHECK(!strcmp(nm, "TINE 2"), "sounds: U05's name (%s)", nm);
        CHECK(up_rec(2)->engine == ENGI_VA && up_rec(4)->engine == ENGI_FM6, "sounds: the engines");
        CHECK(va_store_get(2, out) == 0 && !memcmp(out, vb, VA_BLOB), "sounds: U03's VA patch reads back");
        CHECK(fm6u_get(4, out) == 0 && !memcmp(out, fb, FM6_BLOB), "sounds: U05's FM6 patch reads back");
        for (k = 0, i = 0; i < UP_SLOTS; i++) k += i != 2 && i != 4 && up_used(i);
        CHECK(k == 0, "sounds: the other slots empty");
        CHECK(va_store_get(4, out) != 0 && fm6u_get(2, out) != 0, "sounds: no VA patch in U05, no FM6 patch in U03");
        /* b: rename U03 */
        memset(b0.r[2].name, 0, 12);
        memcpy(b0.r[2].name, "RENAMED", 7);
        CHECK(t_put(6, (uint8_t *)&b0, sizeof b0) == 0, "sounds: rename: bank 0 PUT");
        up_name(2, nm);
        CHECK(!strcmp(nm, "RENAMED") && up_used(2) && va_store_get(2, out) == 0 && !memcmp(out, vb, VA_BLOB),
              "sounds: rename: the new name, the same patch (%s)", nm);
        /* c: delete U03 */
        vs.used &= ~(1u << 2);
        memset(vs.p[2], 0, VA_BLOB);
        memset(&b0.r[2], 0, sizeof b0.r[2]);
        CHECK(t_put(9, (uint8_t *)&vs, sizeof vs) == 0 && t_put(6, (uint8_t *)&b0, sizeof b0) == 0, "sounds: delete: store + bank PUT");
        CHECK(!up_used(2) && va_store_get(2, out) != 0, "sounds: delete: U03 empty, its patch gone");
        up_name(4, nm);
        CHECK(up_used(4) && !strcmp(nm, "TINE 2") && fm6u_get(4, out) == 0 && !memcmp(out, fb, FM6_BLOB), "sounds: delete: U05 intact");
        /* another engine over a slot: U05 becomes a VA sound, its FM6 bit cleared */
        f0.used = 0;
        memset(f0.b[4], 0, FM6_BLOB);
        vs.used |= 1u << 4;
        memcpy(vs.p[4], vb, VA_BLOB);
        b0.r[4].engine = ENGI_VA;
        CHECK(t_put(10, (uint8_t *)&f0, sizeof f0) == 0 && t_put(9, (uint8_t *)&vs, sizeof vs) == 0 &&
              t_put(6, (uint8_t *)&b0, sizeof b0) == 0, "sounds: U05 replaced by a VA sound: PUTs");
        CHECK(up_used(4) && up_rec(4)->engine == ENGI_VA && fm6u_get(4, out) != 0 && va_store_get(4, out) == 0 &&
              !memcmp(out, vb, VA_BLOB), "sounds: U05 now VA, the old FM6 patch unreadable");
#if FELUCCA_CZ
        /* d: a CZ-1 sound in U20 (bank 1, store half 1) */
        memset(&c1, 0, sizeof c1);
        c1.magic = CZU_MAGIC; c1.ver = CZU_VER; c1.nslot = CZU_HALF; c1.first = CZU_HALF; c1.blob = CZ_BYTES; c1.used = 1u << 3;
        memcpy(c1.b[3], CZ_FACTORY[24], CZ_BYTES);
        memset(&b1, 0, sizeof b1);
        b1.magic = UP_BANK_MAGIC; b1.rsize = sizeof(up_rec_t); b1.nslot = UP_PER_BANK;
        b1.r[3] = b0.r[4];
        b1.r[3].engine = ENGI_CZ;
        memset(b1.r[3].name, 0, 12);
        memcpy(b1.r[3].name, "CZ PIANO", 8);
        CHECK(t_put(13, (uint8_t *)&c1, sizeof c1) == 0 && t_put(7, (uint8_t *)&b1, sizeof b1) == 0, "sounds: CZ-1: store half 1 + bank 1 PUT");
        up_name(19, nm);
        CHECK(up_used(19) && up_rec(19)->engine == ENGI_CZ && !strcmp(nm, "CZ PIANO") && czu_get(19, out) == 0 &&
              !memcmp(out, CZ_FACTORY[24], CZ_BYTES), "sounds: CZ-1: U20 used, its tone reads back (%s)", nm);
        CHECK(czu_get(3, out) != 0, "sounds: CZ-1: half 0 empty");
#endif
        /* d2: a QUAD sound in U07 (bank 0, the QUAD store: object 22) */
        {
            static quad_store_t q, bad_q;
            uint8_t qb[QUAD_BLOB];
            trk[0].eng_req = ENGI_QUAD;
            quad_blob_preset(&trk[0], 11);           /* (STRINGS) */
            qb_v3(qb);   /* (the legacy store: an 80-byte version-3 blob, zeros after) */
            memset(&q, 0, sizeof q);
            q.magic = QUAD_STORE_MAGIC; q.ver = QUAD_STORE_VER; q.nslot = UP_LEGACY; q.blob = QUAD_BLOB_V3; q.used = 1u << 6;
            memcpy(q.p[6], qb, QUAD_BLOB_V3);
            b0.r[6] = b0.r[4];
            b0.r[6].engine = ENGI_QUAD;
            memset(b0.r[6].name, 0, 12);
            memcpy(b0.r[6].name, "MY QUAD", 7);
            CHECK(sizeof q == 2576u && t_put(22, (uint8_t *)&q, sizeof q) == 0 && t_put(6, (uint8_t *)&b0, sizeof b0) == 0,
                  "sounds: QUAD: store (22) + bank 0 PUT");
            up_name(6, nm);
            CHECK(up_used(6) && up_rec(6)->engine == ENGI_QUAD && !strcmp(nm, "MY QUAD") && quad_store_get(6, out) == 0 &&
                  !memcmp(out, qb, QUAD_BLOB), "sounds: QUAD: U07 used, its patch reads back (%s)", nm);
            memcpy(&bad_q, &q, sizeof q);
            bad_q.p[6][1] = QUAD_VER + 1u;           /* the blob's version (1..3 are read: eng_quad.c QUAD_VER) */
            CHECK(t_put(22, (uint8_t *)&bad_q, sizeof bad_q) == 2 && t_put_begin(22, 100, 0) == 1 &&
                  quad_store_get(6, out) == 0 && !memcmp(out, qb, QUAD_BLOB), "sounds: QUAD: a bad blob / a wrong size refused, the patch kept");
            CHECK(t_list() == 0 && !find(man, nman, 22) && find(man, nman, 50)->size == ST_PAYLOAD_MAX,
                  "sounds: LIST: the QUAD store went into the pool (bank 1, id 50), not to its own object");
        }
        /* d3 (3y78): a pool bank PUT as the device manager sends one (the owner's unit, 0.22-beta1: "The FM-1 refused
         * the user sounds 011-020 (rc 2)" on a Delete): the bank as read, with an FM TONE slot migrated from 0.21 (an
         * 80-byte version-3 patch) among its slots, and a deleted slot that still holds its bytes. The check is
         * up_patch_get's: a used slot's patch header names its length (FM TONE: 96, or the older 80), an unused slot is
         * not read and is written empty (zeros, as upool_put erases one) */
        {
            static uint8_t pb[SP_PER * SP_SLOT], got[SP_PER * SP_SLOT], bad3[SP_PER * SP_SLOT], z[SP_SLOT];
            uint8_t qb2[QUAD_BLOB], lb[QUAD_BLOB];
            int16_t lp[QP_NP], wp[QP_NP];
            uint32_t erases, pos, ok = 1, off;
            qb_v3(qb2);
            for (i = 0; i < QP_NP5; i += 3u)
                qb2[2 + i] = 0;                      /* (another version-3 patch than the init: each third value its min) */
            ok &= quad_blob_ok(qb2) && quad_unpack(qb2, wp) && t_list() == 0 && find(man, nman, 50)->size == sizeof pb;
            for (off = 0; ok && off < sizeof pb; off += 256u)
                ok &= t_get(50, off, 256u, pb + off) == 0;
            ok &= pb[6 * SP_SLOT] == UP_USED && pb[6 * SP_SLOT + UPP_HDR] == ENGI_QUAD + 1u &&
                  pb[6 * SP_SLOT + UPP_HDR + 1] == QUAD_BLOB_V3 && pb[4 * SP_SLOT + UPP_HDR + 1] == VA_BLOB;
            CHECK(ok, "sounds: d3: bank 1 read: U07 FM TONE with an 80-byte patch, U05 VA");
            memset(pb + 6 * SP_SLOT + UPP_DATA, 0, SP_SLOT - UPP_DATA);
            memcpy(pb + 6 * SP_SLOT + UPP_DATA, qb2, QUAD_BLOB_V3);   /* U07: a used FM TONE slot, an 80-byte v3 patch */
            memcpy(pb + 8 * SP_SLOT, pb + 6 * SP_SLOT, SP_SLOT);
            pb[8 * SP_SLOT] = 0;                     /* U09: deleted, its record and its 16 / 80 patch header left */
            memset(pb + 9 * SP_SLOT, 0x5A, SP_SLOT); /* U10: not used, any bytes (its "header" 0x5A 0x5A) */
            memset(z, 0, sizeof z);
            CHECK(t_put(50, pb, sizeof pb) == 0, "sounds: d3: a bank with a deleted slot's stale FM TONE header and a used "
                                                 "FM TONE slot of 80 bytes: accepted (rc 0)");
            ok = up_used(6) && up_used(4) && !up_used(8) && !up_used(9) && t_list() == 0 &&
                 find(man, nman, 50)->size == sizeof got;
            for (off = 0; ok && off < sizeof got; off += 256u)
                ok &= t_get(50, off, 256u, got + off) == 0;
            CHECK(ok && !memcmp(got + 8 * SP_SLOT, z, SP_SLOT) && !memcmp(got + 9 * SP_SLOT, z, SP_SLOT) &&
                  !memcmp(got + 6 * SP_SLOT, pb + 6 * SP_SLOT, SP_SLOT) && !memcmp(got + 4 * SP_SLOT, pb + 4 * SP_SLOT, SP_SLOT),
                  "sounds: d3: U09 and U10 read back empty (384 zeros), U07 and U05 as sent");
            memset(lb, 0, sizeof lb);
            CHECK(quad_store_read && quad_store_read(6, lb) == 0 && !memcmp(lb, qb2, QUAD_BLOB) && quad_unpack(lb, lp) &&
                  !memcmp(lp, wp, sizeof lp), "sounds: d3: U07's 80-byte patch loads (the version-3 values converted)");
            pos = pool_pos_slot(ENGI_QUAD, 6);
            t_begin(76); t_b(0); t_b(ENGI_QUAD); t_b(pos);
            a = t_send(&n);
            CHECK(pos && a && n == 1 && a[0] == 0 && trk[CR_PART_CHORD].eng_req == ENGI_QUAD && psnd[0].kind == PK_USER &&
                  psnd[0].slot == 6 && !memcmp(quad_patch[CR_PART_CHORD], wp, sizeof wp),
                  "sounds: d3: SELECT chord FM TONE U07: the part plays its patch");
            erases = sp_erases + sp_progs;
            memcpy(bad3, pb, sizeof pb);
            bad3[6 * SP_SLOT + UPP_HDR + 1] = QUAD_BLOB_V3 + 1u;   /* U07 (FM TONE): 81, neither 96 nor 80 */
            ok = t_put(50, bad3, sizeof bad3) == 2;
            memcpy(bad3, pb, sizeof pb);
            bad3[4 * SP_SLOT + UPP_HDR + 1] = QUAD_BLOB_V3;        /* U05 (VA): 80, FM TONE's older length, not the VA's */
            ok &= t_put(50, bad3, sizeof bad3) == 2;
            memcpy(bad3, pb, sizeof pb);
            bad3[6 * SP_SLOT + UPP_HDR] = NENGINES + 1u;            /* U07: a patch of no engine */
            ok &= t_put(50, bad3, sizeof bad3) == 2;
            memset(lb, 0, sizeof lb);
            CHECK(ok && sp_erases + sp_progs == erases && up_used(6) && quad_store_read(6, lb) == 0 && !memcmp(lb, qb2, QUAD_BLOB),
                  "sounds: d3: a used slot's patch of a wrong length for its engine (FM TONE 81, VA 80) or of no engine: "
                  "rc 2, nothing written");
            /* (3y121: the owner's unit, after a GRAIN build: "Place DIGIT-ONE: The FM-1 refused the user sounds 031-040
             * (rc 2)": U33 a GRAIN sound with the GRAIN build's 20-byte patch.) A used slot whose patch is of an engine
             * this build keeps none for (engine 8 while GRAIN was retired; with GRAIN built, 3y133 .., PHYS (9), retired):
             * accepted, its bytes as sent, the slot used, no patch read for it; a length past the slot's room still refused */
#if FELUCCA_GRAIN
#define UPE_NONE ENGI_PHYS
#else
#define UPE_NONE 8u
#endif
            memcpy(bad3, pb, sizeof pb);
            memset(bad3 + 2 * SP_SLOT, 0, SP_SLOT);
            memcpy(bad3 + 2 * SP_SLOT, pb + 4 * SP_SLOT, UPP_HDR);      /* U03: U05's record .. */
            bad3[2 * SP_SLOT + 2] = UPE_NONE;                           /* .. of the engine without a patch here */
            bad3[2 * SP_SLOT + UPP_HDR] = UPE_NONE + 1u;
            bad3[2 * SP_SLOT + UPP_HDR + 1] = 20u;
            for (i = 0; i < 20u; i++)
                bad3[2 * SP_SLOT + UPP_DATA + i] = (uint8_t)(0x30u + i);
            ok = up_patch_len(UPE_NONE) == 0 && t_put(50, bad3, sizeof bad3) == 0 && up_used(2) && up_used(6) && t_list() == 0;
            for (off = 0; ok && off < sizeof got; off += 256u)
                ok &= t_get(50, off, 256u, got + off) == 0;
            ok &= !memcmp(got + 2 * SP_SLOT, bad3 + 2 * SP_SLOT, SP_SLOT) && !memcmp(got + 6 * SP_SLOT, pb + 6 * SP_SLOT, SP_SLOT);
            memset(lb, 0x77, sizeof lb);
            ok &= up_read_engine(2, lb, UPE_NONE) == 1 && up_read_engine(2, lb, ENGI_QUAD) == 1;
            CHECK(ok, "sounds: d3: U03 a sound of an engine with no patch here (a 20-byte patch of a retired engine, as "
                      "GRAIN's on a build without it, 3y121): accepted (rc 0), read back as sent, no patch read for it");
            bad3[2 * SP_SLOT + UPP_HDR + 1] = (uint8_t)(UPP_MAX + 1u);
            CHECK(t_put(50, bad3, sizeof bad3) == 2, "sounds: d3: .. its length past the slot's room: rc 2");
            CHECK(t_put(50, pb, sizeof pb) == 0 && !up_used(2), "sounds: d3: the bank as before (U03 empty again)");
        }
        /* e: refusals */
        memcpy(&bad_v, &vs, sizeof vs);
        bad_v.p[4][0] = 'X';                         /* U05's blob: a wrong magic byte */
        CHECK(t_put(9, (uint8_t *)&bad_v, sizeof bad_v) == 2, "sounds: a VA store with a bad blob magic: rc 2");
        (void)got;
        CHECK(va_store_get(4, out) == 0 && !memcmp(out, vb, VA_BLOB), "sounds: refused: U05's VA patch as it was");
        memcpy(&bad_b, &b0, sizeof b0);
        bad_b.rsize = 190;
        bad_b.r[2] = b0.r[4];                        /* (would add U03) */
        CHECK(t_put(6, (uint8_t *)&bad_b, sizeof bad_b) == 2 && !up_used(2) && up_used(4), "sounds: a bank of rsize 190: rc 2, slots unchanged");
        /* f: LIST: sizes and CRCs of what was written */
        CHECK(t_list() == 0, "sounds: LIST");
        {   /* the legacy objects stay unwritten: an older backup's objects went into the pool */
            st_hdr_t h;
            CHECK(st_current(OBJ_UPRESET0, &h) < 0 && st_current(OBJ_VASTORE, &h) < 0 && st_current(OBJ_FM6STORE0, &h) < 0 &&
                  st_current(OBJ_QUADSTORE, &h) < 0 && find(man, nman, 50)->size == ST_PAYLOAD_MAX &&
                  find(man, nman, 52)->size == ST_PAYLOAD_MAX, "sounds: LIST: the pool's banks; the legacy objects never written");
        }
#if FELUCCA_CZ
        {
            st_hdr_t h;
            CHECK(st_current(OBJ_UPRESET0 + 1, &h) < 0 && st_current(czu_obj(1), &h) < 0, "sounds: the CZ-1 store and bank 1: legacy, never written");
        }
#endif
    }

    /* ---- 74 SCREEN: off (one black frame, the backlight on: PA2 is never raised, it also keeps the key matrix alive,
     * hal/fm1_lcd_hw.h) until SysEx on or a key / button / knob; MIDI in does not wake it ---- */
    {
        uint32_t w, i, ink;
#define T_INK() do { for (i = 0, ink = 0; i < 240u * 240u; i++) ink += emu_hal.lcd[i] != 0; } while (0)
        fm1_ms += 20u;
        cr_ui_draw();
        T_INK();
        CHECK(!cu.screen_off && ink > 1000u, "SCREEN: on before");
        t_begin(74);
        t_b(0);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 0, "SCREEN 0: rc 0");
        w = emu_hal.lcd_writes;
        cr_ui_draw();
        T_INK();
        CHECK(cu.screen_off && !ink && emu_hal.lcd_writes == w + 1u,
              "SCREEN 0: the screen off: one write, every pixel of the panel black (%u not)", (unsigned)ink);
        CHECK(!emu_hal.pa2, "SCREEN 0: PA2 stays low (the backlight on, the key matrix alive)");
        w = emu_hal.lcd_writes;
        fm1_ms += 20u;
        cr_in_msg_n++;                               /* a MIDI channel message in */
        cu_midi_poll();
        cr_ui_draw();
        T_INK();
        CHECK(cu.screen_off && emu_hal.lcd_writes == w && !ink, "SCREEN 0: MIDI in keeps it off, nothing drawn");
        fm1_ms += 20u;
        cu_activity();                               /* a key, button or knob */
        /* MOD's background is black, so the panel's RAM (the firmware only writes it) is filled with a word no view
         * holds: a pixel the wake did not write keeps it */
        for (i = 0; i < 240u * 240u; i++) emu_hal.lcd[i] = 0xF81Fu;
        cr_ui_draw();
        for (i = 0, ink = 0; i < 240u * 240u; i++)
            ink += emu_hal.lcd[i] == 0xF81Fu;
        CHECK(!cu.screen_off && emu_hal.lcd_writes == w + CR_NSTRIP && cr_dc.blits == CR_NSTRIP &&
              cr_dc.drawn == CR_NSTRIP && cr_dc.bytes == 240u * 240u * 2u && !ink,
              "SCREEN 0, then a knob: on, every strip redrawn whole (%u blits, %u bytes, %u pixels left unwritten)",
              (unsigned)cr_dc.blits, (unsigned)cr_dc.bytes, (unsigned)ink);
        t_begin(74);
        t_b(0);
        a = t_send(&n);
        fm1_ms += 20u;
        cr_ui_draw();
        T_INK();
        CHECK(cu.screen_off && !ink && !emu_hal.pa2, "SCREEN 0 again: off, black");
        t_begin(74);
        t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 0, "SCREEN 1: rc 0");
        fm1_ms += 20u;
        cr_ui_draw();
        T_INK();
        CHECK(!cu.screen_off && ink > 1000u && cr_dc.blits == CR_NSTRIP, "SCREEN 1: on, the view redrawn");
#undef T_INK
        t_begin(74);
        a = t_send(&n);
        CHECK(!a, "SCREEN without its byte: no reply");
        t_begin(74);
        t_b(2);
        a = t_send(&n);
        CHECK(!a, "SCREEN 2: no reply");
    }

    /* ---- SELECT (76) and the reload after a PUT ---- */
    {
        static up_bank_t sb;
        static fm6u_t sf;
        uint8_t fa[FM6_BLOB], fb2[FM6_BLOB];
        uint32_t pos, crc0, crc1, rl;
        memset(emu_flash, 0xFF, sizeof emu_flash);
        power_on();
        trk[CR_PART_BASS].eng_req = ENGI_FM6;
        fm6_load_slot(CR_PART_BASS, 3);
        fm6_blob_get(&trk[CR_PART_BASS], fa);
        fm6_load_slot(CR_PART_BASS, 6);
        fm6_blob_get(&trk[CR_PART_BASS], fb2);
        CHECK(memcmp(fa, fb2, FM6_BLOB) != 0, "select: two different FM6 blobs");
        memset(&sb, 0, sizeof sb);
        sb.magic = UP_BANK_MAGIC; sb.rsize = sizeof(up_rec_t); sb.nslot = UP_PER_BANK;
        sb.r[4].used = UP_USED; sb.r[4].ver = UP_VER; sb.r[4].engine = ENGI_FM6; sb.r[4].np = P_COUNT;
        memcpy(sb.r[4].name, "DX UP", 5);
        for (i = 0; i < P_COUNT; i++) sb.r[4].packed[i] = (uint8_t)(64 + (int)(i % 5));
        memset(&sf, 0, sizeof sf);
        sf.magic = FM6U_MAGIC; sf.ver = FM6U_VER; sf.nslot = FM6U_HALF; sf.first = 0; sf.blob = FM6_BLOB; sf.used = 1u << 4;
        memcpy(sf.b[4], fa, FM6_BLOB);
        CHECK(t_put(10, (uint8_t *)&sf, sizeof sf) == 0 && t_put(6, (uint8_t *)&sb, sizeof sb) == 0, "select: U05 (FM6) imported");
        pos = pool_pos_slot(ENGI_FM6, 4);
        CHECK(pos > 0u && pos < 128u, "select: U05's pool position %u", (unsigned)pos);
        t_begin(76); t_b(1); t_b(ENGI_FM6); t_b(pos);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 0 && trk[CR_PART_BASS].eng_req == ENGI_FM6 && psnd[1].kind == PK_USER && psnd[1].slot == 4,
              "SELECT bass FM6 U05: rc 0, the part plays slot 5");
        crc0 = st_crc32(fm6_patch[CR_PART_BASS], sizeof fm6_patch[0]);
        t_begin(76); t_b(0); t_b(ENGI_VA); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 0 && trk[CR_PART_CHORD].eng_req == ENGI_VA && psnd[0].kind == PK_FACTORY && psnd[0].fk == 0,
              "SELECT chord VA position 1: its first factory preset");
        t_begin(76); t_b(0); t_b(NENGINES + 3u); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 1, "SELECT a bad engine: rc 1");
        t_begin(76); t_b(2); t_b(ENGI_VA); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 1, "SELECT a bad part: rc 1");
        t_begin(76); t_b(0); t_b(ENGI_VA); t_b(127); t_b(3);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 2 && trk[CR_PART_CHORD].eng_req == ENGI_VA && psnd[0].fk == 0, "SELECT a bad position: rc 2, nothing changed");
        cu.page = PG_EDIT;
        t_begin(76); t_b(0); t_b(ENGI_VA); t_b(2);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 3 && psnd[0].fk == 0, "SELECT while EDIT is open: rc 3 (busy)");
        cu.page = PG_NONE;
        t_begin(76); t_b(0); t_b(ENGI_VA);
        a = t_send(&n);
        CHECK(!a, "SELECT without its position: no reply");
        /* the reload: a new patch PUT into the slot the bass plays is heard at once */
        rl = crb.reloads;
        memcpy(sf.b[4], fb2, FM6_BLOB);
        CHECK(t_put(10, (uint8_t *)&sf, sizeof sf) == 0, "reload: FM6 store half 0 PUT (a new patch in U05)");
        crc1 = st_crc32(fm6_patch[CR_PART_BASS], sizeof fm6_patch[0]);
        CHECK(crb.reloads == rl + 1u && crc1 != crc0 && psnd[1].kind == PK_USER && psnd[1].slot == 4,
              "reload: the bass part reloaded U05, its patch CRC changed (%08x -> %08x)", (unsigned)crc0, (unsigned)crc1);
        rl = crb.reloads;
        CHECK(t_put(11, (uint8_t *)&sf, 0) == 0 && crb.reloads == rl, "reload: the other half PUT: no reload");
        rl = crb.reloads;
        CHECK(t_put(7, 0, 0) == 0 && crb.reloads == rl, "reload: bank 1 PUT (slots 17..32): no reload");
        CHECK(t_put(6, (uint8_t *)&sb, sizeof sb) == 0 && crb.reloads == rl + 1u, "reload: bank 0 PUT: the bass reloads");
    }

    /* ---- a 0.17 backup with sounds on the engines retired in 0.18 (PHASE 2, LOFI 3, VOICE 5, TRIO 6, NOISE 11, PHYS 9): the
     * bank and the settings restore without error; after the restart the chord part (parked on NOISE) plays FM6's
     * default, the bass (on LOFI) is the VA's DEEP SUB behind BASS, as on a fresh unit; the records stay in their slots,
     * added presets of the VA's pool, and load as the VA's init sound (a stand-in) ---- */
    {
#define NRET 6u
        static const uint8_t ENG[NRET] = {2, 3, 5, 6, 11, ENGI_PHYS};
        static const char *const NM[NRET] = {"PD BRASS", "CHIP LEAD", "AAH", "TRIO PAD", "WIND", "MY HARP"};
        static const char *const WAS[NRET] = {"PHASE", "LOFI", "VOICE", "TRIO", "NOISE", "PHYS"};
        static up_bank_t ob;
        static persist_t pv;
        uint32_t q, nva, good = 0;
        int rc;
        char nm[13], want[40];
        memset(emu_flash, 0xFF, sizeof emu_flash);   /* a fresh device on 0.18 */
        power_on();
        cr_ui_init();
        nva = pool_count(ENGI_VA);
        memset(&ob, 0, sizeof ob);
        ob.magic = UP_BANK_MAGIC; ob.rsize = sizeof(up_rec_t); ob.nslot = UP_PER_BANK;
        for (q = 0; q < NRET; q++) {                 /* U02..U07: one sound per retired engine, as 0.17's SAVE wrote it */
            up_rec_t *r = &ob.r[1u + q];
            r->used = UP_USED; r->ver = UP_VER; r->engine = ENG[q]; r->np = P_COUNT;
            memcpy(r->name, NM[q], strlen(NM[q]));
            for (i = 0; i < P_COUNT; i++) r->packed[i] = (uint8_t)(64 + (int)(i % 5));
            r->note[15] = CB_BIND_MARK;
            r->flags[15] = (uint8_t)(q == 0u ? 1u : q == 5u ? 9u : 0u);   /* (PD BRASS: over PHASE 01 BRASS then, MY HARP
                                                                           * over PHYS 09 HARP; the others added) */
        }
        crs_capture(&crs_rec.cr);                    /* the settings as this unit has them, then the two parts' sounds */
        memcpy(&pv, &crs_rec, sizeof pv);
        CHECK(pv.magic == PERSIST_MAGIC && pv.cr.version == CRS_VERSION, "0.17 backup: a settings record to start from");
        pv.cr.chord_sound = (uint16_t)(11u << 8 | 2u);   /* the chord part on NOISE 02 */
        pv.cr.bass_sound = (uint16_t)(3u << 8 | 1u);     /* the bass on LOFI 01 */
        crs_pool_set(&pv.cr, 0, 3, 4);                   /* slot 3 (PHASE's): its place then, kept as bytes */
        crs_pool_set(&pv.cr, 0, 10, 2);                  /* slot 10 (NOISE's) */
        cr_settings_seal(&pv.cr);
        rc = t_put(6, (uint8_t *)&ob, sizeof ob);
        CHECK(rc == 0, "0.17 backup: the bank with PHASE LOFI VOICE TRIO NOISE PHYS records restores (rc %d)", rc);
        rc = t_put(1, (const uint8_t *)&pv, sizeof pv);
        CHECK(rc == 0 && cr_restore_lock, "0.17 backup: the settings with the chord on NOISE, the bass on LOFI restore (rc %d)", rc);
        cr_restore_lock = 0;                         /* the restart */
        power_on();
        cr_ui_init();
        CHECK(trk[CR_PART_CHORD].eng_req == ENGI_FM6 && !strcmp(psnd[0].name, "TINE EP") && psnd[0].kind == PK_FACTORY,
              "0.17 backup: after the restart the chord part plays FM6 TINE EP (%u %s)", (unsigned)trk[CR_PART_CHORD].eng_req, psnd[0].name);
        CHECK(trk[CR_PART_BASS].eng_req == ENGI_VA && !strcmp(psnd[1].name, "DEEP SUB") && !cs.bass_on,
              "0.17 backup: the bass is the VA's DEEP SUB, off until BASS (%u %s)", (unsigned)trk[CR_PART_BASS].eng_req, psnd[1].name);
        CHECK(crs_pool_get(&crs_rec.cr, 0, 3) == 4 && crs_pool_get(&crs_rec.cr, 0, 10) == 2,
              "0.17 backup: the retired engines' pool_pos slots keep their bytes");
        CHECK(pool_count(ENGI_VA) == nva + NRET, "0.17 backup: six added presets in the VA's pool (%u -> %u)", (unsigned)nva, (unsigned)pool_count(ENGI_VA));
        for (q = 0; q < NRET; q++) {
            uint32_t pos = pool_pos_slot(ENGI_VA, 1u + q);
            pool_name(ENGI_VA, pos, nm);
            cu_pool_load(0, ENGI_VA, pos);
            good += up_used(1u + q) && up_rec(1u + q)->engine == ENG[q] && pos == nva + q && !strcmp(nm, NM[q]) &&
                    cb_bound(1u + q) == PF_NONE && trk[CR_PART_CHORD].eng_req == ENGI_VA && !strcmp(psnd[0].name, NM[q]) &&
                    psnd[0].kind == PK_USER && psnd[0].slot == 1u + q && trk[CR_PART_CHORD].p[P_E0] == ENG_VA.edit[0].def &&
                    (snprintf(want, sizeof want, "%s retired: stand-in", WAS[q]), !strcmp(cu.msg.text, want));
        }
        CHECK(good == NRET, "0.17 backup: each record in its slot under its name, the VA's init sound when picked, \"<ENGINE> retired: stand-in\" (%u of 6; last message \"%s\")", (unsigned)good, cu.msg.text);
        {
            uint8_t raw[sizeof(up_rec_t)];
            int same = 1;
            for (i = 0; i < UP_PER_BANK; i++)
                same &= sp_read(i, 0, raw, sizeof raw) == 0 && !memcmp(raw, &ob.r[i], sizeof raw);
            CHECK(same, "0.17 backup: the bank's records in slots 1..16 byte for byte (nothing rewritten, nothing deleted)");
        }
        t_begin(76); t_b(0); t_b(2); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 1, "SELECT a retired engine (PHASE): rc 1");
        t_begin(76); t_b(1); t_b(ENGI_PHYS); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 1 && trk[CR_PART_BASS].eng_req == ENGI_VA, "SELECT PHYS (retired): rc 1, the bass part as it was");
        /* .. and the settings of a unit left with both parts on PHYS (the chord on PHYS 03 PLUCK, the bass on, on PHYS
         * 02 MARIMBA; PHYS's pool_pos slot 9 at 5): the defaults after the restart, the slot's bytes kept */
        pv.cr.chord_sound = (uint16_t)(ENGI_PHYS << 8 | 3u);
        pv.cr.bass_sound = (uint16_t)(ENGI_PHYS << 8 | 2u);
        crs_pool_set(&pv.cr, 0, 9, 5);
        crs_pool_set(&pv.cr, 1, 9, 2);
        cr_settings_seal(&pv.cr);
        rc = t_put(1, (const uint8_t *)&pv, sizeof pv);
        CHECK(rc == 0 && cr_restore_lock, "0.17 backup: the settings with both parts on PHYS restore (rc %d)", rc);
        cr_restore_lock = 0;
        power_on();
        cr_ui_init();
        CHECK(trk[CR_PART_CHORD].eng_req == ENGI_FM6 && !strcmp(psnd[0].name, "TINE EP") && psnd[0].kind == PK_FACTORY &&
              trk[CR_PART_BASS].eng_req == ENGI_VA && !strcmp(psnd[1].name, "DEEP SUB") &&
              crs_pool_get(&crs_rec.cr, 0, 9) == 5 && crs_pool_get(&crs_rec.cr, 1, 9) == 2,
              "0.17 backup, both parts on PHYS: FM6 TINE EP and the VA's DEEP SUB after the restart; PHYS's pool_pos slot keeps its bytes (%u %s, %u %s)",
              (unsigned)trk[CR_PART_CHORD].eng_req, psnd[0].name, (unsigned)trk[CR_PART_BASS].eng_req, psnd[1].name);
        {   /* the session record of that unit (not in a backup: it stays in flash across the update): a part's sound
             * on PHYS is refused (0: the part keeps what the settings' chord_sound / bass_sound gave it, the default
             * here), one on an engine that is still there is taken */
            static css_part_t sp;
            int took;
            css_part_get(0, &sp);                    /* (a valid part record of this firmware: FM6 TINE EP) */
            sp.eng = ENGI_PHYS; sp.kind = PK_FACTORY; sp.fk = 2; sp.nblob = 0;
            took = css_part_put(0, &sp);
            CHECK(!took && trk[CR_PART_CHORD].eng_req == ENGI_FM6 && !strcmp(psnd[0].name, "TINE EP"),
                  "session: a chord part left on PHYS is refused: FM6 TINE EP stays (%d, %u %s)", took, (unsigned)trk[CR_PART_CHORD].eng_req, psnd[0].name);
            css_part_get(1, &sp);
            sp.eng = ENGI_PHYS; sp.kind = PK_USER; sp.slot = 6; sp.user = 7; sp.nblob = 0;   /* (the user sound MY HARP) */
            took = css_part_put(1, &sp);
            CHECK(!took && trk[CR_PART_BASS].eng_req == ENGI_VA && !strcmp(psnd[1].name, "DEEP SUB"),
                  "session: a bass part left on a PHYS user sound is refused: the VA's DEEP SUB stays (%d)", took);
            css_part_get(0, &sp);
            CHECK(css_part_put(0, &sp) == 1, "session: a part on FM6 is taken as before");
        }
#undef NRET
    }

    /* ---- 0.25.1: FM-1 is the default Chord Keys, and a restored older settings object moves to it once (cr_settings.h
     * CRS_KEYS_MOVED). A 0.25 backup with Classic, LATCH, the SUS Window at 60 ms and LEDs Stock (leds 1: no mark):
     * after the restart FM-1, the rest as the backup had it. A 0.25.1 backup with Classic chosen (the mark): Classic, and
     * Classic again after a power cycle ---- */
    {
        static persist_t pv;
        int rc;
        memset(emu_flash, 0xFF, sizeof emu_flash);   /* a fresh device: the defaults */
        power_on();
        cr_ui_init();
        CHECK(cs.layout == 1u && (crs_rec.cr.leds & CRS_KEYS_MOVED) && !cs.leds,
              "a fresh unit: Chord Keys FM-1, the record marked moved, LEDs Glow (%u %02X)", (unsigned)cs.layout, (unsigned)crs_rec.cr.leds);
        crs_capture(&crs_rec.cr);
        memcpy(&pv, &crs_rec, sizeof pv);
        pv.cr.sticky = CRS_STICKY_BYTE(1, 0, 0, 60);   /* as 0.25 wrote it: Classic, LATCH, 60 ms */
        pv.cr.leds = CRS_LEDS_STOCK;                    /* (0.25's byte: LEDs Stock, no mark) */
        cr_settings_seal(&pv.cr);
        rc = t_put(1, (const uint8_t *)&pv, sizeof pv);
        CHECK(rc == 0 && cr_restore_lock, "0.25 backup with Classic: the settings restore (rc %d)", rc);
        cr_restore_lock = 0;
        power_on();
        cr_ui_init();
        CHECK(cs.layout == 1u && cs.sticky == 1u && cs.suswin == 60u && cs.leds == 1u && (crs_rec.cr.leds & CRS_KEYS_MOVED),
              "0.25 backup with Classic: after the restart FM-1, LATCH, 60 ms and LEDs Stock kept, the record marked (%u %u %u %u)",
              (unsigned)cs.layout, (unsigned)cs.sticky, (unsigned)cs.suswin, (unsigned)cs.leds);
        pv.cr.leds = CRS_LEDS_STOCK | CRS_KEYS_MOVED;   /* a 0.25.1 backup: Classic chosen after the move */
        cr_settings_seal(&pv.cr);
        rc = t_put(1, (const uint8_t *)&pv, sizeof pv);
        cr_restore_lock = 0;
        power_on();
        cr_ui_init();
        CHECK(rc == 0 && cs.layout == 0u && cs.sticky == 1u && cs.leds == 1u,
              "0.25.1 backup with Classic chosen: Classic after the restart (rc %d, %u)", rc, (unsigned)cs.layout);
        power_on();
        cr_ui_init();
        CHECK(cs.layout == 0u, "0.25.1 backup with Classic chosen: Classic after a second power cycle (the move never runs twice)");
    }

    /* ---- DEBUG (73): version 1, a count, count x u32; the stub's key samples are its last four values ---- */
    t_begin(73);
    a = t_send(&n);
    CHECK(a && n >= 2 + 5 * 4 && a[0] == 1 && n == 2u + 5u * a[1], "DEBUG: 1, count, count x u32 (%u values)", a ? a[1] : 0);
    if (a && n >= 22) {
        uint32_t k, z = 0;
        for (k = n - 20; k < n; k++)
            z |= a[k];
        CHECK(z == 0, "DEBUG: the tail (the stub's samples) is zero where no stub ran");
    }

    /* ---- PEAK (78, 0.18): the audio ISR's worst cases since the last read: version 1, 79 values (24 to its second
     * build; the six behind them: when the ISR got to start; then four: the keys' way to the engine; then seven, 3y44:
     * what the overload shed did, the voices faded and taken; then four, 3y46: P5 on two cores; then eight, 3y48: the
     * erases' writers and times; then seven, 3y50: each core's half; then four, 3y52: the quiet writes; then four,
     * 3y60: the UI frame and the flash reads; then eleven, the dual-core fallback's branch (its 35..45, merged by
     * name): c1_bar, why this boot kept core 1 off, the last crash's record, the reset, the breadcrumb, the voices
     * shed since power-on); DEBUG keeps its 127 ---- */
    t_begin(78);
    a = t_send(&n);
    CHECK(a && a[0] == 1 && a[1] == 79 && n == 2u + 5u * 79u, "PEAK: 1, 79, 79 x u32 (n %u)", (unsigned)n);
    t_begin(73);
    a = t_send(&n);
    CHECK(a && a[1] <= 127 && n == 2u + 5u * a[1], "DEBUG after PEAK: its own values, as before (%u; the device's 127 fill its count byte)", a ? a[1] : 0);

    /* ---- STUB (77): a request to the boot stub and a restart ---- */
    CHECK(CRB_STUB_REQ_TEST == STUB_REQ_TEST && CRB_STUB_REQ_RESCUE == STUB_REQ_RESCUE &&
          stub_rescues(CRB_STUB_REQ_TEST) == 0 && stub_rescues(CRB_STUB_REQ_RESCUE) == 0,
          "STUB: the request words are stub_core.h's, and neither reads as a rescue count");
    crb.reboot = crb.stub = 0;
    for (t_stub_ver = 0; t_stub_ver < 2u; t_stub_ver++) {
        t_begin(77); t_b(1);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 127 && !crb.reboot && !crb.stub, "STUB with stub version %u: 127 (unsupported), no restart", t_stub_ver);
    }
    t_stub_ver = 2;
    t_begin(77); t_b(0);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 1 && !crb.reboot, "STUB 0: rc 1 (a bad op)");
    t_begin(77); t_b(3);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 1 && !crb.reboot, "STUB 3: rc 1");
    t_begin(77);
    a = t_send(&n);
    CHECK(!a, "STUB without its op: no reply");
    cu.page = PG_EDIT;
    t_begin(77); t_b(2);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 3 && !crb.reboot && !crb.stub, "STUB while EDIT is open: rc 3 (busy)");
    cu.page = PG_SAVE;
    t_begin(77); t_b(1);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 3 && !crb.reboot, "STUB while SAVE is open: rc 3");
    cu.page = PG_NONE;
    {
        uint8_t keep = cr_snap.lcap;
        cr_snap.lcap = (uint8_t)(CRL_CAP_NONE + 1);
        t_begin(77); t_b(2);
        a = t_send(&n);
        CHECK(a && n == 1 && a[0] == 3 && !crb.reboot, "STUB while a loop is being recorded: rc 3");
        cr_snap.lcap = keep;
    }
    t_begin(77); t_b(1);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot && crb.stub == STUB_REQ_TEST, "STUB 1: rc 0, the restart scheduled with the test request");
    crb.reboot = crb.stub = 0;
    t_begin(77); t_b(2);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot && crb.stub == STUB_REQ_RESCUE, "STUB 2: rc 0, the rescue request");
    CHECK(t_stub_went == 0, "STUB: nothing restarts before the reply has left (ed_service)");

    /* ---- RESTART ---- */
    crb.reboot = 0;
    t_begin(72);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot && !crb.stub, "RESTART: rc 0, the restart scheduled (a plain one: no request left)");

    /* ---- the update gesture: OCT- + OCT+ + HOME held 5 s (cr_update_poll; main.c's loop calls it every pass) ---- */
    {
        uint32_t oct = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]), home = 1u << panel.btn[B_HOME];
        uint32_t t, saw = 0, keep_lock = cr_restore_lock, keep_safe = cr_safe;
#define T_HOLD(mask, ms)                                                                  \
        for (t = 0; t < (ms); t += 10u) {                                                 \
            fm1_in.buttons = (mask);                                                      \
            fm1_ms += 10u;                                                                \
            cr_update_poll();                                                             \
            if (cu.msg.big && (int32_t)(cu.msg.until - fm1_ms) > 0 && cu.msg.text[0] >= '1' && cu.msg.text[0] <= '3' && !cu.msg.text[1]) \
                saw |= 1u << (cu.msg.text[0] - '0');                                      \
        }
        cr_restore_lock = 1;                         /* (no flash write of this test's own on the way out) */
        crb.reboot = crb.stub = 0;
        t_stub_went = 0; t_uboot_went = 0; t_upd_screen = 0;
        fm1_in.buttons = 0;
        cr_update_poll();
        /* two held, the gesture until 0.17: nothing, however long */
        t_stub_ver = 3;
        cu.msg.until = 0;
        T_HOLD(oct, 8000u);
        CHECK(!t_stub_went && !t_uboot_went && !saw && !ui.uboot && !t_upd_screen,
              "gesture: OCT- + OCT+ alone held 8 s: no countdown, no restart (PANIC is the UI's, at the press: tools/emu/test_cr.sh)");
        T_HOLD(oct | home, 1900u);                   /* (HOME joins late: the 5 s count from all three) */
        CHECK(!saw && !ui.uboot, "gesture: the three held 1.9 s: nothing shown yet");
        T_HOLD(oct | home, 2000u);                   /* 3.9 s */
        CHECK(saw == ((1u << 3) | (1u << 2)) && ui.uboot == 2u && !t_stub_went && !strcmp(cu.msg.label, "update mode: let go to cancel"),
              "gesture: from 2 s the countdown 3, 2 over the screen (\"update mode: let go to cancel\"), saw %x", saw);
        cu.msg.until = 0;
        T_HOLD(oct, 30u);                            /* HOME let go at 3.9 s */
        CHECK(!t_stub_went && !t_uboot_went && !ui.uboot && !strcmp(cu.msg.text, "update cancelled"),
              "gesture: released at 3.9 s: cancelled (\"%s\"), no restart", cu.msg.text);
        T_HOLD(oct, 6000u);
        CHECK(!t_stub_went && !t_uboot_went, "gesture: the two still held after a cancel: nothing");
        /* the three, 5 s: the stub's rescue request, once */
        saw = 0;
        T_HOLD(0u, 20u);
        T_HOLD(oct | home, 4990u);
        CHECK(!t_stub_went && saw == 14u, "gesture: at 4.99 s still counting (3, 2, 1 shown and nothing else: %x)", saw);
        T_HOLD(oct | home, 20u);
        CHECK(t_stub_went == STUB_REQ_RESCUE && !t_uboot_went && t_upd_screen && !strcmp(t_upd_screen, "UPDATE MODE") && !ui.uboot,
              "gesture: the three held 5 s: the boot stub's RESCUE request (%08X), the screen says UPDATE MODE", t_stub_went);
        CHECK(!crb.reboot && !crb.stub, "gesture: not through the SysEx restart (no reply to wait for)");
        t_stub_went = 0;
        T_HOLD(oct | home, 9000u);
        CHECK(!t_stub_went && !t_uboot_went, "gesture: asked once (a hook that returns: nothing more while the keys stay down)");
        /* any one of the three let go cancels, whichever */
        for (i = 0; i < 3u; i++) {
            uint32_t one = i == 0 ? 1u << panel.btn[B_OCTDN] : i == 1u ? 1u << panel.btn[B_OCTUP] : home;
            T_HOLD(0u, 20u);
            T_HOLD(oct | home, 4900u);
            T_HOLD((oct | home) & ~one, 20u);
            T_HOLD(oct | home, 4900u);               /* pressed again: the 5 s start over */
            CHECK(!t_stub_went && !t_uboot_went, "gesture: button %u of the three let go at 4.9 s: cancelled, and the count starts over", (unsigned)i);
        }
        /* no stub ran before this boot (version 0, and 1: a stub that takes no request): the ROM's USB boot, said so */
        for (t_stub_ver = 0; t_stub_ver < 2u; t_stub_ver++) {
            t_upd_screen = 0;
            T_HOLD(0u, 20u);
            T_HOLD(oct | home, 3000u);
            CHECK(!strcmp(cu.msg.label, "USB boot: let go to cancel"), "gesture, stub version %u: the countdown says USB boot", t_stub_ver);
            T_HOLD(oct | home, 2100u);
            CHECK(!t_stub_went && t_uboot_went == t_stub_ver + 1u && t_upd_screen && !strcmp(t_upd_screen, "USB BOOT"),
                  "gesture, stub version %u: the ROM's USB boot (fm1_enter_uboot), the screen says USB BOOT", t_stub_ver);
        }
        /* SAFE MODE: the same */
        t_stub_ver = 3;
        cr_safe = 1;
        T_HOLD(0u, 20u);
        T_HOLD(oct | home, 5020u);
        CHECK(t_stub_went == STUB_REQ_RESCUE, "gesture in SAFE MODE: the same request");
        cr_safe = (uint8_t)keep_safe;
        t_stub_went = 0;
        fm1_in.buttons = 0;
        cr_update_poll();
        cr_restore_lock = (uint8_t)keep_lock;
#undef T_HOLD
    }

    /* ---- the favourites (0.22, cr_favs.c; docs/PRESETS.md "Favourites"): object 23 ---- */
    {
        uint8_t fav[CRF_PACK_MAX], other[CRF_PACK_MAX];
        uint32_t fn, tag, id, keep_lock = cr_restore_lock;
        int rc;
        cr_restore_lock = 0;
        cu_pool_load(0, ENGI_FM6, 2);                /* FM BELL */
        CHECK(cu_fav_part_key(0, &tag, &id) && tag == ENGI_FM6 && id == 1u && !cu_fav_part(0) && cu_fav_turn(0, +1) &&
              cu_fav_part(0) && crf_dirty, "favourites: FM6 02 marked through the UI's turn: a mark to be written");
        cu_pool_load(0, ENGI_VA, 0);                 /* INIT */
        cu.msg.text[0] = 0;
        CHECK(!cu_fav_turn(0, +1) && !strcmp(cu.msg.text, "INIT: cannot be marked") && crf_count() == 1u,
              "favourites: INIT cannot be marked (a message), nothing changes");
        {   /* 3y48: a READ never writes flash (LIST, GET, 73 DEBUG, 78 PEAK, 79 USB, INFO), a mark pending or not */
            uint32_t e0 = st_erases, s0 = emu_stalls, cmd[5] = {1, 73, 78, 79, 65}, j, n;
            for (j = 0; j < 5u; j++) {
                t_begin(cmd[j]);
                (void)t_send(&n);
            }
            CHECK(st_erases == e0 && emu_stalls == s0 && crf_dirty,
                  "reads (3y48): INFO, DEBUG, PEAK, USB, LIST with a mark pending: no erase, the mark still to be written");
        }
        CHECK(t_list() == 0 && crf_dirty && find(man, nman, 23)->size == 2u + 2u + 1u,
              "favourites: LIST lists the pending marks from RAM (3y48: nothing written; 5 bytes: version, 1 section, FM6: 1 byte)");
        rc = t_get(23, 0, 5, fav);
        CHECK(!rc && fav[0] == CRF_VER && fav[1] == 1u && fav[2] == ENGI_FM6 && fav[3] == 1u && fav[4] == 0x02u &&
              st_crc32(fav, 5) == find(man, nman, 23)->crc && crf_dirty,
              "favourites: GET 23: the object as cr_favs.c packs it (from RAM, still not written)");
        CHECK(crf_flush() == 0 && !crf_dirty && t_list() == 0 && find(man, nman, 23)->size == 5u &&
              t_get(23, 0, 5, other) == 0 && !memcmp(fav, other, 5), "favourites: once written, LIST / GET from flash: the same bytes");
        fn = 5;
        (void)crf_set(ENGI_FM6, 1, 0);
        (void)crf_set(13, 7, 1);
        CHECK(crf_flush() == 0 && !crf_has(ENGI_FM6, 1), "(the marks changed on the unit: FM6 02 off, VA 08 on)");
        CHECK(t_put(23, fav, fn) == 0 && crf_has(ENGI_FM6, 1) && !crf_has(13, 7) && crf_count() == 1u && !crf_dirty,
              "favourites: PUT 23: the backup's marks replace the unit's, in RAM at once");
        power_on();
        CHECK(crf_has(ENGI_FM6, 1) && crf_count() == 1u && crf_last_rc == 1, "favourites: .. and after a power cycle");
        cu_pool_load(0, ENGI_FM6, 2);
        cu_pool_popup(0, 2);
        CHECK(cu.pop.star == 1 && !cu.pop.mark, "favourites: the pool read-out of FM6 02 carries the star");
        cu_pool_popup(0, 3);
        CHECK(cu.pop.star == 0, "favourites: FM6 03's does not");
        memcpy(other, fav, fn);
        other[0] = CRF_VER + 1u;                     /* a newer firmware's object */
        CHECK(t_put(23, other, fn) == 0 && crf_count() == 0u && crf_last_rc == -1,
              "favourites: PUT of another version: stored, read as no marks (never half-understood)");
        CHECK(t_put_begin(23, 1, 0) == 1 && t_put_begin(23, CRF_PACK_MAX + 1u, 0) == 1, "favourites: a size of 1 or over 264: rc 1");
        CHECK(t_put(23, fav, 0) == 0 && crf_count() == 0u && crf_last_rc == 0, "favourites: PUT of size 0: no marks");
        CHECK(t_put(23, fav, fn) == 0 && crf_count() == 1u, "(the backup's marks again)");
        {   /* 3y48 (the owner's arp on P5): crs_sounding sees each kind of sound, and a mark pending through a minute of
             * it is never written by the poll; once all is quiet for CRS_IDLE_MS it is, and erases_auto_sounding stays 0 */
            uint32_t e0, t, k, quiet = 1;
            static const char *what[4] = {"a P5 chord voice in core 1's slots", "a held voice of the P5 bass", "an arp between its steps (no voice, nothing queued)", "a queued note"};
            memset(scope_buf, 0, sizeof scope_buf);
            for (k = 0; k < 2u; k++)
                for (t = 0; t < NVOICE; t++)
                    quiet &= !trk[k].v[t].active;
            cr_snap.voices = cr_snap.pending = 0;
            cr_snap.perf_step = 0;
            CHECK(quiet && !crs_sounding(), "sounding (3y48): nothing sounds: no");
            for (k = 0; k < 4u; k++) {
                if (k == 0u) trk[CR_PART_CHORD].v[P5_POLY - 1u].active = 1;
                if (k == 1u) trk[CR_PART_BASS].v[0].active = 1;
                if (k == 2u) cr_snap.perf_step = 1;
                if (k == 3u) cr_snap.pending = 1;
                CHECK(crs_sounding(), "sounding (3y48): %s: yes", what[k]);
                trk[CR_PART_CHORD].v[P5_POLY - 1u].active = 0;
                trk[CR_PART_BASS].v[0].active = 0;
                cr_snap.perf_step = 0;
                cr_snap.pending = 0;
            }
            (void)crf_set(ENGI_FM6, 2, 1);
            e0 = st_erases;
            trk[CR_PART_BASS].v[0].active = 1;   /* the arp's chord through a P5 bass, a minute of it */
            for (t = 0; t < 60000u; t += 20u) {
                fm1_ms += 20u;
                cr_settings_poll();
            }
            CHECK(st_erases == e0 && crf_dirty, "sounding (3y48): a mark pending through a minute of sound: not written");
            trk[CR_PART_BASS].v[0].active = 0;
            for (t = 0; t < 6000u && crf_dirty; t += 20u) {   /* (3y52: the spare not blank: the 4 s rule) */
                fm1_ms += 20u;
                cr_settings_poll();
            }
            CHECK(!crf_dirty && st_erases == e0 + 1u && st_last_why == ST_WHY_AUTO_FAV && st_last_obj == OBJ_FAVS &&
                  st_auto_loud == 0u && t >= CRS_IDLE_MS, "sounding (3y48): quiet: written by the poll after %u ms (last writer: the favourites' poll, object 23; erases_auto_sounding 0)", (unsigned)t);
            (void)crf_set(ENGI_FM6, 2, 0);
            CHECK(crf_flush() == 0 && crf_count() == 1u, "(the extra mark off again)");
        }
        /* an overwritten factory preset keeps its factory key: the mark stays through Overwrite and Reset */
        {
            uint32_t k = cb_free_slot();
            cu_pool_load(0, ENGI_FM6, 2);
            rc = cb_store(&trk[CR_PART_CHORD], 0, k, "FM BELL", 1u + 1u);
            cu_pool_load(0, ENGI_FM6, 2);
            CHECK((rc == 0 || rc == 3) && psnd[0].kind == PK_USER && psnd[0].fk == 1u && cu_fav_part(0) &&
                  cu_fav_ent(ENGI_FM6, pool_entry(ENGI_FM6, 2)),
                  "favourites: FM6 02 overwritten by a user record: the same sound of the pool, still marked");
            (void)up_put(k, 0);
            cu_pool_load(0, ENGI_FM6, 2);
            CHECK(psnd[0].kind == PK_FACTORY && cu_fav_part(0), "favourites: .. and after Reset to factory");
            /* an added user sound: its slot; the slot erased: the mark goes with it */
            k = cb_free_slot();
            rc = cb_store(&trk[CR_PART_CHORD], 0, k, "MINE", 0);
            cu_pool_load(0, ENGI_FM6, pool_pos_slot(ENGI_FM6, k));
            CHECK((rc == 0 || rc == 3) && psnd[0].kind == PK_USER && cu_fav_part_key(0, &tag, &id) && tag == CRF_T_USER &&
                  id == k && cu_fav_turn(0, +1) && crf_has(CRF_T_USER, k), "favourites: an added user sound is marked by its slot");
            psnd[0].edited = 1;
            CHECK(cu_fav_part(0), "favourites: an edited sound is still its origin's mark");
            (void)up_put(k, 0);
            CHECK(!crf_has(CRF_T_USER, k) && crf_count() == 1u, "favourites: the slot deleted: its mark goes (a new sound there starts unmarked)");
            (void)crf_set(CRF_T_USER, k, 1);         /* (a stale bit, as a restored object could carry) */
            rc = cb_store(&trk[CR_PART_CHORD], 0, k, "OTHER", 0);
            CHECK((rc == 0 || rc == 3) && !crf_has(CRF_T_USER, k), "favourites: a record into an empty slot clears a stale mark");
            (void)up_put(k, 0);
        }
        (void)crf_set(ENGI_FM6, 1, 0);
        (void)crf_flush();
        cu_pool_load(0, ENGI_FM6, 1);
        cr_restore_lock = (uint8_t)keep_lock;
    }

    /* ---- Dual Core: On by default (0.22, 3y46) and the persistent fallback's Off over it (3y70; cr_cpu1.h
     * cr_c1_bar_boot, cr_settings.c crs_dc_bar): a new unit (no settings record) boots On; with the bar a crash
     * beside core 1 leaves, the same new unit keeps core 1 off, reads Off and the poll saves Off once it is quiet,
     * whatever the default; Reset Settings in that power-on saves Off too; the player's On clears the bar ---- */
    {
        uint32_t t, keep_lock = cr_restore_lock;
        persist_t *rd = (persist_t *)cu_loop_buf;
        cr_restore_lock = 0;
        memset(scope_buf, 0, sizeof scope_buf);
        for (k = 0; k < 2u; k++)
            for (t = 0; t < NVOICE; t++)
                trk[k].v[t].active = 0;
        cr_snap.voices = cr_snap.pending = cr_snap.perf_step = 0;
#define DC_NEW_UNIT() do { (void)st_erase(st_sector(OBJ_SETTINGS, 0)); (void)st_erase(st_sector(OBJ_SETTINGS, 1)); } while (0)
#define DC_BOOT() do { cr_c1_off = 1; cr_c1_barred = 0; crs_dc_bar = 0; cr_settings_boot(); cr_settings_load(); } while (0)
#define DC_QUIET() do { for (t = 0; t < 8000u && crs_pending; t += 20u) { fm1_ms += 20u; cr_settings_poll(); } } while (0)
        DC_NEW_UNIT();
        memset(&cr_c1_bar_mock, 0, sizeof cr_c1_bar_mock);
        DC_BOOT();
        CHECK(st_load(OBJ_SETTINGS, rd, sizeof *rd) <= 0 && CRS_DUALCORE(&crs_rec.cr) && !cr_c1_off && !cr_c1_barred &&
              cs.dualcore == 1u && !crs_pending, "Dual Core: a new unit (no record): On, core 1 to start, nothing to save");
        DC_NEW_UNIT();
        cr_c1_bar_mock.magic = FM1_C1_BAR_MAGIC;     /* (as fm1_c1_fault_c leaves it: a crash of core 0's own beside core 1) */
        cr_c1_bar_mock.ran = 1;
        cr_c1_bar_mock.barred = C1_BAR_CRASH;
        DC_BOOT();
        CHECK(cr_c1_off && cr_c1_barred == C1_BAR_CRASH && crs_dc_bar && cs.dualcore == 0u && crs_pending &&
              !cr_c1_bar_mock.barred && !cr_c1_bar_mock.ran,
              "Dual Core: a new unit after a crash beside core 1: core 1 kept off, Off read over the default On, the save primed, the record cleared");
        DC_QUIET();
        CHECK(!crs_pending && st_load(OBJ_SETTINGS, rd, sizeof *rd) == (int)sizeof *rd && !CRS_DUALCORE(&rd->cr),
              "Dual Core: .. the poll saved Off once quiet (after %u ms)", (unsigned)t);
        cu_reset();                                  /* (Options > Reset Settings, confirmed) */
        CHECK(cs.dualcore == 0u && st_load(OBJ_SETTINGS, rd, sizeof *rd) == (int)sizeof *rd && !CRS_DUALCORE(&rd->cr) &&
              !strcmp(cu.msg.text, "settings reset"),
              "Dual Core: Reset Settings in the barred power-on: Off saved, not the default On (the fallback wins); the message '%s' (no restart)",
              cu.msg.text);
        DC_BOOT();
        CHECK(cr_c1_off && !cr_c1_barred && !crs_dc_bar && cs.dualcore == 0u && !crs_pending,
              "Dual Core: the next power-on: Off from the record itself, no word of the bar");
        opt_set(O_DUALCORE, 1);
        CHECK(cs.dualcore == 1u && !crs_dc_bar && !cr_c1_barred, "Dual Core: the player's On (Options): the record's from here on");
        crs_pending = 1;
        crs_change_ms = fm1_ms;
        DC_QUIET();
        DC_BOOT();
        CHECK(!cr_c1_off && cs.dualcore == 1u && !cr_c1_barred, "Dual Core: .. saved, and the next power-on starts core 1");
        DC_NEW_UNIT();
        cr_c1_bar_mock.magic = FM1_C1_BAR_MAGIC;     /* (a give-up: cr_c1_give_up's reason) */
        cr_c1_bar_mock.ran = 1;
        cr_c1_bar_mock.barred = C1_BAR_GAVE_UP;
        cr_safe = 1;                                 /* SAFE MODE: the bar waits (nothing is saved there) */
        DC_BOOT();
        CHECK(cr_c1_bar_mock.barred == C1_BAR_GAVE_UP && !cr_c1_barred && !crs_dc_bar,
              "Dual Core: SAFE MODE leaves the bar for the next normal boot");
        cr_safe = 0;
        DC_BOOT();
        CHECK(cr_c1_off && cr_c1_barred == C1_BAR_GAVE_UP && cs.dualcore == 0u && !cr_c1_bar_mock.barred,
              "Dual Core: .. which keeps core 1 off (gave up)");
        DC_QUIET();
        memset(&cr_c1_bar_mock, 0, sizeof cr_c1_bar_mock);
#undef DC_NEW_UNIT
#undef DC_BOOT
#undef DC_QUIET
        cr_restore_lock = (uint8_t)keep_lock;
    }

    printf("objects: settings %u (PER4 %u), bank %u, FM6 %u, VA %u, FM6 patches 2 x %u, QUAD %u, loop <= %u bytes\n",
           (unsigned)sizeof(persist_t), (unsigned)(sizeof(persist_t) - sizeof(cr_settings_t)), (unsigned)sizeof(up_bank_t),
           (unsigned)sizeof(fm6_bank_t), (unsigned)sizeof(va_store_t), (unsigned)sizeof(fm6u_t), (unsigned)sizeof(quad_store_t),
           (unsigned)CRL_REC_MAX);
    printf("%s: %d checks, %d failed\n", fails ? "CR BACKUP TESTS FAILED" : "cr backup tests passed", checks, fails);
    return fails ? 1 : 0;
}
