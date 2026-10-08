/* SPDX-License-Identifier: GPL-3.0-only */
/* The emulator's own pieces (no firmware here): the keyboard map, CoreMIDI, image files. */
#pragma once
#include <stdint.h>
#include <SDL.h>

/* ---- keymap.c ---- */
enum {
    KM_KEY,        /* idx: firmware note key 0..26 (MIDI note - 53) */
    KM_BTN,        /* idx: button label (EMU_B_*) */
    KM_OCTBOTH,    /* OCT- and OCT+ together */
    KM_SELECT,     /* idx: knob (EMU_E_*): the one Up / Down turn */
    KM_CYCLE,      /* idx: +1 / -1: select the next / previous of SELECT KNOB1..KNOB4 (wraps) */
    KM_TURN,       /* idx: +1 clockwise, -1 counter-clockwise, the selected knob (repeats while held) */
    KM_SHOT,       /* LCD screenshot */
    KM_RECORD,     /* toggle recording every LCD frame */
    KM_DUMP,       /* print the input state */
    KM_LCDVIEW,    /* show / hide the big LCD view above the panel (window only) */
};
typedef struct {
    SDL_Scancode sc;
    uint8_t kind;
    int8_t idx;
    const char *cap;       /* the key's name as printed on the panel hint and in --help */
} keymap_t;
extern const keymap_t KEYMAP[];
extern const int KEYMAP_N;
void keymap_help(void);
void keymap_check(void);
const keymap_t *keymap_find(SDL_Scancode sc);
const char *keymap_hint(uint8_t kind, int idx);   /* the computer key of a control, or NULL */

/* names of the panel's controls (shared by the drawing, --help and --script) */
extern const char *const EMU_BTN_NAME[];          /* EMU_NB labels */
extern const char *const EMU_ENC_NAME[];          /* EMU_NE names */
void emu_note_name(int key, char *out);           /* firmware key 0..26 -> "F3".."G5" */

/* ---- emu_midi.c (CoreMIDI) ---- */
int emu_midi_open(int log);                       /* virtual source + destination; 0 = ok */
void emu_midi_close(void);
void emu_midi_send(uint32_t pkt);                 /* one USB-MIDI event packet to the virtual source */
int emu_midi_take(uint32_t *pkt);                 /* one USB-MIDI event packet from a host, 0 = none */
void emu_midi_unget(uint32_t pkt);                /* put back the packet just taken (the firmware had no room) */

/* ---- emu_img.c ---- */
int emu_write_png(const char *path, const uint16_t *lcd_be565, int w, int h);
int emu_write_png_argb(const char *path, const uint32_t *argb, int w, int h);
int emu_write_ppm(const char *path, const uint16_t *lcd_be565, int w, int h);
