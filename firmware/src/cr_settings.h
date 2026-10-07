/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the persisted settings record (docs/SETTINGS.md). Standalone (stdint only): Felucca's
 * settings_persist.c includes it for the layout of its ChoralRoot block, so the block survives a build that
 * does not know it.
 *
 * One fixed-layout record, versioned and append-only: a newer field always goes into the reserve at the end
 * (rsv shrinks, the size stays CRS_SIZE), CRS_VERSION goes up, and cr_settings_import() gives it its default
 * when an older record (whose version is lower) is read. Every field is checked on import: a value out of range
 * takes its default; a bad magic / checksum gives all defaults. */
#ifndef CR_SETTINGS_H
#define CR_SETTINGS_H
#include <stdint.h>

#define CRS_MAGIC 0x31535243u           /* "CRS1" */
#define CRS_VERSION 5u
#define CRS_SIZE 192u                   /* bytes, header included; never changes (fields come out of rsv) */
#define CRS_NPM 5                       /* perform modes (cr_engine.h CR_PM_COUNT) */
#define CRS_NPAR 11                     /* parameters per mode (CR_P_COUNT) */
#define CRS_SOUND_DEFAULT 0xFFFFu       /* chord_sound: the UI's own default (TINE EP) */
#define CRS_PALETTE_MOD 0xFFu           /* palette: MOD, ChoralRoot's (gfx.c: the last palette) */
enum { CRS_CLOCK_OFF, CRS_CLOCK_OUT, CRS_CLOCK_IN };
enum { CRS_NONE = 0xFF };               /* out part: no part */
enum { CRS_USB_MASTER, CRS_USB_FIXED };  /* usb_level: USB audio follows MASTER / takes the full level (docs/USB-AUDIO.md) */

typedef struct {
    /* header (12 bytes) */
    uint32_t magic;                     /* CRS_MAGIC */
    uint16_t version;                   /* CRS_VERSION of the writer */
    uint16_t size;                      /* CRS_SIZE of the writer */
    uint32_t check;                     /* FNV-1a over bytes [12, size) */
    /* version 1 */
    uint8_t playstyle, extadd, secret, key_on;       /* cr_playstyle_t, cr_extadd_t, cr_secret_t, Key Mode */
    uint8_t tonic, scale;                            /* 0..11, cr_scale_t */
    int8_t transpose;                                /* -24..24 */
    uint8_t single;                                  /* cr_single_t: Full Octave / Split */
    uint8_t split_pc, vel, bass_on, bass_mode;       /* 0..11; key velocity 1..127; (0 at power-on); cr_bassmode_t */
    int8_t bass_voicing;                             /* bass register, octaves -2..4 */
    uint8_t perform_on, perform_mode, perf_sel;      /* on, cr_pmode_t, the PERF layer's entry 0..6 (Strum 2 ...) */
    uint8_t sticky, rsv0;                            /* latch */
    uint16_t bpm;                                    /* 20..300 */
    int16_t par[CRS_NPM][CRS_NPAR];                  /* per-mode perform parameters (cr_engine.h cr_param_t) */
    uint8_t loop_sync, loop_quant, loop_count_in, loop_level;   /* 0..5 (Free, 1..16 bars), 0..6, 0/1, 0..100 */
    uint8_t midi_en[3], midi_ch[3];                  /* per stream (MAIN BASS RAW): MIDI out on, channel 0..15 */
    uint8_t clock_mode, raw_sound, view, motion;     /* CRS_CLOCK_*, RAW also plays part 0, View 0..4, Motion 0..2 */
    uint8_t palette, leds, fx_on, rsv1;              /* palette index (CRS_PALETTE_MOD), LEDs Glow / Stock, FX on */
    uint16_t chord_sound, bass_sound;                /* list positions: PRESETS (CRS_SOUND_DEFAULT), ALGORITHM (BASS tap's, 1..; 0 = UI default) */
    /* version 2 */
    uint8_t metro_on, metro_sig, metro_vol, loop_slot;   /* the click, 4/4 3/4 6/8, its level 0..100, slot 0..9 */
    /* version 3 */
    uint8_t pick_roots;                              /* the engine picker's white roots: 1 engines, 0 they play */
    /* version 4 (rsv_usb: v4's usb_out, Options > USB Audio Out, the playback removed in v5: always 0 since) */
    uint8_t rsv_usb, usb_in, usb_level;              /* -, Options > USB Record (on), USB Level (CRS_USB_*) */
    uint8_t rsv[20];                                 /* new fields come out of here */
} cr_settings_t;

/* what cr_out.c's routing takes (cr_route_t, written field by field by the unit's glue) */
typedef struct {
    uint8_t part[3];                    /* 0 = CHORD part, CRS_NONE: none (RAW only: Raw Chord Sound) */
    uint8_t midi_en[3], ch[3];
    uint8_t clock_out;
    uint8_t clock_in;                   /* Options > MIDI Clock = In (clock_out is then 0) */
} cr_settings_out_t;

void cr_settings_defaults(cr_settings_t *s);
/* a stored block (n bytes) -> *s, defaults for what is absent or invalid. 1: current, 2: migrated (older or newer
 * version: the fields it has are kept), 0: invalid (all defaults) */
int  cr_settings_import(cr_settings_t *s, const void *blk, uint32_t n);
void cr_settings_seal(cr_settings_t *s);         /* header + checksum, before it is stored */
int  cr_settings_equal(const cr_settings_t *a, const cr_settings_t *b);   /* the payloads */

#ifdef CR_ENGINE_H
/* the record -> the engine's setters (call with the engine idle / the audio IRQ off) and the routing */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o);
/* the reverse: the engine's and the routing's fields of *s (the UI's own fields are left as they are) */
void cr_settings_capture(cr_settings_t *s, const cr_t *c, const cr_settings_out_t *o);
#endif

#endif
