/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: backup and restore over USB-MIDI SysEx (web/EDITOR_PROTOCOL.md, "ChoralRoot: backup and
 * restore"). The subset of Felucca's protocol (editor.c / editor_backup.c, dropped with Felucca's UI) that the
 * installer page (web/fm1backup.js) and tools/fm1_install.py use, on ChoralRoot's objects:
 *
 *   1 INFO      "ChoralRoot <version>", no editor (0 engines), tags 42 01 03 (backup read + restore) and 43 01 02
 *   65 LIST     1, rc, count, per object: id, size u32, crc u32 (CRC-32 zlib of the bytes GET returns)
 *   66 GET      id, offset u32, count (<= 256) -> id, rc, offset u32, count, pack7 data
 *   67 PUT      0 begin (id, size u32, crc u32) / 1 data (id, offset u32, pack7) / 2 commit (id) / 3 abort (id)
 *   72 RESTART  -> rc; the device restarts (everything restored loads from flash)
 *   73 DEBUG    -> version 1, count, count x u32: the diagnostic counters (read-only)
 *   74 SCREEN   0 off / 1 on -> rc 0: the screen off (DISPOFF, the backlight off, nothing drawn) until 1 or a key,
 *               button or knob on the unit (MIDI in does not wake it), as the idle timer's (cr_ui.c cu_screen_sysex)
 *
 * Objects (Felucca's ids where Felucca has them; 9 and 40..49 are ChoralRoot's):
 *   1 settings (persist_t PER5: Felucca's fields + cr_settings_t; a PUT takes PER1..PER5, settings_persist.c migrates)
 *   6, 7 user sound banks (upreset.c, 2 x 16 records), 8 the FM6 patch bank (fm6_bank.c: Melodee's 32-voice layout;
 *   Felucca 1.0's 27 and Melodee's earlier bank restore too, converted at the next boot as at power-on), 9 the VA patch
 *   store (va_store.c), 10, 11 the FM6 patch store (fm6_ustore.c: user slots 1..16, 17..32), 12, 13 the CZ-1 tone store
 *   (cz_ustore.c: user slots 1..16, 17..32), 14..21 the CZ-1 banks A..H (cz_bank.c: one object a bank, as Melodee's
 *   backup ids 9..16; a bank never saved has no object: its default, Casio's tones or empty), 22 the QUAD patch store
 *   (quad_store.c, FELUCCA_QUAD), 40..49 loop slots 1..10
 *   (cr_ui.c's flash records, docs/LOOPER.md). No sample objects: ChoralRoot has
 *   no SAMPLE engine (FELUCCA_SAMPLE 0); Felucca's 32..34 and SMP_BEGIN .. SMP_ERASE (11..14) are not answered, so the
 *   installer page reports a Felucca archive's samples (and a ChoralRoot 0.1 archive's 32 / 33) skipped. The flash of
 *   user sample slots 1 and 2 holds the CZ-1 objects 13..21 (0xA0000..0xB1FFF) and QUAD's 22 (0xB2000 / 0xB3000), the
 *   rest stays free (storage.c's map); slot 3's the loops.
 *
 * Reads come from flash (the current copy of each A/B pair, checked at LIST) in 256-byte st_read windows: no RAM
 * copy, the audio keeps running (a loop may play). LIST first saves a pending settings change (cr_settings_save,
 * not while a loop plays). Writes are staged in cr_ui.c's cu_loop_buf (the largest object, a loop record, fits;
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
#if !FELUCCA_VA || !CR_HAVE_SETTINGS
#error "cr_backup.c: the VA store (FELUCCA_VA) and the settings record (cr_settings.c) are part of the backup"
#endif
#define CRB_HDR0 0x7Du
#define CRB_HDR1 0x46u
#define CRB_HDR2 0x4Cu
enum { CRB_INFO = 1, CRB_LIST = 65, CRB_GET, CRB_PUT, CRB_RESTART = 72, CRB_DEBUG = 73, CRB_SCREEN };
#define CRB_LOOP0 40u                                /* loop slot k: id 40 + k */
#define CRB_VA 9u
#define CRB_FM6S 10u                                 /* 10, 11: the FM6 patch store's halves */
#if FELUCCA_CZ
#define CRB_CZS 12u                                  /* 12, 13: the CZ-1 tone store's halves */
#define CRB_CZB 14u                                  /* 14..21: the CZ-1 banks A..H */
#if FELUCCA_QUAD
#define CRB_QUAD 22u                                 /* 22: the QUAD patch store */
static const uint8_t CRB_IDS[] = {1, 6, 7, 8, CRB_VA, CRB_FM6S, CRB_FM6S + 1u, CRB_CZS, CRB_CZS + 1u, 14, 15, 16, 17, 18, 19,
                                  20, 21, CRB_QUAD, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
_Static_assert(sizeof(quad_store_t) <= CRL_REC_MAX, "backup: the QUAD store stages too");
#else
static const uint8_t CRB_IDS[] = {1, 6, 7, 8, CRB_VA, CRB_FM6S, CRB_FM6S + 1u, CRB_CZS, CRB_CZS + 1u, 14, 15, 16, 17, 18, 19,
                                  20, 21, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
#endif
_Static_assert(sizeof(czu_t) <= CRL_REC_MAX && sizeof(cz_bank_t) <= CRL_REC_MAX, "backup: the CZ-1 objects stage too");
static int crb_is_cz(uint32_t id) { return id >= CRB_CZS && id < CRB_CZB + CZ_BANK_N; }
#else
static const uint8_t CRB_IDS[] = {1, 6, 7, 8, CRB_VA, CRB_FM6S, CRB_FM6S + 1u, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
static int crb_is_cz(uint32_t id) { (void)id; return 0; }
#endif
#define CRB_N ((uint32_t)sizeof CRB_IDS)
_Static_assert(CRL_SLOTS == 10u, "backup ids: 10 loop slots");
_Static_assert(sizeof(persist_t) <= CRL_REC_MAX && sizeof(up_bank_t) <= CRL_REC_MAX && sizeof(fm6_bank_t) <= CRL_REC_MAX &&
               sizeof(va_store_t) <= CRL_REC_MAX && sizeof(fm6u_t) <= CRL_REC_MAX, "backup: every object stages in cu_loop_buf");
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
    uint32_t usb, len, crc, pos, ms, reboot;         /* usb.resets at LIST / begin; the PUT; restart at (ms | 1) */
} crb;

/* ------------------------------------------------------------------ the reply --- */
#define CRB_OUT st_buf                               /* the reply (<= 400 bytes, written after any storage call) .. */
#define CRB_DATA (st_buf + 2048u)                    /* .. and GET's flash data */
static uint32_t crb_n;
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
static uint32_t crb_obj(uint32_t id)                 /* storage.c's object of ids 1, 6..22 */
{
#if FELUCCA_QUAD
    if (id == CRB_QUAD)
        return OBJ_QUADSTORE;
#endif
#if FELUCCA_CZ
    if (crb_is_cz(id))
        return id >= CRB_CZB ? (uint32_t)OBJ_CZBANK0 + id - CRB_CZB : czu_obj(id - CRB_CZS);
#endif
    return id == 1u ? (uint32_t)OBJ_SETTINGS : id == 8u ? (uint32_t)OBJ_FM6BANK : id == CRB_VA ? (uint32_t)OBJ_VASTORE
         : id >= CRB_FM6S ? (uint32_t)OBJ_FM6STORE0 + id - CRB_FM6S : (uint32_t)OBJ_UPRESET0 + id - 6u;
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
static uint32_t crb_sector(uint32_t id, uint32_t copy)
{
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
#if CR_HAVE_SETTINGS
    if (crs_loaded && !CR_SETTINGS_BUSY())
        cr_settings_save();                          /* a change not yet saved goes in first (none: no erase) */
#endif
    for (i = 0; i < CRB_N; i++) {
        uint32_t id = CRB_IDS[i];
        crb.copy[i] = -1;
        crb.size[i] = crc[i] = 0;
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
    if (id == 6u || id == 7u)
        return !len || len == sizeof(up_bank_t);
    if (id == 8u)
        return !len || len == sizeof(fm6_bank_t) || len == CRB_FM6_FEL || len == CRB_FM6_MEL;
    if (id == CRB_FM6S || id == CRB_FM6S + 1u)
        return !len || len == sizeof(fm6u_t);
#if FELUCCA_CZ
    if (crb_is_cz(id))
        return !len || len == (id >= CRB_CZB ? sizeof(cz_bank_t) : sizeof(czu_t));
#endif
#if FELUCCA_QUAD
    if (id == CRB_QUAD)
        return !len || len == sizeof(quad_store_t);
#endif
    if (id == CRB_VA)                                /* version 3 / 2, or version 1's 104-byte blobs */
        return !len || len == sizeof(va_store_t) || len == 16u + UP_SLOTS * VA_BLOB1;
    if (crb_is_loop(id))
        return !len || (len >= CRL_REC_HDR && len <= CRL_REC_MAX);
    return 0;
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
        if (st_save(OBJ_SETTINGS, p, sizeof *p))
            return 4;
#if CR_HAVE_SETTINGS
        crs_rec = *p;                                /* what flash holds now */
        crs_seen = p->cr;
        crs_pending = 0;
#endif
        cr_restore_lock = 1;                         /* no settings save before the restart */
        cu_message("settings restored: restart", CR_COL_GREEN);
        return 0;
    }
    if (id == 6u || id == 7u) {
        const up_bank_t *b = (const up_bank_t *)raw;
        if (len && (b->magic != UP_BANK_MAGIC || b->rsize != sizeof(up_rec_t) || b->nslot != UP_PER_BANK))
            return 2;
    } else if (id == 8u) {
        if (len && !(len == sizeof(fm6_bank_t) ? fm6_bank_valid((const fm6_bank_t *)raw) : crb_fm6_old(raw, len)))
            return 2;                                /* (an older layout: imported by fm6_bank_boot, as at power-on) */
    } else if (id == CRB_FM6S || id == CRB_FM6S + 1u) {
        if (len && !fm6u_valid((const fm6u_t *)raw, id - CRB_FM6S))
            return 2;
#if FELUCCA_CZ
    } else if (crb_is_cz(id)) {
        if (len && !(id >= CRB_CZB ? cz_bank_valid((const cz_bank_t *)raw) : czu_valid((const czu_t *)raw, id - CRB_CZS)))
            return 2;
#endif
#if FELUCCA_QUAD
    } else if (id == CRB_QUAD) {
        if (len && !quad_store_valid((const quad_store_t *)raw))
            return 2;
#endif
    } else if (id == CRB_VA) {
        const va_store_t *s = (const va_store_t *)raw;
        if (len && (s->magic != VA_STORE_MAGIC || s->nslot != UP_SLOTS ||
                    !(len == sizeof *s ? (s->ver == 3u && va_store_valid(s)) || (s->ver == 2u && s->blob == VA_BLOB)
                                       : s->ver == 1u && s->blob == VA_BLOB1)))
            return 2;                                /* (versions 1 / 2: converted by va_store_boot, as at power-on) */
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
    } else {
        up_boot();                                   /* both banks, the FM6 bank, the VA, FM6, CZ-1 and QUAD stores from
                                                      * flash */
        up_gen++;
#if FELUCCA_CZ
        if (id >= CRB_CZB && id < CRB_CZB + CZ_BANK_N)
            cz_bank_import(id - CRB_CZB, raw, len);  /* (parts on that bank: its new tones, cz_bank_poll) */
#endif
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
        rc = crb_commit();
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
 * device's counters for tools/fm1_install.py --debug, which names them in this order (DEBUG_NAMES there) */
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
    for (i = 0; i < CRP_N; i++)
        v[k++] = cr_dbg.avg_us[i];
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
    crb_b(1);
    crb_b(k);
    for (i = 0; i < k; i++)
        crb_u32(v[i]);
}

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
        const char *v = FELUCCA_VERSION;
        uint32_t i;
        if (na)
            return 0;
        for (i = 0; v[i] && i < 24u; i++)
            crb_b((uint8_t)v[i]);
        crb_b(0);
        for (i = 0; i < 7u; i++)
            crb_b(0);                                /* NENGINES P_COUNT G_COUNT NSTEP P_E0, NTRK, CHAIN_ROWS */
        crb_b(0x42);
        crb_b(1);
        crb_b(3);                                    /* backup read + restore */
        crb_b(0x43);
        crb_b(1);
        crb_b(2);                                    /* ChoralRoot backup v2: ids 9, 40..49, RESTART; no samples */
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
    case CRB_RESTART:
        if (na)
            return 0;
        crb.put = crb.listed = 0;
        crb_b(0);
        crb.reboot = fm1_ms | 1u;                    /* ed_service restarts once the reply is out */
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
/* main.c's main loop: our frames (7D 46 4C), anything else stays for ota_service (M-UPGRADE) */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (crb.reboot && fm1_ms - crb.reboot > 150u && so_r == so_w)
        CRB_REBOOT();                                /* (the RESTART reply has left) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != CRB_HDR0 || p[1] != CRB_HDR1 || p[2] != CRB_HDR2)
        return;
    crb_handle(p, n);                                /* (in place: the frame is released after the reply) */
    ota_frame_done();
}
#endif
#endif /* FELUCCA_FLASH */
