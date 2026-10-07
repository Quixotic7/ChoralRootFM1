/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the backup / restore SysEx handler (firmware/src/cr_backup.c, web/EDITOR_PROTOCOL.md "ChoralRoot:
 * backup and restore"): the firmware as the emulator builds it (tools/emu/emu_firmware.h: tests/hostsim.c's sound
 * side, the ChoralRoot UI, storage.c on emu_hal_fw.h's RAM NOR), requests fed to crb_handle as the main loop would,
 * the replies taken at CRB_SEND.
 *   sh tests/run_cr_tests.sh
 * Checks: INFO; LIST of every object (settings, banks, FM6 bank, VA, FM6 patches, 10 loops; no sample slots) with sizes
 * and CRCs; GET of
 * all of them in 256-byte pieces; a restore of everything onto an erased flash (PUT) gives a LIST and bytes
 * identical to the backup and reloads the mirrors; a CRC error, a short object, a malformed record are refused
 * with nothing written; a playing loop answers busy (3) and the commit goes through once it stops; stale sessions
 * (the UI's buffer, a USB reset, 15 s); Felucca's objects: a PER4 settings record becomes PER5 with ChoralRoot's
 * defaults, the banks and the FM6 bank as they are (Felucca 1.0's 27-slot FM6 bank too, imported at the next boot),
 * ids 0 / 2..5 / 32..34 refused, the SMP_* commands unanswered; RESTART. */
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
#include "../tools/emu/emu_firmware.h"

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
static obj_t man[32];
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

int main(void)
{
    uint32_t n, i, k;
    const uint8_t *a;
    static obj_t ref[32];
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
    CHECK(a && n > 12 && !memcmp(a, "EMU", 4) && a[n - 6] == 0x42 && a[n - 4] == 3 && a[n - 3] == 0x43 && a[n - 1] == 2,
          "INFO: version, no engines, tags 42 01 03 and 43 01 02");
    t_begin(2); t_b(0); t_b(0);
    CHECK(!t_send(&n), "an editor command (GET 2) gets no reply");

    /* ---- an empty device: every object listed, sizes 0 but the settings ---- */
    CHECK(t_list() == 0 && nman == 27, "LIST on a fresh flash: 27 objects");
    for (i = 0, k = 0; i < nman; i++) k += man[i].size != 0;
    CHECK(k <= 1, "fresh flash: nothing stored (the settings at most)");

    /* ---- content: settings, both banks, FM6, VA, three loops ---- */
    cs.bpm = 133;
    cs.tonic = 5;
    cr_settings_save();
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
        v.magic = VA_STORE_MAGIC; v.ver = 3; v.nslot = UP_SLOTS; v.blob = VA_BLOB;
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
    }
    make_loop(0, 4);
    make_loop(4, 300);
    make_loop(9, CRL_MAX_EV);
    power_on();
    CHECK(cs.loop_used == (1u | 1u << 4 | 1u << 9), "setup: loops in slots 1, 5, 10");

    /* ---- backup: LIST, GET everything, CRCs ---- */
    CHECK(t_capture() == 0, "backup: LIST + GET of every object, each CRC as listed");
    {
        static const uint8_t ids[27] = {1, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                        40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
        int same = nman == 27;
        for (i = 0; same && i < 27; i++) same = man[i].id == ids[i];
        CHECK(same, "LIST: ids 1 6..21 (12 13 the CZ-1 tones, 14..21 its banks) 40..49 in order (no sample slots)");
    }
    CHECK(find(man, nman, 1)->size == sizeof(persist_t) && find(man, nman, 6)->size == sizeof(up_bank_t) &&
          find(man, nman, 8)->size == sizeof(fm6_bank_t) && find(man, nman, 9)->size == sizeof(va_store_t) &&
          find(man, nman, 10)->size == sizeof(fm6u_t) && find(man, nman, 11)->size == sizeof(fm6u_t) &&
          find(man, nman, 12)->size == sizeof(czu_t) && find(man, nman, 13)->size == sizeof(czu_t) &&
          find(man, nman, 16)->size == sizeof(cz_bank_t) && find(man, nman, 14)->size == 0 &&
          find(man, nman, 40)->size > 16 && find(man, nman, 41)->size == 0 && find(man, nman, 49)->size == CRL_REC_HDR + 2u + 7u * CRL_MAX_EV,
          "LIST: the sizes (settings PER5, banks, FM6, VA, FM6 patches, loops; empty slots 0)");
    {
        const persist_t *p = (const persist_t *)find(man, nman, 1)->bytes;
        CHECK(p->magic == PERSIST_MAGIC && p->cr.bpm == 133 && p->cr.tonic == 5, "backup: the settings carry the last change");
    }
    t_begin(66); t_b(6); t_u32(3000); t_b(200 & 127); t_b(200 >> 7);
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
    CHECK(t_list() == 0 && find(man, nman, 6)->size == 0 && !find(man, nman, 32), "erased: banks empty (no sample object)");
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
        rc = t_put(1, find(ref, nref, 1)->bytes, find(ref, nref, 1)->size);
        CHECK(rc == 0 && cr_restore_lock && CR_SETTINGS_BUSY(), "restore: settings committed, the settings saves held until the restart");
    }
    CHECK(up_used(3) && up_used(16 + 4) && cs.loop_used == (1u | 1u << 4 | 1u << 9) &&
          fm6_bank.used == 5u && va_store.used == (1u << 3 | 1u << 19) && fm6u[0].used == 1u << 3 && fm6u[1].used == 1u << 3,
          "restore: the mirrors reloaded (user sounds, loops, FM6, VA, FM6 patches)");
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
        obj_t *o = find(ref, nref, 6);
        uint32_t erases = emu_stalls;
        uint8_t bad[CRL_REC_MAX];
        int rc = t_put_begin(6, o->size, o->crc ^ 1u);
        for (i = 0; !rc && i < o->size; i += 256) rc = t_put_data(6, i, o->bytes + i, o->size - i < 256 ? o->size - i : 256);
        CHECK(!rc && t_put_op(2, 6) == 2 && emu_stalls == erases, "a CRC error: commit rc 2, nothing erased");
        CHECK(t_put_begin(6, 100, 0) == 1 && t_put_begin(1, 4000, 0) == 1 && t_put_begin(9, 17, 0) == 1, "a wrong size: begin rc 1");
        memcpy(bad, o->bytes, o->size);
        bad[0] ^= 1;                                 /* the bank's magic */
        CHECK(t_put(6, bad, o->size) == 2 && emu_stalls == erases, "a bank with a bad magic: rc 2, nothing written");
        o = find(ref, nref, 40);
        memcpy(bad, o->bytes, o->size);
        bad[4] = 9;                                  /* the loop record's version */
        CHECK(t_put(40, bad, o->size) == 2, "a loop record of another version: rc 2");
        o = find(ref, nref, 8);
        memcpy(bad, o->bytes, o->size);
        bad[12] = 200;                               /* a function setting out of its range */
        CHECK(t_put(8, bad, o->size) == 2, "an FM6 bank with a bad function setting: rc 2");
        o = find(ref, nref, 10);
        memcpy(bad, o->bytes, o->size);
        bad[16 + 3 * FM6_BLOB + 112] = 0;            /* slot 4's blob: its magic */
        CHECK(t_put(10, bad, o->size) == 2, "an FM6 patch store with a bad blob: rc 2");
        CHECK(t_put_data(6, 0, bad, 16) == 5 && t_put_op(2, 6) == 5, "data / commit without a begin: rc 5");
        o = find(ref, nref, 6);
        CHECK(t_put_begin(6, o->size, o->crc) == 0 && t_put_data(6, 256, o->bytes, 256) == 1, "data out of order: rc 1");
        CHECK(t_put_op(3, 6) == 0 && t_put_op(2, 6) == 5, "abort ends the session");
    }

    /* ---- stale sessions ---- */
    {
        obj_t *o = find(ref, nref, 7);
        CHECK(t_put_begin(7, o->size, o->crc) == 0 && t_put_data(7, 0, o->bytes, 256) == 0, "stale: begin + data");
        cu_loop_gen++;                               /* the UI loaded a loop slot through cu_loop_buf */
        CHECK(t_put_data(7, 256, o->bytes + 256, 256) == 5, "stale: the UI reused the staging buffer -> rc 5");
        CHECK(t_put_begin(7, o->size, o->crc) == 0, "stale: begin again");
        fm1_ms += 16000u;
        CHECK(t_put_data(7, 0, o->bytes, 256) == 5, "stale: 15 s without a request -> rc 5");
        CHECK(t_put_begin(7, o->size, o->crc) == 0, "stale: begin again");
        usb.resets++;
        CHECK(t_put_data(7, 0, o->bytes, 256) == 5, "stale: a USB reset -> rc 5");
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
        CHECK(t_put_begin(0, 3584, 0) == 1 && t_put_begin(2, 3584, 0) == 1 && t_put_begin(5, 0, 0) == 1,
              "Felucca: ids 0 (the music) and 2..5 (projects) refused (rc 1)");
        CHECK(t_put_begin(32, 4096, 0) == 1 && t_put_begin(33, 0, 0) == 1 && t_put_begin(34, 0, 0) == 1,
              "Felucca: ids 32..34 (sample slots) refused (rc 1)");
        CHECK(t_smp(11, 0) == -1 && t_smp(14, 0) == -1 && t_smp(11, 2) == -1, "SMP_BEGIN / SMP_ERASE: no reply (no SAMPLE engine)");
        CHECK(t_put(6, find(ref, nref, 6)->bytes, sizeof(up_bank_t)) == 0 && t_put(8, find(ref, nref, 8)->bytes, sizeof(fm6_bank_t)) == 0,
              "Felucca: user preset banks and the FM6 bank restore");
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

    /* ---- RESTART ---- */
    t_begin(72);
    a = t_send(&n);
    CHECK(a && n == 1 && a[0] == 0 && crb.reboot, "RESTART: rc 0, the restart scheduled");

    printf("objects: settings %u (PER4 %u), bank %u, FM6 %u, VA %u, FM6 patches 2 x %u, loop <= %u bytes\n",
           (unsigned)sizeof(persist_t), (unsigned)(sizeof(persist_t) - sizeof(cr_settings_t)), (unsigned)sizeof(up_bank_t),
           (unsigned)sizeof(fm6_bank_t), (unsigned)sizeof(va_store_t), (unsigned)sizeof(fm6u_t), (unsigned)CRL_REC_MAX);
    printf("%s: %d checks, %d failed\n", fails ? "CR BACKUP TESTS FAILED" : "cr backup tests passed", checks, fails);
    return fails ? 1 : 0;
}
