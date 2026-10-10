/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of firmware/src/cr_settings.c (the record, its import, apply / capture with the engine) and of the
 * record through Felucca's flash layer (storage.c on a simulated NOR: A/B copies, a torn write):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o build/host/cr_settings_test tests/cr_settings_test.c \
 *      firmware/src/cr_settings.c firmware/src/cr_engine.c && build/host/cr_settings_test
 * (tests/run_cr_tests.sh runs it.) */
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
    ok(d.chord_sound == CRS_SOUND_DEFAULT && d.fx_on && d.view == 0 && d.motion == 0, "defaults: UI fields");
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
    s.view = 2; s.motion = 1; s.leds = 1; s.palette = 3; s.chord_sound = 17; s.bass_sound = 4; s.vel = 64;
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
    s.pick_roots = 7;
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.pick_roots == 1, "pick_roots out of range: the default");
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
    ok(all && d.chord_sound == CRS_SOUND_DEFAULT && d.bass_sound == CRS_SOUND_DEFAULT && d.version == 9u,
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
    s.loop_rec = 0x41;                             /* an unknown bit */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.loop_rec == 0, "v8: an unknown bit takes the defaults");
    s = d;                                         /* a version-7 record: the byte 0 (rsv_usb), the rest kept */
    s.bpm = 101;
    s.loop_rec = 0x12;                             /* (garbage a v7 writer could not have left: cleared anyway) */
    cr_settings_seal(&s);
    s.version = 7;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && r.bpm == 101 && r.loop_rec == 0 && r.version == CRS_VERSION,
       "version 7 -> 8: kept, the record mode Overwrite and the keys Play");
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
    s.view = 0x85;                                 /* a view out of range, locked */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.view == d.view, "v9: a view out of range takes the default");
    s.view = 0x12;                                 /* an unknown bit */
    cr_settings_seal(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 1 && r.view == d.view, "v9: an unknown bit takes the default");
    s = d;                                         /* a version-8 record: its view kept, unlocked */
    s.view = 2;
    cr_settings_seal(&s);
    s.version = 8;
    s.check = crs_check(&s);
    ok(cr_settings_import(&r, &s, sizeof s) == 2 && CRS_VIEW(&r) == 2 && !CRS_VIEW_LOCK(&r) && r.version == CRS_VERSION,
       "version 8 -> 9: the view kept, unlocked");
}

int main(void)
{
    t_defaults();
    t_roundtrip();
    t_corruption();
    t_version();
    t_presets();
    t_flash();
    t_loop_rec();
    t_view_lock();
    printf("cr_settings: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
