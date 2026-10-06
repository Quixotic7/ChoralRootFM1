/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (firmware/src/chord.c; SCL > CHORD: CHRD, VOIC) against the real keyboard, MIDI IN, arp, recording
 * and voice code: the diatonic triads and sevenths of several scales and roots as music theory has them, the
 * fixed shapes, the voicings, at most 4 notes, the names, MONO plays the root, a key's or a MIDI note's release
 * ends exactly the notes it started (CHRD / VOIC changed while held too), shared notes, live recording writes the
 * chord into one step, the ARP gets the chord as held notes, QNT WHITE maps the key first, a kit (the DRUM engine,
 * SAMPLE PERC) ignores CHRD, MIDI OUT of the keys, and OFF plays exactly as before.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static uint32_t ngated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}
static void reset(void)
{
    ui_power_on();
    memset(kb_chn, 0, sizeof kb_chn); memset(mchord, 0, sizeof mchord); memset(chord_last, 0, sizeof chord_last);
    fm1_in.notes = kb_prev = 0; mo_w = mo_r = 0; usb.config = 1;
    trk[0].p[P_VOICE] = V_POLY; trk[0].p[P_SCALE] = 1; trk[0].p[P_ROOT] = 0;   /* C major, POLY */
    events_block(CTL);
}
/* the chord track 0 plays on note r: "n1 n2 n3 [n4]" compared with want (0-terminated) */
static int chord_is(uint32_t r, const uint8_t *want)
{
    uint8_t out[CHORD_MAX];
    uint32_t n = chord_build(&trk[0], r, out), i;
    for (i = 0; i < n; i++) if (want[i] != out[i]) return 0;
    return i == CHORD_MAX || !want[i];
}
static int name_is(uint32_t r, const char *want)
{
    uint8_t out[CHORD_MAX];
    int32_t root;
    uint16_t mask;
    char b[12];
    chord_make(&trk[0], r, out, &root, &mask);
    chord_name(b, (uint32_t)root, mask);
    if (!str_eq(b, want)) printf("  name of %u: %s, want %s\n", r, b, want);
    return str_eq(b, want);
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_enqueue(st >> 4 | st << 8 | d1 << 16 | d2 << 24, 1);
    events_block(CTL);
}
#define K_C4 7u                                   /* keys from F3 (0): C4 7, D4 9, E4 11, G4 14 */
#define K_D4 9u
#define K_E4 11u

static int theory(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    static const char *const MAJ3[7] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
    static const char *const MAJ7[7] = {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    static const uint8_t CMAJ[7] = {60, 62, 64, 65, 67, 69, 71};
    uint32_t i;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    bad += check("C major DIA3 on D: D F A", chord_is(62, (const uint8_t[]){62, 65, 69, 0}));
    bad += check("C major DIA3 on B: B D F (diminished)", chord_is(71, (const uint8_t[]){71, 74, 77, 0}));
    t->p[P_CHRD] = CH_DIA7;
    bad += check("C major DIA7 on G: G B D F", chord_is(67, (const uint8_t[]){67, 71, 74, 77}));
    bad += check("C major DIA7 on C: C E G B", chord_is(60, (const uint8_t[]){60, 64, 67, 71}));
    for (i = 0, ok = 1; i < 7u; i++) {
        t->p[P_CHRD] = CH_DIA3; ok &= name_is(CMAJ[i], MAJ3[i]);
        t->p[P_CHRD] = CH_DIA7; ok &= name_is(CMAJ[i], MAJ7[i]);
    }
    bad += check("C major: C Dm Em F G Am Bdim, Cmaj7 Dm7 Em7 Fmaj7 G7 Am7 Bm7b5", ok);
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9; t->p[P_CHRD] = CH_DIA3;   /* A minor */
    bad += check("A minor DIA3 on B: B D F", chord_is(71, (const uint8_t[]){71, 74, 77, 0}));
    bad += check("A minor DIA3 on A: A C E (Am), on C: C E G, on E: E G B (Em)",
                 chord_is(69, (const uint8_t[]){69, 72, 76, 0}) && chord_is(60, (const uint8_t[]){60, 64, 67, 0}) &&
                 chord_is(64, (const uint8_t[]){64, 67, 71, 0}) && name_is(69, "Am") && name_is(64, "Em"));
    t->p[P_SCALE] = 7;                                             /* A harmonic minor */
    bad += check("A harmonic minor DIA3 on E: E G# B (E), on C: C E G# (Caug)",
                 chord_is(64, (const uint8_t[]){64, 68, 71, 0}) && name_is(64, "E") &&
                 chord_is(60, (const uint8_t[]){60, 64, 68, 0}) && name_is(60, "Caug"));
    t->p[P_CHRD] = CH_DIA7;
    bad += check("A harmonic minor DIA7 on E: E G# B D (E7), on G#: G# B D F (G#dim7), on A: AmM7",
                 chord_is(64, (const uint8_t[]){64, 68, 71, 74}) && name_is(64, "E7") && name_is(68, "G#dim7") &&
                 name_is(69, "AmM7"));
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 3;                           /* Eb major */
    bad += check("Eb major DIA7 on Bb: Bb D F Ab (A#7)", chord_is(58, (const uint8_t[]){58, 62, 65, 68}) && name_is(58, "A#7"));
    t->p[P_SCALE] = 3; t->p[P_ROOT] = 2; t->p[P_CHRD] = CH_DIA3;   /* D dorian */
    bad += check("D dorian DIA3 on D: Dm, on G: G (the major IV)", name_is(62, "Dm") && name_is(67, "G") &&
                 chord_is(67, (const uint8_t[]){67, 71, 74, 0}));
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 0;
    bad += check("a key outside the scale: the scale note below (C# in C major: C)", chord_is(61, (const uint8_t[]){60, 64, 67, 0}));
    t->p[P_SCALE] = 0; t->p[P_ROOT] = 7;                           /* CHR: the major scale of G */
    bad += check("CHR: the major scale of ROOT (G: on A, Am; on F#, F#dim)", name_is(69, "Am") && name_is(66, "F#dim"));
    t->p[P_SCALE] = 5; t->p[P_ROOT] = 0;                           /* C major pentatonic: every other note */
    bad += check("pentatonic DIA3: in the scale (C E A: C6)", chord_is(60, (const uint8_t[]){60, 64, 69, 0}) && name_is(60, "C6"));
    {   /* every scale, root, mode: every note in the scale */
        uint32_t s, r, n, k, note;
        uint8_t out[CHORD_MAX];
        ok = 1;
        for (s = 1; s <= (uint32_t)TP[P_SCALE].max; s++)
            for (r = 0; r < 12u; r++)
                for (note = 36; note < 96u; note++) {
                    t->p[P_SCALE] = (int16_t)s; t->p[P_ROOT] = (int16_t)r;
                    t->p[P_CHRD] = (int16_t)(note & 1u ? CH_DIA3 : CH_DIA7);
                    n = chord_build(t, note, out);
                    ok &= n == (note & 1u ? 3u : 4u);
                    for (k = 0; k < n; k++)
                        ok &= (scale_mask(t) >> ((out[k] + 120u - r) % 12u)) & 1u;
                }
        bad += check("DIA3 / DIA7 of every scale and root: 3 / 4 notes, every one in key", ok);
    }
    return bad;
}

static int shapes_voicings(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    reset();
    t->p[P_CHRD] = CH_MAJ; bad += check("MAJ on C4: C E G", chord_is(60, (const uint8_t[]){60, 64, 67, 0}) && name_is(60, "C"));
    t->p[P_CHRD] = CH_MIN; bad += check("MIN: C Eb G (Cm)", chord_is(60, (const uint8_t[]){60, 63, 67, 0}) && name_is(60, "Cm"));
    t->p[P_CHRD] = CH_DOM7; bad += check("DOM7: C E G Bb (C7)", chord_is(60, (const uint8_t[]){60, 64, 67, 70}) && name_is(60, "C7"));
    t->p[P_CHRD] = CH_MAJ7; bad += check("MAJ7: C E G B (Cmaj7)", chord_is(60, (const uint8_t[]){60, 64, 67, 71}) && name_is(60, "Cmaj7"));
    t->p[P_CHRD] = CH_MIN7; bad += check("MIN7: C Eb G Bb (Cm7)", chord_is(60, (const uint8_t[]){60, 63, 67, 70}) && name_is(60, "Cm7"));
    t->p[P_CHRD] = CH_SUS4; bad += check("SUS4: C F G (Csus4)", chord_is(60, (const uint8_t[]){60, 65, 67, 0}) && name_is(60, "Csus4"));
    t->p[P_CHRD] = CH_POW; bad += check("POW: C G C (C5)", chord_is(60, (const uint8_t[]){60, 67, 72, 0}) && name_is(60, "C5"));
    t->p[P_CHRD] = CH_MIN; t->p[P_SCALE] = 1;
    bad += check("a fixed shape ignores the scale (MIN on D# in C major: D# F# A#)", chord_is(63, (const uint8_t[]){63, 66, 70, 0}));
    t->p[P_CHRD] = CH_MAJ;
    t->p[P_VOIC] = VC_OPEN; bad += check("OPEN: C G E (1-5-3)", chord_is(60, (const uint8_t[]){60, 67, 76, 0}));
    t->p[P_VOIC] = VC_INV1; bad += check("INV1: E G C", chord_is(60, (const uint8_t[]){64, 67, 72, 0}));
    t->p[P_VOIC] = VC_INV2; bad += check("INV2: G C E", chord_is(60, (const uint8_t[]){67, 72, 76, 0}));
    t->p[P_VOIC] = VC_BASS; bad += check("+OCT: C3 C E G", chord_is(60, (const uint8_t[]){48, 60, 64, 67}));
    t->p[P_CHRD] = CH_MAJ7;
    bad += check("+OCT of a seventh: C3 C E B (the fifth dropped, 4 notes)", chord_is(60, (const uint8_t[]){48, 60, 64, 71}));
    t->p[P_VOIC] = VC_OPEN; bad += check("OPEN seventh: C G B E", chord_is(60, (const uint8_t[]){60, 67, 71, 76}));
    t->p[P_VOIC] = VC_INV1; bad += check("INV1 seventh: E G B C", chord_is(60, (const uint8_t[]){64, 67, 71, 72}));
    t->p[P_VOIC] = VC_INV2; bad += check("INV2 seventh: G B C E", chord_is(60, (const uint8_t[]){67, 71, 72, 76}));
    t->p[P_CHRD] = CH_POW; t->p[P_VOIC] = VC_INV1;
    bad += check("INV1 of POW: G C (the doubled root once)", chord_is(60, (const uint8_t[]){67, 72, 0}));
    {   /* every mode, voicing and note: 1..4 notes, ascending, each once, inside 0..127; the name fits */
        uint32_t c, v, s, note, n, k;
        uint8_t out[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        char b[12];
        ok = 1;
        for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
            for (c = 1; c <= (uint32_t)TP[P_CHRD].max; c++)
                for (v = 0; v <= (uint32_t)TP[P_VOIC].max; v++)
                    for (note = 0; note < 128u; note++) {
                        t->p[P_SCALE] = (int16_t)s; t->p[P_CHRD] = (int16_t)c; t->p[P_VOIC] = (int16_t)v;
                        n = chord_make(t, note, out, &r, &mask);
                        ok &= n >= 1u && n <= CHORD_MAX && (mask & 1u);
                        for (k = 1; k < n; k++) ok &= out[k] > out[k - 1u];
                        for (k = 0; k < n; k++) ok &= out[k] <= 127u;
                        chord_name(b, (uint32_t)r, mask);
                        ok &= str_len(b) >= 1u && str_len(b) <= 7u;
                    }
        bad += check("every scale x CHRD x VOIC x note: 1..4 notes, ascending, once each, 0..127, a name", ok);
    }
    t->p[P_CHRD] = CH_OFF;
    bad += check("OFF: the note alone", chord_is(61, (const uint8_t[]){61, 0}));
    return bad;
}

static int keys(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t before;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    key_down(K_D4);
    bad += check("a key plays its chord (D4: D F A), MIDI OUT the three", gate_note(t, 62) && gate_note(t, 65) && gate_note(t, 69) &&
                 ngated(t) == 3u && mo_w == 3u && last_note == 62);
    bad += check("  chord_last: Dm for the CHORD page", chord_last[0].root == 62 && chord_last[0].n == 3u);
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;               /* changed while held */
    key_up(K_D4);
    bad += check("its release ends exactly those (CHRD / VOIC changed meanwhile), MIDI OUT their note-offs",
                 !ngated(t) && mo_w == 6u && ((midi_out_q[5] >> 8) & 0xF0u) == 0x80u);
    t->p[P_CHRD] = CH_DIA3; t->p[P_VOIC] = VC_CLOSE;
    mo_w = mo_r = 0;
    key_down(K_C4); key_down(K_E4);                                /* C E G and E G B: E and G shared */
    before = mo_w;
    bad += check("two chords: the shared notes sound once (C E G B)", ngated(t) == 4u && gate_note(t, 71) && before == 4u);
    key_up(K_C4);
    bad += check("  C up: C ends, E G stay for the E key", !gate_note(t, 60) && gate_note(t, 64) && gate_note(t, 67) && gate_note(t, 71));
    key_up(K_E4);
    bad += check("  E up: nothing left, MIDI OUT balanced", !ngated(t) && mo_w == 8u);
    t->p[P_VOICE] = V_MONO;
    key_down(K_C4 + 1u);                                           /* C#: DIA snaps to C */
    bad += check("MONO: the root alone (C# in C major: C)", ngated(t) == 1u && gate_note(t, 60));
    t->p[P_VOICE] = V_POLY;
    key_up(K_C4 + 1u);
    bad += check("  released (MONO switched off meanwhile)", !ngated(t));
    t->p[P_VOICE] = V_LEGATO;
    key_down(K_E4);
    bad += check("LEGATO: the root alone (E)", ngated(t) == 1u && gate_note(t, 64));
    key_up(K_E4);
    t->p[P_VOICE] = V_POLY;
    t->p[P_QUANT] = 2;                                             /* WHITE in A minor: D4 is the 4th degree */
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9;
    key_down(K_D4);                                                /* WHITE: C4 = A4 (the root), D4 = B4 */
    bad += check("QNT WHITE: the key mapped first (A minor, D4 -> B4), then its chord B D F",
                 kb_note[K_D4] == 71 && gate_note(t, 71) && gate_note(t, 74) && gate_note(t, 77));
    key_up(K_D4);
    bad += check("  released", !ngated(t));
    t->p[P_CHRD] = CH_OFF; t->p[P_QUANT] = 0;
    before = mo_w;
    key_down(K_C4);
    bad += check("OFF: one note, as before", ngated(t) == 1u && gate_note(t, 60) && mo_w == before + 1u);
    key_up(K_C4);
    return bad;
}

static int arp_rec(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i;
    reset();
    t->p[P_CHRD] = CH_DIA7;
    t->p[P_AMODE] = 1;
    key_down(K_C4);
    bad += check("ARP: one key, the chord as held notes (C E G B)", t->nheld == 4u && t->held[0] == 60 && t->held[1] == 64 &&
                 t->held[2] == 67 && t->held[3] == 71);
    key_up(K_C4);
    bad += check("  key up: nothing held", !t->nheld && !t->arp_phys);
    t->p[P_AMODE] = 0;
    for (i = 0; i < NSTEP; i++) t->step[i] = (step_t){{0}, 0, ST_REST, 0, 0};
    t->p[P_CHRD] = CH_DIA3;
    song.rec = 1; song.playing = 1;
    t->seq_idx = 0; t->seq_pos = 0;
    key_down(K_D4);
    bad += check("live recording: the chord into one step (D F A)", t->step[0].n == 3u && t->step[0].time == ST_NOTE &&
                 t->step[0].note[0] == 62 && t->step[0].note[1] == 65 && t->step[0].note[2] == 69);
    key_up(K_D4);
    t->p[P_CHRD] = CH_MAJ7; t->p[P_VOIC] = VC_BASS;
    t->seq_idx = 4;
    key_down(K_C4);
    bad += check("  a 4-note voicing: 4 notes in the step (the most a step holds)", t->step[4].n == 4u && t->step[4].note[0] == 48);
    key_up(K_C4);
    song.rec = 0; song.playing = 0;
    t->p[P_VOICE] = V_MONO; t->p[P_CHRD] = CH_DIA3; t->p[P_VOIC] = VC_CLOSE;
    song.rec = 1; song.playing = 1; t->seq_idx = 8;
    key_down(K_E4);
    bad += check("  MONO records its root alone", t->step[8].n == 1u && t->step[8].note[0] == 64);
    key_up(K_E4);
    song.rec = 0; song.playing = 0;
    return bad;
}

static int midi_in(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, free = 1;
    reset();
    t->p[P_CHRD] = CH_DIA3;
    midi(0x90, 62, 100);
    bad += check("MIDI IN: a note plays its chord (D F A)", gate_note(t, 62) && gate_note(t, 65) && gate_note(t, 69) && ngated(t) == 3u);
    t->p[P_CHRD] = CH_OFF;
    midi(0x80, 62, 0);
    for (i = 0; i < MCHORD_N; i++) free &= !mchord[i].id;
    bad += check("  its note-off ends the chord (CHRD OFF meanwhile), the table empty", !ngated(t) && free);
    t->p[P_CHRD] = CH_DIA3;
    midi(0x90, 60, 100); key_down(K_E4);                           /* MIDI C E G, key E G B */
    bad += check("MIDI and a key share notes: C E G B once each", ngated(t) == 4u);
    midi(0x80, 60, 0);
    bad += check("  MIDI C off: C ends, E G stay for the key", !gate_note(t, 60) && gate_note(t, 64) && gate_note(t, 67));
    key_up(K_E4);
    bad += check("  key up: nothing left", !ngated(t));
    midi(0xB0, 64, 127); midi(0x90, 65, 100); midi(0x80, 65, 0);
    bad += check("pedal: the chord (F A C) held after its note-off", gate_note(t, 65) && gate_note(t, 69) && gate_note(t, 72));
    midi(0xB0, 64, 0);
    bad += check("  pedal up: released", !ngated(t));
    for (i = 0; i < MCHORD_N + 2u; i++) midi(0x90, 36 + i, 100);   /* more chords than the table holds */
    for (i = 0, free = 1; i < 2u; i++) free &= gate_note(t, 36 + MCHORD_N + i);   /* (the last two: their root alone) */
    for (i = 0; i < MCHORD_N + 2u; i++) midi(0x80, 36 + i, 0);
    bad += check("a full chord table: further notes play alone; all end", free && !ngated(t) && !t->nmono);
    midi(0x90, 62, 100); midi(0xB0, 120, 0);
    for (i = 0, free = 1; i < MCHORD_N; i++) free &= !mchord[i].id;
    bad += check("All Sound Off forgets the track's MIDI chords", free && !ngated(t));
    return bad;
}

static int kits(void)
{
    int bad = 0;
    track_t *d = &trk[3];
    uint8_t out[CHORD_MAX];
    reset();
    song.sel = 3;                                                  /* the DRUM track */
    d->p[P_CHRD] = CH_DIA7;
    key_down(K_C4);
    bad += check("DRUM: CHRD ignored (one hit per key)", chord_build(d, 36, out) == 1u && out[0] == 36 &&
                 kb_chn[K_C4] == 1u && mo_w == 1u);
    key_up(K_C4);
    midi(0x93, 38, 100);
    bad += check("  MIDI IN on the DRUM track: one hit, no chord kept", !mchord[0].id);
    midi(0x83, 38, 0);
    song.sel = 0;
    trk[0].eng_req = trk[0].engine = 4;                             /* SAMPLE PERC: the GM kit */
    trk[0].p[P_CHRD] = CH_DIA3;
    if (smp_perc_set() >= 0) {
        trk[0].p[P_E0] = (int16_t)smp_perc_set();
        bad += check("SAMPLE PERC (a kit): CHRD ignored", chord_kit(&trk[0]) && chord_build(&trk[0], 38, out) == 1u);
    }
    trk[0].p[P_E0] = 0;
    bad += check("SAMPLE with a melodic set: chords", !chord_kit(&trk[0]) && chord_build(&trk[0], 60, out) == 3u);
    return bad;
}

int main(void)
{
    int bad = theory() + shapes_voicings() + keys() + arp_rec() + midi_in() + kits();
    printf("%s\n", bad ? "CHORD TEST FAILED" : "chord keys test passed");
    return bad != 0;
}
