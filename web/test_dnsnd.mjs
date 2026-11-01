// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// fm1dnsnd.js (docs/SOUNDS.md ".dnsnd import") with .dnsnd files built here by our own encoder (a ZIP stored or
// deflated, an LZ4 block, the payload's header and trailer) holding made-up parameters: every mapped field lands with
// its conversion, clamping, the errors and warnings, the import into a slot, the blob checked by the firmware's own
// quad_blob_ok (build/host/sound_templates --quad-check, built by tests/run_cr_tests.sh; skipped when missing).
//   node web/test_dnsnd.mjs
import { deflateRawSync } from "node:zlib";
import { existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { parseDnsnd, dnsndToQuad, DNSND_FIELDS, DN_QUAD_RANGES, DN_QP, DN_RATIO_A, DN_LFO_DEST, dnLz4Block, DN_TIME_TO_QUAD, dnTimeQ,
  DN_QUAD_HIRES, DN_QUAD_RANGES_V4, DN_QUAD_HIRES_V4, quadToV4, quadToV3, dnQuadPack, parseDnSyx, isDnSyx, dnPack87, dnUnpack87 } from "./fm1dnsnd.js";
// AMP REL (version 5): the whole number (times are 1:1), below 127.00 at most 126; 127.xx = INF 127 (fm1dnsnd.js dnTRelA)
const R100 = (v) => v >= 127 ? 127 : Math.min(126, Math.round(dnTimeQ(v)));
const T = (v) => DN_TIME_TO_QUAD[v], DN_RATIO_CB_OF = (i) => [0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16][i];                       // the envelope time curve (fm1dnsnd.js DN_TIME_*)
import { soundFromDnsnd, recordValid, SOUND_IDS, POOL_IDS, placeSound, parseSoundObjects, firstFreeSlot } from "./fm1sounds.js";
import { libraryItemsFromDnsnd, libraryItemsFromFile } from "./fm1manager.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const rejects = async (p, re) => { try { await p; return false; } catch (e) { return re.test(e.message) ? true : (console.log(`  (message: ${e.message})`), false); } };

/* ------------------------------------------------------------------ the encoder --- */
const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ c >>> 1 : c >>> 1; return c >>> 0; });
const crc32 = (b) => { let c = ~0; for (const x of b) c = crcT[(c ^ x) & 255] ^ c >>> 8; return ~c >>> 0; };
const le = (n, v) => Array.from({ length: n }, (_, i) => v >>> 8 * i & 255);
function zip(entries, deflate) {             // [[name, bytes]] -> a ZIP (local headers, central directory, end)
  const parts = [], cd = [];
  let off = 0;
  for (const [name, data] of entries) {
    const nm = new TextEncoder().encode(name), body = deflate ? new Uint8Array(deflateRawSync(data)) : data, m = deflate ? 8 : 0;
    const head = [...le(4, 0x04034b50), ...le(2, 20), 0, 0, ...le(2, m), 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, body.length), ...le(4, data.length), ...le(2, nm.length), 0, 0];
    parts.push(Uint8Array.from(head), nm, body);
    cd.push(Uint8Array.from([...le(4, 0x02014b50), 20, 0, 20, 0, 0, 0, ...le(2, m), 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, body.length), ...le(4, data.length), ...le(2, nm.length), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, off)]), nm);
    off += head.length + nm.length + body.length;
  }
  const cdLen = cd.reduce((n, p) => n + p.length, 0);
  const end = Uint8Array.from([...le(4, 0x06054b50), 0, 0, 0, 0, ...le(2, entries.length), ...le(2, entries.length), ...le(4, cdLen), ...le(4, off), 0, 0]);
  const all = [...parts, ...cd, end], out = new Uint8Array(all.reduce((n, p) => n + p.length, 0));
  let p = 0; for (const x of all) { out.set(x, p); p += x.length; }
  return out;
}
function lz4Literal(d) {                      // one sequence of literals only
  const l = d.length, out = [l >= 15 ? 0xF0 : l << 4];
  if (l >= 15) { let r = l - 15; while (r >= 255) { out.push(255); r -= 255; } out.push(r); }
  return Uint8Array.from([...out, ...d]);
}
// a made-up sound: the record from field values {name: raw byte or [coarse, fine]}
function record(name, set = {}, { version = 1, magic = [0xbe, 0xef, 0xba, 0xce] } = {}) {
  const d = new Uint8Array(302);
  d.set(magic, 0); d.set([version >>> 24, version >>> 16 & 255, version >>> 8 & 255, version & 255], 4);
  for (let i = 0; i < name.length; i++) d[12 + i] = name.charCodeAt(i);
  for (const [k, off] of DNSND_FIELDS.map((f) => [f[0], f[1]])) {
    const v = set[k];
    if (v === undefined) continue;
    if (Array.isArray(v)) { d[off] = v[0]; d[off + 1] = v[1]; } else d[off] = v;
  }
  return d;
}
// the payload as the Digitone writes it (FIZZ research/DN2-FORMAT.md §2): a 0x1f-byte header (the device u16be at 7, the
// build at 9, the kind 3 at 0x0d, the body's length u32be at 0x19, the compressed flag at 0x1d, the trailer's length),
// the body (packed: an LZ4 chain of blocks, u32be size each, then u32be 0), the 12-byte trailer
const be32 = (v) => [v >>> 24, v >>> 16 & 255, v >>> 8 & 255, v & 255];
function dnsnd(rec, { deflate = false, size = rec.length, man = {}, hdrMagic = [0xac, 0x11, 0xd3, 0x03], payload = "MY TEST", block, blocks, packed = 1, device = 9 } = {}) {
  const hdr = new Uint8Array(0x1f);
  hdr.set(hdrMagic, 0); hdr.set([2, 0, 5, device >> 8, device & 255, 0x30, 0x30, 0x39, 0x37, ...be32(3), ...be32(1), ...be32(0), ...be32(size), packed, 12], 4);
  const chain = (bs) => bs.flatMap((x) => [...be32(x.length), ...x]).concat(be32(0));
  const blk = Uint8Array.from(packed ? chain(blocks || [block || lz4Literal(rec)]) : rec);
  const tr = Uint8Array.from([1, 2, 3, 4, 0, 0, 0, 0x2a, 0xaa, 0xa1, 0xda, 0xaa]);
  const pl = new Uint8Array(hdr.length + blk.length + tr.length);
  pl.set(hdr); pl.set(blk, hdr.length); pl.set(tr, hdr.length + blk.length);
  const m = { FormatVersion: 1, ProductType: ["24", "30"], Payload: payload, FileType: "Sound", FirmwareVersion: "1.42A", MetaInfo: { Tags: ["test"] }, ...man };
  return zip([["manifest.json", new TextEncoder().encode(JSON.stringify(m))], [payload, pl]], deflate);
}

/* ----------------------------------------------------------------------- tests --- */
const firmwareOk = (blobs) => {
  const bin = new URL("../build/host/sound_templates", import.meta.url).pathname;
  if (!existsSync(bin)) return null;
  return execFileSync(bin, ["--quad-check"], { input: blobs.map((b) => Buffer.from(b).toString("hex")).join("\n") + "\n" }).toString().trim().split("\n");
};
const ranges = (() => {
  const bin = new URL("../build/host/sound_templates", import.meta.url).pathname;
  return existsSync(bin) ? JSON.parse(execFileSync(bin, ["--quad-ranges"]).toString()) : null;
})();
if (ranges) ok(JSON.stringify(ranges) === JSON.stringify(DN_QUAD_RANGES), "FM TONE's ranges = the firmware's (sound_templates --quad-ranges)");
else console.log("(build/host/sound_templates missing: the firmware checks are skipped)");

// a sound with every mapped field set to a made-up value
const SET = {
  "lfo1.speed": [64 + 20, 128], "lfo2.speed": [64 - 30, 0], "lfo1.mult": 14, "lfo2.mult": 5, "lfo1.fade": 64 + 9, "lfo2.fade": 64 - 7,
  "lfo1.dest": 0, "lfo2.dest": 0, "lfo1.wave": 4, "lfo2.wave": 5, "lfo1.phase": 33, "lfo2.phase": 0, "lfo1.mode": 3, "lfo2.mode": 1,
  "lfo1.depth": [64 + 40, 200], "lfo2.depth": [64 - 12, 0], algo: 5, ratioC: 7, ratioA: 10, harm: [63 + 11, 0], dtun: [40, 129],
  fdbk: 100, mix: 64 - 20, "envA.atk": 11, "envA.dec": 22, "envA.end": 33, "envA.lev": 44, "envB.atk": 55, "envB.dec": 66,
  "envB.end": 77, "envB.lev": 88, "envA.delay": 5, "envA.trig": 0, "envA.reset": 1, "envB.delay": 6, "envB.trig": 1, "envB.reset": 0,
  "filter.type": 2, "filter.freq": [90, 0], "filter.reso": [12, 255], "filter.env": [64 + 25, 0], "filter.atk": 3, "filter.dec": 70,
  "filter.sus": 80, "filter.rel": 90, "filter.base": [10, 0], "filter.width": 100, "amp.atk": 7, "amp.dec": 57,
  "amp.sus": 99, "send.delay": [20, 0], "amp.pan": 64 - 5, "amp.vol": [101, 0], "send.reverb": [0, 0], "send.chorus": [3, 0], "amp.rel": [61, 0],
  ratioB: [0, 2 * (5 * 19 + 7)], phrt: 3, "amp.drive": [30, 0],
};
const file = dnsnd(record("MY TEST TONE", SET));
const p = await parseDnsnd(file);
ok(p.name === "MY TEST TONE" && p.version === 1 && p.firmware === "1.42A" && p.tags.join() === "test" && !p.warnings.length && p.record.length === 302,
   "parse: name, version, firmware, tags, no warnings");
ok(p.params.algo === 6 && p.params["lfo1.speed"] === 20.5 && p.params.harm === 11 && p.confidence.algo === "slot" && p.confidence["filter.env"] === "verified" &&
   p.confidence.phrt === "inferred" && p.params.ofsC === undefined,
   "parse: the params in the Digitone's units, with their confidence");
const pd = await parseDnsnd(dnsnd(record("MY TEST TONE", SET), { deflate: true }));
ok(pd.record.every((x, i) => x === p.record[i]), "parse: a deflated ZIP reads the same");
const q = dnsndToQuad(p), v = q.values, L = (k, f) => 47 + k * 8 + { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 }[f];
ok(q.blob.length === 96 && q.blob[0] === 0x51 && q.blob[1] === 5 && q.blob.subarray(87).every((x) => !x) && DN_QUAD_HIRES.length === 12,
   "blob: 'Q' 5, 73 values (12 of them two bytes), zero padding");
{ // the blob's values back: a byte each, the hi-res ones u16le (eng_quad.c quad_unpack)
  let o = 2, good = true;
  for (let i = 0; i < 73; i++) {
    let u = q.blob[o++];
    if (DN_QUAD_HIRES.includes(i)) u |= q.blob[o++] << 8;
    good &&= u + DN_QUAD_RANGES[i][0] === v[i];
  }
  ok(good && o === 87, "blob: each value = value - min (the hi-res u16le)");
}
{ // the fine byte kept 1:1: value = coarse x 100 + round(fine x 100 / 256), for every fine byte (FREQ, the LFO's DEPTH)
  let bad = 0;
  for (let fine = 0; fine < 256; fine += 1) {
    const qq = dnsndToQuad(await parseDnsnd(dnsnd(record("FINE", { "filter.freq": [42, fine], "lfo1.depth": [64 - 23, fine] }))));
    bad += qq.values[DN_QP.FREQ] !== 4200 + Math.round(fine * 100 / 256) || qq.values[L(0, "DEPTH")] !== -2300 + Math.round(fine * 100 / 256);
  }
  ok(!bad, `fine bytes: FREQ 42 + every fine byte, DEPTH -23 + every fine byte land as hundredths (${bad} wrong)`);
}
{ // version 3 <-> 4 (fm1sounds.js: the legacy store's 80-byte blobs, a sound file of before)
  const v3 = quadToV3(q.blob), back = quadToV4(v3), same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  ok(v3.length === 80 && v3[1] === 3 && back.length === 96 && back[1] === 5 && back[2 + DN_QP.ALGO] === q.blob[2 + DN_QP.ALGO] &&
     same(quadToV4(quadToV3(back)), back), "v3 <-> v5: 80 bytes 'Q' 3 and back to 96 'Q' 5 (stable after one round)");
}
{ // version 5 -> 3 for ChoralRoot 0.21 (its quad_blob_ok: DEST 0..39, so a destination added in 0.22 would make it refuse the
  // whole patch and play INIT): such a destination becomes NONE, with a note; the others stay; AMP DEC INF and the
  // decimals said too
  const p5 = DN_QUAD_RANGES.map((r) => r[2]);
  p5[L(0, "DEST")] = 25; p5[L(1, "DEST")] = 45; p5[L(2, "DEST")] = 57; p5[DN_QP.EDEC] = 127; p5[DN_QP.FREQ] = 4201;
  const notes = [], v3 = quadToV3(dnQuadPack(p5), notes), at = (i) => v3[2 + i] + Math.trunc(DN_QUAD_RANGES[i][0] / (DN_QUAD_HIRES.includes(i) ? 100 : 1));
  ok(v3[1] === 3 && at(L(0, "DEST")) === 25 && at(L(1, "DEST")) === 0 && at(L(2, "DEST")) === 0 && at(DN_QP.FREQ) === 42 &&
     [...Array(3).keys()].every((k) => at(L(k, "DEST")) < 40),
     "v5 -> v3 (0.21): LFO destinations 40..57 -> NONE (FREQ, 25, kept); every DEST under 40");
  ok(notes.length === 4 && /LFO 2 destination LFO1 MODE: ChoralRoot 0.21 has none, off/.test(notes[0]) &&
     /LFO 3 destination FILTER ENV DELAY: ChoralRoot 0.21 has none, off/.test(notes[1]) && /AMP DEC INF/.test(notes[2]) && /1 value with decimals rounded/.test(notes[3]),
     `v5 -> v3 (0.21): one note each (${notes.join(" | ")})`);
  const quiet = []; quadToV3(dnQuadPack(DN_QUAD_RANGES.map((r) => r[2])), quiet);
  ok(!quiet.length, "v5 -> v3 (0.21): the init patch converts without a note");
}
{ // version 4 (3y61's, the owner's unit has them) -> 5: FREQ DTUN RESO BASE clamped to 127.00, AMP REL 126.85 -> 126,
  // 127.xx -> 127 (INF), LEVEL 110.40 -> 110, the rest as it was (a self-made patch)
  const p4 = DN_QUAD_RANGES_V4.map((r) => r[2]);
  p4[DN_QP.FREQ] = 12799; p4[DN_QP.DTUN] = 12750; p4[DN_QP.RESO] = 4201; p4[DN_QP.BASE] = 12700; p4[DN_QP.HARM] = -1234;
  p4[DN_QP.LEVEL] = 11040; p4[DN_QP.EDEC] = 127;
  const pack4 = (rel) => {
    const b = new Uint8Array(96);
    let o = 2;
    b[0] = 0x51; b[1] = 4;
    p4[DN_QP.EREL] = rel;
    for (let i = 0; i < 73; i++) {
      const u = p4[i] - DN_QUAD_RANGES_V4[i][0];
      b[o++] = u & 255;
      if (DN_QUAD_HIRES_V4.includes(i)) b[o++] = u >> 8;
    }
    return b;
  };
  const vals = (b) => {
    const p = [];
    let o = 2;
    for (let i = 0; i < 73; i++) {
      let u = b[o++];
      if (DN_QUAD_HIRES.includes(i)) u |= b[o++] << 8;
      p.push(u + DN_QUAD_RANGES[i][0]);
    }
    return p;
  };
  const a = vals(quadToV4(pack4(12685))), c = vals(quadToV4(pack4(12700))), d = vals(quadToV4(pack4(12799)));
  ok(quadToV4(pack4(12685))[1] === 5 && a[DN_QP.FREQ] === 12700 && a[DN_QP.DTUN] === 12700 && a[DN_QP.RESO] === 4201 &&
     a[DN_QP.BASE] === 12700 && a[DN_QP.HARM] === -1234 && a[DN_QP.LEVEL] === 110 && a[DN_QP.EDEC] === 127 &&
     a[DN_QP.EREL] === 126 && c[DN_QP.EREL] === 127 && d[DN_QP.EREL] === 127,
     "v4 -> v5: tops 127.00, AMP REL 126.85 -> 126, 127.00 / 127.99 -> 127 INF, LEVEL 110.40 -> 110, DEC 127 kept");
}
const want = [
  [DN_QP.ALGO, 6], [DN_QP.RC, 7], [DN_QP.RA, DN_RATIO_A[10] * 4 - 1], [DN_QP.RB1, 5], [DN_QP.RB2, 7], [DN_QP.HARM, 1100], [DN_QP.DTUN, 4050],
  [DN_QP.FDBK, 100], [DN_QP.MIX, -20], [DN_QP.AATK, T(11)], [DN_QP.ADEC, T(22)], [DN_QP.AEND, 33], [DN_QP.ALEV, 44],
  [DN_QP.BATK, T(55)], [DN_QP.BDEC, T(66)], [DN_QP.BEND, 77], [DN_QP.BLEV, 88], [DN_QP.ADLY, 5], [DN_QP.ATRIG, 0], [DN_QP.ARST, 1],
  [DN_QP.BDLY, 6], [DN_QP.BTRIG, 1], [DN_QP.BRST, 0], [DN_QP.FTYPE, 2], [DN_QP.FREQ, 9000], [DN_QP.RESO, 1300], [DN_QP.FDEPTH, 2500],
  [DN_QP.FATK, T(3)], [DN_QP.FDEC, T(70)], [DN_QP.FSUS, 80], [DN_QP.FREL, T(90)], [DN_QP.BASE, 1000], [DN_QP.WIDTH, 100], [DN_QP.EATK, T(7)],
  [DN_QP.EDEC, T(57)], [DN_QP.ESUS, 99], [DN_QP.EREL, R100(61)], [DN_QP.LEVEL, 101],
  [L(0, "SPEED"), 2050], [L(1, "SPEED"), -3000], [L(0, "MULT"), 14], [L(1, "MULT"), 5], [L(0, "FADE"), 9], [L(1, "FADE"), -7],
  [L(0, "DEST"), 0], [L(0, "WAVE"), 5], [L(1, "WAVE"), 4], [L(0, "PHASE"), 33], [L(0, "TRIG"), 3], [L(1, "TRIG"), 1],
  [L(0, "DEPTH"), 4078], [L(1, "DEPTH"), -1200],
  [DN_QP.PHRT, 3], [L(2, "SPEED"), 3200], [L(2, "MULT"), 5], [27, 64], [DN_QP.OFSC, 0], [DN_QP.FDLY, 0],   // LFO 3, VEL, OFS, FDLY: defaults (the INIT: LFO 3 as the Digitone init's LFO 1 and 2)
];
const bad = want.filter(([i, x]) => v[i] !== x);
ok(!bad.length, `convert: ${want.length} values land with their conversion`);
bad.forEach(([i, x]) => console.log(`  value ${i}: ${v[i]}, want ${x}`));
ok(DN_RATIO_A.length === 36 && [[0, 0.25], [3, 1], [9, 2.5], [15, 4], [25, 8], [35, 16]].every(([i, r]) => DN_RATIO_A[i] === r) &&
   DN_RATIO_A.every((r, i) => !i || r > DN_RATIO_A[i - 1]), "convert: RATIO A's table holds the proved steps (0.25 1 2.5 4 8 16), rising");
ok(q.pan === -5 && q.drive === 30, "convert: PAN and DRIVE -> the track's PAN and DRIVE");
const has = (re) => q.notes.some((n) => re.test(n));
ok(!has(/filter type/) && has(/delay send 20\.00 not imported/) &&
   has(/chorus send 3\.00/) && !has(/reverb send/) && !has(/RATIO/) && has(/^DRIVE 30: the FM-1 drives after the filter/) && q.notes.length === 3,
   "notes: the sends (0 sends say nothing), the DRIVE's place, no others (version-1 type 2 = HP: no note)");
// clamping: out-of-range raw values
const qc = dnsndToQuad(await parseDnsnd(dnsnd(record("CLAMP", { algo: 200, ratioC: 99, ratioA: 99, ratioB: [255, 255], harm: [127, 255], fdbk: 255, "lfo1.mult": 90,
  "lfo1.wave": 40, "lfo1.mode": 77, "lfo1.dest": 5, "lfo2.dest": 99, "lfo1.depth": [127, 255], "envA.trig": 9, "filter.type": 9, "amp.pan": 250, "amp.drive": [255, 0] }))));
const cv = qc.values;
ok(cv[DN_QP.ALGO] === 8 && cv[DN_QP.RC] === 18 && cv[DN_QP.RA] === 63 && cv[DN_QP.RB1] === 18 && cv[DN_QP.RB2] === 18 && cv[DN_QP.HARM] === 2600 &&
   cv[DN_QP.FDBK] === 127 && cv[L(0, "MULT")] === 23 && cv[L(0, "WAVE")] === 0 && cv[L(0, "TRIG")] === 4 && cv[L(0, "DEST")] === 42 &&
   cv[L(1, "DEST")] === 0 && cv[L(0, "DEPTH")] === 6399 && cv[DN_QP.ATRIG] === 1 && cv[DN_QP.FTYPE] === 1 && qc.pan === 63 && qc.drive === 127,
   "clamp: every value inside FM TONE's range");
ok(!qc.notes.some((n) => /LFO 1 destination/.test(n)) && qc.notes.some((n) => /LFO 2 destination #99: FM TONE has none, off/.test(n)),
   "notes: every Digitone LFO destination maps (LFO1 FADE: FM TONE's L1FAD); an unknown number is off, with a note");

/* the encodings the owner's controlled exports proved (2026-10-14; the raw bytes as those files hold them, our own
 * records): [field, raw (coarse or [coarse, fine]), the Digitone's value, the FM TONE value index, its value] */
const V5 = { version: 5 };
const PROVED = [
  ["ratioC", 0, 0.25, DN_QP.RC, 0], ["ratioC", 4, 2, DN_QP.RC, 4], ["ratioC", 18, 16, DN_QP.RC, 18],
  ["ratioA", 0, 0.25, DN_QP.RA, 0], ["ratioA", 9, 2.5, DN_QP.RA, 9], ["ratioA", 15, 4, DN_QP.RA, 15], ["ratioA", 25, 8, DN_QP.RA, 31], ["ratioA", 35, 16, DN_QP.RA, 63],
  ["ratioB", [0, 41], 20, DN_QP.RB1, 1], ["ratioB", [0, 41], 20, DN_QP.RB2, 1],             // 0.50 / 0.50 (the low bit ignored)
  ["ratioB", [0, 74], 37, DN_QP.RB2, 18], ["ratioB", [2, 178], 345, DN_QP.RB1, 18], ["ratioB", [2, 178], 345, DN_QP.RB2, 3],   // B2 16 B1 0.5; B2 1 B1 16
  ["ratioB", [0, 115], 57, DN_QP.RB1, 3], ["ratioB", [0, 115], 57, DN_QP.RB2, 0], ["ratioB", [0, 120], 60, DN_QP.RB2, 3],   // B2 0.25 B1 1; 1 / 1
  ["harm", [37, 0], -26, DN_QP.HARM, -2600], ["harm", [63, 0], 0, DN_QP.HARM, 0], ["harm", [89, 0], 26, DN_QP.HARM, 2600],
  ["dtun", [127, 0], 127, DN_QP.DTUN, 12700], ["fdbk", 20, 20, DN_QP.FDBK, 20], ["fdbk", 127, 127, DN_QP.FDBK, 127],
  ["mix", 0, -64, DN_QP.MIX, -64], ["mix", 64, 0, DN_QP.MIX, 0], ["mix", 127, 63, DN_QP.MIX, 63],
  ["envA.atk", 127, 127, DN_QP.AATK, T(127)], ["envA.dec", 0, 0, DN_QP.ADEC, 0], ["envA.end", 0, 0, DN_QP.AEND, 0], ["envA.lev", 127, 127, DN_QP.ALEV, 127],
  ["envB.atk", 127, 127, DN_QP.BATK, T(127)], ["envB.dec", 127, 127, DN_QP.BDEC, T(127)], ["envB.end", 0, 0, DN_QP.BEND, 0], ["envB.lev", 127, 127, DN_QP.BLEV, 127],
  ["filter.type", 0, 0, DN_QP.FTYPE, 0, V5], ["filter.type", 1, 1, DN_QP.FTYPE, 1, V5], ["filter.type", 2, 2, DN_QP.FTYPE, 2, V5], ["filter.type", 3, 3, DN_QP.FTYPE, 3, V5],
  ["filter.freq", [64, 113], 64.44, DN_QP.FREQ, 6444], ["filter.reso", [71, 174], 71.68, DN_QP.RESO, 7168], ["filter.reso", [12, 34], 12.13, DN_QP.RESO, 1213],
  ["filter.env", [0, 0], -64, DN_QP.FDEPTH, -6400], ["filter.env", [102, 15], 38.06, DN_QP.FDEPTH, 3806], ["filter.env", [127, 0], 63, DN_QP.FDEPTH, 6300],
  ["filter.atk", 97, 97, DN_QP.FATK, T(97)], ["filter.dec", 6, 6, DN_QP.FDEC, T(6)], ["filter.dec", 126, 126, DN_QP.FDEC, T(126)], ["filter.sus", 127, 127, DN_QP.FSUS, 127],
  ["filter.rel", 127, 127, DN_QP.FREL, 127], ["amp.atk", 127, 127, DN_QP.EATK, T(127)], ["amp.dec", 0, 0, DN_QP.EDEC, 0], ["amp.sus", 0, 0, DN_QP.ESUS, 0],
  ["amp.rel", [126, 218], 126.85, DN_QP.EREL, R100(126.85)], ["amp.rel", [127, 0], 127, DN_QP.EREL, 127], ["amp.rel", [0, 0], 0, DN_QP.EREL, 0], ["amp.vol", [100, 0], 100, DN_QP.LEVEL, 100],
  ["amp.vol", [110, 102], 110.4, DN_QP.LEVEL, 110],
  ["lfo2.speed", [127, 254], 63.99, L(1, "SPEED"), 6399], ["lfo2.speed", [0, 0], -64, L(1, "SPEED"), -6400], ["lfo2.speed", [64, 0], 0, L(1, "SPEED"), 0],
  ["lfo2.mult", 0, 0, L(1, "MULT"), 0], ["lfo2.mult", 23, 23, L(1, "MULT"), 23],
  ["ofsC", [64, 0], 0, DN_QP.OFSC, 0, V5], ["ofsB2", [96, 0], 32, DN_QP.OFSB2, 50, V5], ["filter.delay", 40, 40, DN_QP.FDLY, 40, V5],
];
const provedBad = [];
for (const [k, raw, dv, qi, qv, opt] of PROVED) {
  const pp = await parseDnsnd(dnsnd(record("PROVED", { [k]: raw }, opt)));
  const qq = dnsndToQuad(pp);
  if (pp.params[k] !== dv || qq.values[qi] !== qv) provedBad.push(`${k} ${JSON.stringify(raw)}: ${pp.params[k]} (want ${dv}), FM TONE ${qq.values[qi]} (want ${qv})`);
}
ok(!provedBad.length, `proved encodings: ${PROVED.length} cases decode to the Digitone's value and convert`);
provedBad.forEach((s) => console.log(`  ${s}`));
{ // the fields the exports placed but that have no FM TONE value: DRIVE, PAN, the sends, AENR
  const pp = await parseDnsnd(dnsnd(record("PROVED", { "amp.drive": [0, 4], "amp.pan": 0, "send.reverb": [127, 0], "send.delay": [127, 0], "send.chorus": [127, 0], "amp.reset": 0 }, V5)));
  const qq = dnsndToQuad(pp), n = qq.notes.join("|");
  ok(pp.params["amp.drive"] === 0.02 && qq.drive === 0 && qq.pan === -64 && /reverb send 127\.00/.test(n) && /delay send 127\.00/.test(n) && /chorus send 127\.00/.test(n) &&
     /AMP ENV RESET off/.test(n), "proved: DRIVE (fine), PAN L64, the three sends, AENR off (version 5)");
}
{ // DEC 127 = INF: AMP DEC INF is FM TONE's AMP DEC 127 (version 5: it holds the level), SUS as it was, no note; the
  // filter's DEC has no INF: its SUS 127, a note. A DEC below 127 is never INF (126 at most)
  const qq = dnsndToQuad(await parseDnsnd(dnsnd(record("INF", { "filter.dec": 127, "filter.sus": 10, "amp.dec": 127, "amp.sus": 20 }))));
  const q6 = dnsndToQuad(await parseDnsnd(dnsnd(record("DEC", { "amp.dec": 126 }))));
  ok(qq.values[DN_QP.FSUS] === 127 && qq.values[DN_QP.EDEC] === 127 && qq.values[DN_QP.ESUS] === 20 && qq.values[DN_QP.FDEC] === T(127) &&
     qq.notes.length === 1 && q6.values[DN_QP.EDEC] === T(126) && T(126) < 127,
     "convert: AMP DEC INF (127) -> AMP DEC 127 (INF), SUS kept; filter DEC INF -> SUS 127 with a note");
}
{ // the LFO destinations: every Digitone number of the table -> FM TONE's; a version-5 record reads its own fields
  const dests = Object.entries(DN_LFO_DEST).map(([n, q]) => [+n, q]), bad = [];
  for (const [n, qd] of dests) {
    const qq = dnsndToQuad(await parseDnsnd(dnsnd(record("DEST", { "lfo1.dest": n, "lfo2.dest": n, "amp.reset": 1, ofsC: 64, ofsA: 64, ofsB1: 64, ofsB2: 64 }, V5))));
    if (qq.values[L(0, "DEST")] !== qd || qq.values[L(1, "DEST")] !== qd || qq.notes.length) bad.push(n);
  }
  ok(dests.length === 58 && !bad.length && DN_LFO_DEST[17] === 3 && DN_LFO_DEST[42] === 1 && DN_LFO_DEST[66] === 38 && DN_LFO_DEST[76] === 10,
     `LFO destinations: ${dests.length} Digitone numbers map to FM TONE's (ALGO 17, PITCH ALL 42, VOL 66 -> LEVEL, B2 OFFSET 76)`);
  const want = { 1: 40, 3: 41, 5: 42, 9: 43, 11: 44, 13: 45, 15: 46, 44: 47, 45: 48, 46: 49, 47: 50, 48: 51, 49: 52, 64: 53, 67: 54,
    68: 55, 69: 56, 77: 57 };
  ok(Object.entries(want).every(([n, q]) => DN_LFO_DEST[n] === q) && new Set(Object.values(DN_LFO_DEST)).size === 58 &&
     Object.values(DN_LFO_DEST).every((q) => q >= 0 && q <= DN_QUAD_RANGES[L(0, "DEST")][1]),
     "LFO destinations 1:1: LFO1 SPEED..DEPTH -> L1SPD..L1DEP, RATIO ALL, AB LEVEL..AB DELAY, DRIVE, REV DEL CHO sends, ENV DELAY; one each");
}

{ // version 1's filter TYPE (the owner's re-saves on OS 1.44, 2026-10-14: 2 -> HP, 3 -> LP4, 4 -> LP4; 0 OFF, 1 LP2 inferred)
  const got = [];
  for (let t = 0; t <= 5; t++) { const qq = dnsndToQuad(await parseDnsnd(dnsnd(record("FT", { "filter.type": t })))); got.push(`${qq.values[DN_QP.FTYPE]}${qq.notes.length}`); }
  ok(got.join() === "00,10,20,30,30,11", "filter type version 1: 0 OFF 1 LP12 2 HP12 3 LP24 4 LP24 (no notes), 5 unknown -> LP12 with a note");
}
{ // the envelope times: 1:1, FM TONE's are the Digitone's since 2026-10-14 (docs/QUAD.md "Times"; the filter DEC 71 was
  // FM TONE's 97 on the old curve); a hi-res AMP REL rounded, at most 126 below 127.00 (127 = INF stays for the releases)
  ok(DN_TIME_TO_QUAD.length === 128 && DN_TIME_TO_QUAD.every((x, i) => x === i) && T(71) === 71 && dnTimeQ(10.13) === 10.13 &&
     dnTimeQ(-1) === 0 && dnTimeQ(130) === 127,
     "envelope times: 1:1 (0 -> 0, 71 -> 71, 127 -> 127)");
  const qq = dnsndToQuad(await parseDnsnd(dnsnd(record("REL", { "amp.rel": [10, 33], "filter.rel": 127, "filter.dec": 71, "envA.atk": 71,
    "envA.end": 99, "envB.lev": 7 }))));
  ok(qq.values[DN_QP.EREL] === 10 && R100(10.13) === 10 && R100(126.85) === 126 && qq.values[DN_QP.FREL] === 127 && qq.values[DN_QP.FDEC] === 71 &&
     qq.values[DN_QP.AATK] === 71 && qq.values[DN_QP.AEND] === 99 && qq.values[DN_QP.BLEV] === 7,
     "envelope times: a hi-res release rounded (10.13 -> 10), REL INF stays 127, DEC / ATK 1:1, END and LEV are levels (1:1)");
}
{ // the sound's OCTAVE (version 1 offset 263, version 5 offset 280, signed semitones): every ratio x 2^(oct / 12)
  const oc = async (set, opt) => dnsndToQuad(await parseDnsnd(dnsnd(record("OCT", set, opt))));
  const RB = (b1, b2) => [0, 2 * (b1 * 19 + b2)];                // RATIO B: B1 / B2 steps
  const a = await oc({ octave: 244, ratioC: 1, ratioA: 3, ratioB: RB(3, 8) }), av = a.values;      // -12: C 0.5 A 1 B1 1 B2 6
  ok(av[DN_QP.RC] === 0 && av[DN_QP.RA] === 1 && av[DN_QP.RB1] === 1 && av[DN_QP.RB2] === 5 && av[DN_QP.OFSC] === 0 && av[DN_QP.OFSB2] === 0 &&
     a.notes.some((n) => /OCTAVE -1: every ratio x 0\.5/.test(n)), "octave -1 (version 1): C 0.5 -> 0.25, A 1 -> 0.5, B 1 / 6 -> 0.5 / 3, a note");
  const b = await oc({ octave: 244, ratioC: 7, ratioA: 3, ratioB: RB(3, 3) }), bv = b.values;      // C 5 -> 2.5: 2.00 + 0.50 or 3.00 - 0.50
  ok(DN_RATIO_CB_OF(bv[DN_QP.RC]) + bv[DN_QP.OFSC] / 100 === 2.5 && Math.abs(bv[DN_QP.OFSC]) === 50, "octave -1: C 5 -> 2.5 as a step and OFS 0.50");
  const c = await oc({ octave: 244, ratioC: 0, ratioA: 3, ratioB: RB(3, 3) });                     // C 0.25 has no half
  ok(c.values[DN_QP.RC] === 0 && c.values[DN_QP.RA] === 3 && c.notes.some((n) => /OCTAVE -1 not imported .* 1 octave\(s\) higher/.test(n)),
     "octave -1: a ratio of 0.25 has no half in FM TONE: nothing changed, a note");
  const d = await oc({ octave: 12, ratioC: 3, ratioA: 3, ratioB: RB(3, 3), ofsC: 64, ofsA: 64, ofsB1: 64, ofsB2: 64 }, V5);                // v5 reads 280 (record() sets both)
  ok(d.values[DN_QP.RC] === 4 && d.values[DN_QP.RA] === 7 && d.values[DN_QP.RB1] === 4 && d.values[DN_QP.RB2] === 4, "octave +1 (version 5, offset 280): 1 -> 2");
  const e = await oc({ ratioC: 1 });
  ok(e.values[DN_QP.RC] === 1 && !e.notes.some((n) => /OCTAVE/.test(n)), "octave 0: nothing changed, no note");
}
const v5 = await parseDnsnd(dnsnd(record("V5", { ofsA: [64 + 16, 0] }, V5)));
ok(v5.version === 5 && v5.params.ofsA === 16 && v5.confidence.ofsA === "slot" && dnsndToQuad(v5).values[DN_QP.OFSA] === 25, "parse: a version-5 record (OS 1.44) reads, with its ratio offsets");
const fw = firmwareOk([q.blob, qc.blob, dnsndToQuad(await parseDnsnd(dnsnd(record("ZERO")))).blob, dnsndToQuad(v5).blob]);
if (fw) ok(fw.join() === "ok,ok,ok,ok", "firmware: quad_blob_ok takes the four blobs");
// the import: a sound in the template's record, placed in the first free slot
const s = soundFromDnsnd(p);
ok(recordValid(s.record) && s.engine === 15 && s.kind === "quad" && s.name === "MY TEST TONE" && s.record[16 + 39] === 64 - 5 && s.record[16 + 33] === 64 + 30 &&
   s.record[16 + 83] === 64 + 6 && s.record[16 + 84] === 64 + 7 && s.record[175] === 0xA6 && s.record[191] === 0, "import: an FM TONE record (name, PAN, DRIVE, macros, added)");
ok(soundFromDnsnd(await parseDnsnd(dnsnd(record("A NAME LONGER THAN 12")))).name === "A NAME LONGE" &&
   soundFromDnsnd(await parseDnsnd(dnsnd(record("")))).name === "DIGITONE", "import: a long name cut to 12, an empty one DIGITONE");
const blank = new Map(SOUND_IDS.map((id) => [id, new Uint8Array(0)]));
const slot = firstFreeSlot(blank), r = placeSound(blank, slot, s);
const row = parseSoundObjects(r.objs).slots[slot - 1];
ok(r.changed.map((c) => c.id).join() === "22,6" && row.used && row.engineName === "FM TONE" && row.patch === "quad" && row.name === "MY TEST TONE" &&
   r.objs.get(22).subarray(16 + (slot - 1) * 80, 16 + slot * 80).every((x, i) => x === quadToV3(q.blob)[i]),
   "import: placed in U01 (the FM TONE store of a firmware before the pool: version 3, then the bank)");
{   // 0.22, the sound pool: slots 1..32 taken, the import goes to U033 (bank 4, id 53) with its FM TONE patch
  let pm = new Map(POOL_IDS.map((id) => [id, new Uint8Array(0)]));
  for (let k = 1; k <= 32; k++) pm = placeSound(pm, k, s).objs;
  const ps = firstFreeSlot(pm), pr = placeSound(pm, ps, s), prow = parseSoundObjects(pr.objs).slots[ps - 1], pb = pr.objs.get(53);
  ok(ps === 33 && pr.changed.map((c) => c.id).join() === "53" && prow.used && prow.label === "U033" && prow.patch === "quad" &&
     pb[2 * 384 + 192] === 16 && pb[2 * 384 + 193] === 96 && pb.subarray(2 * 384 + 196, 2 * 384 + 196 + 96).every((x, i) => x === q.blob[i]),
     "import into the pool (0.22): U033, the first free slot after 32 taken (bank 4, slot 3: the record and the FM TONE patch)");
}
const items = await libraryItemsFromDnsnd("t.dnsnd", file);
ok(items.length === 1 && items[0].engine === 15 && items[0].patch && items[0].notes.length === q.notes.length && items[0].file.dnsnd.notes.length === q.notes.length,
   "manager: a library item with its notes");
// errors
const rec0 = record("X");
ok(await rejects(parseDnsnd(new Uint8Array([1, 2, 3, 4, 5])), /not a Digitone sound file \(not a ZIP/), "error: not a ZIP");
ok(await rejects(parseDnsnd(zip([["other.txt", new Uint8Array(3)]])), /no manifest\.json/), "error: no manifest");
ok(await rejects(parseDnsnd(zip([["manifest.json", new TextEncoder().encode("{x")]])), /not JSON/), "error: a manifest that is not JSON");
ok(await rejects(parseDnsnd(dnsnd(rec0, { man: { FileType: "Pattern" } })), /FileType "Pattern"/), "error: not a Sound");
ok(await rejects(parseDnsnd(dnsnd(rec0, { man: { Payload: "GONE" } })), /payload "GONE" is missing/), "error: the payload missing");
ok(await rejects(parseDnsnd(dnsnd(rec0, { hdrMagic: [1, 2, 3, 4] })), /payload's magic/), "error: the payload's magic");
ok(await rejects(parseDnsnd(dnsnd(rec0, { size: 400 })), /cut short/), "error: a body shorter than its header says");
ok(await rejects(parseDnsnd(dnsnd(rec0, { size: 0x9000 })), /the payload's header: a 36864-byte body/), "error: a body no sound could be");
ok(await rejects(parseDnsnd(dnsnd(rec0, { device: 15 })), /a Digitone II sound/), "error: a Digitone II sound (device 15)");
{ const d2 = new Uint8Array(364); d2.set([0, 0, 0, 2, 0, 0xbe, 0xef, 0xba, 0xce, 0, 0, 0, 2]);
  ok(await rejects(parseDnsnd(dnsnd(d2)), /a Digitone II sound/), "error: a Digitone II body (its 5-byte prefix, then the record)"); }
ok(await rejects(parseDnsnd(dnsnd(record("X", {}, { magic: [1, 2, 3, 4] }))), /the sound's magic/), "error: the record's magic");
ok(await rejects(parseDnsnd(dnsnd(rec0, { block: lz4Literal(rec0).subarray(0, 100) })), /cut short/), "error: the LZ4 block cut short");
ok(await rejects(parseDnsnd(dnsnd(rec0, { block: Uint8Array.from([0x10, 7, 9, 0]) })), /does not decompress/), "error: an LZ4 match before the start");
// warnings
const w = await parseDnsnd(dnsnd(rec0, { man: { FirmwareVersion: "1.50", ProductType: ["31"] } }));
ok(w.warnings.length === 2 && /firmware 1\.50/.test(w.warnings[0]) && /product \["31"\]/.test(w.warnings[1]), "warning: another firmware, another product (read)");
// the LZ4 decoder with a match (an overlapping copy)
const m = dnLz4Block(Uint8Array.from([0x26, 1, 2, 1, 0, 0x30, 7, 8, 9]), 0, 15).data;
ok([...m].join() === "1,2,2,2,2,2,2,2,2,2,2,2,7,8,9", "lz4: literals, an overlapping match, the last literals");

/* FIZZ's import fix (b67df20): a version-4 record (a sound saved on OS 1.42) has version 5's layout: read so, with a
 * note; it was refused as "a newer Digitone format (version 4)" (the Dissonant World pack's .dnsnd are all version 4) */
{
  const set = { ofsC: [96, 0], "filter.delay": 40, "filter.type": 3, mix: 127 };
  const v4 = await parseDnsnd(dnsnd(record("V4 SOUND", set, { version: 4 }))), v5 = await parseDnsnd(dnsnd(record("V4 SOUND", set, V5)));
  ok(v4.version === 5 && v4.fileVersion === 4 && v4.params.ofsC === 32 && v4.params["filter.delay"] === 40 &&
     [...dnsndToQuad(v4).blob].join() === [...dnsndToQuad(v5).blob].join(), "version 4: read with version 5's layout (OFS, filter DELAY), the same blob");
  ok(v4.warnings.includes("a version-4 sound: read as version 5 (the same layout)") && !v5.warnings.length, "version 4: a note says so");
}

/* Digitone SysEx dumps (.syx): F0 00 20 3C 0D 00 53 01 01 nn <the record 8:7 packed> <sum 14 bits> <length 14 bits> F7;
 * the record as a .dnsnd's (a version-1 one 283 bytes). Our own messages around our own records (no preset's bytes) */
const dnMsg = (rec, { type = 0x53, nn = 0, sum, len } = {}) => {
  const e = dnPack87(rec), s = sum ?? e.reduce((a, x) => a + x, 0) & 0x3FFF, l = len ?? e.length + 5;
  return Uint8Array.from([0xF0, 0x00, 0x20, 0x3C, 0x0D, 0x00, type, 1, 1, nn, ...e, s >> 7, s & 127, l >> 7, l & 127, 0xF7]);
};
const cat = (...a) => { const o = new Uint8Array(a.reduce((n, x) => n + x.length, 0)); let p = 0; for (const x of a) { o.set(x, p); p += x.length; } return o; };
{
  const r = new Uint8Array(64).map((_, i) => i * 37 & 255);
  ok([...dnUnpack87(dnPack87(r))].join() === [...r].join() && dnPack87(r).every((x) => x < 128), "8:7 packing: round trip, 7-bit bytes");
  const setA = { mix: 100, "filter.type": 2, "amp.vol": [90, 0] }, setB = { ratioC: 4, fdbk: 20 };
  const a = record("SYX ONE", setA).subarray(0, 283), b5 = record("SYX TWO", { ...setB, ofsC: [96, 0] }, V5);
  const kit = dnMsg(new Uint8Array(100), { type: 0x52 }), file = cat(dnMsg(a), kit, dnMsg(b5, { nn: 5 }));
  const g = parseDnSyx(file);
  ok(isDnSyx(file) && g.sounds.length === 2 && g.sounds[0].name === "SYX ONE" && g.sounds[1].name === "SYX TWO" && g.sounds[1].slot === 5,
     "syx: a pool dump's sounds with their names (a version-1 283-byte record, a version-5 one)");
  const f1 = await parseDnsnd(dnsnd(record("SYX ONE", setA))), f2 = await parseDnsnd(dnsnd(b5));
  ok([...dnsndToQuad(g.sounds[0]).blob].join() === [...dnsndToQuad(f1).blob].join() && [...dnsndToQuad(g.sounds[1]).blob].join() === [...dnsndToQuad(f2).blob].join(),
     "syx: each sound imports as the same record in a .dnsnd does");
  ok(g.notes.length === 1 && /^1 kit message skipped/.test(g.notes[0]) && !g.sounds[0].warnings.length, "syx: a project's other messages skipped, with a note");
  const bad = parseDnSyx(cat(dnMsg(a, { sum: 1 }), dnMsg(a, { len: 3 })));
  ok(/checksum does not match/.test(bad.sounds[0].warnings[0]) && /length field does not match/.test(bad.sounds[1].warnings[0]), "syx: a wrong checksum or length is read, with a note");
  const v2 = parseDnSyx(cat(dnMsg(record("OTHER", {}, { version: 2 })), dnMsg(a), dnMsg(new Uint8Array(40))));
  ok(v2.sounds.length === 2 && /^a version-2 sound \(not one of the versions known, 1 4 5\): read as version 5/.test(v2.sounds[0].warnings[0]) &&
     /message 3: not a Digitone sound file \(the sound's magic\)/.test(v2.notes[0]), "syx: a version not known read, with a note; a message that is no sound skipped");
  ok(await rejects(Promise.resolve().then(() => parseDnSyx(kit)), /no Digitone sound in this SysEx dump/), "error: a dump with no sound");
  ok(await rejects(Promise.resolve().then(() => parseDnSyx(Uint8Array.from([0xF0, 0x43, 0, 0, 0xF7]))), /not a Digitone SysEx dump/), "error: not a Digitone dump");
  const its = libraryItemsFromFile("pool.syx", file);
  ok(its.length === 2 && its.every((x) => x.engine === 15 && x.patch && Array.isArray(x.notes)) && its[0].name === "SYX ONE" && /kit message skipped/.test(its[0].notes[0]) &&
     its[1].file.dnsnd.name === "SYX TWO", "manager: a Digitone .syx gives FM TONE library items with their notes");
}

/* 3y201, the owner: "Presets with a newer format should still import." A version not known reads with the newest
 * layout (5), its tail (the OCTAVE) found from the end marker, with a note; a record cut short gives what it holds; the
 * body's length and compression from the payload's header (a raw body, an LZ4 chain). Our own records, no preset's bytes */
{
  const MARK = [0xba, 0xce, 0xf0, 0x0c], RB = (b1, b2) => [0, 2 * (b1 * 19 + b2)];
  const set = { ofsC: [96, 0], "filter.delay": 40, mix: 127, "filter.freq": [70, 0], ratioC: 3, ratioA: 3, ratioB: RB(3, 3) };
  const blobOf = async (b) => [...dnsndToQuad(await parseDnsnd(b)).blob].join();
  const same5 = (oct) => blobOf(dnsnd(record("NEWER", { ...set, octave: oct }, V5)));
  // a version 7 of 330 bytes: 28 more before its end marker (326), bytes no layout knows at 190..299, the OCTAVE 18 before the marker
  const r7 = new Uint8Array(330);
  r7.set(record("NEWER", set, V5).subarray(0, 183)); r7.set(be32(7), 4); r7.fill(0x55, 190, 300); r7[326 - 18] = 12; r7.set(MARK, 326);
  const p7 = await parseDnsnd(dnsnd(r7)), q7 = dnsndToQuad(p7);
  ok(p7.fileVersion === 7 && p7.version === 5 && p7.params.ofsC === 32 && p7.params["filter.delay"] === 40 && p7.params.octave === 12 &&
     q7.notes.some((n) => /OCTAVE \+1/.test(n)) && [...q7.blob].join() === await same5(12), "a newer version (7, 330 bytes): read as version 5, the OCTAVE from its end marker");
  ok(p7.warnings.length === 1 && p7.warnings[0] === "a version-7 sound (newer than the versions known, 1 4 5): read as version 5; fields the FM-1 does not know were skipped",
     `a newer version: one note naming it (${p7.warnings[0]})`);
  // version 7 with no end marker but version 5's length: read at version 5's places
  const r7b = record("NEWER", { ...set, octave: 244 }, { version: 7 }), p7b = await parseDnsnd(dnsnd(r7b));
  ok(p7b.params.octave === -12 && p7b.warnings.length === 1 && [...dnsndToQuad(p7b).blob].join() === await same5(244), "a newer version, no end marker, 302 bytes: version 5's places");
  // version 3 in a .syx (a 2018 dump's: 282 bytes, the end marker at 278, the OCTAVE 18 before it)
  const r3 = new Uint8Array(282);
  r3.set(record("OLDER", set, V5).subarray(0, 183)); r3.set(be32(3), 4); r3[260] = 244; r3.set(MARK, 278);
  const g3 = parseDnSyx(dnMsg(r3)), s3 = g3.sounds[0];
  ok(s3.fileVersion === 3 && s3.params.octave === -12 && /^a version-3 sound \(not one of the versions known, 1 4 5\)/.test(s3.warnings[0]) &&
     [...dnsndToQuad(s3).blob].join() === await same5(244), "version 3 (.syx, 282 bytes): read as version 5, the OCTAVE 18 before the marker");
  // a message cut short (113 bytes, no end marker): the fields it holds, the rest FM TONE's defaults, a note
  const cut = parseDnSyx(dnMsg(record("CUT", { mix: 100, "filter.freq": [90, 0], "amp.vol": [20, 0] }).subarray(0, 113))).sounds[0], qc5 = dnsndToQuad(cut);
  ok(cut.params.mix === 36 && cut.params["filter.freq"] === undefined && cut.params.octave === undefined && qc5.values[DN_QP.MIX] === 36 &&
     qc5.values[DN_QP.FREQ] === DN_QUAD_RANGES[DN_QP.FREQ][2] && qc5.values[DN_QP.LEVEL] === DN_QUAD_RANGES[DN_QP.LEVEL][2] &&
     /^the sound is cut short \(113 bytes, no end marker\): \d+ of its settings not in it, FM TONE's defaults instead$/.test(cut.warnings.at(-1)) && /: 22 of/.test(cut.warnings.at(-1)),
     `a record cut short: what it holds, FM TONE's defaults for the rest (${cut.warnings.at(-1)})`);
  // the framing: a raw body (flag 0), an LZ4 chain of two blocks (the second's match reaching into the first)
  const rr = record("CHAIN", set, V5); rr.set(rr.subarray(0, 20), 150);
  const two = [lz4Literal(rr.subarray(0, 150)), Uint8Array.from([0x0F, 150, 0, 1, 0xF0, 132 - 15, ...rr.subarray(170)])];
  const pr = await parseDnsnd(dnsnd(rr, { packed: 0 })), pc = await parseDnsnd(dnsnd(rr, { blocks: two }));
  ok([...pr.record].join() === [...rr].join() && [...pc.record].join() === [...rr].join() && !pr.warnings.length && !pc.warnings.length,
     "framing: a raw body and a two-block LZ4 chain read the record");
  const fw7 = firmwareOk([q7.blob, qc5.blob, dnsndToQuad(s3).blob]);
  if (fw7) ok(fw7.join() === "ok,ok,ok", "firmware: quad_blob_ok takes the newer, the cut-short and the version-3 blobs");
}

console.log(fails ? `DNSND TESTS FAILED (${fails})` : "dnsnd tests passed");
process.exit(fails ? 1 : 0);
