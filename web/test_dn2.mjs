// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// The Digitone II importer (fm1dnsnd.js "Digitone II sounds", 3y210; docs/SOUNDS.md ".dnsnd import") with .dn2pst and
// .dn2snd files built here by our own encoder (the record from made-up values, the 5-byte prefix, the container raw or
// as an LZ4 chain, a stored ZIP): every mapped field with its law, the filter machines, AHD, CRUSH, DRIVE's route, the
// sends, the glide and the matrix in the record, the versions, a record cut short or longer, the refusals, the library
// item, the version gate (quadToV5) and the blobs checked by the firmware's own quad_blob_ok (build/host/sound_templates
// --quad-check, built by tests/run_cr_tests.sh; skipped when missing). No Elektron or owner file is read.
//   node web/test_dn2.mjs
import { existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { parseDnsnd, dnsndToQuad, dnQuadValues, quadToV5, DN_QP, DN_QF6, DN_QD, DN2_FIELDS, DN2_RATIO_A, DN2_LFO_DEST, dn2Mult,
  DN_RATIO_CB } from "./fm1dnsnd.js";
import { soundFromDnsnd, recordValid, nameValid, placeSound, parseSoundObjects, POOL_IDS, quadV6Of } from "./fm1sounds.js";
import { libraryItemsFromDnsnd, isDnsndName } from "./fm1manager.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const rejects = async (p, re) => { try { await p; return false; } catch (e) { return re.test(e.message) ? true : (console.log(`  (message: ${e.message})`), false); } };

/* ------------------------------------------------------------------ the encoder --- */
const be32 = (v) => [v >>> 24, v >>> 16 & 255, v >>> 8 & 255, v & 255];
const le = (n, v) => Array.from({ length: n }, (_, i) => v >>> 8 * i & 255);
const F = Object.fromEntries(DN2_FIELDS.map((f) => [f[0], f]));
// a made-up record: raw bytes by field {key: byte or [coarse, fine]}, a byte at an offset {at: [[o, v], ..]}; the
// defaults of a DN1 -> DN2 conversion where the reader cares (filter machine LEGACY LP type 0, AMP MODE ADSR, HOLD NOTE,
// the resets on, the bipolar values at their centres), the end marker
function record(name, set = {}, { version = 2, machine = 0, fmach = 4, at = [], size = 359 } = {}) {
  const d = new Uint8Array(Math.max(size, 359));
  d.set([0xbe, 0xef, 0xba, 0xce, ...be32(version)], 0);
  for (let i = 0; i < name.length; i++) d[12 + i] = name.charCodeAt(i);
  for (const k of ["lfo1.speed", "lfo2.speed", "lfo3.speed", "lfo1.depth", "lfo2.depth", "lfo3.depth", "lfo1.fade", "lfo2.fade",
    "lfo3.fade", "mix", "ofsC", "ofsA", "ofsB1", "ofsB2", "filter.env", "amp.pan", "harm"]) d[F[k][1]] = k === "harm" ? 63 : 64;
  for (const [k, v] of [["amp.mode", 1], ["amp.hold", 127], ["amp.reset", 1], ["filter.reset", 1], ["portamento", 63], ["envA.reset", 1], ["envB.reset", 1]]) d[F[k][1]] = v;
  d[244] = machine; d[245] = fmach; d.set([0xba, 0xce, 0xf0, 0x0c], 355);
  for (const [k, v] of Object.entries(set)) { const o = F[k][1]; if (Array.isArray(v)) { d[o] = v[0]; d[o + 1] = v[1]; } else d[o] = v & 255; }
  for (const [o, v] of at) d[o] = v;
  return d.subarray(0, size);
}
const lz4Literal = (d) => { const l = d.length, out = [l >= 15 ? 0xF0 : l << 4]; if (l >= 15) { let r = l - 15; while (r >= 255) { out.push(255); r -= 255; } out.push(r); } return [...out, ...d]; };
// the stored container (FIZZ research/DN2-FORMAT.md §2): the header (device 15, build 0050, kind 3, the version, the
// body's length, compressed), the body (prefix + record) raw or as an LZ4 chain of one or two blocks, the trailer
function container(rec, { prefix = true, packed = 1, device = 15, split = 0 } = {}) {
  const body = prefix ? Uint8Array.from([...rec.subarray(4, 8), 0, ...rec]) : rec;
  const blocks = split ? [lz4Literal(body.subarray(0, split)), lz4Literal(body.subarray(split))] : [lz4Literal(body)];
  const chain = packed ? blocks.flatMap((b) => [...be32(b.length), ...b]).concat(be32(0)) : [...body];
  const hdr = [0xac, 0x11, 0xd3, 0x03, 2, 0, 5, device >> 8, device & 255, 0x30, 0x30, 0x35, 0x30, ...be32(3), ...rec.subarray(4, 8), ...be32(0), ...be32(body.length), packed, 12];
  return Uint8Array.from([...hdr, ...chain, 1, 2, 3, 4, ...be32(0), 0xaa, 0xa1, 0xda, 0xaa]);
}
function zip(entries) {                       // stored entries (the CRCs are not checked by the reader)
  const parts = [], cd = []; let off = 0;
  for (const [name, data] of entries) {
    const nm = new TextEncoder().encode(name);
    const head = [...le(4, 0x04034b50), 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, 0), ...le(4, data.length), ...le(4, data.length), ...le(2, nm.length), 0, 0];
    parts.push(Uint8Array.from(head), nm, data);
    cd.push(Uint8Array.from([...le(4, 0x02014b50), 20, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, 0), ...le(4, data.length), ...le(4, data.length), ...le(2, nm.length), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, off)]), nm);
    off += head.length + nm.length + data.length;
  }
  const cdLen = cd.reduce((n, p) => n + p.length, 0);
  const all = [...parts, ...cd, Uint8Array.from([...le(4, 0x06054b50), 0, 0, 0, 0, ...le(2, entries.length), ...le(2, entries.length), ...le(4, cdLen), ...le(4, off), 0, 0])];
  const out = new Uint8Array(all.reduce((n, p) => n + p.length, 0)); let p = 0; for (const x of all) { out.set(x, p); p += x.length; }
  return out;
}
const dn2pst = (rec, { man = {}, ...c } = {}) => zip([["manifest.json", new TextEncoder().encode(JSON.stringify({ FormatVersion: "1.0",
  ProductType: [], Payload: "MY DN2", FileType: "Sound", FirmwareVersion: "1.10E", MetaInfo: { Tags: [] }, ...man }))], ["MY DN2", container(rec, c)]]);
const read = async (rec, o) => { const p = await parseDnsnd(dn2pst(rec, o)); return { p, q: dnsndToQuad(p) }; };
const firmwareOk = (blobs) => {
  const bin = new URL("../build/host/sound_templates", import.meta.url).pathname;
  if (!existsSync(bin)) return null;
  return execFileSync(bin, ["--quad-check"], { input: blobs.map((b) => Buffer.from(b).toString("hex")).join("\n") + "\n" }).toString().trim().split("\n");
};
const blobs = [];

/* ----------------------------------------------------------------------- tests --- */
// a plain sound: LEGACY LP, ADSR, nothing set past version 5's but the sends' bit
{
  const { p, q } = await read(record("PLAIN"));
  ok(p.model === 2 && p.name === "PLAIN" && p.fileVersion === 2 && p.machineName === "FM TONE" && p.filterMachineName === "LEGACY LP/HP" && !p.warnings.length,
     "parse: a .dn2pst (model 2, name, version 2, the machines, no warnings)");
  const v = dnQuadValues(q.blob);
  ok(q.blob[1] === 6 && v[DN_QP.FTYPE] === DN_QF6.LP12 && v[DN_QP.SENDS] === 1 && [DN_QP.AMOD, DN_QP.DRPRE, DN_QP.BITS, DN_QP.SRR, DN_QP.BWPOST, DN_QP.FSHAPE].every((k) => !v[k]) &&
     v[DN_QP.HOLD] === 127 && q.glide === null && q.mods === null && !q.notes.length, "plain: version 6 (SENDS only), LEGACY LP = LP12, ADSR, no notes");
  blobs.push(q.blob);
}
// every mapped field, distinct values, each through its law
{
  const set = { "lfo1.speed": [70, 128], "lfo1.mult": 7, "lfo1.fade": 80, "lfo1.dest": 74, "lfo1.wave": 4, "lfo1.phase": 33, "lfo1.mode": 2, "lfo1.depth": [90, 64],
    "lfo2.speed": [20, 0], "lfo2.mult": 13, "lfo2.dest": 1, "lfo2.wave": 5, "lfo2.depth": [40, 0], "lfo3.mult": 20, "lfo3.dest": 37, "lfo3.wave": 6, "lfo3.mode": 4,
    algo: 4, ratioC: 7, ratioA: 24, ratioB: [(2 * (5 * 19 + 11)) >> 8, (2 * (5 * 19 + 11)) & 255], harm: [70, 128], dtun: [12, 64], fdbk: 99, mix: 30, phrt: 3,
    "envA.atk": 11, "envA.dec": 22, "envA.end": 33, "envA.lev": 44, "envB.atk": 55, "envB.dec": 66, "envB.end": 77, "envB.lev": 88,
    "envA.delay": 9, "envA.trig": 0, "envB.delay": 7, "envB.trig": 1, "envB.reset": 0, ofsC: [96, 0], ofsA: [32, 0], keyA: 20, keyB1: 30, keyB2: 40,
    "filter.freq": [80, 192], "filter.reso": [45, 0], "filter.env": [100, 0], "filter.delay": 12, "filter.atk": 5, "filter.dec": 60, "filter.sus": 70, "filter.rel": 127,
    "filter.keytrack": 50, "filter.base": [10, 128], "filter.width": 90, "amp.atk": 3, "amp.dec": 40, "amp.sus": 100, "amp.rel": [50, 230],
    "send.chorus": [20, 0], "send.delay": [30, 128], "send.reverb": [127, 255], "amp.pan": 80, "amp.vol": [111, 64], "fx.overdrive": [42, 0], "fx.odRouting": 0 };
  const { p, q } = await read(record("FIELDS", set)), v = dnQuadValues(q.blob), Q = DN_QP, L = (k, f) => 47 + 8 * k + { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 }[f];
  const want = [
    [L(0, "SPEED"), 650], [L(0, "MULT"), dn2Mult(7)], [L(0, "FADE"), 16], [L(0, "DEST"), DN_QD.FREQ], [L(0, "WAVE"), 5], [L(0, "PHASE"), 33], [L(0, "TRIG"), 2], [L(0, "DEPTH"), 2625],
    [L(1, "SPEED"), -4400], [L(1, "MULT"), dn2Mult(13)], [L(1, "DEST"), DN_QD.L1SPD], [L(1, "WAVE"), 4], [L(1, "DEPTH"), -2400],
    [L(2, "MULT"), dn2Mult(20)], [L(2, "DEST"), DN_QD.HARM], [L(2, "WAVE"), 6], [L(2, "TRIG"), 4],
    [Q.ALGO, 5], [Q.RC, 7], [Q.RA, DN2_RATIO_A[24] * 4 - 1], [Q.RB1, 5], [Q.RB2, 11], [Q.HARM, 750], [Q.DTUN, 1225], [Q.FDBK, 99], [Q.MIX, -34], [Q.PHRT, 3],
    [Q.AATK, 11], [Q.ADEC, 22], [Q.AEND, 33], [Q.ALEV, 44], [Q.BATK, 55], [Q.BDEC, 66], [Q.BEND, 77], [Q.BLEV, 88],
    [Q.ADLY, 9], [Q.ATRIG, 0], [Q.BDLY, 7], [Q.BTRIG, 1], [Q.BRST, 0], [Q.OFSC, 50], [Q.OFSA, -50], [Q.AKEY, 20], [Q.B1KEY, 30], [Q.B2KEY, 40],
    [Q.FREQ, 8075], [Q.RESO, 4500], [Q.FDEPTH, 3600], [Q.FDLY, 12], [Q.FATK, 5], [Q.FDEC, 60], [Q.FSUS, 70], [Q.FREL, 127], [Q.FKTRK, 64],
    [Q.BASE, 1050], [Q.WIDTH, 90], [Q.EATK, 3], [Q.EDEC, 40], [Q.ESUS, 100], [Q.EREL, 51], [Q.LEVEL, 111], [Q.DRPRE, 1], [Q.SENDS, 1]];
  const bad = want.filter(([i, x]) => v[i] !== x);
  ok(!bad.length, `fields: ${want.length} values through their laws${bad.length ? ` (wrong: ${bad.map(([i, x]) => `${i}=${v[i]} not ${x}`).join(", ")})` : ""}`);
  ok(q.pan === 16 && q.drive === 42 && q.sends.chorus === 20 && q.sends.delay === 31 && q.sends.reverb === 127, "fields: PAN, DRIVE and the sends (rounded)");
  ok(p.confidence["filter.keytrack"] === "guess" && p.confidence["lfo1.mult"] === "fw" && p.confidence.algo === "confirmed", "fields: the confidence of each");
  ok(q.notes.some((n) => /OVERDRIVE before the filter/.test(n)) && q.notes.length === 1, "fields: one note (OVERDRIVE's route)");
  const s = soundFromDnsnd(p), r = s.record;
  ok(recordValid(r) && r[16 + 36] === 127 + 64 && r[16 + 35] === 31 + 64 && r[16 + 34] === 20 + 64 && r[16 + 33] === 42 + 64 && r[16 + 39] === 16 + 64,
     "record: the sends (P_REV P_DLY P_CHOR), DRIVE and PAN, + 64");
  blobs.push(q.blob);
}
// LFO destinations FM TONE lacks, LFO 1 on itself, MULT's order, RATIO A's table
{
  const { q } = await read(record("DEST", { "lfo1.dest": 1, "lfo2.dest": 2, "lfo3.dest": 88 }));
  ok(q.notes.length === 3 && /LFO 1 destination LFO1 SPEED/.test(q.notes[0]) && /LFO 2 destination LFO2 SPEED/.test(q.notes[1]) && /LFO 3 destination AMP HOLD/.test(q.notes[2]),
     "LFO: on itself, on LFO 2, on a DN2-only id: off, a note each");
  ok([0, 5, 6, 11, 12, 17, 18, 23].map(dn2Mult).join() === "0,5,12,17,6,11,18,23", "LFO MULT: the DN2's runs of six -> FM TONE's order");
  ok(DN2_RATIO_A[0] === 0.25 && DN2_RATIO_A[19] === 5 && DN2_RATIO_A[20] === 5.5 && DN2_RATIO_A[29] === 10 && DN2_RATIO_A[30] === 11 && DN2_RATIO_A[35] === 16 &&
     DN2_RATIO_A.every((x) => x * 4 === Math.round(x * 4)), "RATIO A: the firmware's table, every step a quarter");
  ok(Object.values(DN2_LFO_DEST).every((x) => x >= 0 && x <= 57), "LFO DEST: every id lands on one of FM TONE's 58 destinations");
}
// the filter machines
{
  const cases = [[1, 0, DN_QF6.LP24], [4, 0, DN_QF6.LP12], [4, 1, DN_QF6.HP12], [4, 2, DN_QF6.OFF], [4, 3, DN_QF6.OFF], [0, 64, DN_QF6.MULTI], [5, 40, DN_QF6.COMBP],
    [3, 90, DN_QF6.COMBN], [2, 30, DN_QF6.EQ], [9, 0, DN_QF6.OFF]];
  let good = 0;
  for (const [fm, ty, ft] of cases) {
    const { q } = await read(record("FILT", { "filter.type": ty, "filter.reso": [100, 0] }, { fmach: fm })), v = dnQuadValues(q.blob);
    const shape = fm === 0 || fm === 2 || fm === 3 || fm === 5 ? ty : 0;
    if (v[DN_QP.FTYPE] === ft && v[DN_QP.FSHAPE] === shape && v[DN_QP.FGAIN] === (fm === 2 ? 36 : 0) && v[DN_QP.RESO] === 10000 &&
        (fm === 9) === q.notes.some((n) => /not a known filter machine/.test(n)) && (fm === 0) === q.notes.some((n) => /MULTI-MODE TYPE 64/.test(n))) good++;
    else console.log(`  (machine ${fm} type ${ty}: FTYPE ${v[DN_QP.FTYPE]} FSHAPE ${v[DN_QP.FSHAPE]} FGAIN ${v[DN_QP.FGAIN]} ${q.notes.join(" | ")})`);
    blobs.push(q.blob);
  }
  ok(good === cases.length, "filter: LOWPASS 4, LEGACY LP / HP / OFF, MULTI, COMB+, COMB-, EQ (GAIN = RESO - 64), unknown: OFF + note");
  const { q } = await read(record("BW", { "fx.bwRouting": 1 }));
  ok(dnQuadValues(q.blob)[DN_QP.BWPOST] === 1, "filter: BW ROUTING 1 -> base-width after the filter");
  const d = await read(record("FDEC", { "filter.dec": 127, "filter.sus": 10, "filter.reset": 0, "amp.reset": 0 }));
  ok(dnQuadValues(d.q.blob)[DN_QP.FSUS] === 127 && d.q.notes.length === 3, "filter: DEC INF -> SUS 127; the resets off: notes");
}
// the amp: AHD with a timed hold, AHD to NOTE, ADSR's INF
{
  const a = await read(record("AHD", { "amp.mode": 0, "amp.hold": 32, "amp.dec": 127, "amp.rel": [127, 0] })), v = dnQuadValues(a.q.blob);
  ok(v[DN_QP.AMOD] === 1 && v[DN_QP.HOLD] === 32 && v[DN_QP.EDEC] === 127 && v[DN_QP.EREL] === 127 && a.q.blob[87] === ((127 - 32) | 0x80), "amp: AHD, HOLD 32 (byte 87 = 127 - HOLD | AMOD)");
  const n = await read(record("NOTE", { "amp.mode": 0, "amp.hold": 127 }));
  ok(dnQuadValues(n.q.blob)[DN_QP.HOLD] === 127 && n.q.blob[87] === 0x80, "amp: AHD, HOLD NOTE (127)");
  const r = await read(record("ADSR", { "amp.dec": 127, "amp.rel": [126, 200] })), w = dnQuadValues(r.q.blob);
  ok(w[DN_QP.AMOD] === 0 && w[DN_QP.EDEC] === 127 && w[DN_QP.EREL] === 126, "amp: ADSR, DEC 127 INF, REL 126.78 -> 126");
  blobs.push(a.q.blob, n.q.blob);
}
// CRUSH and DRIVE's route
{
  const c = await read(record("CRUSH", { "fx.bitreduction": 20, "fx.srr": 40, "fx.srrRouting": 0 })), v = dnQuadValues(c.q.blob);
  ok(v[DN_QP.BITS] === 20 && v[DN_QP.SRR] === 40 && v[DN_QP.CRPRE] === 1 && c.q.notes.some((n) => /BIT REDUCTION 20, SAMPLE RATE REDUCTION 40/.test(n)), "crush: BITS, SRR 1:1, SRR ROUTING 0 = before the filter");
  const d = await read(record("CRPOST", { "fx.srr": 9, "fx.srrRouting": 1 }));
  ok(dnQuadValues(d.q.blob)[DN_QP.CRPRE] === 0, "crush: SRR ROUTING 1 = after the filter");
  const o = await read(record("ODPOST", { "fx.overdrive": [30, 0], "fx.odRouting": 1 }));
  ok(dnQuadValues(o.q.blob)[DN_QP.DRPRE] === 0 && o.q.notes.some((n) => /OVERDRIVE after the filter/.test(n)), "drive: OD ROUTING 1 -> after (POST), a note");
  const z = await read(record("NODRIVE"));
  ok(dnQuadValues(z.q.blob)[DN_QP.DRPRE] === 0 && z.q.drive === 0, "drive: none -> no ROUTE PRE");
  blobs.push(c.q.blob, d.q.blob, o.q.blob);
}
// portamento -> the part's GLIDE (TIME mode)
{
  const g = await read(record("GLIDE", { portamento: 64, "portamento.time": 50 })), r = soundFromDnsnd(g.p).record;
  ok(g.q.glide === 50 && r[16 + 38] === 50 + 64 && r[16 + 41] === 1 + 64 && g.q.notes.some((n) => /PORTAMENTO TIME 50/.test(n)), "glide: PORTAMENTO ON (64) -> GLIDE 50, mode TIME, a note");
  const o = await read(record("NOGLIDE", { portamento: 63, "portamento.time": 39 })), s = soundFromDnsnd(o.p).record;
  ok(o.q.glide === null && s[16 + 38] === 64 && s[16 + 41] === 64, "glide: PORTAMENTO 63 (off, as BASSSINE's) -> the template's GLIDE 0");
}
// the modulation records -> the matrix (P_M1SRC 49 ..: SRC DST AMT + 64)
{
  const m = (k, depth, id) => [[252 + 3 * k, depth + 64], [253 + 3 * k, 0], [254 + 3 * k, id]];
  // (k: PB 0..3, VEL 4..7, MW 8..11, BR 12..15, AT 16..19, KEY 20..23)
  const at = [...m(4, 20, 37), ...m(5, 10, 74), ...m(16, -10, 96), ...m(8, 30, 58), ...m(20, 5, 95), ...m(0, 40, 37), ...m(12, 40, 37), ...m(9, 12, 36), ...m(6, 0, 38), ...m(10, 7, 104)];
  const { p, q } = await read(record("MODS", {}, { at })), r = soundFromDnsnd(p).record, slot = (k) => [0, 1, 2].map((j) => r[16 + 49 + 3 * k + j] - 64);
  ok(JSON.stringify(q.mods) === JSON.stringify([[6, 1, 30], [6, 15, 12], [3, 16, 20], [7, 4, -10]]) && q.notes.some((n) => /MOD MW -> RATIO B: .*B2 only/.test(n)),
     `mods: VEL -> slot 3, AT -> slot 4, the first two others -> slots 1 and 2 (${JSON.stringify(q.mods)})`);
  ok(JSON.stringify([0, 1, 2, 3].map(slot)) === JSON.stringify(q.mods), "mods: the record's matrix (P_M1SRC ..) holds them");
  ok(q.notes.some((n) => /MOD VEL -> FILTER FREQ: .*no such destination/.test(n)) && q.notes.some((n) => /MOD PB -> HARM: .*no pitch bend source/.test(n)) &&
     q.notes.some((n) => /MOD BR -> HARM: .*no breath source/.test(n)) && q.notes.some((n) => /MOD: 4 routings/.test(n)) && q.notes.some((n) => /MOD: 2 more routings not imported/.test(n)) &&
     !q.notes.some((n) => /DETUNE/.test(n)), "mods: notes (no destination, no PB / BR source, four in, two lost, a zero depth skipped)");
}
// the OCTAVE, the tags and the name
{
  const o = await read(record("OCT", { octave: -24, ratioC: 7, ratioA: 7, ratioB: [1, 24] }));
  ok(o.q.notes.some((n) => /OCTAVE -2: every ratio x 0.25/.test(n)) && dnQuadValues(o.q.blob)[DN_QP.RC] === DN_RATIO_CB.indexOf(1), "octave: -24 -> every ratio x 0.25 (RATIO C 4 -> 1)");
  const rec = record("PLUCKY E\xc5"); rec.set(be32((1 << 10) | (1 << 12)), 8);
  const t = await read(rec, { man: { MetaInfo: { Tags: ["BASS"] } } });
  ok(t.p.name === "PLUCKY EÅ" && t.p.tags.join() === "BASS,PAD" && t.p.warnings.some((w) => /the manifest's tags \(BASS\) are not the record's \(BASS, PAD\)/.test(w)),
     "name: Windows-1252; tags: the record's bits, a manifest that differs noted");
  const s = soundFromDnsnd(t.p);
  ok(nameValid(s.name) && s.name === "PLUCKY E", "record: the name in ASCII (an accented letter dropped)");
}
// versions, sizes, containers
{
  const v3 = await read(record("V3", {}, { version: 3 }));
  ok(v3.p.fileVersion === 3 && v3.p.warnings.length === 1 && /version-3 Digitone II sound: read as version 2/.test(v3.p.warnings[0]), "version 3 (OS 1.11): read as version 2, a note");
  const v9 = await read(record("V9", { algo: 3 }, { version: 9 }));
  ok(v9.p.warnings.some((w) => /version-9 .*newer than the versions known, 1 2 3\): read as version 2/.test(w)) && dnQuadValues(v9.q.blob)[DN_QP.ALGO] === 4,
     "version 9 (unknown): read as version 2 with a note, not refused");
  const cut = await read(record("CUT", { algo: 3 }, { size: 200 })), cv = dnQuadValues(cut.q.blob);
  ok(cut.p.warnings.some((w) => /cut short \(200 bytes\): \d+ of its settings not in it/.test(w)) && cv[DN_QP.ALGO] === 4 && cv[DN_QP.EATK] === 8 && cv[DN_QP.LEVEL] === 110,
     "a record cut short: the fields it holds, FM TONE's defaults for the rest, a note");
  const long = await read(record("LONG", { algo: 3 }, { size: 400 }));
  ok(long.p.warnings.some((w) => /400-byte record: read as 359/.test(w)) && dnQuadValues(long.q.blob)[DN_QP.ALGO] === 4, "a longer record: read to 359 bytes, a note");
  const raw = await parseDnsnd(dn2pst(record("RAW", { algo: 5 }), { packed: 0 }));
  const two = await parseDnsnd(dn2pst(record("TWO", { algo: 5 }), { split: 100 }));
  ok(raw.params.algo === 6 && two.params.algo === 6 && !raw.warnings.length && !two.warnings.length, "container: a raw body, a two-block LZ4 chain");
  const snd = await parseDnsnd(container(record("BARE", { algo: 2 })));
  ok(snd.model === 2 && snd.params.algo === 3 && !snd.warnings.length, ".dn2snd: the container alone (a +Drive backup's file)");
  ok(await rejects(parseDnsnd(dn2pst(record("WT", {}, { machine: 1 }))), /"WT" is a WAVETONE sound: only Digitone II FM TONE sounds import/), "refused: a WAVETONE sound");
  ok(await rejects(parseDnsnd(dn2pst(record("SW", {}, { machine: 3 }))), /SWARMER/), "refused: a SWARMER sound");
  { const d = Uint8Array.from([0x44, 0x4e, 0x31, 0x53, ...new Uint8Array(300)]);
    ok(await rejects(parseDnsnd(dn2pst(d, { prefix: false })), /DN1S/), "refused: a Digitone sound kept on a Digitone II (DN1S)"); }
  ok(await rejects(parseDnsnd(dn2pst(new Uint8Array(359), { prefix: false })), /the sound's magic/), "refused: no record");
  ok(await rejects(parseDnsnd(Uint8Array.from([0xac, 0x11, 0xd3, 0x03, 1, 2])), /payload's magic/), "refused: a container cut short");
}
// the library item, the file names, the version gate
{
  ok(isDnsndName("A.dn2pst") && isDnsndName("b.DN2SND") && isDnsndName("c.dnsnd") && !isDnsndName("d.dn2prj"), "manager: .dnsnd, .dn2pst and .dn2snd go to the Digitone reader");
  const rec = record("LIB", { "amp.mode": 0, "amp.hold": 20, "fx.srr": 30, "fx.overdrive": [50, 0], "send.reverb": [40, 0] }, { fmach: 2 });
  const items = await libraryItemsFromDnsnd("lib.dn2pst", dn2pst(rec));
  ok(items.length === 1 && items[0].engine === 15 && items[0].file.dnsnd.model === 2 && items[0].notes.length >= 2, "manager: a library item (FM TONE, model 2, its notes)");
  const blob = dnsndToQuad(await parseDnsnd(dn2pst(rec))).blob, notes = [], v5 = quadToV5(blob, notes);
  ok(v5[1] === 5 && notes.length === 5 && /filter EQ: .*OFF instead/.test(notes[0]), "gate: quadToV5 for 0.27 (EQ -> OFF, ROUTE PRE, AHD, CRUSH, the sends: a note each)");
  ok(!quadV6Of("ChoralRoot 0.27") && !quadV6Of("ChoralRoot 0.22 (simulated)") && !quadV6Of("ChoralRoot 0.27 1-core") && !quadV6Of("Felucca 1.2") && !quadV6Of(null) &&
     quadV6Of("ChoralRoot 0.28") && quadV6Of("ChoralRoot 0.28-beta1 1-core") && quadV6Of("ChoralRoot 1.0") && quadV6Of("ChoralRoot 0.30"), "gate: quadV6Of (0.28 and later read version 6)");
  // (the pool, fm1sounds.js: slot 1's patch at 196 of bank 50, after the 192-byte record and its kind, size)
  const pool = new Map(POOL_IDS.map((id) => [id, new Uint8Array(0)])), s = soundFromDnsnd(await parseDnsnd(dn2pst(rec)));
  const old = placeSound(pool, 1, s, -1, { quadV6: false }), now = placeSound(pool, 1, s, -1, { quadV6: true });
  const patch = (res) => res.objs.get(POOL_IDS[0]).subarray(196, 196 + 96);
  ok(parseSoundObjects(old.objs).slots[0].used && patch(old)[1] === 5 && old.notes.length === 5 && patch(now)[1] === 6 && !now.notes.length,
     "gate: placeSound into the pool, quadV6 false: version 5 and the notes; true: version 6");
  blobs.push(patch(old), patch(now));
}
const fw = firmwareOk(blobs);
if (fw) ok(fw.length === blobs.length && fw.every((l) => /^ok/.test(l)), `firmware: quad_blob_ok takes the ${blobs.length} Digitone II blobs (version 6)`);
else console.log("(build/host/sound_templates missing: the firmware check is skipped)");

console.log(fails ? `DN2 TESTS FAILED (${fails})` : "dn2 tests passed");
process.exit(fails ? 1 : 0);
