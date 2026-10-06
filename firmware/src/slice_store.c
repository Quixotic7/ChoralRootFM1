/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE's MAN slices in flash, kept with the sample they were set on (based on hugelton/Felucca#27 by andreahaku).
 *
 * Where: in the user slot itself, a record at the start of the slot's last sector (slot + SLC_REC_OFF). The slot
 * header has no room (its 32 spare bytes hold 8 slice starts at most, and NOR could write them only once), and every
 * free sector of the map is taken (FM6's bank has 0x9F000 / 0xFE000). An upload writes the sample from the slot's
 * start, so its last sector is free unless the sample fills it: up to 77,312 bytes of ADPCM (about 7.0 s at the
 * 22.05 kHz the editor uploads at; a slot holds 7.4 s). A longer sample keeps its slices until the power goes
 * ("TOO LONG TO SAVE").
 * Record: magic, the material (the slot's sample length and its data CRC: the header's), the starts and the end,
 * a CRC over all of it. At each scan of a valid slot (boot, an upload's end: slc_man_load) a record that matches
 * the slot's material and passes slc_man_restore (sorted, SLC_MIN apart, in range) is put in use; anything else
 * (an erased sector, an older sample's record, the end of a longer sample's data) is ignored. An upload erases each
 * sector it writes into, so a long sample overwrites the record; uploading the same sample again brings its slices
 * back. One copy: a save torn by a power cut loses the slices (the CRC fails), never the sample.
 * Saving: the SLICES page marks the slot (slc_man_save, eng_slice.c) when one leaves it after an edit; slc_store_poll
 * writes when the transport is stopped (no erase while playing, as settings_poll), only if the flash holds something
 * else ("SLICES SAVED").
 * Flash access through storage.c's hooks (st_erase / st_prog); the host tests give their own. */
#define SLC_REC_OFF (SMP_USER_SIZE - 0x1000u)        /* the slot's last sector */
#define SLC_REC_MAGIC 0x314D4C53u                    /* "SLM1" */
typedef struct {
    uint32_t magic;
    uint32_t len, crc;                               /* the material: samples, the slot header's data CRC */
    uint32_t n, end;                                 /* slices (1..SLC_AUTO), where the last ends (0 = the material's end) */
    uint32_t pos[SLC_AUTO];
    uint32_t rcrc;                                   /* st_crc32 of everything above */
} slc_rec_t;
_Static_assert(sizeof(slc_rec_t) == 152u, "SLICE record layout");

/* slot k's sample leaves its last sector free (the record can go there) */
static int slc_rec_room(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    return usr_nz[k] && h->data_len <= SLC_REC_OFF - SMP_USER_DATA;
}

/* what slot k's record should hold now (its MAN slices in use, or none: n = 0) */
static void slc_rec_make(uint32_t k, slc_rec_t *r)
{
    const slc_src_t *s = slc_get(k + 1u);
    const slc_man_t *m = s ? slc_man_of(s) : 0;
    memset(r, 0, sizeof *r);
    r->magic = SLC_REC_MAGIC;
    r->len = s ? s->len : 0u;
    r->crc = ((const smp_user_hdr_t *)smp_user_xip(k))->crc;
    if (m) {
        r->n = m->n;
        r->end = m->end;
        memcpy(r->pos, m->pos, m->n * sizeof m->pos[0]);
    }
    r->rcrc = st_crc32(r, sizeof *r - 4u);
}

/* slc_man_load (eng_slice.c): slot k was read and is valid: its stored slices, if they are its material's */
static void slc_store_load(uint32_t k)
{
    const slc_rec_t *r = (const slc_rec_t *)(smp_user_xip(k) + SLC_REC_OFF);
    const slc_src_t *s = slc_get(k + 1u);
    slc_rec_t c;
    if (!s || !slc_rec_room(k))
        return;
    memcpy(&c, r, sizeof c);                         /* (one read of the flash) */
    if (c.magic != SLC_REC_MAGIC || c.rcrc != st_crc32(&c, sizeof c - 4u) || c.len != s->len ||
        c.crc != ((const smp_user_hdr_t *)smp_user_xip(k))->crc || !c.n)
        return;
    slc_man_restore(k, c.n, c.end, c.pos);
}

/* write slot k's record if the flash holds something else: 0 nothing to do, 1 no room, 2 flash error, 3 written */
static int slc_store_write(uint32_t k)
{
    slc_rec_t r;
    uint32_t off = SMP_USER_BASE + k * SMP_USER_SIZE + SLC_REC_OFF;
    if (!usr_nz[k])                                  /* emptied or being uploaded meanwhile: nothing to keep */
        return 0;
    if (!slc_rec_room(k))
        return 1;
    slc_rec_make(k, &r);
    if (!memcmp(&r, smp_user_xip(k) + SLC_REC_OFF, sizeof r))
        return 0;                                    /* unchanged: no erase cycle */
    if (st_erase(off) || st_prog(off, &r, sizeof r))
        return 2;
    return 3;
}

/* main loop: the slices asked for, once the transport stops */
static void slc_store_poll(void)
{
    uint32_t k;
    int rc = 0, w = 0;
    if (!slc_man_save || transport_busy() || slice_page_on())   /* (written once the page is left) */
        return;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        if ((slc_man_save >> k) & 1u) {
            int r = flash_ok ? slc_store_write(k) : 0;
            w |= r == 3;
            rc = r != 3 && r > rc ? r : rc;
        }
    slc_man_save = 0;
    if (rc)
        ui_message(rc == 1 ? "TOO LONG TO SAVE" : "SAVE ERROR");
    else if (w)
        ui_message("SLICES SAVED");
}

static void slc_store_boot(void) { slc_man_load = slc_store_load; }   /* persist_boot: before the slot scans */
