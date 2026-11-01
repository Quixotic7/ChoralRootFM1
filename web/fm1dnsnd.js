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
//   parseDnsnd(bytes)   -> Promise<{name, tags, firmware, product, version, params, confidence, warnings, record}>; a
//                          Digitone II .dn2pst / .dn2snd (3y210, "Digitone II sounds" below): + model 2, the machines
//   dnsndToQuad(parsed) -> {blob: Uint8Array(96) FM TONE version 5 or 6, pan, drive, sends, notes: [what was lost /
//                          approximated]}; a Digitone II sound's also glide and mods (the record's GLIDE and matrix)
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
export const DN_QUAD_RANGES = [[1,8,1],[0,18,3],[0,63,3],[0,18,3],[-2600,2600,0],[0,12700,0],[0,127,0],[-64,63,-64],[-100,100,0],[-100,100,0],[-100,100,0],[-100,100,0],[0,127,0],[0,127,32],[0,127,127],[0,127,0],[0,127,0],[0,127,32],[0,127,127],[0,127,0],[0,127,0],[0,1,1],[0,1,1],[0,4,1],[0,127,0],[0,1,1],[0,1,1],[0,127,64],[0,127,0],[0,127,0],[0,127,0],[0,127,64],[0,127,0],[0,127,64],[0,12700,12700],[0,12700,0],[0,3,1],[-6400,6399,0],[0,127,0],[0,127,0],[0,12700,0],[0,127,127],[0,127,8],[0,127,32],[0,127,96],[0,127,24],[0,127,110],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[-6400,6399,3200],[0,23,5],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-6400,6399,0],[0,18,3],[0,127,0],
  [0,1,0],[0,127,127],[0,1,0],[0,127,0],[-64,63,0],[0,1,0],[0,127,0],[0,127,0],[0,1,0],[0,1,0]];
export const DN_QP = { ALGO: 0, RC: 1, RA: 2, RB1: 3, HARM: 4, DTUN: 5, FDBK: 6, MIX: 7, OFSC: 8, OFSA: 9, OFSB1: 10, OFSB2: 11,
  AATK: 12, ADEC: 13, AEND: 14, ALEV: 15, BATK: 16, BDEC: 17, BEND: 18, BLEV: 19, ADLY: 20, ATRIG: 21, ARST: 22, PHRT: 23,
  BDLY: 24, BTRIG: 25, BRST: 26, VEL: 27, AKEY: 28, B1KEY: 29, FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, FREQ: 34, RESO: 35, FTYPE: 36, FDEPTH: 37,
  FDLY: 38, FKTRK: 39, BASE: 40, WIDTH: 41, EATK: 42, EDEC: 43, ESUS: 44, EREL: 45, LEVEL: 46, RB2: 71, B2KEY: 72,
  // version 6 (eng_quad.c QP_AMOD .., 2026-10-31: FIZZ's engine values; their defaults are today's sound)
  AMOD: 73, HOLD: 74, DRPRE: 75, FSHAPE: 76, FGAIN: 77, BWPOST: 78, BITS: 79, SRR: 80, CRPRE: 81, SENDS: 82 };
const DN_QL = { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 };
const dnQlfo = (k, f) => 47 + k * 8 + DN_QL[f];
export const DN_QUAD_NP = 73, DN_QUAD_NP6 = 83, DN_QUAD_BLOB = 96, DN_QUAD_BLOB_V3 = 80;
// version 6's FTYPE top: the machines MULTI 4, COMB+ 5, COMB- 6, EQ 7 (FIZZ's QD_QF_*; a version-5 blob holds 0..3)
export const DN_QUAD_FT6 = 7;
export const DN_QF6 = { OFF: 0, LP12: 1, HP12: 2, LP24: 3, MULTI: 4, COMBP: 5, COMBN: 6, EQ: 7 };
// the hi-res values (eng_quad.c quad_hires): two bytes each in the blob (u16le, value - min); version 4's: + AMP REL, LEVEL
export const DN_QUAD_HIRES = [4, 5, 34, 35, 37, 40, 47, 54, 55, 62, 63, 70];
export const DN_QUAD_HIRES_V4 = [4, 5, 34, 35, 37, 40, 45, 46, 47, 54, 55, 62, 63, 70];
const dnHiRes = (i) => DN_QUAD_HIRES.includes(i);
const dnHiResV4 = (i) => DN_QUAD_HIRES_V4.includes(i);
export const DN_QUAD_VER = 6;                     // the newest read and written (a sound without a version-6 value: 5)
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
export const DN_QD = { NONE: 0, PITCH: 1, PAB2: 2, ALGO: 3, RC: 4, RA: 5, RB: 6, OFSC: 7, OFSA: 8, OFSB1: 9, OFSB2: 10, HARM: 11, DTUN: 12,
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
// a send (reverb, delay, chorus): the sound's own (version 6's SENDS: an FM TONE load sets the part's sends to its
// record's P_REV P_DLY P_CHOR, cr_ui.c cu_sends_load); a whole number 0..127
const dnSend = (k) => (v, out) => { out.sends[k] = Math.max(0, Math.min(127, dnR(v))); };
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
  ["send.reverb", 162, 1, "verified", dnHi, dnSend("reverb")],
  ["send.delay", 164, 1, "verified", dnHi, dnSend("delay")],
  ["send.chorus", 166, 1, "verified", dnHi, dnSend("chorus")],
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
const dnMagic = (b, o, m) => o + 4 <= b.length && b[o] === m[0] && b[o + 1] === m[1] && b[o + 2] === m[2] && b[o + 3] === m[3];
const DN_BEEF = [0xbe, 0xef, 0xba, 0xce], DN_BOX = [0xac, 0x11, 0xd3, 0x03];
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

// a .dnsnd's bytes -> the sound (throws with a clear message on anything that is not a Digitone sound). Also a Digitone
// II file (3y210): a .dn2pst (the same ZIP, the container's device 15) or a .dn2snd (the container alone, as a +Drive
// backup keeps it): parsed.model 2, read by dn2Parse; dnsndToQuad takes either
export async function parseDnsnd(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes || []);
  const warnings = [];
  let man = null, pl = bytes;
  if (!dnMagic(bytes, 0, DN_BOX)) {
    if (bytes.length < 4 || dnU32(bytes, 0) !== 0x04034b50) throw new Error(`${DN_NOT} (not a ZIP archive)`);
    const zip = await dnReadZip(bytes);
    const mt = await zip.get("manifest.json");
    if (!mt) throw new Error(`${DN_NOT} (no manifest.json)`);
    try { man = JSON.parse(new TextDecoder().decode(mt)); } catch (_) { throw new Error(`${DN_NOT} (manifest.json is not JSON)`); }
    // (a Digitone II's Transfer may say "Preset": FIZZ reads it as a sound, so does this)
    if (!man || (man.FileType !== "Sound" && man.FileType !== "Preset")) throw new Error(`${DN_NOT} (FileType ${man && JSON.stringify(man.FileType)}, not "Sound")`);
    if (man.FileType !== "Sound") warnings.push(`FileType "${man.FileType}": read as a sound`);
    pl = typeof man.Payload === "string" && await zip.get(man.Payload);
    if (!pl) throw new Error(`${DN_NOT} (the payload ${JSON.stringify(man.Payload)} is missing)`);
  }
  if (pl.length < DN_HDR + 12 || !dnMagic(pl, 0, DN_BOX)) throw new Error(`${DN_NOT} (the payload's magic)`);
  // the body's length and its compression from the header (before 3y201: a u16le at 0x1c, the length's low byte and the
  // flag, 302 by luck); any length is read (dnRecord takes what its layout covers). The build at 9 is the OS that saved
  // it; the kind at 0x0d is 3, a sound (DNX: the firmware checks it; so do all 512 .dnsnd here); the device at 7: 9 the Digitone, 15 the Digitone II
  const size = dnU32be(pl, 0x19), packed = pl[0x1d], device = dnU16be(pl, 7), kind = dnU32be(pl, 0x0d);
  if (kind !== 3) warnings.push(`the container says content kind ${kind} (a sound is 3): read as a sound`);
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
  const tags = man && Array.isArray(man.MetaInfo && man.MetaInfo.Tags) ? man.MetaInfo.Tags.map(String) : [];
  const meta = { tags, firmware: man ? man.FirmwareVersion ?? null : null, product: man ? man.ProductType ?? null : null };
  if (device === 15 || dn2Prefixed(d)) return dn2Parse(d, warnings, { ...meta, manTags: man ? tags : null }, device);
  if (man && man.FirmwareVersion !== "1.42A") warnings.push(`made on Digitone firmware ${man.FirmwareVersion}: read as 1.42A`);
  if (man && JSON.stringify(man.ProductType) !== '["24","30"]') warnings.push(`product ${JSON.stringify(man.ProductType)}: read as a Digitone`);
  return dnRecord(d, warnings, meta);
}
// the record (from a .dnsnd or a SysEx dump; 302 bytes in version 5, 283 in a version-1 dump, any length read) -> the
// parsed sound; version: the layout's (dnsndToQuad's laws go by it), fileVersion: the record's own. A version not known
// is read with the newest layout (DN_NEWEST); a record cut short (no end marker, shorter than its layout): the fields it
// holds, the others left at FM TONE's defaults; either with a note. Only a record that is not a Digitone sound is refused
function dnRecord(d, warnings, meta) {
  if (dn2Prefixed(d)) return dn2Parse(d, warnings, meta, 15);    // (a Digitone II body: its 5-byte prefix, 3y210)
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

// ---- Digitone II sounds (3y210: .dn2pst, the same ZIP and container as a .dnsnd with the device 15; .dn2snd, the
// container alone, as a +Drive backup keeps it). Ported from FIZZ's site/js/dn2pst.js (the owner's, MPL-2.0, reused
// here under GPL-3.0 by its author); FIZZ research/DN2-FORMAT.md cites each fact (DNX's captures and its DN1 -> DN2
// map from Elektron's own conversions, syxgrid's firmware tables, the owner's BASSSINE and crackle). The body: a 5-byte
// prefix (the record's version u32be, a 0) and the 359-byte record: be ef ba ce, the version u32be (2: OS 1.10, the
// owner's BASSSINE; 1: crackle, saved on 1.10; 3: OS 1.11), the tag bits u32be at 8, the name (16 bytes, Windows-1252) at
// 12, parameter id at 28 + 2 x id (the DN1's coarse and fine bytes: DNX found no FM TONE value rescaled, so the DN1's
// laws above hold), the machine at 244 (0 FM TONE), the filter machine at 245, the OCTAVE at 251, the modulation
// records at 252 + 3k, the end marker ba ce f0 0c at 355. Into FM TONE version 6 (docs/QUAD.md "Version 6"): the filter
// machines, AHD, CRUSH, DRIVE's route and the sends; portamento and the modulation records into the record (the part's
// GLIDE, mod.c's matrix: dnsndToQuad's glide and mods, fm1sounds.js soundFromDnsnd)
export const DN2_MACHINES = ["FM TONE", "WAVETONE", "FM DRUM", "SWARMER", "MIDI"];
export const DN2_FILTERS = ["MULTI-MODE", "LOWPASS 4", "EQUALIZER", "COMB-", "LEGACY LP/HP", "COMB+"];
// the 32 tag bits at +8, bit t = tag t (DNX tags.ts; Transfer spells ATMOSPHERIC)
export const DN2_TAGS = ["KICK", "SNARE", "DEEP", "BRASS", "STRINGS", "PERCUSSION", "HI-HAT", "CYMBAL", "EVOLVING", "EXPRESSIVE",
  "BASS", "LEAD", "PAD", "TEXTURE", "CHORD", "SOUND FX", "ARPEGGIO", "METALLIC", "ACOUSTIC", "ATMOSPHERIC", "NOISY", "GLITCH",
  "HARD", "SOFT", "DARK", "BRIGHT", "VINTAGE", "EPIC", "FAIL", "LOOP", "MINE", "FAVOURITE"];
// RATIO A, the firmware's own strings (syxgrid PLOCK_STRTAB): quarters to 5, halves to 10, whole numbers to 16; every
// DN1 step proved by an export lands on it, and every one is a quarter, so FM TONE's RATIO A holds each exactly
export const DN2_RATIO_A = Array.from({ length: 36 }, (_, i) => i < 20 ? (i + 1) / 4 : i < 30 ? 5 + (i - 19) / 2 : i - 19);
// LFO MULT in runs of six (syxgrid read 0..11 off a DN2: x1..x32 at the BPM, then at 120): -> FM TONE's (0..11 at the
// BPM, 12..23 at 120)
export const dn2Mult = (n) => { const f = Math.floor(n / 6); return (n % 6) + 6 * (f >> 1) + 12 * (f & 1); };
// an LFO DESTINATION is a parameter id (DNX's selector table lands every DN1 number on the id of the same parameter) ->
// FM TONE's; the ids FM TONE has no destination for (LFO 2's own, the DN2's KEY.T HOLD BR SRR ..): off, with a note
export const DN2_LFO_DEST = { 0: DN_QD.NONE, 1: DN_QD.L1SPD, 5: DN_QD.L1MUL, 9: DN_QD.L1FAD, 17: DN_QD.L1WAV, 21: DN_QD.L1SPH,
  25: DN_QD.L1MOD, 29: DN_QD.L1DEP, 33: DN_QD.ALGO, 34: DN_QD.RC, 35: DN_QD.RA, 36: DN_QD.RB, 37: DN_QD.HARM, 38: DN_QD.DTUN,
  39: DN_QD.FDBK, 40: DN_QD.MIX, 43: DN_QD.AATK, 44: DN_QD.ADEC, 45: DN_QD.AEND, 46: DN_QD.ALEV, 47: DN_QD.BATK, 48: DN_QD.BDEC,
  49: DN_QD.BEND, 50: DN_QD.BLEV, 51: DN_QD.ADLY, 54: DN_QD.BDLY, 58: DN_QD.PITCH, 59: DN_QD.PAB2, 60: DN_QD.RALL, 61: DN_QD.ABLEV,
  62: DN_QD.ABATK, 63: DN_QD.ABDEC, 64: DN_QD.ABEND, 65: DN_QD.ABDLY, 66: DN_QD.OFSC, 67: DN_QD.OFSA, 68: DN_QD.OFSB1,
  69: DN_QD.OFSB2, 74: DN_QD.FREQ, 75: DN_QD.RESO, 76: DN_QD.FENV, 77: DN_QD.FDLY, 78: DN_QD.FATK, 79: DN_QD.FDEC, 80: DN_QD.FSUS,
  81: DN_QD.FREL, 83: DN_QD.BASE, 84: DN_QD.WIDTH, 87: DN_QD.EATK, 89: DN_QD.EDEC, 90: DN_QD.ESUS, 91: DN_QD.EREL, 92: DN_QD.SCHO,
  93: DN_QD.SDLY, 94: DN_QD.SREV, 95: DN_QD.PAN, 96: DN_QD.LEVEL, 104: DN_QD.DRIVE };
// the names of the ids FM TONE has no destination for (DNX plockparams.ts, syxgrid PLOCK_NAMES), for the notes
const DN2_ID_NAMES = { 2: "LFO2 SPEED", 6: "LFO2 MULT", 10: "LFO2 FADE", 14: "LFO2 DEST", 18: "LFO2 WAVE", 22: "LFO2 SPH",
  26: "LFO2 MODE", 30: "LFO2 DEPTH", 41: "PHASE RESET", 52: "A TRIG", 53: "A RESET", 55: "B TRIG", 56: "B RESET",
  70: "KEY TRACK A", 71: "KEY TRACK B1", 72: "KEY TRACK B2", 73: "FILTER TYPE", 82: "FILTER KEY TRACK", 85: "FILTER RESET",
  86: "AMP DELAY", 88: "AMP HOLD", 97: "AMP MODE", 98: "AMP RESET", 99: "PORTAMENTO", 100: "PORTAMENTO TIME",
  101: "BIT REDUCTION", 102: "SAMPLE RATE REDUCTION", 103: "SRR ROUTING", 105: "BW ROUTING", 106: "OVERDRIVE ROUTING" };
const dn2Name = (id) => DN2_ID_NAMES[id] ?? (DN2_LFO_DEST[id] !== undefined ? dnQdName(DN2_LFO_DEST[id]) : `#${id}`);
// the record's fields: [key, offset, hi-res, confidence, decode, apply] as DNSND_FIELDS; confidence (FIZZ's): "confirmed"
// (the offset measured on a DN2 by DNX, the law a DN1 one proved by the owner's exports), "fw" (offset and law from the
// firmware's tables, syxgrid), "inferred", "guess"; "info": decoded, not imported. apply null: dn2ToQuad's own
const dn2Lfo = (k) => [
  [`lfo${k + 1}.speed`, 30 + 2 * k, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo100(dnQlfo(k, "SPEED"))],
  [`lfo${k + 1}.mult`, 38 + 2 * k, 0, "fw", dnLo, dnTo(dnQlfo(k, "MULT"), dn2Mult)],
  [`lfo${k + 1}.fade`, 46 + 2 * k, 0, "confirmed", (d, o) => d[o] - 64, dnTo(dnQlfo(k, "FADE"))],
  [`lfo${k + 1}.dest`, 54 + 2 * k, 0, "confirmed", dnLo, (v, out) => {
    let q = DN2_LFO_DEST[v];
    if (k === 0 && q >= DN_QD.L1SPD && q <= DN_QD.L1DEP) q = undefined;     // (LFO 1 on itself: FM TONE has none)
    out.set(dnQlfo(k, "DEST"), q ?? DN_QD.NONE);
    if (q === undefined) out.note(`LFO ${k + 1} destination ${dn2Name(v)}: FM TONE has none, off`);
  }],
  [`lfo${k + 1}.wave`, 62 + 2 * k, 0, "confirmed", dnLo, (v, out) => out.set(dnQlfo(k, "WAVE"), DN_WAVE_TO_QUAD[v] ?? 0)],
  [`lfo${k + 1}.phase`, 70 + 2 * k, 0, "confirmed", dnLo, dnTo(dnQlfo(k, "PHASE"))],
  [`lfo${k + 1}.mode`, 78 + 2 * k, 0, "confirmed", dnLo, dnTo(dnQlfo(k, "TRIG"))],
  // (the DN2 shows DEPTH doubled, -128..127.99: the bytes are the DN1's, copied unscaled)
  [`lfo${k + 1}.depth`, 86 + 2 * k, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo100(dnQlfo(k, "DEPTH"))],
];
const dn2Env = (x, base) => ["atk", "dec", "end", "lev"].map((f, i) => [`env${x}.${f}`, base + 2 * i, 0, "confirmed", dnLo,
  dnTo(DN_QP[`${x}${f.toUpperCase()}`], i < 2 ? dnT : undefined)]);
export const DN2_FIELDS = [
  ...dn2Lfo(0), ...dn2Lfo(1), ...dn2Lfo(2),
  ["algo", 94, 0, "confirmed", (d, o) => d[o] + 1, dnTo(DN_QP.ALGO)],
  ["ratioC", 96, 0, "confirmed", (d, o) => DN_RATIO_CB[Math.min(18, d[o])], (v, out) => out.set(DN_QP.RC, Math.max(0, DN_RATIO_CB.indexOf(v)))],
  ["ratioA", 98, 0, "confirmed", (d, o) => DN2_RATIO_A[Math.min(35, d[o])], (v, out) => out.set(DN_QP.RA, dnR(v * 4) - 1)],
  ["ratioB", 100, 1, "confirmed", (d, o) => Math.min(360, (d[o] << 8 | d[o + 1]) >> 1), (v, out) => {
    out.set(DN_QP.RB1, Math.floor(v / 19)); out.set(DN_QP.RB2, v % 19);
  }],
  ["harm", 102, 1, "confirmed", (d, o) => dnHi(d, o) - 63, dnTo100(DN_QP.HARM)],
  ["dtun", 104, 1, "confirmed", dnHi, dnTo100(DN_QP.DTUN)],
  ["fdbk", 106, 0, "confirmed", dnLo, dnTo(DN_QP.FDBK)],
  ["mix", 108, 0, "confirmed", (d, o) => d[o] - 64, dnTo(DN_QP.MIX)],
  ["phrt", 110, 0, "confirmed", dnLo, dnTo(DN_QP.PHRT)],
  ...dn2Env("A", 114), ...dn2Env("B", 122),
  ["envA.delay", 130, 0, "confirmed", dnLo, dnTo(DN_QP.ADLY)],
  ["envA.trig", 132, 0, "confirmed", dnLo, dnTo(DN_QP.ATRIG)],
  ["envA.reset", 134, 0, "confirmed", dnLo, dnTo(DN_QP.ARST)],
  ["envB.delay", 136, 0, "confirmed", dnLo, dnTo(DN_QP.BDLY)],
  ["envB.trig", 138, 0, "confirmed", dnLo, dnTo(DN_QP.BTRIG)],
  ["envB.reset", 140, 0, "confirmed", dnLo, dnTo(DN_QP.BRST)],
  ["unk142", 142, 0, "info", dnLo, null],                                   // (id 57: the DN1's unk110)
  ["ofsC", 160, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.OFSC, (v) => dnR(v * 100 / 64))],   // -1.00..+0.99
  ["ofsA", 162, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.OFSA, (v) => dnR(v * 100 / 64))],
  ["ofsB1", 164, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.OFSB1, (v) => dnR(v * 100 / 64))],
  ["ofsB2", 166, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.OFSB2, (v) => dnR(v * 100 / 64))],
  ["keyA", 168, 0, "inferred", dnLo, dnTo(DN_QP.AKEY)],                    // KEY TRACK A B1 B2 (the DN1 keeps them at 274..276)
  ["keyB1", 170, 0, "inferred", dnLo, dnTo(DN_QP.B1KEY)],
  ["keyB2", 172, 0, "inferred", dnLo, dnTo(DN_QP.B2KEY)],
  ["filter.type", 174, 0, "fw", dnLo, null],                                // (per filter machine: dn2ToQuad)
  ["filter.freq", 176, 1, "confirmed", dnHi, dnTo100(DN_QP.FREQ)],
  ["filter.reso", 178, 1, "confirmed", dnHi, dnTo100(DN_QP.RESO)],
  ["filter.env", 180, 1, "confirmed", (d, o) => dnHi(d, o) - 64, dnTo100(DN_QP.FDEPTH)],
  ["filter.delay", 182, 0, "confirmed", dnLo, dnTo(DN_QP.FDLY)],
  ["filter.atk", 184, 0, "confirmed", dnLo, dnTo(DN_QP.FATK, dnT)],
  ["filter.dec", 186, 0, "confirmed", dnLo, dnTo(DN_QP.FDEC, dnT)],         // (127 INF: dn2ToQuad)
  ["filter.sus", 188, 0, "confirmed", dnLo, dnTo(DN_QP.FSUS)],
  ["filter.rel", 190, 0, "confirmed", dnLo, dnTo(DN_QP.FREL, dnTRel)],
  // KEY.T 0..100 % (syxgrid "pct") -> FM TONE's KTRK 0..127: the scale is a guess (FIZZ's)
  ["filter.keytrack", 192, 0, "guess", dnLo, dnTo(DN_QP.FKTRK, (v) => dnR(Math.min(100, v) * 127 / 100))],
  ["filter.base", 194, 1, "confirmed", dnHi, dnTo100(DN_QP.BASE)],
  ["filter.width", 196, 0, "confirmed", dnLo, dnTo(DN_QP.WIDTH)],
  ["filter.reset", 198, 0, "fw", dnLo, null],
  ["amp.atk", 202, 0, "confirmed", dnLo, dnTo(DN_QP.EATK, dnT)],
  ["amp.hold", 204, 0, "confirmed", dnLo, null],                            // 0..126, 127 NOTE (dn2ToQuad)
  ["amp.dec", 206, 0, "confirmed", dnLo, null],
  ["amp.sus", 208, 0, "confirmed", dnLo, null],
  ["amp.rel", 210, 1, "confirmed", dnHi, null],
  ["send.chorus", 212, 1, "confirmed", dnHi, dnSend("chorus")],
  ["send.delay", 214, 1, "confirmed", dnHi, dnSend("delay")],
  ["send.reverb", 216, 1, "confirmed", dnHi, dnSend("reverb")],
  ["amp.pan", 218, 0, "confirmed", (d, o) => d[o] - 64, (v, out) => { out.pan = Math.max(-64, Math.min(63, v)); }],
  ["amp.vol", 220, 1, "confirmed", dnHi, dnTo(DN_QP.LEVEL)],
  ["amp.mode", 222, 0, "confirmed", dnLo, null],                            // 0 AHD, 1 ADSR
  ["amp.reset", 224, 0, "confirmed", dnLo, null],
  ["portamento", 226, 0, "fw", dnLo, null],                                 // 64 ON (syxgrid; BASSSINE's 63: off)
  ["portamento.time", 228, 0, "fw", dnLo, null],
  ["fx.bitreduction", 230, 0, "confirmed", dnLo, null],
  ["fx.srr", 232, 0, "confirmed", dnLo, null],
  ["fx.srrRouting", 234, 0, "fw", dnLo, null],
  ["fx.overdrive", 236, 1, "confirmed", dnHi, (v, out) => { out.drive = Math.max(0, Math.min(127, dnR(v))); }],
  ["fx.bwRouting", 238, 0, "fw", dnLo, null],
  ["fx.odRouting", 240, 0, "fw", dnLo, null],
  ["octave", 251, 0, "inferred", (d, o) => (d[o] << 24) >> 24, null],      // (the DN1 v5's +280, copied)
];
export const DN2_RECORD = 359, DN2_VERSIONS = [1, 2, 3], DN2_LAYOUT = 2;
const DN2_END = 355;                              // the end marker's offset
const DN2_CP1252 = "€\u0081‚ƒ„…†‡ˆ‰Š‹Œ\u008dŽ\u008f\u0090‘’“”•–—˜™š›œ\u009džŸ";
const dn2Text = (b) => Array.from(b, (c) => (c >= 0x80 && c < 0xa0 ? DN2_CP1252[c - 0x80] : String.fromCharCode(c))).join("");
// a Digitone II body (the 5-byte prefix, then the record)
function dn2Prefixed(d) { return d.length >= 9 && d[4] === 0 && dnMagic(d, 5, DN_BEEF); }
// the body (with its prefix, or a bare record) -> the parsed sound, as parseDnsnd's + model 2, the machines, the tag
// bits; device: the container's (15). A version not known is read with version 2's layout and a note, a record cut short
// gives the fields it holds (FM TONE's defaults for the rest), a longer one is read to 359 bytes, each with a note. Not
// read: a body that is no record (DN1S: a Digitone sound kept on a Digitone II, its layout not decoded) and a sound of
// another machine (WAVETONE, FM DRUM, SWARMER, MIDI reuse the ids for other parameters: no FM TONE patch in them)
function dn2Parse(d, warnings, meta, device) {
  if (dnMagic(d, 0, [0x44, 0x4e, 0x31, 0x53]))
    throw new Error("a Digitone sound kept on a Digitone II (its body opens \"DN1S\"): import its .dnsnd, or save it on the Digitone II once as its own sound");
  let rec;
  if (dn2Prefixed(d)) {
    rec = d.subarray(5);
    if (dnU32be(d, 0) !== dnU32be(rec, 4)) warnings.push("the body's prefix is not the record's version: read anyway");
  } else if (dnMagic(d, 0, DN_BEEF)) { rec = d; warnings.push("a Digitone II record without its 5-byte prefix: read as one"); }
  else throw new Error(`${DN_NOT} (the sound's magic)`);
  if (device !== 15) warnings.push(`the container's device is ${device}, not 15 (the Digitone II): read as one`);
  const version = rec.length >= 8 ? dnU32be(rec, 4) : 0;
  if (version !== DN2_LAYOUT) warnings.push(DN2_VERSIONS.includes(version) ? `a version-${version} Digitone II sound: read as version ${DN2_LAYOUT} (the same layout, as FIZZ reads it)` :
    `a version-${version} Digitone II sound (${version > Math.max(...DN2_VERSIONS) ? "newer than" : "not one of"} the versions known, ${DN2_VERSIONS.join(" ")}): read as version ${DN2_LAYOUT}; fields the FM-1 does not know were skipped`);
  if (rec.length > DN2_RECORD) warnings.push(`a ${rec.length}-byte record: read as ${DN2_RECORD} bytes (the rest skipped)`);
  else if (rec.length >= DN2_END + 4 && !dnMagic(rec, DN2_END, [0xba, 0xce, 0xf0, 0x0c])) warnings.push("the record does not end ba ce f0 0c: read anyway");
  let n = 0;
  while (n < 16 && 12 + n < rec.length && rec[12 + n]) n++;
  const name = dn2Text(rec.subarray(12, 12 + n)), tagBits = rec.length >= 12 ? dnU32be(rec, 8) : 0;
  const tags = DN2_TAGS.filter((_, t) => (tagBits >>> t) & 1);
  if (meta.manTags && meta.manTags.join() !== tags.join()) warnings.push(`the manifest's tags (${meta.manTags.join(", ") || "none"}) are not the record's (${tags.join(", ") || "none"}): the record's kept`);
  const machine = rec.length > 244 ? rec[244] : 0, filterMachine = rec.length > 245 ? rec[245] : 4;
  const machineName = DN2_MACHINES[machine] ?? `machine ${machine}`, filterMachineName = DN2_FILTERS[filterMachine] ?? `filter machine ${filterMachine}`;
  if (machine !== 0) throw new Error(`"${name}" is a ${machineName} sound: only Digitone II FM TONE sounds import (the others hold no FM TONE patch)`);
  const params = {}, confidence = {}, missing = [];
  for (const [k, o, hi, c, dec] of DN2_FIELDS) {
    if (o + (hi ? 2 : 1) > Math.min(rec.length, DN2_END)) { if (c !== "info") missing.push(k); continue; }
    params[k] = +dec(rec, o).toFixed(2); confidence[k] = c;
  }
  if (missing.length) warnings.push(`the sound is cut short (${rec.length} bytes): ${missing.length} of its settings not in it, FM TONE's defaults instead`);
  const { manTags, ...m } = meta;
  return { name, firmware: null, product: null, ...m, tags, tagBits, model: 2, machine, machineName, filterMachine, filterMachineName,
    version: DN2_LAYOUT, fileVersion: version, params, confidence, warnings, record: rec.slice(0, DN2_RECORD) };
}

// ChoralRoot's matrix (mod.c: four slots of SRC, DST, AMT -64..63; the record's P_M1SRC ..): the Digitone II's
// modulation records (FIZZ's reading, not verified: 24 of 3 bytes at 252, four a source in the order PB VEL MW BR AT KEY,
// the depth in the first two bytes about 64, the destination's id the third) -> the slots where an equivalent exists.
// mod.c's sources MS_VEL 3, MS_KEY 4, MS_MODW 6, MS_AT 7 (pitch bend and breath: none); its destinations for an FM TONE
// sound: PITCH 1, AMP 4, PAN 5, DIST 6 (the sound's drive), CHO 7, DLY 8, REV 9, E1..E8 12..19 (FM TONE's macros, eng_quad.c
// QUAD_MAC: ALGO RATIO C RATIO A RATIO B2 HARM DTUNE FDBK MIX); CUT and SHP move nothing in FM TONE (its filter is its
// own): FREQ and every other destination are notes. Slot 3 is the sound's velocity routing and 4 its aftertouch's (the
// editor's EXPR screen): a VEL record takes slot 3, an AT one slot 4, the others (and more of them) slots 1 and 2
export const DN2_MOD_SRC = ["PB", "VEL", "MW", "BR", "AT", "KEY"];
const DN2_MOD_MS = { VEL: 3, KEY: 4, MW: 6, AT: 7 };
export const DN2_MOD_MD = { 58: 1, 96: 4, 95: 5, 104: 6, 92: 7, 93: 8, 94: 9, 33: 12, 34: 13, 35: 14, 36: 15, 37: 16, 38: 17, 39: 18, 40: 19 };
const DN2_MOD_AT = 252, DN2_MOD_N = 24;
function dn2Mods(rec, notes) {
  const slots = [null, null, null, null], want = { VEL: [2, 0, 1], AT: [3, 0, 1] };
  let n = 0, lost = 0;
  for (let k = 0; k < DN2_MOD_N && DN2_MOD_AT + 3 * k + 2 < Math.min(rec.length, DN2_END); k++) {
    const o = DN2_MOD_AT + 3 * k, id = rec[o + 2], src = DN2_MOD_SRC[k >> 2];
    const amt = Math.max(-64, Math.min(63, dnR(rec[o] + rec[o + 1] / 256 - 64))), md = DN2_MOD_MD[id];
    if (!id || !amt) continue;
    if (!DN2_MOD_MS[src]) { notes.push(`MOD ${src} -> ${dn2Name(id)}: the FM-1's matrix has no ${src === "PB" ? "pitch bend" : "breath"} source, not imported`); continue; }
    if (md === undefined) { notes.push(`MOD ${src} -> ${dn2Name(id)}: the FM-1's matrix has no such destination for FM TONE, not imported`); continue; }
    const at = (want[src] || [0, 1]).find((s) => !slots[s]);
    if (at === undefined) { lost++; continue; }
    slots[at] = [DN2_MOD_MS[src], md, amt]; n++;
    if (id === 36) notes.push(`MOD ${src} -> RATIO B: the FM-1's matrix moves RATIO B2 only`);
  }
  if (n) notes.push(`MOD: ${n} routing${n === 1 ? "" : "s"} from the Digitone II's modulation records into the FM-1's matrix (their layout and depth are FIZZ's reading, not verified)`);
  if (lost) notes.push(`MOD: ${lost} more routing${lost === 1 ? "" : "s"} not imported (the matrix has four slots, 3 the velocity's, 4 the aftertouch's)`);
  return n ? slots : null;
}

// the parsed Digitone II sound -> as dnsndToQuad's, + glide (the part's GLIDE 0..127, TIME mode; null: off) and mods
// (the matrix's four slots [SRC, DST, AMT] or null each; null: none)
function dn2ToQuad(parsed) {
  const P = parsed.params, Q = DN_QP, F = DN_QF6, p = DN_QUAD_RANGES.map((r) => r[2]), notes = [];
  const out = {
    pan: 0, drive: 0, sends: { reverb: 0, delay: 0, chorus: 0 }, note: (s) => notes.push(s),
    set: (i, v) => { const [mn, mx, df] = DN_QUAD_RANGES[i]; p[i] = Math.max(mn, Math.min(mx, Number.isFinite(v) ? v : df)); },
  };
  for (const [k, , , c, , apply] of DN2_FIELDS) if (apply && c !== "info" && P[k] !== undefined) apply(P[k], out);
  // the filter: the machine and its TYPE (FM TONE version 6's FTYPE 4..7, FIZZ's machines; TYPE their third control,
  // FSHAPE: MULTI's LP -> BP -> HP morph, the combs' damping, EQ's Q; EQ's GAIN is RESO's coarse - 64)
  const fm = parsed.filterMachine, ty = P["filter.type"] ?? 0;
  if (fm === 1) p[Q.FTYPE] = F.LP24;                                                   // LOWPASS 4
  else if (fm === 4) {                                                                 // LEGACY LP/HP: LP HP OFF (3: the DN1's OFF)
    p[Q.FTYPE] = ty === 0 ? F.LP12 : ty === 1 ? F.HP12 : F.OFF;
    if (ty > 3) notes.push(`LEGACY LP/HP type ${ty}: not a known value, the filter off`);
  } else if (fm === 0 || fm === 2 || fm === 3 || fm === 5) {                          // MULTI-MODE, EQUALIZER, COMB-, COMB+
    p[Q.FTYPE] = fm === 0 ? F.MULTI : fm === 2 ? F.EQ : fm === 3 ? F.COMBN : F.COMBP;
    out.set(Q.FSHAPE, ty);
    if (fm === 2) out.set(Q.FGAIN, dnR(P["filter.reso"] || 0) - 64);
    if (fm === 0 && ty) notes.push(`MULTI-MODE TYPE ${ty} taken 1:1 as FM TONE's MULTI shape (FIZZ's law, not verified)`);
  } else { p[Q.FTYPE] = F.OFF; notes.push(`${parsed.filterMachineName}: not a known filter machine, the filter off`); }
  if (P["fx.bwRouting"]) p[Q.BWPOST] = 1;                                             // BW ROUTING 1: after the filter
  if (P["filter.dec"] === 127) { p[Q.FSUS] = 127; notes.push("filter DEC INF: FM TONE's filter SUS set to 127"); }
  if (P["filter.reset"] === 0) notes.push("FILTER ENV RESET off not imported (FM TONE's filter envelope always resets)");
  // the amp: ADSR as it is; MODE 0 AHD: FM TONE's AHD (AMOD, HOLD 1:1, 127 NOTE). HOLD and SUS are kept in either mode
  // (stored though the mode does not use them: a switch of mode has them, as on the Digitone II)
  const adec = P["amp.dec"], rel = P["amp.rel"];
  if (P["amp.hold"] !== undefined) out.set(Q.HOLD, P["amp.hold"]);
  if (P["amp.mode"] === 0) p[Q.AMOD] = 1;
  if (adec !== undefined) p[Q.EDEC] = adec >= 127 ? 127 : P["amp.mode"] === 0 ? adec : Math.min(126, adec);
  if (P["amp.sus"] !== undefined) out.set(Q.ESUS, P["amp.sus"]);
  if (rel !== undefined) p[Q.EREL] = rel >= 127 ? 127 : Math.min(126, dnR(rel));
  if (P["amp.reset"] === 0) notes.push("AMP ENV RESET off not imported (FM TONE's amp envelope always resets)");
  // CRUSH: BIT REDUCTION and SAMPLE RATE REDUCTION 1:1, SRR ROUTING 0 before the filter (FIZZ's reading of both laws)
  if (P["fx.bitreduction"] || P["fx.srr"]) {
    out.set(Q.BITS, P["fx.bitreduction"] | 0); out.set(Q.SRR, P["fx.srr"] | 0); p[Q.CRPRE] = P["fx.srrRouting"] ? 0 : 1;
    notes.push(`BIT REDUCTION ${P["fx.bitreduction"] | 0}, SAMPLE RATE REDUCTION ${P["fx.srr"] | 0}: taken 1:1 (the laws are not verified)`);
  }
  // OVERDRIVE: the DN1's DRIVE (Elektron copied the byte); OD ROUTING 0 read as before the filter (FIZZ's guess):
  // DRIVE ROUTE PRE; else after it, where the FM-1 drives the part
  if (out.drive > 0) {
    if (!P["fx.odRouting"]) { p[Q.DRPRE] = 1; notes.push("OVERDRIVE before the filter (OD ROUTING 0 read as PRE: the encoding is not verified)"); }
    else notes.push(`OVERDRIVE after the filter (OD ROUTING ${P["fx.odRouting"]}): the FM-1 drives the part after its voices`);
  }
  p[Q.SENDS] = 1;                                                                      // (the sends travel with the sound)
  // portamento (64 ON, syxgrid): the part's GLIDE, TIME mode (FIZZ's PTIM 1:1 on ChoralRoot's voice.c law: TIME 1 + t^2 /
  // 8 ticks whatever the interval); the FM-1 glides in MONO and LEGATO, so on the bass part
  let glide = null;
  if (P.portamento === 64) {
    glide = Math.max(0, Math.min(127, P["portamento.time"] | 0));
    notes.push(`PORTAMENTO TIME ${glide}: the part's GLIDE (TIME mode; the FM-1 glides on the bass part, MONO and LEGATO)`);
  }
  const oct = P.octave | 0;
  if (oct) {
    notes.push(dnOctave(p, oct) ? `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12}: every ratio x ${2 ** (oct / 12)}` :
      `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12} not imported (a ratio has no such value in FM TONE): it plays ${Math.abs(oct / 12)} octave(s) ${oct > 0 ? "lower" : "higher"} than on the Digitone II`);
  }
  const mods = dn2Mods(parsed.record || new Uint8Array(0), notes);
  return { blob: dnQuadPack(p), values: p, pan: out.pan, drive: out.drive, sends: out.sends, glide, mods, notes };
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

// the parsed sound (or its params: read as version 1) -> FM TONE's blob, the track PAN, DRIVE and sends (P_PAN, P_DIST,
// P_REV P_DLY P_CHOR: not in the patch; the patch's SENDS says the sends are the sound's), and what was lost or approximated
export function dnsndToQuad(parsed) {
  if (parsed && parsed.model === 2) return dn2ToQuad(parsed);       // (a Digitone II sound, 3y210)
  const params = parsed && parsed.params ? parsed.params : parsed;
  const p = DN_QUAD_RANGES.map((r) => r[2]), notes = [];
  const out = {
    pan: 0, drive: 0, sends: { reverb: 0, delay: 0, chorus: 0 }, version: (parsed && parsed.params && parsed.version) || 1,
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
  // the Digitone's DRIVE sits before its filter, per voice (the manual l. 2826, the voice diagram): FM TONE's DRIVE
  // ROUTE PRE (version 6, FIZZ's digitone.js the same); 0 stays POST (no drive: no difference, the sound stays version 5)
  if (out.drive > 0) p[DN_QP.DRPRE] = 1;
  // the sends travel with the sound (the owner, 2026-10-31: "lets let these presets change the FX send amounts when
  // you load"): SENDS on, the amounts in the record (fm1sounds.js soundFromDnsnd)
  p[DN_QP.SENDS] = 1;
  const oct = params.octave | 0;
  if (oct) {
    const r = dnOctave(p, oct);
    notes.push(r ? `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12}: every ratio x ${2 ** (oct / 12)}` :
      `the sound's OCTAVE ${oct > 0 ? "+" : ""}${oct / 12} not imported (a ratio has no such value in FM TONE): it plays ${Math.abs(oct / 12)} octave(s) ${oct > 0 ? "lower" : "higher"} than on the Digitone`);
  }
  return { blob: dnQuadPack(p), values: p, pan: out.pan, drive: out.drive, sends: out.sends, notes };
}

// version 6's five bytes (87..91; eng_quad.c quad_pack6, docs/QUAD.md "Version 6"): a 7-bit value and a flag each,
// every default 0: 87 127 - HOLD | AMOD << 7, 88 FSHAPE | BWPOST << 7, 89 FGAIN (7-bit two's complement) | DRPRE << 7,
// 90 BITS | CRPRE << 7, 91 SRR | SENDS << 7
const dnQv = (p, k) => { const [mn, mx, df] = DN_QUAD_RANGES[k], v = p[k]; return Number.isInteger(v) ? Math.max(mn, Math.min(mx, v)) : df; };
export function dnQuadTail(p) {
  const Q = DN_QP, v = (k) => dnQv(p, k);
  return [(127 - v(Q.HOLD)) | v(Q.AMOD) << 7, v(Q.FSHAPE) | v(Q.BWPOST) << 7, (v(Q.FGAIN) & 127) | v(Q.DRPRE) << 7,
    v(Q.BITS) | v(Q.CRPRE) << 7, v(Q.SRR) | v(Q.SENDS) << 7];
}
function dnQuadUntail(t, p) {
  const Q = DN_QP;
  p[Q.HOLD] = 127 - (t[0] & 127); p[Q.AMOD] = t[0] >> 7; p[Q.FSHAPE] = t[1] & 127; p[Q.BWPOST] = t[1] >> 7;
  p[Q.FGAIN] = (t[2] & 127) >= 64 ? (t[2] & 127) - 128 : t[2] & 127; p[Q.DRPRE] = t[2] >> 7;
  p[Q.BITS] = t[3] & 127; p[Q.CRPRE] = t[3] >> 7; p[Q.SRR] = t[4] & 127; p[Q.SENDS] = t[4] >> 7;
  return p;
}
// FM TONE's values (73: version 5's; 83: with version 6's) -> its blob: 'Q', value - min a byte, the hi-res ones u16le;
// version 6 (its five bytes) when one of them is set or FTYPE is a machine (4..7), else version 5 as before (older
// firmware plays it); zeros (eng_quad.c quad_pack)
export function dnQuadPack(p) {
  const blob = new Uint8Array(DN_QUAD_BLOB), t = dnQuadTail(p);
  let o = 2;
  blob[0] = 0x51;
  for (let i = 0; i < DN_QUAD_NP; i++) {
    const [mn, mx] = i === DN_QP.FTYPE ? [0, DN_QUAD_FT6] : DN_QUAD_RANGES[i], u = Math.max(mn, Math.min(mx, p[i])) - mn;
    blob[o++] = u & 255;
    if (dnHiRes(i)) blob[o++] = u >> 8;
  }
  blob[1] = t.some((x) => x) || p[DN_QP.FTYPE] > DN_QF.LP24 ? 6 : 5;
  if (blob[1] === 6) blob.set(t, o);
  return blob;
}
// version 3's ranges (eng_quad.c quad_range_v3): the hi-res ones in whole numbers, FDBK 0..120
const dnRangeV3 = (i) => {
  const [mn, mx] = DN_QUAD_RANGES_V4[i];
  return dnHiResV4(i) ? [Math.trunc(mn / 100), Math.trunc(mx / 100)] : i === DN_QP.FDBK ? [0, 120] : [mn, mx];
};
// a blob's values in the current version's units, all 83 (version 6, 5, 4 or 3: version 6's at their defaults below 6;
// null: another)
export function dnQuadValues(blob) {
  const p = [], tail = (q) => { for (let i = DN_QUAD_NP; i < DN_QUAD_NP6; i++) q.push(DN_QUAD_RANGES[i][2]); return q; };
  if (blob[1] >= 4 && blob[1] <= 6) {
    const v4 = blob[1] === 4;
    let o = 2;
    for (let i = 0; i < DN_QUAD_NP; i++) {
      let u = blob[o++];
      if (v4 ? dnHiResV4(i) : dnHiRes(i)) u |= blob[o++] << 8;
      const v = u + (v4 ? DN_QUAD_RANGES_V4 : DN_QUAD_RANGES)[i][0];
      p.push(v4 ? dnV5OfV4(i, v) : v);
    }
    tail(p);
    return blob[1] === 6 ? dnQuadUntail(blob.subarray(o, o + 5), p) : p;
  }
  if (blob[1] !== 3) return null;
  for (let i = 0; i < DN_QUAD_NP; i++) {
    const [mn, mx] = dnRangeV3(i), v = Math.min(mx, blob[2 + i] + mn);
    const w = dnHiResV4(i) ? v * 100 : i === DN_QP.FDBK ? Math.trunc((v * 127 + 60) / 120) : v;
    p.push(dnV5OfV4(i, Math.max(DN_QUAD_RANGES_V4[i][0], Math.min(DN_QUAD_RANGES_V4[i][1], w))));
  }
  return tail(p);
}
// a stored blob as the newest this code writes: version 6 and 5 as they are, 1..4 converted to 5 (the name is 3y61's)
export function quadToV4(blob) {
  if (!(blob instanceof Uint8Array) || blob[0] !== 0x51) return blob;
  if (blob.length === DN_QUAD_BLOB && (blob[1] === 5 || blob[1] === 6)) return blob;
  if (blob.length === DN_QUAD_BLOB && blob[1] === 4) return dnQuadPack(dnQuadValues(blob));
  if (blob.length !== DN_QUAD_BLOB_V3 || blob[1] < 1 || blob[1] > 3) return blob;
  if (blob[1] !== 3) { const b = new Uint8Array(DN_QUAD_BLOB); b.set(blob); return b; }
  return dnQuadPack(dnQuadValues(blob));
}
// a blob for a firmware that reads up to version 5 (ChoralRoot 0.22 .. 0.27, FM1VST, fm1emu: their quad_blob_ok refuses
// version 6 and plays INIT): version 6's values dropped (each a note, as the importer's notes), a machine FTYPE -> its
// nearest (MULTI, the combs: LP12; EQ: OFF); 1..5 as quadToV4
const DN_QF6_TO_5 = [0, 1, 2, 3, 1, 1, 1, 0];
export function quadToV5(blob, notes = []) {
  if (!(blob instanceof Uint8Array) || blob.length !== DN_QUAD_BLOB || blob[0] !== 0x51 || blob[1] !== 6) return quadToV4(blob);
  const p = dnQuadValues(blob), Q = DN_QP, said = (c, s) => { if (c) notes.push(`${s}: this firmware has none (ChoralRoot 0.27 or older)`); };
  if (p[Q.FTYPE] > DN_QF.LP24) {
    notes.push(`filter ${["MULTI", "COMB+", "COMB-", "EQ"][p[Q.FTYPE] - 4]}: this firmware has none, ${["OFF", "LP12"][DN_QF6_TO_5[p[Q.FTYPE]]]} instead`);
    p[Q.FTYPE] = DN_QF6_TO_5[p[Q.FTYPE]];
  }
  said(p[Q.DRPRE], "DRIVE before the filter (ROUTE PRE): driven after it");
  said(p[Q.AMOD], "AMP MODE AHD: ADSR instead");
  said(p[Q.BWPOST], "base-width after the filter: before it");
  said(p[Q.BITS] || p[Q.SRR], "CRUSH (bit / sample-rate reduction)");
  said(p[Q.SENDS], "the sound's own sends: the part's stay");
  return dnQuadPack(p.slice(0, DN_QUAD_NP));
}
// a version-4 / 5 patch -> version 3's 80 bytes (for a firmware before the pool, whose QUAD store holds versions 1..3):
// the hi-res values rounded to whole numbers, FDBK 0..127 -> 0..120; an LFO destination of the 18 added in 0.22
// (40..57: ChoralRoot 0.21's quad_blob_ok refuses the whole patch over one of them, and it would play the init sound) ->
// NONE; a version 1..3 blob as it is. notes (an array, optional): what the older firmware will not have, one line each,
// as the importer's notes
export const DN_QD_V3 = 40;                       // destinations 0..39: the ones ChoralRoot 0.21 knows
const dnQdName = (q) => { const v = Object.keys(DN_LFO_DEST).find((k) => DN_LFO_DEST[k] === q); return v === undefined ? `#${q}` : DN_LFO_DEST_NAMES[v]; };
export function quadToV3(blob, notes = []) {
  if (!(blob instanceof Uint8Array) || blob.length !== DN_QUAD_BLOB || blob[0] !== 0x51) return blob;
  if (blob[1] === 6) blob = quadToV5(blob, notes);  // (version 6's values first: notes)
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
