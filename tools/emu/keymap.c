/* SPDX-License-Identifier: GPL-3.0-only */
/* ===========================================================================================
 * THE KEYBOARD MAP of the FM-1 emulator. Change it here; --help and the panel hints follow.
 *
 * Physical key positions (SDL scancodes: the US-layout names, whatever the system layout is).
 * Firmware note keys: index = MIDI note - 53 (0 = F3 .. 26 = G5).
 *
 *   root keys, white   A  S  D  F  G  H  J  K  L  ;  '   = D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (keys 9 ..)
 *   root keys, black   W  E  T  Y  I  O  [               = D#4 F#4 G#4 A#4 C#5 D#5 F#5
 *   chord block        F1 F2 F3 F4                       = F#3 G#3 A#3 C#4 (keys 1 3 5 8)
 *                      2  3  4  5                        = F3  G3  A3  C4  (keys 0 2 4 7); B3 (6) unmapped
 *   buttons            Z X C V B N                       = FX SEL ENV LFO EDIT GLO
 *                      , . / RightShift Return Backspace = HOME SAVE ARP SEQ PLAY REC
 *                      Left Right                        = OCT- OCT+;  Esc = OCT- + OCT+ together
 *   knobs              Q = MASTER (pot)  R = SELECT  U = PRESETS  P = ALGORITHM  6 7 8 9 = KNOB 1..4:
 *                      select the knob; Up / Down turn the selected one (one detent per press, repeats)
 *                      mouse: wheel over a knob turns it, click selects it, vertical drag turns it
 *   tools              F10 LCD screenshot (build/emu/shot_NNN.png)
 *                      F11 record every LCD frame (build/emu/rec/NNNN.ppm), again to stop
 *                      F12 print the fm1_in state
 * (On a Mac keyboard the F keys may need fn.)
 * =========================================================================================== */
#include <stdio.h>
#include "emu.h"
#include "emu_hooks.h"

const keymap_t KEYMAP[] = {
    /* root keys, white: D4 = key 9 upward */
    {SDL_SCANCODE_A, KM_KEY, 9, "A"},
    {SDL_SCANCODE_S, KM_KEY, 11, "S"},
    {SDL_SCANCODE_D, KM_KEY, 12, "D"},
    {SDL_SCANCODE_F, KM_KEY, 14, "F"},
    {SDL_SCANCODE_G, KM_KEY, 16, "G"},
    {SDL_SCANCODE_H, KM_KEY, 18, "H"},
    {SDL_SCANCODE_J, KM_KEY, 19, "J"},
    {SDL_SCANCODE_K, KM_KEY, 21, "K"},
    {SDL_SCANCODE_L, KM_KEY, 23, "L"},
    {SDL_SCANCODE_SEMICOLON, KM_KEY, 24, ";"},
    {SDL_SCANCODE_APOSTROPHE, KM_KEY, 26, "'"},
    /* root keys, black */
    {SDL_SCANCODE_W, KM_KEY, 10, "W"},
    {SDL_SCANCODE_E, KM_KEY, 13, "E"},
    {SDL_SCANCODE_T, KM_KEY, 15, "T"},
    {SDL_SCANCODE_Y, KM_KEY, 17, "Y"},
    {SDL_SCANCODE_I, KM_KEY, 20, "I"},
    {SDL_SCANCODE_O, KM_KEY, 22, "O"},
    {SDL_SCANCODE_LEFTBRACKET, KM_KEY, 25, "["},
    /* the chord block */
    {SDL_SCANCODE_F1, KM_KEY, 1, "F1"},
    {SDL_SCANCODE_F2, KM_KEY, 3, "F2"},
    {SDL_SCANCODE_F3, KM_KEY, 5, "F3"},
    {SDL_SCANCODE_F4, KM_KEY, 8, "F4"},
    {SDL_SCANCODE_2, KM_KEY, 0, "2"},
    {SDL_SCANCODE_3, KM_KEY, 2, "3"},
    {SDL_SCANCODE_4, KM_KEY, 4, "4"},
    {SDL_SCANCODE_5, KM_KEY, 7, "5"},
    /* buttons */
    {SDL_SCANCODE_Z, KM_BTN, EMU_B_FX, "Z"},
    {SDL_SCANCODE_X, KM_BTN, EMU_B_SEL, "X"},
    {SDL_SCANCODE_C, KM_BTN, EMU_B_ENV, "C"},
    {SDL_SCANCODE_V, KM_BTN, EMU_B_LFO, "V"},
    {SDL_SCANCODE_B, KM_BTN, EMU_B_EDIT, "B"},
    {SDL_SCANCODE_N, KM_BTN, EMU_B_GLO, "N"},
    {SDL_SCANCODE_COMMA, KM_BTN, EMU_B_HOME, ","},
    {SDL_SCANCODE_PERIOD, KM_BTN, EMU_B_SAVE, "."},
    {SDL_SCANCODE_SLASH, KM_BTN, EMU_B_ARP, "/"},
    {SDL_SCANCODE_RSHIFT, KM_BTN, EMU_B_SEQ, "RSHIFT"},
    {SDL_SCANCODE_RETURN, KM_BTN, EMU_B_PLAY, "RETURN"},
    {SDL_SCANCODE_BACKSPACE, KM_BTN, EMU_B_REC, "BKSP"},
    {SDL_SCANCODE_LEFT, KM_BTN, EMU_B_OCTDN, "LEFT"},
    {SDL_SCANCODE_RIGHT, KM_BTN, EMU_B_OCTUP, "RIGHT"},
    {SDL_SCANCODE_ESCAPE, KM_OCTBOTH, 0, "ESC"},
    /* knobs */
    {SDL_SCANCODE_Q, KM_SELECT, EMU_E_MASTER, "Q"},
    {SDL_SCANCODE_R, KM_SELECT, EMU_E_SELECT, "R"},
    {SDL_SCANCODE_U, KM_SELECT, EMU_E_PRESETS, "U"},
    {SDL_SCANCODE_P, KM_SELECT, EMU_E_ALGO, "P"},
    {SDL_SCANCODE_6, KM_SELECT, EMU_E_K1, "6"},
    {SDL_SCANCODE_7, KM_SELECT, EMU_E_K2, "7"},
    {SDL_SCANCODE_8, KM_SELECT, EMU_E_K3, "8"},
    {SDL_SCANCODE_9, KM_SELECT, EMU_E_K4, "9"},
    {SDL_SCANCODE_UP, KM_TURN, +1, "UP"},
    {SDL_SCANCODE_DOWN, KM_TURN, -1, "DOWN"},
    /* tools */
    {SDL_SCANCODE_F10, KM_SHOT, 0, "F10"},
    {SDL_SCANCODE_F11, KM_RECORD, 0, "F11"},
    {SDL_SCANCODE_F12, KM_DUMP, 0, "F12"},
};
const int KEYMAP_N = (int)(sizeof KEYMAP / sizeof KEYMAP[0]);

const char *const EMU_BTN_NAME[] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                    "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
const char *const EMU_ENC_NAME[] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "MASTER"};

void emu_note_name(int key, char *out)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int m = 53 + key;
    sprintf(out, "%s%d", N[m % 12], m / 12 - 1);
}

const keymap_t *keymap_find(SDL_Scancode sc)
{
    int i;
    for (i = 0; i < KEYMAP_N; i++)
        if (KEYMAP[i].sc == sc)
            return &KEYMAP[i];
    return NULL;
}

const char *keymap_hint(uint8_t kind, int idx)
{
    int i;
    for (i = 0; i < KEYMAP_N; i++)
        if (KEYMAP[i].kind == kind && KEYMAP[i].idx == idx)
            return KEYMAP[i].cap;
    return NULL;
}

void keymap_help(void)
{
    int i;
    char n[8];
    printf("Keyboard map (tools/emu/keymap.c; physical US-layout positions):\n");
    for (i = 0; i < KEYMAP_N; i++) {
        const keymap_t *k = &KEYMAP[i];
        printf("  %-8s ", k->cap);
        switch (k->kind) {
        case KM_KEY:
            emu_note_name(k->idx, n);
            printf("key %-4s (firmware key %d, MIDI %d)\n", n, k->idx, 53 + k->idx);
            break;
        case KM_BTN: printf("button %s\n", EMU_BTN_NAME[k->idx]); break;
        case KM_OCTBOTH: printf("OCT- and OCT+ together\n"); break;
        case KM_SELECT: printf("select knob %s (Up / Down turn it)\n", EMU_ENC_NAME[k->idx]); break;
        case KM_TURN: printf("turn the selected knob %s\n", k->idx > 0 ? "clockwise (+1)" : "counter-clockwise (-1)"); break;
        case KM_SHOT: printf("screenshot of the LCD -> build/emu/shot_NNN.png\n"); break;
        case KM_RECORD: printf("record every LCD frame -> build/emu/rec/NNNN.ppm (toggle)\n"); break;
        case KM_DUMP: printf("print the fm1_in state\n"); break;
        }
    }
    printf("  mouse    click a key / button: press it (right-click: latch it down, again to release)\n"
           "           wheel over a knob: one detent per notch; click a knob: select it; drag it up / down: turn\n"
           "  Cmd-Q or closing the window quits (and prints the audio / frame timing).\n");
}
