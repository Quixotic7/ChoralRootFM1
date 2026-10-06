/* SPDX-License-Identifier: GPL-3.0-only
 * ChoralRoot FM-1: the chord / performance engine (port of choralroot's d_cr_engine.lua).
 * Portable C99, integer only, no allocation, no libc (stdint.h only). Everything lives in one cr_t the
 * caller allocates; two instances can coexist. See CR_ENGINE.md for the timing model and the tables. */
#ifndef CR_ENGINE_H
#define CR_ENGINE_H

#include <stdint.h>

/* ------------------------------------------------------------- sizes --- */
#ifndef CR_MAX_VOICES
#define CR_MAX_VOICES 16         /* simultaneously held / latched root voices */
#endif
#define CR_MAX_PADS 8            /* chord memory pads (design.md 10.7) */
#ifndef CR_MAX_EV
#define CR_MAX_EV 128            /* pending scheduler events (pooled, no allocation) */
#endif
#ifndef CR_MAX_LOOPV
#define CR_MAX_LOOPV 8           /* loop voices: chords a looper plays back (cr_loop_event) */
#endif
#define CR_NVOICE (CR_MAX_VOICES + CR_MAX_PADS + CR_MAX_LOOPV)
#define CR_CHORD_MAX 8           /* notes in one chord: 3 + 4 extensions = 7 */
#define CR_OWN_MAX 32            /* notes one performance owner may hold (7 notes x 4 octaves) */
#define CR_SEQ_MAX 56            /* arp sequence: up/down over 28 notes */
#define CR_NPATTERN 12
#define CR_SUP_MAX 12            /* chord-name superscript buffer ("add4 7 6", "5 JAZZ") */

/* ----------------------------------------------------------- streams --- */
typedef enum { CR_STREAM_MAIN = 0, CR_STREAM_BASS = 1, CR_STREAM_RAW = 2 } cr_stream_t;  /* Orchid: performance / bass / raw chord */
#define CR_NSTREAM 3

typedef struct {
    void (*note_on)(void *ud, cr_stream_t s, uint8_t note, uint8_t vel);
    void (*note_off)(void *ud, cr_stream_t s, uint8_t note);
    void (*all_off)(void *ud, cr_stream_t s);     /* panic: CC 123 on the stream's channel */
    void *ud;
    /* optional (a looper's capture): a played chord gesture began (on = 1) or ended (on = 0). serial names the
     * gesture; root is the absolute resolved root (transpose and Key Mode applied), q / ext the resolved quality
     * and extension mask, as they are at that moment (Advanced / Free: the end carries the final state). Keyed
     * voices and pads only, never a loop voice. A same-root retrigger ends one gesture and begins another. */
    void (*gesture)(void *ud, uint32_t serial, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on);
} cr_out_t;

/* ---------------------------------------------------- the chord block --- */
typedef enum { CR_MOD_DIM, CR_MOD_MIN, CR_MOD_MAJ, CR_MOD_SUS, CR_MOD_6, CR_MOD_M7, CR_MOD_MAJ7, CR_MOD_9 } cr_mod_t;
#define CR_NMOD 8

/* chord qualities (ids 1-4 equal the KEYMAP quality codes; 5-7 are the Secret Chords) */
enum { CR_Q_NONE = 0, CR_Q_DIM, CR_Q_MIN, CR_Q_MAJ, CR_Q_SUS, CR_Q_AUG, CR_Q_POW, CR_Q_MIN4, CR_Q_COUNT };
/* extension mask bits (the Lua ext_mask() packing) */
enum { CR_EXT_6 = 1, CR_EXT_M7 = 2, CR_EXT_MAJ7 = 4, CR_EXT_9 = 8 };

/* ---------------------------------------------------------- settings --- */
typedef enum { CR_PS_SIMPLE, CR_PS_ADVANCED, CR_PS_FREE } cr_playstyle_t;
typedef enum { CR_EXTADD_ADD_NOTE, CR_EXTADD_PLAY_CHORD } cr_extadd_t;
typedef enum { CR_SECRET_OFF, CR_SECRET_SIMPLE, CR_SECRET_ALL } cr_secret_t;
typedef enum { CR_SCALE_MAJOR, CR_SCALE_MINOR } cr_scale_t;
typedef enum { CR_SINGLE_FULL, CR_SINGLE_SPLIT } cr_single_t;   /* Single Notes (design.md 8.3) */
typedef enum { CR_BASS_CHORDS_ONLY, CR_BASS_UNISON, CR_BASS_SINGLE_NOTES, CR_BASS_SOLO } cr_bassmode_t;
typedef enum { CR_PM_STRUM, CR_PM_SLOP, CR_PM_ARP, CR_PM_PATTERN, CR_PM_HARP } cr_pmode_t;
#define CR_PM_COUNT 5
typedef enum { CR_DIR_UP, CR_DIR_DOWN, CR_DIR_UPDOWN, CR_DIR_DOWNUP, CR_DIR_PLAYED, CR_DIR_RANDOM } cr_dir_t;
typedef enum {                   /* design.md 12.3, editor order x5..x16 */
    CR_DIV_2_1, CR_DIV_1_1, CR_DIV_1_2, CR_DIV_1_2T, CR_DIV_1_4, CR_DIV_1_4T,
    CR_DIV_1_8, CR_DIV_1_8T, CR_DIV_1_16, CR_DIV_1_16T, CR_DIV_1_32, CR_DIV_1_32T
} cr_div_t;
#define CR_DIV_COUNT 12
typedef enum {                   /* per-mode performance parameters (design.md 12) */
    CR_P_RATE,                   /* strum / slop / harp: 120-BPM reference interval, ms (1..1000) */
    CR_P_DIV,                    /* arp / pattern: cr_div_t */
    CR_P_DIR,                    /* cr_dir_t (strum/slop/harp: up, down, updown, random) */
    CR_P_RANGE,                  /* octaves 1..4 */
    CR_P_GATE,                   /* % 1..200 (arp / pattern / harp) */
    CR_P_SWING,                  /* % 50..90 (arp / pattern) */
    CR_P_RETRIG,                 /* 0 / 1 (arp / pattern) */
    CR_P_PATTERN,                /* 1..12 */
    CR_P_ROTATE,                 /* -12..12 */
    CR_P_AMOUNT,                 /* slop amount % 0..100 */
    CR_P_HOLD                    /* 0 / 1: latch while this mode performs (arp Hold, default on) */
} cr_param_t;
#define CR_P_COUNT 11

/* --------------------------------------------------------- internals --- */
typedef struct { uint8_t live, n, note[CR_OWN_MAX]; } cr_own_t;      /* main / performance owner */
typedef struct { uint8_t live, n, note[CR_CHORD_MAX]; } cr_rown_t;   /* raw-chord owner */
typedef struct { uint8_t live, n, note; } cr_bown_t;                 /* bass owner */

typedef struct {
    uint8_t on, mode, nseq;
    uint16_t rem;                /* anchor fraction, 1/bpm ms */
    uint32_t anchor;             /* ms; step k lands at anchor + pos(k) (beat-accumulated, no drift) */
    uint32_t step, pos;          /* timing step since anchor / melodic position */
    uint8_t seq[CR_SEQ_MAX];     /* arp order, or the range-expanded notes for pattern */
} cr_clk_t;

typedef struct {
    uint8_t used, pad;           /* pad: chord-pad slot + 1 (not a keyed voice) */
    uint8_t loop;                /* a loop voice (cr_loop_event): slot + 1 */
    uint8_t key, id, superseded, is_free;
    uint8_t vel, pc, pc_pressed, note_pressed;
    int8_t transpose;            /* captured at note-on */
    uint8_t sq, sx;              /* spec: quality (CR_Q_*) + extension mask, mutated by Advanced/Free */
    uint8_t rq;                  /* resolved quality (Key Mode / secret) */
    int16_t root;                /* resolved root note, untransposed */
    uint8_t nbase, base[CR_CHORD_MAX], nnotes, notes[CR_CHORD_MAX];
    uint16_t gate_t;             /* harp gate, ms (0 = sustain) */
    uint8_t last_note, has_last;
    uint32_t serial;
    uint32_t gid;                /* gesture id (out.gesture): new at note-on and at a same-root retrigger */
    cr_own_t main;
    cr_rown_t raw;
    cr_bown_t bass;
    cr_clk_t clk;
} cr_voice_t;

typedef struct { uint32_t due; uint8_t kind, owner, vi, note; } cr_ev_t;

typedef struct { uint8_t used, q, x, vel; int16_t root; } cr_chord_t;   /* a stored / last chord */

typedef struct {
    uint8_t valid, attached, key, id, vel, pc_pressed, note_pressed, sq, sx;
    uint8_t vi;
    uint32_t serial;
} cr_lastplayed_t;

typedef struct {
    /* settings (design.md 21) */
    uint8_t playstyle, extadd, secret;
    uint8_t key_on, tonic, scale;
    int8_t transpose;
    uint8_t single, split_pc;        /* Single Notes: Full Octave / Split; keys with pc < split_pc sound an octave lower */
    uint8_t bass_on, bass_mode;
    int8_t bass_voicing, voicing;
    uint8_t perform_on, perform_mode, sticky;
    uint16_t bpm;
    uint8_t stream_on[CR_NSTREAM];
    int16_t par[CR_PM_COUNT][CR_P_COUNT];
    /* runtime */
    cr_out_t out;
    uint32_t now, serial, rng, gid;
    uint8_t started;
    uint32_t clk_anchor, clk_units;  /* global transport anchor (ms) and 1/24-beat units rebased so far */
    uint16_t clk_rem;
    uint8_t reg[CR_NSTREAM][128];    /* note registry: refcount per (stream, note) (design.md 17) */
    uint8_t phys[128];               /* physically held root presses: bit id per note */
    uint8_t phys_n;
    uint8_t mods_down, qstack[CR_NMOD], qn;
    cr_voice_t v[CR_NVOICE];         /* [0, CR_MAX_VOICES): keyed voices; then one per pad */
    cr_ev_t ev[CR_MAX_EV];
    uint16_t nev;
    uint16_t ev_overflow;            /* events refused because the pool was full (diagnostic) */
    cr_lastplayed_t lp;              /* Free play style: the last real voice */
    cr_chord_t pads[CR_MAX_PADS];
    int8_t rec_slot;
    uint8_t rec_played;
    cr_chord_t last_chord;           /* the last played chord (pad recording) */
    uint8_t disp_vi;                 /* voice shown on screen (0xFF = none) */
    uint8_t disp_valid, disp_q, disp_x, disp_pc, disp_n, disp_notes[CR_CHORD_MAX];
} cr_t;

/* -------------------------------------------------------------- core --- */
void cr_init(cr_t *c, const cr_out_t *out);
void cr_seed(cr_t *c, uint32_t seed);              /* random order / slop jitter */
void cr_tick(cr_t *c, uint32_t now_ms);            /* run the scheduler up to now (every ~1-3 ms) */
void cr_key(cr_t *c, uint8_t midi_note, uint8_t vel, int down);
/* a root key with a press id 0..7 (grid: the velocity row): a second press on a held root retriggers it
 * at the new velocity and the earlier press's release is ignored (design.md 5.3). cr_key uses id 0. */
void cr_key_ex(cr_t *c, uint8_t midi_note, uint8_t id, uint8_t vel, int down);
void cr_mod(cr_t *c, cr_mod_t m, int down);        /* the chord block */
void cr_voicing_step(cr_t *c, int d);              /* KNOB 1: +1 / -1, clamp -12..12 */
void cr_bass_voicing_step(cr_t *c, int d);         /* octaves, clamp -2..4 */
void cr_panic(cr_t *c);

/* ---------------------------------------------------------- settings --- */
void cr_set_playstyle(cr_t *c, cr_playstyle_t p);
void cr_set_ext_addition(cr_t *c, cr_extadd_t e);
void cr_set_secret(cr_t *c, cr_secret_t s);
void cr_set_key(cr_t *c, int enabled, uint8_t tonic_pc, cr_scale_t scale);  /* new gestures only */
void cr_set_transpose(cr_t *c, int semis);         /* -24..24, new gestures only */
/* Single Notes (design.md 8.3): Split = a single note (no chord quality) whose pressed key's pitch class is below
 * split_pc (0..11, C = 0) sounds an octave lower; chords and the bass are unaffected. New gestures only. */
void cr_set_single_notes(cr_t *c, cr_single_t mode, int split_pc);
void cr_set_bass(cr_t *c, int enabled);
void cr_set_bass_mode(cr_t *c, cr_bassmode_t m);
void cr_set_perform(cr_t *c, int enabled);         /* restarts sounding voices, flushes a Hold latch */
void cr_set_perform_mode(cr_t *c, cr_pmode_t m);
void cr_set_param(cr_t *c, cr_pmode_t m, cr_param_t p, int value);   /* live: rephase / rebuild / restart */
int  cr_get_param(const cr_t *c, cr_pmode_t m, cr_param_t p);
void cr_set_sticky(cr_t *c, int on);               /* latch (Sticky Keys) */
void cr_set_tempo(cr_t *c, int bpm);               /* 20..300; rephases clocked voices */
void cr_set_stream(cr_t *c, cr_stream_t s, int enabled);   /* gates new gestures; owners finish */
void cr_clock_reset(cr_t *c);                      /* the global grid restarts now (transport start) */
uint32_t cr_clock_epoch(const cr_t *c);            /* a grid point of the global clock (ms) */
int  cr_latching(const cr_t *c);

/* --------------------------------------------------------- chord pads --- */
void cr_pad_arm(cr_t *c, int slot);                /* record the next chord into slot 0..7 */
int  cr_pad_armed(const cr_t *c);                  /* armed slot or -1 */
void cr_pad_down(cr_t *c, int slot);               /* play a stored chord (momentary) */
void cr_pad_up(cr_t *c, int slot);
void cr_pad_stop_all(cr_t *c);
int  cr_pad_get(const cr_t *c, int slot, int16_t *root, uint8_t *q, uint8_t *ext, uint8_t *vel);
void cr_pad_set(cr_t *c, int slot, int used, int16_t root, uint8_t q, uint8_t ext, uint8_t vel);

/* -------------------------------------------------------- loop voices --- */
/* A looper's playback (cr_loop.c): plays a resolved chord as loop voice lv (0..CR_MAX_LOOPV-1), bypassing the
 * chord-key state machine: root is absolute (no transpose, no Key Mode: the recorded harmony), q a CR_Q_* quality
 * (CR_Q_NONE: a single note), ext a CR_EXT_* mask. Voicing, performance and bass apply live, as to a pad. on = 0
 * ends it (an on for a busy lv replaces it). Returns 1 when it sounds. Loop voices never reach out.gesture. */
int  cr_loop_event(cr_t *c, int lv, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on);
int  cr_loop_busy(const cr_t *c, int lv);          /* loop voice lv is sounding (panic clears them all) */

/* ----------------------------------------------- queries: screen, LEDs --- */
typedef struct {
    uint8_t valid;               /* a chord has been played (since init / panic) */
    uint8_t sounding;            /* ... and it is still sounding */
    uint8_t root_pc;             /* sounding root pitch class (Key Mode resolved, transposed) */
    uint8_t quality;             /* CR_Q_*; CR_Q_NONE = a single note */
    uint8_t ext;                 /* CR_EXT_* */
    uint8_t secret;              /* quality is a Secret Chord type */
    uint8_t nnotes, notes[CR_CHORD_MAX];   /* voiced notes, low to high */
    char root[3], qual[5], sup[CR_SUP_MAX]; /* Orchid Standard Chord Naming: "C" "m" "7 9"; "C" "5"; "C" "m" "add4" */
} cr_chord_info_t;
void cr_chord_info(const cr_t *c, cr_chord_info_t *ci);
void cr_chord_name(uint8_t root_pc, uint8_t quality, uint8_t ext, char root[3], char qual[5], char sup[CR_SUP_MAX]);
void cr_note_name(uint8_t note, char buf[5]);     /* "C#4" (MIDI 60 = C4) */
uint16_t cr_scale_mask(const cr_t *c);             /* bit pc = in the scale; 0 when Key Mode is off */
uint8_t cr_mods_held(const cr_t *c);               /* bit m = physically held */
uint8_t cr_mods_active(const cr_t *c);             /* held, or latched in a held voice (Advanced / Free) */
int  cr_perform_pos(const cr_t *c, uint8_t *note); /* index into the shown chord's notes of the last
                                                    * performed note (-1: none); *note = its pitch */
int  cr_sounding(const cr_t *c, cr_stream_t s, uint8_t note);   /* registry refcount */
int  cr_voices(const cr_t *c);                     /* live voices (keyed + pads) */
int  cr_pending(const cr_t *c);                    /* queued events */
const char *cr_pattern_name(int pattern);          /* 1..12 */

/* chord helpers, shared with a looper (pure functions of the tables) */
int  cr_chord_base(int root_note, uint8_t quality, uint8_t ext, uint8_t out[CR_CHORD_MAX]);
int  cr_voice_apply(const uint8_t *base, int n, int k, uint8_t out[CR_CHORD_MAX]);

#endif
