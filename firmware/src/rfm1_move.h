/* SPDX-License-Identifier: MPL-2.0 */
/* rfm1_move.h: a firmware's own Ableton Move interface, its "takeover" (docs/RFM1-TAKEOVER.md, capability tk1). One
 * header a firmware vendors (copy it into your tree; MPL-2.0, unlike the GPL fm1core.h). An .rfm1 built with
 * fm1_add_rfm1(... WANTS tk1) defines FM1_HOST_TK for every unit, and its shim (core-glue/common/rfm1_shim.c,
 * RFM1_TK) keeps the record the host reads: the Move's 128 x 64 screen, its LEDs, the claims, the words to say.
 * Without FM1_HOST_TK (the FM-1, a native host, an .rfm1 without tk1) every function below is an inline no-op
 * (rfm1_tk_screen returns 0, rfm1_tk_shown 0), so the device image is the same bytes. Keep the Move interface in
 * source files only the tk1 build compiles, or behind #ifdef FM1_HOST_TK.
 *
 *   static void my_input(const uint8_t pkt[4]);       a claimed control: pkt as the Move sends it, a USB-MIDI packet
 *                                                     (pkt[0] cable 0 / CIN, pkt[1] status, pkt[2], pkt[3] data)
 *   static void my_state(int on, unsigned claims, unsigned flags);   1: shown, with these claims granted; 0: gone.
 *                                                     flags RFM1_TK_F_SPEECH: the screen reader is on
 *   static const rfm1_tk_ops_t OPS = { my_input, my_state };
 *   rfm1_tk_register("CHORDS", RFM1_TK_ALL & ~RFM1_TK_TRACKS, &OPS);   once, from init
 *
 *   uint8_t *px = rfm1_tk_screen();     the 1024 bytes (8 pages of 128 columns, a byte 8 pixels down, bit 0 the top);
 *   rfm1_tk_screen_done();              draw, then this: scr_seq moves if the picture changed
 *   rfm1_tk_led(RFM1_TK_L_PAD0 + 5, 255, 120, 0);   an LED's colour (led_seq moves if it changed)
 *   rfm1_tk_white(RFM1_TK_L_SHIFT, 255);            a white LED's brightness, 0..255
 *   rfm1_tk_claims(RFM1_TK_ALL);        the parts taken from now on (0: no takeover now)
 *   rfm1_tk_say("Chord D minor seven", 1);          words for the screen reader (1: interrupt what is being said)
 *   rfm1_tk_shown()                     1 while the takeover is on the Move
 *
 * Call them from the firmware's own calls (init, its frame, its input handler), never from its audio. */
#ifndef RFM1_MOVE_H
#define RFM1_MOVE_H
#include <stdint.h>

/* ---- the claims (section 2): the controls' input and their LEDs ---- */
#define RFM1_TK_SCREEN 0x001u    /* the 128 x 64 screen */
#define RFM1_TK_PADS 0x002u      /* the 32 pads: notes 68..99, poly pressure, channel pressure; LEDs 0..31 */
#define RFM1_TK_STEPS 0x004u     /* the 16 step buttons: notes 16..31; LEDs 32..47 and the icons 76..91 */
#define RFM1_TK_KNOBS 0x008u     /* knobs 1..8: CC 71..78 (relative), touch notes 0..7; the rings 55..62 */
#define RFM1_TK_JOG 0x010u       /* the wheel: CC 14 (relative), its click CC 3, touch note 9 */
#define RFM1_TK_VOLUME 0x020u    /* the volume knob: CC 79 (relative), touch note 8 */
#define RFM1_TK_BUTTONS 0x040u   /* Shift .. Sample (below); LEDs 48..50, 63..75 */
#define RFM1_TK_TRACKS 0x080u    /* Track 1..4: CC 43, 42, 41, 40; LEDs 51..54 */
#define RFM1_TK_ALL 0x0FFu

#define RFM1_TK_F_SPEECH 1u      /* rfm1_tk_state's flags: the screen reader is on */
#define RFM1_TK_SAY_INTERRUPT 1u /* the record's say_flags */

/* ---- the LEDs (section 4.2: rfm1cs-abletonmove's order) ---- */
#define RFM1_TK_L_PAD0 0         /* + 0..31: the pads, bottom-left first, rows of 8 (pad n = note 68 + n) */
#define RFM1_TK_L_STEP0 32       /* + 0..15 */
#define RFM1_TK_L_PLAY 48
#define RFM1_TK_L_REC 49
#define RFM1_TK_L_SAMPLE 50
#define RFM1_TK_L_TRACK1 51      /* + 0..3: Track 1..4 */
#define RFM1_TK_L_KNOB1 55       /* + 0..7: the knob rings 1..8 */
#define RFM1_TK_L_WHITE0 63      /* the white LEDs from here on (r: the brightness) */
#define RFM1_TK_L_SHIFT 63
#define RFM1_TK_L_MENU 64
#define RFM1_TK_L_BACK 65
#define RFM1_TK_L_CAPTURE 66
#define RFM1_TK_L_DOWN 67
#define RFM1_TK_L_UP 68
#define RFM1_TK_L_UNDO 69
#define RFM1_TK_L_LOOP 70
#define RFM1_TK_L_COPY 71
#define RFM1_TK_L_LEFT 72
#define RFM1_TK_L_RIGHT 73
#define RFM1_TK_L_MUTE 74
#define RFM1_TK_L_DELETE 75
#define RFM1_TK_L_ICON0 76       /* + 0..15: the icons under the steps */
#define RFM1_TK_L_N 92

#define RFM1_TK_W 128
#define RFM1_TK_H 64
#define RFM1_TK_SCREEN_BYTES 1024
#define RFM1_TK_NAME_LEN 16      /* with the NUL: at most 15 characters */
#define RFM1_TK_SAY_LEN 128      /* with the NUL */

/* ---- the Move's surface MIDI (cable 0), for decoding ---- */
#define RFM1_TK_NOTE_PAD0 68         /* .. 99 */
#define RFM1_TK_NOTE_STEP0 16        /* .. 31 */
#define RFM1_TK_NOTE_KNOB_TOUCH0 0   /* .. 7 */
#define RFM1_TK_NOTE_VOLUME_TOUCH 8
#define RFM1_TK_NOTE_JOG_TOUCH 9
#define RFM1_TK_CC_JOG_CLICK 3
#define RFM1_TK_CC_JOG 14            /* relative */
#define RFM1_TK_CC_TRACK4 40
#define RFM1_TK_CC_TRACK3 41
#define RFM1_TK_CC_TRACK2 42
#define RFM1_TK_CC_TRACK1 43
#define RFM1_TK_CC_SHIFT 49
#define RFM1_TK_CC_MENU 50
#define RFM1_TK_CC_BACK 51
#define RFM1_TK_CC_CAPTURE 52
#define RFM1_TK_CC_DOWN 54
#define RFM1_TK_CC_UP 55
#define RFM1_TK_CC_UNDO 56
#define RFM1_TK_CC_LOOP 58
#define RFM1_TK_CC_COPY 60
#define RFM1_TK_CC_LEFT 62
#define RFM1_TK_CC_RIGHT 63
#define RFM1_TK_CC_KNOB1 71          /* .. 78, relative */
#define RFM1_TK_CC_VOLUME 79         /* relative */
#define RFM1_TK_CC_PLAY 85
#define RFM1_TK_CC_REC 86
#define RFM1_TK_CC_MUTE 88
#define RFM1_TK_CC_SAMPLE 118
#define RFM1_TK_CC_DELETE 119
/* a relative CC's value as a signed step: 1..63 clockwise, 127..65 anticlockwise (127 = -1) */
#define RFM1_TK_REL(v) ((int)(v) < 64 ? (int)(v) : (int)(v) - 128)

/* the claim that covers a surface packet (pkt[1] status, pkt[2] note / CC), 0: none (not the firmware's, ever) */
static inline unsigned rfm1_tk_claim_of(const uint8_t pkt[4])
{
    const unsigned st = pkt[1] & 0xF0u, d = pkt[2];
    if (st == 0x90u || st == 0x80u) {
        if (d >= 68u && d <= 99u) return RFM1_TK_PADS;
        if (d >= 16u && d <= 31u) return RFM1_TK_STEPS;
        if (d <= 7u) return RFM1_TK_KNOBS;
        if (d == 8u) return RFM1_TK_VOLUME;
        if (d == 9u) return RFM1_TK_JOG;
        return 0u;
    }
    if (st == 0xA0u)
        return d >= 68u && d <= 99u ? RFM1_TK_PADS : 0u;
    if (st == 0xD0u)
        return RFM1_TK_PADS;
    if (st == 0xB0u) {
        if (d >= 71u && d <= 78u) return RFM1_TK_KNOBS;
        if (d == 79u) return RFM1_TK_VOLUME;
        if (d == 14u || d == 3u) return RFM1_TK_JOG;
        if (d >= 40u && d <= 43u) return RFM1_TK_TRACKS;
        switch (d) {
        case 49: case 50: case 51: case 52: case 54: case 55: case 56: case 58: case 60: case 62: case 63:
        case 85: case 86: case 88: case 118: case 119:
            return RFM1_TK_BUTTONS;
        default:
            return 0u;
        }
    }
    return 0u;
}

typedef struct {
    void (*input)(const uint8_t pkt[4]);                    /* a claimed control (rfm1_tk_input) */
    void (*state)(int on, unsigned claims, unsigned flags); /* shown / gone (rfm1_tk_state); may be NULL */
} rfm1_tk_ops_t;

#ifdef FM1_HOST_TK
/* (the shim's, weak: a build without it links, and the calls do nothing) */
void rfm1_tk__register(const char *name, unsigned claims, const rfm1_tk_ops_t *ops) __attribute__((weak));
uint8_t *rfm1_tk__screen(void) __attribute__((weak));
void rfm1_tk__screen_done(void) __attribute__((weak));
void rfm1_tk__led(int i, uint8_t r, uint8_t g, uint8_t b) __attribute__((weak));
void rfm1_tk__claims(unsigned claims) __attribute__((weak));
void rfm1_tk__say(const char *text, int interrupt) __attribute__((weak));
int rfm1_tk__shown(void) __attribute__((weak));
static inline void rfm1_tk_register(const char *name, unsigned claims, const rfm1_tk_ops_t *ops)
{
    if (rfm1_tk__register) rfm1_tk__register(name, claims, ops);
}
static inline uint8_t *rfm1_tk_screen(void) { return rfm1_tk__screen ? rfm1_tk__screen() : (uint8_t *)0; }
static inline void rfm1_tk_screen_done(void)
{
    if (rfm1_tk__screen_done) rfm1_tk__screen_done();
}
static inline void rfm1_tk_led(int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (rfm1_tk__led) rfm1_tk__led(i, r, g, b);
}
static inline void rfm1_tk_white(int i, uint8_t v)
{
    if (rfm1_tk__led) rfm1_tk__led(i, v, v, v);
}
static inline void rfm1_tk_claims(unsigned claims)
{
    if (rfm1_tk__claims) rfm1_tk__claims(claims);
}
static inline void rfm1_tk_say(const char *text, int interrupt)
{
    if (rfm1_tk__say) rfm1_tk__say(text, interrupt);
}
static inline int rfm1_tk_shown(void) { return rfm1_tk__shown ? rfm1_tk__shown() : 0; }
#else
static inline void rfm1_tk_register(const char *name, unsigned claims, const rfm1_tk_ops_t *ops)
{
    (void)name, (void)claims, (void)ops;
}
static inline uint8_t *rfm1_tk_screen(void) { return (uint8_t *)0; }
static inline void rfm1_tk_screen_done(void) {}
static inline void rfm1_tk_led(int i, uint8_t r, uint8_t g, uint8_t b) { (void)i, (void)r, (void)g, (void)b; }
static inline void rfm1_tk_white(int i, uint8_t v) { (void)i, (void)v; }
static inline void rfm1_tk_claims(unsigned claims) { (void)claims; }
static inline void rfm1_tk_say(const char *text, int interrupt) { (void)text, (void)interrupt; }
static inline int rfm1_tk_shown(void) { return 0; }
#endif

#endif
