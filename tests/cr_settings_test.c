/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of firmware/src/cr_settings.c (the record, its import, apply / capture with the engine) and of the
 * record through Felucca's flash layer (storage.c on a simulated NOR: A/B copies, a torn write):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o build/host/cr_settings_test tests/cr_settings_test.c \
 *      firmware/src/cr_settings.c firmware/src/cr_engine.c && build/host/cr_settings_test
 * (tests/run_cr_tests.sh runs it.) */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/cr_engine.h"
#include "../firmware/src/cr_settings.h"

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

/* ------------------------------------------------- a NOR for storage.c --- */
static uint8_t nor[0x100000];
static int fail_after = -1;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#define __attribute__(x)
#define FELUCCA_CZ 1                               /* ChoralRoot's object list: the session's object (OBJ_SESSION) */
#define FELUCCA_QUAD 1
#define ST_AHEAD 1                                 /* (choralroot.c's: the erase ahead) */
#include "../firmware/src/storage.c"
#undef __attribute__

static void nop_on(void *u, cr_stream_t s, uint8_t n, uint8_t v) { (void)u; (void)s; (void)n; (void)v; }
static void nop_off(void *u, cr_stream_t s, uint8_t n) { (void)u; (void)s; (void)n; }
static void nop_all(void *u, cr_stream_t s) { (void)u; (void)s; }
static const cr_out_t OUT = {nop_on, nop_off, nop_all, 0, 0};

static cr_t C;

static void t_defaults(void)
{
    cr_settings_t d, e;
    cr_settings_out_t o;
    int m, p;
    cr_settings_defaults(&d);
    ok(sizeof(cr_settings_t) == CRS_SIZE, "the record is CRS_SIZE bytes");
    ok(d.magic == CRS_MAGIC && d.version == CRS_VERSION && d.size == CRS_SIZE, "defaults: sealed header");
    ok(d.clock_mode == CRS_CLOCK_OUT, "defaults: MIDI clock OUT");
    ok(d.midi_en[CR_STREAM_MAIN] && d.midi_en[CR_STREAM_BASS] && !d.midi_en[CR_STREAM_RAW], "defaults: RAW stream off");
    ok(d.midi_ch[0] == 0 && d.midi_ch[1] == 1 && d.midi_ch[2] == 2, "defaults: channels 1 / 2 / 3");
    ok(!d.bass_on && d.bass_mode == CR_BASS_CHORDS_ONLY, "defaults: bass off, Chords Only");
    ok(d.palette == CRS_PALETTE_MOD, "defaults: the MOD palette");
    ok(d.pick_roots == 1, "defaults: the engine picker's roots choose engines");
    ok(d.usb_in == 1 && d.usb_level == CRS_USB_MASTER && !d.loop_rec,
       "defaults: USB Record on, USB Level Master (Felucca's)");
    ok(d.bpm == 120 && d.vel == 100 && d.playstyle == CR_PS_SIMPLE && !d.key_on && d.single == CR_SINGLE_FULL,
       "defaults: 120 BPM, velocity 100, Simple, Key Mode off, Full Octave");
    ok(d.chord_sound == CRS_SOUND_DEFAULT && d.fx_on && d.view == CRS_DC_ON && d.motion == 0, "defaults: UI fields (the view 0, Dual Core On)");
    /* the defaults are the engine's power-on state */
    cr_init(&C, &OUT);
    for (m = 0; m < CR_PM_COUNT; m++)
        for (p = 0; p < CR_P_COUNT; p++)
            if (cr_get_param(&C, (cr_pmode_t)m, (cr_param_t)p) != d.par[m][p]) {
                ok(0, "defaults: perform parameters = the engine's");
                m = CR_PM_COUNT;
                break;
            }
    ok(1, "defaults: perform parameters checked");
    e = d;
    o.clock_out = 1; o.clock_in = 0; o.part[0] = 0; o.part[1] = 1; o.part[2] = CRS_NONE;
    o.midi_en[0] = o.midi_en[1] = 1; o.midi_en[2] = 0; o.ch[0] = 0; o.ch[1] = 1; o.ch[2] = 2;
    cr_settings_capture(&e, &C, &o);
    ok(cr_settings_equal(&e, &d), "capture of a fresh engine = the defaults");
}

/* a fresh unit's Play Style (the owner, 2026-10-12: his unit came up Advanced): every way cr_settings_boot can come
 * without a ChoralRoot record of its own gives the defaults, Simple, in the record and in the engine it is applied
 * to: no record at all (st_load fails: a zeroed persist_t), erased flash read as a block (0xFF), a Felucca / Melodee
 * PER1..PER4 record (settings_import zeroes the block), another firmware's bytes where the block would be (no CRS1
 * magic), a ChoralRoot block cut short or with a bad checksum. Only a valid ChoralRoot record that says Advanced
 * gives Advanced */
static void t_fresh_playstyle(void)
{
    cr_settings_t s, d, adv;
    uint8_t blk[CRS_SIZE];
    uint32_t i;
    int rc;
    cr_settings_defaults(&d);
    cr_settings_defaults(&adv);
    adv.playstyle = CR_PS_ADVANCED;
    cr_settings_seal(&adv);
#define FRESH(what) do { \
        cr_init(&C, &OUT); \
        cr_set_playstyle(&C, CR_PS_FREE); \
        cr_settings_apply(&s, &C, 0); \
        ok(rc == 0 && s.playstyle == CR_PS_SIMPLE && s.extadd == CR_EXTADD_ADD_NOTE && cr_settings_equal(&s, &d) && \
           C.playstyle == CR_PS_SIMPLE, what); \
    } while (0)
    memset(&s, 0x5A, sizeof s);
    rc = cr_settings_import(&s, 0, 0);
    FRESH("fresh: no block: the defaults, Play Style Simple (the engine too)");
    memset(blk, 0, sizeof blk);                    /* (cr_settings_boot: no record, or PER1..PER4: the block zeroed) */
    memset(&s, 0x5A, sizeof s);
    rc = cr_settings_import(&s, blk, sizeof blk);
    FRESH("fresh: a zeroed block (no record / a Felucca PER4 record): Simple");
    memcpy(&s, blk, sizeof s);                     /* (.. imported in place, as cr_settings_boot does) */
    rc = cr_settings_import(&s, &s, sizeof s);
    FRESH("fresh: a zeroed block imported in place: Simple");
    memset(blk, 0xFF, sizeof blk);
    rc = cr_settings_import(&s, blk, sizeof blk);
    FRESH("fresh: erased flash (0xFF): Simple");
    for (i = 0; i < sizeof blk; i++)               /* another firmware's data there: every byte 1 (Advanced's value) */
        blk[i] = 1;
    rc = cr_settings_import(&s, blk, sizeof blk);
    FRESH("fresh: another firmware's bytes (no CRS1): Simple");
    memcpy(blk, &adv, sizeof blk);                 /* a real Advanced record, damaged: not trusted */
    blk[CRS_SIZE - 1u] ^= 0x40u;
    rc = cr_settings_import(&s, blk, sizeof blk);
    FRESH("fresh: an Advanced record with a bad checksum: Simple");
    memcpy(blk, &adv, sizeof blk);
    rc = cr_settings_import(&s, blk, CRS_SIZE - 8u);
    FRESH("fresh: an Advanced record cut short: Simple");
#undef FRESH
    rc = cr_settings_import(&s, &adv, sizeof adv);
    ok(rc == 1 && s.playstyle == CR_PS_ADVANCED, "a valid record that says Advanced: Advanced (the stored setting)");
}

/* Options > Reset Settings (cr_settings_factory): every field of the record back to cr_settings_defaults' value,
 * whatever it held, but the loop slot (the looper's place, not an option: the loop in RAM is that slot's); what is
 * applied to an engine that had everything changed is the power-on engine. The record is all the reset writes: the
 * panel calibration, the sounds and the loops are other objects (the emulator's test: test_persist.sh, the flash
 * outside the settings object byte for byte, the panel table inside it) */
static void t_factory(void)
{
    cr_settings_t d, s, e;
    cr_settings_out_t o, od;
    uint32_t i, diff = 0, k;
    int m, p;
    cr_t F;
    cr_settings_defaults(&d);
    for (k = 0; k < 3u; k++) {                     /* every byte 0x00, 0xFF, then a valid record with every field set */
        memset(&s, k == 0u ? 0x00 : 0xFF, sizeof s);
        if (k == 2u) {
            cr_settings_defaults(&s);
            s.playstyle = CR_PS_ADVANCED; s.extadd = 1; s.secret = CR_SECRET_ALL; s.key_on = 7; s.tonic = 9; s.scale = 1;
            s.transpose = -5; s.single = CR_SINGLE_SPLIT_BASS; s.split_pc = 2; s.vel = 64; s.bass_mode = CR_BASS_SOLO;
            s.bass_voicing = 2; s.perform_on = 1; s.perform_mode = CR_PM_COUNT - 1; s.perf_sel = 5; s.sticky = 1; s.bpm = 137;
            for (m = 0; m < CRS_NPM; m++)
                for (p = 0; p < CRS_NPAR; p++)
                    s.par[m][p] = (int16_t)(d.par[m][p] + 1);
            s.loop_sync = 3; s.loop_quant = 4; s.loop_count_in = 0; s.loop_level = 40; s.metro_on = 1; s.metro_sig = 2;
            s.metro_vol = 10; s.loop_rec = 0xE3; s.pick_roots = 0; s.usb_in = 0; s.usb_level = CRS_USB_FIXED;
            for (i = 0; i < 3u; i++) { s.midi_en[i] = 1; s.midi_ch[i] = 15; }
            s.clock_mode = CRS_CLOCK_IN; s.raw_sound = 1; s.view = 0xC2; s.motion = 2; s.leds = 1; s.fx_on = 0;
            s.palette = 0; s.chord_sound = 0x0C1A; s.bass_sound = 0x0203;
            for (i = 0; i < (uint32_t)CRS_NENG; i++) { crs_pool_set(&s, 0, i, 20); crs_pool_set(&s, 1, i, 30); }
            cr_settings_seal(&s);
        }
        s.loop_slot = 6;
        cr_settings_factory(&s);
        e = d;
        e.loop_slot = 6;
        cr_settings_seal(&e);
        for (i = 0, diff = 0; i < CRS_SIZE; i++)
            diff += ((const uint8_t *)&s)[i] != ((const uint8_t *)&e)[i];
        ok(diff == 0 && s.check == e.check && s.playstyle == CR_PS_SIMPLE && s.loop_slot == 6,
           k == 0u ? "reset: a zeroed record -> the defaults, the loop slot kept"
           : k == 1u ? "reset: an 0xFF record -> the defaults, the loop slot kept"
           : "reset: every field changed -> every byte the default's, the loop slot kept, sealed");
        ok(cr_settings_import(&e, &s, sizeof s) == 1 && cr_settings_equal(&e, &s), "reset: the record imports as current");
    }
    s.loop_slot = 200;                             /* (a slot out of range is not kept) */
    cr_settings_factory(&s);
    ok(cr_settings_equal(&s, &d), "reset: an invalid loop slot -> the default's");
    /* applied to an engine with everything changed: the power-on engine (capture = the defaults) */
    cr_init(&C, &OUT);
    cr_set_playstyle(&C, CR_PS_FREE); cr_set_ext_addition(&C, CR_EXTADD_PLAY_CHORD); cr_set_secret(&C, CR_SECRET_ALL);
    cr_set_key(&C, 1, 9, CR_SCALE_MINOR); cr_set_key_plays(&C, 1); cr_set_transpose(&C, 7);
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 3); cr_set_bass_mode(&C, CR_BASS_SOLO); cr_bass_voicing_step(&C, 2);
    cr_set_bass(&C, 1); cr_set_perform_mode(&C, CR_PM_ARP); cr_set_perform(&C, 1); cr_set_sticky(&C, 1); cr_set_tempo(&C, 77);
    cr_set_param(&C, CR_PM_ARP, CR_P_RATE, 3);
    cr_settings_defaults(&s);
    cr_settings_factory(&s);
    cr_settings_apply(&s, &C, &o);
    cr_init(&F, &OUT);
    cr_settings_apply(&d, &F, &od);
    e = d;
    cr_settings_capture(&e, &C, &o);
    ok(cr_settings_equal(&e, &d), "reset: applied to a changed engine, its capture is the defaults");
    ok(C.playstyle == F.playstyle && C.extadd == F.extadd && C.secret == F.secret && C.key_on == F.key_on &&
       C.key_notes == F.key_notes && C.transpose == F.transpose && C.single == F.single && C.split_pc == F.split_pc &&
       C.bass_mode == F.bass_mode && C.bass_voicing == F.bass_voicing && C.bass_on == F.bass_on &&
       C.perform_on == F.perform_on && C.perform_mode == F.perform_mode && C.sticky == F.sticky && C.bpm == F.bpm &&
       C.playstyle == CR_PS_SIMPLE && !memcmp(&o, &od, sizeof o),
       "reset: .. and it is the engine and the routing of a unit with no record (Play Style Simple)");
}

static void t_roundtrip(void)
{
    cr_settings_t s, back, imp;
    cr_settings_out_t o, o2;
    cr_settings_defaults(&s);
    s.playstyle = CR_PS_FREE; s.extadd = 1; s.secret = CR_SECRET_ALL; s.key_on = 1; s.tonic = 9; s.scale = 1;
    s.transpose = -5; s.single = CR_SINGLE_SPLIT; s.split_pc = 7; s.bass_mode = CR_BASS_UNISON; s.bass_voicing = 3;
    s.perform_on = 1; s.perform_mode = CR_PM_ARP; s.sticky = 1; s.bpm = 97;
    s.par[CR_PM_ARP][CR_P_DIV] = CR_DIV_1_16T; s.par[CR_PM_HARP][CR_P_RANGE] = 4; s.par[CR_PM_PATTERN][CR_P_ROTATE] = -3;
    s.midi_en[2] = 1; s.midi_ch[2] = 9; s.clock_mode = CRS_CLOCK_OFF; s.raw_sound = 1;
    s.view = 2; s.motion = 1; s.leds = CRS_LEDS_STOCK | CRS_KEYS_MOVED; s.palette = 3; s.chord_sound = 17; s.bass_sound = 4; s.vel = 64;
    s.loop_sync = 3; s.loop_quant = 2; s.loop_count_in = 1; s.loop_level = 80;
    cr_settings_seal(&s);
    cr_init(&C, &OUT);
    cr_tick(&C, 1);
    cr_settings_apply(&s, &C, &o);
    ok(C.playstyle == CR_PS_FREE && C.key_on && C.tonic == 9 && C.transpose == -5 && C.single == CR_SINGLE_SPLIT &&
       C.split_pc == 7 && C.bass_voicing == 3 && C.perform_on && C.perform_mode == CR_PM_ARP && C.sticky &&
       C.bpm == 97 && C.stream_on[CR_STREAM_RAW], "apply: the engine's setters");
    ok(cr_get_param(&C, CR_PM_ARP, CR_P_DIV) == CR_DIV_1_16T && cr_get_param(&C, CR_PM_PATTERN, CR_P_ROTATE) == -3,
       "apply: per-mode parameters");
    ok(!o.clock_out && o.midi_en[2] && o.ch[2] == 9 && o.part[2] == 0 && o.part[0] == 0 && o.part[1] == 1,
       "apply: the routing");
    back = s;
    back.bpm = 1; back.tonic = 0;                  /* engine fields: overwritten by the capture */
    cr_settings_capture(&back, &C, &o);
    ok(cr_settings_equal(&back, &s), "capture(apply(s)) == s");
    ok(cr_settings_import(&imp, &s, sizeof s) == 1 && cr_settings_equal(&imp, &s), "import(seal(s)) == s");
    o2 = o; o2.clock_out = 1;
    cr_settings_capture(&back, &C, &o2);
    ok(back.clock_mode == CRS_CLOCK_OUT, "capture: clock out");
    o2.clock_out = 0; o2.clock_in = 1;
    cr_settings_capture(&back, &C, &o2);
    ok(back.clock_mode == CRS_CLOCK_IN, "capture: clock in");
    cr_settings_apply(&back, &C, &o2);
    ok(o2.clock_in && !o2.clock_out, "apply: clock in");
}

static void t_corruption(void)
{
    cr_settings_t s, r, d;
    uint8_t raw[CRS_SIZE];
    int i, bad = 0;
    cr_settings_defaults(&d);
    cr_settings_defaults(&s);
    s.bpm = 150; s.tonic = 4;
    cr_settings_seal(&s);
    memcpy(raw, &s, sizeof raw);
    raw[40] ^= 0x10;                               /* a flipped bit */
    ok(cr_settings_import(&r, raw, sizeof raw) == 0 && cr_settings_equal(&r, &d), "a flipped bit: defaults");
    memset(raw, 0xFF, sizeof raw);
    ok(cr_settings_import(&r, raw, sizeof raw) == 0 && cr_settings_equal(&r, &d), "erased flash: defaults");
    memset(raw, 0, sizeof raw);
    ok(cr_settings_import(&r, raw, sizeof raw) == 0 && cr_settings_equal(&r, &d), "a zeroed block (PER1..4): defaults");
    ok(cr_settings_import(&r, &s, 7) == 0 && cr_settings_import(&r, NULL, 0) == 0, "short / absent: defaults");
    ok(cr_settings_import(&r, &s, CRS_SIZE - 1) == 0, "truncated: defaults");
    for (i = 0; i < 2000; i++) {                   /* random garbage never imports and never hangs */
        int k;
        for (k = 0; k < (int)CRS_SIZE; k++) raw[k] = (uint8_t)((i * 1103515245u + k * 12345u) >> 7);
        if (cr_settings_import(&r, raw, sizeof raw)) bad++;
    }
    ok(!bad, "garbage: never imported");
    /* a sealed record with values out of range: per-field defaults, the rest kept */
    s.bpm = 999; s.tonic = 40; s.par[0][CR_P_RATE] = 0; s.midi_ch[1] = 33; s.vel = 0;
    s.key_on = 1;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1, "out-of-range fields: the record is still read");
    ok(r.bpm == 120 && r.tonic == 0 && r.par[0][CR_P_RATE] == 40 && r.midi_ch[1] == 1 && r.vel == 100 && r.key_on,
       "out-of-range fields take their defaults; the others are kept");
}

static void t_version(void)
{
    cr_settings_t s, r;
    uint8_t big[CRS_SIZE + 64];
    uint32_t h, i;
    /* an older record: version 0 is invalid; a v1 writer's size is the reference. Simulate an older, shorter
     * record (written before the loop fields existed: size up to par[]) */
    cr_settings_defaults(&s);
    s.bpm = 88; s.loop_level = 7;
    s.version = 1;
    s.size = (uint16_t)((const uint8_t *)&s.loop_sync - (const uint8_t *)&s);
    for (h = 2166136261u, i = 12; i < s.size; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, s.size) == 2, "an older (shorter) record: migrated");
    ok(r.bpm == 88 && r.loop_level == 100 && r.size == CRS_SIZE && r.version == CRS_VERSION,
       "older: its fields kept, the newer ones default, resealed current");
    /* a newer firmware's record (version + 1, longer): our prefix is read */
    cr_settings_defaults(&s);
    s.bpm = 133;
    memset(big, 0xAB, sizeof big);
    memcpy(big, &s, sizeof s);
    ((cr_settings_t *)big)->version = CRS_VERSION + 1;
    ((cr_settings_t *)big)->size = sizeof big;
    for (h = 2166136261u, i = 12; i < sizeof big; i++) { h ^= big[i]; h *= 16777619u; }
    ((cr_settings_t *)big)->check = h;
    ok(cr_settings_import(&r, big, sizeof big) == 2 && r.bpm == 133, "a newer (longer) record: our prefix kept");
    /* a version bump with the same size (a field taken from rsv) imports as migrated */
    cr_settings_defaults(&s);
    s.version = CRS_VERSION + 1;
    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, sizeof s) == 2, "version bump, same size: migrated");
    s.version = 0;
    ok(cr_settings_import(&r, &s, sizeof s) == 0, "version 0: invalid");
    /* a version-1 record (written before the metronome fields: zeros in their place) */
    cr_settings_defaults(&s);
    s.bpm = 99;
    s.metro_on = s.metro_sig = s.metro_vol = s.loop_slot = 0;
    s.version = 1;
    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 99 && r.metro_vol == 70 && r.metro_sig == 0 &&
       r.version == CRS_VERSION, "version 1 -> 2: kept, the metronome level takes its default (70)");
    /* a version-2 record (written before pick_roots: a zero in its place) */
    cr_settings_defaults(&s);
    s.bpm = 98;
    s.pick_roots = 0;
    s.version = 2;
    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 98 && r.pick_roots == 1 && r.version == CRS_VERSION,
       "version 2 -> 3: kept, the picker's roots take their default (engines)");
    /* pick_roots off round-trips; out of range takes the default */
    cr_settings_defaults(&s);
    s.pick_roots = 0;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.pick_roots == 0, "pick_roots 0 (the roots play) kept");
    s.pick_roots = 33;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.pick_roots == 33 && CRS_VCURVE(&r) == 1,
       "pick_roots 33: bit 5 is Velocity Curve +1 since 3y112 (it read as out of range before), kept");
    /* 0.19: Options > Output in bit 1 of that byte (CRS_OUT_CLASSIC): clear = Clean, the default and every older
     * record's; set = Classic, kept beside the retired bit 0 */
    cr_settings_defaults(&s);
    ok(CRS_OUT_CLEAN(&s) == 1u, "Output: the default is Clean");
    s.pick_roots = CRS_OUT_CLASSIC;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_OUT_CLEAN(&r) == 0u, "Output Classic (bit 1 of pick_roots) kept");
    s.pick_roots = CRS_OUT_CLASSIC | 1u;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_OUT_CLEAN(&r) == 0u && r.pick_roots == 3u,
       "Output Classic beside the retired picker bit: both kept");
    s.pick_roots = 0;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_OUT_CLEAN(&r) == 1u, "a 0.18 record (the byte 0): Output Clean");
    {
        cr_settings_t m2, o2;
        cr_settings_defaults(&m2);
        cr_settings_defaults(&o2);
        m2.pick_roots = 1;
        o2.pick_roots = CRS_OUT_CLASSIC;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == (CRS_OUT_CLASSIC | 1u), "Output is automatic (an Option): its bit alone is taken from the Options of now");
    }
    /* 0.19, 2026-10-12: Options > USB Rate in bit 2 of the same byte (CRS_USB_48K): clear = 44.1 kHz only, the
     * default and every older record's (0.18's device); set = 44.1 + 48 kHz. No version: nothing to migrate */
    cr_settings_defaults(&s);
    ok(CRS_USB48(&s) == 0u && CRS_VERSION == 15u, "USB Rate: the default is 44.1 kHz only; the record's version is still 15");
    cr_settings_factory(&s);
    ok(CRS_USB48(&s) == 0u, "USB Rate: Reset Settings' record is 44.1 kHz only");
    cr_settings_defaults(&s);
    s.pick_roots = CRS_USB_48K;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_USB48(&r) == 1u && CRS_OUT_CLEAN(&r) == 1u,
       "USB Rate 44.1 + 48 kHz (bit 2 of pick_roots) kept; Output beside it still Clean");
    s.pick_roots = CRS_USB_48K | CRS_OUT_CLASSIC | 1u;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_USB48(&r) == 1u && CRS_OUT_CLEAN(&r) == 0u && r.pick_roots == 7u,
       "USB Rate, Output Classic and the retired picker bit together: the three kept");
    {
        static const uint8_t OLD[] = {0, 1, 2, 3};         /* what 0.17, 0.18 and 0.19 before the option wrote there */
        uint32_t k, good = 0;
        for (k = 0; k < sizeof OLD; k++) {
            cr_settings_defaults(&s);
            s.pick_roots = OLD[k];
            cr_settings_seal(&s);
            good += cr_settings_import(&r, &s, sizeof s) == 1 && CRS_USB48(&r) == 0u && r.pick_roots == OLD[k];
        }
        ok(good == sizeof OLD, "a record of before the option (the byte 0..3): USB Rate 44.1 kHz only, its other bits as they were");
    }
    {
        uint32_t v, good = 0;                              /* every older version's record: 48 kHz off */
        for (v = 1; v < CRS_VERSION; v++) {
            cr_settings_defaults(&s);
            s.pick_roots = v < 3 ? 0 : 1;
            s.version = (uint16_t)v;
            for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
            s.check = h;
            good += cr_settings_import(&r, &s, sizeof s) != 0 && CRS_USB48(&r) == 0u;
        }
        ok(good == CRS_VERSION - 1u, "records of versions 1..14: all read USB Rate 44.1 kHz only");
    }
    {
        cr_settings_t m2, o2;
        cr_settings_defaults(&m2);
        cr_settings_defaults(&o2);
        m2.pick_roots = 1;
        o2.pick_roots = CRS_USB_48K;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == (CRS_USB_48K | 1u), "USB Rate is automatic (an Option): saved by itself, bit 2 alone taken");
        o2.pick_roots = 0;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == 1u, "  and cleared the same way");
    }
    /* 0.20: Options > Chord Names in bit 3 of the same byte (CRS_NAMES_STD): clear = Orchid, the default and every
     * older record's; set = Standard. No version: nothing to migrate (docs/CHORD-NAMES.md) */
    cr_settings_defaults(&s);
    ok(CRS_NAMES(&s) == 0u && CRS_VERSION == 15u, "Chord Names: the default is Orchid; the record's version is still 15");
    cr_settings_factory(&s);
    ok(CRS_NAMES(&s) == 0u, "Chord Names: Reset Settings' record is Orchid");
    cr_settings_defaults(&s);
    s.pick_roots = CRS_NAMES_STD;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_NAMES(&r) == 1u && CRS_OUT_CLEAN(&r) == 1u && CRS_USB48(&r) == 0u,
       "Chord Names Standard (bit 3 of pick_roots) round-trips; Output still Clean, USB Rate still 44.1 kHz");
    s.pick_roots = CRS_NAMES_STD | CRS_USB_48K | CRS_OUT_CLASSIC | 1u;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_NAMES(&r) == 1u && CRS_USB48(&r) == 1u && CRS_OUT_CLEAN(&r) == 0u &&
       r.pick_roots == 15u, "Chord Names, USB Rate, Output Classic and the retired picker bit together: the four kept");
    {
        uint32_t k, good = 0;                              /* what 0.17 .. 0.19 wrote there: 0 .. 7 */
        for (k = 0; k < 8u; k++) {
            cr_settings_defaults(&s);
            s.pick_roots = (uint8_t)k;
            cr_settings_seal(&s);
            good += cr_settings_import(&r, &s, sizeof s) == 1 && CRS_NAMES(&r) == 0u && r.pick_roots == k;
        }
        ok(good == 8u, "a record of before the option (the byte 0..7): Chord Names Orchid, its other bits as they were");
    }
    {
        uint32_t v, good = 0;                              /* every older version's record: Orchid */
        for (v = 1; v < CRS_VERSION; v++) {
            cr_settings_defaults(&s);
            s.pick_roots = v < 3 ? 0 : 1;
            s.version = (uint16_t)v;
            for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
            s.check = h;
            good += cr_settings_import(&r, &s, sizeof s) != 0 && CRS_NAMES(&r) == 0u;
        }
        ok(good == CRS_VERSION - 1u, "records of versions 1..14: all read Chord Names Orchid");
    }
    {
        cr_settings_t m2, o2;
        cr_settings_defaults(&m2);
        cr_settings_defaults(&o2);
        m2.pick_roots = 1;
        o2.pick_roots = CRS_NAMES_STD;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == (CRS_NAMES_STD | 1u), "Chord Names is automatic (an Option): saved by itself, bit 3 alone taken");
        o2.pick_roots = 0;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == 1u, "  and cleared the same way");
    }
    /* 0.20: Options > 6th with 7th in bit 4 of the same byte (CRS_SIXTH_UP): clear = In place, the default and every
     * older record's (the Orchid's: the 6th under the seventh); set = Octave up. No version: nothing to migrate
     * (docs/CHORD-NAMES.md "The 6th above a seventh") */
    cr_settings_defaults(&s);
    ok(CRS_SIXTH(&s) == 0u && CRS_VERSION == 15u, "6th with 7th: the default is In place; the record's version is still 15");
    cr_settings_factory(&s);
    ok(CRS_SIXTH(&s) == 0u, "6th with 7th: Reset Settings' record is In place");
    cr_settings_defaults(&s);
    s.pick_roots = CRS_SIXTH_UP;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SIXTH(&r) == 1u && CRS_NAMES(&r) == 0u && CRS_OUT_CLEAN(&r) == 1u && CRS_USB48(&r) == 0u,
       "6th with 7th Octave up (bit 4 of pick_roots) round-trips; Chord Names still Orchid, Output Clean, USB Rate 44.1 kHz");
    s.pick_roots = CRS_SIXTH_UP | CRS_NAMES_STD | CRS_USB_48K | CRS_OUT_CLASSIC | 1u;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SIXTH(&r) == 1u && CRS_NAMES(&r) == 1u && CRS_USB48(&r) == 1u && CRS_OUT_CLEAN(&r) == 0u &&
       r.pick_roots == 31u, "6th with 7th, Chord Names, USB Rate, Output Classic and the retired picker bit together: the five kept");
    {
        uint32_t k, good = 0;                              /* what 0.17 .. 0.20 before the option wrote there: 0 .. 15 */
        for (k = 0; k < 16u; k++) {
            cr_settings_defaults(&s);
            s.pick_roots = (uint8_t)k;
            cr_settings_seal(&s);
            good += cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SIXTH(&r) == 0u && r.pick_roots == k;
        }
        ok(good == 16u, "a record of before the option (the byte 0..15): 6th with 7th In place, its other bits as they were");
    }
    {
        uint32_t v, good = 0;                              /* every older version's record: In place */
        for (v = 1; v < CRS_VERSION; v++) {
            cr_settings_defaults(&s);
            s.pick_roots = v < 3 ? 0 : 1;
            s.version = (uint16_t)v;
            for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
            s.check = h;
            good += cr_settings_import(&r, &s, sizeof s) != 0 && CRS_SIXTH(&r) == 0u;
        }
        ok(good == CRS_VERSION - 1u, "records of versions 1..14: all read 6th with 7th In place");
    }
    {
        cr_settings_t m2, o2;
        cr_settings_defaults(&m2);
        cr_settings_defaults(&o2);
        m2.pick_roots = 1;
        o2.pick_roots = CRS_SIXTH_UP;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == (CRS_SIXTH_UP | 1u), "6th with 7th is automatic (an Option): saved by itself, bit 4 alone taken");
        o2.pick_roots = 0;
        cr_settings_auto(&m2, &o2);
        ok(m2.pick_roots == 1u, "  and cleared the same way");
    }
    {   /* the record -> the engine and back (cr_settings_apply / cr_settings_capture): it is the engine's setting */
        cr_t E;
        cr_settings_t t;
        uint8_t nn[CR_CHORD_MAX];
        int k;
        cr_init(&E, 0);
        ok(E.sixth_up == 0, "the engine: In place after cr_init");
        cr_settings_defaults(&s);
        s.pick_roots = CRS_SIXTH_UP | CRS_NAMES_STD | CRS_OUT_CLASSIC;
        cr_settings_apply(&s, &E, 0);
        cr_loop_event(&E, 0, 60, CR_Q_MAJ, CR_EXT_6 | CR_EXT_M7, 100, 1);
        k = cr_chord_base_ex(60, CR_Q_MAJ, CR_EXT_6 | CR_EXT_M7, E.sixth_up, nn);
        ok(E.sixth_up == 1 && k == 5 && nn[4] == 81 && E.v[CR_MAX_VOICES + CR_MAX_PADS].notes[4] == 81,
           "a record with the bit applied: the engine plays C7 6 with its 6th at 81 (A5)");
        t = s;
        cr_settings_capture(&t, &E, 0);
        ok(CRS_SIXTH(&t) == 1u && t.pick_roots == s.pick_roots, "  captured: the bit set, the byte's other bits as they were");
        cr_set_sixth_up(&E, 0);
        cr_settings_capture(&t, &E, 0);
        ok(CRS_SIXTH(&t) == 0u && t.pick_roots == (CRS_NAMES_STD | CRS_OUT_CLASSIC), "  In place captured: bit 4 alone cleared");
        cr_settings_defaults(&s);
        cr_set_sixth_up(&E, 1);
        cr_settings_apply(&s, &E, 0);
        ok(E.sixth_up == 0, "a record without the bit applied over Octave up: In place (GLO + SAVE, Reset Settings, a backup put back)");
    }
    {   /* what an older firmware does with the byte (docs/SETTINGS.md): its crs_sanitize was CRS_FIX(pick_roots, 0, hi)
         * with hi 1 up to 0.18, 7 in 0.19, 15 in the 0.20 builds of before this option: a value above hi becomes that
         * firmware's default for the whole byte, 1. This is the rule, modelled; the old binaries were not run */
        static const uint8_t HI[3] = { 1, 7, 15 };
        uint32_t k, hi, lost = 0, same = 0, wrong = 0;
        for (hi = 0; hi < 3u; hi++)
            for (k = 0; k < 32u; k++) {
                uint8_t old = (uint8_t)(k > HI[hi] ? 1u : k);
                cr_settings_t o;
                cr_settings_defaults(&o);
                o.pick_roots = old;
                if (k & CRS_SIXTH_UP) {            /* Octave up set here: the old firmware reads the byte's default */
                    if (old == 1u && CRS_OUT_CLEAN(&o) == 1u && CRS_USB48(&o) == 0u && CRS_NAMES(&o) == 0u) lost++;
                    else wrong++;
                } else if (k <= HI[hi]) {          /* In place here and nothing newer than it knows: as it wrote it */
                    if (old == k) same++;
                    else wrong++;
                }
            }
        ok(!wrong && lost == 3u * 16u && same == 2u + 8u + 16u,
           "an older firmware (the rule of 0.18, 0.19, 0.20 before the option): with Octave up set it finds the byte out of range and "
           "takes its default (Output Clean, USB Rate 44.1 kHz, Chord Names Orchid, the 6th in place); with In place it reads its own bits as before");
    }
    /* a version-3 record (written before the USB audio settings: zeros in their place, which would read as the
     * recording off) */
    cr_settings_defaults(&s);
    s.bpm = 97;
    s.pick_roots = 0;
    s.loop_rec = s.usb_in = s.usb_level = 0;
    s.version = 3;
    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 97 && r.pick_roots == 0 && r.usb_in == 1 &&
       r.usb_level == CRS_USB_MASTER && !r.loop_rec && r.version == CRS_VERSION,
       "version 3 -> 5: kept, USB Record takes its default (on), USB Level Master");
    /* a version-4 record (a 0.14 dev build: usb_out = USB Audio Out, 1 by default, where loop_rec is now): the byte
     * is cleared; USB Record and USB Level are kept */
    cr_settings_defaults(&s);
    s.bpm = 96;
    s.loop_rec = 1;
    s.usb_in = 0;
    s.usb_level = CRS_USB_FIXED;
    s.version = 4;
    for (h = 2166136261u, i = 12; i < CRS_SIZE; i++) { h ^= ((uint8_t *)&s)[i]; h *= 16777619u; }
    s.check = h;
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 96 && r.loop_rec == 0 && r.usb_in == 0 &&
       r.usb_level == CRS_USB_FIXED && r.version == CRS_VERSION,
       "version 4 -> 5: USB Audio Out's byte cleared, USB Record Off and USB Level Fixed kept");
    /* the USB settings round-trip; out of range takes the default */
    cr_settings_defaults(&s);
    s.usb_in = 0;
    s.usb_level = CRS_USB_FIXED;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && !r.usb_in && r.usb_level == CRS_USB_FIXED,
       "USB Record off, USB Level Fixed: kept");
    s.usb_in = 9;
    s.usb_level = 2;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.usb_in == 1 && r.usb_level == CRS_USB_MASTER,
       "USB settings out of range: their defaults (Record on, Master)");
}

static uint32_t crs_check(const cr_settings_t *s)   /* the header's checksum (FNV-1a over [12, CRS_SIZE)) */
{
    const uint8_t *b = (const uint8_t *)s;
    uint32_t h = 2166136261u, i;
    for (i = 12u; i < CRS_SIZE; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

/* version 6 (docs/PRESETS.md): the parts' sounds as (engine << 8) | pool position, each part's place per engine */
static void t_presets(void)
{
    cr_settings_t s, r, d;
    unsigned i, part, all = 1;
    cr_settings_defaults(&d);
    for (part = 0; part < 2u; part++)
        for (i = 0; i < (unsigned)CRS_NENG; i++)
            all &= crs_pool_get(&d, part, i) == CRS_POOL_DEFAULT;
    ok(all && d.chord_sound == CRS_SOUND_DEFAULT && d.bass_sound == CRS_SOUND_DEFAULT && d.version == CRS_VERSION,
       "v7 defaults: the UI's sounds (TINE EP, DEEP SUB), every engine's first preset for both parts");
    s = d;
    s.chord_sound = (uint16_t)(13u << 8 | 4u);     /* VA 04 */
    s.bass_sound = (uint16_t)(CRS_ENG_QUAD << 8 | 8u);   /* FM TONE 08 */
    crs_pool_set(&s, 0, 2, 4);
    crs_pool_set(&s, 0, 10, 3);                    /* rank 10: the byte that was rsv0 */
    crs_pool_set(&s, 1, 10, 0);                    /* .. rsv1 (0: INIT) */
    crs_pool_set(&s, 1, 0, 8);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && cr_settings_equal(&r, &s) && crs_pool_get(&r, 0, 10) == 3 &&
       r.pool_pos10_chord == 3 && crs_pool_get(&r, 1, 10) == 0 && crs_pool_get(&r, 0, 2) == 4 &&
       crs_pool_get(&r, 1, 0) == 8 && r.chord_sound == (13u << 8 | 4u), "v7: the sounds and the places per engine round-trip");
    s.pool_pos[1][3] = 200;                        /* (no pool is that long) */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && crs_pool_get(&r, 1, 3) == CRS_POOL_DEFAULT &&
       crs_pool_get(&r, 0, 2) == 4, "v7: a place out of range takes its default, the others kept");
    /* version 6 -> 7 (ANALOG retired: slot 0 is FM TONE's). A 0.14 dev record: FM TONE's chord place in loop_rec (5),
     * slot 0 ANALOG's (9 / 6), the bass on FM TONE 04; a 0.13 record: loop_rec 0, the parts' sounds on ANALOG */
    s = d;
    s.loop_rec = 5;
    crs_pool_set(&s, 0, 0, 9);
    crs_pool_set(&s, 1, 0, 6);
    crs_pool_set(&s, 0, 3, 7);
    s.chord_sound = (uint16_t)(12u << 8 | 3u);     /* FM6 03 */
    s.bass_sound = (uint16_t)(CRS_ENG_QUAD << 8 | 4u);
    cr_settings_seal(&s);
    s.version = 6;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && crs_pool_get(&r, 0, 0) == 5 && crs_pool_get(&r, 1, 0) == 4 &&
       !r.loop_rec && crs_pool_get(&r, 0, 3) == 7 && r.chord_sound == (12u << 8 | 3u) &&
       r.bass_sound == (CRS_ENG_QUAD << 8 | 4u) && r.version == CRS_VERSION,
       "version 6 (0.14 dev) -> 7: FM TONE's chord place from loop_rec into slot 0, the bass's from bass_sound, loop_rec 0");
    s = d;
    crs_pool_set(&s, 0, 0, 9);
    crs_pool_set(&s, 1, 0, 6);
    s.chord_sound = (uint16_t)(CRS_ENG_ANALOG << 8 | 2u);
    s.bass_sound = (uint16_t)(CRS_ENG_ANALOG << 8 | 8u);   /* ANALOG 08 SUB BASS */
    cr_settings_seal(&s);
    s.version = 6;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && crs_pool_get(&r, 0, 0) == CRS_POOL_DEFAULT &&
       crs_pool_get(&r, 1, 0) == CRS_POOL_DEFAULT && r.chord_sound == CRS_SOUND_DEFAULT &&
       r.bass_sound == CRS_SOUND_DEFAULT, "version 6 (0.13) -> 7: ANALOG's slot and sounds read as the defaults (DEEP SUB)");
    /* a version-5 record: list positions of the old bank (17, 4) and zeros where the places are now */
    s = d;
    s.chord_sound = 17;
    s.bass_sound = 4;
    for (i = 0; i < (unsigned)CRS_NENG; i++) {
        crs_pool_set(&s, 0, i, 0);
        crs_pool_set(&s, 1, i, 0);
    }
    s.bpm = 95;
    cr_settings_seal(&s);
    s.version = 5;
    s.check = crs_check(&s);
    all = 1;
    ok(cr_settings_import(&r, &s, sizeof s) == 2, "version 5: migrated");
    for (part = 0; part < 2u; part++)
        for (i = 0; i < (unsigned)CRS_NENG; i++)
            all &= crs_pool_get(&r, part, i) == CRS_POOL_DEFAULT;
    ok(all && r.chord_sound == CRS_SOUND_DEFAULT && r.bass_sound == CRS_SOUND_DEFAULT && r.bpm == 95 &&
       r.version == CRS_VERSION, "version 5 -> 7: the old list positions read as the defaults (TINE EP, DEEP SUB), every "
       "engine's first preset, the rest kept");
}

static void t_flash(void)
{
    cr_settings_t s, r;
    int n;
    memset(nor, 0xFF, sizeof nor);
    ok(st_load(OBJ_SETTINGS, &r, sizeof r) < 0, "flash: empty -> nothing (the caller uses defaults)");
    cr_settings_defaults(&s);
    s.bpm = 140;
    cr_settings_seal(&s);
    ok(st_save(OBJ_SETTINGS, &s, sizeof s) == 0, "flash: saved");
    n = st_load(OBJ_SETTINGS, &r, sizeof r);
    ok(n == (int)sizeof r && cr_settings_import(&r, &r, (uint32_t)n) == 1 && r.bpm == 140, "flash: read back, 140 BPM");
    s.bpm = 160;
    cr_settings_seal(&s);
    fail_after = 1;                                /* torn: the payload written, the commit record not */
    ok(st_save(OBJ_SETTINGS, &s, sizeof s) != 0, "flash: a torn save fails");
    fail_after = -1;
    n = st_load(OBJ_SETTINGS, &r, sizeof r);
    ok(n == (int)sizeof r && cr_settings_import(&r, &r, (uint32_t)n) == 1 && r.bpm == 140,
       "flash: after a torn save the previous copy is in charge");
    ok(st_save(OBJ_SETTINGS, &s, sizeof s) == 0 && st_load(OBJ_SETTINGS, &r, sizeof r) == (int)sizeof r &&
       cr_settings_import(&r, &r, sizeof r) == 1 && r.bpm == 160, "flash: the next save wins");
    /* the quiet writes (storage.c "erase ahead"; its states and its schedule: tests/storage_test.c): in ChoralRoot's
     * object list the session and the favourites; NOT the settings (3y52, the owner's decision of 2026-10-14: their
     * previous copy is the calibration's fallback) and no store */
    ok(st_ahead_ix(OBJ_SETTINGS) < 0 && st_ahead_ix(OBJ_SESSION) == 1 && st_ahead_ix(OBJ_FAVS) == 2 &&
       st_ahead_ix(OBJ_QUADSTORE) < 0 && st_ahead_ix(OBJ_UPRESET0) < 0 && st_ahead_ix(OBJ_FM6BANK) < 0 &&
       st_ahead_ix(OBJ_CZSTORE1) < 0 && st_ahead_ix(OBJ_PROJECT0) < 0, "flash: erased ahead: session, favourites (not the settings)");
    ok(st_prep(OBJ_SETTINGS) < 0 && !st_ready(OBJ_SETTINGS) && st_load(OBJ_SETTINGS, &r, sizeof r) == (int)sizeof r &&
       cr_settings_import(&r, &r, sizeof r) == 1 && r.bpm == 160 && st_prep(OBJ_SESSION) == 0 && st_prep(OBJ_FAVS) == 0,
       "flash: the settings refused by st_prep, both copies kept; the other two blank already");
}

/* version 8 (docs/LOOPER-MODES.md): the record mode and the PLAY menu's keys in loop_rec (rsv_usb before) */
static void t_loop_rec(void)
{
    cr_settings_t d, s, r;
    cr_settings_defaults(&d);
    ok(d.loop_rec == 0 && CRS_LOOP_MODE(&d) == 0 && CRS_LOOP_KEYS(&d) == 0, "v8 defaults: Overwrite, keys Play");
    s = d;
    s.loop_rec = CRS_LOOP_REC(3u, 1u);             /* Replace, Loops */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LOOP_MODE(&r) == 3 && CRS_LOOP_KEYS(&r) == 1,
       "v8: Replace + Loops round-trip");
    s.loop_rec = CRS_LOOP_REC(4u, 0u);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LOOP_MODE(&r) == 4 && !CRS_LOOP_KEYS(&r), "v8: Step + Play round-trip");
    s.loop_rec = 7;                                /* no such mode */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == 0, "v8: a mode out of range takes the defaults");
    s.loop_rec = 0x47;                             /* no such mode, with a v13 bit: the defaults all the same */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == 0, "v8: a mode out of range with other bits takes the defaults");
    s = d;                                         /* a version-7 record: the byte 0 (rsv_usb), the rest kept */
    s.bpm = 101;
    s.loop_rec = 0x12;                             /* (garbage a v7 writer could not have left: cleared anyway) */
    cr_settings_seal(&s);
    s.version = 7;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 101 && r.loop_rec == 0 && r.version == CRS_VERSION,
       "version 7 -> 8: kept, the record mode Overwrite and the keys Play");
}

/* version 13: the KEY layer's keys (0 pick, 1 play) in bit 5 of loop_rec, beside the record mode and the PLAY menu's
 * keys; default pick; an older record's bit 5 (no writer set it) is cleared, the rest of the byte kept */
static void t_key_keys_v13(void)
{
    cr_settings_t d, s, r;
    cr_settings_defaults(&d);
    ok(CRS_VERSION >= 13u && d.loop_rec == 0 && CRS_KEY_KEYS(&d) == 0, "v13 defaults: the KEY layer's keys pick");
    s = d;
    s.loop_rec = CRS_KEY_PLAY;                     /* play alone */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_KEY_KEYS(&r) == 1 && CRS_LOOP_MODE(&r) == 0 && !CRS_LOOP_KEYS(&r),
       "v13: keys play round-trip (Overwrite, Play beside it)");
    ok(cr_settings_import(&s, &r, sizeof r) == 1 && s.loop_rec == CRS_KEY_PLAY, "v13: play, a second round trip");
    s.loop_rec = (uint8_t)(CRS_LOOP_REC(3u, 1u) | CRS_KEY_PLAY);   /* Replace, Loops, play */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_KEY_KEYS(&r) == 1 && CRS_LOOP_MODE(&r) == 3 && CRS_LOOP_KEYS(&r) == 1,
       "v13: play with Replace + Loops: the three kept apart");
    s.loop_rec = CRS_LOOP_REC(3u, 1u);             /* .. and pick with them */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_KEY_KEYS(&r) == 0 && CRS_LOOP_MODE(&r) == 3 && CRS_LOOP_KEYS(&r) == 1,
       "v13: pick with Replace + Loops");
    s.loop_rec = (uint8_t)(CRS_KEY_PLAY | 7u);     /* no such record mode: the whole byte's defaults (pick too) */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == 0, "v13: a mode out of range takes the defaults (pick)");
    s.loop_rec = (uint8_t)(CRS_LOOP_REC(1u, 1u) | CRS_LOCK_KEEP);   /* LOCK's styles (bits 6, 7), each alone and both */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == s.loop_rec && (r.loop_rec & CRS_LOCK_KEEP) &&
       !(r.loop_rec & CRS_LOCK_PUSH) && CRS_LOOP_MODE(&r) == 1 && CRS_LOOP_KEYS(&r) == 1 && !CRS_KEY_KEYS(&r),
       "v13: LOCK's Type key Keeps round-trip, the byte's other fields kept");
    s.loop_rec = CRS_LOCK_PUSH;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == CRS_LOCK_PUSH, "v13: LOCK's Ext keys Pushed round-trip");
    s.loop_rec = (uint8_t)(CRS_LOCK_KEEP | CRS_LOCK_PUSH | CRS_KEY_PLAY | CRS_LOOP_REC(4u, 1u));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == s.loop_rec, "v13: every bit of the byte set (Step, Loops, play, Keeps, Pushed) round-trip");
    ok(d.loop_rec == 0, "v13 defaults: LOCK's Type key Clears, Ext keys Toggle (today's behaviour)");
    s = d;                                         /* a version-12 record (0.16): Replace + Loops kept, pick */
    s.bpm = 97;
    s.loop_rec = CRS_LOOP_REC(3u, 1u);
    cr_settings_seal(&s);
    s.version = 12;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 97 && CRS_KEY_KEYS(&r) == 0 && CRS_LOOP_MODE(&r) == 3 &&
       CRS_LOOP_KEYS(&r) == 1 && r.version == CRS_VERSION, "version 12 -> 13: the record mode and the PLAY menu's keys kept, the KEY layer's keys pick");
    s.loop_rec = (uint8_t)(CRS_LOOP_REC(2u, 0u) | CRS_KEY_PLAY);   /* a v12 record with bit 5 set (no writer did) */
    cr_settings_seal(&s);
    s.version = 12;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && CRS_KEY_KEYS(&r) == 0 && CRS_LOOP_MODE(&r) == 2,
       "version 12 -> 13: a stray bit 5 cleared (pick), the record mode kept");
    s.loop_rec = (uint8_t)(CRS_LOOP_REC(2u, 1u) | CRS_LOCK_KEEP | CRS_LOCK_PUSH);   /* .. and stray bits 6 / 7 */
    cr_settings_seal(&s);
    s.version = 12;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.loop_rec == CRS_LOOP_REC(2u, 1u),
       "version 12 -> 13: stray bits 6 / 7 cleared (Clears, Toggle), the record mode and the PLAY menu's keys kept");
}

/* version 10: every mode's Hold off (an older record's arp Hold 1 cleared) */
/* version 14 (0.17): the KEY layer's page 2 in key_on's byte (bit 1 Key plays Notes, bit 2 the Full keyboard), Single's
 * third value (2 Split Bass) */
static void t_page2_v14(void)
{
    cr_settings_t d, s, r;
    cr_settings_out_t o;
    cr_t E;
    unsigned k;
    cr_settings_defaults(&d);
    ok(CRS_VERSION >= 14u && d.key_on == 0 && !CRS_KEY_PLAYS(&d) && !CRS_KEYBOARD(&d) && d.single == CR_SINGLE_FULL,
       "v14 defaults: Key Mode off, Key plays Chords, Keyboard Chords, Single Full Octave");
    for (k = 0; k < 8u; k++) {                     /* every combination of the three bits */
        s = d;
        s.key_on = (uint8_t)k;
        cr_settings_seal(&s);
        if (cr_settings_import(&r, &s, sizeof s) != 1 || r.key_on != k || CRS_KEY_ON(&r) != (k & 1u) ||
            CRS_KEY_PLAYS(&r) != ((k >> 1) & 1u) || CRS_KEYBOARD(&r) != ((k >> 2) & 1u))
            break;
    }
    ok(k == 8u, "v14: Key Mode / Key plays / Keyboard: the eight combinations round-trip");
    s = d;
    s.key_on = 24;                                 /* an unknown value (8 .. 23 are Snap's since 0.22): the byte's defaults */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.key_on == 0, "v14: key_on out of range takes the defaults");
    s = d;
    s.single = CR_SINGLE_SPLIT_BASS;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.single == CR_SINGLE_SPLIT_BASS, "v14: Single = Split Bass round-trips");
    s.single = 3;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.single == CR_SINGLE_FULL, "v14: Single out of range: Full Octave");
    /* the engine: apply / capture */
    s = d;
    s.key_on = CRS_KEY_BYTE(1, 1, 1);
    s.tonic = 2;
    s.single = CR_SINGLE_SPLIT_BASS;
    cr_settings_seal(&s);
    cr_init(&E, 0);
    cr_settings_apply(&s, &E, &o);
    ok(E.key_on == 1 && E.key_notes == 1 && E.full == 1 && E.single == CR_SINGLE_SPLIT_BASS && E.tonic == 2,
       "v14 apply: Key Mode on, Key plays Notes, the Full keyboard, Split Bass");
    r = d;
    cr_settings_capture(&r, &E, &o);
    ok(r.key_on == s.key_on && r.single == CR_SINGLE_SPLIT_BASS, "v14 capture(apply(s)): the byte and Single back");
    /* a version-13 record (0.17 before this): Key Mode on kept, the page-2 settings off; its Split kept */
    s = d;
    s.key_on = 1;
    s.single = CR_SINGLE_SPLIT;
    s.bpm = 97;
    cr_settings_seal(&s);
    s.version = 13;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 97 && r.key_on == 1 && r.single == CR_SINGLE_SPLIT &&
       r.version == CRS_VERSION, "version 13 -> 14: Key Mode and Split (Split Oct) kept, Key plays Chords, Keyboard Chords");
    s.key_on = 6;                                  /* a v13 record with the new bits set (no writer did): off */
    s.single = 2;
    cr_settings_seal(&s);
    s.version = 13;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.key_on == 0 && r.single == CR_SINGLE_FULL,
       "version 13 -> 14: stray bits there in an older record read as the defaults");
    /* what an older firmware (CRS_FIX(key_on, 0, 1), CRS_FIX(single, 0, 1)) does with a v14 record is its own code:
     * key_on > 1 or single > 1 are out of its ranges: Key Mode off, Full Octave, the rest of the record kept
     * (docs/SETTINGS.md); with the three at their defaults the bytes are what v13 wrote */
    s = d;
    s.key_on = 1;
    s.single = 1;
    cr_settings_seal(&s);
    ok(s.key_on <= 1 && s.single <= 1, "v14 with page 2 at its defaults and Single Full / Split Oct: the bytes an older firmware knows");
}

static void t_hold_v10(void)
{
    cr_settings_t d, s, r;
    cr_settings_defaults(&d);
    ok(d.par[CR_PM_ARP][CR_P_HOLD] == 0, "v10 defaults: arp Hold off");
    s = d; s.par[CR_PM_ARP][CR_P_HOLD] = 1; s.par[CR_PM_STRUM][CR_P_HOLD] = 1;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.par[CR_PM_STRUM][CR_P_HOLD] == 1, "v10: a set Hold round-trips");
    s.version = 9u; s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.par[CR_PM_ARP][CR_P_HOLD] == 0 && r.par[CR_PM_STRUM][CR_P_HOLD] == 0,
       "version 9 -> 10: Hold off in every mode");
}

/* version 9: the view lock in bit 7 of the view's byte (HOME held's menu; no reserve left) */
static void t_view_lock(void)
{
    cr_settings_t d, s, r;
    cr_settings_defaults(&d);
    ok(CRS_VIEW(&d) == 0 && !CRS_VIEW_LOCK(&d), "v9 defaults: Chord, unlocked");
    s = d;
    s.view = CRS_VIEW_BYTE(3u, 1);                 /* Geek Out, locked */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VIEW(&r) == 3 && CRS_VIEW_LOCK(&r), "v9: Geek Out + lock round-trip");
    s.view = CRS_VIEW_BYTE(4u, 0);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VIEW(&r) == 4 && !CRS_VIEW_LOCK(&r), "v9: Scope, unlocked");
    s.view = CRS_VIEW_BYTE(5u, 1);                 /* 0.22: Geek Out 2 (the sixth view), locked */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VIEW(&r) == 5 && CRS_VIEW_LOCK(&r), "Geek Out 2 + lock round-trip");
    s.view = 0x86;                                 /* a view out of range, locked */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VIEW(&r) == CRS_VIEW(&d) && !CRS_VIEW_LOCK(&r), "v9: a view out of range takes the default");
    s.view = 0x12;                                 /* an unknown bit */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VIEW(&r) == CRS_VIEW(&d) && !CRS_VIEW_LOCK(&r), "v9: an unknown bit takes the default");
    s = d;                                         /* a version-8 record: its view kept, unlocked */
    s.view = 2;
    cr_settings_seal(&s);
    s.version = 8;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && CRS_VIEW(&r) == 2 && !CRS_VIEW_LOCK(&r) && r.version == CRS_VERSION,
       "version 8 -> 9: the view kept, unlocked");
}

/* version 12 (0.16): Options > Dual Core in bit 6 of view, set = On, default Off. v11 (0.15) had set = Off and On the
 * default: every record older than v12 loads Off whatever its bit says; the view and its lock are not touched.
 * 0.22 (the owner, 2026-10-13): THE DEFAULT IS ON: a unit with no record (erased, zeroed, none), Reset Settings. The
 * version stays: a record that is there keeps what it holds (its Off cannot be told from 0.16 .. 0.21's default) */
static void crs_old(cr_settings_t *s, unsigned version)   /* s resealed as a record of an older version */
{
    cr_settings_seal(s);
    s->version = (uint16_t)version;
    s->check = crs_check(s);
}
static void t_dualcore_v12(void)
{
    cr_settings_t s, r;
    uint8_t z[CRS_SIZE];
    cr_settings_defaults(&s);
    ok(CRS_DUALCORE(&s) == 1 && s.view == CRS_DC_ON && CRS_VIEW(&s) == 0 && !CRS_VIEW_LOCK(&s),
       "0.22 defaults: Dual Core On (the view byte 0x40: view 0, unlocked)");
    memset(z, 0xFF, sizeof z);                     /* no record: an erased sector, zeros, nothing */
    ok(cr_settings_import(&r, z, sizeof z) == 0 && CRS_DUALCORE(&r), "an erased record: defaults, Dual Core On");
    memset(z, 0, sizeof z);
    ok(cr_settings_import(&r, z, sizeof z) == 0 && CRS_DUALCORE(&r) && cr_settings_import(&r, NULL, 0) == 0 &&
       CRS_DUALCORE(&r), "a zeroed record, no record: defaults, Dual Core On");
    cr_settings_defaults(&r);
    r.view = CRS_VIEW_BYTE(3, 1);
    r.loop_slot = 4;
    cr_settings_factory(&r);                       /* Options > Reset Settings */
    ok(CRS_DUALCORE(&r) && CRS_VIEW(&r) == 0 && r.loop_slot == 4, "Reset Settings (a record with Dual Core Off): On");
    s.view = (uint8_t)(CRS_VIEW_BYTE(3, 1) | CRS_DC_ON);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_DUALCORE(&r) && CRS_VIEW(&r) == 3 && CRS_VIEW_LOCK(&r),
       "v12: Dual Core On kept, with the view and its lock");
    ok(cr_settings_import(&s, &r, sizeof r) == 1 && CRS_DUALCORE(&s) && s.view == r.view, "v12: On round trip");
    s.view = CRS_VIEW_BYTE(2, 0);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && !CRS_DUALCORE(&r) && CRS_VIEW(&r) == 2, "v12: Dual Core Off kept");
    ok(cr_settings_import(&s, &r, sizeof r) == 1 && !CRS_DUALCORE(&s) && s.view == r.view, "v12: Off round trip");
    s.view = (uint8_t)(CRS_DC_ON | 0x08u);         /* an unknown bit: the default view, Dual Core as the record has it */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_DUALCORE(&r) && r.view == CRS_DC_ON, "v12: an unknown view bit: the default view, Dual Core kept On");
    s.view = 0x88u;                                /* .. and a record that has it Off stays Off (0.22: the default is On) */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && !CRS_DUALCORE(&r) && r.view == 0, "v12: an unknown view bit, Dual Core Off: the default view, kept Off");
    cr_settings_defaults(&s);                      /* a version-11 record, bit 6 clear (0.15's On, its default) */
    s.view = CRS_VIEW_BYTE(4, 1);
    crs_old(&s, 11);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_DUALCORE(&r) && CRS_VIEW(&r) == 4 && CRS_VIEW_LOCK(&r) &&
       r.view == CRS_VIEW_BYTE(4, 1) && r.version == CRS_VERSION,
       "version 11 -> 12, bit 6 clear (0.15's On): Dual Core Off, the view and lock kept");
    cr_settings_defaults(&s);                      /* a version-11 record, bit 6 set (0.15's Off) */
    s.view = (uint8_t)(CRS_VIEW_BYTE(3, 1) | 0x40u);
    crs_old(&s, 11);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_DUALCORE(&r) && CRS_VIEW(&r) == 3 && CRS_VIEW_LOCK(&r) &&
       r.view == CRS_VIEW_BYTE(3, 1) && r.version == CRS_VERSION,
       "version 11 -> 12, bit 6 set (0.15's Off): Dual Core Off, the view and lock kept");
    s.view = (uint8_t)(CRS_VIEW_BYTE(1, 0) | 0x40u);
    crs_old(&s, 11);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_DUALCORE(&r) && r.view == CRS_VIEW_BYTE(1, 0),
       "version 11 -> 12, bit 6 set, unlocked: Off, the view kept");
    ok(cr_settings_import(&s, &r, sizeof r) == 1 && !CRS_DUALCORE(&s) && s.view == r.view,
       "the migrated record read again (v12 now): still Off");
    cr_settings_defaults(&s);                      /* a version-10 record: bit 6 0 */
    s.view = CRS_VIEW_BYTE(4, 1);
    crs_old(&s, 10);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_DUALCORE(&r) && CRS_VIEW(&r) == 4 && CRS_VIEW_LOCK(&r) &&
       r.version == CRS_VERSION, "version 10 -> 12: Dual Core Off, the view and lock kept");
    cr_settings_defaults(&s);                      /* a version-8 record (before the lock): its view, Off */
    s.view = 2;
    crs_old(&s, 8);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_DUALCORE(&r) && CRS_VIEW(&r) == 2 && !CRS_VIEW_LOCK(&r),
       "version 8 -> 12: Dual Core Off, the view kept, unlocked");
}

/* ------------------------------------------------------------------ v15 and the session (0.18) --- */
/* version 15: the block's layout is v14's (an older firmware reads a v15 block as it read v14's; a longer block it
 * would not read at all: settings_persist.c's exact size); bass_on is always written 0. Migration from v12, v13, v14 */
static void t_v15(void)
{
    cr_settings_t d, s, r;
    cr_settings_out_t o;
    cr_t E;
    uint8_t big[CRS_SIZE + 64];
    uint32_t h, i;
    unsigned v;
    cr_settings_defaults(&d);
    ok(CRS_VERSION == 15u && CRS_SIZE == 192u && sizeof(cr_settings_t) == 192u && !d.bass_on,
       "v15: the block is still 192 bytes (0.17 and the tools read it); bass_on 0 by default");
    for (v = 12u; v <= 14u; v++) {                 /* a record of 0.16 (v12) / 0.17 (v13, v14): every field it has kept */
        s = d;
        s.bpm = 97; s.transpose = -3; s.sticky = 1; s.perform_on = 1; s.perform_mode = CR_PM_ARP; s.bass_voicing = 2;
        s.view = CRS_VIEW_BYTE(3, 1) | CRS_DC_ON; s.metro_on = 1; s.loop_slot = 2; s.chord_sound = 0x0D05; s.fx_on = 0;
        if (v >= 13u) s.loop_rec = (uint8_t)(CRS_LOOP_REC(2, 1) | CRS_KEY_PLAY | CRS_LOCK_PUSH);
        else s.loop_rec = CRS_LOOP_REC(2, 1);
        if (v >= 14u) { s.key_on = CRS_KEY_BYTE(1, 1, 1); s.single = CR_SINGLE_SPLIT_BASS; }
        else s.key_on = 1;
        crs_old(&s, v);
        if (cr_settings_import(&r, &s, sizeof s) != 2 || r.version != 15u || r.size != CRS_SIZE || r.bpm != 97 ||
            r.transpose != -3 || !r.sticky || !r.perform_on || r.perform_mode != CR_PM_ARP || r.bass_voicing != 2 ||
            CRS_VIEW(&r) != 3u || !CRS_VIEW_LOCK(&r) || !CRS_DUALCORE(&r) || !r.metro_on || r.loop_slot != 2 ||
            r.chord_sound != 0x0D05 || r.fx_on || CRS_LOOP_MODE(&r) != 2u || !CRS_LOOP_KEYS(&r) || r.bass_on ||
            CRS_KEY_KEYS(&r) != (v >= 13u) || ((r.loop_rec & CRS_LOCK_PUSH) != 0) != (v >= 13u) ||
            r.key_on != (v >= 14u ? CRS_KEY_BYTE(1, 1, 1) : 1u) || r.single != (v >= 14u ? CR_SINGLE_SPLIT_BASS : CR_SINGLE_FULL))
            break;
    }
    ok(v == 15u, "v12, v13, v14 -> v15: migrated (rc 2), every field of the older record kept, resealed v15");
    /* what 0.17 does with a v15 block: its import takes a newer version of the same size by its fields (the rule
     * this import has had since v1); modelled here by reading a v16 block of our size */
    s = d;
    s.bpm = 141;
    crs_old(&s, CRS_VERSION + 1u);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 141 && r.version == CRS_VERSION,
       "a too-new block of the same size (0.17 reading v15, v15 reading v16): its fields kept");
    /* a too-new, longer record: our prefix */
    cr_settings_defaults(&s);
    s.bpm = 133; s.vel = 77;
    memset(big, 0x5A, sizeof big);
    memcpy(big, &s, sizeof s);
    ((cr_settings_t *)big)->version = CRS_VERSION + 3;
    ((cr_settings_t *)big)->size = sizeof big;
    for (h = 2166136261u, i = 12; i < sizeof big; i++) { h ^= big[i]; h *= 16777619u; }
    ((cr_settings_t *)big)->check = h;
    ok(cr_settings_import(&r, big, sizeof big) == 2 && r.bpm == 133 && r.vel == 77 && r.size == CRS_SIZE,
       "a too-new, longer block: our 192-byte prefix kept");
    ok(cr_settings_import(&r, big, sizeof big - 1u) == 0 && r.bpm == 120, "a too-new block cut short: the defaults");
    /* bass_on: the engine's capture never puts it into what an older firmware would switch on (the glue writes 0;
     * the portable capture reads the engine: a record with it set still applies, as it always did) */
    cr_init(&E, &OUT);
    s = d;
    s.bass_on = 1;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.bass_on == 1, "bass_on = 1 in a block is still in range");
    cr_settings_apply(&r, &E, &o);
    ok(E.bass_on == 1, ".. and applies (cr_settings.c's glue writes 0: Bass on is the session's)");
    /* 0.22: the KEY layer's Numerals in bit 1 of bass_on (CRS_NUMERALS): off by default and in every older record;
     * round-trips; never the engine's Bass on; the engine's capture keeps it; and the rule 0.17 .. 0.21 apply to the
     * byte (their CRS_FIX(bass_on, 0, 1), default 0: modelled here, the old binaries were not run) reads 2 as 0, which
     * is what they would write themselves: nothing is reset there */
    ok(!CRS_NUMS(&d) && CRS_VERSION == 15u, "Numerals: Off by default, no settings version for it");
    s = d;
    s.bass_on = CRS_NUMERALS;
    s.key_on = CRS_KEY_BYTE(1, 1, 1);
    s.pick_roots = 31;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_NUMS(&r) == 1u && r.bass_on == CRS_NUMERALS && r.key_on == 7u &&
       r.pick_roots == 31u, "Numerals On round-trips (bit 1 of bass_on); the key byte and pick_roots untouched");
    cr_init(&E, &OUT);
    cr_settings_apply(&r, &E, &o);
    ok(E.bass_on == 0, ".. and does not switch the engine's bass on");
    cr_settings_capture(&r, &E, &o);
    ok(CRS_NUMS(&r) == 1u && !(r.bass_on & 1u), ".. the engine's capture keeps the bit");
    s.bass_on = 4;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.bass_on == 4 && CRS_TOUCH(&r) == 1u,
       "bass_on 4: Key Touch Dynamic since 0.22 (bits 2..7 of the byte: docs/VELOCITY.md), kept");
    {
        unsigned old = CRS_NUMERALS;               /* what 0.21 reads: out of its 0..1, so its default */
        if (old > 1u) old = d.bass_on;
        ok(old == 0u && d.bass_on == 0u, "0.17 .. 0.21 read a Numerals-On byte as bass_on 0: nothing reset");
    }
    s = d;                                         /* an older record: whatever its version, the bit is 0 */
    s.bpm = 97;
    cr_settings_seal(&s);
    s.version = 14;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 97 && !CRS_NUMS(&r), "a version-14 record: Numerals Off");
    {                                              /* manual: the automatic save does not carry it */
        cr_settings_t a = d, m = d;
        a.bass_on = CRS_NUMERALS;
        cr_settings_auto(&m, &a);
        ok(!CRS_NUMS(&m), "Numerals is manual (SAVE held), as Key plays and Keyboard");
    }
}

static void css_reseal(uint8_t *b, unsigned version, unsigned size)   /* a session record of another version / size */
{
    uint32_t h = 2166136261u, i;
    ((cr_session_t *)(void *)b)->version = (uint16_t)version;
    ((cr_session_t *)(void *)b)->size = (uint16_t)size;
    for (i = 12; i < size; i++) { h ^= b[i]; h *= 16777619u; }
    ((cr_session_t *)(void *)b)->check = h;
}
static void css_fill(cr_session_t *x)              /* a recognisable session: every item set */
{
    unsigned k, i;
    cr_session_defaults(x);
    x->bind = 0xC0FFEE01u;
    x->voicing = 2; x->octave = -1; x->bass_on = 1; x->lock = 1;
    x->latched = (uint8_t)((1u << CR_MOD_MAJ) | (1u << CR_MOD_MAJ7));
    x->loop_rate = 2;                              /* x2 */
    x->fx.sel = CSS_FXE_DELAY;
    x->fx.amt[CSS_FXE_REVERB] = 30; x->fx.amt[CSS_FXE_CHORUS] = 0; x->fx.amt[CSS_FXE_DELAY] = 99; x->fx.amt[CSS_FXE_DRIVE] = 127;
    cr_session_fx_set(x, CSS_FXP_DTIME, 5);
    cr_session_fx_set(x, CSS_FXP_DFDBK, 77);
    cr_session_fx_set(x, CSS_FXP_RSIZE, 12);
    for (k = 0; k < 2u; k++) {
        css_part_t *r = &x->part[k];
        r->valid = 1; r->eng = (uint8_t)(k ? 13 : 12); r->preset = (uint8_t)(3 + k); r->user = 0; r->trim = (int8_t)(-2 - (int)k);
        r->edited = 1; r->kind = 1; r->fk = (uint8_t)(3 + k); r->slot = 0xFF; r->np = 91; r->nblob = (uint8_t)(k ? 110 : 128);
        strcpy(r->name, k ? "DEEP SUB" : "TINE EP");
        for (i = 0; i < 91u; i++) r->p[i] = (int16_t)((int)i * 7 - 200 + (int)k);
        for (i = 0; i < r->nblob; i++) r->blob[i] = (uint8_t)(i * 3u + k);
    }
    cr_session_seal(x);
}
static cr_session_t SX, SR, SD;
static uint8_t sbig[CSS_SIZE + 128];
static void t_session(void)
{
    uint32_t i, s;
    int v = 0, rc;
    unsigned off_fx = (unsigned)((const uint8_t *)&SX.fx - (const uint8_t *)&SX);
    unsigned off_part = (unsigned)((const uint8_t *)&SX.part[0] - (const uint8_t *)&SX);
    /* the record */
    cr_session_defaults(&SD);
    ok(sizeof(cr_session_t) == CSS_SIZE && CSS_SIZE == 1024u && CSS_SIZE <= ST_PAYLOAD_MAX,
       "session: 1024 bytes, a quarter of the object's payload");
    ok(SD.magic == CSS_MAGIC && SD.version == CSS_VERSION && SD.size == CSS_SIZE && !SD.voicing && !SD.octave &&
       !SD.bass_on && !SD.lock && !SD.latched && !SD.part[0].valid && !SD.part[1].valid && SD.fx.version == CSS_FX_VERSION &&
       SD.fx.n == 0 && SD.fx.sel == CSS_FXE_REVERB && SD.fx.amt[0] == CSS_FX_NONE && SD.fx.amt[CSS_NFXE - 1] == CSS_FX_NONE,
       "session defaults: nothing stored (the unit as it powered on until 0.17: bass off, voicing 0, the sounds' own sends)");
    ok(sizeof SD.rsv + 2u * sizeof SD.part[0].rsv + sizeof SD.rsv_end == 120u && sizeof SD.smk_master == 2u &&
       (const uint8_t *)SD.smk_master - (const uint8_t *)&SD == 22 && !SD.smk_master[0] && !SD.smk_master[1] &&
       (const uint8_t *)&SD.fx - (const uint8_t *)&SD == 68 &&
       CSS_NFXP - 29 == 0 && CSS_NP - 91 == 5,
       "session: 120 bytes of reserve (the SMK-37's MASTER at bytes 22, 23, 0 = not stored; 122 until issue 30, 126 until the routing took rsv_fx's last 4, 142 until 0.22's master filter took 16 of it), no free FX parameter entry of 29, 5 free part parameters");
    ok(SD.loop_rate == 0 && (const uint8_t *)&SD.loop_rate - (const uint8_t *)&SD == 21,
       "session: the loop's Rate, byte 21 (the first of the old reserve), 0 = x1: a record written before it reads x1");
    /* every item round-trips */
    css_fill(&SX);
    rc = cr_session_import(&SR, &SX, sizeof SX);
    ok(rc == 1 && !memcmp(&SR, &SX, sizeof SX), "session: every item round-trips, byte for byte (rc 1)");
    ok(SR.voicing == 2 && SR.octave == -1 && SR.bass_on == 1 && SR.lock == 1 && SR.loop_rate == 2 &&
       SR.latched == ((1u << CR_MOD_MAJ) | (1u << CR_MOD_MAJ7)), "session: voicing +2, octave -1, Bass on, LOCK with MAJ + M7, the loop's Rate x2");
    ok(SR.fx.sel == CSS_FXE_DELAY && SR.fx.amt[CSS_FXE_DELAY] == 99 && SR.fx.amt[CSS_FXE_DRIVE] == 127 && SR.fx.n == 3 &&
       cr_session_fx_get(&SR, CSS_FXP_DTIME, &v) && v == 5 && cr_session_fx_get(&SR, CSS_FXP_DFDBK, &v) && v == 77 &&
       cr_session_fx_get(&SR, CSS_FXP_RSIZE, &v) && v == 12 && !cr_session_fx_get(&SR, CSS_FXP_CRATE, &v),
       "session: the FX block: the effect picked, the amounts, the parameters by tag (one not stored: not found)");
    ok(SR.part[0].valid && SR.part[0].eng == 12 && SR.part[0].edited && !strcmp(SR.part[0].name, "TINE EP") &&
       SR.part[0].p[90] == 430 && SR.part[0].blob[127] == (uint8_t)(127 * 3) && SR.part[1].eng == 13 &&
       !strcmp(SR.part[1].name, "DEEP SUB") && SR.part[1].trim == -3 && SR.part[1].nblob == 110,
       "session: both parts' edit buffers (parameters, deep patch, name, pool entry, edited)");
    memcpy(sbig, &SX, sizeof SX);                  /* in place (the flash side imports into the buffer it read) */
    ok(cr_session_import((cr_session_t *)(void *)sbig, sbig, CSS_SIZE) == 1 && !memcmp(sbig, &SX, CSS_SIZE),
       "session: imported in place");
    /* the hash: the state, not the binding */
    SR = SX;
    SR.bind = 0x12345678u;
    cr_session_seal(&SR);
    ok(cr_session_hash(&SR) == cr_session_hash(&SX) && SR.check != SX.check, "session: `bind` is not part of the change detection");
    SR.voicing = 3;
    ok(cr_session_hash(&SR) != cr_session_hash(&SX), "session: a changed item changes the hash");
    /* invalid: the defaults */
    ok(cr_session_import(&SR, 0, 0) == 0 && !memcmp(&SR, &SD, sizeof SD), "session: none -> the defaults");
    ok(cr_session_import(&SR, &SX, 15) == 0 && !memcmp(&SR, &SD, sizeof SD), "session: cut short -> the defaults");
    for (i = 12; i < CSS_SIZE; i += 37u) {         /* a flipped bit anywhere */
        memcpy(sbig, &SX, sizeof SX);
        sbig[i] ^= 0x10u;
        if (cr_session_import(&SR, sbig, CSS_SIZE) != 0 || memcmp(&SR, &SD, sizeof SD))
            break;
    }
    ok(i >= CSS_SIZE, "session: a flipped bit anywhere -> the defaults");
    memcpy(sbig, &SX, sizeof SX);
    sbig[0] ^= 1u;
    ok(cr_session_import(&SR, sbig, CSS_SIZE) == 0, "session: a bad magic -> the defaults");
    css_fill(&SX);
    memcpy(sbig, &SX, sizeof SX);
    css_reseal(sbig, 0, CSS_SIZE);
    ok(cr_session_import(&SR, sbig, CSS_SIZE) == 0, "session: version 0 -> the defaults");
    for (s = 0, i = 0; i < 2000u; i++) {           /* garbage never hangs, never reads as a session */
        uint32_t k;
        for (k = 0; k < sizeof sbig; k++) { s = s * 1664525u + 1013904223u; sbig[k] = (uint8_t)(s >> 24); }
        if (i & 1u) { memcpy(sbig, &SX, 8); }      /* (half of them with a good magic and version) */
        if (cr_session_import(&SR, sbig, (s >> 8) % (sizeof sbig + 1u)) != 0 || memcmp(&SR, &SD, sizeof SD))
            break;
    }
    ok(i == 2000u, "session: 2000 garbage blocks -> the defaults each time");
    /* an older, shorter record (a future field appended after it): what it has kept, the rest the defaults */
    css_fill(&SX);
    memcpy(sbig, &SX, sizeof SX);
    css_reseal(sbig, CSS_VERSION, off_part);       /* state + FX, no parts */
    rc = cr_session_import(&SR, sbig, off_part);
    ok(rc == 2 && SR.voicing == 2 && SR.lock == 1 && SR.fx.sel == CSS_FXE_DELAY && SR.fx.amt[CSS_FXE_DELAY] == 99 &&
       !SR.part[0].valid && !SR.part[1].valid && SR.size == CSS_SIZE,
       "session: a shorter record (no parts): the state and the FX kept, no sound stored (rc 2)");
    memcpy(sbig, &SX, sizeof SX);
    css_reseal(sbig, CSS_VERSION, off_fx);         /* the state alone */
    rc = cr_session_import(&SR, sbig, off_fx);
    ok(rc == 2 && SR.voicing == 2 && SR.octave == -1 && SR.bass_on == 1 && SR.fx.version == CSS_FX_VERSION &&
       SR.fx.n == 0 && SR.fx.amt[CSS_FXE_DELAY] == CSS_FX_NONE && SR.fx.sel == CSS_FXE_REVERB,
       "session: a shorter record (the state alone): the FX block's defaults");
    /* a too-new record: longer, a higher version: our prefix */
    memset(sbig, 0xA5, sizeof sbig);
    memcpy(sbig, &SX, sizeof SX);
    css_reseal(sbig, CSS_VERSION + 2u, sizeof sbig);
    rc = cr_session_import(&SR, sbig, sizeof sbig);
    SX.bind = SR.bind;
    ok(rc == 2 && SR.version == CSS_VERSION && SR.size == CSS_SIZE && cr_session_hash(&SR) == cr_session_hash(&SX),
       "session: a too-new, longer record: our prefix, every item (rc 2)");
    ok(cr_session_import(&SR, sbig, sizeof sbig - 1u) == 0, "session: a too-new record cut short -> the defaults");
    /* the FX sub-block is versioned on its own: another layout -> its defaults, the rest kept */
    css_fill(&SX);
    SX.fx.version = CSS_FX_VERSION + 1u;
    cr_session_seal(&SX);
    rc = cr_session_import(&SR, &SX, sizeof SX);
    ok(rc == 1 && SR.fx.version == CSS_FX_VERSION && SR.fx.n == 0 && SR.fx.amt[CSS_FXE_DELAY] == CSS_FX_NONE &&
       SR.voicing == 2 && SR.part[0].valid && SR.part[1].valid && SR.lock == 1,
       "session: an FX block of another version: the FX defaults, everything else kept");
    /* .. and by tag: a tag this firmware does not know is carried and skipped, a new one finds room, a full block says so */
    css_fill(&SX);
    ok(cr_session_fx_set(&SX, 200, -5) && SX.fx.n == 4 && cr_session_fx_get(&SX, 200, &v) && v == -5 &&
       cr_session_fx_get(&SX, CSS_FXP_DTIME, &v) && v == 5, "session FX: an unknown tag sits beside the known ones");
    ok(cr_session_fx_set(&SX, CSS_FXP_DTIME, 9) && SX.fx.n == 4 && cr_session_fx_get(&SX, CSS_FXP_DTIME, &v) && v == 9,
       "session FX: a tag set again replaces its value");
    for (i = 0; i < 40u; i++)
        if (!cr_session_fx_set(&SX, 20u + i, (int)i))
            break;
    ok(i == CSS_NFXP - 4u && SX.fx.n == CSS_NFXP && !cr_session_fx_set(&SX, 0, 1) && !cr_session_fx_set(&SX, 256, 1),
       "session FX: 29 parameters fit (28 until the routing, 24 until 0.22's master filter); a full block, tag 0 and a tag over 255 are refused");
    ok(CSS_NFXP == 29 && sizeof(css_fx_t) == 128 && offsetof(cr_session_t, fx) == 68 && offsetof(cr_session_t, part) == 196 &&
       offsetof(css_fx_t, par) == 12 && CSS_FXP_XQ_LOW == 31 && CSS_FXP_XQ_ON == 34 && CSS_FXE_FILTER == 7 &&
       CSS_FXP_XF_ORDER == 35 && CSS_FXP_XF_RMODE == 36 && CSS_FXE_ROUTE == 8,
       "session FX: the block's new entries took rsv_fx's bytes (0.22's four, the routing's one): every field before and after them where it was");
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.fx.n == CSS_NFXP && cr_session_fx_get(&SR, 200, &v) && v == -5,
       "session FX: a full block round-trips");
    /* ranges: a field out of range takes its default, the others are kept */
    css_fill(&SX);
    SX.voicing = 13; SX.octave = -3; SX.bass_on = 2; SX.fx.amt[CSS_FXE_REVERB] = 128; SX.part[1].np = CSS_NP + 1;
    cr_session_seal(&SX);
    rc = cr_session_import(&SR, &SX, sizeof SX);
    ok(rc == 1 && SR.voicing == 0 && SR.octave == 0 && SR.bass_on == 0 && SR.fx.amt[CSS_FXE_REVERB] == CSS_FX_NONE &&
       SR.fx.amt[CSS_FXE_DELAY] == 99 && SR.part[0].valid && !SR.part[1].valid && SR.lock == 1,
       "session: out-of-range fields take their defaults (a part with too many parameters: no sound stored)");
    css_fill(&SX);
    SX.loop_rate = 4;
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.loop_rate == 0, "session: a Rate out of range (x1/16 .. x4 = -7 .. 3): x1");
    SX.loop_rate = -8;
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.loop_rate == 0, "session: Rate -8 (below x1/16): x1");
    SX.loop_rate = -7;
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.loop_rate == -7, "session: Rate x1/16 (-7) kept");
    SX.loop_rate = -3;
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.loop_rate == -3, "session: Rate x1/4 kept");
    css_fill(&SX);
    SX.lock = 0;                                   /* a latch without LOCK cannot be */
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && !SR.lock && !SR.latched, "session: LOCK off: no latched keys");
    css_fill(&SX);
    memset(SX.part[0].name, 'X', sizeof SX.part[0].name);
    cr_session_seal(&SX);
    ok(cr_session_import(&SR, &SX, sizeof SX) == 1 && strlen(SR.part[0].name) == CSS_NAME - 1u, "session: a name is always terminated");
}

/* the session through the flash layer: its own A/B pair (0xB4000 / 0xB5000), beside the settings object */
static void t_session_flash(void)
{
    cr_settings_t s;
    int n;
    memset(nor, 0xFF, sizeof nor);
    ok(st_sector(OBJ_SESSION, 0) == 0xB4000u && st_sector(OBJ_SESSION, 1) == 0xB5000u &&
       st_sector(OBJ_SETTINGS, 0) == 0xFC000u && st_sector(OBJ_QUADSTORE, 1) == 0xB3000u,
       "session flash: the pair 0xB4000 / 0xB5000 (the first free sectors after the FM TONE store)");
    ok(st_load(OBJ_SESSION, &SR, sizeof SR) < 0, "session flash: empty -> none");
    css_fill(&SX);
    ok(st_save(OBJ_SESSION, &SX, CSS_SIZE) == 0, "session flash: saved");
    n = st_load(OBJ_SESSION, &SR, sizeof SR);
    ok(n == (int)CSS_SIZE && cr_session_import(&SR, &SR, (uint32_t)n) == 1 && !memcmp(&SR, &SX, sizeof SX),
       "session flash: read back and imported in place, every item");
    SX.voicing = -4;
    cr_session_seal(&SX);
    fail_after = 2;                                /* torn: two payload pages written, no commit record */
    ok(st_save(OBJ_SESSION, &SX, CSS_SIZE) != 0, "session flash: a torn save fails");
    fail_after = -1;
    n = st_load(OBJ_SESSION, &SR, sizeof SR);
    ok(n == (int)CSS_SIZE && cr_session_import(&SR, &SR, (uint32_t)n) == 1 && SR.voicing == 2,
       "session flash: after a torn save the previous session is in charge");
    ok(st_save(OBJ_SESSION, &SX, CSS_SIZE) == 0 && st_load(OBJ_SESSION, &SR, sizeof SR) == (int)CSS_SIZE && SR.voicing == -4,
       "session flash: the next save wins");
    cr_settings_defaults(&s);                      /* the settings object is another pair: neither touches the other */
    ok(st_save(OBJ_SETTINGS, &s, sizeof s) == 0 && st_load(OBJ_SESSION, &SR, sizeof SR) == (int)CSS_SIZE && SR.voicing == -4 &&
       st_load(OBJ_SETTINGS, &s, sizeof s) == (int)sizeof s, "session flash: independent of the settings object");
    ok(st_save(OBJ_SESSION, &SX, 0) == 0 && st_load(OBJ_SESSION, &SR, sizeof SR) == 0,
       "session flash: an empty object (a settings restore clears it): nothing to import");
    ok(cr_session_import(&SR, &SR, 0) == 0 && !SR.part[0].valid && !SR.bass_on, ".. the defaults");
}

/* 0.18, saving is manual (the owner, 2026-10-11): the block's fields are of two kinds. cr_settings_auto copies the
 * AUTOMATIC ones (what the Options menu alone sets) and leaves the MANUAL ones: flash's block with the Options of now
 * is what an automatic write stores; every byte of the block is one or the other, never both */
static void t_auto(void)
{
    cr_settings_t d, a, b, m;
    uint32_t i, nauto = 0, nman = 0, both = 0;
    int k, p;
    cr_settings_defaults(&d);
    a = d;                                         /* a: "flash" (the last global save); b: the state now, every field changed */
    b = d;
    b.playstyle = CR_PS_ADVANCED; b.extadd = 1; b.secret = CR_SECRET_ALL; b.vel = 64;
    for (i = 0; i < 3u; i++) { b.midi_en[i] = (uint8_t)!d.midi_en[i]; b.midi_ch[i] = 9; }
    b.clock_mode = CRS_CLOCK_IN; b.raw_sound = 1; b.motion = 2; b.leds = 1; b.palette = 3; b.usb_in = 0;
    b.usb_level = CRS_USB_FIXED;
    b.view = (uint8_t)(CRS_VIEW_BYTE(3, 1) | CRS_DC_ON);
    b.key_on = 7; b.tonic = 9; b.scale = 1; b.transpose = -5; b.single = CR_SINGLE_SPLIT_BASS; b.split_pc = 2;
    b.bass_mode = CR_BASS_SOLO; b.bass_voicing = 2; b.perform_on = 1; b.perform_mode = CR_PM_ARP; b.perf_sel = 4;
    b.sticky = 1; b.bpm = 137;
    for (k = 0; k < CRS_NPM; k++)
        for (p = 0; p < CRS_NPAR; p++)
            b.par[k][p] = (int16_t)(d.par[k][p] + 1);
    b.loop_sync = 3; b.loop_quant = 4; b.loop_count_in = 0; b.loop_level = 40; b.metro_on = 1; b.metro_sig = 2;
    b.metro_vol = 10; b.loop_slot = 6; b.loop_rec = 0xE3; b.fx_on = 0; b.chord_sound = 0x0C1A; b.bass_sound = 0x0203;
    for (i = 0; i < (uint32_t)CRS_NENG; i++) { crs_pool_set(&b, 0, i, 20); crs_pool_set(&b, 1, i, 30); }
    m = a;
    cr_settings_auto(&m, &b);
    ok(m.playstyle == CR_PS_ADVANCED && m.extadd == 1 && m.secret == CR_SECRET_ALL && m.vel == 64 && m.midi_ch[2] == 9 &&
       m.midi_en[2] == b.midi_en[2] && m.clock_mode == CRS_CLOCK_IN && m.raw_sound == 1 && m.motion == 2 && m.leds == 1 &&
       m.usb_in == 0 && m.usb_level == CRS_USB_FIXED && CRS_DUALCORE(&m) == 1,
       "auto: the Options alone are automatic: Play Style, Extension Addition, Secret Chords, Velocity, the MIDI channels, "
       "Raw Chord Sound, MIDI Clock, Motion, LEDs, USB Record, USB Level, Dual Core");
    ok(CRS_KEY_ON(&m) == 0 && m.tonic == d.tonic && m.scale == d.scale && m.transpose == 0 && m.single == d.single &&
       m.split_pc == d.split_pc && m.bass_mode == d.bass_mode && m.bass_voicing == 0 && m.perform_on == 0 &&
       m.perform_mode == d.perform_mode && m.perf_sel == 0 && m.sticky == 0 && m.bpm == 120 &&
       m.par[0][0] == d.par[0][0] && m.par[CRS_NPM - 1][CRS_NPAR - 1] == d.par[CRS_NPM - 1][CRS_NPAR - 1],
       "manual: Key Mode and the KEY layer (tonic, scale, Transpose, Single, the split, Key plays, Keyboard), the bass "
       "behaviour and register, Perform and its parameters, LATCH, the tempo: left as last saved");
    ok(m.loop_sync == 0 && m.loop_quant == 0 && m.loop_count_in == 1 && m.loop_level == 100 && m.metro_on == 0 &&
       m.metro_sig == 0 && m.metro_vol == 70 && m.loop_slot == 0 && m.loop_rec == 0 && m.fx_on == 1 &&
       m.chord_sound == d.chord_sound && m.bass_sound == d.bass_sound && crs_pool_get(&m, 0, 3) == CRS_POOL_DEFAULT &&
       crs_pool_get(&m, 1, 10) == CRS_POOL_DEFAULT,
       "manual: the looper's settings and slot, the record mode and the layers' keys, the metronome, FX on, each "
       "part's sound and pool places: left as last saved");
    ok(CRS_VIEW(&m) == 0 && CRS_VIEW_LOCK(&m) == 0, "manual: the view and its lock (Dual Core, in the same byte, is automatic)");
    for (i = 12; i < CRS_SIZE; i++) {              /* each byte of the block: a's or b's, by one rule */
        cr_settings_t x, y, z;
        memset(&x, 0x00, sizeof x);
        memset(&y, 0xFF, sizeof y);
        z = x;
        cr_settings_auto(&z, &y);                  /* 0xFF where automatic */
        if (((const uint8_t *)&z)[i] == 0xFF) nauto++;
        else if (((const uint8_t *)&z)[i] == 0x00) nman++;
        else both++;                               /* (the view's byte: bit 6 alone; pick_roots': bits 1 .. 4, Output, USB Rate, Chord Names, 6th with 7th; bass_on's: bits 2 .. 7, Key Touch and the two Sens) */
    }
    printf("     block: %u automatic bytes, %u manual, %u shared (the view's: Dual Core in bit 6; pick_roots': Output in bit 1, USB Rate in bit 2, Chord Names in bit 3, 6th with 7th in bit 4; bass_on's: Key Touch and the Sens in bits 2 .. 7; sticky's: the chord layouts' Options in bits 1 .. 6, 0.24)\n", (unsigned)nauto,
           (unsigned)nman, (unsigned)both);
    ok(nauto == 17u && both == 4u && nauto + nman + both == CRS_SIZE - 12u, "auto: 17 bytes and four bytes' bits of the block's 180 are automatic (0.24: sticky's bits 1 .. 6)");
    m = b;
    cr_settings_auto(&m, &b);
    ok(!memcmp(&m, &b, sizeof b), "auto: copying a record's own Options into it changes nothing");
}

/* the trim of a part put back from the session is the running firmware's table's, not the record's (0.19:
 * cr_settings.h cr_session_part_trim; cr_settings.c css_part_put calls it with cr_bank.c's cb_trim) */
static int trim_019(unsigned e, unsigned fk) { (void)e; (void)fk; return 0; }   /* 0.19's CB_TRIM: no row */
static int trim_018(unsigned e, unsigned fk)       /* 0.18's: CZ-1 (engine 14) BRASS 1, STRINGS 2, PIPE ORGAN 1 */
{
    return e != 14u ? 0 : fk == 1u ? -10 : fk == 5u ? -6 : fk == 35u ? -12 : 0;
}
static int trim_new(unsigned e, unsigned fk) { return e == 13u && fk == 7u ? -8 : 0; }   /* a row added later */
static void t_session_trim(void)
{
    static const struct { uint8_t fk; int8_t trim; const char *name; } Z[3] = {
        {1, -10, "BRASS 1"}, {5, -6, "STRINGS 2"}, {35, -12, "PIPE ORGAN 1"}};
    unsigned k, part, good = 0;
    css_part_t *r;
    for (k = 0; k < 3u; k++)                       /* as 0.18's css_part_get wrote them: the trim beside the pool entry */
        for (part = 0; part < 2u; part++) {
            css_fill(&SX);
            r = &SX.part[part];
            r->eng = 14; r->preset = Z[k].fk; r->user = 0; r->trim = Z[k].trim; r->edited = 0;
            r->kind = CSS_KIND_FACTORY; r->fk = Z[k].fk; r->slot = CSS_FK_NONE; r->nblob = 0;
            strcpy(r->name, Z[k].name);
            cr_session_seal(&SX);
            if (cr_session_import(&SR, &SX, sizeof SX) == 1 && SR.part[part].trim == Z[k].trim &&
                cr_session_part_trim(&SR.part[part], trim_018) == Z[k].trim &&
                cr_session_part_trim(&SR.part[part], trim_019) == 0)
                good++;
        }
    ok(good == 6u, "session trim: 0.18's session on CZ-1 BRASS 1, STRINGS 2, PIPE ORGAN 1 (trim -10 / -6 / -12 in the "
                   "record, either part) still loads and is put back with trim 0: the row is gone");
    css_fill(&SX);
    r = &SX.part[0];
    r->eng = 13; r->kind = CSS_KIND_FACTORY; r->fk = 7; r->slot = CSS_FK_NONE; r->trim = 0;
    ok(cr_session_part_trim(r, trim_new) == -8, "session trim: a sound with a row today, a record that says 0: the row's -8");
    r->trim = 5;
    ok(cr_session_part_trim(r, trim_new) == -8, "session trim: .. a record that says +5: the row's -8");
    r->edited = 1;
    ok(cr_session_part_trim(r, trim_new) == -8, "session trim: an edited, unsaved sound keeps its sound's row (an edit never moved the trim)");
    r->fk = 8;
    ok(cr_session_part_trim(r, trim_new) == 0, "session trim: its neighbour has no row: 0, whatever the record says");
    r->fk = 7; r->kind = CSS_KIND_USER; r->slot = 3; r->user = 4; r->edited = 0; r->trim = 0;
    ok(cr_session_part_trim(r, trim_new) == -8, "session trim: a user preset bound to that factory preset (an overwritten one): its row, as PRESETS loads it");
    r->fk = CSS_FK_NONE; r->trim = -8;
    ok(cr_session_part_trim(r, trim_new) == 0, "session trim: an added user preset: none, as PRESETS loads it");
    r->kind = CSS_KIND_INIT; r->fk = 7;
    ok(cr_session_part_trim(r, trim_new) == 0, "session trim: INIT: none");
    r->kind = 3; r->trim = -8;
    ok(cr_session_part_trim(r, trim_new) == 0, "session trim: a kind this firmware does not know: none");
    r->kind = CSS_KIND_FACTORY; r->valid = 0;
    ok(cr_session_part_trim(r, trim_new) == 0 && cr_session_part_trim(&SX.part[1], 0) == 0,
       "session trim: no sound stored, no table: none");
}

/* 0.22: Options > Key Touch, Velocity Sens, Aftertouch Sens in bits 2..7 of bass_on (docs/VELOCITY.md): 0 = Fixed /
 * Mid, the defaults and every older record's; mode 3 is not a mode; automatic (the Options of now) */
static void t_touch(void)
{
    cr_settings_t d, s, r, m2, o2;
    cr_settings_defaults(&d);
    ok(CRS_TOUCH(&d) == 0u && CRS_VSENS(&d) == 0u && CRS_ATSENS(&d) == 0u && d.bass_on == 0u, "defaults: Key Touch Fixed, both Sens Mid");
    s = d;
    s.bass_on = (uint8_t)(CRS_NUMERALS | CRS_TOUCH_BITS(2, 1, 2));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_TOUCH(&r) == 2u && CRS_VSENS(&r) == 1u && CRS_ATSENS(&r) == 2u && CRS_NUMS(&r) == 1u,
       "Velocity + Aftertouch, Sens Low / High beside Numerals: every bit kept");
    s.bass_on = (uint8_t)(CRS_TOUCH_BITS(3, 0, 0));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_TOUCH(&r) == 3u, "Key Touch 3: Vel + Poly AT (a mode since 3y123), kept");
    s.bass_on = 0;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_TOUCH(&r) == 0u && CRS_VSENS(&r) == 0u, "a 0.21 record (the byte 0): Fixed, Mid");
    cr_settings_defaults(&m2);
    cr_settings_defaults(&o2);
    m2.bass_on = CRS_NUMERALS;
    o2.bass_on = CRS_TOUCH_BITS(1, 2, 1);
    cr_settings_auto(&m2, &o2);
    ok(m2.bass_on == (uint8_t)(CRS_NUMERALS | CRS_TOUCH_BITS(1, 2, 1)), "Key Touch and the Sens are automatic (the Options of now); Numerals stays the record's");
    /* Velocity Curve: bits 5..7 of pick_roots, -3 .. +3 as two's complement */
    s = d;
    s.pick_roots = (uint8_t)(CRS_SIXTH_UP | CRS_VCURVE_OF(-2));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VCURVE(&r) == -2 && CRS_SIXTH(&r) == 1u, "Velocity Curve -2 beside 6th with 7th: both kept");
    s.pick_roots = CRS_VCURVE_OF(3);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VCURVE(&r) == 3, "Velocity Curve +3");
    s.pick_roots = (uint8_t)(4u << 5);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_VCURVE(&r) == -3, "the bits at -4 (not a curve): -3");
    ok(CRS_VCURVE(&d) == 0, "defaults: Velocity Curve 0");
    cr_settings_defaults(&m2);
    cr_settings_defaults(&o2);
    o2.pick_roots = CRS_VCURVE_OF(1);
    cr_settings_auto(&m2, &o2);
    ok(CRS_VCURVE(&m2) == 1, "Velocity Curve is automatic (an Option)");
}

/* 0.22, GitHub issue 18: the KEY layer's Snap in bits 3 .. 4 of key_on (CRS_SNAP, CRS_SNAP_BITS): Off (0) by default
 * and in every older record, no version; round-trips with the v14 bits; applies to and is captured from the engine;
 * manual (SAVE held); and the rule 0.17 .. 0.22-beta2 apply to the byte (their CRS_FIX(key_on, 0, 7): modelled here,
 * the old binaries were not run) reads a record with Snap on as the byte's defaults there: Key Mode off, Key plays
 * Chords, Keyboard Chords, until set again (the v14 caveat, docs/SETTINGS.md) */
/* GitHub issue 15: the KEY layer's Numerals = Solfege in bit 7 of scale (CRS_SOLFEGE), a bit of its own apart from
 * bass_on's On: off by default and in every older record; round-trips beside either scale; the engine gets the scale
 * without it and its capture keeps it; an out-of-range scale keeps the bit; manual; and the rule 0.17 .. 0.22 apply
 * to the byte (their CRS_FIX(scale, 0, 1), default Major: modelled, the old binaries were not run) */
static void t_solfege(void)
{
    cr_settings_t d, s, r;
    cr_settings_out_t o;
    cr_t E;
    unsigned sc;
    cr_settings_defaults(&d);
    ok(!CRS_SOL(&d) && CRS_SCALE(&d) == CR_SCALE_MAJOR && CRS_VERSION == 15u, "Solfege: off by default, no settings version for it");
    for (sc = 0; sc < 2u; sc++) {
        s = d;
        s.scale = (uint8_t)(CRS_SOLFEGE | sc);
        s.tonic = 9;
        cr_settings_seal(&s);
        ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SOL(&r) && CRS_SCALE(&r) == sc && !CRS_NUMS(&r) &&
           r.bass_on == 0, sc ? "Solfege round-trips with A minor (bit 7 of scale), bass_on untouched"
                              : "Solfege round-trips with A major (bit 7 of scale), bass_on untouched");
        cr_init(&E, &OUT);
        cr_settings_apply(&r, &E, &o);
        ok(E.scale == sc && E.key_on == 0 && E.bass_on == 0, sc ? ".. the engine gets minor without the bit" : ".. the engine gets major without the bit");
        cr_settings_capture(&r, &E, &o);
        ok(CRS_SOL(&r) && CRS_SCALE(&r) == sc, ".. and its capture keeps the bit");
    }
    s = d;
    s.scale = (uint8_t)(CRS_SOLFEGE | 5u);         /* an unknown scale: Major, the bit kept */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SOL(&r) && CRS_SCALE(&r) == CR_SCALE_MAJOR, "scale 5 with Solfege: Major, Solfege kept");
    s.scale = 0x7Fu;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.scale == CR_SCALE_MAJOR, "scale 127: Major, no Solfege");
    {
        unsigned old = CRS_SOLFEGE | CR_SCALE_MINOR;   /* what 0.17 .. 0.22 read: out of their 0..1, so Major */
        if (old > 1u) old = d.scale;
        ok(old == CR_SCALE_MAJOR, "0.17 .. 0.22 read a Solfege byte as Major (the scale lost there, nothing else)");
    }
    s = d;                                         /* an older record: whatever its version, the bit is 0 */
    s.scale = CR_SCALE_MINOR;
    cr_settings_seal(&s);
    s.version = 14;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && !CRS_SOL(&r) && CRS_SCALE(&r) == CR_SCALE_MINOR, "a version-14 minor record: no Solfege");
    {                                              /* manual: the automatic save does not carry it */
        cr_settings_t a = d, m = d;
        a.scale = CRS_SOLFEGE;
        cr_settings_auto(&m, &a);
        ok(!CRS_SOL(&m), "Solfege is manual (SAVE held), as Numerals");
    }
}

static void t_snap(void)
{
    cr_settings_t d, s, r, m, o2;
    cr_settings_out_t o;
    cr_t E;
    unsigned k;
    cr_settings_defaults(&d);
    ok(CRS_SNAP(&d) == 0u && CRS_VERSION == 15u && CRS_KEY_MAX == 23u, "Snap: Off by default, no settings version for it");
    for (k = 0; k <= CRS_KEY_MAX; k++) {           /* Snap 0 .. 2 with the eight v14 combinations */
        s = d;
        s.key_on = (uint8_t)k;
        cr_settings_seal(&s);
        if (cr_settings_import(&r, &s, sizeof s) != 1 || r.key_on != k || CRS_SNAP(&r) != (k >> 3) ||
            CRS_KEY_ON(&r) != (k & 1u) || CRS_KEY_PLAYS(&r) != ((k >> 1) & 1u) || CRS_KEYBOARD(&r) != ((k >> 2) & 1u))
            break;
    }
    ok(k == CRS_KEY_MAX + 1u, "Snap Off / Scale / Chord with Key Mode, Key plays and Keyboard: the 24 bytes round-trip");
    for (k = CRS_KEY_MAX + 1u; k < 256u; k++) {    /* Snap 3, a higher bit: the byte's defaults */
        s = d;
        s.key_on = (uint8_t)k;
        cr_settings_seal(&s);
        if (cr_settings_import(&r, &s, sizeof s) != 1 || r.key_on != 0u) break;
    }
    ok(k == 256u, "key_on 24 .. 255 (a Snap of 3, a higher bit): the byte's defaults");
    s = d;
    s.key_on = (uint8_t)(CRS_KEY_BYTE(1, 1, 0) | CRS_SNAP_BITS(CR_KEYSNAP_CHORD));
    cr_settings_seal(&s);
    cr_init(&E, 0);
    cr_settings_apply(&s, &E, &o);
    ok(E.snap == CR_KEYSNAP_CHORD && E.key_on == 1 && E.key_notes == 1 && !E.full, "apply: Snap Chord with Key Mode on and Key plays Notes");
    r = d;
    cr_settings_capture(&r, &E, &o);
    ok(r.key_on == s.key_on, "capture(apply(s)): the byte back, Snap in it");
    m = d; o2 = d;
    o2.key_on = (uint8_t)(1u | CRS_SNAP_BITS(CR_KEYSNAP_SCALE));
    cr_settings_auto(&m, &o2);
    ok(m.key_on == 0u, "Snap is manual (SAVE held), as the page's others: cr_settings_auto leaves the byte");
    {
        unsigned old = 1u | CRS_SNAP_BITS(CR_KEYSNAP_SCALE);   /* what 0.17 .. 0.22-beta2 read: out of their 0 .. 7 */
        if (old > 7u) old = d.key_on;
        ok(old == 0u, "0.17 .. 0.22-beta2 read a Snap-on record as key_on 0: Key Mode off, Chords, Chords there, until set again");
    }
    s = d;                                         /* a v14 record: its byte read as today (no older writer set the bits) */
    s.key_on = 5;
    cr_settings_seal(&s);
    s.version = 14;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.key_on == 5u && CRS_SNAP(&r) == 0u, "a v14 record: Key Mode on, the Full keyboard, Snap Off");
}

/* 0.24 (docs/CHORD-LAYOUT.md): Options > Chord Keys, Full K Shorts and the SUS Window in the spare bits 1 .. 6 of
 * `sticky` (bit 0 stays LATCH), MIDI Keys in bits 1 .. 5 of `raw_sound` (bit 0 stays Raw Chord Sound): zero the
 * defaults (no version), the ranges, the automatic bits, the engine's bit 0 alone (apply / capture), and the rule of
 * every older firmware (CRS_FIX(sticky, 0, 1), CRS_FIX(raw_sound, 0, 1): modelled, the old binaries were not run) */
static void t_layout(void)
{
    cr_settings_t d, s, r, m2, o2;
    cr_settings_out_t o;
    cr_settings_defaults(&d);
    ok(d.sticky == CRS_LAYOUT_FM1 && CRS_LAYOUT(&d) == 1u && CRS_FULLSHORT(&d) == 0u && CRS_SUSWIN_MS(&d) == 30u &&
       !CRS_LATCH(&d), "defaults: FM-1 (0.25.1), Full K Shorts Off, the SUS Window 30 ms, LATCH off");
    ok(d.raw_sound == 0 && CRS_MIDI_KEYS(&d) == 0u, "defaults: MIDI Keys Off (the byte 0)");
    s = d;
    s.sticky = CRS_STICKY_BYTE(1, 1, 1, 30);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LATCH(&r) == 1u && CRS_LAYOUT(&r) == 1u && CRS_FULLSHORT(&r) == 1u &&
       CRS_SUSWIN_MS(&r) == 30u, "LATCH, FM-1, Full K Shorts, 30 ms: every bit kept");
    s.sticky = CRS_STICKY_BYTE(0, 1, 0, 80);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SUSWIN_MS(&r) == 80u && CRS_LAYOUT(&r) == 1u && !CRS_LATCH(&r), "80 ms, FM-1");
    s.sticky = (uint8_t)(CRS_LAYOUT_FM1 | (11u << 3));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SUSWIN_MS(&r) == 30u && CRS_LAYOUT(&r) == 1u, "a window code out of range (85 ms): 30, FM-1 kept");
    s.sticky = (uint8_t)(CRS_LAYOUT_FM1 | (15u << 3));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_SUSWIN_MS(&r) == 30u && CRS_LAYOUT(&r) == 1u, ".. and the largest code (105 ms): 30");
    s.sticky = 0x81u;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.sticky == d.sticky, "bit 7 (unknown): the byte's default");
    s.sticky = 0;
    s.raw_sound = CRS_RAW_BYTE(1, 16);
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_RAW(&r) == 1u && CRS_MIDI_KEYS(&r) == 16u, "MIDI Keys ch 16 beside Raw Chord Sound: kept");
    s.raw_sound = (uint8_t)(1u | (17u << 1));
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_RAW(&r) == 1u && CRS_MIDI_KEYS(&r) == 0u, "MIDI Keys 17 (no such channel): Off, Raw Chord Sound kept");
    cr_settings_defaults(&m2);
    cr_settings_defaults(&o2);
    m2.sticky = 0;
    o2.sticky = CRS_STICKY_BYTE(1, 1, 1, 60);
    o2.raw_sound = CRS_RAW_BYTE(0, 5);
    cr_settings_auto(&m2, &o2);
    ok(CRS_LAYOUT(&m2) == 1u && CRS_FULLSHORT(&m2) == 1u && CRS_SUSWIN_MS(&m2) == 60u && !CRS_LATCH(&m2) && CRS_MIDI_KEYS(&m2) == 5u,
       "the three Options and MIDI Keys are automatic; LATCH (bit 0) stays the record's (manual)");
    cr_init(&C, &OUT);
    s = d;
    s.sticky = CRS_STICKY_BYTE(0, 1, 1, 40);
    s.raw_sound = CRS_RAW_BYTE(0, 9);
    cr_settings_apply(&s, &C, &o);
    ok(!C.sticky && !C.stream_on[CR_STREAM_RAW] && o.part[CR_STREAM_RAW] == CRS_NONE,
       "the engine takes bit 0 alone: FM-1 and a MIDI Keys channel switch on neither LATCH nor the RAW stream");
    cr_set_sticky(&C, 1);
    cr_settings_capture(&s, &C, &o);
    ok(CRS_LATCH(&s) == 1u && CRS_LAYOUT(&s) == 1u && CRS_FULLSHORT(&s) == 1u && CRS_SUSWIN_MS(&s) == 40u && CRS_MIDI_KEYS(&s) == 9u,
       "the engine's capture writes bit 0 and keeps the UI's bits");
    cr_set_sticky(&C, 0);
    s.sticky = CRS_STICKY_BYTE(1, 1, 0, 30);   /* an older firmware's CRS_FIX(sticky, 0, 1), CRS_FIX(raw_sound, 0, 1) */
    s.raw_sound = CRS_RAW_BYTE(1, 3);
    ok(s.sticky > 1u && s.raw_sound > 1u && CRS_STICKY_BYTE(1, 0, 0, 30) == 1u && CRS_RAW_BYTE(1, 0) == 1u,
       "an older firmware: a byte with a new bit is out of its range (LATCH off, Raw Chord Sound off there); with the defaults it is one it knows");
}

/* 0.25.1: FM-1 is the default Chord Keys; every record no 0.25.1 firmware wrote moves to FM-1 once, marked in bit 1 of
 * `leds` (cr_settings.h CRS_KEYS_MOVED); a marked record keeps its layout */
static cr_settings_t keys_rec(unsigned version, uint8_t sticky, uint8_t leds)
{
    cr_settings_t s;
    cr_settings_defaults(&s);
    s.version = (uint16_t)version;
    s.sticky = sticky;
    s.leds = leds;
    s.bpm = 111;
    s.check = 0;
    {   /* (seal by hand: cr_settings_seal writes this firmware's version) */
        const uint8_t *b = (const uint8_t *)&s;
        uint32_t h = 2166136261u, i;
        for (i = 12; i < CRS_SIZE; i++) { h ^= b[i]; h *= 16777619u; }
        s.check = h;
    }
    return s;
}
static void t_keys_default(void)
{
    cr_settings_t d, s, r, r2, m;
    unsigned v, good;
    cr_settings_defaults(&d);
    ok(CRS_LAYOUT(&d) == 1u && (d.leds & CRS_KEYS_MOVED) && CRS_LEDS(&d) == 0u && !CRS_LATCH(&d),
       "a fresh unit: FM-1, the record marked moved, LEDs Glow");
    ok(cr_settings_import(&r, NULL, 0) == 0 && CRS_LAYOUT(&r) == 1u && (r.leds & CRS_KEYS_MOVED),
       "no record (a new unit, Flash Data): FM-1");
    cr_settings_factory(&r);
    ok(CRS_LAYOUT(&r) == 1u && (r.leds & CRS_KEYS_MOVED), "Reset Settings: FM-1");
    /* 0.25 (v15, no mark) with Classic stored, LATCH, Full K Shorts, 60 ms, LEDs Stock: FM-1, the rest kept */
    s = keys_rec(15, CRS_STICKY_BYTE(1, 0, 1, 60), CRS_LEDS_STOCK);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LAYOUT(&r) == 1u && CRS_LATCH(&r) == 1u && CRS_FULLSHORT(&r) == 1u &&
       CRS_SUSWIN_MS(&r) == 60u && CRS_LEDS(&r) == 1u && (r.leds & CRS_KEYS_MOVED) && r.bpm == 111,
       "0.25 with Classic: FM-1 at the first start (current, rc 1: the session stays), LATCH / Full K Shorts / 60 ms / Stock kept");
    s = keys_rec(15, CRS_STICKY_BYTE(0, 1, 0, 45), 0);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LAYOUT(&r) == 1u && CRS_SUSWIN_MS(&r) == 45u && CRS_LEDS(&r) == 0u &&
       (r.leds & CRS_KEYS_MOVED), "0.25 with FM-1: FM-1, marked, 45 ms kept");
    s = keys_rec(15, 1, CRS_LEDS_STOCK);           /* 0.23: LATCH only (no Chord Keys), Stock */
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LAYOUT(&r) == 1u && CRS_LATCH(&r) == 1u && CRS_SUSWIN_MS(&r) == 30u &&
       CRS_LEDS(&r) == 1u && (r.leds & CRS_KEYS_MOVED), "0.23 (no Chord Keys): FM-1, LATCH and Stock kept, 30 ms");
    for (v = 1, good = 0; v < 15u; v++) {          /* every older version: FM-1 */
        s = keys_rec(v, 0, 0);
        good += cr_settings_import(&r, &s, sizeof s) == 2 && CRS_LAYOUT(&r) == 1u && (r.leds & CRS_KEYS_MOVED);
    }
    ok(good == 14u, "records of versions 1..14: FM-1, marked");
    /* Classic chosen after the move: kept by every later start (the move never runs twice) */
    s = keys_rec(15, CRS_STICKY_BYTE(0, 0, 0, 30), CRS_KEYS_MOVED);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LAYOUT(&r) == 0u && (r.leds & CRS_KEYS_MOVED),
       "Classic chosen after the move (marked): Classic at the next start");
    ok(cr_settings_import(&r2, &r, sizeof r) == 1 && cr_settings_equal(&r2, &r) && CRS_LAYOUT(&r2) == 0u,
       ".. and the start after that (a power cycle): Classic, the record unchanged");
    s = keys_rec(15, CRS_STICKY_BYTE(0, 0, 0, 30), 0);
    cr_settings_import(&r, &s, sizeof s);
    ok(cr_settings_import(&r2, &r, sizeof r) == 1 && cr_settings_equal(&r2, &r), "the move is idempotent: import(import(x)) == import(x)");
    /* the mark is automatic: an Options save (cr_settings_auto) carries it with the layout */
    cr_settings_defaults(&m);
    m.leds = 0;
    m.sticky = 0;
    s = d;
    s.sticky = 0;                                  /* (Classic chosen in Options, the mark from the import) */
    cr_settings_auto(&m, &s);
    ok(CRS_LAYOUT(&m) == 0u && (m.leds & CRS_KEYS_MOVED), "an Options save writes the layout and the mark together");
    /* out of range: leds 4 .. 255 -> the default byte (Glow, marked) */
    s = keys_rec(15, 0, 0x80);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.leds == d.leds && CRS_LAYOUT(&r) == 0u,
       "leds out of range (0x80): the default byte (Glow, marked), so Classic stays");
    /* what an older firmware reads (modelled from its ranges: the old binaries were not run): 0.18 .. 0.25 check leds
     * 0 .. 1, so a marked byte reads as their default, Glow; 0.24 / 0.25 read sticky 0 .. 0x7F (the layout as stored),
     * 0.23 and older sticky 0 .. 1 (a byte with FM-1 out of range: LATCH off). Their next write has no mark */
    s = d;
    ok(s.leds > 1u && CRS_LAYOUT(&s) == 1u && s.sticky <= 0x7Fu && s.sticky > 1u,
       "an older firmware on a 0.25.1 record: LEDs Glow (out of 0 .. 1), 0.24 / 0.25 the layout stored, 0.23 LATCH off");
    s = keys_rec(15, CRS_STICKY_BYTE(0, 0, 0, 30), CRS_LEDS_STOCK);   /* 0.25 saved Classic after a downgrade */
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && CRS_LAYOUT(&r) == 1u,
       "a downgrade that saved, then 0.25.1 again: the record comes from an older firmware and moves to FM-1 again");
}

int main(void)
{
    t_auto();
    t_layout();
    t_keys_default();
    t_defaults();
    t_fresh_playstyle();
    t_factory();
    t_roundtrip();
    t_corruption();
    t_version();
    t_presets();
    t_flash();
    t_loop_rec();
    t_view_lock(); t_hold_v10(); t_dualcore_v12(); t_key_keys_v13(); t_page2_v14();
    t_v15(); t_session(); t_session_trim(); t_session_flash();
    t_touch();
    t_snap();
    t_solfege();
    printf("cr_settings: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
