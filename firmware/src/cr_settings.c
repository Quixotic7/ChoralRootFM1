/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the settings record (cr_settings.h, docs/SETTINGS.md).
 *
 * Part 1 (always): defaults, import (version / size / checksum / per-field ranges), seal, apply to the engine and
 * the routing, capture back. Portable C99, no libc: the host test (tests/cr_settings_test.c) builds it with
 * cr_engine.c alone.
 *
 * Part 2 (in the firmware unit, after cr_ui.c and storage.c: CR_VERSION is defined): the flash side on Felucca's
 * settings object (settings_persist.c's persist_t, PER5, whose last member is this record) and the glue to the
 * UI's mirror (cr_ui.c's cs):
 *   cr_settings_boot()   persist_boot: the flash object -> Felucca's fields (panel table, HOLD) + our record
 *   cr_settings_load()   the end of cr_ui_init: the record -> engine, routing, the UI mirror, sounds, palette
 *   cr_settings_poll()   every UI frame: a change (captured from the UI) is saved once nothing changed for
 *                        CRS_QUIET_MS and nothing has sounded for CRS_IDLE_MS (crs_sounding: no voice of the parts,
 *                        no chord held or latched, no scheduled note, the master output below -60 dBFS), never
 *                        while a loop plays (CR_SETTINGS_BUSY()), retried after a flash error. A flash erase holds
 *                        every IRQ for ~45 ms with the audio buffer zeroed (storage_hw.c st_erase): saved under a
 *                        sounding chord it was a 46 ms hole with a click at each edge (docs/INTEGRATION.md,
 *                        Performance)
 *   cr_settings_save()   save now (still deferred while a loop plays)
 * Nothing here writes flash from the audio ISR. */
#ifndef CR_ENGINE_H
#include "cr_engine.h"
#endif
#include "cr_settings.h"

#define CRS_HDR 12u
static const int16_t CRS_PAR_DEF[CRS_NPM][CRS_NPAR] = {     /* = cr_engine.c CR_PAR_DEFAULT (the test checks) */
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 0, 0},
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 30, 0},
    {40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 1},
    {40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0},
    {8, CR_DIV_1_8, 0, 3, 100, 50, 1, 1, 0, 0, 0}};
static const int16_t CRS_PAR_MIN[CRS_NPAR] = {1, 0, 0, 1, 1, 50, 0, 1, -12, 0, 0};
static const int16_t CRS_PAR_MAX[CRS_NPAR] = {1000, CR_DIV_COUNT - 1, CR_DIR_RANDOM, 4, 200, 90, 1, CR_NPATTERN, 12, 100, 1};

typedef char crs_size_ok[sizeof(cr_settings_t) == CRS_SIZE ? 1 : -1];
typedef char crs_npm_ok[CRS_NPM == CR_PM_COUNT && CRS_NPAR == CR_P_COUNT ? 1 : -1];

static uint32_t crs_fnv(const cr_settings_t *s, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)s;
    uint32_t h = 2166136261u, i;
    for (i = CRS_HDR; i < size; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
static void crs_copy(void *d, const void *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ((uint8_t *)d)[i] = ((const uint8_t *)s)[i];
}

void cr_settings_defaults(cr_settings_t *s)
{
    uint32_t i;
    int m, p;
    for (i = 0; i < sizeof *s; i++)
        ((uint8_t *)s)[i] = 0;
    s->magic = CRS_MAGIC;
    s->version = CRS_VERSION;
    s->size = CRS_SIZE;
    s->playstyle = CR_PS_SIMPLE;
    s->extadd = CR_EXTADD_ADD_NOTE;
    s->secret = CR_SECRET_OFF;
    s->scale = CR_SCALE_MAJOR;
    s->single = CR_SINGLE_FULL;
    s->split_pc = 5;
    s->vel = 100;
    s->bass_on = 0;                                /* the bass is OFF at power-on */
    s->bass_mode = CR_BASS_CHORDS_ONLY;
    s->perform_mode = CR_PM_STRUM;
    s->bpm = 120;
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            s->par[m][p] = CRS_PAR_DEF[m][p];
    s->loop_level = 100;
    s->loop_count_in = 1;
    s->metro_vol = 70;
    s->pick_roots = 1;                             /* the engine picker: the roots choose engines */
    s->usb_in = 1;                                 /* USB Record (ChoralRoot In) on */
    s->usb_level = CRS_USB_MASTER;
    for (i = 0; i < 3u; i++) {
        s->midi_en[i] = i != CR_STREAM_RAW;        /* RAW stream off (Orchid) */
        s->midi_ch[i] = (uint8_t)i;                /* channels 1 / 2 / 3 */
    }
    s->clock_mode = CRS_CLOCK_OUT;                 /* Orchid sends clock */
    s->palette = CRS_PALETTE_MOD;
    s->fx_on = 1;
    s->chord_sound = CRS_SOUND_DEFAULT;
    s->bass_sound = CRS_SOUND_DEFAULT;
    for (i = 0; i < (uint32_t)CRS_NENG; i++) {
        crs_pool_set(s, 0, i, CRS_POOL_DEFAULT);
        crs_pool_set(s, 1, i, CRS_POOL_DEFAULT);
    }
    cr_settings_seal(s);
}

void cr_settings_seal(cr_settings_t *s)
{
    s->magic = CRS_MAGIC;
    s->version = CRS_VERSION;
    s->size = CRS_SIZE;
    s->check = crs_fnv(s, CRS_SIZE);
}

int cr_settings_equal(const cr_settings_t *a, const cr_settings_t *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint32_t i;
    for (i = CRS_HDR; i < CRS_SIZE; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

/* a field out of range takes its default */
#define CRS_FIX(f, lo, hi) do { if ((int)s->f < (lo) || (int)s->f > (hi)) s->f = d.f; } while (0)
static void crs_sanitize(cr_settings_t *s)
{
    cr_settings_t d;
    int m, p, i;
    cr_settings_defaults(&d);
    CRS_FIX(playstyle, 0, CR_PS_FREE);
    CRS_FIX(extadd, 0, 1);
    CRS_FIX(secret, 0, CR_SECRET_ALL);
    CRS_FIX(key_on, 0, 1);
    CRS_FIX(tonic, 0, 11);
    CRS_FIX(scale, 0, 1);
    CRS_FIX(transpose, -24, 24);
    CRS_FIX(single, 0, 1);
    CRS_FIX(split_pc, 0, 11);
    CRS_FIX(vel, 1, 127);
    CRS_FIX(bass_on, 0, 1);
    CRS_FIX(bass_mode, 0, CR_BASS_SOLO);
    CRS_FIX(bass_voicing, -2, 4);
    CRS_FIX(perform_on, 0, 1);
    CRS_FIX(perform_mode, 0, CRS_NPM - 1);
    CRS_FIX(perf_sel, 0, 6);
    CRS_FIX(sticky, 0, 1);
    CRS_FIX(bpm, 20, 300);
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            if (s->par[m][p] < CRS_PAR_MIN[p] || s->par[m][p] > CRS_PAR_MAX[p])
                s->par[m][p] = d.par[m][p];
    CRS_FIX(loop_sync, 0, 5);
    CRS_FIX(loop_quant, 0, 6);
    CRS_FIX(loop_count_in, 0, 1);
    CRS_FIX(loop_level, 0, 100);
    for (i = 0; i < 3; i++) {
        if (s->midi_en[i] > 1u) s->midi_en[i] = d.midi_en[i];
        if (s->midi_ch[i] > 15u) s->midi_ch[i] = d.midi_ch[i];
    }
    CRS_FIX(clock_mode, 0, CRS_CLOCK_IN);
    CRS_FIX(raw_sound, 0, 1);
    CRS_FIX(view, 0, 4);
    CRS_FIX(motion, 0, 2);
    CRS_FIX(leds, 0, 1);
    CRS_FIX(fx_on, 0, 1);
    CRS_FIX(metro_on, 0, 1);
    CRS_FIX(metro_sig, 0, 2);
    CRS_FIX(metro_vol, 0, 100);
    CRS_FIX(loop_slot, 0, 9);
    CRS_FIX(pick_roots, 0, 1);
    CRS_FIX(usb_in, 0, 1);
    CRS_FIX(usb_level, 0, CRS_USB_FIXED);
    if ((s->loop_rec & ~0x1Fu) || CRS_LOOP_MODE(s) >= CRS_NLOOPMODE)   /* (an unknown mode or bit: the defaults) */
        s->loop_rec = d.loop_rec;
    for (i = 0; i < CRS_NENG; i++)                 /* (a pool: INIT + 64 CZ-1 presets + 32 user ones at most) */
        for (m = 0; m < 2; m++)
            if (crs_pool_get(s, (unsigned)m, (unsigned)i) > 127u)
                crs_pool_set(s, (unsigned)m, (unsigned)i, crs_pool_get(&d, (unsigned)m, (unsigned)i));
    /* palette, chord_sound, bass_sound, pool_pos: checked against the pools by the UI glue (the pools are the UI's) */
}

int cr_settings_import(cr_settings_t *s, const void *blk, uint32_t n)
{
    cr_settings_t in;
    uint32_t size;
    uint32_t hnew = 2166136261u, i;
    int rc;
    for (i = 0; i < sizeof in; i++)
        ((uint8_t *)&in)[i] = 0;
    if (blk && n >= CRS_HDR) {                     /* (blk may be s itself: read it all before the defaults) */
        crs_copy(&in, blk, n < CRS_SIZE ? n : CRS_SIZE);
        if (in.size > CRS_SIZE && in.size <= n)
            for (i = CRS_HDR; i < in.size; i++) {
                hnew ^= ((const uint8_t *)blk)[i];
                hnew *= 16777619u;
            }
    }
    cr_settings_defaults(s);
    if (!blk || n < CRS_HDR)
        return 0;
    size = in.size;
    if (in.magic != CRS_MAGIC || in.version == 0u || size < CRS_HDR || size > n)
        return 0;
    if (size > CRS_SIZE) {                         /* a newer writer with a longer record: its prefix is ours */
        if (hnew != in.check)
            return 0;
        size = CRS_SIZE;
    } else if (crs_fnv(&in, size) != in.check) {
        return 0;
    }
    rc = in.version == CRS_VERSION && in.size == CRS_SIZE ? 1 : 2;
    /* the writer's fields over the defaults; an older version's never-written reserve stays default */
    crs_copy((uint8_t *)s + CRS_HDR, (const uint8_t *)&in + CRS_HDR, size - CRS_HDR);
    if (in.version < CRS_VERSION) {
        /* version-specific migrations go here: if (in.version < 2) s->new_field = d.new_field; ... */
        if (in.version < 2u) {                     /* version 2: the metronome and the loop slot */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->metro_on = d.metro_on;
            s->metro_sig = d.metro_sig;
            s->metro_vol = d.metro_vol;
            s->loop_slot = d.loop_slot;
        }
        if (in.version < 3u) {                     /* version 3: the engine picker's roots (a 0 there: not set) */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->pick_roots = d.pick_roots;
        }
        if (in.version < 4u) {                     /* version 4: USB audio (zeros there would switch both off) */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->usb_in = d.usb_in;
            s->usb_level = d.usb_level;
        }
        if (in.version < 5u)                       /* version 5: the USB playback removed, its byte (v4's usb_out, */
            s->loop_rec = 0;                       /* 1 by default there) cleared */
        if (in.version < 6u) {                     /* version 6: presets per engine (docs/PRESETS.md): the old list
                                                    * positions mean nothing in the pools: the defaults (TINE EP, SUB
                                                    * BASS), every engine's first preset */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->chord_sound = d.chord_sound;
            s->bass_sound = d.bass_sound;
            for (i = 0; i < (uint32_t)CRS_NENG; i++) {
                crs_pool_set(s, 0, i, crs_pool_get(&d, 0, i));
                crs_pool_set(s, 1, i, crs_pool_get(&d, 1, i));
            }
        }
        if (in.version < 7u) {                     /* version 7 (0.14): ANALOG retired; its pool_pos slot 0 is FM
                                                    * TONE's. The chord part's place there: rsv_usb, where a 0.14 dev
                                                    * build kept it (0 in a 0.13 record: the first preset); the bass
                                                    * part's: bass_sound's when the bass played FM TONE. A part's sound
                                                    * on ANALOG: the default (TINE EP, DEEP SUB) */
            uint8_t q = s->loop_rec;               /* (rsv_usb then) */
            crs_pool_set(s, 0, 0, q && q <= 127u ? q : CRS_POOL_DEFAULT);
            crs_pool_set(s, 1, 0, s->bass_sound != CRS_SOUND_DEFAULT && (s->bass_sound >> 8) == CRS_ENG_QUAD ?
                                      (uint8_t)(s->bass_sound & 0xFFu) : CRS_POOL_DEFAULT);
            s->loop_rec = 0;
            if (s->chord_sound != CRS_SOUND_DEFAULT && (s->chord_sound >> 8) == CRS_ENG_ANALOG)
                s->chord_sound = CRS_SOUND_DEFAULT;
            if (s->bass_sound != CRS_SOUND_DEFAULT && (s->bass_sound >> 8) == CRS_ENG_ANALOG)
                s->bass_sound = CRS_SOUND_DEFAULT;
        }
        if (in.version < 8u) {                     /* version 8: the looper's record mode and keys (docs/LOOPER-MODES.md)
                                                    * in the byte that was rsv_usb (0 since v7): the defaults */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->loop_rec = d.loop_rec;
        }
    }
    crs_sanitize(s);
    cr_settings_seal(s);
    return rc;
}

/* ------------------------------------------------------ engine <-> record --- */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o)
{
    int m, i;
    unsigned p;                                    /* (a cr_param_t: that name is the screen's in the unit) */
    cr_set_playstyle(c, (cr_playstyle_t)s->playstyle);
    cr_set_ext_addition(c, (cr_extadd_t)s->extadd);
    cr_set_secret(c, (cr_secret_t)s->secret);
    cr_set_key(c, s->key_on, s->tonic, (cr_scale_t)s->scale);
    cr_set_transpose(c, s->transpose);
    cr_set_single_notes(c, (cr_single_t)s->single, s->split_pc);
    cr_set_bass_mode(c, (cr_bassmode_t)s->bass_mode);
    cr_bass_voicing_step(c, s->bass_voicing - c->bass_voicing);
    cr_set_bass(c, s->bass_on);
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            if (cr_get_param(c, (cr_pmode_t)m, p) != s->par[m][p])
                cr_set_param(c, (cr_pmode_t)m, p, s->par[m][p]);
    cr_set_perform_mode(c, (cr_pmode_t)s->perform_mode);
    cr_set_perform(c, s->perform_on);
    cr_set_sticky(c, s->sticky);
    cr_set_tempo(c, s->bpm);
    cr_set_stream(c, CR_STREAM_RAW, s->midi_en[CR_STREAM_RAW] || s->raw_sound);
    if (o) {
        for (i = 0; i < 3; i++) {
            o->midi_en[i] = s->midi_en[i];
            o->ch[i] = s->midi_ch[i];
        }
        o->part[CR_STREAM_MAIN] = 0;
        o->part[CR_STREAM_BASS] = 1;
        o->part[CR_STREAM_RAW] = s->raw_sound ? 0 : CRS_NONE;
        o->clock_out = s->clock_mode == CRS_CLOCK_OUT;
        o->clock_in = s->clock_mode == CRS_CLOCK_IN;
    }
}

void cr_settings_capture(cr_settings_t *s, const cr_t *c, const cr_settings_out_t *o)
{
    int m, i;
    unsigned p;
    s->playstyle = c->playstyle;
    s->extadd = c->extadd;
    s->secret = c->secret;
    s->key_on = c->key_on;
    s->tonic = c->tonic;
    s->scale = c->scale;
    s->transpose = c->transpose;
    s->single = c->single;
    s->split_pc = c->split_pc;
    s->bass_on = c->bass_on;
    s->bass_mode = c->bass_mode;
    s->bass_voicing = c->bass_voicing;
    s->perform_on = c->perform_on;
    s->perform_mode = c->perform_mode;
    s->sticky = c->sticky;
    s->bpm = c->bpm;
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            s->par[m][p] = (int16_t)cr_get_param(c, (cr_pmode_t)m, p);
    if (o) {
        for (i = 0; i < 3; i++) {
            s->midi_en[i] = o->midi_en[i] ? 1u : 0u;
            s->midi_ch[i] = (uint8_t)(o->ch[i] & 15u);
        }
        s->raw_sound = o->part[CR_STREAM_RAW] != CRS_NONE;
        s->clock_mode = o->clock_in ? CRS_CLOCK_IN : o->clock_out ? CRS_CLOCK_OUT : CRS_CLOCK_OFF;
    }
    cr_settings_seal(s);
}

/* =========================================== the firmware unit: flash + UI glue --- */
#ifdef CR_VERSION
#ifndef CR_SETTINGS_BUSY
#define CR_SETTINGS_BUSY() 0                       /* (cr_ui.c defines it: a loop is playing, no flash erase) */
#endif
#define CRS_QUIET_MS 1500u                         /* a change is saved once nothing changed for this long */
#define CRS_IDLE_MS 1000u                          /* .. and nothing sounded for this long (crs_sounding) */
#define CRS_QUIET_PEAK 32                          /* the master output (audio.c scope_buf, Q15): -60 dBFS */

#include "settings_persist.c"                      /* Felucca's record, PER5: its last member is cr_settings_t */

static persist_t crs_rec;                          /* the last record read or written (Felucca's fields + ours) */
static cr_settings_t crs_seen;                     /* the last captured state (change detection) */
static uint8_t crs_booted, crs_loaded, crs_pending;
static uint32_t crs_change_ms, crs_retry_ms;
static int crs_last_rc;                            /* the last import: 1 current, 2 migrated, 0 defaults */
static uint32_t crs_saves;                         /* flash writes done (diagnostics, the emulator) */

/* the USB settings to usb.c (which devices the host is given; ua_off at boot, usb_start builds the configuration)
 * and fx.c (USB Level) */
static void crs_usb_apply(const cr_settings_t *s)
{
    fx_usb_fixed = s->usb_level == CRS_USB_FIXED;
#if FELUCCA_UAC
    ua_off = ua_off_want = s->usb_in ? 0u : UA_OFF_IN;
#endif
}

static void cr_settings_boot(void)                 /* persist_boot (flash_ok known), before settings_init / panel_init */
{
    int n = -1;
    crs_booted = 1;
    memset(&crs_rec, 0, sizeof crs_rec);
#if FELUCCA_FLASH
    if (flash_ok)
        n = st_load(OBJ_SETTINGS, &crs_rec, sizeof crs_rec);
#endif
    if (n <= 0 || !settings_import(&crs_rec, n)) {  /* absent / unknown: defaults, Felucca's fields as they are */
        memset(&crs_rec, 0, sizeof crs_rec);
        settings_export(&crs_rec);
    }
    crs_last_rc = cr_settings_import(&crs_rec.cr, &crs_rec.cr, sizeof crs_rec.cr);
    crs_usb_apply(&crs_rec.cr);                    /* (before usb_start: the configuration the host first reads) */
}

/* the record's pool_pos slot of the engine of ENGINE_ORDER rank r (cr_settings.h): CRS_NENG = 11 slots, fixed per engine
 * in 0.13's display order, so a 0.13 record means what it meant whatever the order shown today; ANALOG's slot 0 is FM
 * TONE's since version 7 (ANALOG retired, FELUCCA_ANALOG 0). -1: an engine with no slot (none on ChoralRoot) */
#if FELUCCA_ANALOG || !FELUCCA_VA || !FELUCCA_CZ || !FELUCCA_QUAD
#error "cr_settings.c: the record's engine slots are ChoralRoot's (FELUCCA_ANALOG 0, FELUCCA_VA / CZ / QUAD 1)"
#endif
typedef char crs_neng_ok[CRS_NENG == NENG_SHOWN && CRS_ENG_QUAD == ENGI_QUAD ? 1 : -1];   /* (one per engine shown) */
static const uint8_t CRS_SLOT_ENG[CRS_NENG] = {ENGI_QUAD, ENGI_FM6, ENGI_VA, 2, ENGI_CZ, 3, 5, 6, 7, ENGI_PHYS, 11};
static int32_t crs_slot(uint32_t r)
{
    uint32_t e = eng_vis(r), k;
    for (k = 0; k < (uint32_t)CRS_NENG; k++)
        if (CRS_SLOT_ENG[k] == e)
            return (int32_t)k;
    return -1;
}

/* a stored sound (engine << 8 | pool position) into the part, if it is one of a pool today; 1 loaded */
static int crs_sound(uint32_t part, uint16_t v)
{
    uint32_t e = v >> 8, pos = v & 0xFFu;
    if (v == CRS_SOUND_DEFAULT || !cu_engine_melodic(e) || pos >= pool_count(e))
        return 0;
    if (e != trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req || pos != cu_part_pos(part))
        cu_pool_load(part, e, pos);
    return 1;
}

/* the UI's mirror -> the record (with the engine's fields read under the IRQ lock) */
static void crs_capture(cr_settings_t *s)
{
    cr_settings_out_t o;
    uint32_t i;
    *s = crs_rec.cr;
    for (i = 0; i < 3u; i++) {
        o.midi_en[i] = cr_route.midi_en[i];
        o.ch[i] = cr_route.ch[i];
        o.part[i] = cr_route.part[i] == CR_NOPART ? CRS_NONE : cr_route.part[i];
    }
    o.clock_out = cr_route.clock_out;
    o.clock_in = cr_route.clock_in;
    fm1_irq_off();
    cr_settings_capture(s, &cr, &o);
    fm1_irq_on();
    /* the UI's own (and what the UI holds ahead of the ISR: the queue may not be drained yet) */
    s->playstyle = cs.playstyle;
    s->extadd = cs.extadd;
    s->secret = cs.secret;
    s->key_on = cs.key_on;
    s->tonic = cs.tonic;
    s->scale = cs.scale;
    s->transpose = cs.transpose;
    s->single = cs.single;
    s->vel = cs.vel;
    s->bass_mode = cs.bass_mode;
    s->bass_on = 0;                                /* (power-on: the bass is OFF; BASS tap brings bass_sound) */
    s->perform_on = cs.perform_on;
    s->perf_sel = cs.perf_sel;
    s->sticky = cs.sticky;
    s->bpm = cs.bpm;
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        s->par[i / CRS_NPAR][i % CRS_NPAR] = cs.par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++) {
        s->midi_en[i] = cs.ch[i] != 0u;
        if (cs.ch[i])
            s->midi_ch[i] = (uint8_t)(cs.ch[i] - 1u);
    }
    s->raw_sound = cs.raw_sound;
    s->loop_sync = cs.loop_len;
    s->loop_quant = cs.loop_quant;
    s->loop_count_in = cs.loop_count_in;
    s->loop_level = cs.loop_level;
    s->metro_on = cs.metro;
    s->metro_sig = cs.metro_sig;
    s->metro_vol = cs.metro_vol;
    s->loop_slot = cs.loop_slot;
    s->loop_rec = CRS_LOOP_REC(cs.loop_mode, cs.loop_keys);
    s->pick_roots = cs.pick_roots;
    s->usb_in = cs.usb_in;
    s->usb_level = cs.usb_fixed;
    s->split_pc = cs.split;
    s->view = cs.view;
    s->motion = cr_motion;
    s->leds = cs.leds;
    s->fx_on = cs.fx_on;
    s->chord_sound = (uint16_t)((trk[CR_PART_CHORD].eng_req % NENGINES) << 8 | (cs.sound & 0xFFu));
    s->bass_sound = (uint16_t)((trk[CR_PART_BASS].eng_req % NENGINES) << 8 |
                               cs.pool_pos[1][eng_rank(trk[CR_PART_BASS].eng_req % NENGINES)]);
    for (i = 0; i < (uint32_t)NENG_SHOWN; i++) {
        int32_t k = crs_slot(i);
        if (k < 0)
            continue;
        crs_pool_set(s, 0, (unsigned)k, cs.pool_pos[0][i]);
        crs_pool_set(s, 1, (unsigned)k, cs.pool_pos[1][i]);
    }
    cr_settings_seal(s);
}

/* the end of cr_ui_init: the record -> the engine (directly, IRQ off: nothing sounds yet), the routing, the mirror */
static void cr_settings_load(void)
{
    cr_settings_t *s = &crs_rec.cr;
    cr_settings_out_t o;
    uint32_t i;
    if (!crs_booted)
        cr_settings_boot();
    panel_init();                                  /* (a calibration table from flash: validated) */
    fm1_irq_off();
    cr_settings_apply(s, &cr, &o);
    fm1_irq_on();
    cs.playstyle = s->playstyle;
    cs.extadd = s->extadd;
    cs.secret = s->secret;
    cs.key_on = s->key_on;
    cs.tonic = s->tonic;
    cs.scale = s->scale;
    cs.transpose = s->transpose;
    cs.single = s->single;
    cs.vel = s->vel;
    cs.bass_mode = s->bass_mode;
    cs.perform_on = s->perform_on;
    cs.perf_sel = s->perf_sel;
    cs.sticky = s->sticky;
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        cs.par[i / CRS_NPAR][i % CRS_NPAR] = s->par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++)
        cs.ch[i] = (uint8_t)(s->midi_en[i] ? s->midi_ch[i] + 1u : 0u);
    cs.raw_sound = s->raw_sound;
    cr_route.clock_out = o.clock_out;
    cr_route.clock_in = o.clock_in;
    cs.loop_len = s->loop_sync;
    cs.loop_quant = s->loop_quant;
    cs.loop_count_in = s->loop_count_in;
    cs.loop_level = s->loop_level;
    cs.metro = s->metro_on;
    cs.metro_sig = s->metro_sig;
    cs.metro_vol = s->metro_vol;
    cs.loop_slot = s->loop_slot;
    cs.loop_mode = (uint8_t)CRS_LOOP_MODE(s);
    cs.loop_keys = (uint8_t)CRS_LOOP_KEYS(s);
    cs.pick_roots = s->pick_roots;
    cs.usb_in = s->usb_in;
    cs.usb_fixed = s->usb_level;
    fx_usb_fixed = cs.usb_fixed;                   /* (USB Record: crs_usb_apply at boot, before usb_start) */
    cs.split = s->split_pc;
    cs.view = s->view;
    cr_motion = s->motion;
    cs.leds = s->leds;
    palette_set(s->palette < NPALETTES ? s->palette : NPALETTES - 1u);
    for (i = 0; i < (uint32_t)NENG_SHOWN; i++) {    /* each part's place per engine (the pools: checked on use) */
        int32_t k = crs_slot(i);
        if (k < 0) {                                /* (an engine with no slot: its first preset) */
            cs.pool_pos[0][i] = cs.pool_pos[1][i] = CRS_POOL_DEFAULT;
            continue;
        }
        cs.pool_pos[0][i] = crs_pool_get(s, 0, (unsigned)k);
        cs.pool_pos[1][i] = crs_pool_get(s, 1, (unsigned)k);
    }
    if (crs_sound(0, s->chord_sound))              /* the chord part's sound (its own sends become the FX amounts) */
        cu_sends_to_fx();
    else
        s->chord_sound = CRS_SOUND_DEFAULT;
    cs.fx_on = s->fx_on;
    cu_fx_apply();
    if (crs_sound(1, s->bass_sound))               /* what BASS tap brings; the bass stays OFF at power-on */
        trk[CR_PART_BASS].engine = trk[CR_PART_BASS].eng_req;
    else
        s->bass_sound = CRS_SOUND_DEFAULT;
    cu_pos_sync();
    cu_route();
    cu_set_tempo(s->bpm);
    crs_loaded = 1;
    crs_capture(&crs_seen);                        /* the state now: nothing to save until it changes */
    crs_change_ms = fm1_ms;
    crs_pending = 0;
}

static int crs_write(void)                         /* the captured state -> flash; 0: written (or no flash) */
{
#if FELUCCA_FLASH
    persist_t p = crs_rec;
    if (!flash_ok)
        return 0;
#ifdef ST_BLOCKED
    if (ST_BLOCKED())
        return 0;                                  /* SAFE MODE (core.h): kept in RAM only, the flash untouched */
#endif
    settings_export(&p);                           /* Felucca's fields (panel table, HOLD, LEDs, palette) */
    p.cr = crs_seen;
    cr_settings_seal(&p.cr);
    if (!memcmp(&p, &crs_rec, sizeof p))
        return 0;                                  /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) != 0)
        return -1;
    crs_rec = p;
    crs_saves++;
#else
    crs_rec.cr = crs_seen;
#endif
    return 0;
}

/* something sounds or is about to: a voice of the parts (release tails too), a chord the engine holds or latches,
 * a scheduled note (arp, strum), or the master output (the FX tails) above CRS_QUIET_PEAK over the scope's last
 * 512 samples (~23 ms). No flash erase then: it would cut the audio (see the top) */
static uint32_t crs_loud_ms;
static int crs_sounding(void)
{
    uint32_t i;
    if (cr_parts_busy() || cr_snap.voices || cr_snap.pending)
        return 1;
    for (i = 0; i < SCOPE_N; i++)
        if (scope_buf[i] > CRS_QUIET_PEAK || scope_buf[i] < -CRS_QUIET_PEAK)
            return 1;
    return 0;
}

/* every UI frame (main.c settings_poll) */
static void cr_settings_poll(void)
{
    cr_settings_t now;
    if (!crs_loaded)
        return;
    if (crs_sounding())
        crs_loud_ms = fm1_ms;
    crs_capture(&now);
    if (!cr_settings_equal(&now, &crs_seen)) {
        crs_seen = now;
        crs_change_ms = fm1_ms;
        crs_pending = 1;
        return;
    }
    if (!crs_pending || CR_SETTINGS_BUSY() || (uint32_t)(fm1_ms - crs_change_ms) < CRS_QUIET_MS ||
        (uint32_t)(fm1_ms - crs_loud_ms) < CRS_IDLE_MS)
        return;
    if (crs_pending == 2u && (uint32_t)(fm1_ms - crs_retry_ms) < 1000u)
        return;
    if (crs_write() == 0) {
        crs_pending = 0;
    } else {
        crs_pending = 2;                           /* flash error: retry in a second */
        crs_retry_ms = fm1_ms;
    }
}

/* save now (Felucca's settings_save: the calibration, the Options): still not while a loop plays */
static void cr_settings_save(void)
{
    if (!crs_loaded) {                             /* (power-on calibration: before the UI) */
        if (!crs_booted)
            cr_settings_boot();
        crs_seen = crs_rec.cr;
    } else {
        crs_capture(&crs_seen);
    }
    crs_pending = 1;
    crs_change_ms = fm1_ms - CRS_QUIET_MS;
    if (!CR_SETTINGS_BUSY() && crs_write() == 0)
        crs_pending = 0;
}
#endif /* CR_VERSION */
