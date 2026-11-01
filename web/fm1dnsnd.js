// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Elektron Digitone sound files (.dnsnd) read for FM TONE (engine 15, eng_quad.c), docs/SOUNDS.md ".dnsnd import".
// Clean-room: the container was decoded from files, no Elektron code or data is used. Pure: no DOM; the files are read
// on this computer, nothing is uploaded.
//
// A .dnsnd is a ZIP of manifest.json ({FormatVersion, ProductType, Payload, FileType "Sound", FirmwareVersion,
// MetaInfo.Tags}) and the payload named by Payload: a 0x1f-byte header (magic ac 11 d3 03, the device u16be at 7: 9 the
// Digitone, 15 the Digitone II; the build in ASCII at 9: "0051" OS 1.31, "0095" 1.42, "0097" 1.44; the body's length
// u32be at 0x19 = 302, the compressed flag at 0x1d), the body (flag 1: an LZ4 chain, u32be size + block .. u32be 0; flag
// 0: as it is), a 12-byte trailer (CRC, length, end magic aa a1 da aa; FIZZ research/DN2-FORMAT.md §2). The 302-byte
// record: be ef ba ce, u32be version (1: the factory sounds; 5: a sound saved on OS 1.44, the owner's exports), u32 tag
// bits, the name (16 bytes, NUL padded) at 12, then the parameters as byte pairs from 30 (the coarse value, the fine
// byte on the hi-res ones). Slot s (offset 28 + 2 s) is also LFO destination number s (proved by the owner's controlled
// exports, 2026-10-14: docs/SOUNDS.md ".dnsnd import").
//
//   parseDnsnd(bytes)   -> Promise<{name, tags, firmware, product, version, params, confidence, warnings, record}>
//   dnsndToQuad(parsed) -> {blob: Uint8Array(96) FM TONE version 5, pan, drive, notes: [what was lost / approximated]}
//   quadToV4(blob)      -> (= quadToV5) a 96-byte FM TONE patch of the current version 5: version 5 as it is, a
//                          version 4 converted (FREQ DTUN RESO BASE clamped to 127.00, AMP REL 127.xx -> 127 INF, below
//                          it rounded but at most 126, LEVEL rounded), an 80-byte version 3 converted (best effort, as
//                          eng_quad.c quad_unpack: the hi-res values x 100, FDBK 0..120 -> 0..127, then as version 4),
//                          1 / 2 zero-padded (the firmware converts them)
//
// Since 2026-10-14 (FM TONE version 4) FM TONE has the Digitone's ranges: the hi-res values (DN_QUAD_HIRES: HARM DTUN
// FREQ RESO filter DEPTH BASE, the LFOs' SPEED and DEPTH) in hundredths, as the Digitone's coarse and fine bytes:
// value = coarse x 100 + round(fine x 100 / 256); FDBK 0..127 as the Digitone's. Version 5 (the owner's corrections,
// the same day): no value tops at 127.99 (FREQ DTUN RESO BASE 0..127.00); AMP DEC and AMP REL whole numbers 0..126 +
// 127 = INF; LEVEL a whole number 0..127

// ---- FM TONE's patch (eng_quad.c QP_*: [min, max, def] of each value, version 5: the hi-res ones in hundredths;
// `sound_templates --quad-ranges` prints it)
export const DN_QUAD_RANGES = [[1,8,1],[0,18,3],[0,63,3],[0,18,3],[-2600,2600,0],[0,12700,0],[0,127,0],[-64,63,-64],[-100,100,0],[-100,100,0],[-100,100,0],[-100,100,0],[0,127,0],[0,127,32],[0,127,127],[0,127,0],[0,127,0],[0,127,32],[0,127,127],[0,127,0],[0,127,0],[0,1,1],[0,1,1],[0,4,1],[0,127,0],[0,1,1],[0,1,1],[0,127,64],[0,127,0],[0,127,0],[0,127,0],[0,127,64],[0,127,0],[0,127,64],[0,12700,12700],[0,12700,0],[0,3,1],[-6400,6399,0],[0,127,0],[0,127,0],[0,12700,0],[0,127,127],[0,127,8],[0,127,32],[0,127,96],[0,127,24],[0,127,110],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[0,18,3],[0,127,0]];
export const DN_QP = { ALGO: 0, RC: 1, RA: 2, RB1: 3, HARM: 4, DTUN: 5, FDBK: 6, MIX: 7, OFSC: 8, OFSA: 9, OFSB1: 10, OFSB2: 11,
  AATK: 12, ADEC: 13, AEND: 14, ALEV: 15, BATK: 16, BDEC: 17, BEND: 18, BLEV: 19, ADLY: 20, ATRIG: 21, ARST: 22, PHRT: 23,
  BDLY: 24, BTRIG: 25, BRST: 26, FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, FREQ: 34, RESO: 35, FTYPE: 36, FDEPTH: 37,
  FDLY: 38, BASE: 40, WIDTH: 41, EATK: 42, EDEC: 43, ESUS: 44, EREL: 45, LEVEL: 46, RB2: 71 };
const DN_QL = { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 };
const dnQlfo = (k, f) => 47 + k * 8 + DN_QL[f];
export const DN_QUAD_NP = 73, DN_QUAD_BLOB = 96, DN_QUAD_BLOB_V3 = 80;
// the hi-res values (eng_quad.c quad_hires): two bytes each in the blob (u16le, value - min); version 4's: + AMP REL, LEVEL
export const DN_QUAD_HIRES = [4, 5, 34, 35, 37, 40, 47, 54, 55, 62, 63, 70];
export const DN_QUAD_HIRES_V4 = [4, 5, 34, 35, 37, 40, 45, 46, 47, 54, 55, 62, 63, 70];
const dnHiRes = (i) => DN_QUAD_HIRES.includes(i);
const dnHiResV4 = (i) => DN_QUAD_HIRES_V4.includes(i);
export const DN_QUAD_VER = 5;
// version 4's ranges (eng_quad.c quad_range_v4): DTUN FREQ RESO BASE AMP REL LEVEL 0..127.99 (hundredths)
export const DN_QUAD_RANGES_V4 = DN_QUAD_RANGES.map((r, i) => [5, 34, 35, 40].includes(i) ? [r[0], 12799, r[2]] :
  i === 45 || i === 46 ? [0, 12799, r[2] * 100] : r);
// a version-4 value -> version 5's (eng_quad.c quad_v5_of_v4, then clamped)
const dnV5OfV4 = (i, v) => {
  const w = i === 45 ? (v >= 12700 ? 127 : v >= 12650 ? 126 : Math.trunc((v + 50) / 100)) :
    i === 46 ? Math.trunc((v + 50) / 100) : v;
  return Math.max(DN_QUAD_RANGES[i][0], Math.min(DN_QUAD_RANGES[i][1], w));
};
const DN_QF = { OFF: 0, LP12: 1, HP12: 2, LP24: 3 };
export const DN_VERSIONS = [1, 4, 5];             // the record versions known
// the layout each is read with: version 4 (a sound saved on OS 1.42, payload header "0095") has version 5's, byte for
// byte (FIZZ b67df20, the owner's F186 KICK01: the tail and its end marker at version 5's offsets, OFS and the filter
// DELAY in place); it was refused as "a newer Digitone format (version 4)"
export const DN_LAYOUT = { 1: 1, 4: 5, 5: 5 };
// Any other version (3y201, the owner: "Presets with a newer format should still import.") is read with the newest
// layout, with a note: no file is refused for its version number. The parameters sit at 28 + 2 x id, the law the
// Digitone II kept for its own 359-byte record (FIZZ research/DN2-FORMAT.md §3: what grew was the tail, the machine
// bytes, the mod and arp records after the parameters), so the head reads where the newest layout has it; the tail (the
// sound's OCTAVE) is found from the record's end marker ba ce f0 0c, at the newest layout's distance from it; what the
// record holds past the fields known is skipped. A version-3 dump (a 2018 .syx: 159 sounds of 282 bytes)
// bears it out: the head as version 5's to 182 (its constants at 170 / 172, OFS 64-centred), the end marker at 278 (20
// before version 5's), the byte 18 before it the multiples of 12 an OCTAVE holds
export const DN_NEWEST = 5;
const DN_END = { 1: 279, 5: 298 };                // the end marker's offset in each layout
const DN_TAIL = 183;                              // the tail: fields from here are read relative to the end marker

// ---- the Digitone's tables (the manual OS 1.44 and the owner's controlled exports, 2026-10-14)
// RATIO C, B1, B2: 19 steps (= FM TONE's QUAD_RCB); exports: C 0 = 0.25, 3 = 1, 4 = 2, 18 = 16; B 0.25 0.5 1 16
export const DN_RATIO_CB = [0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
// RATIO A, 36 steps. Verified anchors (exports): 0 = 0.25, 3 = 1, 9 = 2.5, 15 = 4, 25 = 8, 35 = 16. 0..15 in quarters
// fits every anchor; 16..24 and 26..34 are NOT proved: linear between the anchors (4.4 4.8 .. and 8.8 9.6 ..)
export const DN_RATIO_A = Array.from({ length: 36 }, (_, i) => Math.round((i <= 15 ? (i + 1) / 4 : i <= 25 ? 4 + (i - 15) * 0.4 : 8 + (i - 25) * 0.8) * 100) / 100);
export const DN_RATIO_A_ANCHORS = [0, 3, 9, 15, 25, 35];
const DN_WAVE = ["TRI", "SINE", "SQR", "SAW", "EXP", "RMP", "RND"];   // the manual's order (not proved by an export)
const DN_WAVE_TO_QUAD = [0, 1, 2, 3, 5, 4, 6];                       // FM TONE: TRI SINE SQR SAW RAMP EXP RAND
// filter TYPE. Version 5 (exports C110..C113): 0 OFF, 1 LP2, 2 HP2, 3 LP4 = the manual's order = FM TONE's, 1:1.
// Version 1 (the factory files) has five values 0..4. The owner re-saved three on OS 1.44 (2026-10-14): CHAPPET DL 2 ->
// version 5 2 (HP), DIGIT-ONE 3 -> 3 (LP4), SQB TK 4 -> 3 (LP4; "SQB TK Filter type on the Digitone is LP4"). 0 OFF and
// 1 LP2 are by continuity with version 5 (inferred, no re-saved sound has them)
export const DN_FTYPE_V1 = ["OFF", "LP2", "HP", "LP4", "LP4"];
export const DN_FTYPE_V1_PROVED = [false, false, true, true, true];
const DN_FTYPE_V1_TO_QUAD = [DN_QF.OFF, DN_QF.LP12, DN_QF.HP12, DN_QF.LP24, DN_QF.LP24];

// ---- the envelope times: a Digitone time value 0..127 -> FM TONE's: 1:1. Since 2026-10-14 FM TONE's times are the
// Digitone's, measured on the owner's unit (docs/QUAD.md "Times", tools/digitone/digitone_times.json): a table per
// envelope type (amp attack, amp decay / release, filter attack, filter decay / release, operator attack, operator
// decay), the DELAYs v / 32 beats, the LFO FADE's times. (Before, a power law through two points of a recording mapped
// them onto FM TONE's old 1 ms .. 10 s curve: the Digitone's filter DEC 71 was FM TONE's 97.) A value with hundredths
// (AMP REL) is rounded; a release's 127 is INF on both
export const dnTimeQ = (v) => Math.max(0, Math.min(127, v));
export const DN_TIME_TO_QUAD = Array.from({ length: 128 }, (_, v) => v);

// ---- the sound's OCTAVE (the manual §9.5.10: "Sets the base octave of the Sound"): a signed byte in semitones, version
// 1 offset 263, version 5 offset 280 (the record's tail moved by 17). Inferred: the factory corpus holds only multiples
// of 12 there (0 147, -12 53, -24 29, +12 12, -36 5, +24 5, -60 1; the init sound 0), the owner's 143 exports 0, and SQB
// TK's -12 is the octave its recording sounds below the FM-1's import (2026-10-14). FM TONE has no transpose in the
// patch and a sound load keeps the part's TRN: the import scales every operator's ratio by 2^(octave / 12) instead (the
// same timbre, the octave down), when each ratio has that value in FM TONE's tables (+ OFS, 1/100) within DN_OCT_TOL
const DN_OCT_TOL = 0.003;                                         // 0.3 % (5 cents)
// the LFO DEST numbers (each one an export, C042..C099; the number = the record slot of the target, (offset - 28) / 2)
// -> FM TONE's (eng_quad.c QD_*): every one, 1:1 (since 2026-10-14 FM TONE has the Digitone's whole list: LFO 1's own
// parameters for LFO 2, RATIO ALL, the AB macros, DRIVE, the three sends, the filter's ENV DELAY were added, numbers 40..57;
// its editor lists them in the Digitone's order). A number not in the table (none known): off, with a note
export const DN_LFO_DEST_NAMES = {
  0: "NONE", 1: "LFO1 SPEED", 3: "LFO1 MULT", 5: "LFO1 FADE", 9: "LFO1 WAVE", 11: "LFO1 SPH", 13: "LFO1 MODE", 15: "LFO1 DEPTH",
  17: "ALGO", 18: "RATIO C", 19: "RATIO A", 20: "RATIO B", 21: "HARM", 22: "DETUNE", 23: "FEEDBACK", 24: "MIX",
  27: "A ATTACK", 28: "A DECAY", 29: "A END", 30: "A LEVEL", 31: "B ATTACK", 32: "B DECAY", 33: "B END", 34: "B LEVEL",
  35: "A DELAY", 38: "B DELAY", 42: "PITCH ALL", 43: "PITCH A+B2", 44: "RATIO ALL", 45: "AB LEVEL", 46: "AB ATTACK",
  47: "AB DECAY", 48: "AB END", 49: "AB DELAY", 51: "FILTER FREQ", 52: "FILTER RESO", 53: "FILTER ENV DEPTH",
  54: "FILTER ATTACK", 55: "FILTER DECAY", 56: "FILTER SUSTAIN", 57: "FILTER RELEASE", 58: "FILTER BASE", 59: "FILTER WIDTH",
  60: "AMP ATTACK", 61: "AMP DECAY", 62: "AMP SUSTAIN", 63: "AMP RELEASE", 64: "DRIVE", 65: "PAN", 66: "VOLUME",
  67: "REVERB SEND", 68: "DELAY SEND", 69: "CHORUS SEND", 73: "RATIO C OFFSET", 74: "RATIO A OFFSET", 75: "RATIO B1 OFFSET",
  76: "RATIO B2 OFFSET", 77: "FILTER ENV DELAY",
};
const DN_QD = { NONE: 0, PITCH: 1, PAB2: 2, ALGO: 3, RC: 4, RA: 5, RB: 6, OFSC: 7, OFSA: 8, OFSB1: 9, OFSB2: 10, HARM: 11, DTUN: 12,
  FDBK: 13, MIX: 14, ALEV: 15, BLEV: 16, AATK: 17, ADEC: 18, AEND: 19, BATK: 20, BDEC: 21, BEND: 22, ADLY: 23, BDLY: 24, FREQ: 25,
  RESO: 26, FENV: 27, BASE: 28, WIDTH: 29, FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, EATK: 34, EDEC: 35, ESUS: 36, EREL: 37, LEVEL: 38, PAN: 39,
  L1SPD: 40, L1MUL: 41, L1FAD: 42, L1WAV: 43, L1SPH: 44, L1MOD: 45, L1DEP: 46, RALL: 47, ABLEV: 48, ABATK: 49, ABDEC: 50, ABEND: 51,
  ABDLY: 52, DRIVE: 53, SREV: 54, SDLY: 55, SCHO: 56, FDLY: 57 };
export const DN_LFO_DEST = {
  0: DN_QD.NONE, 1: DN_QD.L1SPD, 3: DN_QD.L1MUL, 5: DN_QD.L1FAD, 9: DN_QD.L1WAV, 11: DN_QD.L1SPH, 13: DN_QD.L1MOD,
  15: DN_QD.L1DEP, 17: DN_QD.ALGO, 18: DN_QD.RC, 19: DN_QD.RA, 20: DN_QD.RB, 21: DN_QD.HARM, 22: DN_QD.DTUN, 23: DN_QD.FDBK,
  24: DN_QD.MIX, 27: DN_QD.AATK, 28: DN_QD.ADEC, 29: DN_QD.AEND, 30: DN_QD.ALEV, 31: DN_QD.BATK, 32: DN_QD.BDEC, 33: DN_QD.BEND,
  34: DN_QD.BLEV, 35: DN_QD.ADLY, 38: DN_QD.BDLY, 42: DN_QD.PITCH, 43: DN_QD.PAB2, 44: DN_QD.RALL,
  45: DN_QD.ABLEV, 46: DN_QD.ABATK, 47: DN_QD.ABDEC, 48: DN_QD.ABEND, 49: DN_QD.ABDLY, 51: DN_QD.FREQ, 52: DN_QD.RESO, 53: DN_QD.FENV,
  54: DN_QD.FATK, 55: DN_QD.FDEC, 56: DN_QD.FSUS, 57: DN_QD.FREL, 58: DN_QD.BASE, 59: DN_QD.WIDTH, 60: DN_QD.EATK, 61: DN_QD.EDEC,
  62: DN_QD.ESUS, 63: DN_QD.EREL, 64: DN_QD.DRIVE, 65: DN_QD.PAN, 66: DN_QD.LEVEL, 67: DN_QD.SREV, 68: DN_QD.SDLY, 69: DN_QD.SCHO,
  73: DN_QD.OFSC, 74: DN_QD.OFSA, 75: DN_QD.OFSB1, 76: DN_QD.OFSB2, 77: DN_QD.FDLY,
};

// ---- the field table: [name, offset, hi-res, confidence, decode(d, o) -> the Digitone's value, apply(value, out), since]
// confidence: "verified" (a controlled export changed exactly this field: offset and encoding proved), "slot" (the offset
// proved by the LFO destination numbering, the encoding inferred), "inferred" (corpus ranges, the manual), "guess",
// "unknown" (decoded for the diff tool, not imported). since / until: the first / last layout that has the field
// a hi-res value: the coarse byte and the fine byte, in hundredths: coarse + round(fine x 100 / 256) / 100
const dnLo = (d, o) => d[o], dnHi = (d, o) => (d[o] * 100 + Math.round(d[o + 1] * 100 / 256)) / 100;
const dnR = Math.round;
const dnTo = (qp, f = (v) => dnR(v)) => (v, out) => out.set(qp, f(v));
const dnTo100 = (qp) => dnTo(qp, (v) => dnR(v * 100));                // a hi-res value 1:1 (hundredths)
const dnLfo = (k) => {
  const c = k ? "verified" : "slot";                             // LFO 2's SPEED / MULT were turned (C100..C109)
  return [
    [`lfo${k + 1}.speed`, 30 + 2 * k, 1, c, (d, o) => dnHi(d, o) - 64, dnTo100(dnQlfo(k, "SPEED"))],   // -64.00..63.99
    [`lfo${k + 1}.mult`, 34 + 2 * k, 0, c, dnLo, dnTo(dnQlfo(k, "MULT"))],    // 0 = 1, 5 = 32, 21..23 = 512 1k 2k: = FM TONE's 24
    [`lfo${k + 1}.fade`, 38 + 2 * k, 0, "slot", (d, o) => d[o] - 64, dnTo(dnQlfo(k, "FADE"))],
    [`lfo${k + 1}.dest`, 42 + 2 * k, 0, "verified", dnLo, (v, out) => {
      const q = DN_LFO_DEST[v];
      out.set(dnQlfo(k, "DEST"), q ?? 0);
      if (q === undefined) out.note(`LFO ${k + 1} destination ${DN_LFO_DEST_NAMES[v] ?? `#${v}`}: FM TONE has none, off`);
    }],
    [`lfo${k + 1}.wave`, 46 + 2 * k, 0, "slot", dnLo, (v, out) => out.set(dnQlfo(k, "WAVE"), DN_WAVE_TO_QUAD[v] ?? 0)],
    [`lfo${k + 1}.phase`, 50 + 2 * k, 0, "slot", dnLo, dnTo(dnQlfo(k, "PHASE"))],
    [`lfo${k + 1}.mode`, 54 + 2 * k, 0, "slot", dnLo, dnTo(dnQlfo(k, "TRIG"))],          // FREE TRG HLD ONE HLF: = FM TONE's
    [`lfo${k + 1}.depth`, 58 + 2 * k, 1, "slot", (d, o) => dnHi(d, o) - 64, dnTo100(dnQlfo(k, "DEPTH"))],
  ];
};
const dnT = (v) => DN_TIME_TO_QUAD[Math.max(0, Math.min(127, dnR(v)))];             // a time value: 1:1
const dnTRel = (v) => v >= 127 ? 127 : dnR(dnTimeQ(v));                                 // a release: 127 INF
const dnTRelA = (v) => v >= 127 ? 127 : Math.min(126, dnR(dnTimeQ(v)));               // AMP REL: 127.xx INF, else <= 126
const dnEnv = (x, base) => ["atk", "dec", "end", "lev"].map((f, i) => [`env${x}.${f}`, base + 2 * i, 0, "verified", dnLo,
  dnTo(DN_QP[`${x}${f.toUpperCase()}`], i < 2 ? dnT : undefined)]);
const dnLost = (what) => (v, out) => { if (v > 0) out.note(`${what} ${v.toFixed(2)} not imported: effects are global on the FM-1`); };
// a ratio offset (version 5 only, 64 = 0; its scale is not proved: +-64 read as +-1.00, FM TONE's OFS -100..100)
const dnOfs = (qp, what) => (v, out) => { out.set(qp, dnR(v * 100 / 64)); if (dnR(v * 100 / 64)) out.note(`${what} offset read as ${(v / 64).toFixed(2)} (its scale is not proved)`); };
export const DNSND_FIELDS = [
  ...dnLfo(0), ...dnLfo(1),
  ["algo", 62, 0, "slot", (d, o) => d[o] + 1, dnTo(DN_QP.ALGO)],                             // 0..7 -> ALGO 1..8
  ["ratioC", 64, 0, "verified", (d, o) => DN_RATIO_CB[Math.min(18, d[o])], (v, out) => out.set(DN_QP.RC, Math.max(0, DN_RATIO_CB.indexOf(v)))],
  ["ratioA", 66, 0, "verified", (d, o) => DN_RATIO_A[Math.min(35, d[o])], (v, out) => {
    out.set(DN_QP.RA, dnR(v * 4) - 1);                                                         // FM TONE: 0.25 .. 16 in quarters
    const i = DN_RATIO_A.indexOf(v);
    if (i > 15 && !DN_RATIO_A_ANCHORS.includes(i)) out.note(`RATIO A step ${i} read as ${v.toFixed(2)} (between the proved steps)`);
  }],
  // RATIO B: a u16be at 68 = 2 x (B1 step x 19 + B2 step) + a low bit (not known; ignored). The value is FM TONE's own
  // RATIO B pair (QP_RB1 x 19 + QP_RB2: B2 the fast hand, the manual's §11.3.4)
  ["ratioB", 68, 1, "verified", (d, o) => Math.min(360, (d[o] << 8 | d[o + 1]) >> 1), (v, out) => {
    out.set(DN_QP.RB1, Math.floor(v / 19)); out.set(DN_QP.RB2, v % 19);
  }],
  ["harm", 70, 1, "verified", (d, o) => dnHi(d, o) - 63, dnTo100(DN_QP.HARM)],                 // 37 = -26, 63 = 0, 89 = +26
  ["dtun", 72, 1, "verified", dnHi, dnTo100(DN_QP.DTUN)],
  ["fdbk", 74, 0, "verified", dnLo, dnTo(DN_QP.FDBK)],                                         // 0..127 (MAX = 127) = FM TONE's
  ["mix", 76, 0, "verified", (d, o) => d[o] - 64, dnTo(DN_QP.MIX)],                            // 0 = -64, 64 = 0, 127 = +63
  ["phrt", 78, 0, "inferred", dnLo, dnTo(DN_QP.PHRT)],                                         // 0..4 OFF ALL C A+B A+B2 = FM TONE's
  ...dnEnv("A", 82), ...dnEnv("B", 90),
  ["envA.delay", 98, 0, "slot", dnLo, dnTo(DN_QP.ADLY)],
  ["envA.trig", 100, 0, "inferred", dnLo, dnTo(DN_QP.ATRIG)],
  ["envA.reset", 102, 0, "inferred", dnLo, dnTo(DN_QP.ARST)],
  ["envB.delay", 104, 0, "slot", dnLo, dnTo(DN_QP.BDLY)],
  ["envB.trig", 106, 0, "inferred", dnLo, dnTo(DN_QP.BTRIG)],
  ["envB.reset", 108, 0, "inferred", dnLo, dnTo(DN_QP.BRST)],
  ["unk110", 110, 0, "unknown", dnLo, null],
  ["filter.type", 128, 0, "verified", dnLo, (v, out) => {
    if (out.version >= 5) { out.set(DN_QP.FTYPE, v); return; }                                 // version 5: 1:1
    const q = DN_FTYPE_V1_TO_QUAD[v] ?? DN_QF.LP12, n = DN_FTYPE_V1[v] ?? `#${v}`;
    out.set(DN_QP.FTYPE, q);
    // 0 / 1 (inferred, DN_FTYPE_V1_PROVED) are 174 of the 256 factory sounds: no note; a value past 4 is unknown
    if (DN_FTYPE_V1[v] === undefined) out.note(`filter type ${n} (a version-1 sound: not a known value) -> ${["OFF", "LP12", "HP12", "LP24"][q]}`);
  }],
  ["filter.freq", 130, 1, "verified", dnHi, dnTo100(DN_QP.FREQ)],
  ["filter.reso", 132, 1, "verified", dnHi, dnTo100(DN_QP.RESO)],
  ["filter.env", 134, 1, "verified", (d, o) => dnHi(d, o) - 64, dnTo100(DN_QP.FDEPTH)],
  ["filter.atk", 136, 0, "verified", dnLo, dnTo(DN_QP.FATK, dnT)],
  ["filter.dec", 138, 0, "verified", dnLo, dnTo(DN_QP.FDEC, dnT)],                             // 127 = INF (dnsndToQuad)
  ["filter.sus", 140, 0, "verified", dnLo, dnTo(DN_QP.FSUS)],
  ["filter.rel", 142, 0, "verified", dnLo, dnTo(DN_QP.FREL, dnTRel)],                          // 127 = INF = FM TONE's
  ["filter.base", 144, 1, "slot", dnHi, dnTo100(DN_QP.BASE)],
  ["filter.width", 146, 0, "slot", dnLo, dnTo(DN_QP.WIDTH)],
  ["amp.atk", 148, 0, "verified", dnLo, dnTo(DN_QP.EATK, dnT)],
  ["amp.dec", 150, 0, "verified", dnLo, dnTo(DN_QP.EDEC, (v) => v >= 127 ? 127 : dnT(v))],    // 127 = INF = FM TONE's
  ["amp.sus", 152, 0, "verified", dnLo, dnTo(DN_QP.ESUS)],
  ["amp.rel", 154, 1, "verified", dnHi, dnTo(DN_QP.EREL, dnTRelA)],                            // 127 = INF = FM TONE's
  ["amp.drive", 156, 1, "verified", dnHi, (v, out) => { out.drive = Math.max(0, Math.min(127, dnR(v))); }],
  ["amp.pan", 158, 0, "verified", (d, o) => d[o] - 64, (v, out) => { out.pan = Math.max(-64, Math.min(63, v)); }],
  ["amp.vol", 160, 1, "verified", dnHi, dnTo(DN_QP.LEVEL)],                                   // a whole number
  ["send.reverb", 162, 1, "verified", dnHi, dnLost("reverb send")],
  ["send.delay", 164, 1, "verified", dnHi, dnLost("delay send")],
  ["send.chorus", 166, 1, "verified", dnHi, dnLost("chorus send")],
  ["amp.reset", 168, 0, "verified", dnLo, (v, out) => { if (out.version >= 5 && v === 0) out.note("AMP ENV RESET off not imported (FM TONE's amp envelope always resets)"); }],
  ["unk170", 170, 0, "unknown", dnLo, null, 5],
  ["unk172", 172, 0, "unknown", dnLo, null, 5],
  ["ofsC", 174, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSC, "RATIO C"), 5],
  ["ofsA", 176, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSA, "RATIO A"), 5],
  ["ofsB1", 178, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSB1, "RATIO B1"), 5],
  ["ofsB2", 180, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSB2, "RATIO B2"), 5],
  ["filter.delay", 182, 0, "slot", dnLo, dnTo(DN_QP.FDLY), 5],
  ["octave", 263, 0, "inferred", (d, o) => (d[o] << 24) >> 24, null, 1, 1],                  // version 1 (dnsndToQuad)
  ["octave", 280, 0, "inferred", (d, o) => (d[o] << 24) >> 24, null, 5],                     // version 5
];

// ---- the container
const DN_HDR = 0x1f;
const dnU16 = (b, o) => b[o] | b[o + 1] << 8, dnU32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const dnU16be = (b, o) => b[o] << 8 | b[o + 1], dnU32be = (b, o) => (b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]) >>> 0;
const DN_NOT = "not a Digitone sound file";
// a Digitone II sound (its own 359-byte record: other parameter numbers, the machine bytes; FIZZ reads them)
const DN_DN2 = "a Digitone II sound: its parameters are numbered differently, this importer reads Digitone sounds";
// a raw deflate stream -> its bytes: the browser's DecompressionStream, else node's zlib
export async function dnInflateRaw(bytes) {
  if (typeof DecompressionStream !== "undefined") {
    const s = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  const zlib = await import("node:zlib");
  return new Uint8Array(zlib.inflateRawSync(bytes));
}
// the ZIP's entries (the central directory): name -> Promise<bytes>
export async function dnReadZip(b) {
  let e = -1;
  for (let i = b.length - 22; i >= Math.max(0, b.length - 22 - 65535); i--) if (dnU32(b, i) === 0x06054b50) { e = i; break; }
  if (e < 0) throw new Error(`${DN_NOT} (not a ZIP archive)`);
  const n = dnU16(b, e + 10), out = new Map();
  let p = dnU32(b, e + 16);
  for (let k = 0; k < n; k++) {
    if (p + 46 > b.length || dnU32(b, p) !== 0x02014b50) throw new Error(`${DN_NOT} (a damaged ZIP directory)`);
    const method = dnU16(b, p + 10), cs = dnU32(b, p + 20), nl = dnU16(b, p + 28), xl = dnU16(b, p + 30), cl = dnU16(b, p + 32), lh = dnU32(b, p + 42);
    const name = new TextDecoder().decode(b.subarray(p + 46, p + 46 + nl));
    if (lh + 30 > b.length || dnU32(b, lh) !== 0x04034b50) throw new Error(`${DN_NOT} (a damaged ZIP entry ${name})`);
    const start = lh + 30 + dnU16(b, lh + 26) + dnU16(b, lh + 28), data = b.subarray(start, start + cs);
    if (data.length !== cs) throw new Error(`${DN_NOT} (the ZIP entry ${name} is cut short)`);
    if (method !== 0 && method !== 8) throw new Error(`${DN_NOT} (ZIP method ${method})`);
    out.set(name, method === 8 ? data : data.slice());
    out.get(name).deflated = method === 8;
    p += 46 + nl + xl + cl;
  }
  return { names: [...out.keys()], get: async (name) => { const d = out.get(name); return d && (d.deflated ? dnInflateRaw(d) : d); } };
}
// an LZ4 block from src[p] (ending at stop) -> want bytes (the payload's compression); o / n: a chain's output so far
// (its blocks share one dictionary)
export function dnLz4Block(src, p, want, stop = src.length, o = new Uint8Array(want), n = 0) {
  const byte = () => { if (p >= stop) throw new Error(`${DN_NOT} (the sound data is cut short)`); return src[p++]; };
  while (n < want) {
    const t = byte(); let l = t >> 4;
    if (l === 15) { let b; do { b = byte(); l += b; } while (b === 255); }
    if (n + l > want) throw new Error(`${DN_NOT} (the sound data does not decompress)`);
    for (let i = 0; i < l; i++) o[n++] = byte();
    if (n >= want || p >= stop) break;
    const off = byte() | byte() << 8;
    let m = (t & 15) + 4;
    if ((t & 15) === 15) { let b; do { b = byte(); m += b; } while (b === 255); }
    if (!off || off > n || n + m > want) throw new Error(`${DN_NOT} (the sound data does not decompress)`);
    for (let i = 0; i < m; i++, n++) o[n] = o[n - off];
  }
  return { data: o, end: p, n };
}

// a .dnsnd's bytes -> the sound (throws with a clear message on anything that is not a Digitone sound)
export async function parseDnsnd(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes || []);
  if (bytes.length < 4 || dnU32(bytes, 0) !== 0x04034b50) throw new Error(`${DN_NOT} (not a ZIP archive)`);
  const zip = await dnReadZip(bytes), warnings = [];
  const mt = await zip.get("manifest.json");
  if (!mt) throw new Error(`${DN_NOT} (no manifest.json)`);
  let man;
  try { man = JSON.parse(new TextDecoder().decode(mt)); } catch (_) { throw new Error(`${DN_NOT} (manifest.json is not JSON)`); }
  if (!man || man.FileType !== "Sound") throw new Error(`${DN_NOT} (FileType ${man && JSON.stringify(man.FileType)}, not "Sound")`);
  const pl = typeof man.Payload === "string" && await zip.get(man.Payload);
  if (!pl) throw new Error(`${DN_NOT} (the payload ${JSON.stringify(man.Payload)} is missing)`);
  if (pl.length < DN_HDR + 12 || dnU32(pl, 0) !== 0x03d311ac) throw new Error(`${DN_NOT} (the payload's magic)`);
  // the body's length and its compression from the header (before 3y201: a u16le at 0x1c, the length's low byte and the
  // flag, 302 by luck); any length is read (dnRecord takes what its layout covers). The build at 9 is the OS that saved it
  const size = dnU32be(pl, 0x19), packed = pl[0x1d];
  if (dnU16be(pl, 7) === 15) throw new Error(DN_DN2);
  if (size < 28 || size > 0x8000 || packed > 1) throw new Error(`${DN_NOT} (the payload's header: a ${size}-byte body${packed > 1 ? `, compression ${packed}` : ""})`);
  let d, end = DN_HDR + size;
  if (packed) {                                   // an LZ4 chain: u32be size, the block, .., u32be 0
    d = new Uint8Array(size);
    let n = 0;
    for (end = DN_HDR; ;) {
      if (end + 4 > pl.length) throw new Error(`${DN_NOT} (the sound data is cut short)`);
      const sz = dnU32be(pl, end); end += 4;
      if (!sz) break;
      if (end + sz > pl.length) throw new Error(`${DN_NOT} (the sound data is cut short)`);
      n = dnLz4Block(pl, end, size, end + sz, d, n).n; end += sz;
    }
    if (n < size) throw new Error(`${DN_NOT} (the sound data is cut short)`);
  } else if (end > pl.length) throw new Error(`${DN_NOT} (the sound data is cut short)`);
  else d = pl.slice(DN_HDR, end);
  const tail = pl.subarray(end);
  if (tail.length < 4 || dnU32(tail, tail.length - 4) !== 0xaadaa1aa) warnings.push("the payload's trailer is not the usual one");
  if (man.FirmwareVersion !== "1.42A") warnings.push(`made on Digitone firmware ${man.FirmwareVersion}: read as 1.42A`);
  if (JSON.stringify(man.ProductType) !== '["24","30"]') warnings.push(`product ${JSON.stringify(man.ProductType)}: read as a Digitone`);
  const tags = Array.isArray(man.MetaInfo && man.MetaInfo.Tags) ? man.MetaInfo.Tags.map(String) : [];
  return dnRecord(d, warnings, { tags, firmware: man.FirmwareVersion ?? null, product: man.ProductType ?? null });
}
// the record (from a .dnsnd or a SysEx dump; 302 bytes in version 5, 283 in a version-1 dump, any length read) -> the
// parsed sound; version: the layout's (dnsndToQuad's laws go by it), fileVersion: the record's own. A version not known
// is read with the newest layout (DN_NEWEST); a record cut short (no end marker, shorter than its layout): the fields it
// holds, the others left at FM TONE's defaults; either with a note. Only a record that is not a Digitone sound is refused
function dnRecord(d, warnings, meta) {
  if (d.length >= 9 && d[0] === 0 && d[5] === 0xbe && d[6] === 0xef && d[7] === 0xba && d[8] === 0xce) throw new Error(DN_DN2);
  if (d.length < 28 || !(d[0] === 0xbe && d[1] === 0xef && d[2] === 0xba && d[3] === 0xce)) throw new Error(`${DN_NOT} (the sound's magic)`);
  const version = (d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7]) >>> 0, known = DN_LAYOUT[version] !== undefined;
  const layout = known ? DN_LAYOUT[version] : DN_NEWEST;
  if (!known) warnings.push(`a version-${version} sound (${version > Math.max(...DN_VERSIONS) ? "newer than" : "not one of"} the versions known, ${DN_VERSIONS.join(" ")}): read as version ${layout}; fields the FM-1 does not know were skipped`);
  else if (layout !== version) warnings.push(`a version-${version} sound: read as version ${layout} (the same layout)`);
  // where the record's own bytes end: its end marker (the tail read from it), else the layout's when it is that long
  let mark = -1;
  for (let i = d.length - 4; i >= DN_TAIL && mark < 0; i--) if (d[i] === 0xba && d[i + 1] === 0xce && d[i + 2] === 0xf0 && d[i + 3] === 0x0c) mark = i;
  const whole = mark >= 0 || d.length >= DN_END[layout] + 4, size = mark >= 0 ? mark : whole ? DN_END[layout] : d.length;
  const shift = mark >= 0 ? mark - DN_END[layout] : 0;
  let name = "";
  for (let i = 12; i < 28 && d[i]; i++) name += String.fromCharCode(d[i]);
  const params = {}, confidence = {}, missing = [];
  for (const [k, o, hi, c, dec, , since = 1, until = DN_NEWEST] of DNSND_FIELDS) {
    if (layout < since || layout > until) continue;
    const at = o >= DN_TAIL ? o + shift : o;
    if ((o >= DN_TAIL && !whole) || at + (hi ? 2 : 1) > size) { if (c !== "unknown") missing.push(k); continue; }
    params[k] = +dec(d, at).toFixed(2); confidence[k] = c;
  }
  if (missing.length) warnings.push(`the sound is cut short (${d.length} bytes${mark < 0 ? ", no end marker" : ""}): ${missing.length} of its settings not in it, FM TONE's defaults instead`);
  return { name, tags: [], firmware: null, product: null, ...meta, version: layout, fileVersion: version, params, confidence, warnings, record: d };
}

// ---- Digitone SysEx dumps (.syx: one sound, the pool's 128, or a whole project's dump). Decoded from the files (the
// Dissonant World pack's DW_Play.syx, 2018: 128 messages of 339 bytes, each the same sound as the pack's .dnsnd):
//   F0 00 20 3C (Elektron) 0D (Digitone) 00 53 (a sound) 01 01 nn | the data, 8:7 packed (each group of 8: a byte of
//   the next seven's top bits, bit 6 first, then the seven) | the checksum (the packed data's byte sum & 0x3FFF, 7 + 7
//   bits) | the length (the message's bytes after nn up to F7: 7 + 7 bits) | F7
// The data is the .dnsnd's record, without the LZ4: a version-1 record is 283 bytes (its end marker ba ce f0 0c at 279,
// as in the factory .dnsnd), a version-3 one 282 (a 2018 dump), read as dnRecord reads any; a message cut
// short (Revealed.syx holds ten of 113 bytes, their sum and length wrong) gives what it holds, with a note. Other messages (kits, patterns, songs, settings: a project's dump) hold
// no sound of their own here: skipped and counted. The kits' sounds are not read (their layout is not decoded)
export const isDnSyx = (b) => b && b.length > 5 && b[0] === 0xF0 && b[1] === 0x00 && b[2] === 0x20 && b[3] === 0x3C && b[4] === 0x0D;
export const DN_SYX_SOUND = 0x53;
export const dnUnpack87 = (e) => {
  const o = [];
  for (let i = 0; i < e.length; i += 8) for (let j = 1; j < 8 && i + j < e.length; j++) o.push(e[i + j] | (e[i] << j & 0x80));
  return Uint8Array.from(o);
};
export const dnPack87 = (d) => {
  const o = [];
  for (let i = 0; i < d.length; i += 7) {
    const g = d.subarray(i, i + 7);
    o.push(g.reduce((m, x, j) => m | (x >> 7) << 6 - j, 0), ...Array.from(g, (x) => x & 0x7F));
  }
  return Uint8Array.from(o);
};
// a Digitone .syx's bytes -> { sounds: [parsed, as parseDnsnd's, + slot: the message's nn], notes: the file's }; throws
// when it is not a Digitone dump or holds no sound
export function parseDnSyx(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes || []);
  if (!isDnSyx(bytes)) throw new Error("not a Digitone SysEx dump (no F0 00 20 3C 0D)");
  const sounds = [], notes = [], other = {};
  let bad = 0;
  for (let i = 0; i < bytes.length;) {
    if (bytes[i] !== 0xF0) { i++; continue; }
    let e = i + 1;
    while (e < bytes.length && bytes[e] !== 0xF7) e++;
    const m = bytes.subarray(i, e + 1), n = sounds.length + bad + Object.values(other).reduce((a, x) => a + x, 0) + 1;
    i = e + 1;
    if (e >= bytes.length || m.length < 15 || !isDnSyx(m)) { bad++; notes.push(`message ${n}: not a Digitone message, skipped`); continue; }
    if (m[6] !== DN_SYX_SOUND) { other[m[6]] = (other[m[6]] || 0) + 1; continue; }
    const data = m.subarray(10, m.length - 5), w = [];
    const sum = data.reduce((a, x) => a + x, 0) & 0x3FFF, len = m[m.length - 3] << 7 | m[m.length - 2];
    if ((m[m.length - 5] << 7 | m[m.length - 4]) !== sum) w.push("the message's checksum does not match: read as it is");
    if (len !== m.length - 10) w.push("the message's length field does not match: read as it is");
    if (m[7] !== 1 || m[8] !== 1) w.push(`a sound message of version ${m[7]}.${m[8]}: read as 1.1`);
    try { sounds.push({ ...dnRecord(dnUnpack87(data), w, {}), slot: m[9] }); } catch (x) { bad++; notes.push(`message ${n}: ${x.message}`); }
  }
  const kinds = { 0x52: "kit", 0x54: "pattern", 0x55: "song", 0x56: "settings", 0x57: "global" };
  for (const [k, c] of Object.entries(other)) notes.push(`${c} ${kinds[k] || `type ${(+k).toString(16).toUpperCase()}`} message${c === 1 ? "" : "s"} skipped (only the sounds are read; a kit's own sounds are not)`);
  if (!sounds.length) throw new Error(`no Digitone sound in this SysEx dump${notes.length ? ` (${notes.join("; ")})` : ""}`);
  return { sounds, notes };
}

// the sound's OCTAVE (semitones, a multiple of 12) into FM TONE values p: each operator's ratio (its table step + OFS
// / 100) x 2^(oct / 12), as a step + OFS within DN_OCT_TOL; all four or none (true: done). A step's OFS is -100..100
const DN_RA_Q = Array.from({ length: 64 }, (_, i) => (i + 1) / 4);
function dnOctave(p, oct) {
  const f = 2 ** (oct / 12), rb1 = p[DN_QP.RB1], rb2 = p[DN_QP.RB2];
  const ops = [[DN_RATIO_CB, DN_QP.RC, DN_QP.OFSC, p[DN_QP.RC]], [DN_RA_Q, DN_QP.RA, DN_QP.OFSA, p[DN_QP.RA]],
    [DN_RATIO_CB, DN_QP.RB1, DN_QP.OFSB1, rb1], [DN_RATIO_CB, DN_QP.RB2, DN_QP.OFSB2, rb2]];
  const set = [];
  for (const [tab, qs, qo, step] of ops) {
    const want = (tab[step] + p[qo] / 100) * f;
    let best = null;
    tab.forEach((x, i) => {
      const o = Math.max(-100, Math.min(100, dnR((want - x) * 100))), got = x + o / 100;
      const e = Math.abs(got - want) / want;
      if (got > 0 && e <= DN_OCT_TOL && (!best || Math.abs(o) < Math.abs(best[1]) || (Math.abs(o) === Math.abs(best[1]) && e < best[2]))) best = [i, o, e];
    });
    if (!best) return false;
    set.push([qs, qo, best[0], best[1]]);
  }
  for (const [qs, qo, i, o] of set) { p[qs] = i; p[qo] = o; }
  return true;
}

// the parsed sound (or its params: read as version 1) -> FM TONE's blob, the track PAN and DRIVE (P_PAN, P_DIST: not in
// the patch), and what was lost or approximated
export function dnsndToQuad(parsed) {
  const params = parsed && parsed.params ? parsed.params : parsed;
  const p = DN_QUAD_RANGES.map((r) => r[2]), notes = [];
  const out = {
    pan: 0, drive: 0, version: (parsed && parsed.params && parsed.version) || 1,
    note: (s) => notes.push(s),
    set: (i, v) => {
      const [mn, mx] = DN_QUAD_RANGES[i], c = Math.max(mn, Math.min(mx, Number.isFinite(v) ? v : DN_QUAD_RANGES[i][2]));
      p[i] = c;
    },
  };
  for (const [k, , , c, , apply] of DNSND_FIELDS) if (apply && c !== "unknown" && params[k] !== undefined) apply(params[k], out);
  // the Digitone's filter and amp DEC 127 is INF (the owner's exports C126, C135): the level holds after the attack.
  // FM TONE's AMP DEC has INF (127, version 5: taken as it is); its filter DEC has none: the filter's SUS at 127
  if (params["filter.dec"] === 127) { p[DN_QP.FSUS] = 127; notes.push("filter DEC INF: FM TONE's filter SUS set to 127"); }
  // the Digitone's DRIVE sits before its filter; the FM-1's (the part's P_DIST, fx.c track_dist) after the voice, its
  // filter included. FIZZ moved it in front (its DRIVE ROUTE PRE, an engine change ChoralRoot has not taken): a note
  if (out.drive > 0) notes.push(`DRIVE ${out.drive}: the FM-1 drives after the filter (the Digitone before it)`);
  const oct = params.octave | 0;
  if (oct) {
    const r = dnOctave(p, oct);
    notes.push(r ? `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12}: every ratio x ${2 ** (oct / 12)}` :
      `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12} not imported (a ratio has no such value in FM TONE): it plays ${Math.abs(oct / 12)} octave(s) ${oct > 0 ? "lower" : "higher"} than on the Digitone`);
  }
  return { blob: dnQuadPack(p), values: p, pan: out.pan, drive: out.drive, notes };
}

// FM TONE's values (version 5) -> its blob: 'Q' 5, value - min a byte, the hi-res ones u16le, zeros (eng_quad.c quad_pack)
export function dnQuadPack(p) {
  const blob = new Uint8Array(DN_QUAD_BLOB);
  let o = 2;
  blob[0] = 0x51; blob[1] = DN_QUAD_VER;
  for (let i = 0; i < DN_QUAD_NP; i++) {
    const u = p[i] - DN_QUAD_RANGES[i][0];
    blob[o++] = u & 255;
    if (dnHiRes(i)) blob[o++] = u >> 8;
  }
  return blob;
}
// version 3's ranges (eng_quad.c quad_range_v3): the hi-res ones in whole numbers, FDBK 0..120
const dnRangeV3 = (i) => {
  const [mn, mx] = DN_QUAD_RANGES_V4[i];
  return dnHiResV4(i) ? [Math.trunc(mn / 100), Math.trunc(mx / 100)] : i === DN_QP.FDBK ? [0, 120] : [mn, mx];
};
// a blob's values in the current version's units (version 5, 4 or 3; null: another)
function dnQuadValues(blob) {
  const p = [];
  if (blob[1] === 4 || blob[1] === 5) {
    const v4 = blob[1] === 4;
    let o = 2;
    for (let i = 0; i < DN_QUAD_NP; i++) {
      let u = blob[o++];
      if (v4 ? dnHiResV4(i) : dnHiRes(i)) u |= blob[o++] << 8;
      const v = u + (v4 ? DN_QUAD_RANGES_V4 : DN_QUAD_RANGES)[i][0];
      p.push(v4 ? dnV5OfV4(i, v) : v);
    }
    return p;
  }
  if (blob[1] !== 3) return null;
  for (let i = 0; i < DN_QUAD_NP; i++) {
    const [mn, mx] = dnRangeV3(i), v = Math.min(mx, blob[2 + i] + mn);
    const w = dnHiResV4(i) ? v * 100 : i === DN_QP.FDBK ? Math.trunc((v * 127 + 60) / 120) : v;
    p.push(dnV5OfV4(i, Math.max(DN_QUAD_RANGES_V4[i][0], Math.min(DN_QUAD_RANGES_V4[i][1], w))));
  }
  return p;
}
export function quadToV4(blob) {
  if (!(blob instanceof Uint8Array) || blob[0] !== 0x51) return blob;
  if (blob.length === DN_QUAD_BLOB && blob[1] === DN_QUAD_VER) return blob;
  if (blob.length === DN_QUAD_BLOB && blob[1] === 4) return dnQuadPack(dnQuadValues(blob));
  if (blob.length !== DN_QUAD_BLOB_V3 || blob[1] < 1 || blob[1] > 3) return blob;
  if (blob[1] !== 3) { const b = new Uint8Array(DN_QUAD_BLOB); b.set(blob); return b; }
  return dnQuadPack(dnQuadValues(blob));
}
export const quadToV5 = quadToV4;
// a version-4 / 5 patch -> version 3's 80 bytes (for a firmware before the pool, whose QUAD store holds versions 1..3):
// the hi-res values rounded to whole numbers, FDBK 0..127 -> 0..120; an LFO destination of the 18 added in 0.22
// (40..57: ChoralRoot 0.21's quad_blob_ok refuses the whole patch over one of them, and it would play the init sound) ->
// NONE; a version 1..3 blob as it is. notes (an array, optional): what the older firmware will not have, one line each,
// as the importer's notes
export const DN_QD_V3 = 40;                       // destinations 0..39: the ones ChoralRoot 0.21 knows
const dnQdName = (q) => { const v = Object.keys(DN_LFO_DEST).find((k) => DN_LFO_DEST[k] === q); return v === undefined ? `#${q}` : DN_LFO_DEST_NAMES[v]; };
export function quadToV3(blob, notes = []) {
  if (!(blob instanceof Uint8Array) || blob.length !== DN_QUAD_BLOB || blob[0] !== 0x51) return blob;
  if (blob[1] !== 4 && blob[1] !== 5) return blob.slice(0, DN_QUAD_BLOB_V3);
  const p = dnQuadValues(blob), b = new Uint8Array(DN_QUAD_BLOB_V3);
  b[0] = 0x51; b[1] = 3;
  let fine = 0;
  for (let k = 0; k < 3; k++) {
    const d = dnQlfo(k, "DEST");
    if (p[d] >= DN_QD_V3) { notes.push(`LFO ${k + 1} destination ${dnQdName(p[d])}: ChoralRoot 0.21 has none, off`); p[d] = DN_QD.NONE; }
  }
  if (p[DN_QP.EDEC] >= 127) notes.push("AMP DEC INF: ChoralRoot 0.21 has no INF there, its longest decay instead");
  for (let i = 0; i < DN_QUAD_NP; i++) {
    const v = p[i], [mn, mx] = dnRangeV3(i);
    if (dnHiRes(i) && v % 100) fine++;
    const w = dnHiRes(i) ? Math.round(v / 100) : i === DN_QP.FDBK ? Math.trunc((v * 120 + 63) / 127) : v;
    b[2 + i] = Math.max(mn, Math.min(mx, w)) - mn;
  }
  if (fine) notes.push(`${fine} value${fine === 1 ? "" : "s"} with decimals rounded to whole numbers (ChoralRoot 0.21 has no decimals)`);
  return b;
}
// the macro value P_E0 + k of FM TONE value v (eng_quad.c quad_mac_val: HARM, DTUNE their coarse part)
export const dnQuadMacro = (i, v) => (dnHiRes(i) ? Math.trunc(v / 100) : v);
