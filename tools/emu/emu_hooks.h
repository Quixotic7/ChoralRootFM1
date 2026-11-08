/* SPDX-License-Identifier: GPL-3.0-only */
/* The boundary between the Mac emulator (emu.c: SDL window, audio device, CoreMIDI, keyboard map)
 * and the firmware build (emu_fw.c: the firmware sources through emu_firmware.h, the HAL stubs and
 * these hooks). emu.c never sees a firmware symbol: everything goes through emu_hal and the hooks below,
 * so the instrument's UI files can be swapped (emu_firmware.h, emu_fw.c) without touching the emulator.
 *
 * Threads (realtime mode):
 *   main   SDL events, emu_fw_frame() every 15 ms and emu_fw_idle() in between (the device's main loop)
 *   timer  emu_fw_tick() every 1 ms (the device's TIMER5 ISR: fm1_ms, the input scan, the USB poll)
 *   audio  emu_fw_audio() from the SDL audio callback (the device's ALNK0 ISR)
 * The firmware side serialises the two "ISRs" and the UI's interrupt-off sections with one lock. */
#pragma once
#include <stdint.h>

#define EMU_LCD_W 240
#define EMU_LCD_H 240
#define EMU_NCOL 11          /* LED / matrix columns (hal/fm1_input.h FM1_NCOL) */
#define EMU_NKEY 27          /* note keys: 0 = F3 (MIDI 53) .. 26 = G5 */
#define EMU_NBTN 14          /* matrix buttons 0..13 */
#define EMU_NENC 7           /* matrix encoders 0..6 */
#define EMU_FS 44100
#define EMU_BLOCK 128        /* frames per audio block (core.h HALF_FRAMES: one I2S half buffer) */

/* button labels, in the panel's order (the designer's BTN_IDS; SEL is the firmware's B_SCL) */
enum { EMU_B_FX, EMU_B_SEL, EMU_B_ENV, EMU_B_LFO, EMU_B_EDIT, EMU_B_GLO, EMU_B_HOME, EMU_B_SAVE,
       EMU_B_ARP, EMU_B_SEQ, EMU_B_PLAY, EMU_B_REC, EMU_B_OCTDN, EMU_B_OCTUP, EMU_NB };
/* knob roles (the firmware's EN_* order), then the MASTER pot */
enum { EMU_E_SELECT, EMU_E_ALGO, EMU_E_PRESETS, EMU_E_K1, EMU_E_K2, EMU_E_K3, EMU_E_K4, EMU_E_MASTER, EMU_NE };

typedef struct {
    /* emulator -> firmware: the panel as the matrix sees it (written by the main thread) */
    volatile uint32_t keys;            /* bit n: note key n held */
    volatile uint32_t buttons;         /* bit i: matrix button i held */
    volatile uint32_t keys_tap, buttons_tap;   /* went down since the last tick (a tap shorter than 1 ms) */
    volatile int32_t enc[EMU_NENC];    /* detent steps not yet taken by the tick, per matrix encoder, + = CW */
    volatile int32_t master;           /* the MASTER pot: ADC 0..1023 */
    volatile uint8_t key_vel[32];      /* a held key's velocity as the script gave it (0: none given, 100), keys 0..19
                                        * (the analog rows): emu_fw.c feeds cr_sense.c the readings that land there */
    volatile uint8_t key_lean[32];     /* .. and its lean, 0..100 % of full aftertouch (the "lean" command) */
    /* firmware -> emulator */
    uint8_t led[EMU_NCOL], led_dim[EMU_NCOL];   /* fm1_led / fm1_led_dim: packed row bits per column */
    uint8_t led_mid[EMU_NCOL];                  /* fm1_led_dim2: the middle level (dim < mid < lit) */
    uint8_t led_faint[EMU_NCOL];                /* fm1_led_faint: the faint level (faint < dim) */
    uint16_t lcd[EMU_LCD_W * EMU_LCD_H];        /* the panel's RAM, RGB565 as sent (big-endian) */
    volatile uint32_t lcd_writes;               /* bumped by every fill / blit */
    /* PA2 driven high (hal/fm1_lcd_hw.h): on the unit that pin is not the backlight alone, it also keeps the key
     * matrix / 595 chain alive (seen 2026-10-10: the screen off of 0.14 / 0.15 raised it and the unit went deaf). While
     * set the panel shows black, emu_fw_tick reads no key, button or knob and no LED lights (emu.c), and the dump
     * says so. Nothing in the firmware raises PA2 any more; a HAL stand-in (emu_hal_fw.h) for code that ever does
     * must set it, and the script's "pa2 high" sets it by hand (scripts/cr_pa2.txt: the model itself, checked) */
    volatile uint8_t pa2;
    /* the panel: label -> matrix id (firmware panel.c), role -> matrix encoder and direction */
    uint8_t btn_id[EMU_NB];
    uint8_t enc_id[EMU_NE - 1];
    int8_t enc_dir[EMU_NE - 1];
    uint32_t audio_wait_max_us;                 /* the longest the audio ISR waited for the CPU lock */
    uint32_t ui_lock_max_us;                    /* the longest the UI held it (ui_input + ui_leds) */
    uint8_t led_play_green;                     /* (col << 3) | row bit of PLAY's green LED */
    uint8_t ready;
    /* the keys while a frame draws (README "A redraw in simulated time"; firmware/src/cr_ui.c cr_ui_pump): the
     * firmware calls strip_done after each strip of a redraw (headless: emu.c moves the device's clock on by a
     * strip's time, EMU_STRIP_US; 0: nothing) and pump_mark each time it is about to read the keys inside a redraw
     * (EMU_UI_GAP=1: emu.c measures the stretches between). pump_off (EMU_PUMP=0): the keys are not read there, as
     * before 0.18: the reference the latency is measured against */
    void (*strip_done)(void);
    void (*pump_mark)(void);
    uint8_t pump_off;
    /* a script's "knobs quick" (1) / "knobs real" (0, the default): every knob detent at the quickest speed (the
     * firmware's knob acceleration, firmware/src/cr_pages.c kn_turn: the range / 20 a detent, the step of before
     * 0.22), for the scripts written against that step; real: the speed from the detents' times, as the unit */
    volatile uint8_t knob_quick;
    /* the script's "keys classic" (emu.c load_script, read once at power-on by emu_fw.c): a unit with no settings record
     * starts with Options > Chord Keys Classic, as one whose player chose it (0.25.1: FM-1 is the default; the scripts
     * written for the Classic keys say so) */
    uint8_t keys_classic;
} emu_hal_t;
extern emu_hal_t emu_hal;

/* key id (0..13 buttons, 14..40 note keys) at (column, row bit); -1: none (hal/fm1_input.h FM1_KEYMAP) */
extern const int8_t emu_keymap[6][EMU_NCOL];

/* ---- hooks, implemented by the firmware build (emu_fw.c) ---- */
/* the flash file (before emu_fw_init): flash_path NULL = the default (build/emu/flash.bin; headless: none, a fresh
 * flash each run), no_flash = RAM only, save_on_exit = the settings saved at exit */
void emu_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless);
void emu_fw_erase_ms(int ms);                     /* --erase-ms: a sector erase's time (emu_hal_fw.h) */
/* the boot guard before the power-on (firmware/src/cr_bootguard.h): fail < 0 none (a clean record), reason NULL a
 * power-on, stage < 0 the default; 0 = a bad reason */
int emu_fw_boot_options(int fail, const char *reason, int stage);
void emu_fw_init(int demo);                 /* power-on (main.c felucca_init); demo: 4 patterns, PLAY */
void emu_fw_tick(uint32_t ms);              /* 1 ms timer: fm1_ms, input edges, MIDI in/out queues */
void emu_fw_frame(void);                    /* one UI frame: ui_input + ui_leds + ui_draw */
void emu_fw_idle(void);                     /* between frames: ui_input (main.c's wait loop) */
void emu_fw_audio(int16_t *stereo, uint32_t frames);   /* frames: a multiple of EMU_BLOCK; s16 interleaved */
int emu_fw_midi_in(uint32_t pkt);           /* one USB-MIDI event packet in; 0: no room (retry later) */
void emu_fw_usb_lost(void);                 /* the USB peer went (usb.c usb_midi_lost, 3y261) */
int emu_fw_midi_out_take(uint32_t *pkt);    /* one USB-MIDI event packet out, 0: none */
void emu_fw_dump(void);                     /* print the firmware's input state (F12) */
void emu_fw_stats(uint32_t *shed, uint32_t *cpu_pct);  /* voices shed on overload, the CPU meter */
void emu_fw_stages(char *buf, uint32_t n);  /* the audio ISR's stage profile (EMU_STAGES=1), "" = none */
/* the keys' way to the engine since power-on (firmware/src/cr_out.c cr_klat, the unit's 78 PEAK): v[0] the note-ons
 * timed, then us from the tick that saw the key down: v[1] the longest to the main loop's post, v[2] the average and
 * v[3] the longest to the audio block that applied it; v[4] the key scans done inside redraws. Returns the strips
 * the redraw in progress (or the last one) has composed */
uint32_t emu_fw_keys(uint32_t v[5]);
uint32_t emu_fw_layout(void);              /* Options > Chord Keys: 1 FM-1, 0 Classic (the panel's labels) */
void emu_fw_ui_info(char *buf, uint32_t n);  /* the last UI frame's screen: kind, text, strips (EMU_UI_LOG) */
