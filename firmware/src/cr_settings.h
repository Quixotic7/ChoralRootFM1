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
#define CRS_VERSION 15u                 /* v15 (0.18): the layout of v14, and beside it the session (cr_session_t below) */
#define CRS_SIZE 192u                   /* bytes, header included; never changes (fields come out of rsv; since v15 what
                                         * needs room goes into the session record, which has its own size and reserve) */
#define CRS_NPM 5                       /* perform modes (cr_engine.h CR_PM_COUNT) */
#define CRS_NPAR 11                     /* parameters per mode (CR_P_COUNT) */
#define CRS_SOUND_DEFAULT 0xFFFFu       /* chord_sound / bass_sound: the UI's own default (FM6 TINE EP, VA DEEP SUB) */
#define CRS_NENG 11                     /* the pool_pos slots, one per engine shown in 0.14 (five shown since 0.18: cr_settings.c) */
#define CRS_ENG_ANALOG 0u               /* engine numbers the record's migrations know (core.h): ANALOG (retired in 0.14), */
#define CRS_ENG_QUAD 15u                /* FM TONE (ENGI_QUAD, code name QUAD): pool_pos slot 0 since version 7 */
#define CRS_POOL_DEFAULT 1u             /* pool_pos: an engine not played yet lands on its first preset */
#define CRS_PALETTE_MOD 0xFFu           /* palette: MOD, ChoralRoot's (gfx.c: the last palette) */
/* view (v9): bits 0..2 the main view 0..5 (Chord, Keyboard, Notes, Geek Out, Scope; 5 Geek Out 2 since 0.22, which an
 * older firmware reads as out of range: Chord), bit 7 the view lock (HOME held's menu: locked, HOME tap never cycles
 * the view); the other bits 0. No reserve was left: the lock rides in the view's byte (an older firmware reads a locked
 * record's view as out of range: Chord) */
#define CRS_VIEW(s) ((unsigned)(s)->view & 7u)
#define CRS_VIEW_LOCK(s) (((unsigned)(s)->view >> 7) & 1u)
#define CRS_VIEW_BYTE(v, lock) ((uint8_t)(((v) & 7u) | ((lock) ? 0x80u : 0u)))
/* v12: bit 6 of view, Options > Dual Core On (docs/DUALCORE.md: core 1 started at the next power-on). Still no
 * reserve: it rides in the view's byte as the lock does, set = On, so a fresh, erased or zeroed record reads Off, the
 * default (0.16: one core unless the user turns the second on). v11 (0.15) had it the other way round, set = Off and
 * On the default: a v11 record cannot tell a chosen On from that default, so every record older than v12 loads Off
 * whatever its bit says (cr_settings_import). An older firmware reads an On record's view as out of range: Chord */
#define CRS_DC_ON 0x40u
#define CRS_DUALCORE(s) (((unsigned)(s)->view & CRS_DC_ON) ? 1u : 0u)
/* key_on (v14): bit 0 Key Mode on; bit 1 the KEY layer's page 2 "Key plays" (0 Chords, the default; 1 Notes: Key Mode
 * blocks the roots out of the scale and generates no chord); bit 2 its "Keyboard" (0 Chords, the default; 1 Full: all
 * 27 keys are single notes). Still no reserve and loop_rec is full: the two ride in Key Mode's own byte, 0 = the
 * defaults, so every older record reads them off. A firmware older than v14 reads a record with bit 1 or 2 set as
 * key_on out of range and takes its default: Key Mode off, nothing else (its Single reads 2, Split Bass, the same
 * way: Full Octave) */
#define CRS_KEY_NOTES 0x02u
#define CRS_KBD_FULL 0x04u
#define CRS_KEY_ON(s) ((unsigned)(s)->key_on & 1u)
#define CRS_KEY_PLAYS(s) (((unsigned)(s)->key_on & CRS_KEY_NOTES) ? 1u : 0u)
#define CRS_KEYBOARD(s) (((unsigned)(s)->key_on & CRS_KBD_FULL) ? 1u : 0u)
#define CRS_KEY_BYTE(on, notes, full) ((uint8_t)(((on) ? 1u : 0u) | ((notes) ? CRS_KEY_NOTES : 0u) | ((full) ? CRS_KBD_FULL : 0u)))
/* The KEY layer's page 2 "Snap" (0.22, GitHub issue 18: a key out of the scale plays the nearest note of the scale or
 * of the chord sounding, with Key plays Notes / the Full keyboard; 0 Off, the default / 1 Scale / 2 Chord,
 * cr_keysnap_t) rides in bits 3 .. 4 of the same byte: 0 in every older record and 0 is the default: no version,
 * nothing to migrate. A firmware from 0.17 to 0.22-beta2 checks the byte against 0 .. 7 and takes its default for a
 * record with Snap on: Key Mode off, Key plays Chords, Keyboard Chords there, until set again (as with v14's Notes /
 * Full, docs/SETTINGS.md); the other 0 / 1 bytes looked at would cost the scale or Single instead, away from Key
 * Mode's byte. Manual (SAVE held), as the page's others */
#define CRS_SNAP(s) (((unsigned)(s)->key_on >> 3) & 3u)
#define CRS_SNAP_BITS(v) ((uint8_t)(((v) & 3u) << 3))
#define CRS_KEY_MAX 23u                 /* crs_sanitize: bits 0 .. 4 with Snap 0 .. 2 (24 .. 31 would be Snap 3) */
/* The KEY layer's page 2 "Numerals" (0.22: the chord's Roman numeral beside the key on the top line; Off, the default
 * / On) rides in bit 1 of bass_on. That byte's bit 0 is always written 0 since v15 (Bass on is the session's), and
 * every firmware from 0.17 to 0.21 reads the byte with CRS_FIX(bass_on, 0, 1), default 0: it finds 2 out of range and
 * takes 0, which is what it would have read anyway: NOTHING is reset on the way back (key_on's bit 3 would have cost
 * Key Mode, Key plays and Keyboard there; pick_roots' bit 5 Output, USB Rate, Chord Names and 6th with 7th). 0 in
 * every older record and 0 is the default: no version, nothing to migrate. Manual (SAVE held), as the page's others */
#define CRS_NUMERALS 0x02u
#define CRS_NUMS(s) (((unsigned)(s)->bass_on & CRS_NUMERALS) ? 1u : 0u)
/* Its third value, Solfege (GitHub issue 15: the same degree as DO RE MI ..), is a bit of its own, apart from the bass
 * byte (the owner, 2026-10-16: "Add a new bit for the Solfege mode. don't mix it with bass"): bit 7 of `scale`
 * (CRS_SOLFEGE; the scale itself is bits 0..6, CRS_SCALE, room for more scales). Off: neither bit; On: CRS_NUMERALS
 * alone; Solfege: CRS_SOLFEGE alone (set, it wins over the other). 0 in every older record and 0 is the default: no
 * version, nothing to migrate. Every firmware from 0.17 to 0.22 checks the byte against 0 .. 1 (CRS_FIX(scale, 0,
 * 1)), so after a downgrade with Solfege saved it reads the scale as Major and Numerals as Off until set again,
 * nothing else; without Solfege the byte is one it knows. Manual (SAVE held), as the page's others */
#define CRS_SOLFEGE 0x80u
#define CRS_SCALE(s) ((unsigned)(s)->scale & 0x7Fu)
#define CRS_SOL(s) (((unsigned)(s)->scale & CRS_SOLFEGE) ? 1u : 0u)
/* Options > Key Touch, Velocity Sens, Aftertouch Sens (0.22; docs/VELOCITY.md) ride in bits 2..7 of the same byte:
 * bits 2-3 Key Touch (0 Fixed, the default: the keys send Options > Velocity, no sensing; 1 Dynamic: the keys'
 * velocity; 2 Velocity + Aftertouch; 3 Vel + Poly AT: the same, the pressure out as polyphonic aftertouch), bits 4-5
 * Velocity Sens and bits 6-7 Aftertouch Sens (0 Mid, the default; 1 Low;
 * 2 High). 0 in every older record and 0 is the default: no version, nothing to migrate; a firmware up to 0.21 finds
 * 4..255 out of range and takes its default for the byte (Numerals off) until it is set there */
#define CRS_TOUCH(s) (((unsigned)(s)->bass_on >> 2) & 3u)
#define CRS_VSENS(s) (((unsigned)(s)->bass_on >> 4) & 3u)
#define CRS_ATSENS(s) (((unsigned)(s)->bass_on >> 6) & 3u)
#define CRS_TOUCH_BITS(t, v, a) ((uint8_t)(((t) & 3u) << 2 | ((v) & 3u) << 4 | ((a) & 3u) << 6))
enum { CRS_CLOCK_OFF, CRS_CLOCK_OUT, CRS_CLOCK_IN };
enum { CRS_NONE = 0xFF };               /* out part: no part */
enum { CRS_USB_MASTER, CRS_USB_FIXED };
/* loop_rec (v8): bits 0..3 the record mode (cr_loop.h CRL_MODE_*: 0 Overwrite .. 4 Step), bit 4 the PLAY menu's keys
 * (0 Play: they play chords, 1 Loops: the slot shortcuts); the other bits 0. 0 = Overwrite, Play (the defaults).
 * v13: bit 5 the KEY layer's keys (0 pick: a root = the tonic, MAJ / MIN = the scale, the default; 1 play: the keys
 * play as outside the layer; OPT tapped in the layer). Still no reserve: it rides in this byte, next to the other
 * "what do the keys do in this menu" bit. A firmware older than v13 reads a record with bit 5 set as an unknown bit
 * and gives the whole byte its defaults (Overwrite, Play): its record mode and PLAY-menu keys are then lost on a
 * downgrade, nothing else.
 * v13 also: bits 6 and 7 the LOCK layer's two styles (B3 held): bit 6 "Type key" (0 Clears: a type key clears the
 * latched extensions, today's; 1 Keeps), bit 7 "Ext keys" (0 Toggle, today's; 1 Pushed). The same downgrade caveat */
#define CRS_NLOOPMODE 5u
#define CRS_KEY_PLAY 0x20u
#define CRS_LOCK_KEEP 0x40u
#define CRS_LOCK_PUSH 0x80u
#define CRS_LOOP_MODE(s) ((unsigned)(s)->loop_rec & 15u)
#define CRS_LOOP_KEYS(s) (((unsigned)(s)->loop_rec >> 4) & 1u)
#define CRS_KEY_KEYS(s) (((unsigned)(s)->loop_rec & CRS_KEY_PLAY) ? 1u : 0u)
#define CRS_LOOP_REC(mode, keys) ((uint8_t)(((mode) & 15u) | ((keys) & 1u) << 4))

/* The chord layouts (docs/CHORD-LAYOUT.md, 0.24): three Options in the spare bits of `sticky` (bit 0 stays LATCH, the
 * manual one): bit 1 Options > Chord Keys FM-1 (0 Classic), bit 2 Full K Shorts On, bits 3 .. 6 the SUS Window as
 * (ms - 30) / 5, 0 .. 10 (30 .. 80 ms; 0 = 30 ms, the owner's default). In 0.24 / 0.25 zero was every default: no
 * version, no migration (0.25.1: FM-1 is the default, bit 1 set; the move of older records: CRS_KEYS_MOVED below). Every older firmware checks the byte with CRS_FIX(sticky, 0, 1): a byte with a new bit set
 * reads as its default (LATCH off), nothing else touched, so it ignores the layout and starts in Classic.
 * MIDI Keys rides in `raw_sound` the same way: bit 0 stays Raw Chord Sound, bits 1 .. 5 the channel (0 Off, 1 .. 16);
 * an older firmware reads a byte with a channel as Raw Chord Sound Off. Both bytes are the engine's too
 * (cr_settings_apply / capture): it takes bit 0 alone */
#define CRS_LAYOUT_FM1 0x02u
#define CRS_FULL_SHORT 0x04u
#define CRS_STICKY_AUTO 0x7Eu                /* bits 1 .. 6: Options (automatic); bit 0, LATCH, manual */
#define CRS_LATCH(s) ((unsigned)(s)->sticky & 1u)
#define CRS_LAYOUT(s) (((unsigned)(s)->sticky & CRS_LAYOUT_FM1) ? 1u : 0u)
#define CRS_FULLSHORT(s) (((unsigned)(s)->sticky & CRS_FULL_SHORT) ? 1u : 0u)
#define CRS_SUSWIN_CODE(s) ((int)(((unsigned)(s)->sticky >> 3) & 15u))
#define CRS_SUSWIN_MS(s) ((unsigned)(30 + 5 * CRS_SUSWIN_CODE(s)))
#define CRS_STICKY_BYTE(latch, layout, fshort, ms) ((uint8_t)(((latch) ? 1u : 0u) | ((layout) ? CRS_LAYOUT_FM1 : 0u) | \
    ((fshort) ? CRS_FULL_SHORT : 0u) | (((unsigned)(((int)(ms) - 30) / 5) & 15u) << 3)))
/* 0.25.1: FM-1 is the default Chord Keys (the owner, 2026-10-16: "the new FM-1 chord key layout is significantly better
 * on this device than the classic, let's make it the default"; "Units from old should get put on the new chord key
 * layout"). A record with no bit 1 in `leds` (every writer before 0.25.1: the byte is LEDs Glow 0 / Stock 1) has not been
 * moved yet: cr_settings_import sets FM-1 in it and the mark, once; a record with the mark keeps the layout it holds
 * (a player's Classic, chosen after the move). No version: a version bump would make every 0.18 .. 0.25 firmware, and
 * this one on its first start, read the block as migrated and so drop the session unless the object is still the one
 * it was saved beside (css_read). An older firmware checks `leds` with CRS_FIX(leds, 0, 1): with the mark it reads
 * LEDs Glow (Stock is lost until set again there) and reads the layout bit as it is (0.24, 0.25); its next write of
 * the block has no mark, so this firmware moves that record to FM-1 again (it comes from an older firmware). The
 * SMK-37 (Chord Keys hidden, Classic) neither sets the mark nor moves a record: its block is 0.25's */
#define CRS_LEDS_STOCK 0x01u
#define CRS_KEYS_MOVED 0x02u
#define CRS_LEDS(s) ((unsigned)(s)->leds & CRS_LEDS_STOCK)
#define CRS_RAW(s) ((unsigned)(s)->raw_sound & 1u)
#define CRS_MIDI_KEYS(s) (((unsigned)(s)->raw_sound >> 1) & 31u)
#define CRS_RAW_BYTE(raw, keys) ((uint8_t)(((raw) ? 1u : 0u) | (((keys) & 31u) << 1)))  /* usb_level: USB audio follows MASTER / takes the full level (docs/USB-AUDIO.md) */

/* Options > Output (0.19, docs/AUDIO-QUALITY.md) rides in bit 1 of pick_roots (bit 0: the retired picker option): set =
 * Classic (0.18's master), clear = Clean, so every older record reads as the default and no version is needed. A
 * firmware up to 0.18 finds 2 or 3 there out of range and takes its own default for the byte, nothing else */
#define CRS_OUT_CLASSIC 0x02u
#define CRS_OUT_CLEAN(s) (((unsigned)(s)->pick_roots & CRS_OUT_CLASSIC) ? 0u : 1u)
/* Options > USB Rate (0.19, the owner 2026-10-12: "Make 48 kHz opt-in for now"; docs/USB-AUDIO.md "48 kHz") rides in
 * bit 2 of the same byte: set = ChoralRoot In offers 44.1 and 48 kHz, clear = 44.1 kHz only (0.18's device, the
 * default). The same reasoning as Output's bit: every older record has 0 there and 0 is the default, so there is no
 * version and nothing to migrate; a firmware up to 0.18 finds 4..7 out of range and takes its own default for the
 * byte, a 0.19 build of before the option finds them out of range too and reads Output as Clean until it is set */
#define CRS_USB_48K 0x04u
#define CRS_USB48(s) (((unsigned)(s)->pick_roots & CRS_USB_48K) ? 1u : 0u)
/* Options > Chord Names (0.20; docs/CHORD-NAMES.md) rides in bit 3 of the same byte: set = Standard (lead-sheet
 * names), clear = Orchid (the ORC-1's naming, the default: the owner, "I would prefer we match the ORC-1 by
 * default"). The same reasoning once more: 0 in every older record and 0 is the default, so no version and nothing to
 * migrate; a firmware up to 0.19 finds 8..15 out of range and takes its own default for the byte (Output Clean, USB
 * Rate 44.1 kHz) until those are set there */
#define CRS_NAMES_STD 0x08u
#define CRS_NAMES(s) (((unsigned)(s)->pick_roots & CRS_NAMES_STD) ? 1u : 0u)
/* Options > 6th with 7th (0.20; docs/CHORD-NAMES.md "The 6th above a seventh") rides in bit 4 of the same byte: set =
 * Octave up (a chord with the 6 and a seventh plays its 6th an octave higher, a 13th), clear = In place (the Orchid's,
 * the default: the owner, "Let's add as option you can turn on, defaulted off"). Once more: 0 in every older record
 * and 0 is the default, so no version and nothing to migrate. A firmware up to this option (0.19: range 0..7; a 0.20
 * build of before it: 0..15) finds 16..31 out of range and takes its own default for the WHOLE byte: with this bit
 * set it reads Output Clean, USB Rate 44.1 kHz and Chord Names Orchid until they are set there (and its next save
 * writes that); with it clear the byte is in its range and is read as before */
#define CRS_SIXTH_UP 0x10u
#define CRS_SIXTH(s) (((unsigned)(s)->pick_roots & CRS_SIXTH_UP) ? 1u : 0u)
/* Options > Velocity Curve (0.22; docs/VELOCITY.md "fm1-ctl") rides in bits 5..7 of the same byte: the curve -3 .. +3
 * as three bits of two's complement (0 = 0, the default: every older record's, no version; 5 6 7 = -3 -2 -1; 4 is
 * not a value and reads as -3). A firmware up to 3y93 finds 32..255 out of range and takes its default for the byte */
#define CRS_VCURVE_BITS 0xE0u
#define CRS_VCURVE(s) ((int)(((unsigned)(s)->pick_roots >> 5) & 7u) >= 4 ? (int)(((unsigned)(s)->pick_roots >> 5) & 7u) - 8 : (int)(((unsigned)(s)->pick_roots >> 5) & 7u))
#define CRS_VCURVE_OF(k) ((uint8_t)(((unsigned)(k) & 7u) << 5))
#define CRS_PICK_AUTO (CRS_OUT_CLASSIC | CRS_USB_48K | CRS_NAMES_STD | CRS_SIXTH_UP | CRS_VCURVE_BITS)   /* the byte's automatic bits (cr_settings_auto) */

typedef struct {
    /* header (12 bytes) */
    uint32_t magic;                     /* CRS_MAGIC */
    uint16_t version;                   /* CRS_VERSION of the writer */
    uint16_t size;                      /* CRS_SIZE of the writer */
    uint32_t check;                     /* FNV-1a over bytes [12, size) */
    /* version 1 */
    uint8_t playstyle, extadd, secret, key_on;       /* cr_playstyle_t, cr_extadd_t, cr_secret_t, Key Mode (bit 0;
                                                      * v14: bit 1 Key plays Notes, bit 2 the Full keyboard; 0.22:
                                                      * bits 3 .. 4 Snap, CRS_SNAP) */
    uint8_t tonic, scale;                            /* 0..11, cr_scale_t (bits 0..6; bit 7: Numerals' Solfege,
                                                      * CRS_SOLFEGE, after 0.22) */
    int8_t transpose;                                /* -24..24 */
    uint8_t single;                                  /* cr_single_t: Full Octave / Split Oct / (v14) 2 Split Bass */
    uint8_t split_pc, vel, bass_on, bass_mode;       /* 0..11; key velocity 1..127; (bit 0 always written 0: a firmware
                                                      * older than v15 turns the engine's bass on from it with its own
                                                      * UI off; Bass on is the session's since v15); cr_bassmode_t */
    int8_t bass_voicing;                             /* bass register, octaves -2..4 */
    uint8_t perform_on, perform_mode, perf_sel;      /* on, cr_pmode_t, the PERF layer's entry 0..6 (Strum 2 ...) */
    uint8_t sticky, pool_pos10_chord;                /* latch; v6: pool_pos of engine rank 10 for the chord part
                                                      * (crs_pool_get; was rsv0) */
    uint16_t bpm;                                    /* 20..300 */
    int16_t par[CRS_NPM][CRS_NPAR];                  /* per-mode perform parameters (cr_engine.h cr_param_t) */
    uint8_t loop_sync, loop_quant, loop_count_in, loop_level;   /* 0..5 (Free, 1..16 bars), 0..6, 0/1, 0..100 */
    uint8_t midi_en[3], midi_ch[3];                  /* per stream (MAIN BASS RAW): MIDI out on, channel 0..15 */
    uint8_t clock_mode, raw_sound, view, motion;     /* CRS_CLOCK_*, RAW also plays part 0, View 0..5 (bits 0..2; v9:
                                                      * bit 7 the view lock, CRS_VIEW / CRS_VIEW_LOCK; v12: bit 6
                                                      * Dual Core On, CRS_DUALCORE), Motion 0..2 */
    uint8_t palette, leds, fx_on, pool_pos10_bass;   /* palette index (CRS_PALETTE_MOD), LEDs Glow / Stock (bit 0; 0.25.1:
                                                      * bit 1 the record moved to FM-1, CRS_KEYS_MOVED), FX on; v6: the
                                                      * bass part's pool_pos of engine rank 10 (was rsv1) */
    uint16_t chord_sound, bass_sound;                /* v6: the chord part's sound and the bass part's (what BASS tap
                                                      * brings) as (engine << 8) | pool position (docs/PRESETS.md);
                                                      * CRS_SOUND_DEFAULT the UI's. v1..5: positions in the old bank
                                                      * list, read as CRS_SOUND_DEFAULT */
    /* version 2 */
    uint8_t metro_on, metro_sig, metro_vol, loop_slot;   /* the click, 4/4 3/4 6/8, its level 0..100, slot 0..9 */
    /* version 3 */
    uint8_t pick_roots;                              /* RESERVED since 0.18 (the engine picker's white roots: 1 engines, 0
                                                      * they play: they always play now; written 0, read and ignored) */
    /* version 4. loop_rec (v8): the looper's record mode and PLAY-menu keys, CRS_LOOP_MODE / CRS_LOOP_KEYS (the last
     * free byte: before v8 it was rsv_usb, v4's usb_out (Options > USB Audio Out, the playback removed in v5: 0
     * since; a 0.14 dev build's version-6 record kept FM TONE's chord place there, read once by the version-7
     * migration, then 0) */
    uint8_t loop_rec, usb_in, usb_level;             /* (above; v13: bit 5 the KEY layer's keys, CRS_KEY_KEYS), Options >
                                                      * USB Record (on), USB Level (CRS_USB_*) */
    /* version 6 */
    uint8_t pool_pos[2][10];                         /* per part (chord, bass), per engine slot 0..9 (slot 10's:
                                                      * pool_pos10_*): the pool position last played there, where
                                                      * OPT + PRESETS lands (CRS_POOL_DEFAULT before). The slots are
                                                      * 0.13's display order, fixed: 0 FM TONE (ANALOG's until version
                                                      * 7), 1 FM6, 2 VA, 3 PHASE, 4 CZ-1, 5 LOFI, 6 VOICE, 7 TRIO,
                                                      * 8 WHEEL, 9 PHYS, 10 NOISE (cr_settings.c crs_slot). The reserve
                                                      * is used up: a new field needs a longer record (docs/SETTINGS.md) */
} cr_settings_t;

/* what cr_out.c's routing takes (cr_route_t, written field by field by the unit's glue) */
typedef struct {
    uint8_t part[3];                    /* 0 = CHORD part, CRS_NONE: none (RAW only: Raw Chord Sound) */
    uint8_t midi_en[3], ch[3];
    uint8_t clock_out;
    uint8_t clock_in;                   /* Options > MIDI Clock = In (clock_out is then 0) */
} cr_settings_out_t;

void cr_settings_defaults(cr_settings_t *s);
/* Options > Reset Settings: the record back to cr_settings_defaults (the one place the defaults come from: a unit
 * with no record, SAFE MODE and this), keeping the one field that is the looper's place and not an option: loop_slot
 * (the loop in RAM belongs to it; the loops are not the reset's). Sealed */
void cr_settings_factory(cr_settings_t *s);
/* a stored block (n bytes) -> *s, defaults for what is absent or invalid. 1: current, 2: migrated (older or newer
 * version: the fields it has are kept), 0: invalid (all defaults) */
int  cr_settings_import(cr_settings_t *s, const void *blk, uint32_t n);
void cr_settings_seal(cr_settings_t *s);         /* header + checksum, before it is stored */
int  cr_settings_equal(const cr_settings_t *a, const cr_settings_t *b);   /* the payloads */
/* Saving is manual since 0.18 (the owner, 2026-10-11; docs/SETTINGS.md "Manual and automatic"): the block holds two
 * kinds of field. AUTOMATIC: what the Options menu alone sets (Play Style, Extension Addition, Secret Chords, Velocity,
 * the MIDI channels, Raw Chord Sound, MIDI Clock, Motion, Chord Names, 6th with 7th, LEDs, USB Record, USB Level, USB Rate, Dual Core, Output; the palette, which
 * has no control), written by itself when the unit is quiet. MANUAL: everything else, how the instrument is set to
 * play, written only by the global save (SAVE held). cr_settings_auto copies the automatic fields of *src into *dst
 * and leaves dst's manual ones (not sealed): flash's block with the Options as they are now is what an automatic
 * write stores, and what a revert (GLO + SAVE) applies */
void cr_settings_auto(cr_settings_t *dst, const cr_settings_t *src);
/* the pool position of part (0 chord, 1 bass) on the engine of pool_pos slot r (0..CRS_NENG-1) */
static inline uint8_t crs_pool_get(const cr_settings_t *s, unsigned part, unsigned r)
{
    part &= 1u;
    if (r < 10u)
        return s->pool_pos[part][r];
    return part ? s->pool_pos10_bass : s->pool_pos10_chord;
}
static inline void crs_pool_set(cr_settings_t *s, unsigned part, unsigned r, uint8_t v)
{
    part &= 1u;
    if (r < 10u)
        s->pool_pos[part][r] = v;
    else if (part)
        s->pool_pos10_bass = v;
    else
        s->pool_pos10_chord = v;
}

/* ------------------------------------------------------------------ the session (v15, 0.18) --- */
/* "Switch off, switch on: exactly where you left off" (the owner, 2026-10-11): what the unit was doing that is not
 * an option of the 192-byte block above: both parts' sounds as they are (the edit buffers: every parameter, the deep
 * patch, the name and the pool entry, edited or not; the part's Level and Pan are among the parameters), the voicing,
 * the octave, Bass on, LOCK and its latched chord keys, the FX layer in full. One record, cr_session_t, in a flash
 * object of its own (storage.c OBJ_SESSION, the A/B pair 0xB4000 / 0xB5000), NOT inside persist_t: a firmware older
 * than v15 takes a settings object of another size for no record at all (settings_persist.c settings_import, its
 * panel calibration with it) and the tools know the block as 192 bytes (web/fm1backup.js, tools/fm1_install.py
 * CR_BLOCK), so the block keeps its size and its layout and an older firmware reads a v15 block as it read v14's.
 * The record has its own header (magic, version, size, check: FNV-1a over [12, size)), the block's import rules (a
 * shorter, older record over the defaults; a longer, newer one by its prefix; anything else: the defaults = the unit as
 * 0.17 powered on) and real reserve: CSS_SIZE is 1024 of the object's 3840 bytes, 119 of them unused today (43 in the
 * state, 8 in each part, 60 at the end; the SMK-37's MASTER took two of the state's, its Pedal one).
 * `bind`: the CRC of the settings object in flash when the session was written (0: none). The session is used at
 * power-on if the settings block was read as current (written by a v15 firmware: an older one in between would have
 * left its own version there) or, with any other block, if the settings object is still the one the session was
 * written beside: a session left behind by a downgrade, a restore or another firmware's erase is not applied over
 * settings it does not belong to */
#define CSS_MAGIC 0x31535343u           /* "CSS1" */
#define CSS_VERSION 1u
#define CSS_SIZE 1024u
#define CSS_HDR 12u
#define CSS_NP 96                       /* a part's parameters (core.h P_COUNT, 91 today: cr_settings.c checks) */
#define CSS_BLOB 160                    /* .. its deep patch (core.h ENG_BLOB_MAX) */
#define CSS_NAME 13                     /* .. its name (cr_ui.c cu_snd_t) */
/* the FX sub-block, versioned on its own so that the effects can change their parameters without a new record: an
 * effect and a parameter are stored under a TAG, never by position. A reader skips a tag it does not know, keeps its
 * default for one it does not find or whose value is out of its range, and takes the defaults for the whole block
 * when `version` is not its CSS_FX_VERSION (bump it only if this struct's layout changes). A parameter whose meaning
 * or range changes gets a NEW tag (the old one retired, never reused); room for 24 parameters and 8 effects */
#define CSS_FX_VERSION 1u
#define CSS_NFXP 29                     /* (29 used; 24 until 0.22's master filter: its four tags, 31..34, are
                                         * written only when they are not at their defaults, so a record of a
                                         * unit that never touched the filter keeps 23 and a firmware before
                                         * 0.22 reads it as before; one with more than 24 entries makes such a
                                         * firmware take this block's defaults: cr_session_import. 28 until the
                                         * routing's (issue 21): its two tags, 35 and 36, are written only off their defaults
                                         * too, so a record holds 29 only when Order AND Mode AND all four of the
                                         * filter's are off their defaults; a firmware of 0.22 .. 0.26 (28 entries)
                                         * reads a record of 28 or fewer as before, skipping 35 / 36 as unknown
                                         * tags (the chain stays parallel), and one of 29 as another layout: its
                                         * css_sanitize takes this block's defaults, the rest of the session kept) */
#define CSS_NFXE 8
#define CSS_FX_NONE 0xFFu               /* amt: not stored (the default send) */
enum { CSS_FXE_REVERB, CSS_FXE_CHORUS, CSS_FXE_DELAY, CSS_FXE_DRIVE,              /* effect tags (amt's index, sel) */
       CSS_FXE_REVERB2, CSS_FXE_DELAY2,                                            /* .. sel only (0.18): the effect's
                                                                                    * second page in the FX layer */
       CSS_FXE_SENDS,                                                              /* .. sel only: the Sends page (a
                                                                                    * record without it: sel is the
                                                                                    * effect's page it was left on) */
       CSS_FXE_FILTER,                                                             /* .. sel only (0.22): the Filter page */
       CSS_FXE_ROUTE };                                                            /* .. sel only (issue 21): the Routing page.
                                          * 8 is past amt[] (CSS_NFXE): a sel, never an amt index (only CSS_FXE[]'s
                                          * four effect tags index amt). A firmware before it does not know 8: its
                                          * css_apply finds no page for it and the FX layer opens where it would */
/* parameter tags (1..255). 1..3 and 6..8 were Felucca's ROOM / SPRING reverb and its mono delay (0.17 and the first
 * 0.18 builds: size, damp, type; time, feedback, colour): RETIRED with them, never reused; a record that holds them
 * loses them at its next save. 9..24: the stereo delay's and the large reverb's (cr_fx.c XP_*, docs/FX.md) */
enum { CSS_FXP_NONE, CSS_FXP_RSIZE, CSS_FXP_RDAMP, CSS_FXP_RTYPE, CSS_FXP_CRATE, CSS_FXP_CDEPTH, CSS_FXP_DTIME,
       CSS_FXP_DFDBK, CSS_FXP_DCOLOR,
       CSS_FXP_XD_TIME, CSS_FXP_XD_DIV, CSS_FXP_XD_SPREAD, CSS_FXP_XD_RATIO, CSS_FXP_XD_FDBK, CSS_FXP_XD_SYNC,
       CSS_FXP_XD_MODE, CSS_FXP_XD_TONE, CSS_FXP_XD_WOW,                           /* 9..17: the delay */
       CSS_FXP_XR_SIZE, CSS_FXP_XR_DAMP, CSS_FXP_XR_DECAY, CSS_FXP_XR_PRE, CSS_FXP_XR_MOD, CSS_FXP_XR_LOW,
       CSS_FXP_XR_WIDTH,                                                           /* 18..24: the reverb */
       CSS_FXP_XD_TIME2,                 /* 25: the delay's free time, 16 .. 740 ms (the delay at the full rate: 9's
                                          * 21 .. 1000 ms is retired with the half-rate line) */
       CSS_FXP_BS_REV, CSS_FXP_BS_CHO, CSS_FXP_BS_DLY, CSS_FXP_BS_DRIVE,           /* 26..29: the BASS part's sends (the
                                          * chord part's are amt[]): the sends are the parts', not the sounds' */
       CSS_FXP_XC_WIDTH,                 /* 30 (0.21): the chorus's Width (cr_fx.c XP_CWIDTH). A record without it:
                                          * 127, the chorus of 0.19 and 0.20 */
       CSS_FXP_XQ_LOW, CSS_FXP_XQ_HIGH, CSS_FXP_XQ_SLOPE, CSS_FXP_XQ_ON,           /* 31..34 (0.22): the master filter's
                                          * Low cut, High cut, Slope, Filter (cr_fx.c XP_FLOW ..): stored only off
                                          * their defaults (above) */
       CSS_FXP_XF_ORDER, CSS_FXP_XF_RMODE };                                       /* 35, 36 (GitHub issue 21):
                                          * the routing's Order 0..23 and Mode 0 Parallel / 1 Chain (cr_fx.c
                                          * XP_ORDER, XP_RMODE; the second design): stored only off their defaults
                                          * (above). 36 was the first design's Feed, and 35 its order of three:
                                          * neither shipped in a release (one dev unit, b1e1222); a value out of
                                          * range takes the default (css_fxg_ok), so an old Feed > 1 is Parallel */
typedef struct { uint8_t tag, rsv; int16_t v; } css_fxp_t;
typedef struct {
    uint8_t version, n, sel, on_rsv;    /* CSS_FX_VERSION, entries of par[] used, the FX layer's picked effect (its
                                         * tag), 0 (FX on / off is the block's fx_on) */
    uint8_t amt[CSS_NFXE];              /* per effect tag: the chord part's send 0..127 (the "Send" cell), CSS_FX_NONE */
    css_fxp_t par[CSS_NFXP];            /* the effects' own (global) parameters */
} css_fx_t;                             /* 128 bytes (124 until the routing: par[] took rsv_fx's four bytes for one more
                                         * entry; 108 until 0.22: par[] grew by four entries into rsv_fx, the
                                         * entries before them where they were) */
typedef struct {
    uint8_t valid;                      /* 1: a sound is stored */
    uint8_t eng, preset, user;          /* core.h track_t eng_req, preset, user (the slot it is as stored, + 1) */
    int8_t trim;
    uint8_t edited, kind, fk, slot;     /* cr_ui.c cu_snd_t: edited since its load; its pool entry */
    uint8_t np, nblob;                  /* the writer's P_COUNT and the deep patch's size (0: none): a reader with
                                         * another count, or whose engine has another blob, keeps the pool's sound */
    char name[CSS_NAME];
    int16_t p[CSS_NP];
    uint8_t blob[CSS_BLOB];
    uint8_t rsv[8];
} css_part_t;                           /* 384 bytes */
typedef struct {
    uint32_t magic;                     /* CSS_MAGIC */
    uint16_t version, size;             /* CSS_VERSION, CSS_SIZE of the writer */
    uint32_t check;                     /* FNV-1a over bytes [12, size) */
    uint32_t bind;                      /* (above; not part of the change detection: cr_session_hash) */
    int8_t voicing, octave;             /* KNOB 1 -12..12; OCT -2..2 */
    uint8_t bass_on;                    /* 0/1 (v15: the bass comes back on; until 0.17 it was off at every power-on) */
    uint8_t lock, latched;              /* LOCK on; the chord keys it latched (bit cr_mod_t) */
    int8_t loop_rate;                   /* the loop's Rate (the PLAY menu's KNOB 2), steps from x1: -7 x1/16, -6 x1/12,
                                         * -5 x1/8, -4 x1/6 (those four since 2026-10-11), -3 x1/4, -2 x1/2,
                                         * -1 x3/4, 0 x1, 1 x1.5, 2 x2, 3 x4 (cr_loop.h CRL_NRATE; 0 = the default, so
                                         * a record written before it reads x1: no migration) */
    uint8_t smk_master[2];              /* the SMK-37's only (GitHub issue 30): K1's virtual MASTER pot (hal/smk_input.h
                                         * smk_master, 0..1023) + 1, little-endian; 0 = not stored (an older record, a
                                         * 0.26-smk37 one: 724 as before). The FM-1 build neither writes nor reads it
                                         * (its MASTER is a physical pot): 0 in its records. Bytes 22, 23 (the first
                                         * two of the old reserve) */
    uint8_t smk_pedal;                  /* the SMK-37's only: Options > Pedal, what its pedal jack does: 0 Sustain (the
                                         * default: an older record, a record of the FM-1 build), 1 Expression. The
                                         * FM-1 build neither writes nor reads it. Byte 24 (the old reserve's third) */
    uint8_t rsv[43];
    css_fx_t fx;                        /* (rsv_fx[4] after it until the routing (issue 21): its entry took it) */
    css_part_t part[2];                 /* chord, bass */
    uint8_t rsv_end[60];
} cr_session_t;

void cr_session_defaults(cr_session_t *x);         /* nothing stored: the unit as it powers on without a session */
/* a stored record (n bytes) -> *x; 1 current, 2 migrated (older / newer), 0 invalid (the defaults). blk may be x */
int  cr_session_import(cr_session_t *x, const void *blk, uint32_t n);
void cr_session_seal(cr_session_t *x);
uint32_t cr_session_hash(const cr_session_t *x);   /* the state alone (bytes [16, CSS_SIZE)): change detection */
/* the FX block's entries by tag: 1 found (*v), 0 not stored; set: 0 when the block is full */
int  cr_session_fx_get(const cr_session_t *x, unsigned tag, int *v);
int  cr_session_fx_set(cr_session_t *x, unsigned tag, int v);
/* a part record's trim when it is put back (0.19). track_t.trim is the SOUND's, from the running firmware's table
 * (cr_bank.c CB_TRIM), never the record's: 0.19 took CZ-1's three rows out, and a session saved on BRASS 1, STRINGS 2
 * or PIPE ORGAN 1 brought their -5 / -3 / -6 dB back. The rule is cr_ui.c cu_pool_load's, so the session gives what
 * picking the sound by hand gives: a factory preset its row, a user preset bound to one (an overwritten factory
 * preset) that preset's row, an added user preset and INIT none; edited or not (an edit never moves the trim).
 * row(engine, factory preset) is the table (0: no row). The record's own `trim` is still written (what the part had:
 * an older firmware reads it) and no longer read */
enum { CSS_KIND_INIT, CSS_KIND_FACTORY, CSS_KIND_USER };   /* css_part_t.kind (cr_bank.c PK_*) */
#define CSS_FK_NONE 0xFFu                          /* css_part_t.fk: no factory preset (cr_bank.c PF_NONE) */
int  cr_session_part_trim(const css_part_t *r, int (*row)(unsigned engine, unsigned fk));

#ifdef CR_ENGINE_H
/* the record -> the engine's setters (call with the engine idle / the audio IRQ off) and the routing */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o);
/* the reverse: the engine's and the routing's fields of *s (the UI's own fields are left as they are) */
void cr_settings_capture(cr_settings_t *s, const cr_t *c, const cr_settings_out_t *o);
#endif

#endif
