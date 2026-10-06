/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real sequencer/project/UI sources: motion lifecycle and compact migration. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int motion_recording(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 23; song.sel = 0; song.rec = 1;
    seq_start(); seq_tick(t, CTL); /* first real step */
    t->p[P_REV] = 92;
    bad += check("motion records the selected armed track at its current step", !motion_capture(t, P_REV, 92) &&
        motion.count == 1u && motion.event[0].place == 0u && motion.event[0].value == 92);
    t->p[P_REV] = 110; motion_capture(t, P_REV, 110);
    bad += check("same step/parameter overwrites rather than consuming capacity", motion.count == 1u && motion.event[0].value == 110);
    bad += check("sounding values never replace the original patch base", t->p[P_REV] == 110 && motion_base_value(t, P_REV) == 23);
    project_t q; project_store_t packed;
    project_capture(&q);
    bad += check("project snapshot saves base + independent events while sounding", q.t[0].p[P_REV] == 23 && q.motion.event[0].value == 110 &&
        proj_pack(&packed, &q) && sizeof packed == 3584u);
    seq_stop();
    bad += check("stop before another step restores the original parameter", t->p[P_REV] == 23);
    seq_start(); seq_tick(t, CTL);
    bad += check("recorded motion plays back at step zero", t->p[P_REV] == 110);
    motion_set_enabled(t, 0);
    bad += check("bypass restores base without deleting automation", t->p[P_REV] == 23 && motion_count(t) == 1u && !motion_enabled(t));
    motion_set_enabled(t, 1); motion_step(t, 0, &motion);
    song.rec = 0; t->p[P_REV] = 37; motion_capture(t, P_REV, 37); seq_stop();
    bad += check("manual edits outside recording become the new base", t->p[P_REV] == 37);
    song.rec = 3; seq_start(); seq_tick(t, CTL);
    trk[1].p[P_REV] = 45; motion_capture(&trk[1], P_REV, 45);
    bad += check("only the selected track captures knob motion", motion_count(&trk[1]) == 0u);
    bad += check("transport and engine-switch parameters cannot be captured", !motion_param(P_SLEN) && !motion_param(P_AMODE) &&
        motion_set_event(t, 0, P_SLEN, 8) == 1);
    seq_stop(); motion_clear(t);
    t->p[P_REV] = 42; song.rec = 1; seq_start(); seq_tick(t, CTL);
    motion_clear(t); t->p[P_REV] = 70; motion_capture(t, P_REV, 70); seq_stop();
    bad += check("clearing then recording mid-play keeps the correct new base", t->p[P_REV] == 42);
    return bad;
}
static int motion_capacity(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t i = 0; i < MOTION_MAX; i++) bad += motion_set_event(&trk[0], i, P_REV, (int16_t)i);
    motion_store_t saved = motion;
    bad += check("full event pool refuses append without overwriting earlier events", motion_set_event(&trk[1], 0, P_REV, 90) == 2 &&
        !memcmp(&motion, &saved, sizeof saved));
    bad += check("full pool still permits a targeted overwrite", !motion_set_event(&trk[0], 12, P_REV, 100) && motion.count == MOTION_MAX);
    motion_delete_event(&trk[0], 12, P_REV);
    bad += check("deleting one event releases one slot", motion.count == MOTION_MAX - 1u && !motion_set_event(&trk[1], 0, P_REV, 90));
    saved = motion; motion_store_t invalid = motion; invalid.count = MOTION_MAX + 1;
    bad += check("invalid restore leaves the pool intact", motion_replace_track(&trk[0], &invalid) != 0 && !memcmp(&motion, &saved, sizeof saved));
    motion_clear(&trk[0]);
    bad += check("track clear keeps every other track's events", motion.count == 1u && motion.event[0].place >> 6 == 1u);
    return bad;
}
static int probability_playback(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    step_t s = {{60, 64, 0, 0}, 2, ST_NOTE, 0, 90, 1u << DV_KICK, 0, 0};
    bad += check("zero-initialized probability remains legacy 100 percent", step_chance(&s) == 100u);
    step_set_chance(&s, 0); seq_step(t, &s, div_samples(2), 0);
    bad += check("zero percent suppresses the whole chord and drum hits", step_chance(&s) == 0u && !t->seq_n);
    step_set_chance(&s, 100); seq_step(t, &s, div_samples(2), 0);
    bad += check("100 percent plays all chord notes and drum hits", t->seq_n == 3u);
    seq_release(t); step_set_chance(&s, 50); uint32_t heard = 0;
    for (uint32_t i = 0; i < 1000u; i++) { seq_step(t, &s, div_samples(2), 0); heard += t->seq_n != 0; seq_release(t); }
    bad += check("chance is evaluated each repeat with one decision per step", heard > 350u && heard < 650u);
    return bad;
}
static int compact_project(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->step[3] = (step_t){{60, 67}, 2, ST_NOTE, SF_ACCENT, 110, 0x81, 0x80, 0};
    step_set_chance(&t->step[3], 25); t->p[P_FM1_LEVEL] = 80; t->p[P_ED_FLT] = -50;
    motion_set_event(t, 3, P_REV, 110);
    project_t before, after; project_store_t packed, corrupt;
    project_capture(&before);
    bad += check("FUN8 fits the retained and flash extent", sizeof(proj_slot) == 4u * 3584u && proj_pack(&packed, &before));
    bad += check("FUN8 round trip preserves signed values/FM params/probability/motion", proj_import(&after, &packed, sizeof packed) &&
        !memcmp(&before, &after, sizeof before));
    corrupt = packed; corrupt.raw[112] ^= 1u;
    bad += check("FUN7 torn or corrupted payload is refused", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; corrupt.raw[68] = 255; uint32_t sum = proj_hash(corrupt.raw, sizeof corrupt - 4u);
    memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("compact parameter range is validated even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; uint32_t probability_byte = 68u + P_COUNT + 2u + 8u; corrupt.raw[probability_byte] = 127;
    sum = proj_hash(corrupt.raw, sizeof corrupt - 4u); memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("invalid probability is refused even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    project_v6_t old; memset(&old, 0, sizeof old); old.magic = PROJ_MAGIC_V6; old.size = sizeof old;
    memcpy(old.g, before.g, sizeof old.g); old.parts = NPART; old.phys = PROJ_PHYS;
    for (uint32_t k = 0; k < NTRK; k++) {
        for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = before.t[k].p[j];
        for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = before.t[k].p[P_E0 + j];
        old.t[k].engine = before.t[k].engine; old.t[k].preset = before.t[k].preset;
        for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &before.t[k].step[j], sizeof(step10_t));
    }
    old.chain = before.chain; old.sum = proj_hash(&old, sizeof old - 4u);
    bad += check("real FUN6 disk image migrates with FM defaults/100% chance/no motion", proj_import(&after, &old, sizeof old) &&
        after.t[0].p[P_E0] == before.t[0].p[P_E0] && after.t[0].p[P_ED_FLT] == -50 &&
        after.t[0].p[P_FM1_LEVEL] == 127 && !after.motion.count && step_chance(&after.t[0].step[3]) == 100u);
    bad += check("migrated FUN6 can be written as fixed-size FUN7", proj_pack(&packed, &after));
    project_save(1); motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    project_load(1);
    bad += check("actual project save/load restores motion, chance and operator settings", motion_count(t) == 1u &&
        step_chance(&t->step[3]) == 25u && t->p[P_FM1_LEVEL] == 80);
    return bad;
}
/* a FUN7 image as the firmware of 89 parameters (before the chord keys P_CHRD / P_VOIC) wrote it: the engine's
 * values at 81..88, motion ids from 81 on for E0..E7 (m: its events as that firmware numbered them) */
static void pack_fun7_89(project_store_t *out, const project_t *q, const motion_store_t *m)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = 89;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < 89u; i++) b[pos++] = (uint8_t)(q->t[t].p[i < 81u ? i : i + 2u] + 64);
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            memcpy(b + pos, s->note, 4); pos += 4;
            b[pos++] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
            b[pos++] = s->vel; b[pos++] = s->hit; b[pos++] = s->acc; b[pos++] = s->probability;
        }
    }
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, m, sizeof *m);
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
}
static int fun7_89(void)
{
    int bad = 0, ok; ui_power_on(); track_t *t = &trk[0];
    project_t before, after; project_store_t old; motion_store_t m;
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 8u; i++) trk[k].p[P_E0 + i] = (int16_t)(ENGINES[trk[k].eng_req]->edit[i].min + (int16_t)(k + i) %
            (ENGINES[trk[k].eng_req]->edit[i].max - ENGINES[trk[k].eng_req]->edit[i].min + 1));
    t->p[P_FM1_ATK] = 33; t->p[P_REV] = 20;
    t->step[2] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 100};
    project_capture(&before);
    memset(&m, 0, sizeof m);                                       /* as the 89-parameter firmware numbered them */
    m.count = 3; m.on = 1;
    m.event[0] = (motion_event_t){3, P_REV, 90};
    m.event[1] = (motion_event_t){5, 81, 40};                      /* its P_E0 (81) */
    m.event[2] = (motion_event_t){6, 61, 20};                      /* FM OP1 ATK: 61 then and now */
    pack_fun7_89(&old, &before, &m);
    ok = proj_import(&after, &old, sizeof old);
    for (k = 0; ok && k < NTRK; k++) {
        for (i = 0; i < 8u; i++) ok &= after.t[k].p[P_E0 + i] == before.t[k].p[P_E0 + i];
        for (i = 0; i < 81u; i++) ok &= after.t[k].p[i] == before.t[k].p[i];
        ok &= after.t[k].p[P_CHRD] == 0 && after.t[k].p[P_VOIC] == 0;
    }
    bad += check("FUN7 of 89 parameters: E0..E7 at 83..90, the chord keys OFF / CLOSE, the rest in place", ok &&
        after.t[0].p[P_FM1_ATK] == 33 && !memcmp(after.t[0].step, before.t[0].step, sizeof before.t[0].step));
    bad += check("  its motion: E0 (81) -> 83, REV and FM OP1 ATK (61) kept",
        after.motion.count == 3u && after.motion.event[0].param == P_REV && after.motion.event[1].param == P_E0 &&
        after.motion.event[1].value == 40 && after.motion.event[2].param == P_FM1_ATK);
    bad += check("  written again as FUN7 of 91: the same project", proj_pack(&old, &after) && old.raw[66] == P_COUNT &&
        proj_import(&before, &old, sizeof old) && !memcmp(&before, &after, sizeof before));
    pack_fun7_89(&old, &before, &m);                                /* SONG: a slot of the old firmware */
    memcpy(&proj_slot[2], &old, sizeof old);
    chain_config.count = 1; chain_config.row[0] = (chain_row_t){2, 1};
    ok = chain_prepare() == 0;
    bad += check("  SONG: an 89-parameter slot's motion plays at today's ids (E0 at 83)", ok &&
        chain.source[2].motion.count == 3u && chain.source[2].motion.event[1].param == P_E0);
    seq_stop(); chain_config.count = 0; chain.armed = 0;
    m.event[1].param = 82;                                          /* (any id P_E0 .. P_E7 of then moves by 2) */
    pack_fun7_89(&old, &before, &m);
    bad += check("  E1 (82) -> 84", proj_import(&after, &old, sizeof old) && after.motion.event[1].param == P_E1);
    return bad;
}
static int loads_and_song(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 21; motion_set_event(t, 0, P_REV, 100); t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    apply_preset_to(t, 1);
    bad += check("sound load clears incompatible motion and keeps pattern", !motion_count(t) && t->step[0].note[0] == 60);
    undo_swap(); bad += check("sound undo restores the original motion pool and base", motion_count(t) == 1u && t->p[P_REV] == 21);
    undo_swap(); bad += check("sound redo restores the loaded motion state", !motion_count(t));
    undo_swap(); project_save(0);
    t->p[P_REV] = 43; t->step[0].note[0] = 72; chain_config.count = 1; chain_config.row[0] = (chain_row_t){0, 1};
    bad += check("song preparation imports saved motion alongside steps", chain_prepare() == 0 && chain.source[0].motion.count == 1u);
    seq_start(); seq_tick(t, CTL);
    bad += check("song plays saved automation with current instruments", chain.running && t->p[P_REV] == 100 && t->step[0].note[0] == 72);
    seq_stop(); bad += check("song stop restores current base and editable pattern", t->p[P_REV] == 43 && t->step[0].note[0] == 72);
    return bad;
}
static int repeat_mode(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_AMODE] = 6; t->p[P_AOCT] = 4; t->p[P_APROB] = 127;
    arp_add(t, 72); arp_add(t, 60);
    arp_tick(t, CTL);
    bad += check("REPEAT retriggers the last played note without octave traversal", t->arp_note == 60);
    t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT remains on the same note on the next pulse", t->arp_note == 60);
    arp_remove(t, 60); arp_remove(t, 72); arp_tick(t, CTL);
    bad += check("REPEAT releases after the last held key is released", !t->nheld && !t->arp_note);
    t->p[P_AHOLD] = 1; arp_add(t, 65); arp_remove(t, 65); t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT supports ARP HOLD", t->nheld == 1u && t->arp_note == 65);
    return bad;
}
int main(void)
{
    int bad = motion_recording() + motion_capacity() + probability_playback() + compact_project() + fun7_89() + loads_and_song() + repeat_mode();
    printf("%s\n", bad ? "MOTION TEST FAILED" : "motion/chance/compact storage tests passed"); return bad != 0;
}
