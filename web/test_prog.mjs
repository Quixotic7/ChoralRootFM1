// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the progressions' web side (web/fm1prog.js; GitHub issue 64; the contract firmware/src/cr_prog.h), no
// browser, no hardware. Run from the repo root: node web/test_prog.mjs
// - the object: encode -> decode round trip (the bytes as cr_prog.h lays them out), and every rule of crp_valid, each
//   broken once on a valid object; the encoder's refusals (a bad name, too many notes / keys, too big: which to shorten)
// - Ripchord .rpc: the synthetic files of tests/fixtures/rpc (gaps kept as silent keys, more than 48 keys cut, chords of
//   more than 8 notes trimmed by the rule, junk after the root element, empty / reversed / single-quoted attributes, a
//   self-closing input), the name from the file name (12 printable ASCII), export -> import gives the same progression
// - the trimming rule (doubled pitch classes from the middle out, then the middle; the lowest and highest kept)
// - the chord names, the editing helpers, a backup file's progressions as library items
// - the owner's example folder (tmp/ChoralRoot/ChordProgressions, third-party, never in the repo): every file parses,
//   when it is there (skipped quietly otherwise)
import { readFileSync, readdirSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";
import {
  PROG_ID, PROG_N, PROG_KEYS, PROG_NOTES, PROG_DATA, PROG_SIZE, PROG_KEYBEDS, progEncode, progDecode, progValid, progInvalidReason,
  progEmpty, progIsEmpty, progCount, progBytes, progUsedBytes, progFitText, progSame, progCopySet, progParseRpc, progToRpc, progRpcFileName,
  progTrimChord, progNotesSummary, progChordName, progNoteName, progCleanName, progSetChord, progToggleNote, progTidy, progNew, progPlace,
  progItemsFromFile, progSimFactory,
} from "./fm1prog.js";
import { bkCrc } from "./fm1backup.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(76)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const throws = (f, re) => { try { f(); return false; } catch (e) { return !re || re.test(e.message); } };
const HERE = new URL(".", import.meta.url).pathname, FIX = join(HERE, "..", "tests", "fixtures", "rpc");
const fix = (n) => readFileSync(join(FIX, n), "utf8");
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

/* ------------------------------------------------------------- the object --- */
const A = { name: "POP FOUR", base: 48, keys: [[36, 48, 52, 55], [43, 55, 59, 62], [], [45, 57, 60, 64]] };
const B = { name: "", base: 60, keys: [[60], [], []] };                                // an all-zero name, trailing silent keys
const set = progCopySet([]); set[0] = A; set[5] = B;
const bytes = progEncode(set), v = new DataView(bytes.buffer);
ok(bytes.length === PROG_DATA + progBytes(A) + progBytes(B) && v.getUint32(0, true) === 0x31505243 && bytes[4] === 1 && bytes[5] === 16 &&
   v.getUint16(6, true) === bytes.length && bytes.subarray(8, 16).every((x) => !x), "encode: header (CRP1, version 1, 16, used = the length, rsv 0)");
ok(String.fromCharCode(...bytes.subarray(16, 24)) === "POP FOUR" && bytes[24] === 0 && bytes[28] === 48 && bytes[29] === 4 && v.getUint16(30, true) === PROG_DATA &&
   bytes[16 + 5 * 16 + 12] === 60 && bytes[16 + 5 * 16 + 13] === 3 && v.getUint16(16 + 5 * 16 + 14, true) === PROG_DATA + progBytes(A) &&
   bytes.subarray(32, 32 + 16).every((x) => !x), "encode: directory (name, base, nkeys, off; an empty slot all 0), data in slot order");
ok(eq([...bytes.subarray(PROG_DATA, PROG_DATA + 5)], [4, 36, 48, 52, 55]) && bytes[PROG_DATA + 10] === 0, "encode: records [n][notes], a silent key n = 0");
const back = progDecode(bytes);
ok(back.length === PROG_N && eq(back[0], A) && eq(back[5], B) && back.every((p, k) => k === 0 || k === 5 || progIsEmpty(p)), "decode: the same set back (trailing silent keys kept)");
ok(eq(progEncode(back), bytes) && progValid(bytes), "encode(decode(x)) === x; valid");
const empty16 = progEncode([]);
ok(empty16.length === PROG_DATA && progValid(empty16) && progDecode(empty16).every(progIsEmpty), "encode: no progression: 272 bytes, valid");
const full = progCopySet(Array.from({ length: 16 }, (_, k) => ({ name: `P${k}`, base: k, keys: Array.from({ length: 16 }, (_, i) => [k + i, k + i + 4, k + i + 7, k + i + 11, k + i + 14, k + i + 17, k + i + 21, k + i + 24].slice(0, (i % 8) + 1)) })));
const fb = progEncode(full);
ok(progValid(fb) && eq(progDecode(fb), full) && fb.length === progUsedBytes(full), `encode: 16 progressions of 16 keys (${fb.length} bytes), round trip`);

// every rule of crp_valid, each broken once
const bad = (what, f, base = bytes) => { const b = base.slice(); f(b, new DataView(b.buffer)); ok(!progValid(b) && progInvalidReason(b), `invalid: ${what} (${progInvalidReason(b)})`); };
const okay = (what, f, base = bytes) => { const b = base.slice(); f(b, new DataView(b.buffer)); ok(progValid(b), `valid: ${what}`); };
ok(!progValid(bytes.slice(0, 271)) && !progValid(new Uint8Array(3841)), "invalid: shorter than header + directory, longer than 3840");
bad("another magic", (b) => { b[0] ^= 1; });
bad("another version", (b) => { b[4] = 2; });
bad("n != 16", (b) => { b[5] = 15; });
bad("used != the length", (_, d) => d.setUint16(6, bytes.length - 1, true));
{ const b = new Uint8Array(bytes.length + 1); b.set(bytes); ok(!progValid(b), "invalid: a byte past used (length != used)"); }
bad("a reserved header byte set", (b) => { b[11] = 1; });
bad("an empty slot with a name byte", (b) => { b[16 + 2 * 16] = 65; });
bad("an empty slot with an offset", (_, d) => d.setUint16(16 + 2 * 16 + 14, PROG_DATA, true));
okay("an empty slot's base set (<= 127)", (b) => { b[16 + 2 * 16 + 12] = 99; });
bad("an empty slot's base > 127", (b) => { b[16 + 2 * 16 + 12] = 128; });
bad("a used slot's base > 127", (b) => { b[16 + 12] = 128; });
bad("nkeys > 48", (b) => { b[16 + 13] = 49; });
bad("a name byte < 0x20", (b) => { b[16 + 3] = 0x1F; });
bad("a name byte > 0x7E", (b) => { b[16 + 3] = 0x7F; });
bad("a name byte after a 0", (b) => { b[16 + 8] = 0; b[16 + 9] = 65; });
okay("a used slot named all 0", (b) => { b.fill(0, 16, 28); });
okay("a 12-character name without a 0", (b) => { for (let i = 0; i < 12; i++) b[16 + i] = 65 + i; });
bad("off < 272", (_, d) => d.setUint16(16 + 14, PROG_DATA - 1, true));
bad("off >= used", (_, d) => d.setUint16(16 + 14, bytes.length, true));
bad("a record past used", (b, d) => { b[16 + 5 * 16 + 13] = 4; });   // B: 3 keys -> 4 (the 4th past the end)
bad("n > 8", (b) => { b[PROG_DATA] = 9; });
bad("notes not ascending", (b) => { b[PROG_DATA + 2] = 30; });
bad("a note twice (not strictly ascending)", (b) => { b[PROG_DATA + 2] = 36; });
bad("a note > 127", (b) => { b[PROG_DATA + 4] = 128; });
bad("two slots' data overlap", (_, d) => d.setUint16(16 + 5 * 16 + 14, PROG_DATA + 2, true));
{ // a gap between the slots' data, and bytes no slot uses: allowed (crp_valid checks overlaps, not packing)
  const b = new Uint8Array(bytes.length + 4); b.set(bytes); new DataView(b.buffer).setUint16(6, b.length, true);
  const d = new DataView(b.buffer), offB = d.getUint16(16 + 5 * 16 + 14, true);
  b.copyWithin(offB + 4, offB, bytes.length); d.setUint16(16 + 5 * 16 + 14, offB + 4, true);
  ok(progValid(b) && eq(progDecode(b)[5], B), "valid: a gap before a slot's data (it decodes the same)");
}
ok(throws(() => progDecode(bytes.slice(0, 280)), /Not a valid/), "decode: an invalid object throws (the unit would use its factory set)");

// the encoder's refusals
const one = (p) => { const s = progCopySet([]); s[3] = p; return s; };
ok(throws(() => progEncode(one({ name: "THIRTEEN CHAR", base: 0, keys: [[1]] })), /12 printable/) && throws(() => progEncode(one({ name: "café", base: 0, keys: [[1]] })), /12 printable/),
   "encode: a name of 13 characters, a non-ASCII name refused");
ok(throws(() => progEncode(one({ name: "X", base: 128, keys: [[1]] }))) && throws(() => progEncode(one({ name: "X", base: 0, keys: Array.from({ length: 49 }, () => [1]) })), /48/),
   "encode: base > 127, 49 keys refused");
ok(throws(() => progEncode(one({ name: "X", base: 0, keys: [[1, 2, 3, 4, 5, 6, 7, 8, 9]] })), /8 notes/) && throws(() => progEncode(one({ name: "X", base: 0, keys: [[5, 3]] })), /ascending/) &&
   throws(() => progEncode(one({ name: "X", base: 0, keys: [[128]] }))), "encode: 9 notes, notes not ascending, a note > 127 refused");
const huge = Array.from({ length: 16 }, (_, k) => ({ name: `BIG ${k + 1}`, base: 0, keys: Array.from({ length: 48 }, (_, i) => [i, i + 12, i + 19, i + 24, i + 28, i + 31, i + 36, i + 40]).slice(0, k < 9 ? 48 : 1) }));
const why = progFitText(huge);
ok(progUsedBytes(huge) > PROG_SIZE && throws(() => progEncode(huge), /shorten/) && /BIG 1 \(432 bytes\)/.test(why) && /more than the FM-1 holds \(3840\)/.test(why),
   `capacity: too big refused, the biggest named: ${why.slice(0, 90)}…`);
ok(progFitText(set) === "" && progUsedBytes(set) === bytes.length, "capacity: a set that fits: no text, used = the object's length");

/* ------------------------------------------------------------- trimming --- */
{
  const a = progTrimChord([36, 48, 52, 55, 60, 64, 67, 72, 76, 79]);
  ok(eq(a.notes, [36, 48, 52, 55, 67, 72, 76, 79]) && eq(a.dropped, [60, 64]), "trim: 10 notes: the doubled C and E nearest the middle go");
  const b = progTrimChord([50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61]);
  ok(eq(b.notes, [50, 51, 52, 53, 58, 59, 60, 61]) && eq(b.dropped, [54, 55, 56, 57]), "trim: 12 distinct pitch classes: the middle four go, the ends stay");
  const c = progTrimChord([38, 45, 50, 53, 57, 60, 64, 67, 71]);
  ok(eq(c.notes, [38, 45, 50, 53, 60, 64, 67, 71]) && eq(c.dropped, [57]), "trim: 9 notes: the doubled A (57) nearest the middle goes, not the middle C");
  const d = progTrimChord([24, 36, 48, 60, 72, 84, 96, 108, 120]);
  ok(d.notes[0] === 24 && d.notes.at(-1) === 120 && d.notes.length === 8, "trim: all one pitch class: the lowest and the highest always kept");
  ok(eq(progTrimChord([1, 2, 3]).notes, [1, 2, 3]) && !progTrimChord([1, 2, 3]).dropped.length, "trim: 8 notes or fewer: untouched");
}

/* ------------------------------------------------------------- Ripchord --- */
{
  const g = progParseRpc(fix("Gaps Test.rpc"), "Gaps Test.rpc");
  ok(g.prog.name === "Gaps Test" && g.prog.base === 60 && g.prog.keys.length === 5 && eq(g.prog.keys[2], []) && eq(g.prog.keys[3], [41, 53, 57, 60]) &&
     g.chords === 4 && !g.notes.length && progNotesSummary(g) === "4 chords · ready", "rpc: a gap (62) kept as a silent key; empty name= attributes; no notes");
  const w = progParseRpc(fix("Wide Sixty Keys Preset.rpc"), "Wide Sixty Keys Preset.rpc");
  ok(w.prog.keys.length === 48 && w.prog.base === 36 && eq(w.prog.keys[47], [83, 87, 90]) && w.span === 60 && w.notes.includes("60 keys: the first 48 kept") &&
     w.prog.name === "Wide Sixty K", `rpc: 60 keys: the first 48 kept, flagged; the name cut to 12 (${w.prog.name})`);
  const bc = progParseRpc(fix("big-chords.rpc"), "big-chords.rpc");
  ok(eq(bc.prog.keys[0], [36, 48, 52, 55, 67, 72, 76, 79]) && eq(bc.prog.keys[1], [50, 51, 52, 53, 58, 59, 60, 61]) && eq(bc.prog.keys[2], [38, 45, 50, 53, 60, 64, 67, 71]) &&
     eq(bc.prog.keys[3], [36, 43, 48, 52, 55, 59, 62, 64]) && bc.notes.length === 3 && bc.notes[0] === "key 1: 10 notes cut to 8 (C4 E4 left out)" &&
     progNotesSummary(bc) === "4 chords · 3 cut to 8 notes", "rpc: chords of 10, 12, 9 notes trimmed by the rule and each flagged; 8 notes kept");
  const j = progParseRpc(fix("junk after.rpc"), "junk after.rpc");
  ok(j.prog.base === 57 && j.prog.keys.length === 2 && eq(j.prog.keys[0], [40, 52, 55, 59, 62, 66]) && !j.notes.length, "rpc: a BOM, CRLF, junk and a second <ripchord> after the first: ignored");
  const q = progParseRpc(fix("quotes-and-silent.rpc"), "Ébauche Élégante – très longue.rpc");
  ok(q.prog.base === 48 && q.prog.keys.length === 3 && eq(q.prog.keys[0], [36, 48, 52, 55]) && eq(q.prog.keys[1], []) && eq(q.prog.keys[2], [38, 50, 53, 57]) &&
     q.prog.name === "Ebauche Eleg", `rpc: single quotes, notes= before name=, a self-closing input (silent), <chord></chord>; accents off the name (${q.prog.name})`);
  ok(throws(() => progParseRpc("<ripchord><preset></preset></ripchord>", "x.rpc"), /no <input>/) && throws(() => progParseRpc("<foo/>", "x.rpc"), /not a Ripchord/) &&
     throws(() => progParseRpc("", "x.rpc")), "rpc: no input, no <ripchord>, an empty file: refused with the reason");
  const odd = progParseRpc('<ripchord><preset><input note="200"><chord notes="1"/></input><input note="61"><chord notes="64;60;60;x;300"/></input>' +
    '<input note="61"><chord notes="62;65"/></input></preset></ripchord>', "odd.rpc");
  ok(odd.prog.base === 61 && eq(odd.prog.keys, [[62, 65]]) && odd.notes.some((n) => /outside 0\.\.127 left out/.test(n)) && odd.notes.some((n) => /twice/.test(n)),
     "rpc: a trigger outside 0..127 and junk notes left out (flagged); a trigger twice: the last kept");
  ok(progCleanName("  ") === "" && progParseRpc('<ripchord><input note="1"><chord notes="1"/></input></ripchord>', "☃.rpc").prog.name === "Ripchord" &&
     progCleanName("a\tb\u0007c") === "a bc", "rpc: a name of no printable ASCII: \"Ripchord\"; control characters out");
  // export -> import: the same progression
  for (const p of [g.prog, w.prog, bc.prog, j.prog, q.prog, A, { name: "Lead Gap", base: 50, keys: [[], [60, 64], [], [62], []] }]) {
    const text = progToRpc(p), r = progParseRpc(text, progRpcFileName(p));
    ok(progSame(r.prog, p) && !r.notes.length, `export -> import: ${p.name} (${p.keys.length} keys) the same`);
  }
  const t = progToRpc(A);
  ok(t.startsWith('<?xml version="1.0" encoding="UTF-8"?>') && t.includes('<input note="48">') && t.includes('<chord name="C2 C3 E3 G3" notes="36;48;52;55"/>') &&
     !t.includes('note="50"') && t.includes('<input note="51">'), "export: Ripchord's layout, names as Ripchord writes them (sharps, 60 = C4); a silent key inside left out");
  ok(progRpcFileName({ name: "A/B: C?" }) === "A_B_ C_.rpc" && progRpcFileName(progEmpty()) === "progression.rpc" && throws(() => progToRpc(progEmpty())),
     "export: a file name the file system takes; an empty progression has nothing to export");
}

/* ------------------------------------------------------- names, editing --- */
ok(["C", "Am", "G7", "Cmaj7", "Dm7", "F/A", "Bdim", "Csus4", "C9"].join() === [[48, 52, 55], [57, 60, 64], [43, 47, 50, 53], [48, 52, 55, 59], [50, 53, 57, 60], [45, 53, 60, 65], [47, 50, 53],
   [48, 53, 55], [48, 52, 55, 58, 62]].map(progChordName).join() && progChordName([]) === "—" && progChordName([40, 52, 55, 59, 62, 66]) === "Em9",
   "names: triads, sevenths, a slash chord, Em9 of the example's first chord");
ok(progNoteName(60) === "C4" && progNoteName(61) === "C#4" && progNoteName(0) === "C-1" && progNoteName(127) === "G9", "names: note names (60 = C4, sharps)");
ok(PROG_KEYBEDS.fm1.keys === 27 && PROG_KEYBEDS.fm1.low === 53 && PROG_KEYBEDS.smk.keys === 37 && PROG_KEYBEDS.smk.low === 48, "keybeds: FM-1 27 keys F3..G5, SMK-37 37 keys C3..C6");
{
  let p = progEmpty();
  p = progToggleNote(p, 4, 64); p = progToggleNote(p, 4, 60); p = progToggleNote(p, 4, 67);
  ok(p.base === 48 && p.keys.length === 5 && eq(p.keys[4], [60, 64, 67]) && p.keys.slice(0, 4).every((c) => !c.length), "edit: notes into key 5 of an empty slot (base C3, keys 1..4 silent)");
  p = progToggleNote(p, 4, 64);
  ok(eq(p.keys[4], [60, 67]), "edit: a note clicked again comes out");
  let q = p; for (const n of [70, 72, 74, 76, 79, 81]) q = progToggleNote(q, 4, n);
  ok(q.keys[4].length === 8 && throws(() => progToggleNote(q, 4, 83), /at most 8/), "edit: a ninth note refused");
  ok(progIsEmpty(progSetChord(p, 4, [])) && eq(progSetChord(p, 1, [72, 60, 60]).keys[1], [60, 72]) && throws(() => progSetChord(p, 48, [60])),
     "edit: Clear of the last chord drops the trailing silent keys; Paste sorts and dedupes; key 49 refused");
  ok(eq(progTidy({ name: "x", base: 1, keys: [[1], [], []] }).keys, [[1]]) && progNew().base === 48 && progCount(p) === 1, "edit: tidy, a new progression");
  const s2 = progPlace(set, 9, A);
  ok(progSame(s2[9], A) && s2[9] !== A && progIsEmpty(set[9]) && throws(() => progPlace(set, 16, A)), "place: into slot 10 (a copy; the set before untouched); slot 17 refused");
}

/* ------------------------------------------------- library items, the factory stand-in --- */
{
  const it = progItemsFromFile("Gaps Test.rpc", fix("Gaps Test.rpc"));
  ok(it.length === 1 && it[0].type === "prog" && it[0].name === "Gaps Test" && progSame(it[0].prog, progParseRpc(fix("Gaps Test.rpc"), "Gaps Test.rpc").prog), "library: a .rpc -> one item");
  const fac = progSimFactory(), b64 = Buffer.from(fac).toString("base64");
  const backup = { format: "felucca-backup", version: 1, firmware: "ChoralRoot 0.42", created: "2026-11-18T00:00:00Z",
    objects: [{ id: PROG_ID, size: fac.length, crc: bkCrc(fac), data: b64 }] };
  const items = progItemsFromFile("choralroot-backup.json", JSON.stringify(backup));
  ok(items.length === 6 && items[0].name === "Pop Four" && /slot 1$/.test(items[0].source), "library: a backup holding id 24 -> its six progressions");
  ok(throws(() => progItemsFromFile("x.txt", "hello"), /not a Ripchord/), "library: another file refused");
  const f = progDecode(fac);
  ok(fac.length === 560 && f.slice(0, 6).every((p) => p.keys.length === 8 && p.base === 48 && p.keys.every((c) => c.length === 5)) && f.slice(6).every(progIsEmpty),
     "simulator's factory stand-in: the firmware's shape (slots 1..6, 8 keys of 5 notes, base 48, 560 bytes)");
}

/* ------------------------------------------------- the owner's example folder (if present) --- */
{
  const D = join(HERE, "..", "..", "..", "..", "..", "tmp", "ChoralRoot", "ChordProgressions");
  const files = [];
  const walk = (d) => { for (const n of readdirSync(d)) { const p = join(d, n); if (statSync(p).isDirectory()) walk(p); else if (/\.rpc$/i.test(n)) files.push(p); } };
  const roots = [D, join(HERE, "..", "..", "tmp", "ChoralRoot", "ChordProgressions"), process.env.CR_RPC_DIR || ""].filter((d) => d && existsSync(d));
  if (roots.length) walk(roots[0]);
  if (files.length) {
    let bad = 0, round = 0, cut = 0; const why = [];
    for (const f of files) {
      try {
        const r = progParseRpc(readFileSync(f, "utf8"), f.split("/").pop());
        if (progBytes(r.prog) > PROG_SIZE - PROG_DATA || r.prog.keys.length > PROG_KEYS || r.prog.keys.some((c) => c.length > PROG_NOTES)) throw new Error("does not fit");
        if (!progSame(progParseRpc(progToRpc(r.prog), progRpcFileName(r.prog)).prog, r.prog)) round++;
        if (r.notes.length) cut++;
        progEncode([r.prog]);
      } catch (e) { bad++; why.push(`${f.split("/").pop()}: ${e.message}`); }
    }
    ok(!bad && !round, `examples: ${files.length} .rpc files parse, encode and round-trip (${cut} with import notes)${why.length ? ": " + why.slice(0, 3).join("; ") : ""}`);
  } else console.log("examples: the owner's Ripchord folder is not here: skipped");
}

if (failed) { console.log(`${failed} progression test(s) FAILED`); process.exit(1); }
console.log("progression tests passed");
