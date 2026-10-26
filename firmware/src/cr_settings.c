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
 *   cr_settings_poll()   every UI frame: a change of an AUTOMATIC setting (cr_settings.h cr_settings_auto: what the
 *                        Options menu alone sets; and Felucca's HOLD, which Options > Hold Time sets outside our
 *                        record: crs_hold_seen) is saved once nothing changed for CRS_QUIET_MS and nothing has
 *                        sounded for CRS_IDLE_MS (crs_sounding: no voice of the parts, no chord held or latched, no
 *                        scheduled note, the master output below -60 dBFS), never while a loop plays
 *                        (CR_SETTINGS_BUSY()), retried after a flash error. A flash erase holds every IRQ for ~45 ms
 *                        with the audio buffer zeroed (storage_hw.c st_erase): saved under a sounding chord it was a
 *                        46 ms hole with a click at each edge (docs/INTEGRATION.md, Performance). Since 0.22 the
 *                        erase is done AHEAD (power-on, a long idle) and the write only programs: "THE QUIET WRITES"
 *                        below, storage.c st_write_due / st_ahead_due. What is written:
 *                        flash's block with the automatic fields as they are now; the MANUAL fields stay what the
 *                        last global save wrote
 *   cr_settings_save()   the automatic part now (Felucca's settings_save: the calibration, the update gesture; not
 *                        a backup's LIST since 3y48: a read never writes), still deferred while a loop plays
 *   cr_settings_commit() the GLOBAL SAVE (SAVE held, cr_ui.c cu_gsave; Options > Reset Settings): the whole block
 *                        and the session, now. The only writer of the manual settings and of the session
 *   cr_settings_revert() GLO + SAVE: flash's block (with the Options as they are now) and flash's session applied
 *                        again, as a power-on applies them
 * The session (0.18, cr_settings.h cr_session_t: where the unit was left; part 1 has its defaults / import / seal,
 * part 2 its glue, css_*): a flash object of its own (storage.c OBJ_SESSION), read by cr_settings_boot, applied once
 * by cr_settings_load, written by cr_settings_commit alone (until 2026-10-11 the poll wrote it when the unit was
 * quiet: "switch off, switch on: where you left off"; the owner's rule since: the unit comes back as last SAVED).
 * Nothing here writes flash from the audio ISR. */
#ifndef CR_ENGINE_H
#include "cr_engine.h"
#endif
#include "cr_settings.h"
#include <stddef.h>                         /* (offsetof: the session's layout checks) */

#define CRS_HDR 12u
static const int16_t CRS_PAR_DEF[CRS_NPM][CRS_NPAR] = {     /* = cr_engine.c CR_PAR_DEFAULT (the test checks) */
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 0, 0},
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 30, 0},
    {40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0},
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
#if BOARD_SMK37
    s->bass_on = CRS_TOUCH_BITS(1u, 0u, 0u);       /* SMK-37: Key Touch Dynamic, the keys' velocity (SMK37.md) */
#else
    s->bass_on = 0;                                /* (always 0 in the block: Bass on is the session's, cr_settings.h) */
#endif
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
    s->view = CRS_DC_ON;                           /* Options > Dual Core On (0.22, the owner, 2026-10-13: "Lets also
                                                    * default to using Dual Core. If users have issues, then they can
                                                    * switch to 1 core"): a unit with no record, Reset Settings. A
                                                    * record that is there keeps what it holds: its Off cannot be
                                                    * told from 0.16 .. 0.21's default (docs/DUALCORE.md) */
    s->fx_on = 1;
#if !BOARD_SMK37
    s->sticky = CRS_LAYOUT_FM1;                    /* Options > Chord Keys FM-1 (0.25.1, the owner, 2026-10-16: "let's
                                                    * make it the default"): a unit with no record, Flash Data, Reset
                                                    * Settings. Moved already (CRS_KEYS_MOVED): the layout a record of
                                                    * this firmware holds is the player's (the SMK-37: Classic, hidden) */
    s->leds = CRS_KEYS_MOVED;
#endif
    s->chord_sound = CRS_SOUND_DEFAULT;
    s->bass_sound = CRS_SOUND_DEFAULT;
    for (i = 0; i < (uint32_t)CRS_NENG; i++) {
        crs_pool_set(s, 0, i, CRS_POOL_DEFAULT);
        crs_pool_set(s, 1, i, CRS_POOL_DEFAULT);
    }
    cr_settings_seal(s);
}

void cr_settings_factory(cr_settings_t *s)
{
    uint8_t slot = s->loop_slot;
    cr_settings_defaults(s);
    if (slot <= 9u)
        s->loop_slot = slot;
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

void cr_settings_auto(cr_settings_t *dst, const cr_settings_t *src)
{
    uint32_t i;
    dst->playstyle = src->playstyle;
    dst->extadd = src->extadd;
    dst->secret = src->secret;
    dst->vel = src->vel;
    dst->bass_on = (uint8_t)((dst->bass_on & 3u) | (src->bass_on & 0xFCu));   /* Key Touch and the sensitivities */
    for (i = 0; i < 3u; i++) {
        dst->midi_en[i] = src->midi_en[i];
        dst->midi_ch[i] = src->midi_ch[i];
    }
    dst->clock_mode = src->clock_mode;
    dst->raw_sound = src->raw_sound;
    dst->motion = src->motion;
    dst->leds = src->leds;
    dst->palette = src->palette;
    dst->usb_in = src->usb_in;
    dst->usb_level = src->usb_level;
    dst->pick_roots = (uint8_t)((dst->pick_roots & (uint8_t)~CRS_PICK_AUTO) | (src->pick_roots & CRS_PICK_AUTO));
    dst->sticky = (uint8_t)((dst->sticky & (uint8_t)~CRS_STICKY_AUTO) | (src->sticky & CRS_STICKY_AUTO));   /* (the
                                                    * chord layouts' Options; LATCH, bit 0, is manual) */
    dst->view = (uint8_t)((dst->view & (uint8_t)~CRS_DC_ON) | (src->view & CRS_DC_ON));   /* (Dual Core rides in the
                                                    * view's byte: the view and its lock are manual) */
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
    CRS_FIX(key_on, 0, (int)CRS_KEY_MAX);          /* (v14: bit 1 Key plays Notes, bit 2 the Full keyboard; 0.22:
                                                    * bits 3 .. 4 Snap 0 .. 2, CRS_SNAP) */
    CRS_FIX(tonic, 0, 11);
    if (CRS_SCALE(s) > CR_SCALE_MINOR)             /* (bits 0..6 the scale; bit 7 Numerals' Solfege, CRS_SOLFEGE: kept) */
        s->scale = (uint8_t)((s->scale & CRS_SOLFEGE) | d.scale);
    CRS_FIX(transpose, -24, 24);
    CRS_FIX(single, 0, CR_SINGLE_SPLIT_BASS);      /* (v14: 2 Split Bass) */
    CRS_FIX(split_pc, 0, 11);
#if BOARD_SMK37
    if (!s->vel)                                   /* the SMK builds of 0.22 (PR #14): Velocity 0 "keys", the keys'
                                                    * velocity; since 0.24 that is Key Touch Dynamic (Velocity 100) */
        s->bass_on = (uint8_t)((s->bass_on & 3u) | CRS_TOUCH_BITS(1u, CRS_VSENS(s), 0u));
#endif
    CRS_FIX(vel, 1, 127);
    CRS_FIX(bass_on, 0, 255);                      /* (bit 1: the KEY layer's Numerals, CRS_NUMERALS; bits 2..7 Key Touch
                                                    * and the two sensitivities, CRS_TOUCH / VSENS / ATSENS: 0.22) */
    CRS_FIX(bass_mode, 0, CR_BASS_SOLO);
    CRS_FIX(bass_voicing, -2, 4);
    CRS_FIX(perform_on, 0, 1);
    CRS_FIX(perform_mode, 0, CRS_NPM - 1);
    CRS_FIX(perf_sel, 0, 6);
    CRS_FIX(sticky, 0, 0x7F);                      /* (0.24: bit 1 Chord Keys, bit 2 Full K Shorts, bits 3 .. 6 the SUS
                                                    * Window; bit 7 unknown: the default byte) */
    if (CRS_SUSWIN_CODE(s) > 10)                   /* (above 80 ms: 30, the default, the other bits kept) */
        s->sticky = (uint8_t)(s->sticky & 0x07u);
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
    CRS_FIX(raw_sound, 0, 0x3F);                   /* (0.24: bits 1 .. 5 MIDI Keys' channel) */
    if (CRS_MIDI_KEYS(s) > 16u)                    /* (no such channel: Off, Raw Chord Sound kept) */
        s->raw_sound = (uint8_t)(s->raw_sound & 1u);
    if ((s->view & 0x38u) || CRS_VIEW(s) > 5u)    /* (v9: bit 7 the lock, v12: bit 6 Dual Core On; 5 Geek Out 2
                                                    * since 0.22; an unknown bit or view: the default view, unlocked;
                                                    * Dual Core as the record has it: since 0.22 the default is On,
                                                    * and a record's Off is never turned On for it) */
        s->view = (uint8_t)(s->view & CRS_DC_ON);
    CRS_FIX(motion, 0, 2);
    CRS_FIX(leds, 0, (int)(CRS_LEDS_STOCK | CRS_KEYS_MOVED));   /* (0.25.1: bit 1 the move to FM-1, CRS_KEYS_MOVED) */
    CRS_FIX(fx_on, 0, 1);
    CRS_FIX(metro_on, 0, 1);
    CRS_FIX(metro_sig, 0, 2);
    CRS_FIX(metro_vol, 0, 100);
    CRS_FIX(loop_slot, 0, 9);
    CRS_FIX(pick_roots, 0, 255);                   /* (bits 5..7: Velocity Curve, CRS_VCURVE) */
    if (((s->pick_roots >> 5) & 7u) == 4u)         /* -4: not a curve: -3 */
        s->pick_roots = (uint8_t)((s->pick_roots & 0x1Fu) | (5u << 5));                    /* (bit 1: Output Classic, CRS_OUT_CLASSIC; bit 2: USB Rate, CRS_USB_48K;
                                                    * bit 3: Chord Names Standard, CRS_NAMES_STD; bit 4: 6th with 7th
                                                    * Octave up, CRS_SIXTH_UP) */
    CRS_FIX(usb_in, 0, 1);
    CRS_FIX(usb_level, 0, CRS_USB_FIXED);
    if (CRS_LOOP_MODE(s) >= CRS_NLOOPMODE)         /* (an unknown mode: the defaults; v13: bit 5 the KEY layer's keys,
                                                    * bits 6 / 7 LOCK's styles: every bit of the byte has a job) */
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
        if (in.version < 10u) {                    /* version 10: the arp's Hold defaults off (it latched with no
                                                    * control in the ARP layer); LOCK is the latch */
            int m;
            for (m = 0; m < CRS_NPM; m++) s->par[m][CR_P_HOLD] = 0;
        }
        if (in.version < 12u)                      /* version 12 (0.16): Dual Core is Off by default and view bit 6
                                                    * means On (v11: set = Off, On the default, so its bit cannot
                                                    * tell a chosen On from that default): every older record Off */
            s->view &= (uint8_t)~CRS_DC_ON;
        if (in.version < 13u)                      /* version 13: the KEY layer's keys in bit 5 of loop_rec (no older
                                                    * writer set it: an unknown bit there): pick, the default */
            s->loop_rec &= (uint8_t)~(CRS_KEY_PLAY | CRS_LOCK_KEEP | CRS_LOCK_PUSH);   /* (and LOCK's two styles) */
        if (in.version < 14u)                      /* version 14: Key plays and Keyboard in bits 1 / 2 of key_on (no
                                                    * older writer set them; one out of range there read as off): Chords,
                                                    * Chords; Single's 2 (Split Bass) did not exist: Full Octave */
        {
            s->key_on = (uint8_t)(s->key_on == 1u ? 1u : 0u);
            if (s->single > CR_SINGLE_SPLIT) s->single = CR_SINGLE_FULL;
        }
        /* version 15 (0.18): no field of the block changed (the session record beside it has what is new); bass_on
         * was 0 in every older record and stays 0 */
    }
    crs_sanitize(s);
#if !BOARD_SMK37
    if (!(s->leds & CRS_KEYS_MOVED))               /* 0.25.1: a record no firmware since 0.25.1 has written (any version: 0.23
                                                    * and older have no Chord Keys, 0.24 / 0.25 Classic or FM-1): FM-1,
                                                    * once; the mark keeps a later Classic (cr_settings.h CRS_KEYS_MOVED).
                                                    * The rest of the byte and the record as they were; rc unchanged (a
                                                    * current record stays current: the session beside it is applied) */
    {
        s->sticky = (uint8_t)(s->sticky | CRS_LAYOUT_FM1);
        s->leds = (uint8_t)(s->leds | CRS_KEYS_MOVED);
    }
#endif
    cr_settings_seal(s);
    return rc;
}

/* ------------------------------------------------------ the session record --- */
typedef char css_size_ok[sizeof(cr_session_t) == CSS_SIZE && sizeof(css_fx_t) == 128 && sizeof(css_part_t) == 384 ? 1 : -1];
static uint32_t css_fnv(const cr_session_t *x, uint32_t from, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)x;
    uint32_t h = 2166136261u, i;
    for (i = from; i < size; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
static void css_fx_defaults(css_fx_t *f)
{
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        ((uint8_t *)f)[i] = 0;
    f->version = CSS_FX_VERSION;
    f->sel = CSS_FXE_REVERB;
    for (i = 0; i < (uint32_t)CSS_NFXE; i++)
        f->amt[i] = CSS_FX_NONE;
}
void cr_session_defaults(cr_session_t *x)
{
    uint32_t i;
    for (i = 0; i < sizeof *x; i++)
        ((uint8_t *)x)[i] = 0;
    css_fx_defaults(&x->fx);
    cr_session_seal(x);
}
void cr_session_seal(cr_session_t *x)
{
    x->magic = CSS_MAGIC;
    x->version = CSS_VERSION;
    x->size = CSS_SIZE;
    x->check = css_fnv(x, CSS_HDR, CSS_SIZE);
}
uint32_t cr_session_hash(const cr_session_t *x) { return css_fnv(x, CSS_HDR + 4u, CSS_SIZE); }
int cr_session_part_trim(const css_part_t *r, int (*row)(unsigned engine, unsigned fk))
{
    if (!r->valid || !row || r->fk == CSS_FK_NONE || (r->kind != CSS_KIND_FACTORY && r->kind != CSS_KIND_USER))
        return 0;
    return row(r->eng, r->fk);
}
int cr_session_fx_get(const cr_session_t *x, unsigned tag, int *v)
{
    uint32_t i;
    for (i = 0; i < x->fx.n && i < (uint32_t)CSS_NFXP; i++)
        if (x->fx.par[i].tag == tag) {
            *v = x->fx.par[i].v;
            return 1;
        }
    return 0;
}
int cr_session_fx_set(cr_session_t *x, unsigned tag, int v)
{
    uint32_t i;
    for (i = 0; i < x->fx.n && i < (uint32_t)CSS_NFXP; i++)
        if (x->fx.par[i].tag == tag)
            break;
    if (i >= (uint32_t)CSS_NFXP || !tag || tag > 255u)
        return 0;
    x->fx.par[i].tag = (uint8_t)tag;
    x->fx.par[i].rsv = 0;
    x->fx.par[i].v = (int16_t)v;
    if (i == x->fx.n)
        x->fx.n++;
    return 1;
}
static void css_sanitize(cr_session_t *x)
{
    uint32_t k, i;
    if (x->voicing < -12 || x->voicing > 12) x->voicing = 0;   /* (cr_engine.c CR_VOICE_MIN / MAX: cr_voicing_step clamps) */
    if (x->octave < -2 || x->octave > 2) x->octave = 0;
    if (x->bass_on > 1u) x->bass_on = 0;
    if (x->lock > 1u) x->lock = 0;
    if (!x->lock) x->latched = 0;                  /* (the latch belongs to LOCK) */
    if (x->loop_rate < -7 || x->loop_rate > 3)
        x->loop_rate = 0;                          /* (cr_loop.h CRL_NRATE: x1/16 .. x4 around x1 (css_fit_ok); until
                                                    * 2026-10-11 -3 .. 3: a 0.18 dev build of before reads the four
                                                    * new ones as x1) */
    if (x->fx.version != CSS_FX_VERSION || x->fx.n > (uint32_t)CSS_NFXP)
        css_fx_defaults(&x->fx);                   /* another layout of the FX block: the sounds' own sends, the
                                                    * effects' defaults */
    for (i = 0; i < (uint32_t)CSS_NFXE; i++)
        if (x->fx.amt[i] > 127u) x->fx.amt[i] = CSS_FX_NONE;
    for (k = 0; k < 2u; k++) {
        css_part_t *r = &x->part[k];
        if (r->valid != 1u || r->np > (uint32_t)CSS_NP || r->nblob > (uint32_t)CSS_BLOB) {
            for (i = 0; i < sizeof *r; i++)
                ((uint8_t *)r)[i] = 0;             /* no sound stored: the pool's (the block's chord_sound / bass_sound) */
            continue;
        }
        r->edited = r->edited != 0u;
        r->name[CSS_NAME - 1] = 0;
    }
}
int cr_session_import(cr_session_t *x, const void *blk, uint32_t n)
{
    struct { uint32_t magic; uint16_t version, size; uint32_t check; } h;
    const uint8_t *src = (const uint8_t *)blk;
    uint32_t size, use, hc = 2166136261u, i;
    if (!blk || n < CSS_HDR + 4u) {
        cr_session_defaults(x);
        return 0;
    }
    crs_copy(&h, blk, CSS_HDR);
    size = h.size;
    if (h.magic == CSS_MAGIC && h.version != 0u && size >= CSS_HDR + 4u && size <= n)
        for (i = CSS_HDR; i < size; i++) {
            hc ^= src[i];
            hc *= 16777619u;
        }
    else
        hc = ~h.check;
    if (hc != h.check) {                           /* a bad magic / version / size / checksum: nothing stored */
        cr_session_defaults(x);
        return 0;
    }
    use = size > CSS_SIZE ? CSS_SIZE : size;       /* a newer writer with a longer record: its prefix is ours */
    if ((const void *)x != blk)                    /* (blk may be x itself, not overlap it otherwise) */
        crs_copy(x, blk, use);
    for (i = use; i < CSS_SIZE; i++)               /* an older, shorter record: what it lacks is 0, the default of every
                                                    * field added after it (another default: a migration line below) */
        ((uint8_t *)x)[i] = 0;
    if (use < (uint32_t)((const uint8_t *)&x->fx - (const uint8_t *)x) + (uint32_t)sizeof x->fx)
        css_fx_defaults(&x->fx);
    /* version-specific migrations go here, as cr_settings_import's: if (h.version < 2u) x->new_field = ...; */
    css_sanitize(x);
    cr_session_seal(x);
    return h.version == CSS_VERSION && h.size == CSS_SIZE ? 1 : 2;
}

/* ------------------------------------------------------ engine <-> record --- */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o)
{
    int m, i;
    unsigned p;                                    /* (a cr_param_t: that name is the screen's in the unit) */
    cr_set_playstyle(c, (cr_playstyle_t)s->playstyle);
    cr_set_ext_addition(c, (cr_extadd_t)s->extadd);
    cr_set_secret(c, (cr_secret_t)s->secret);
    cr_set_sixth_up(c, (int)CRS_SIXTH(s));         /* Options > 6th with 7th (bit 4 of pick_roots) */
    cr_set_key(c, (int)CRS_KEY_ON(s), s->tonic, (cr_scale_t)CRS_SCALE(s));
    cr_set_key_plays(c, (int)CRS_KEY_PLAYS(s));
    cr_set_key_snap(c, (cr_keysnap_t)CRS_SNAP(s));
    cr_set_keyboard(c, (int)CRS_KEYBOARD(s), c->split_note);   /* (the split key: the UI's, with its octave) */
    cr_set_transpose(c, s->transpose);
    cr_set_single_notes(c, (cr_single_t)s->single, s->split_pc);
    cr_set_bass_mode(c, (cr_bassmode_t)s->bass_mode);
    cr_bass_voicing_step(c, s->bass_voicing - c->bass_voicing);
    cr_set_bass(c, s->bass_on & 1u);               /* (bit 1: Numerals, the UI's) */
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            if (cr_get_param(c, (cr_pmode_t)m, p) != s->par[m][p])
                cr_set_param(c, (cr_pmode_t)m, p, s->par[m][p]);
    cr_set_perform_mode(c, (cr_pmode_t)s->perform_mode);
    cr_set_perform(c, s->perform_on);
    cr_set_sticky(c, (int)CRS_LATCH(s));           /* (bit 0: the other bits are the UI's Options) */
    cr_set_tempo(c, s->bpm);
    cr_set_stream(c, CR_STREAM_RAW, s->midi_en[CR_STREAM_RAW] || CRS_RAW(s));
    if (o) {
        for (i = 0; i < 3; i++) {
            o->midi_en[i] = s->midi_en[i];
            o->ch[i] = s->midi_ch[i];
        }
        o->part[CR_STREAM_MAIN] = 0;
        o->part[CR_STREAM_BASS] = 1;
        o->part[CR_STREAM_RAW] = CRS_RAW(s) ? 0 : CRS_NONE;
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
    s->pick_roots = (uint8_t)((s->pick_roots & (uint8_t)~CRS_SIXTH_UP) | (c->sixth_up ? CRS_SIXTH_UP : 0u));
    s->key_on = (uint8_t)(CRS_KEY_BYTE(c->key_on, c->key_notes, c->full) | CRS_SNAP_BITS(c->snap));
    s->tonic = c->tonic;
    s->scale = (uint8_t)((s->scale & CRS_SOLFEGE) | c->scale);   /* (bit 7: Numerals' Solfege, the UI's) */
    s->transpose = c->transpose;
    s->single = c->single;
    s->split_pc = c->split_pc;
    s->bass_on = (uint8_t)((s->bass_on & CRS_NUMERALS) | (c->bass_on ? 1u : 0u));
    s->bass_mode = c->bass_mode;
    s->bass_voicing = c->bass_voicing;
    s->perform_on = c->perform_on;
    s->perform_mode = c->perform_mode;
    s->sticky = (uint8_t)((s->sticky & (uint8_t)~1u) | (c->sticky ? 1u : 0u));   /* (bits 1 .. 6: the UI's) */
    s->bpm = c->bpm;
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            s->par[m][p] = (int16_t)cr_get_param(c, (cr_pmode_t)m, p);
    if (o) {
        for (i = 0; i < 3; i++) {
            s->midi_en[i] = o->midi_en[i] ? 1u : 0u;
            s->midi_ch[i] = (uint8_t)(o->ch[i] & 15u);
        }
        s->raw_sound = (uint8_t)((s->raw_sound & (uint8_t)~1u) | (o->part[CR_STREAM_RAW] != CRS_NONE ? 1u : 0u));
        s->clock_mode = o->clock_in ? CRS_CLOCK_IN : o->clock_out ? CRS_CLOCK_OUT : CRS_CLOCK_OFF;
    }
    cr_settings_seal(s);
}

/* =========================================== the firmware unit: flash + UI glue --- */
#ifdef CR_VERSION
#ifndef CR_SETTINGS_BUSY
#define CR_SETTINGS_BUSY() 0                       /* (cr_ui.c defines it: a loop is playing, no flash erase) */
#endif
#define CRS_QUIET_MS ST_SETTLE_MS                   /* an automatic change is saved once nothing changed for this long */
#define CRS_IDLE_MS ST_SILENT_MS                   /* .. and nothing sounded for this long (crs_sounding); storage.c
                                                    * st_write_due has the whole rule (the quiet writes, 0.22) */
#define CRS_QUIET_PEAK 32                          /* the master output (audio.c scope_buf, Q15): -60 dBFS */

#include "settings_persist.c"                      /* Felucca's record, PER5: its last member is cr_settings_t */

#define CSS_GAP_MS ST_GAP_MS                       /* an automatic write waits this long after another object's */

static persist_t crs_rec;                          /* the last record read or written (Felucca's fields + ours) */
static cr_settings_t crs_seen;                     /* the last captured state (change detection) */
static uint8_t crs_hold_seen;                      /* .. and Felucca's HOLD then (panel.c settings_hold, the record's
                                                    * `bold`): Options > Hold Time sets it, outside our block. The one
                                                    * field of Felucca's the UI changes with no save call: the panel
                                                    * table is saved by the calibration (cr_settings_save); the
                                                    * palette, LOWCUT and Felucca's LEDS have no control here (the
                                                    * block has ChoralRoot's palette and LEDs) */
static uint8_t crs_booted, crs_loaded, crs_pending;
static uint32_t crs_change_ms, crs_retry_ms;
static uint32_t crs_loud_ms;                       /* the last time something sounded (crs_sounding, the poll) */
static int crs_last_rc;                            /* the last import: 1 current, 2 migrated, 0 defaults */
static uint32_t crs_saves;                         /* flash writes done (diagnostics, the emulator) */
/* the session (cr_settings.h cr_session_t; docs/SETTINGS.md "The session"): ONE copy in RAM, css_buf, in the pool: what
 * flash held at power-on (or at a revert) until cr_settings_load applied it, else the scratch the state is captured
 * into for a write; what flash holds is kept as a hash (cr_session_hash), so that "unchanged: no erase" costs no
 * second kilobyte */
typedef char css_fit_ok[P_COUNT <= CSS_NP && ENG_BLOB_MAX <= CSS_BLOB && sizeof psnd[0].name == CSS_NAME &&
                        CR_NMOD <= 8 && CRL_NRATE == 11 && CRL_RATE_1 == 7 ? 1 : -1];
static cr_session_t css_buf __attribute__((section(".pool")));
static uint32_t css_flash;                         /* cr_session_hash of the session in flash (0: none) */
static uint32_t crs_obj_crc;                       /* the settings object's payload CRC in flash (0: none): the session's
                                                    * `bind` */
static uint8_t css_have;                           /* power-on, a revert: css_buf holds a session to apply
                                                    * (cr_settings_load) */
static int8_t css_last_rc;                         /* the session at power-on: 1 current, 2 migrated, 0 none, -1 one
                                                    * that does not belong to these settings (not applied) */
static uint32_t crs_wr_ms;
static uint32_t css_saves;                         /* flash writes done (diagnostics, the emulator) */
/* the quiet writes (0.22; storage.c "erase ahead", docs/SETTINGS.md "When flash is written"): what the poll wrote by
 * itself (an Option's change, a favourite's), the erases done ahead (at power-on, after a long idle), and the erase
 * ahead given up for this power-on after a flash error (the saves then erase for themselves, as before 0.22) */
static struct { uint32_t auto_saves, ahead, ahead_boot; uint8_t off; } crq;

/* the USB settings to usb.c (which devices the host is given; ua_off at boot, usb_start builds the configuration)
 * and fx.c (USB Level) */
static void crs_usb_apply(const cr_settings_t *s)
{
    fx_usb_fixed = s->usb_level == CRS_USB_FIXED;
    fx_output_set(CRS_OUT_CLEAN(s), 1);            /* Options > Output: before the first block, no fade */
#if FELUCCA_UAC
    ua_off = ua_off_want = (uint8_t)((s->usb_in ? 0u : UA_OFF_IN) | (CRS_USB48(s) ? 0u : UA_OFF_48));   /* (USB Rate) */
#endif
}

/* the session in flash -> css_buf (css_have: one to apply; css_last_rc, css_flash). Power-on and a revert */
static void css_read(void)
{
    int n = 0;
    css_have = 0;
    css_flash = 0;
    css_last_rc = 0;
#if FELUCCA_FLASH
    if (flash_ok && (n = st_load(OBJ_SESSION, &css_buf, sizeof css_buf)) > 0 &&
        (n = cr_session_import(&css_buf, &css_buf, (uint32_t)n)) != 0) {
        if (crs_last_rc == 1 || crs_saves || css_buf.bind == crs_obj_crc) {   /* it belongs to these settings
                                                    * (cr_settings.h; crs_saves: this firmware has written the block
                                                    * since power-on) */
            css_have = 1;
            css_last_rc = (int8_t)n;
            css_flash = cr_session_hash(&css_buf);
        } else {
            css_last_rc = -1;
        }
    }
#endif
    (void)n;
}

static uint8_t crs_lowcut_found;                   /* Felucca's SPEAKER setting as this power-on found it (diagnostics) */
static uint8_t crs_dc_bar;                         /* 1: Dual Core is Off whatever the record says (cr_cpu1.h: core 1 kept
                                                    * off after a crash beside it, or given up); the load reads Off and
                                                    * the poll saves it. The player's own choice in Options clears it */
static void cr_settings_boot(void)                 /* persist_boot (flash_ok known), before settings_init / panel_init */
{
    int n = -1;
    crs_booted = 1;
    memset(&crs_rec, 0, sizeof crs_rec);
#if FELUCCA_FLASH
    if (flash_ok)
        n = st_load(OBJ_SETTINGS, &crs_rec, sizeof crs_rec);
    crs_obj_crc = n > 0 ? st_crc32(&crs_rec, (uint32_t)n) : 0u;   /* (as stored: before the import normalises it) */
#endif
    if (n <= 0 || !settings_import(&crs_rec, n)) {  /* absent / unknown: defaults, Felucca's fields as they are */
        memset(&crs_rec, 0, sizeof crs_rec);
        settings_export(&crs_rec);
    }
    /* Felucca's SPEAKER setting (settings.lowcut: 1 LOWCUT, 2 BASS+, fx.c's master) is in the same object, and a unit
     * that ran Felucca with it on brought it along: every sound high-passed (BASS+: with harmonics added) by a stage
     * ChoralRoot has no control for and Reset Settings did not reach (0.19, docs/AUDIO-QUALITY.md "Left behind").
     * ChoralRoot holds it off: here before settings_init applies it, whatever flash, a backup or the RAM that
     * survives a reset says; crs_rec keeps what flash holds, so the next write of the object stores the 0 */
    crs_lowcut_found = (uint8_t)(settings.lowcut % 3u);
    settings.lowcut = 0;
    fx_lowcut = 0;
    crs_last_rc = cr_settings_import(&crs_rec.cr, &crs_rec.cr, sizeof crs_rec.cr);
    crs_usb_apply(&crs_rec.cr);                    /* (before usb_start: the configuration the host first reads) */
    cr_c1_off = (uint8_t)!CRS_DUALCORE(&crs_rec.cr);   /* Options > Dual Core Off: core 1 not started (On: the default
                                                        * of a unit with no record since 0.22) */
#if CR_CPU1
#ifdef ST_BLOCKED
    if (!ST_BLOCKED())                             /* (SAFE MODE saves nothing: the bar waits for the next boot) */
#endif
        cr_c1_bar_boot(bootguard.cls);             /* a crash or a hang beside core 1 last time: Off (cr_cpu1.h) */
    if (cr_c1_barred)
        crs_dc_bar = 1;
#endif
    css_read();                                    /* the session: read here, applied by cr_settings_load */
}

/* the record's pool_pos slot of the engine of ENGINE_ORDER rank r (cr_settings.h): CRS_NENG = 11 slots, fixed per engine
 * in 0.13's display order, so a 0.13 record means what it meant whatever the order shown today; ANALOG's slot 0 is FM
 * TONE's since version 7 (ANALOG retired, FELUCCA_ANALOG 0). PHASE's, LOFI's, VOICE's, TRIO's, PHYS's and NOISE's slots (3, 5, 6, 7,
 * 9, 10) are reserved since 0.18 (the engines retired: no rank asks for them, the record keeps their bytes). -1: an engine
 * with no slot (none on ChoralRoot) */
#if FELUCCA_ANALOG || !FELUCCA_VA || !FELUCCA_CZ || !FELUCCA_QUAD
#error "cr_settings.c: the record's engine slots are ChoralRoot's (FELUCCA_ANALOG 0, FELUCCA_VA / CZ / QUAD 1)"
#endif
typedef char crs_neng_ok[CRS_NENG >= NENG_SHOWN && CRS_ENG_QUAD == ENGI_QUAD ? 1 : -1];   /* (a slot per engine shown) */
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
    s->key_on = (uint8_t)(CRS_KEY_BYTE(cs.key_on, cs.key_notes, cs.kbd_full) | CRS_SNAP_BITS(cs.snap));
    s->tonic = cs.tonic;
    s->scale = (uint8_t)(cs.scale | (cs.numerals == 2u ? CRS_SOLFEGE : 0u));   /* (bit 7: Numerals' Solfege) */
    s->transpose = cs.transpose;
    s->single = cs.single;
    s->vel = cs.vel;
    s->bass_mode = cs.bass_mode;
    s->bass_on = (uint8_t)((cs.numerals == 1u ? CRS_NUMERALS : 0u) | CRS_TOUCH_BITS(cs.touch, cs.vel_sens, cs.at_sens));
                                                   /* (bit 0 always 0 here: Bass on is the session's, css_capture; an
                                                    * older firmware would switch the engine's bass on from it. Bit 1:
                                                    * the KEY layer's Numerals On, which 0.17 .. 0.21 read as 0;
                                                    * Solfege is scale's bit 7, not this byte's) */
    s->perform_on = cs.perform_on;
    s->perf_sel = cs.perf_sel;
    s->sticky = CRS_STICKY_BYTE(cs.sticky, cs.layout, cs.full_short, cs.suswin);   /* (docs/CHORD-LAYOUT.md) */
    s->bpm = cs.bpm;
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        s->par[i / CRS_NPAR][i % CRS_NPAR] = cs.par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++) {
        s->midi_en[i] = cs.ch[i] != 0u;
        if (cs.ch[i])
            s->midi_ch[i] = (uint8_t)(cs.ch[i] - 1u);
    }
    s->raw_sound = CRS_RAW_BYTE(cs.raw_sound, cs.midi_keys);
    s->loop_sync = cs.loop_len;
    s->loop_quant = cs.loop_quant;
    s->loop_count_in = cs.loop_count_in;
    s->loop_level = cs.loop_level;
    s->metro_on = cs.metro;
    s->metro_sig = cs.metro_sig;
    s->metro_vol = cs.metro_vol;
    s->loop_slot = cs.loop_slot;
    s->loop_rec = (uint8_t)(CRS_LOOP_REC(cs.loop_mode, cs.loop_keys) | (cs.key_play ? CRS_KEY_PLAY : 0u) |
                            (cs.lock_keep ? CRS_LOCK_KEEP : 0u) | (cs.lock_push ? CRS_LOCK_PUSH : 0u));
    s->pick_roots = (uint8_t)((cs.out_clean ? 0u : CRS_OUT_CLASSIC) | (cs.usb48 ? CRS_USB_48K : 0u) |
                              (cs.names_std ? CRS_NAMES_STD : 0u) | (cs.sixth_up ? CRS_SIXTH_UP : 0u) |
                              CRS_VCURVE_OF(cs.vel_curve));   /* (bit 0 reserved since 0.18: the engine picker's roots always play; written
                                                    * as "play", so an older firmware reading this record plays too) */
    s->usb_in = cs.usb_in;
    s->usb_level = cs.usb_fixed;
    s->split_pc = cs.split;
    s->view = (uint8_t)(CRS_VIEW_BYTE(cs.view, cs.view_lock) | (cs.dualcore ? CRS_DC_ON : 0u));
    s->motion = cr_motion;
    s->leds = (uint8_t)((cs.leds ? CRS_LEDS_STOCK : 0u) | (s->leds & CRS_KEYS_MOVED));   /* (the record's mark, as
                                                    * the import left it: set, but on the SMK-37) */
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

/* ------------------------------------------------------------ the session's glue --- */
/* the FX layer by tag (cr_settings.h css_fx_t): cr_ui.c CU_FX's effects in its order, its pages (CU_FXP's order: the
 * second pages have a tag of their own for `sel`), and the effects' GLOBAL parameters: the chorus's (song.g) and, since
 * the delay and the reverb are cr_fx.c's (0.18), its xfp[] (x: 1). A reworked effect: its new parameters get new
 * CSS_FXP_* tags and a line here; a parameter gone loses its line (its tag stays retired) */
static const uint8_t CSS_FXE[CU_NFX] = {CSS_FXE_REVERB, CSS_FXE_CHORUS, CSS_FXE_DELAY, CSS_FXE_DRIVE};
static const uint8_t CSS_FXS[CU_NFXP] = {CSS_FXE_SENDS, CSS_FXE_REVERB, CSS_FXE_REVERB2, CSS_FXE_CHORUS, CSS_FXE_DELAY,
                                         CSS_FXE_DELAY2, CSS_FXE_DRIVE, CSS_FXE_FILTER, CSS_FXE_ROUTE};   /* CU_FXP's
                                         * order (ROUTE, 8: a sel only, past amt[]: amt is indexed by CSS_FXE[]) */
static const struct { uint8_t tag, x, id, opt; } CSS_FXG[] = {   /* x: 0 song.g, 1 xfp[], 2 the bass part's p[]; opt:
                                                                  * written only off its default (the block's room:
                                                                  * cr_settings.h CSS_NFXP) */
    {CSS_FXP_CRATE, 0, G_CRATE}, {CSS_FXP_CDEPTH, 0, G_CDEPTH},
    {CSS_FXP_BS_REV, 2, P_REV}, {CSS_FXP_BS_CHO, 2, P_CHOR}, {CSS_FXP_BS_DLY, 2, P_DLY}, {CSS_FXP_BS_DRIVE, 2, P_DIST},
    {CSS_FXP_XD_TIME2, 1, XP_DTIME}, {CSS_FXP_XD_DIV, 1, XP_DDIV}, {CSS_FXP_XD_SPREAD, 1, XP_DSPREAD},
    {CSS_FXP_XD_RATIO, 1, XP_DRATIO}, {CSS_FXP_XD_FDBK, 1, XP_DFDBK}, {CSS_FXP_XD_SYNC, 1, XP_DSYNC},
    {CSS_FXP_XD_MODE, 1, XP_DMODE}, {CSS_FXP_XD_TONE, 1, XP_DTONE}, {CSS_FXP_XD_WOW, 1, XP_DWOW},
    {CSS_FXP_XR_SIZE, 1, XP_RSIZE}, {CSS_FXP_XR_DAMP, 1, XP_RDAMP}, {CSS_FXP_XR_DECAY, 1, XP_RDECAY},
    {CSS_FXP_XR_PRE, 1, XP_RPRE}, {CSS_FXP_XR_MOD, 1, XP_RMOD}, {CSS_FXP_XR_LOW, 1, XP_RLOW},
    {CSS_FXP_XR_WIDTH, 1, XP_RWIDTH}, {CSS_FXP_XC_WIDTH, 1, XP_CWIDTH},
    {CSS_FXP_XQ_LOW, 1, XP_FLOW, 1}, {CSS_FXP_XQ_HIGH, 1, XP_FHIGH, 1}, {CSS_FXP_XQ_SLOPE, 1, XP_FSLOPE, 1},
    {CSS_FXP_XQ_ON, 1, XP_FON, 1},
    {CSS_FXP_XF_ORDER, 1, XP_ORDER, 1}, {CSS_FXP_XF_RMODE, 1, XP_RMODE, 1}};   /* (the routing, issue 21: as the filter's) */
_Static_assert(NELEM(CSS_FXG) <= CSS_NFXP, "the FX block holds every global parameter");
_Static_assert(offsetof(cr_session_t, part) == 196 && sizeof(css_fx_t) == 128 && CSS_FXE_ROUTE >= CSS_NFXE,
               "the routing's entry took rsv_fx: the parts where they were; ROUTE is a sel, never an amt[] index");
_Static_assert(CSS_FXP_DCOLOR == 8 && CSS_FXP_XD_TIME == 9 && CSS_FXP_XR_SIZE == 18 && CSS_FXP_XR_WIDTH == 24 &&
               CSS_FXP_XD_TIME2 == 25 && CSS_FXP_BS_REV == 26 && CSS_FXP_BS_DRIVE == 29 && CSS_FXP_XC_WIDTH == 30 && CSS_FXP_XQ_LOW == 31 && CSS_FXP_XQ_ON == 34 && CSS_FXE_DRIVE == 3 && CSS_FXE_REVERB2 == 4 && CSS_FXE_DELAY2 == 5 && CSS_FXE_SENDS == 6 && CSS_FXE_FILTER == 7 && CSS_FXP_XF_ORDER == 35 && CSS_FXP_XF_RMODE == 36 && CSS_FXE_ROUTE == 8, "the session's FX tags are never renumbered");
static int16_t *css_fxg_p(uint32_t i)
{
    if (CSS_FXG[i].x == 2u && CSS_FXG[i].id == P_DIST)
        return cu_bdrive_p();                      /* (the bass part's drive SEND: beside its track while its engine is
                                                    * FM TONE, whose Drive is the sound's and rides in part[1].p[]) */
    return CSS_FXG[i].x == 2u ? &trk[CR_PART_BASS].p[CSS_FXG[i].id] : CSS_FXG[i].x ? &xfp[CSS_FXG[i].id] : &song.g[CSS_FXG[i].id];
}
static int css_fxg_ok(uint32_t i, int v)
{
    return CSS_FXG[i].x == 2u ? v >= 0 && v <= 127
         : CSS_FXG[i].x ? v >= XFP[CSS_FXG[i].id].min && v <= XFP[CSS_FXG[i].id].max
                        : v >= GP[CSS_FXG[i].id].min && v <= GP[CSS_FXG[i].id].max;
}
static void css_fxg_defaults(void)                 /* a new unit's effects (cr_out.c's and params.c's) */
{
    uint32_t i;
    xf_defaults();
    for (i = 0; i < NELEM(CSS_FXG); i++)
        if (!CSS_FXG[i].x)
            song.g[CSS_FXG[i].id] = GP[CSS_FXG[i].id].def;
    cu_sends_default();                            /* (the parts' sends: the chord part's amounts, the bass part's) */
}

/* a part's sound as it is -> its record (as the engine picker's snapshot, cr_ui.c cu_pick_begin) */
static void css_part_get(uint32_t part, css_part_t *r)
{
    const track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const eng_deep_t *d = cp_deep(t);
    uint32_t i;
    r->valid = 1;
    r->eng = t->eng_req;
    r->preset = t->preset;
    r->user = t->user;
    r->trim = t->trim;
    r->edited = psnd[part].edited != 0u;
    r->kind = psnd[part].kind;
    r->fk = psnd[part].fk;
    r->slot = psnd[part].slot;
    r->np = P_COUNT;
    for (i = 0; i < CSS_NAME - 1u && psnd[part].name[i]; i++)
        r->name[i] = psnd[part].name[i];
    for (i = 0; i < P_COUNT; i++)
        r->p[i] = t->p[i];
    if (d && d->blob_get && d->blob_set && d->blob_size && d->blob_size <= CSS_BLOB) {
        d->blob_get(t, r->blob);
        r->nblob = (uint8_t)d->blob_size;
    }
}
_Static_assert(CSS_KIND_INIT == PK_INIT && CSS_KIND_FACTORY == PK_FACTORY && CSS_KIND_USER == PK_USER &&
               CSS_FK_NONE == PF_NONE, "the session's pool entry is cr_bank.c's");
static int css_trim_row(unsigned e, unsigned fk)   /* cr_bank.c's table, for cr_session_part_trim */
{
    return e < NENGINES && fk < ENGINES[e]->npresets ? cb_trim(e, fk) : 0;
}
/* .. and back (cu_pick_end's restore; the trim: the table's, cr_settings.h); 0: not this firmware's (another parameter count, an engine gone or without
 * this blob, a pool entry that cannot be): the part keeps the sound the block's chord_sound / bass_sound loaded */
static int css_part_put(uint32_t part, const css_part_t *r)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const eng_deep_t *d;
    uint32_t i, e = r->eng;
#if FELUCCA_PROPHET
    uint32_t r_preset = r->preset, r_fk = r->fk, r_kind = r->kind, r_edited = r->edited;   /* (P5: its place in this build) */
#else                                              /* (the record's own: the image without P5 byte for byte) */
#define r_preset r->preset
#define r_fk r->fk
#define r_kind r->kind
#define r_edited r->edited
#endif
    if (!r->valid || r->np != P_COUNT || e >= NENGINES || !cu_engine_melodic(e))
        return 0;
#if FELUCCA_PROPHET
    /* P5: the record's `preset` and `fk` are places in the pool of the build that wrote it, and the pool is a list
     * that a later build may change (assets/prophet5-factory/shipped.txt). The record also holds the program itself
     * (the blob), and the program names itself: its Sequential number (p5_patch_id). The place is taken from that:
     * the same program's place here, or, when this build does not ship it, no place (the pool's INIT position, shown
     * edited) with the program and its name as they were. The sound is the blob's either way */
    if (e == ENGI_PROPHET && r->nblob == sizeof(p5_patch_t) && r_kind == PK_FACTORY) {
        p5_patch_t pp;
        uint32_t k;
        memcpy(&pp, r->blob, sizeof pp);
        k = p5_preset_of(p5_patch_id(&pp));
        if (k != r_preset || k != r_fk) {
            cu_trace("session: part %u P5 program %u: place %u -> %u\n", (unsigned)part, (unsigned)p5_patch_id(&pp),
                     (unsigned)r_fk, (unsigned)k);
            r_preset = k;
            r_fk = k ? k : PF_NONE;
            if (!k) {
                r_kind = PK_INIT;
                r_edited = 1;
            }
        }
    }
#endif
    d = ENGINES[e]->deep;
    if (r->nblob != (d && d->pages && d->npages && d->get && d->set && d->blob_get && d->blob_set &&
                     d->blob_size <= CSS_BLOB ? d->blob_size : 0u)
#if FELUCCA_QUAD
        && !(e == ENGI_QUAD && r->nblob == QUAD_BLOB_V3)   /* (an FM TONE patch of before 2026-10-14: converted) */
#endif
    )
        return 0;
    if ((r_kind == PK_USER && r->slot >= UP_SLOTS) || r->user > UP_SLOTS ||
        (r_kind == PK_FACTORY && r_fk >= ENGINES[e]->npresets))
        return 0;
    fm1_irq_off();
    t->eng_req = (uint8_t)e;
    t->preset = r_preset;
    t->user = r->user;
    t->trim = (int8_t)cr_session_part_trim(r, css_trim_row);   /* (not r->trim: a row removed since stays removed) */
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = r->p[i];
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    fm1_irq_on();
    fm6_track_loaded(t);                           /* (the engine's own state for the sound) */
    if (r->nblob) {                                /* the deep patch as it was left (zeros past a shorter one) */
        uint8_t bb[CSS_BLOB];
        memset(bb, 0, sizeof bb);
        memcpy(bb, r->blob, r->nblob);
        d->blob_set(t, bb);
    }
#if FELUCCA_PROPHET
    if (e == ENGI_PROPHET)
        p5_voice_restore(t, r->p[P_VOICE]);        /* (the load took the factory program's UNISON: the part's mode as
                                                    * it was left) */
#endif
    str_cpy(psnd[part].name, r->name, sizeof psnd[0].name);
    psnd[part].edited = r_edited;
    psnd[part].kind = r_kind;
    psnd[part].fk = r_fk;
    psnd[part].slot = r->slot;
    t->engine = t->eng_req;                        /* (power-on: no fade) */
    return 1;
}
#if !FELUCCA_PROPHET
#undef r_preset
#undef r_fk
#undef r_kind
#undef r_edited
#endif

/* the unit's state now -> *x (sealed; `bind` is the writer's: css_write) */
static void css_capture(cr_session_t *x)
{
    uint32_t i;
    cr_session_defaults(x);
    fm1_irq_off();
    x->voicing = cr.voicing;
    fm1_irq_on();
    x->octave = cu.octave;
    x->bass_on = cs.bass_on != 0u;
    x->lock = cu.clock != 0u;
    x->latched = x->lock ? cu.mlatch : 0u;
    x->loop_rate = (int8_t)((int)cs.loop_rate - (int)CRL_RATE_1);
    x->fx.sel = CSS_FXS[cu_fx_pg()];
    for (i = 0; i < CU_NFX; i++)
        x->fx.amt[CSS_FXE[i]] = (uint8_t)(cs.fx_amt[i] & 127u);
    for (i = 0; i < NELEM(CSS_FXG); i++)
        if (!CSS_FXG[i].opt || *css_fxg_p(i) != XFP[CSS_FXG[i].id].def)   /* (opt: a default is not written) */
            cr_session_fx_set(x, CSS_FXG[i].tag, *css_fxg_p(i));
    css_part_get(0, &x->part[0]);
    css_part_get(1, &x->part[1]);
    cr_session_seal(x);
}

/* power-on (cr_settings_load, once): the session over what the block gave. Nothing sounds yet: the engine's fields
 * directly (IRQ off), LOCK's latch through the queue as the keys post it */
static void css_apply(const cr_session_t *x)
{
    uint32_t i, m, got[2];
    int v;
    got[0] = (uint32_t)css_part_put(0, &x->part[0]);
    got[1] = (uint32_t)css_part_put(1, &x->part[1]);
    for (i = 0; i < NELEM(CSS_FXG); i++)             /* (a tag not stored, a value out of range: the default stays; a
                                                    * 0.17 / early 0.18 record's retired tags are not read) */
        if (cr_session_fx_get(x, CSS_FXG[i].tag, &v) && css_fxg_ok(i, v))
            *css_fxg_p(i) = (int16_t)v;
    for (i = 0; i < CU_NFXP; i++)
        if (CSS_FXS[i] == x->fx.sel) {             /* (a tag this firmware does not know: the page stays. A record
                                                    * from before the Sends page opens on the effect's page it names) */
            if (CU_FXP[i].page != CU_FXPG_SENDS && CU_FXP[i].page != CU_FXPG_FILTER && CU_FXP[i].page != CU_FXPG_ROUTE)
                cs.fx_sel = CU_FXP[i].fx;
            cs.fx_page = CU_FXP[i].page;
        }
    for (i = 0; i < CU_NFX; i++)                   /* (the chord part's sends; the bass part's: CSS_FXG above, over what
                                                    * its record held) */
        if (x->fx.amt[CSS_FXE[i]] != CSS_FX_NONE)
            cs.fx_amt[i] = x->fx.amt[CSS_FXE[i]];
    cu_fx_apply();
    fm1_irq_off();
    cr_voicing_step(&cr, x->voicing - cr.voicing);
    if (x->bass_on)
        cr_set_bass(&cr, 1);
    fm1_irq_on();
    cu.octave = x->octave;                         /* (cu_post_kbd follows: the split key's note moves with it) */
    cs.loop_rate = (uint8_t)((int)x->loop_rate + (int)CRL_RATE_1);   /* (cu_loop_conf posts it with the looper's) */
    if (x->bass_on) {                              /* v15: the bass comes back on, on its sound (ALGORITHM's place:
                                                    * cu_pos_sync) */
        cs.bass_on = 1;
        cs.bass_sound = 1;
    }
    if (x->lock) {                                 /* LOCK and the chord keys it held down (cu_lock_tap, cu_mod_release) */
        cu.clock = 1;
        cu.mlatch = x->latched;
        cr_post(CRE_MODLOCK, 0, 0, 1);
        for (m = 0; m < CR_NMOD; m++)
            if ((x->latched >> m) & 1u)
                cr_post(CRE_MOD, m, 0, 1);
    }
    cu_trace("session: applied (parts %u %u, voicing %d, octave %d, bass %u, lock %u latched %02X, fx %u)\n",
             (unsigned)got[0], (unsigned)got[1], (int)x->voicing, (int)x->octave, (unsigned)x->bass_on,
             (unsigned)x->lock, (unsigned)x->latched, (unsigned)cs.fx_sel);
}

static int css_write(void)                         /* the state now -> flash; 0: written, or nothing to write */
{
#if FELUCCA_FLASH
    uint32_t h;
    if (!flash_ok)
        return 0;
#ifdef ST_BLOCKED
    if (ST_BLOCKED())
        return 0;                                  /* SAFE MODE: the flash untouched */
#endif
    css_capture(&css_buf);
    h = cr_session_hash(&css_buf);
    if (h == css_flash)
        return 0;                                  /* unchanged: no erase cycle */
    css_buf.bind = crs_obj_crc;
    cr_session_seal(&css_buf);
    if (st_save(OBJ_SESSION, &css_buf, CSS_SIZE) != 0)
        return -1;
    css_flash = h;
    css_saves++;
    crs_wr_ms = fm1_ms;
#endif
    return 0;
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
    cs.key_on = (uint8_t)CRS_KEY_ON(s);
    cs.key_notes = (uint8_t)CRS_KEY_PLAYS(s);
    cs.kbd_full = (uint8_t)CRS_KEYBOARD(s);
    cs.snap = (uint8_t)CRS_SNAP(s);
    cs.numerals = (uint8_t)(CRS_SOL(s) ? 2u : CRS_NUMS(s));   /* Off / On (bass_on bit 1) / Solfege (scale bit 7) */
    cs.touch = (uint8_t)CRS_TOUCH(s);              /* Options > Key Touch and the sensitivities (docs/VELOCITY.md) */
    cs.vel_curve = (int8_t)CRS_VCURVE(s);
    cs.vel_sens = (uint8_t)CRS_VSENS(s);
    cs.at_sens = (uint8_t)CRS_ATSENS(s);
    cu_touch_apply();
    cs.tonic = s->tonic;
    cs.scale = (uint8_t)CRS_SCALE(s);
    cs.transpose = s->transpose;
    cs.single = s->single;
    cs.vel = s->vel;
    cs.bass_mode = s->bass_mode;
    cs.perform_on = s->perform_on;
    cs.perf_sel = s->perf_sel;
    cs.sticky = (uint8_t)CRS_LATCH(s);
    cs.layout = (uint8_t)(CU_FN_KEYS ? CRS_LAYOUT(s) : 0u);   /* (the SMK-37: hidden, Classic) */
    cs.full_short = (uint8_t)CRS_FULLSHORT(s);
    cs.suswin = (uint8_t)CRS_SUSWIN_MS(s);
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        cs.par[i / CRS_NPAR][i % CRS_NPAR] = s->par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++)
        cs.ch[i] = (uint8_t)(s->midi_en[i] ? s->midi_ch[i] + 1u : 0u);
    cs.raw_sound = (uint8_t)CRS_RAW(s);
    cs.midi_keys = (uint8_t)(CU_FN_KEYS ? CRS_MIDI_KEYS(s) : 0u);
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
    cs.key_play = (uint8_t)CRS_KEY_KEYS(s);
    cs.lock_keep = (uint8_t)((s->loop_rec & CRS_LOCK_KEEP) != 0);
    cs.lock_push = (uint8_t)((s->loop_rec & CRS_LOCK_PUSH) != 0);
    cs.usb_in = s->usb_in;
    cs.usb_fixed = s->usb_level;
    cs.usb48 = (uint8_t)CRS_USB48(s);              /* (a revert, a reset, a restore: cu_usb_apply tells usb.c) */
    cs.names_std = (uint8_t)CRS_NAMES(s);          /* Options > Chord Names (0.20): the names only */
    cs.sixth_up = (uint8_t)CRS_SIXTH(s);           /* Options > 6th with 7th (0.20): the engine has it (cr_settings_apply) */
    cr_chord_names_set(cs.names_std);
    fx_usb_fixed = cs.usb_fixed;                   /* (USB Record: crs_usb_apply at boot, before usb_start) */
    if (cs.out_clean != CRS_OUT_CLEAN(s)) {        /* (a revert, a reset, a restore while it plays: faded over) */
        cs.out_clean = (uint8_t)CRS_OUT_CLEAN(s);
        fx_output_set(cs.out_clean, 0);
    }
    cs.split = s->split_pc;
    cs.view = (uint8_t)CRS_VIEW(s);
    cs.view_lock = (uint8_t)CRS_VIEW_LOCK(s);
    cs.dualcore = crs_dc_bar ? 0u : (uint8_t)CRS_DUALCORE(s);   /* (what this power-on did: cr_c1_off, cr_settings_boot;
                                                    * Off after a bar, crs_dc_bar) */
    cr_motion = s->motion;
    cs.leds = (uint8_t)CRS_LEDS(s);                /* (bit 1: the move to FM-1, the record's) */
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
    if (!crs_sound(0, s->chord_sound))             /* the chord part's sound (the sends are the part's: they stay) */
        s->chord_sound = CRS_SOUND_DEFAULT;
    cs.fx_on = s->fx_on;
    cu_fx_apply();
    if (crs_sound(1, s->bass_sound))               /* what BASS tap brings (Bass on: the session's, css_apply) */
        trk[CR_PART_BASS].engine = trk[CR_PART_BASS].eng_req;
    else
        s->bass_sound = CRS_SOUND_DEFAULT;
    if (css_have) {                                /* power-on: where the unit was left (the session) */
        css_have = 0;
        css_apply(&css_buf);
    }
    cu_pos_sync();
    cu_route();
    cu_fsc_enter();                                /* (Full K Shorts: F#5 off, G5 as Key plays makes it) */
    cu_post_kbd();                                 /* (the split key's note: the UI's, with its octave) */
    cu_set_tempo(s->bpm);
    crs_loaded = 1;
    crs_capture(&crs_seen);                        /* the state now: nothing to save until an Option changes */
    crs_hold_seen = settings_hold;
    crs_change_ms = fm1_ms;
    /* .. but Dual Core Off after a bar with the record still On: the poll writes Off (crs_write: crs_seen's Off over
     * flash's On), once it is quiet */
    crs_pending = (uint8_t)(crs_dc_bar && CRS_DUALCORE(s));
}

/* cr_ui.c: core 1 given up this session (cr_cpu1.h cr_c1_give_up): Dual Core Off, saved by the poll (it sees the
 * Option change), and Off on a revert too */
static void cr_settings_dc_off(void)
{
    crs_dc_bar = 1;
    cs.dualcore = 0;
}
/* cr_ui.c: the player set Options > Dual Core: the record says what they chose from here on */
static void cr_settings_dc_user(void) { crs_dc_bar = 0; }

/* the captured state (crs_seen) -> flash; 0: written (or no flash, or nothing to write). full: the whole block (the
 * global save); else flash's block with the automatic fields of the captured state: the manual ones stay as last saved */
static int crs_write(int full)
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
    if (full)
        p.cr = crs_seen;
    else
        cr_settings_auto(&p.cr, &crs_seen);
    cr_settings_seal(&p.cr);
    if (!memcmp(&p, &crs_rec, sizeof p))
        return 0;                                  /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) != 0)
        return -1;
    crs_rec = p;
    crs_obj_crc = st_crc32(&p, sizeof p);
    crs_wr_ms = fm1_ms;
    crs_saves++;
#else
    if (full)
        crs_rec.cr = crs_seen;
    else
        cr_settings_auto(&crs_rec.cr, &crs_seen);
    cr_settings_seal(&crs_rec.cr);
#endif
    return 0;
}

/* something sounds or is about to: a voice of the parts (release tails too), a chord the engine holds or latches,
 * a scheduled note (arp, strum), or the master output (the FX tails) above CRS_QUIET_PEAK over the scope's last
 * 512 samples (~23 ms). No flash erase then: it would cut the audio (see the top). There is no way round it: a
 * reverb at DECAY Inf (cr_fx.c) keeps the output above the floor and so keeps the automatic settings from being
 * written until it is turned down or the effects are switched off (the first 0.18 build wrote after 30 s anyway: a
 * 46 ms hole in the tail, heard on the unit as a drop-out; 73 DEBUG's flash_holes counts erases into a sounding
 * output: 0 is the rule for the automatic writes; the global save is the player's own press and writes at once).
 * What is lost if the power is cut meanwhile: the Options changed since the last quiet moment */
static int crs_sounding(void)
{
    uint32_t i;
    if (cr_parts_busy() || cr_snap.voices || cr_snap.pending)
        return 1;
    if (cr_snap.perf_step)                         /* 3y48: an arp / perform pattern stepping, between its steps too
                                                    * (belt and braces: its held chord is a used voice, cr_snap.voices;
                                                    * not cr_snap.latching, which is LOCK's mode, keys held or not) */
        return 1;
    for (i = 0; i < SCOPE_N; i++)
        if (scope_buf[i] > CRS_QUIET_PEAK || scope_buf[i] < -CRS_QUIET_PEAK)
            return 1;
    return 0;
}


/* THE QUIET WRITES (0.22; the owner, 2026-10-13, on the build with the favourites' filter: "I am now getting lots of
 * audio glitches and dropouts now"; the unit: 9 erases, irq_gap_us 17 851 at st_erase's return, stale_halves 46).
 * Until then every automatic write erased a sector (storage.c st_save: one erase a save, the interrupts masked for
 * 17.8 ms), a second after the output fell silent: where the next note is. Now (storage.c has the rule and the
 * writer's side): the spare copy of the session and the favourites is erased AHEAD (st_prep): at
 * power-on before the audio starts (crs_ahead_boot) and after ST_AHEAD_IDLE_MS without a sound and without input
 * (crs_ahead_poll), so a save finds it blank and only programs (NOT the settings, the owner's decision of 2026-10-14:
 * they hold the panel calibration, whose previous copy stays the fallback; their save erases, under the quiet rule); a change that comes before the next erase ahead
 * (the spare is the copy written a moment ago) waits for ST_ERASE_IDLE_MS of the same instead of one silent second.
 * Input is cu.last_input (cr_ui.c: a key, a button, a knob, a MIDI channel message). What is lost if the power is
 * cut meanwhile: the Options and the marks changed since the last write (SAVE held writes them at once) */
static void crs_when(st_when_t *w, uint32_t change_ms, uint32_t obj)
{
    w->now = fm1_ms;
    w->change = change_ms;
    w->loud = crs_loud_ms;
    w->input = cu.last_input;
    w->wrote = crs_wr_ms;
    w->busy = (uint8_t)(CR_SETTINGS_BUSY() != 0);
#if FELUCCA_FLASH
    /* (3y52: an object not erased ahead, the settings, keeps the rule it had before the quiet writes: its write
     * erases once nothing changed for 1.5 s and nothing sounded for 1 s; no extra wait for the input's quiet) */
    w->ready = (uint8_t)(st_ahead_ix(obj) < 0 || st_ready(obj));
#else
    w->ready = 1;
    (void)obj;
#endif
}
#if FELUCCA_FLASH
#ifdef CR_FAVS
static const uint8_t CRQ_OBJ[] = {OBJ_SESSION, OBJ_FAVS};   /* (not the settings: storage.c st_ahead_ix) */
#else
static const uint8_t CRQ_OBJ[] = {OBJ_SESSION};
#endif
#endif
/* power-on, after the objects were read and before the audio starts (cr_shim.c persist_boot): each spare copy that
 * is not blank is erased now, when nothing can be heard (a unit coming from an older firmware, or switched off before
 * the idle erase: at most two erases, about 18 ms each, and for each object the read of one sector) */
static void crs_ahead_boot(void)
{
#if FELUCCA_FLASH
    uint32_t i;
    if (!flash_ok)
        return;
    st_ahead_ok[3] = 1;                            /* (the emulator's flash model: no audio to stall yet) */
    for (i = 0; i < sizeof CRQ_OBJ; i++) {
        int rc = st_prep(CRQ_OBJ[i]);
        if (rc > 0) {
            crq.ahead++;
            crq.ahead_boot++;
        } else if (rc < 0 && !ST_BLOCKED()) {
            crq.off = 1;
        }
    }
#if UP_POOL
    {   /* the sound pool's next target (storage.c sp_prep): a SAVE then programs only (0.22) */
        int rc = sp_prep();
        if (rc > 0) {
            crq.ahead++;
            crq.ahead_boot++;
        }
    }
#endif
    st_ahead_ok[3] = 0;
#endif
}
/* every UI frame, after the writes: ONE erase ahead when the unit has been idle long enough (st_ahead_due) and the
 * object has nothing to write (a pending write goes first: st_write_due). The input and the sound are read here, in
 * the same frame, so an input before the erase starts cancels it; once started it runs to its end */
static void crs_ahead_poll(void)
{
#if FELUCCA_FLASH
    uint32_t i;
    if (crq.off || !flash_ok)
        return;
    for (i = 0; i < sizeof CRQ_OBJ; i++) {
        st_when_t w;
        int rc;
        if (st_ready(CRQ_OBJ[i]))
            continue;
#ifdef CR_FAVS
        if (CRQ_OBJ[i] == OBJ_FAVS && crf_dirty)
            continue;
#endif
        crs_when(&w, fm1_ms, CRQ_OBJ[i]);
        if (!st_ahead_due(&w))
            return;
        st_auto = 1;
        rc = st_prep(CRQ_OBJ[i]);
        st_auto = 0;
        crs_wr_ms = fm1_ms;
        if (rc > 0)
            crq.ahead++;
        else if (rc < 0)
            crq.off = 1;                           /* a flash error: no erase ahead until the next power-on */
        return;
    }
#if UP_POOL
    if (!sp_ready()) {                             /* the sound pool's next target, after a SAVE used it (0.22) */
        st_when_t w;
        int rc;
        crs_when(&w, fm1_ms, OBJ_SESSION);
        w.ready = 0;
        if (!st_ahead_due(&w))
            return;
        st_auto = 1;
        rc = sp_prep();
        st_auto = 0;
        crs_wr_ms = fm1_ms;
        if (rc > 0)
            crq.ahead++;
        else if (rc < 0)
            crq.off = 1;
    }
#endif
#endif
}

/* the favourites (cr_favs.c): a mark that changed is written as an Option is: once no mark changed for CRS_QUIET_MS
 * and nothing sounded for CRS_IDLE_MS (and, when the write has to erase, no input came for ST_ERASE_IDLE_MS:
 * st_write_due), never while a loop plays or after a restore; a flash error: again in a second.
 * (Marked while a chord rings, the write waits for the quiet) */
/* 3y48: the writer about to erase (storage_hw.c st_why): an automatic one also notes whether anything sounded in the
 * second before (erases_auto_sounding, 78 PEAK: the guard above makes it 0) */
static void crs_why(uint32_t why)
{
    st_why = why;
    st_why_loud = (uint32_t)(crs_sounding() || (uint32_t)(fm1_ms - crs_loud_ms) < CRS_IDLE_MS);
}
static uint32_t crf_retry_ms;
static uint8_t crf_failed;
static void crf_poll(void)
{
#ifdef CR_FAVS
    st_when_t w;
    if (!crf_dirty || (crf_failed && (uint32_t)(fm1_ms - crf_retry_ms) < 1000u))
        return;
    crs_when(&w, crf_change_ms, OBJ_FAVS);
    if (!st_write_due(&w))
        return;
    crs_why(ST_WHY_AUTO_FAV);
    st_auto = 1;
    crf_failed = crf_flush() != 0;
    st_auto = 0;
    st_why = ST_WHY_OTHER;
    crq.auto_saves += (uint32_t)!crf_failed;
    crf_retry_ms = crs_wr_ms = fm1_ms;
#endif
}

/* every UI frame (main.c settings_poll) */
static void cr_settings_poll(void)
{
    cr_settings_t now;
    st_when_t w;
    if (!crs_loaded)
        return;
    if (crs_sounding())
        crs_loud_ms = fm1_ms;
    crf_poll();
    crs_capture(&now);
    if (!cr_settings_equal(&now, &crs_seen) || settings_hold != crs_hold_seen) {   /* (HOLD: crs_write's
                                                    * settings_export puts it in the record) */
        cr_settings_t was = crs_seen;
        cr_settings_auto(&was, &now);              /* (the last state with the Options of now: differs = an Option
                                                    * changed; a manual setting alone starts no write) */
        if (!cr_settings_equal(&was, &crs_seen) || settings_hold != crs_hold_seen) {
            crs_change_ms = fm1_ms;
            crs_pending = 1;
        }
        crs_seen = now;
        crs_hold_seen = settings_hold;
        return;
    }
    crs_ahead_poll();
    if (!crs_pending || (crs_pending == 2u && (uint32_t)(fm1_ms - crs_retry_ms) < 1000u))
        return;
    crs_when(&w, crs_change_ms, OBJ_SETTINGS);
    if (!st_write_due(&w))
        return;
    {
        uint32_t n = crs_saves;
        int rc;
        crs_why(ST_WHY_AUTO_OPT);
        st_auto = 1;
        rc = crs_write(0);
        st_auto = 0;
        st_why = ST_WHY_OTHER;
        crq.auto_saves += crs_saves - n;           /* (a block that came out unchanged: no write) */
        if (rc == 0) {
            crs_pending = 0;
        } else {
            crs_pending = 2;                       /* flash error: retry in a second */
            crs_retry_ms = fm1_ms;
        }
    }
}

/* the automatic part now (Felucca's settings_save: the power-on calibration; the update gesture; 3y48: not LIST):
 * still not while a loop plays. The manual settings and the session are the global save's (cr_settings_commit) */
static void cr_settings_save(void)
{
    if (!crs_loaded) {                             /* (power-on calibration: before the UI) */
        if (!crs_booted)
            cr_settings_boot();
        crs_seen = crs_rec.cr;
    } else {
        crs_capture(&crs_seen);
    }
    crs_hold_seen = settings_hold;
    crs_pending = 1;
    crs_change_ms = fm1_ms - CRS_QUIET_MS;
    st_why = ST_WHY_SAVE;
    if (!CR_SETTINGS_BUSY() && crs_write(0) == 0)
        crs_pending = 0;
#ifdef CR_FAVS
    if (!CR_SETTINGS_BUSY())
        (void)crf_flush();                         /* a mark not yet written goes in too (the update is a power cycle) */
#endif
    st_why = ST_WHY_OTHER;
}

/* THE GLOBAL SAVE (the owner, 2026-10-11: "Hold Save button to manually do a global save, instead of trying to
 * auto-save settings at runtime"): the whole block as it is now and the session, at once, whatever sounds (the
 * player's own press: cr_ui.c cu_gsave keeps it off a playing loop). An object that did not change is not written
 * (crs_write's compare, css_write's hash): no erase for it. 0: done, -1: a flash error (what was written stays) */
static int cr_settings_commit(void)
{
    int rc = 0;
    if (!crs_loaded || cr_restore_lock)            /* (a restore: flash is newer than the RAM until the restart) */
        return 0;
    crs_capture(&crs_seen);
    crs_hold_seen = settings_hold;
    st_why = ST_WHY_GSAVE;
    if (crs_write(1) == 0)
        crs_pending = 0;
    else
        rc = -1;
    if (css_write() != 0)
        rc = -1;
#ifdef CR_FAVS
    if (crf_flush() != 0)                          /* (a mark the poll has not written yet) */
        rc = -1;
#endif
    st_why = ST_WHY_OTHER;
    return rc;
}

/* both parts and the effects as cr_ui_init leaves them (the default sounds loaded: an edit not saved is gone; the
 * parts' own parameters, Level and Pan among them; the effects' parameters and the effect picked; the bass off):
 * what cr_settings_load then puts the block and a session over. Reset Settings and a revert */
static void crs_reinit(void)
{
    uint32_t i, k;
    for (k = 0; k < 2u; k++)
        for (i = 0; i < P_E0; i++)
            trk[k ? CR_PART_BASS : CR_PART_CHORD].p[i] = TP[i].def;
    css_fxg_defaults();
    cs.fx_sel = 0;
    cs.fx_page = CU_FXPG_SENDS;                    /* (the FX layer's first page: cr_ui_init's) */
    trk[CR_PART_CHORD].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);   /* (cr_ui_init's) */
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);
    cu_pool_load(0, ENGI_FM6, cu_pool_find(ENGI_FM6, "TINE EP"));
    cu_pool_load(1, ENGI_VA, cu_pool_find(ENGI_VA, "DEEP SUB"));
    cs.bass_sound = 0;                             /* ALGORITHM at OFF: the bass off */
    cs.bass_on = 0;
    cs.loop_rate = CRL_RATE_1;                     /* the loop's Rate: x1 */
}

/* Options > Reset Settings (the owner, 2026-10-12): every option back to what a new unit has, now and in flash. The
 * record: cr_settings_factory (cr_settings_defaults: the same defaults as a unit with no record and as SAFE MODE);
 * Felucca's one field the Options set outside it: HOLD. Then the unit as a power-on leaves it with those settings:
 * the default sounds on both parts (TINE EP, DEEP SUB; an edit not saved is gone), their levels and pans, the
 * effects' parameters, the bass off (crs_reinit), and cr_settings_load again: the record -> the engine, the routing,
 * the UI's mirror. Both sides, the manual and the automatic, and written at once: it is a deliberate act, no SAVE held
 * is needed after it (cr_settings_commit; a loop playing: cr_ui.c cu_reset stopped it first).
 * NOT touched: the panel calibration (persist_t.panel: settings_export writes the table in RAM, as every save), the
 * saved sounds and patches (upreset / va / fm6 / cz / quad stores), the loops and the slot selected, Felucca's
 * favourites. Dual Core: On is saved (the default since 0.22), but Off in a power-on that kept core 1 off after a
 * crash beside it or gave it up (crs_dc_bar: the fallback wins over the default until the player sets the Option);
 * the core this power-on did or did not start stays as it is until the next one (cr_c1_off) */
static void cr_settings_reset(void)
{
    if (!crs_loaded)
        return;
    cr_settings_factory(&crs_rec.cr);
    settings_hold = HOLD_DEF;
    settings.lowcut = 0;                           /* (Felucca's SPEAKER setting: held off since 0.19, cr_settings_boot) */
    fx_lowcut = 0;
    crs_reinit();                                  /* the session too (0.18): cu_reset: LOCK, the voicing, the octave.
                                                    * The session then captured is a new unit's */
    cr_settings_load();
    cu_loop_conf();                                /* the looper's options and the metronome -> the ISR */
    cu_usb_apply();                                /* USB Record on again, if it was off; USB Rate 44.1 kHz (usb.c) */
    cr_settings_commit();
}

/* GLO + SAVE (the owner, 2026-10-11: "reload the global settings and current preset ... change a bunch of settings
 * then quickly revert back"): what the last global save wrote, applied again as a power-on applies it: flash's block
 * (crs_rec mirrors it; its automatic fields are the Options as they are now, which are not the revert's) and flash's
 * session (read again: both parts' sounds as saved there, the voicing, the octave, Bass on, LOCK and its latch, the
 * FX layer). No session in flash (a unit that never saved): the parts on the block's sounds, the rest at the
 * defaults, as that unit powers on. cr_ui.c cu_revert quiets the unit first and reloads the loop slot after */
static void cr_settings_revert(void)
{
    cr_settings_t keep, now;
    if (!crs_loaded)
        return;
    crs_capture(&now);
    keep = crs_rec.cr;
    cr_settings_auto(&crs_rec.cr, &now);
    cr_settings_seal(&crs_rec.cr);
    css_read();
    crs_reinit();
    fm1_irq_off();
    cr_voicing_step(&cr, -(int)cr.voicing);        /* (no session: 0; css_apply sets the session's) */
    fm1_irq_on();
    cr_settings_load();
    crs_rec.cr = keep;                             /* (still what flash holds) */
    crs_pending = 1;                               /* an Option not written yet: still to write (none: no erase) */
    crs_change_ms = fm1_ms;
}
#endif /* CR_VERSION */
