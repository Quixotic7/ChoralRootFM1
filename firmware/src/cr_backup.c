/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: backup and restore over USB-MIDI SysEx (web/EDITOR_PROTOCOL.md, "ChoralRoot: backup and
 * restore"). The subset of Felucca's protocol (editor.c / editor_backup.c, dropped with Felucca's UI) that the
 * installer page (web/fm1backup.js) and tools/fm1_install.py use, on ChoralRoot's objects:
 *
 *   1 INFO      "ChoralRoot <version>", no editor (0 engines), tags 42 01 03 (backup read + restore) and 43 01 02, then
 *               (3y66) 49 08 and the unit's ID, 8 hex digits in ASCII (cr_ui.c cr_unit_id; no tag when none is known)
 *   65 LIST     1, rc, count, per object: id, size u32, crc u32 (CRC-32 zlib of the bytes GET returns)
 *   66 GET      id, offset u32, count (<= 256) -> id, rc, offset u32, count, pack7 data
 *   67 PUT      0 begin (id, size u32, crc u32) / 1 data (id, offset u32, pack7) / 2 commit (id) / 3 abort (id)
 *   72 RESTART  -> rc; the device restarts (everything restored loads from flash)
 *   73 DEBUG    -> version 1, count, count x u32: the diagnostic counters (read-only)
 *   74 SCREEN   0 off / 1 on -> rc 0: the screen off (a black frame, the backlight on, nothing drawn) until 1 or a
 *               key, button or knob on the unit (MIDI in does not wake it), as the idle timer's (cr_ui.c
 *               cu_screen_sysex)
 *   75 CORE1_TEST  a CR_CORE1_TEST build only (tools/core1_fault_test.py); else "unsupported"
 *   76 SELECT   part, engine, pos -> rc: a part to an engine's pool position, live (the device manager's audition)
 *   77 STUB     op 1 test / 2 rescue -> rc: a request to the boot stub, then a restart (docs/BOOT-SAFETY.md)
 *   78 PEAK     -> version 1, count, count x u32: the audio ISR's worst cases since the last read (read-only; reading
 *               clears them; docs/FX.md)
 * (not backup commands, 73..78: they share the transport; each has its own comment below.)
 *
 * Objects (Felucca's ids where Felucca has them: 1, 8; 14..23, 40..62 are ChoralRoot's):
 *   1 settings (persist_t PER5: Felucca's fields + cr_settings_t; a PUT takes PER1..PER5, settings_persist.c migrates)
 *   8 the FM6 patch bank (fm6_bank.c: Melodee's 32-voice layout; Felucca 1.0's 27 and Melodee's earlier bank restore
 *   too, converted at the next boot as at power-on), 14..21 the CZ-1 banks A..H (cz_bank.c: one object a bank, as
 *   Melodee's backup ids 9..16; a bank never saved has no object: its default, Casio's tones or empty), 23 the
 *   favourites (cr_favs.c, 0.22: the marks on sounds; an older firmware has no id 23: it does not list it and answers a
 *   PUT begin of it rc 1, which the clients report as skipped), 40..49 loop slots 1..10 (cr_ui.c's flash records,
 *   docs/LOOPER.md), 50..62 the sound pool's banks (below). 0.21's sound objects 6, 7, 9..13 and 22 are refused (below).
 *   No sample objects: ChoralRoot has no SAMPLE engine; Felucca's 32..34 and SMP_BEGIN .. SMP_ERASE (11..14) are not
 *   answered, so the installer page reports a Felucca archive's samples (and a ChoralRoot 0.1 archive's 32 / 33)
 *   skipped.
 *
 * Reads come from flash (the current copy of each A/B pair, checked at LIST) in 256-byte st_read windows: no RAM
 * copy, the audio keeps running (a loop may play). A read never writes flash (3y48): LIST lists the Options as last
 * written and the favourites from RAM when a mark is not written yet. Writes are staged in cr_ui.c's cu_loop_buf (the largest object, a loop record, fits;
 * cu_loop_gen tells when the UI reused it: rc 5) and validated whole before one commit through storage.c's
 * protocol (st_save: the other copy, the header last; a loop: crl_fl_save, the same protocol): a torn write leaves
 * the previous copy. A begin or a commit while a loop plays, records or a slot load is on its way answers rc 3
 * (busy: the page retries); the commit's erase holds the audio ~45 ms (storage_hw.c), which a restore accepts.
 * After a commit the RAM mirrors are reloaded from flash; a restored settings record also sets cr_restore_lock
 * (cr_ui.c: no settings save until the restart, so the UI's older state cannot overwrite it).
 *
 * Scratch: the replies and GET's data use storage.c's st_buf between storage calls (no RAM of its own but the
 * session state below). Device unit (choralroot.c) and the emulator / host test (tools/emu/emu_firmware.h,
 * tests/cr_backup_test.c): CRB_SEND / CRB_REBOOT are the transport hooks; the device's ed_service (main.c) is here. */
#if FELUCCA_FLASH
#if !FELUCCA_VA || !FELUCCA_CZ || !FELUCCA_QUAD || !UP_POOL || !CR_HAVE_SETTINGS
#error "cr_backup.c: ChoralRoot's objects (the sound pool, the CZ-1 banks, the settings record) are the backup"
#endif
#define CRB_HDR0 0x7Du
#define CRB_HDR1 0x46u
#define CRB_HDR2 0x4Cu
enum { CRB_INFO = 1, CRB_LIST = 65, CRB_GET, CRB_PUT, CRB_RESTART = 72, CRB_DEBUG = 73, CRB_SCREEN, CRB_CORE1_TEST, CRB_SELECT,
       CRB_STUB, CRB_PEAK, CRB_USB, CRB_BLE = 81, CRB_BLE_TEST };   /* (80 was IMPORT, 0.22 .. 0.29) */
#define CRB_LOOP0 40u                                /* loop slot k: id 40 + k */
#define CRB_CZB 14u                                  /* 14..21: the CZ-1 banks A..H */
#define CRB_FAVS 23u                                 /* 23: the favourites (after the sounds they mark) */
/* ids 50..62: the sound pool's 13 banks (0.22, upool.c; storage.c sp_*: a bank's ST_PAYLOAD_MAX bytes, ten slots of
 * SP_SLOT: the record and its patch). 0.21's sound objects (6, 7 the user banks, 9 the VA store, 10 / 11 FM6's, 12 / 13
 * the CZ-1 tones', 22 FM TONE's) are refused like any id this firmware does not list: 0.22 .. 0.29 took them from an
 * older backup into slots 1..32; since HISTORY 3y231 such a backup restores without its sounds (through 0.29.2 first) */
#define CRB_POOL 50u                                 /* (not 24..36: Felucca's sample ids 32..34 are refused) */
static const uint8_t CRB_IDS[] = {1, 8, 14, 15, 16, 17, 18, 19, 20, 21, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
                                  CRB_FAVS, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
_Static_assert(SP_NBANK == 13u && CRB_POOL >= CRB_LOOP0 + CRL_SLOTS && CRB_POOL + SP_NBANK <= 64u &&
               ST_PAYLOAD_MAX <= sizeof cu_loop_buf, "backup ids: the pool's banks 50..62 stage in cu_loop_buf");
static int crb_is_pool(uint32_t id) { return id >= CRB_POOL && id < CRB_POOL + SP_NBANK; }
_Static_assert(sizeof(cz_bank_t) <= CRL_REC_MAX, "backup: the CZ-1 banks stage too");
static int crb_is_czbank(uint32_t id) { return id >= CRB_CZB && id < CRB_CZB + CZ_BANK_N; }
#define CRB_N ((uint32_t)sizeof CRB_IDS)
_Static_assert(CRL_SLOTS == 10u, "backup ids: 10 loop slots");
_Static_assert(sizeof(persist_t) <= CRL_REC_MAX && sizeof(fm6_bank_t) <= CRL_REC_MAX,
               "backup: every object stages in cu_loop_buf");
_Static_assert(ST_PAYLOAD_MAX >= 2048u + 256u, "backup: the reply and the data share st_buf");

#ifndef CRB_SEND
#if FELUCCA_OTA
#define CRB_SEND(p, n) ota_wire_send((p), (n))
#else
#define CRB_SEND(p, n) ((void)(p), (void)(n))        /* (the emulator: no SysEx transport) */
#endif
#endif

static struct {
    uint8_t listed, put, id, gen;                    /* a LIST snapshot; a PUT session: its id, cu_loop_gen */
    int8_t copy[CRB_N];                              /* the current copy at LIST (-1: none) */
    uint32_t size[CRB_N];                            /* the object's size at LIST */
    uint32_t usb, len, crc, pos, ms;                 /* usb.resets at LIST / begin; the PUT */
    uint32_t reloads;                                /* parts reloaded after a PUT (crb_reload; the tests read it) */
    uint32_t reboot;                                 /* a restart is owed (72 RESTART, 77 STUB; crb_leave below): 0 none,
                                                      * 1 asked, 2 its reply marked in the ring, 3 the reply seen taken */
    uint32_t reboot_ms, mark, gone_ms;               /* fm1_ms at the request; the reply's end (ota_wire_mark); fm1_ms
                                                      * when the host was seen to have it */
    uint32_t stub;                                   /* 77 STUB: the request the restart leaves for the stub */
} crb;
#define CRB_COPY_RAM 64                              /* crb.copy: served from RAM (the favourites not yet written; not a
                                                      * copy 0 / 1 nor a pool sector 0..15) */
#if defined(CR_FAVS) && CRF_FLASH
static uint8_t crb_favram[CRF_PACK_MAX];             /* .. their packed bytes at LIST */
#endif

/* ------------------------------------------------------------------ the reply --- */
#define CRB_OUT st_buf                               /* the reply (<= 400 bytes, written after any storage call) .. */
#define CRB_DATA (st_buf + 2048u)                    /* .. and GET's flash data */
static uint32_t crb_n;
#if BOARD_SMK37
#define CRB_VER_MAX 40u                              /* INFO's version: SMK-37 test builds' are longer (ChoralRoot
                                                      * 0.24-beta2-smk37 1-core: 34) */
#else
#define CRB_VER_MAX 32u
#endif
static void crb_b(uint32_t v)
{
    if (crb_n < 1024u - 1u)
        CRB_OUT[crb_n++] = (uint8_t)(v & 0x7Fu);
}
static void crb_u32(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        crb_b(v >> (7u * i));
}
static uint32_t crb_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void crb_pack(const uint8_t *p, uint32_t n)  /* pack7: a byte of top bits, then up to 7 low-7-bit bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        crb_b(m);
        for (i = 0; i < k; i++)
            crb_b(p[i]);
        p += k;
        n -= k;
    }
}
static uint32_t crb_unpack(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)   /* 0: malformed / too long */
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j, k;
        na--;
        k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static void crb_send(uint32_t cmd, uint32_t from)   /* the reply's bytes are CRB_OUT[from..crb_n) */
{
    CRB_OUT[from - 5u] = 0xF0;
    CRB_OUT[from - 4u] = CRB_HDR0;
    CRB_OUT[from - 3u] = CRB_HDR1;
    CRB_OUT[from - 2u] = CRB_HDR2;
    CRB_OUT[from - 1u] = (uint8_t)cmd;
    CRB_OUT[crb_n++] = 0xF7;
    CRB_SEND(CRB_OUT + from - 5u, crb_n - (from - 5u));
}

/* ------------------------------------------------------------------ the objects --- */
static int crb_index(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < CRB_N; i++)
        if (CRB_IDS[i] == id)
            return (int)i;
    return -1;
}
static int crb_is_loop(uint32_t id) { return id >= CRB_LOOP0 && id < CRB_LOOP0 + CRL_SLOTS; }
static uint32_t crb_obj(uint32_t id)                 /* storage.c's object of ids 1, 8, 14..21, 23 */
{
    if (id == CRB_FAVS)
        return OBJ_FAVS;
    if (crb_is_czbank(id))
        return OBJ_CZBANK0 + id - CRB_CZB;
    return id == 1u ? (uint32_t)OBJ_SETTINGS : (uint32_t)OBJ_FM6BANK;
}
/* an FM6 bank of earlier firmware, as fm6_bank.c fm6_bank_import takes it at boot: Felucca 1.0's (27 records, version
 * 1) or Melodee's before its version 2 (32 packed voices) */
#define CRB_FM6_FEL (16u + 27u * 128u)
#define CRB_FM6_MEL (4u + FM6_BANK_N * FM6_BANK_PK)
static int crb_fm6_old(const uint8_t *raw, uint32_t len)
{
    uint32_t magic = raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24;
    return magic == FM6_BANK_MAGIC && (len == CRB_FM6_MEL || (len == CRB_FM6_FEL && raw[4] == 1u && raw[6] == 27u));
}
/* an A/B object (storage.c's or a loop slot's: the same commit record, another type and place) */
static uint32_t crb_sector(uint32_t id, uint32_t copy)   /* (a pool bank: copy is its sector, 0..15) */
{
    if (crb_is_pool(id))
        return sp_addr(copy);
    return crb_is_loop(id) ? crl_fl_sector(id - CRB_LOOP0, copy) : st_sector(crb_obj(id), copy);
}
static int crb_head(uint32_t id, uint32_t copy, st_hdr_t *h)   /* 0: a valid commit record */
{
    uint32_t type = crb_is_loop(id) ? CRL_FL_TYPE + id - CRB_LOOP0 : crb_obj(id);
    if (st_read(crb_sector(id, copy), h, sizeof *h) || h->magic != ST_MAGIC || h->type != type || h->slot != copy ||
        h->len > ST_PAYLOAD_MAX || h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}
static int crb_body(uint32_t id, uint32_t copy, const st_hdr_t *h)   /* the payload (into st_buf) is intact: 0 */
{
    return st_read(crb_sector(id, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc ? -1 : 0;
}
static int crb_current(uint32_t id, st_hdr_t *h)    /* storage.c st_current's choice: the newest intact copy */
{
    st_hdr_t a, b;
    if (crb_is_pool(id)) {                           /* the bank's current copy (sp_scan's choice): its sector */
        uint32_t s;
        sp_scan();
        s = sp_cur[id - CRB_POOL];
        if (s == SP_NONE || sp_head(s, h))
            return -1;
        return (int)s;
    }
    int va = crb_head(id, 0, &a) == 0, vb = crb_head(id, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u)) && crb_body(id, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    if (va && crb_body(id, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && crb_body(id, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}
/* a flash erase now would cut a loop: it plays, records, or a slot load is on its way */
static int crb_busy(void)
{
    return cr_snap.lstate == CRL_PLAYING || cr_snap.lcap != CRL_CAP_NONE || crl_stage_busy;
}

/* ------------------------------------------------------------------ LIST / GET --- */
static void crb_list(void)
{
    uint32_t i, crc[CRB_N];
    st_hdr_t h;
    /* 3y48: a READ never writes flash (the owner, 2026-10-13, a stutter while an arp played: "I did not do anything
     * around then ... So not sure why it wrote to flash"; until 3y48 LIST called cr_settings_save, which erased for a
     * pending Option or favourite whatever sounded). The Options: as last written (the poll writes a change once
     * nothing sounds); the favourites not yet written: their RAM form (crb_favram, CRB_COPY_RAM) */
    for (i = 0; i < CRB_N; i++) {
        uint32_t id = CRB_IDS[i];
        crb.copy[i] = -1;
        crb.size[i] = crc[i] = 0;
#if defined(CR_FAVS) && CRF_FLASH
        if (id == CRB_FAVS && crf_dirty && !ST_BLOCKED()) {
            crb.copy[i] = CRB_COPY_RAM;
            crb.size[i] = crf_pack(crb_favram);
            crc[i] = st_crc32(crb_favram, crb.size[i]);
            continue;
        }
#endif
        if (flash_ok && (crb.copy[i] = (int8_t)crb_current(id, &h)) >= 0) {
            crb.size[i] = h.len;
            crc[i] = h.crc;
        }
        fm1_wdt_feed();
    }
    crb.listed = 1;
    crb.put = 0;
    crb.usb = usb.resets;
    crb_b(1);
    crb_b(0);
    crb_b(CRB_N);
    for (i = 0; i < CRB_N; i++) {
        crb_b(CRB_IDS[i]);
        crb_u32(crb.size[i]);
        crb_u32(crc[i]);
    }
}
static void crb_get(const uint8_t *a, uint32_t n)
{
    uint32_t off = n >= 6u ? crb_r32(a + 1) : 0u, count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u, rc;
    int i = n ? crb_index(a[0]) : -1;
    if (!crb.listed || crb.usb != usb.resets)
        rc = 5;
    else if (n != 8u || a[5] > 15u || i < 0 || !count || count > 256u || off > crb.size[i] || count > crb.size[i] - off)
        rc = 1;
#if defined(CR_FAVS) && CRF_FLASH
    else if (crb.copy[i] == CRB_COPY_RAM) {          /* the marks as LIST packed them (RAM) */
        uint32_t j;
        for (j = 0; j < count; j++)
            CRB_DATA[j] = crb_favram[off + j];
        rc = 0;
    }
#endif
    else
        rc = st_read(crb_sector(a[0], (uint32_t)crb.copy[i]) + ST_PAYLOAD_OFF + off, CRB_DATA, count) ? 4u : 0u;
    crb_b(n ? a[0] : 127u);
    crb_b(rc);
    crb_u32(off);
    crb_b(rc ? 0u : count & 127u);
    crb_b(rc ? 0u : count >> 7);
    if (!rc)
        crb_pack(CRB_DATA, count);
}

/* ------------------------------------------------------------------ PUT --- */
#define CRB_RAW cu_loop_buf                          /* staging: cr_ui.c's loop record buffer (see the top) */
static int crb_panel_ok(const panel_t *p)
{
    uint32_t i, b = 0, e = 0;
    if (p->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (p->btn[i] >= NB || (b & (1u << p->btn[i])))
            return 0;
        b |= 1u << p->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (p->enc[i] >= NE || (e & (1u << p->enc[i])) || (p->dir[i] != 1 && p->dir[i] != -1))
            return 0;
        e |= 1u << p->enc[i];
    }
    return 1;
}
static int crb_size_ok(uint32_t id, uint32_t len)   /* a begin's size for this id */
{
    if (id == 1u)                                    /* PER1 (8 + panel) .. PER5 (this record) */
        return len >= 8u + sizeof(panel_t) && len <= sizeof(persist_t);
    if (id == 8u)
        return !len || len == sizeof(fm6_bank_t) || len == CRB_FM6_FEL || len == CRB_FM6_MEL;
    if (crb_is_czbank(id))
        return !len || len == sizeof(cz_bank_t);
    if (id == CRB_FAVS)                              /* (0: none; else the version and the count at least; 264 at most) */
        return !len || (len >= 2u && len <= CRF_PACK_MAX);
    if (crb_is_pool(id))                             /* (0: an empty bank) */
        return !len || len == ST_PAYLOAD_MAX;
    if (crb_is_loop(id))
        return !len || (len >= CRL_REC_HDR && len <= CRL_REC_MAX);
    return 0;
}
/* after a PUT of a pool bank, the slots reloaded: a part playing a slot the bank holds loads it again from the new
 * data, so a patch uploaded into the playing slot is heard at once; so does a part on a factory preset a slot now binds
 * (it plays that record now). A slot emptied under a part: the part keeps its sound (RAM). A slot now of another
 * engine: the part follows it into that engine's pool */
static void crb_reload(uint32_t id)
{
    uint32_t part, lo, hi;
    if (!crb_is_pool(id))
        return;
    lo = (id - CRB_POOL) * SP_PER;
    hi = lo + SP_PER;
    for (part = 0; part < 2u; part++) {
        uint32_t e = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES, k = UP_SLOTS;
        if (psnd[part].kind == PK_USER)
            k = psnd[part].slot;
        else if (psnd[part].kind == PK_FACTORY)
            k = pool_bound_slot(e, psnd[part].fk);
        if (k >= UP_SLOTS || k < lo || k >= hi || !up_used(k))
            continue;
        e = cb_rec_engine(k);
        cu_pool_load(part, e, pool_pos_slot(e, k));
        if (!part)
            cu_sends_to_fx();
        crb.reloads++;
        cu_trace("reload: part %u slot %u %s\n", (unsigned)part, (unsigned)k + 1u, psnd[part].name);
    }
}

/* 76 SELECT part engine pos [pos_hi] -> rc: part 0 (chord) / 1 (bass) to engine e and its pool position pos (0 INIT,
 * 1.. the factory presets, then the added ones: cr_bank.c's pool order, as PRESETS turns), live, as OPT + PRESETS
 * then PRESETS would (the device manager's audition). rc 0 done, 1 bad part or engine (not a melodic engine shown),
 * 2 bad position, 3 busy (EDIT or SAVE open: the sound is being edited) */
static uint32_t crb_select(const uint8_t *a, uint32_t na)
{
    uint32_t part = a[0], e = a[1], pos = a[2] | (na > 3u ? (uint32_t)a[3] << 7 : 0u);
    if (part > 1u || e >= NENGINES || !eng_ok(e) || cu_engine_rank(e) < 0)
        return 1;
    if (pos >= pool_count(e))
        return 2;
    if (cu.page == PG_EDIT || cu.page == PG_SAVE)
        return 3;
    cu_pos_sync();                                   /* (the place in the engine left: remembered, as cu_epk_commit) */
    cu_pool_load(part, e, pos);
    if (!part)
        cu_sends_to_fx();
    cu_sound_popup((int)part);
    ui.force = 1;
    cu_trace("select: part %u engine %s pos %u %s\n", (unsigned)part, ENGINES[e]->name, (unsigned)pos, psnd[part].name);
    return 0;
}

static const uint8_t *crb_bank_src;
/* a pool bank's PUT (0: empty): its used slots as sent, every other slot written empty (zeros, as upool_put erases
 * one), so a deleted slot's bytes (the device manager sends the bank as read) never stay in flash (3y78) */
static int crb_bank_fill(uint32_t j, uint8_t *slot)
{
    const uint8_t *s = crb_bank_src ? crb_bank_src + j * SP_SLOT : 0;
    if (s && s[0] == UP_USED)
        memcpy(slot, s, SP_SLOT);
    else
        memset(slot, 0, SP_SLOT);
    return 0;
}
/* each slot unused (its bytes not read: crb_bank_fill empties it) or a record whose patch header is one up_patch_get
 * takes: none, or an engine's with that engine's length (FM TONE: also an older slot's 80-byte version 1..3 blob,
 * which eng_quad.c converts; 3y78: 0.22-beta1 refused a bank holding one, the slots 0.21's FM TONE sounds migrated to),
 * or a patch of an engine this build keeps none for, its bytes kept as sent (3y121: a GRAIN sound saved on the GRAIN
 * branch's build, 20 bytes, made every PUT of its bank rc 2 on this one; up_read_engine never reads such a patch,
 * the slot loads as its record says, and a build that has the engine finds its patch again) */
static int crb_bank_ok(const uint8_t *raw)
{
    uint32_t j, e, n;
    for (j = 0; j < SP_PER; j++) {
        const uint8_t *s = raw + j * SP_SLOT;
        if (s[0] != UP_USED)
            continue;
        if (!up_valid((const up_rec_t *)s))
            return 0;
        if (!s[UPP_HDR])
            continue;
        e = s[UPP_HDR] - 1u;
        n = s[UPP_HDR + 1u];
        if (e >= NENGINES || !n || n > UPP_MAX ||
            (up_patch_len(e) && n != up_patch_len(e) && !(e == ENGI_QUAD && n == QUAD_BLOB_V3) &&
             !(FELUCCA_GRAIN && e == ENGI_GRAIN && n == GRAIN_BLOB_V1)))   /* (GRAIN's 20-byte version 1 too, 3y139) */
            return 0;
    }
    return 1;
}
static uint32_t crb_commit(void)
{
    uint8_t *raw = CRB_RAW;
    uint32_t id = crb.id, len = crb.len;
    if (crb.pos != len || st_crc32(raw, len) != crb.crc)
        return 2;
    if (id == 1u) {                                  /* settings: Felucca's fields migrated, ChoralRoot's block checked */
        persist_t *p = (persist_t *)raw;
        const panel_t *pn = (const panel_t *)(raw + (len == 8u + sizeof(panel_t) ? 8u : 16u));
        if (!crb_panel_ok(pn) || !settings_import(p, (int)len))
            return 2;
        cr_settings_import(&p->cr, &p->cr, sizeof p->cr);   /* (a PER1..4 record: a zeroed block -> the defaults) */
        p->lowcut = 0;                               /* Felucca's SPEAKER setting: not ChoralRoot's to run (0.19,
                                                      * cr_settings.c cr_settings_boot): a restored object stores 0 */
        settings.lowcut = 0;
        if (st_save(OBJ_SETTINGS, p, sizeof *p))
            return 4;
#if CR_HAVE_SETTINGS
        crs_rec = *p;                                /* what flash holds now */
        crs_seen = p->cr;
        crs_pending = 0;
        crs_obj_crc = st_crc32(p, sizeof *p);
        (void)st_save(OBJ_SESSION, p, 0);            /* 0.18: the session belonged to the settings replaced: an empty
                                                      * object, so the restart comes up on the restored settings alone
                                                      * (their sounds, the bass off, voicing 0) */
        css_flash = 0;
#endif
        cr_restore_lock = 1;                         /* no settings save before the restart */
        cu_message("settings restored: restart", CR_COL_GREEN);
        return 0;
    }
    if (id == 8u) {
        if (len && !(len == sizeof(fm6_bank_t) ? fm6_bank_valid((const fm6_bank_t *)raw) : crb_fm6_old(raw, len)))
            return 2;                                /* (an older layout: imported by fm6_bank_boot, as at power-on) */
    } else if (crb_is_czbank(id)) {
        if (len && !cz_bank_valid((const cz_bank_t *)raw))
            return 2;
    } else if (id == CRB_FAVS) {                     /* the marks: stored as sent (an object this firmware cannot read
                                                      * reads as no marks, cr_favs.c), the RAM copy from flash */
        if (st_save(OBJ_FAVS, raw, len))
            return 4;
        crf_boot();
        ui.force = 1;
        return 0;
    } else if (crb_is_pool(id)) {
        if (len && !crb_bank_ok(raw))
            return 2;
        if (!len && !sp_used(id - CRB_POOL))
            return 0;                                /* (an empty bank onto none: nothing to write) */
        crb_bank_src = len ? raw : 0;
        if (sp_commit(id - CRB_POOL, crb_bank_fill))
            return 4;
        upool_table();
        crb_reload(id);
        ui.force = 1;
        return 0;
    } else if (crb_is_loop(id)) {
        uint32_t k = id - CRB_LOOP0;
        if (crl_stage_busy)
            return 3;
        if (len && !cr_loop_unpack(raw, len, &crl_stage))   /* (crl_stage: free while not busy, as cu_loop_scan) */
            return 2;
        if (len ? crl_fl_save(k, raw, len) : crl_fl_delete(k))
            return 4;
        if (len)
            cs.loop_used |= (uint16_t)(1u << k);
        else
            cs.loop_used &= (uint16_t)~(1u << k);
        return 0;
    } else {
        return 1;
    }
    if (st_save(crb_obj(id), raw, len))
        return 4;
    if (id == 8u) {                                  /* the mirror from flash; parts on a bank patch reload theirs */
        uint32_t t;
        fm6_bank_boot();
        for (t = 0; t < NTRK; t++)
            if (fm6_slot[t] >= FM6_NFAC)
                fm6_slot[t] = 0xFFu;
    } else {                                         /* a CZ-1 bank */
        up_boot();                                   /* the pool's table, the FM6 bank, the CZ-1 banks from flash */
        up_gen++;
        cz_bank_import(id - CRB_CZB, raw, len);      /* (parts on that bank: its new tones, cz_bank_poll) */
    }
    ui.force = 1;
    return 0;
}
static uint32_t crb_put(const uint8_t *a, uint32_t n)
{
    uint32_t rc;
    if (n < 2u || a[0] > 3u)
        return 1;
    if (a[0] == 0u) {
        uint32_t len;
        if (n != 12u || a[6] > 15u || a[11] > 15u || !crb_size_ok(a[1], len = crb_r32(a + 2)))
            return 1;
        if (!flash_ok)
            return 4;
        if (crb_busy())
            return 3;
        crb.listed = 0;                              /* (a begin ends the snapshot, as Felucca) */
        crb.put = 1;
        crb.id = a[1];
        crb.len = len;
        crb.crc = crb_r32(a + 7);
        crb.pos = 0;
        crb.gen = cu_loop_gen;
        crb.usb = usb.resets;
        crb.ms = fm1_ms;
        return 0;
    }
    if (!crb.put || crb.id != a[1] || crb.usb != usb.resets || fm1_ms - crb.ms > 15000u || crb.gen != cu_loop_gen) {
        crb.put = 0;                                 /* stale: no begin, another id, a USB reset, 15 s, the UI's buffer */
        return 5;
    }
    crb.ms = fm1_ms;
    if (a[0] == 3u) {
        crb.put = 0;
        return n == 2u ? 0u : 1u;
    }
    if (a[0] == 2u) {
        if (n != 2u)
            return 1;
        if (crb_busy())
            return 3;                                /* (the session stays: the page sends the commit again) */
        st_why = ST_WHY_PUT;
        rc = crb_commit();
        st_why = ST_WHY_OTHER;
        crb.put = rc == 3u;
        return rc;
    }
    if (n < 9u || a[6] > 15u || crb_r32(a + 2) != crb.pos)
        return 1;
    {
        uint32_t left = crb.len - crb.pos, k = crb_unpack(a + 7, n - 7u, CRB_RAW + crb.pos, left < 256u ? left : 256u);
        if (!k)
            return 1;
        crb.pos += k;
    }
    return 0;
}

/* ------------------------------------------------------------------ DEBUG --- */
/* 73 DEBUG (read-only, not a backup object: LIST does not name it) -> version 1, count, count x u32 (crb_u32): the
 * device's counters for tools/fm1_install.py --debug, which names them in this order (DEBUG_NAMES there, the USB
 * event log's 32 values, then DEBUG_TAIL: values are only ever appended, so an older tool names what it knows) */
static void crb_debug(void)
{
    uint32_t v[144], k = 0, i;
#ifdef DBG_MAGIC
    v[k++] = fm1_ms;
    v[k++] = felucca_dbg.halves;
    v[k++] = felucca_dbg.max_us;
    v[k++] = felucca_dbg.late;
    v[k++] = felucca_dbg.nested;
    v[k++] = felucca_dbg.timer_irqs;
    v[k++] = felucca_dbg.ui_frames;
    v[k++] = felucca_dbg.last_us;
    v[k++] = cpu_last.avg_us;
    v[k++] = cpu_last.max_us;
    v[k++] = cpu_last.max_all_us;
    v[k++] = cpu_last.halves;
#else
    for (i = 0; i < 12u; i++)
        v[k++] = 0;
#endif
    v[k++] = cr_dbg.c1;
    v[k++] = cr_dbg.c1_jobs;
    v[k++] = cr_c1_rejected();                       /* core 1's misreads, refused (docs/DUALCORE.md "Reliability") */
    v[k++] = cr_c1_faults();                         /* faults about core 1, caught */
    v[k++] = cr_dbg.c1_fallback;                     /* 1: core 1 given up this session (one core since) */
    v[k++] = cr_dbg.rail_boot[0];                    /* SYSVDD: the boot loader's, now (11 1.26 V, 14 1.35 V) */
    v[k++] = cr_dbg.rail_now[0];
    v[k++] = cr_dbg.rail_boot[1];                    /* VDC14 (3 1.40 V, 5 1.50 V) */
    v[k++] = cr_dbg.rail_now[1];
    for (i = 0; i < CRP_N; i++)
        v[k++] = cr_dbg.avg_us[i];
#ifdef FM1_HAVE_STUB                                 /* the boot stub's mailbox (firmware/stub/stub.c, docs/BOOT-SAFETY.md) */
    {
        uint32_t info = fm1_stub_word(1), ok = info >> 16 == 0x5342u;
        v[k++] = ok ? fm1_stub_word(2) / (1000u * FM1_TICKS_PER_US) : 0u;   /* stub_ms: the boot's delay */
        v[k++] = ok ? fm1_stub_word(2) / FM1_TICKS_PER_US : 0u;             /* stub_us */
        v[k++] = ok ? (info >> 8) & 0xFFu : 0u;      /* stub_ver (0: no stub ran) */
        v[k++] = ok ? info & 0xFFu : 0u;             /* stub_decision: 1 boot (2 loader, 3 UBOOT never reach here) */
        v[k++] = ok ? fm1_stub_word(3) : 0u;         /* stub_detail: marker, loader << 8, rescues found << 16, key << 24 */
    }
#else
    for (i = 0; i < 5u; i++)                         /* (the emulator: no stub) */
        v[k++] = 0;
#endif
#if FELUCCA_UAC
    v[k++] = ua.cap_alt;
    v[k++] = ua_stage_on;
    v[k++] = ua.cap_ready;
    v[k++] = ua.tx_packets;
    v[k++] = ua.cap_underruns;
    v[k++] = ua.cap_overruns;
    v[k++] = ua.cw - ua.cr;
    v[k++] = (uint32_t)ua.cap_fill_q8;
    v[k++] = ua.missed_frames;
    v[k++] = ua.poll_max_ticks / FM1_TICKS_PER_US;
    v[k++] = ua.service_max_ticks / FM1_TICKS_PER_US;
    v[k++] = ua_dbg.svc_t5;
    v[k++] = ua_dbg.svc_isr;
    v[k++] = ua_dbg.isr_calls;
    v[k++] = ua_dbg.isr_gated;
    v[k++] = ua_dbg.fill_free;
    v[k++] = ua_dbg.fill_busy;
    v[k++] = ua_dbg.csr_or;
    v[k++] = ua_dbg.csr_last;
    v[k++] = ua_dbg.sofs;
    v[k++] = ua_dbg.frame_last;
    v[k++] = ua_dbg.armed_frame_dup;
    v[k++] = usb.resets;
    v[k++] = usb.suspended;
    v[k++] = usb.timeouts;
    v[k++] = usb.suspends;
    v[k++] = usb.sof_seen;
    v[k++] = ua_dbg.arm_gap_max / FM1_TICKS_PER_US;
    v[k++] = ua_dbg.arm_gap[0];
    v[k++] = ua_dbg.arm_gap[1];
    v[k++] = ua_dbg.arm_gap[2];
    v[k++] = ua_dbg.open_ms + (ua.cap_alt ? fm1_ms - ua_dbg.open_at : 0u);
    v[k++] = ua_dbg.opens;
    v[k++] = ua_dbg.arm_calls;
    v[k++] = ua_dbg.pk528;
    v[k++] = ua_dbg.pk540;
    v[k++] = ua_dbg.pk_other;
    v[k++] = ua_dbg.pk_other_last;
    for (i = 0; i < 6u; i++)
        v[k++] = ua_dbg.take_bin[i];
    v[k++] = ua_dbg.first_take_ms;
    v[k++] = ua_dbg.first_take_max;
    v[k++] = ua_dbg.open_close_ms;
    v[k++] = ua_dbg.holds;
    v[k++] = ua.cap_hold;
    v[k++] = UA_EP_MAXP;
    v[k++] = ua.cap_nudges_up;
    v[k++] = ua.cap_nudges_down;
    v[k++] = ua_dbg.frames_open;
    v[k++] = ua_dbg.frames_last_open;
    v[k++] = ua_dbg.isr_busy;
    v[k++] = ua_dbg.t5_busy;
    v[k++] = usb.setups;
    v[k++] = ua_dbg.stalls;
    v[k++] = ua_dbg.log_n;
    for (i = 0; i < 16u; i++) {                      /* the event log, oldest first */
        uint32_t e = (ua_dbg.log_n + i) & 15u;
        v[k++] = ua_dbg.log[e][0];
        v[k++] = ua_dbg.log[e][1];
    }
#endif
#ifdef FM1_HAVE_STUB                                 /* the stub's key samples (version 2 on: words 4..7 of its mailbox) */
    {
        uint32_t info = fm1_stub_word(1), ok = info >> 16 == 0x5342u && ((info >> 8) & 0xFFu) >= 2u;
        for (i = 4; i < 8u; i++)                     /* OCT-, OCT+, no column (bit n = sample n, 1 = closed), PA in */
            v[k++] = ok ? fm1_stub_word(i) : 0u;
    }
#else
    for (i = 0; i < 4u; i++)
        v[k++] = 0;
#endif
    crb_b(1);
    crb_b(k);
    for (i = 0; i < k; i++)
        crb_u32(v[i]);
}

/* ------------------------------------------------------------------ PEAK --- */
/* 78 PEAK (read-only; 0.18, cr_cpu1.h; fm1_install.py --debug prints it after 73's, DEBUG_PEAK names the values) ->
 * version 1, count, count x u32: the audio ISR's WORST CASES since the last PEAK read (reading clears them: play,
 * read, and the numbers are that stretch's): the largest whole half and each stage's (us; a half's budget is 2902),
 * the late halves and the latest one's largest stage and time, the halves counted, the fx stage's parts (the average
 * over the halves in which the effects ran, and the largest), then since power-on the flash's: erases, erases that
 * cut a sounding output (holes: 0 is the rule for what the unit writes by itself; a global save, SAVE held, into a
 * sounding output counts here too: the player's own press, 0.18), the settings' and the session's saves. A command of its own because
 * 73 DEBUG is full: its count is one 7-bit byte and it has 127 values. Values are only appended, and named by
 * tools/fm1_install.py DEBUG_PEAK (two branches' appends are merged by name: the dual-core fallback's 35..45 are
 * 69..79 here).
 * 3y44, values 35..41 (the owner's crackle on P5, docs/PROPHET.md "The crackle"): WHAT THE OVERLOAD SHED DID since the
 * last read (audio.c shed_check / shed_voice): voices shed, the latest one (0 none, else 0x10000 | part << 12 |
 * voice << 8 | note) and why (1 a released voice, 2 a held one, 3 asked and none to give), the halves above 85 %, the
 * time of the half that asked for the latest shed (us, its lag in it); then voice.c's: voices faded for another note
 * or part or shed (voice_kills) and own voices taken for a new note (voice_steals). A steady count of the last two
 * while a chord is only HELD would be a voice given up and taken again in a circle */
static void crb_peak(void)
{
    uint32_t v[80], k = 0, i;                       /* (79 values; 80 room) */
#ifdef FM1_CRASH_MAGIC                               /* (the device: hal/fm1_irq.h; the host and the emulator have no record) */
    int crashed = fm1_crash.magic == FM1_CRASH_MAGIC;
#else
    struct { uint32_t count, vec, pc, rets, emu, dbg, uptime_ms; } fm1_crash = {0, 0, 0, 0, 0, 0, 0};
    int crashed = 0;
#endif
    v[k++] = cr_dbg.pk_all;
    for (i = 0; i < CRP_N; i++)
        v[k++] = cr_dbg.pk[i] / CR_PROF_PER_US;
    v[k++] = cr_dbg.late_n;
    v[k++] = cr_dbg.late_stage;                      /* 0 none; 1 tick 2 part0 3 part1 4 fx 5 master 6 wait */
    v[k++] = cr_dbg.late_us;
    v[k++] = cr_dbg.pk_n;
    v[k++] = cr_dbg.x_busy;
    for (i = 0; i < CRX_N; i++) {
        v[k++] = cr_dbg.x_busy ? (uint32_t)(cr_dbg.x_sum[i] / cr_dbg.x_busy) / CR_PROF_PER_US : 0u;
        v[k++] = cr_dbg.x_pk[i] / CR_PROF_PER_US;
    }
    v[k++] = st_erases;
    v[k++] = st_holes;
#if CR_HAVE_SETTINGS
    v[k++] = crs_saves;
    v[k++] = css_saves;
#else
    v[k++] = 0;
    v[k++] = 0;
#endif
    /* 0.18's third build: when the ISR got to start (cr_cpu1.h), 30 values */
    v[k++] = cr_dbg.pk_lat / FM1_TICKS_PER_US;       /* the largest lag of an entry behind its boundary */
    v[k++] = cr_dbg.pk_wall / FM1_TICKS_PER_US;      /* the largest whole half, TIMER5 nested in it */
    v[k++] = cr_dbg.pk_t5 / FM1_TICKS_PER_US;        /* .. the most TIMER5 took of one half */
    v[k++] = cr_dbg.stale;                           /* halves in which the DAC played something old */
    v[k++] = cr_dbg.pk_t5gap / FM1_TICKS_PER_US;     /* TIMER5's largest gap (100: nothing masked the interrupts) */
    v[k++] = cr_dbg.pk_t5pc;                         /* .. and where it came back to */
    /* the keys' way to the engine (cr_out.c cr_klat; docs/INTEGRATION.md "Keys while a frame draws"), 34 values: the
     * note-ons timed, then us from the first scan that saw the key closed (its debounce, 1.1 .. 2.2 ms, in it): the
     * longest until the main loop posted the note-on, the average and the longest until the audio ISR applied it */
    v[k++] = cr_klat.n;
    v[k++] = CR_KLAT_US(cr_klat.pick);
    v[k++] = CR_KLAT_AVG_US();
    v[k++] = CR_KLAT_US(cr_klat.eng);
    cr_klat.n = cr_klat.sum = cr_klat.pick = cr_klat.eng = 0;
    v[k++] = shed_count - cr_dbg.shed0;              /* 3y44: the shed (above), 41 values */
    v[k++] = cr_dbg.shed_last;
    v[k++] = cr_dbg.shed_why;
    v[k++] = cr_dbg.shed_over_n;
    v[k++] = cr_dbg.shed_us;
    v[k++] = voice_kills - cr_dbg.kill0;
    v[k++] = voice_steals - cr_dbg.steal0;
    cr_dbg.shed0 = shed_count;
    cr_dbg.kill0 = voice_kills;
    cr_dbg.steal0 = voice_steals;
    cr_dbg.shed_last = cr_dbg.shed_why = cr_dbg.shed_over_n = cr_dbg.shed_us = 0;
    /* 3y46, values 42..45: P5 on two cores (cr_cpu1.h cr_dbg): the most P5 voices core 0 and core 1 rendered in one
     * block, the bass part's most P5 voices, held bass voices given up to the chord part. (Core 1's longest job of a
     * half is value 3, pk_part1_us: the bass part and core 1's share of the chord's voices) */
    v[k++] = cr_dbg.p5_c0;
    v[k++] = cr_dbg.p5_c1;
    v[k++] = cr_dbg.p5_bass;
#if FELUCCA_PROPHET
    v[k++] = p5_bass_gave - cr_dbg.gave0;
    cr_dbg.gave0 = p5_bass_gave;
#else
    v[k++] = 0;
#endif
    cr_dbg.p5_c0 = cr_dbg.p5_c1 = cr_dbg.p5_bass = 0;
    /* 3y48, values 46..53 (the owner's stutter on P5 with an arp, 2026-10-13: "So not sure why it wrote to flash"):
     * since power-on, never cleared: automatic erases (the Options' or favourites' poll) that started while something
     * sounded in the second before (the rule: 0), the latest erase's writer (storage_hw.c ST_WHY_*), its object
     * (storage.c OBJ_*, 0x100 + k a loop slot) and its time (fm1_ms; 0 none); then the time of irq_gap_us (value 28,
     * a peak: cleared with it), of ua_arm_gap_max_us (since power-on) and of the latest capture under- or overrun
     * (79 DEBUG's ua_underruns / ua_overruns: the USB stream restarted, a gap the host hears) */
    v[k++] = st_auto_loud;
    v[k++] = st_last_why;
    v[k++] = st_last_obj;
    v[k++] = st_last_ms;
    v[k++] = cr_dbg.pk_t5gap_ms;
#if FELUCCA_UAC
    v[k++] = ua_dbg.arm_gap_ms;
    v[k++] = ua.cap_reset_ms;
#else
    v[k++] = 0;
    v[k++] = 0;
#endif
    v[k++] = fm1_ms;                                 /* (the clock the times above are on) */
    /* 3y50, values 55..61 (cr_cpu1.h cr_dbg; docs/FX.md "Each core's half"): since the last read, the largest half's
     * busy time of core 0 (its stages but its waits, without core 1's job) and of core 1 (its voices and the reverb),
     * core 0's waits in one half (both joins: pk_wait_us's), the effects' join alone, core 1 idle between its two
     * jobs; which core ran which effect in the largest whole half (1 chorus, 2 delay, 4 reverb ran, 8 reverb on
     * core 1); the reverb jobs core 1 ran */
    v[k++] = cr_dbg.pk_c0_busy / CR_PROF_PER_US;
    v[k++] = cr_dbg.pk_c1_busy / CR_PROF_PER_US;
    v[k++] = cr_dbg.pk_w0 / CR_PROF_PER_US;
    v[k++] = cr_dbg.pk_fxw / CR_PROF_PER_US;
    v[k++] = cr_dbg.pk_w1 / CR_PROF_PER_US;
    v[k++] = cr_dbg.pk_fxmap;
    v[k++] = cr_dbg.c1x_jobs;
    /* 3y52, values 61..64: the quiet writes (0.22, cr_settings.c, storage.c "erase ahead"; p5-g e31c096's values
     * 35..38 there, merged by name), all since power-on: the saves the unit did by itself (an Option's, a
     * favourite's), the erases done ahead (the session's and the favourites', not the settings': at power-on, after a
     * long idle), the erases the unit started by itself while a voice was active or within 2 s of an input (the rule:
     * 0), the longest page program in us (the wait for the chip, the interrupts masked) */
#if CR_HAVE_SETTINGS
    v[k++] = crq.auto_saves;
    v[k++] = crq.ahead;
#else
    v[k++] = 0;
    v[k++] = 0;
#endif
    v[k++] = st_near;
    v[k++] = st_prog_us;
    /* 3y60, values 65..68 (the owner, 2026-10-14: "with all the user presets filled, my FM-1's UI is now very laggy";
     * docs/PRESETS.md "What browsing reads"), since the last read: a UI frame's own time (input, LEDs, draw: audio.c
     * ui_ft) the longest and the average in us, then the flash reads (storage_hw.c st_read) per second, calls and
     * bytes. Browsing reads no record: a full pool turned reads a sound's record and patch per load, no more */
    {
        static uint32_t rd0, rb0, ms0;
        uint32_t dt = fm1_ms - ms0;
        v[k++] = ui_ft.max_us;
        v[k++] = ui_ft.n ? ui_ft.sum_us / ui_ft.n : 0u;
        v[k++] = dt ? (uint32_t)((uint64_t)(st_reads - rd0) * 1000u / dt) : 0u;
        v[k++] = dt ? (uint32_t)((uint64_t)(st_read_bytes - rb0) * 1000u / dt) : 0u;
        rd0 = st_reads;
        rb0 = st_read_bytes;
        ms0 = fm1_ms;
        ui_ft.max_us = ui_ft.sum_us = ui_ft.n = 0;
    }
    /* 3y70 / 3y72 / 3y73, values 69..79 (the dual-core fallback's branch, its 35..45, merged by name): why this boot
     * kept core 1 off, then the last crash's record (7: count, vec, pc, rets, EMU_MSG, DBG_MSG, uptime ms), this
     * boot's reset reason and the last reset's breadcrumb, the voices the shed dropped since power-on */
    v[k++] = cr_c1_barred;                           /* why this boot kept core 1 off (cr_cpu1.h C1_BAR_*: 1 a crash beside
                                                      * it, 2 a hang, 3 gave up; 0 none): 73 DEBUG is full */
    /* the last crash's record (hal/fm1_irq.h fm1_crash, .noinit: what the crash screen showed for 4 s, kept until a
     * power-off; count 0: none since) and this boot's reset reason and the breadcrumb the last reset left
     * (cr_bootguard.h): what a reporter cannot read off the screen in time (ChoralRoot#9) */
    v[k++] = crashed ? fm1_crash.count : 0u;
    v[k++] = crashed ? fm1_crash.vec : 0u;
    v[k++] = crashed ? fm1_crash.pc : 0u;
    v[k++] = crashed ? fm1_crash.rets : 0u;
    v[k++] = crashed ? fm1_crash.emu : 0u;
    v[k++] = crashed ? fm1_crash.dbg : 0u;
    v[k++] = crashed ? fm1_crash.uptime_ms : 0u;
    v[k++] = bootguard.reason;                       /* P3_RST_SRC bits 0..7, the core reset as bit 8 (bootguard_reason) */
    v[k++] = felucca_dbg.prev_stage;                 /* BS_* (bootguard_stage_name) */
    v[k++] = shed_count;                             /* voices dropped by the overload shed since power-on (audio.c
                                                      * shed_voice: two halves in a row over 85 %); console voices_shed
                                                      * (value 35, shed_voices, is since the last read) */
    cr_prof_clear();
    crb_b(1);
    crb_b(k);
    for (i = 0; i < k; i++)
        crb_u32(v[i]);
}

/* ------------------------------------------------------------------- USB --- */
/* 79 USB (read-only; 0.19: 73 DEBUG is full) -> version 1, count, count x u32: ChoralRoot In's rate and what 48 kHz
 * adds to 73's counters, named in this order by tools/fm1_install.py --debug (DEBUG_USB) and read by
 * tools/uac_check.py; reading clears nothing and values are only ever appended. The rate in force (44100 / 48000) and
 * the last whole open's (0 before one: what ua_frames_last_open counts frames of), SET_CURs seen and those that
 * changed the rate, the 48 kHz packets armed by size (48 frames; 47 and 49, the drift's), then the resampler in the
 * audio ISR: blocks, the recent average and the longest time of one block in us (TIMER5 nested in it included; four
 * blocks a half), and the largest packet's bytes; then Options > USB Rate (2026-10-12): 1 = 48 kHz is offered (0: the
 * device is 0.18's, 44.1 kHz only), the SET_CURs for 48 kHz that came while it was not, and the times the FM-1 left
 * the bus for a USB option */
#if FELUCCA_UAC
static void crb_usb(void)
{
    uint32_t v[16], k = 0, i;
    v[k++] = ua.r48 ? UA_RATE48 : UA_RATE;
    v[k++] = !ua_dbg.open_close_ms ? 0u : ua_dbg.rate_last_open ? UA_RATE48 : UA_RATE;
    v[k++] = ua.rate_sets;
    v[k++] = ua.rate_changes;
    v[k++] = ua_dbg.pk576;
    v[k++] = ua_dbg.pk564;
    v[k++] = ua_dbg.pk588;
    v[k++] = ua.rs_blocks;
    v[k++] = ua.rs_blocks ? ua.rs_ticks / ua.rs_blocks / FM1_TICKS_PER_US : 0u;
    v[k++] = ua.rs_max_ticks / FM1_TICKS_PER_US;
    v[k++] = UA_PACKET;
    v[k++] = (ua_off & UA_OFF_48) ? 0u : 1u;
    v[k++] = ua_dbg.rate_refused;
    v[k++] = ua_replugs;
    crb_b(1);
    crb_b(k);
    for (i = 0; i < k; i++)
        crb_u32(v[i]);
}
#endif

/* ------------------------------------------------------------------ STUB --- */
/* 77 STUB op -> rc: a request to the boot stub (firmware/stub/stub_core.h, docs/BOOT-SAFETY.md), left in its rescue
 * word, and a restart. op 1 TEST: the stub boots as usual and checks the staged loader on the way (73 DEBUG's
 * stub_detail then carries the loader's code: the rescue's read, without a rescue). op 2 RESCUE: as if OCT- were
 * held at power-on: the staged loader (update mode, ota-FM-1) if it passes the check, else the ROM's USB boot; left
 * by an install, or by switching off and on. rc 0 accepted (the restart about 150 ms later, and not before the host
 * has taken the reply: crb_leave), 1 a bad op, 3 busy (EDIT or SAVE open, a loop being recorded), 127 no stub of
 * version 2 or later ran before this boot (the emulator; a firmware started some other way) */
#define CRB_STUB_REQ_TEST 0x54514552u                /* stub_core.h STUB_REQ_TEST, STUB_REQ_RESCUE (cr_backup_test) */
#define CRB_STUB_REQ_RESCUE 0x52514552u
#ifndef CRB_STUB_VER
#ifdef FM1_HAVE_STUB
#define CRB_STUB_VER() (fm1_stub_word(1) >> 16 == 0x5342u ? (fm1_stub_word(1) >> 8) & 0xFFu : 0u)
#define CRB_STUB_GO(w) do { bootguard.pending = 0; usb_detach(); fm1_delay_ms(30); fm1_stub_request(w); } while (0)
#else
#define CRB_STUB_VER() 0u
#define CRB_STUB_GO(w) ((void)(w))
#endif
#endif
/* ---- the update gesture: OCT- + OCT+ + HOME held 5 s (the owner, 2026-10-11: "less accidental" than the two OCT keys
 * alone, which now do nothing held beyond their PANIC). Every main-loop pass (main.c; the emulator's emu_fw_frame),
 * SAFE MODE too. From 2 s a countdown over the whole screen; any of the three let go cancels. At 5 s the audio is
 * stopped and the automatic part of the settings is written (cr_settings_save: the Options; not in SAFE MODE). The
 * manual fields, the session and a loop in RAM are NOT saved: the update is a power cycle, they come back as last
 * saved (SAVE held). Then
 *   a boot stub of version 2 or later ran before this boot: its RESCUE request, the restart 77 STUB op 2 asks for
 *     (CRB_STUB_GO: USB detached, the boot guard told): the stub hands over to the staged update loader (three
 *     flashes: update mode, what the web installer and tools/fm1_install.py talk to) or, with none staged, to the
 *     ROM's USB boot (one long flash): the stub's own rule (firmware/stub/stub_core.h);
 *   no such stub (the firmware was started by another loader): the ROM's USB boot directly, as the gesture did
 *     until 0.17, and the screen says "USB boot" instead of "update mode".
 * The buttons are read as main.c read them (fm1_in.buttons through the panel table), not from the UI's scan */
#ifndef CRB_UPDATE_SCREEN
#ifdef FM1_HAVE_STUB
#define CRB_UPDATE_SCREEN(t) do { fm1_audio_stop(); lcd_fill(0, 0, 240, 240, T_BG); \
                                  draw_text_box(0, 110, 240, CR_FONT_M, (t), T_THEME, 1); } while (0)
#define CRB_UBOOT_GO() do { usb_detach(); fm1_delay_ms(30); bootguard_intentional(&bootguard); fm1_enter_uboot(); } while (0)
#else
#define CRB_UPDATE_SCREEN(t) ((void)(t))
#define CRB_UBOOT_GO() ((void)0)
#endif
#endif
#define CRB_UPD_SHOW_MS 2000u
#define CRB_UPD_GO_MS 5000u
static struct { uint32_t t0; uint8_t left, gone; } crb_upd;
static void cr_update_poll(void)
{
    uint32_t all = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_HOME]);
    uint32_t held = fm1_ms - crb_upd.t0, stub = CRB_STUB_VER() >= 2u;
    if ((fm1_in.buttons & all) != all) {
        if (crb_upd.left) {
            crb_upd.left = 0;
            ui.uboot = 0;
            ui.force = 1;                            /* what the countdown covered */
            cu_message(stub ? "update cancelled" : "USB boot cancelled", CR_COL_WHITE);
            cu_trace("update: cancelled\n");
        }
        crb_upd.gone = 0;
        crb_upd.t0 = fm1_ms;
        return;
    }
    if (crb_upd.gone || held <= CRB_UPD_SHOW_MS)     /* (gone: a host test's hooks returned; until the keys are let go) */
        return;
    if (held < CRB_UPD_GO_MS) {                      /* 3, 2, 1 */
        uint32_t left = (CRB_UPD_GO_MS - held + 999u) / 1000u;
        if (left != crb_upd.left) {
            crb_upd.left = (uint8_t)left;
            ui.uboot = (uint8_t)left;                /* (Felucca's flag: main.c's breadcrumb of the countdown) */
            ui.force = 1;
            cu_trace("update: %u (%s)\n", (unsigned)left, stub ? "update mode" : "USB boot");
        }
        cu.msg.until = cu_now() + 100u;              /* the countdown, over everything, redrawn each frame */
        cu.msg.big = 1;
        cu.msg.col = CR_COL_RED;
        cu.msg.text[0] = (char)('0' + left % 10u);
        cu.msg.text[1] = 0;
        cu_cpy(cu.msg.label, stub ? "update mode: let go to cancel" : "USB boot: let go to cancel", sizeof cu.msg.label);
        return;
    }
    crb_upd.gone = 1;
    crb_upd.left = 0;
    ui.uboot = 0;
    cu.msg.until = 0;
    cu_trace("update: go (%s)\n", stub ? "the stub's rescue request" : "no stub: the ROM's USB boot");
    CRB_UPDATE_SCREEN(stub ? "UPDATE MODE" : "USB BOOT");
#if CR_HAVE_SETTINGS
    if (!cr_safe)
        cr_settings_save();                          /* the Options as they are (automatic); the rest comes back as
                                                      * last saved: the update is a power cycle, no global save (0.18) */
#endif
    if (stub)
        CRB_STUB_GO(CRB_STUB_REQ_RESCUE);            /* (never returns on the unit) */
    else
        CRB_UBOOT_GO();
}

/* a restart is owed after this reply (72 RESTART: w 0; 77 STUB: w its request): ed_service does it (crb_leave). Asked
 * again while one is owed: the newer request says what is left for the stub and which reply is waited for, the
 * clock stays the first one's (the bound cannot be pushed back) */
static void crb_reboot_ask(uint32_t w)
{
    crb.put = crb.listed = 0;
    crb.stub = w;
    if (!crb.reboot)
        crb.reboot_ms = fm1_ms;
    crb.reboot = 1;
}
static uint32_t crb_stub(uint32_t op)
{
    if (CRB_STUB_VER() < 2u)
        return 127;
    if (op != 1u && op != 2u)
        return 1;
    if (cu.page == PG_EDIT || cu.page == PG_SAVE || cr_snap.lcap != CRL_CAP_NONE || crl_stage_busy)
        return 3;
    crb_reboot_ask(op == 1u ? CRB_STUB_REQ_TEST : CRB_STUB_REQ_RESCUE);
    return 0;
}

/* ------------------------------------------------------------------ CORE1_TEST --- */
/* 75 CORE1_TEST op [args] -> op, rc, count, count x u32 (crb_u32); a CR_CORE1_TEST build only (tools/core1_fault_test.py;
 * any other build answers the single byte 127: unsupported). After Melodee PR 21's editor command 79 (Kerem Kilic /
 * Melodee, GPL-3.0). ops: 0 report; 1 a misread (the sequence read as +0x00200000 for 1 ms, as an affected unit
 * reads it); 2 a null job at rest (fn 0 and the next sequence for 1 ms: the idle crash Melodee decoded); 3 a stall (a
 * job that never returns: the 8 ms wait); 4 a fault on core 1 (a job that calls address 0); 5 the rails; 6 rail a[1]
 * (0 SYSVDD, 1 VDC14) to level a[2], down too (SYSVDD 6..14, VDC14 0..5: never above the cap); 7 the bar: the record
 * a crash beside core 1 leaves (fm1_c1_fault_c: C1_BAR_CRASH), then the restart 72 RESTART does (a soft reset, so the
 * next boot decides from the reason, as after a real crash report: core 1 off, Dual Core saved Off; cr_cpu1.h
 * cr_c1_bar_boot); its reply goes out first, but the unit leaves the bus 150 .. 500 ms after it (a tool must not
 * count on reading it). rc: 0 done, 1 refused (core 1 not running for 1..4 and 7, a level out of range for 6).
 * Values: c1 state, jobs, rejected, faults, stalls, fallback, SYSVDD boot / now, VDC14 boot / now (the registers), the
 * fault's core / DBG_MSG / pc / rets / EMU_MSG, the last misread, audio halves, the injection's time in us.
 * An injected failure is not the unit's: ops 1..4 clear the bar's reason and set cr_c1_injected (no Dual Core Off
 * saved for a give-up this session: cr_ui.c) */
#if CR_CPU1 && CR_CORE1_TEST && defined(FM1_HAVE_CPU1)
static void crb_core1_test(const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 0u, rc = 0, waited = 0, v[18], i;
    if (((op >= 1u && op <= 4u) || op == 7u) && cr_dbg.c1 != C1_OK) {
        rc = 1;
    } else if (op == 7u) {
        cr_c1_bar_set(C1_BAR_CRASH);                 /* as fm1_c1_fault_c leaves it before the crash report */
        crb_reboot_ask(0);                           /* .. and 72 RESTART's soft reset (crb_leave, after this reply) */
    } else if (op >= 1u && op <= 4u) {
        cr_c1_injected = 1;                          /* (before: the audio ISR may give up during the injection) */
        uint32_t t0 = fm1_ticks();
        fm1_irq_off();                               /* the audio ISR neither hands over nor checks meanwhile */
        if (op <= 2u) {
            fm1_c1_test_misread(op == 2u);
            (void)cr_c1_check();
        } else if (cr_c1_run(op == 3u ? fm1_c1_test_stall : fm1_c1_test_fault, 0)) {
            rc = cr_c1_wait() ? 0u : 2u;             /* 2: it "finished" (it should not) */
        }
        waited = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        fm1_irq_on();                                /* (an exception held while masked is taken here) */
        fm1_delay_us(2000);
        cr_c1_bar_set(C1_BAR_NONE);                  /* an injected failure is not the unit's: no bar at the next boot
                                                      * (the tool restarts with core 1 On, cr_cpu1.h cr_c1_bar_boot) */
    } else if (op == 6u) {
        fm1_irq_off();
        rc = na == 3u ? (uint32_t)fm1_power_goto(a[1], a[2]) : 1u;
        fm1_irq_on();
        cr_rails_now();
    } else if (op != 0u && op != 5u) {
        rc = 1;
    }
    v[0] = cr_dbg.c1;
    v[1] = cr_dbg.c1_jobs;
    v[2] = fm1_c1_rejected;
    v[3] = fm1_c1_fault.count;
    v[4] = fm1_c1_stalls;
    v[5] = cr_dbg.c1_fallback;
    fm1_irq_off();
    v[6] = fm1_power.boot[FM1_RAIL_SYSVDD];
    v[7] = fm1_power_get(FM1_RAIL_SYSVDD);
    v[8] = fm1_power.boot[FM1_RAIL_VDC14];
    v[9] = fm1_power_get(FM1_RAIL_VDC14);
    fm1_irq_on();
    v[10] = fm1_c1_fault.cpu;
    v[11] = fm1_c1_fault.dbg;
    v[12] = fm1_c1_fault.pc;
    v[13] = fm1_c1_fault.rets;
    v[14] = fm1_c1_fault.emu;
    v[15] = fm1_c1_misread;
#ifdef DBG_MAGIC
    v[16] = felucca_dbg.halves;
#else
    v[16] = 0;
#endif
    v[17] = waited;
    crb_b(op);
    crb_b(rc);
    crb_b(18);
    for (i = 0; i < 18u; i++)
        crb_u32(v[i]);
}
#else
static void crb_core1_test(const uint8_t *a, uint32_t na) { (void)a; (void)na; crb_b(127); }   /* unsupported */
#endif

/* ------------------------------------------------------------------ the dispatcher --- */
/* f: the bytes between F0 and F7 (7D 46 4C cmd args); 1 = answered */
static int crb_handle(const uint8_t *f, uint32_t n)
{
    const uint8_t *a = f + 4;
    uint32_t cmd, na, from = 5u;
    if (n < 4u || f[0] != CRB_HDR0 || f[1] != CRB_HDR1 || f[2] != CRB_HDR2)
        return 0;
    cmd = f[3];
    na = n - 4u;
    crb_n = from;
    switch (cmd) {
    case CRB_INFO: {                                 /* Felucca's INFO layout, no editor: 0 engines, 0 tracks */
        const char *v = CR_VERSION;                  /* (cr_ui.h: the one version string) */
        uint32_t i;
        if (na)
            return 0;
        for (i = 0; v[i] && i < CRB_VER_MAX; i++)    /* a beta single-core string is 28 (hosts read to the NUL) */
            crb_b((uint8_t)v[i]);
        crb_b(0);
        for (i = 0; i < 7u; i++)
            crb_b(0);                                /* NENGINES P_COUNT G_COUNT NSTEP P_E0, NTRK, CHAIN_ROWS */
        crb_b(0x42);
        crb_b(1);
        crb_b(3);                                    /* backup read + restore */
        crb_b(0x43);
        crb_b(1);
        crb_b(3);                                    /* ChoralRoot backup v3 (0.22): the pool's ids 50..62 (v2: ids 9,
                                                      * 40..49, RESTART; no samples). (0.22 .. 0.29 also had 80 IMPORT and
                                                      * took 0.21's sound objects; no host reads this number) */
        if (cr_unit_id[0]) {                         /* 49 08: the unit's ID, 8 hex digits in ASCII (3y66, cr_ui.c
                                                      * cr_unit_id; none known: no tag, the reply as before). Appended:
                                                      * the hosts read the version to its NUL, and no host reads a tag
                                                      * by its place from the end */
            crb_b(0x49);
            crb_b(8);
            for (i = 0; i < 8u; i++)
                crb_b((uint8_t)cr_unit_id[i]);
        }
        break;
    }
    case CRB_LIST:
        if (na) {
            crb_b(1);
            crb_b(1);
            crb_b(0);
        } else {
            crb_list();
        }
        break;
    case CRB_GET:
        crb_get(a, na);
        break;
    case CRB_PUT: {
        uint32_t rc = crb_put(a, na);
        crb_b(na ? a[0] : 127u);
        crb_b(na >= 2u ? a[1] : 127u);
        crb_b(rc);
        break;
    }
    case CRB_DEBUG:
        if (na)
            return 0;
        crb_debug();
        break;
    case CRB_SCREEN:                                 /* (not a backup object) */
        if (na != 1u || a[0] > 1u)
            return 0;
        cu_screen_sysex(a[0]);
        crb_b(0);
        break;
    case CRB_CORE1_TEST:                             /* (a test build's; else "unsupported") */
        crb_core1_test(a, na);
        break;
    case CRB_SELECT:                                 /* (the device manager's audition) */
        if (na != 3u && na != 4u)
            return 0;
        crb_b(crb_select(a, na));
        break;
    case CRB_PEAK:                                   /* (the audio ISR's worst cases since the last read) */
        if (na)
            return 0;
        crb_peak();
        break;
#if FELUCCA_UAC
    case CRB_USB:                                    /* (ChoralRoot In's rate and its 48 kHz counters) */
        if (na)
            return 0;
        crb_usb();
        break;
#endif
#if CR_BLE
    case CRB_BLE_TEST:                               /* (a BLE_DIAG build: the bisection's mask, next power-on) */
        if (na != 1u)
            return 0;
        crb_b(cr_ble_set_test(a[0]));
        break;
    case CRB_BLE:                                    /* (Bluetooth: the radio's state, cr_ble.c cr_ble_debug, 3y238) */
        if (na)
            return 0;
        {
            uint32_t v[CRBL_DBG_N], k = cr_ble_debug(v), i;
            crb_b(1);
            crb_b(k);
            for (i = 0; i < k; i++)
                crb_u32(v[i]);
        }
        break;
#endif
    case CRB_STUB:                                   /* (the boot stub's test and rescue requests) */
        if (na != 1u)
            return 0;
        crb_b(crb_stub(a[0]));
        break;
    case CRB_RESTART:
        if (na)
            return 0;
        crb_b(0);
        crb_reboot_ask(0);                           /* ed_service restarts once the host has the reply (crb_leave) */
        break;
    default:
        return 0;
    }
    crb_send(cmd, from);
    return 1;
}

#if FELUCCA_OTA
#ifndef CRB_REBOOT
#define CRB_REBOOT() do { bootguard.pending = 0; usb_detach(); fm1_delay_ms(30); fm1_reboot(); } while (0)
#endif
#define CR_BACKUP_SERVICE 1                          /* cr_shim.c: no ed_service stub */
/* The restart a reply promised (72 RESTART, 77 STUB). usb_detach takes along whatever of the reply the host has not
 * taken, so the restart waits for the fact, not for a time: the reply's last USB packet taken by the host (usb.c
 * ota_wire_gone: TXCSR1's TxPktRdy seen clear after it; clock and notes sent behind it are not waited for), then
 *   CRB_LEAVE_GRACE  5 ms more (ticks of fm1_ms: four whole USB frames at least). A host controller hands a packet
 *                    it has taken to its driver at its next interrupt, a frame (1 ms) later at most (the frame's end;
 *                    EHCI and xHCI hold their interrupts back 1 ms by default); our leaving is a port change that
 *                    must not overtake those bytes on their way up the host's MIDI stack: four frames for the one;
 *   CRB_LEAVE_MIN    and not before 150 ms from the request, as before and as web/EDITOR_PROTOCOL.md says: the
 *                    tools close their ports in that time, while the FM-1 is still there (tools/fm1_install.py);
 *   CRB_LEAVE_MAX    500 ms from the request at the latest, taken or not: a host that has stopped reading (or is
 *                    gone: no configuration, the SIE silent) does not keep the unit from restarting.
 * Before (to 0.16): "150 ms from the request and the ring empty", the request's time kept as fm1_ms | 1. On an
 * even millisecond that is one ahead: the unsigned difference read 0xFFFFFFFF until the next tick, the 150 ms
 * counted as over, and the unit left as soon as usb_poll had put the reply into the endpoint, before the host's IN
 * token: the reply lost, the unit restarted all the same. One request in four by its phases (an even millisecond,
 * and the poll that loads the reply before the next tick: tests/cr_restart_test.c on that code); the unit had
 * shown 3 in 13 */
#define CRB_LEAVE_GRACE 5u
#define CRB_LEAVE_MIN 150u
#define CRB_LEAVE_MAX 500u
static void crb_leave(void)
{
    uint32_t now = fm1_ms, age = now - crb.reboot_ms;
    if (crb.reboot == 1u) {                          /* (crb_handle has returned: the reply is in the ring) */
        crb.mark = ota_wire_mark();
        crb.reboot = 2;
    }
    if (crb.reboot == 2u && ota_wire_gone(crb.mark)) {
        crb.gone_ms = now;
        crb.reboot = 3;
    }
    if (age < CRB_LEAVE_MAX && (crb.reboot != 3u || age < CRB_LEAVE_MIN || now - crb.gone_ms < CRB_LEAVE_GRACE))
        return;
    crb.reboot = 0;                                  /* (once: a host test's hooks return) */
    if (crb.stub)
        CRB_STUB_GO(crb.stub);                       /* (77 STUB: the request, a core reset; never returns on the unit) */
    CRB_REBOOT();
}
/* main.c's main loop: our frames (7D 46 4C), anything else stays for ota_service (M-UPGRADE) */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (crb.reboot)
        crb_leave();
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != CRB_HDR0 || p[1] != CRB_HDR1 || p[2] != CRB_HDR2)
        return;
    crb_handle(p, n);                                /* (in place: the frame is released after the reply) */
    ota_frame_done();
}
#endif
#endif /* FELUCCA_FLASH */
