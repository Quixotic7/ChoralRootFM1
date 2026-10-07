// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// fm1sounds.js (docs/SOUNDS.md) with synthetic objects: the slot table (valid / invalid records, matching and stale
// blobs), export -> file -> import round trips, rename, delete, empty objects created with their headers, only the
// changed objects written (the stores before the bank), the file checks, and the reads / writes against a simulated
// ChoralRoot (busy retries, a refused commit that stops before the bank, a device that changes during the read).
//   node web/test_sounds.mjs
import { CR_BACKUP_IDS, BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc } from "./fm1backup.js";
import { SOUND_IDS, ENGINE_NAMES, PATCH_SIZE, patchKindOf, parseSoundObjects, exportSound, soundFileName, readSoundFile,
  importSound, renameSound, deleteSound, readSounds, writeSounds, recordValid, SOUND_TEMPLATES, FM6_FN_DEFAULTS, fm6BlobToVced,
  vcedToFm6Blob, fm6VcedSyx, czToneSyx, parseSyx, soundFromSyx, exportSyx, syxFileName } from "./fm1sounds.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throwsMsg = (f, re) => { try { f(); return false; } catch (e) { return re.test(e.message) ? true : (console.log(`  (message: ${e.message})`), false); } };
const athrowsMsg = async (f, re) => { try { await f(); return false; } catch (e) { return re.test(e.message) ? true : (console.log(`  (message: ${e.message})`), false); } };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const view = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const b64 = (bytes) => Buffer.from(bytes).toString("base64");

/* ---------------------------------------------------------------- synthetic objects --- */
function rec(engine, name, { ver = 4, np = 40, seed = 1 } = {}) {
  const r = new Uint8Array(192);
  r[0] = 0xA5; r[1] = ver; r[2] = engine; r[3] = np;
  for (let i = 0; i < name.length; i++) r[4 + i] = name.charCodeAt(i);
  for (let i = 0; i < 144; i++) r[16 + i] = ver >= 4 ? (i * 7 + seed) % 192 : (i * 3 + seed) & 255;
  for (let i = 160; i < 192; i++) r[i] = (i + seed) & 255;
  return r;
}
function bank(recs) {
  const b = new Uint8Array(3080), v = view(b);
  v.setUint32(0, 0x31425055, true); v.setUint16(4, 192, true); v.setUint16(6, 16, true);
  for (const [i, r] of Object.entries(recs)) b.set(r, 8 + Number(i) * 192);
  return b;
}
const vaBlob = (seed) => { const b = Uint8Array.from({ length: 110 }, (_, i) => (i * 5 + seed) & 127); b[0] = 0x56; b[1] = 3; return b; };
const fm6Blob = (seed) => { const b = Uint8Array.from({ length: 128 }, (_, i) => (i * 3 + seed) & 127); b[112] = 0x46; b[113] = 1; return b; };
const czBlob = (seed) => Uint8Array.from({ length: 144 }, (_, i) => (i * 11 + seed) & 255);
function vaStore(blobs) {                       // {slot: blob}
  const b = new Uint8Array(3536), v = view(b);
  v.setUint32(0, 0x31534156, true); v.setUint16(4, 3, true); v.setUint16(6, 32, true); v.setUint16(12, 110, true);
  let used = 0;
  for (const [s, blob] of Object.entries(blobs)) { b.set(blob, 16 + (s - 1) * 110); used |= 1 << (s - 1); }
  v.setUint32(8, used >>> 0, true);
  return b;
}
function half(kind, h, blobs) {                 // kind fm6 / cz, half 0 / 1, {slot: blob}
  const n = PATCH_SIZE[kind], b = new Uint8Array(16 + 16 * n), v = view(b);
  v.setUint32(0, kind === "fm6" ? 0x55364D46 : 0x55315A43, true); v.setUint16(4, 1, true); v.setUint16(6, 16, true);
  v.setUint16(8, h * 16, true); v.setUint16(10, n, true);
  let used = 0;
  for (const [s, blob] of Object.entries(blobs)) { const k = (s - 1) % 16; b.set(blob, 16 + k * n); used |= 1 << k; }
  v.setUint32(12, used, true);
  return b;
}
const recs6 = {
  0: rec(13, "MY PAD", { seed: 2 }),            // U01 VA, its blob
  1: rec(12, "TINE 2", { seed: 3 }),            // U02 FM6, its blob
  2: rec(14, "BRASSY TONES", { seed: 4 }),      // U03 CZ-1, 12 characters, its blob
  3: rec(0, "BASS", { ver: 3, np: 60, seed: 5 }), // U04 ANALOG, an older layout
  4: (() => { const r = rec(6, "BROKEN", { seed: 6 }); r[1] = 9; return r; })(),   // U05 invalid (ver 9): empty
  5: rec(13, "NO PATCH", { seed: 7 }),          // U06 VA without its blob
};
const recs7 = { 0: rec(14, "CZ17", { seed: 8 }), 3: rec(12, "FM20", { seed: 9 }) };   // U17 CZ-1 (blob), U20 FM6 (no blob)
const blobs = { va1: vaBlob(1), fm2: fm6Blob(2), cz3: czBlob(3), cz17: czBlob(17), staleVa7: vaBlob(7), staleFm1: fm6Blob(11) };
const source = () => new Map([
  [6, bank(recs6)], [7, bank(recs7)],
  [9, vaStore({ 1: blobs.va1, 7: blobs.staleVa7 })],                         // U07's blob is stale (empty slot)
  [10, half("fm6", 0, { 2: blobs.fm2, 1: blobs.staleFm1 })], [11, new Uint8Array(0)],   // U01's FM6 blob is stale (VA)
  [12, half("cz", 0, { 3: blobs.cz3 })], [13, half("cz", 1, { 17: blobs.cz17 })],
]);

/* ------------------------------------------------------------------------ the table --- */
ok(SOUND_IDS.join() === "6,7,9,10,11,12,13" && ENGINE_NAMES.length === 15 && ENGINE_NAMES[13] === "VA" && ENGINE_NAMES[14] === "CZ-1" &&
   patchKindOf(13) === "va" && patchKindOf(12) === "fm6" && patchKindOf(14) === "cz" && patchKindOf(0) === null && patchKindOf(1) === null,
   "constants: ids, 15 engines, patch kinds");
const src = source();
const { slots } = parseSoundObjects(src);
const S = (n) => slots[n - 1];
ok(slots.length === 32 && slots.every((s, i) => s.slot === i + 1), "parse: 32 slots");
ok(S(1).used && S(1).name === "MY PAD" && S(1).engine === 13 && S(1).engineName === "VA" && S(1).patch === "va", "parse: U01 VA with its patch");
ok(S(2).name === "TINE 2" && S(2).engineName === "FM6" && S(2).patch === "fm6", "parse: U02 FM6 with its patch");
ok(S(3).name === "BRASSY TONES" && S(3).engineName === "CZ-1" && S(3).patch === "cz", "parse: U03 a 12-character name (no terminator), CZ-1 with its tone");
ok(S(4).used && S(4).name === "BASS" && S(4).engineName === "ANALOG" && S(4).patch === null, "parse: U04 ANALOG (version 3 record), no patch kind");
ok(!S(5).used && S(5).name === "", "parse: U05 an invalid record (ver 9) reads as empty");
ok(S(6).used && S(6).engineName === "VA" && S(6).patch === null && S(6).kind === "va", "parse: U06 VA without its blob: patch none");
ok(!S(7).used && S(17).used && S(17).patch === "cz" && S(20).used && S(20).patch === null && !S(32).used,
   "parse: U07 empty (its VA blob is stale), U17 CZ-1 (second half), U20 FM6 without blob");
ok(parseSoundObjects({}).slots.every((s) => !s.used) && parseSoundObjects({ 6: new Uint8Array(100) }).slots.every((s) => !s.used),
   "parse: no objects / a foreign bank: every slot empty");
{
  const bad = bank(recs6); view(bad).setUint16(6, 15, true);
  ok(parseSoundObjects({ 6: bad }).slots.every((s) => !s.used), "parse: a bank with another slot count reads as empty");
  const r = rec(13, "X", { np: 20 }); r[16 + 5] = 192;
  ok(!recordValid(r) && recordValid(rec(13, "X", { np: 20 })) && !recordValid(rec(15, "X")) && !recordValid(rec(1, "")) &&
     !recordValid(rec(1, "X", { ver: 3, np: 73 })) && recordValid(rec(1, "X", { ver: 3, np: 72 })) && !recordValid(rec(1, "X", { np: 7 })),
     "parse: the validity test (packed > 191, engine 15, no name, np range)");
}

/* ---------------------------------------------------------------- export / file --- */
const created = "2026-10-07T12:00:00Z";
const f1 = exportSound(src, 1, { firmware: "ChoralRoot 0.14", created });
ok(f1.format === "choralroot-sound" && f1.version === 1 && f1.firmware === "ChoralRoot 0.14" && f1.created === created && f1.slot === 1 &&
   f1.name === "MY PAD" && f1.engine === 13 && f1.engineName === "VA" && f1.record === b64(recs6[0]) &&
   f1.patch.kind === "va" && f1.patch.data === b64(blobs.va1), "export: U01 -> the file of the doc (record, VA patch)");
ok(exportSound(src, 6).patch === null && exportSound(src, 4).patch === null, "export: no blob / no patch kind -> patch null");
ok(throwsMsg(() => exportSound(src, 5), /U05 is empty/) && throwsMsg(() => exportSound(src, 33), /1\.\.32/), "export: an empty slot, slot 33 refused");
ok(soundFileName(5, "MY PAD") === "choralroot-sound-U05-MY_PAD.json" && soundFileName(12, "a/b.c-d") === "choralroot-sound-U12-a_b_c-d.json",
   "export: file names");
const s1 = readSoundFile(JSON.stringify(f1));
ok(same(s1.record, recs6[0]) && s1.name === "MY PAD" && s1.engine === 13 && s1.kind === "va" && same(s1.patch, blobs.va1), "file: JSON text -> record, name, patch");

/* ------------------------------------------------------------------------- import --- */
{
  // export U01 (VA) -> import into U07 (empty; a stale VA blob there): the record and the patch byte for byte
  const r = importSound(src, 7, readSoundFile(f1));
  const t = parseSoundObjects(r.objs).slots[6];
  ok(t.used && t.name === "MY PAD" && t.patch === "va" && same(r.objs.get(6).subarray(8 + 6 * 192, 8 + 7 * 192), recs6[0]) &&
     same(r.objs.get(9).subarray(16 + 6 * 110, 16 + 7 * 110), blobs.va1), "import: U01 -> U07: record and VA patch byte for byte");
  ok(r.changed.map((c) => c.id).join() === "9,6", "import: only the VA store and bank 0 change, the store first");
  ok(exportSound(r.objs, 7).record === exportSound(src, 1).record && exportSound(r.objs, 7).patch.data === f1.patch.data,
     "import: re-export of U07 = the export of U01");
  ok([...r.objs.keys()].every((id) => id === 6 || id === 9 || r.objs.get(id) === src.get(id)), "import: the other objects untouched");
}
{
  // a CZ-1 sound into U01 (VA, with a stale FM6 blob): the VA and FM6 bits cleared, the CZ-1 tone set
  const f3 = exportSound(src, 3);
  const r = importSound(src, 1, f3);
  const va = r.objs.get(9), fm = r.objs.get(10), cz = r.objs.get(12);
  ok(!(view(va).getUint32(8, true) & 1) && va.subarray(16, 126).every((b) => !b) && (view(va).getUint32(8, true) >>> 6 & 1) === 1,
     "import: the other kinds' bit cleared and bytes zeroed (VA U01; U07's bit kept)");
  ok(!(view(fm).getUint32(12, true) & 1) && fm.subarray(16, 144).every((b) => !b) && (view(fm).getUint32(12, true) >>> 1 & 1) === 1,
     "import: ... FM6 U01 (stale) cleared; U02's kept");
  ok((view(cz).getUint32(12, true) & 1) === 1 && same(cz.subarray(16, 160), blobs.cz3), "import: the CZ-1 tone into U01");
  ok(r.changed.map((c) => c.id).join() === "9,10,12,6", "import: changed = VA, FM6 half 0, CZ-1 half 0, then the bank");
  const t = parseSoundObjects(r.objs).slots[0];
  ok(t.name === "BRASSY TONES" && t.engineName === "CZ-1" && t.patch === "cz", "import: U01 reads as the CZ-1 sound");
}
{
  // the JSON's name renames; engine in the JSON must agree
  const f = { ...exportSound(src, 2), name: "NEW NAME" };
  const r = importSound(src, 9, readSoundFile(f));
  const rec9 = r.objs.get(6).subarray(8 + 8 * 192, 8 + 9 * 192);
  ok(parseSoundObjects(r.objs).slots[8].name === "NEW NAME" && rec9.subarray(4, 16).every((b, i) => b === ("NEW NAME".charCodeAt(i) || 0)) &&
     same(rec9.subarray(16), recs6[1].subarray(16)) && rec9[2] === 12, "import: the JSON's name is written into the record (zero-padded)");
  ok(r.changed.map((c) => c.id).join() === "10,6", "import: an FM6 sound writes FM6 half 0 then the bank");
  const g = { ...exportSound(src, 2) }; delete g.name; g.engine = null;
  ok(readSoundFile(g).name === "TINE 2", "import: a missing name keeps the record's");
}
{
  // empty objects created from the headers: a sound into U20 (FM6 half 1 is size 0) and everything into a blank FM-1
  const blank = new Map(SOUND_IDS.map((id) => [id, new Uint8Array(0)]));
  const rv = importSound(blank, 32, exportSound(src, 1));
  const v = rv.objs.get(9), b = rv.objs.get(7);
  ok(rv.changed.map((c) => c.id).join() === "9,7" && v.length === 3536 && b.length === 3080 &&
     view(v).getUint32(0, true) === 0x31534156 && view(v).getUint16(4, true) === 3 && view(v).getUint16(6, true) === 32 &&
     view(v).getUint32(8, true) === 0x80000000 && view(v).getUint16(12, true) === 110 && view(v).getUint16(14, true) === 0 &&
     same(v.subarray(16 + 31 * 110), blobs.va1) && v.subarray(16, 16 + 31 * 110).every((x) => !x),
     "empty: a VA store created (VAS1, ver 3, 32 slots, used bit 31, blob 110), 3536 bytes");
  ok(view(b).getUint32(0, true) === 0x31425055 && view(b).getUint16(4, true) === 192 && view(b).getUint16(6, true) === 16 &&
     same(b.subarray(8 + 15 * 192), recs6[0]) && b.subarray(8, 8 + 15 * 192).every((x) => !x), "empty: bank 1 created (UPB1, 192, 16), 3080 bytes, U32's record");
  const rf = importSound(blank, 20, exportSound(src, 2)), f = rf.objs.get(11);
  ok(f.length === 2064 && view(f).getUint32(0, true) === 0x55364D46 && view(f).getUint16(4, true) === 1 && view(f).getUint16(6, true) === 16 &&
     view(f).getUint16(8, true) === 16 && view(f).getUint16(10, true) === 128 && view(f).getUint32(12, true) === 1 << 3 &&
     same(f.subarray(16 + 3 * 128, 16 + 4 * 128), blobs.fm2), "empty: an FM6 half 1 created (FM6U, ver 1, 16, first 16, blob 128, bit 3), 2064 bytes");
  const rc = importSound(blank, 17, exportSound(src, 3)), c = rc.objs.get(13);
  ok(c.length === 2320 && view(c).getUint32(0, true) === 0x55315A43 && view(c).getUint16(4, true) === 1 && view(c).getUint16(6, true) === 16 &&
     view(c).getUint16(8, true) === 16 && view(c).getUint16(10, true) === 144 && view(c).getUint32(12, true) === 1 &&
     same(c.subarray(16, 160), blobs.cz3), "empty: a CZ-1 half 1 created (CZ1U, ver 1, 16, first 16, blob 144, bit 0), 2320 bytes");
  const rc0 = importSound(blank, 1, exportSound(src, 3));
  ok(rc0.objs.get(12).length === 2320 && view(rc0.objs.get(12)).getUint16(8, true) === 0 && rc0.changed.map((x) => x.id).join() === "12,6",
     "empty: a CZ-1 half 0 (first 0); the empty VA / FM6 stores are not written");
  const ra = importSound(blank, 4, exportSound(src, 4));
  ok(ra.changed.map((x) => x.id).join() === "6", "empty: an ANALOG sound writes the bank only");
}
{
  // a VA store of version 2 (converted at the FM-1's boot): no VA blob written into it
  const old = source(); const v = old.get(9).slice(); view(v).setUint16(4, 2, true); old.set(9, v);
  ok(parseSoundObjects(old).slots[0].patch === "va", "VA store version 2: read");
  ok(throwsMsg(() => importSound(old, 7, exportSound(src, 1)), /older version/), "VA store version 2: a VA patch is not written into it");
  ok(importSound(old, 1, exportSound(src, 4)).changed.map((x) => x.id).join() === "9,10,6", "VA store version 2: clearing a slot in it is fine");
}

/* ------------------------------------------------------------------ rename / delete --- */
{
  const r = renameSound(src, 3, "LEAD");
  const before = src.get(6), after = r.objs.get(6);
  const diff = [...after].map((b, i) => (b !== before[i] ? i : -1)).filter((i) => i >= 0);
  ok(r.changed.map((c) => c.id).join() === "6" && diff.every((i) => i >= 8 + 2 * 192 + 4 && i < 8 + 2 * 192 + 16) &&
     parseSoundObjects(r.objs).slots[2].name === "LEAD", `rename: only the bank, only the 12 name bytes (${diff.length} differ)`);
  ok(renameSound(src, 3, "BRASSY TONES").changed.length === 0, "rename: the same name: nothing to write");
  ok(throwsMsg(() => renameSound(src, 5, "X"), /U05 is empty/) && throwsMsg(() => renameSound(src, 1, ""), /1 to 12/) &&
     throwsMsg(() => renameSound(src, 1, "THIRTEEN CHAR"), /1 to 12/) && throwsMsg(() => renameSound(src, 1, "café"), /1 to 12/),
     "rename: an empty slot, an empty / 13-character / non-ASCII name refused");
}
{
  const r = deleteSound(src, 1);
  const rec1 = r.objs.get(6).subarray(8, 200), va = r.objs.get(9), fm = r.objs.get(10);
  ok(rec1.every((b) => !b) && !parseSoundObjects(r.objs).slots[0].used, "delete: the record zeroed (192 x 0)");
  ok(!(view(va).getUint32(8, true) & 1) && va.subarray(16, 126).every((b) => !b) && !(view(fm).getUint32(12, true) & 1) &&
     fm.subarray(16, 144).every((b) => !b), "delete: the VA and (stale) FM6 bits cleared, their bytes zeroed");
  ok(r.changed.map((c) => c.id).join() === "9,10,6", "delete: changed = the stores that held a bit, then the bank");
  const r17 = deleteSound(src, 17);
  ok(r17.changed.map((c) => c.id).join() === "13,7" && !(view(r17.objs.get(13)).getUint32(12, true) & 1), "delete: U17 clears CZ-1 half 1 and bank 1");
  ok(deleteSound(src, 30).changed.length === 0 && deleteSound(new Map(), 3).changed.length === 0, "delete: an empty slot / a blank FM-1: nothing to write");
}

/* ---------------------------------------------------------------------- file checks --- */
{
  const good = exportSound(src, 1), j = (o) => JSON.stringify(o);
  const mut = (f) => { const o = JSON.parse(j(good)); f(o); return o; };
  ok(throwsMsg(() => readSoundFile(j({ format: "felucca-backup", version: 1, objects: [] })), /whole backup, not a sound file/), "file: a felucca-backup file -> a whole backup");
  ok(throwsMsg(() => readSoundFile("{nope"), /not JSON/) && throwsMsg(() => readSoundFile(j({ format: "other" })), /Not a ChoralRoot sound file/) &&
     throwsMsg(() => readSoundFile(mut((o) => { o.version = 2; })), /version 2/), "file: not JSON, another format, version 2");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.record = "@@@"; })), /record is not base64/), "file: bad base64");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.record = b64(new Uint8Array(191)); })), /191 bytes, not 192/), "file: wrong record length");
  ok(throwsMsg(() => readSoundFile(mut((o) => { const r = recs6[0].slice(); r[0] = 0; o.record = b64(r); })), /not a valid user sound/), "file: invalid record (not used)");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.patch.kind = "fm6"; o.patch.data = b64(blobs.fm2); })), /not the kind of its engine VA/), "file: a patch of another kind");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.record = b64(recs6[3]); o.engine = 0; })), /not the kind of its engine ANALOG/), "file: a patch for an engine without one");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.patch.data = b64(blobs.va1.subarray(0, 100)); })), /100 bytes, not 110/), "file: wrong blob length");
  ok(throwsMsg(() => readSoundFile(mut((o) => { const b = blobs.va1.slice(); b[0] = 0x57; o.patch.data = b64(b); })), /magic or version/) &&
     throwsMsg(() => readSoundFile(mut((o) => { const b = blobs.va1.slice(); b[1] = 4; o.patch.data = b64(b); })), /magic or version/), "file: VA blob magic / version");
  const fm = exportSound(src, 2);
  ok(throwsMsg(() => readSoundFile({ ...fm, patch: { kind: "fm6", data: b64(Uint8Array.from(blobs.fm2, (b, i) => (i === 113 ? 2 : b))) } }), /FM6 patch is not/),
     "file: FM6 blob magic");
  ok(["", "THIRTEEN CHAR", "tab\there", 5].every((n) => throwsMsg(() => readSoundFile(mut((o) => { o.name = n; })), /name must be 1 to 12/)), "file: name not 1..12 printable ASCII");
  ok(throwsMsg(() => readSoundFile(mut((o) => { o.engine = 12; })), /says engine 12, its record holds 13/), "file: engine in the JSON disagrees with the record");
  ok(readSoundFile(mut((o) => { o.patch = null; })).patch === null, "file: patch null accepted (the slot plays the engine's defaults)");
}

/* ------------------------------------------------- the FM-1: read / write (simulated) --- */
// test_backup.mjs's device(), with ChoralRoot's ids; opt.failCommit: rc 2 at that id's commit
function device(objs, opt = {}) {
  const ids = opt.ids || CR_BACKUP_IDS;
  const d = { objs: new Map(objs), log: [], staged: null, busy: opt.busy || 0, gets: 0 };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      d.log.push("list");
      const out = [1, 0, ids.length];
      for (const id of ids) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      d.gets++;
      if (opt.changeOnGet && opt.changeOnGet.id === id && off === 0 && opt.changeOnGet.times-- > 0) v[100] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0) {
        if (!ids.includes(id)) return [op, id, 1];
        if (d.busy) { d.busy--; d.log.push(`busy ${id}`); return [op, id, 3]; }
        d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0];
      }
      if (op === 1) { const off = bkR32(a, 2), n = Math.min(256, d.staged.size - off); d.staged.bytes.push(...bkUnpack(a.slice(7), n)); return [op, id, 0]; }
      if (op === 2) {
        const s = Uint8Array.from(d.staged.bytes);
        const rc = opt.failCommit === id || bkCrc(s) !== d.staged.crc ? 2 : 0;
        if (!rc) { d.objs.set(id, s); d.log.push(id); }
        return [op, id, rc];
      }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const crExtra = [[1, Uint8Array.from({ length: 764 }, (_, i) => i & 255)], [40, Uint8Array.from({ length: 46 }, (_, i) => i)]];
{
  const dev = device([...source(), ...crExtra]);
  let prog = 0;
  const { objs } = await readSounds(dev.request, (d, total) => { prog = d / total; });
  ok(SOUND_IDS.every((id) => same(objs.get(id), source().get(id))) && objs.get(11).length === 0 && !objs.has(1) && !objs.has(40) && prog === 1,
     "read: LIST + GET of the seven objects (an empty one as 0 bytes), nothing else");
  ok(dev.gets === Math.ceil(3080 / 256) * 2 + Math.ceil(3536 / 256) + Math.ceil(2064 / 256) + 2 * Math.ceil(2320 / 256), "read: 256-byte pieces");
  const once = device([...source()], { changeOnGet: { id: 9, times: 1 } });
  await readSounds(once.request);
  ok(once.log.filter((x) => x === "list").length === 2, "read: a CRC mismatch (the FM-1 changed): read again once");
  const always = device([...source()], { changeOnGet: { id: 9, times: 9 } });
  ok(await athrowsMsg(() => readSounds(always.request), /changed during the read/), "read: a second mismatch -> the FM-1 changed during the read");
  ok(await athrowsMsg(() => readSounds(device([[6, bank(recs6)]], { ids: [0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34] }).request), /ChoralRoot needed/),
     "read: Felucca's list (no stores) refused");

  // import U03 (CZ-1) into U01: VA, FM6, CZ-1 half 0, then bank 0; one busy answer retried
  const busy = device([...source(), ...crExtra], { busy: 1 });
  const { objs: o2 } = await readSounds(busy.request);
  const r = importSound(o2, 1, exportSound(o2, 3));
  let busies = 0, p = 0;
  const written = await writeSounds(busy.request, r.changed, { onBusy: () => busies++, onProgress: (d, t) => { p = d / t; } });
  ok(written.join() === "9,10,12,6" && busy.log.filter((x) => typeof x === "number").join() === "9,10,12,6" && busy.log.includes("busy 9") && busies === 1 && p === 1,
     "write: the stores, then the bank; a busy begin retried after a second");
  const { objs: o3 } = await readSounds(busy.request);
  ok(parseSoundObjects(o3).slots[0].name === "BRASSY TONES" && SOUND_IDS.every((id) => same(o3.get(id), r.objs.get(id))) && same(busy.objs.get(1), crExtra[0][1]),
     "write: read back = the edited objects; the settings untouched");

  // a refused commit (rc 2) on the CZ-1 store: aborted, nothing more written (the bank keeps the old record)
  const refuse = device([...source()], { failCommit: 12 });
  const r2 = importSound((await readSounds(refuse.request)).objs, 1, exportSound(src, 3));
  ok(await athrowsMsg(() => writeSounds(refuse.request, r2.changed), /refused the CZ-1 tones 1-16 \(rc 2\)/) &&
     refuse.log.filter((x) => typeof x === "number").join() === "9,10" && refuse.log.includes("abort 12") && same(refuse.objs.get(6), source().get(6)),
     "write: rc 2 at a commit -> reported with the object, aborted, the bank not written");
  const stubborn = device([...source()], { busy: 99 });
  ok(await athrowsMsg(() => writeSounds(stubborn.request, renameSound(source(), 1, "X").changed, { busyTries: 1 }), /busy/), "write: still busy after the retries: stops");
}

/* -------------------------------------------------------------- .syx (docs/SOUNDS.md) --- */
// the fixtures of tests/sound_templates.c (factory F1 TINE EP's blob and voice, the packed function defaults, Casio's
// A-1 BRASS 1)
const FX = {
  fm6_blob: "XygePGNQACeAAAA4DDQAXx4UPGNaAKeAgAAwhAIA4b6ovGMAACeAgAA7xJwAXxSUsl+AACcAAIAI2oKAXzKjY0sAACcAALscOoKAYBlDY0sAACcAgLsIYgKA4+PjMrIyMgSiIQCAKRjUTkUgRVCgIEYBMwBgHAAAAEAAAAAAAAA=",
  fm6_vced: "XygePGNQAAAnAAAAAAAAAzQAAQAHXx4UPGNaAAAnAAAAAAAAAU4AAQAGYT4oPGM8AAAnAAAAAAMABkQADgAHXxQUMmNfAAAnAAAAAAIAAloAAQAIXzIjTmNLAAAnAAAAAAMABzoAAQAHYBkZQ2NLAAAnAAAAAAMAAmIAAQAHY2NjYzIyMjIEAwEiIQAAAQQCGFRJTkUgRVAgICA=",
  fm6_fn: "MwBgHAAAAEA=",
  cz_tone: "CgEUAAgAAAAy4AkAAQCgIAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQAQV0h1wBAAEAAQABAAEAAQACgAAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQA8V0h1wBAAEAAQABAAEAAQAAgICAgQlJBU1MgMSAgICAg",
};
{
  const B = (s) => Uint8Array.from(Buffer.from(s, "base64"));
  const fBlob = B(FX.fm6_blob), fVced = B(FX.fm6_vced), fFn = B(FX.fm6_fn), fTone = B(FX.cz_tone);
  ok(fBlob.length === 128 && fVced.length === 155 && fFn.length === 8 && fTone.length === 144, "syx: the fixtures' sizes (128, 155, 8, 144)");
  ok(same(FM6_FN_DEFAULTS, fFn) && same(FM6_FN_DEFAULTS, fBlob.subarray(114, 122)), "syx: FM6_FN_DEFAULTS = fm6_fn = the fixture blob's bytes 114..121");
  ok(same(fm6BlobToVced(fBlob), fVced), "syx: fm6BlobToVced(fm6_blob) = fm6_vced byte for byte");
  ok(same(vcedToFm6Blob(fVced), fBlob), "syx: vcedToFm6Blob(fm6_vced) = fm6_blob byte for byte (function defaults included)");
  {
    const wild = fVced.slice(); wild[0] = 127; wild[20] = 127; wild[134] = 127; wild[145] = 5;   // R1, DET, ALG out of range; a name byte
    const back = fm6BlobToVced(vcedToFm6Blob(wild));
    ok(back[0] === 99 && back[20] === 14 && back[134] === 31 && back[145] === 32, "syx: vcedToFm6Blob clamps (R1 99, DET 14, ALG 31, a name byte -> space)");
  }
  ok(throwsMsg(() => fm6BlobToVced(new Uint8Array(128)), /Not an FM6 patch/), "syx: fm6BlobToVced refuses a blob without 'F' 1");

  const one = fm6VcedSyx(fVced), sum = (d) => d.reduce((a, x) => a + x, 0);
  ok(one.length === 163 && same(one.subarray(0, 6), [0xF0, 0x43, 0, 0, 1, 0x1B]) && one[162] === 0xF7 && same(one.subarray(6, 161), fVced) &&
     (sum(one.subarray(6, 162)) & 127) === 0, "syx: fm6VcedSyx: 163 bytes, F0 43 00 00 01 1B, the voice, checksum (sum + check = 0 mod 128), F7");
  const p1 = parseSyx(one);
  ok(p1.kind === "fm6" && p1.voices.length === 1 && same(p1.voices[0], fVced), "syx: parseSyx(single voice) -> the voice back");
  const ch5 = one.slice(); ch5[2] = 0x05;
  ok(same(parseSyx(ch5).voices[0], fVced), "syx: the channel nibble is ignored");
  const bad = one.slice(); bad[161] ^= 1;
  ok(throwsMsg(() => parseSyx(bad), /checksum is wrong/), "syx: a bad checksum refused");

  // a 32-voice bank: the fixture's VMEM 32 times (voice 7 renamed)
  const vmem = new Uint8Array(4096);
  const vm1 = new Uint8Array(128);                // fm6_unpack7 of the fixture blob (the VMEM voice), inline
  for (let i = 0, o = 0; i < 128; i += 8, o += 7) for (let j = 0; j < 7; j++) { vm1[i + j] = fBlob[o + j] & 127; vm1[i + 7] |= (fBlob[o + j] >> 7) << j; }
  for (let k = 0; k < 32; k++) vmem.set(vm1, k * 128);
  "BANK SEVEN".split("").forEach((c, i) => { vmem[6 * 128 + 118 + i] = c.charCodeAt(0); });
  const bankSyx = Uint8Array.from([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00, ...vmem, (128 - (sum(vmem) & 127)) & 127, 0xF7]);
  const pb = parseSyx(bankSyx);
  ok(bankSyx.length === 4104 && pb.kind === "fm6" && pb.voices.length === 32 && pb.voices.every((v, k) => k === 6 || same(v, fVced)),
     "syx: a 32-voice bank (4104 bytes) -> 32 voices, each the fixture's");
  ok(soundFromSyx(pb, 7).name === "BANK SEVEN" && soundFromSyx(pb).name === "TINE EP" && throwsMsg(() => soundFromSyx(pb, 33), /1\.\.32/),
     "syx: soundFromSyx picks the bank's voice (1..32; 7 = BANK SEVEN, default 1)");
  const bbad = bankSyx.slice(); bbad[4102] ^= 1;
  ok(throwsMsg(() => parseSyx(bbad), /bank's checksum is wrong/), "syx: a bank with a bad checksum refused");
  const two = Uint8Array.from([...one, ...ch5]);
  ok(parseSyx(two).voices.length === 2, "syx: two frames in one file -> two voices");
  ok(same(parseSyx(fVced).voices[0], fVced) && parseSyx(vmem).voices.length === 32 && same(parseSyx(vmem).voices[0], fVced) &&
     same(parseSyx(fTone).tones[0], fTone), "syx: raw files (155 voice, 4096 bank, 144 tone)");
  ok(throwsMsg(() => parseSyx(new Uint8Array(100)), /Not a DX7 or CZ-1 file/) && throwsMsg(() => parseSyx(Uint8Array.from([0xF0, 0x41, 1, 2, 0xF7])), /not a DX7/) &&
     throwsMsg(() => parseSyx(one.subarray(0, 100)), /no end/) && throwsMsg(() => parseSyx(new Uint8Array(0)), /empty/),
     "syx: other files refused (a 100-byte file, a Roland frame, a cut frame, an empty file)");

  const cz = czToneSyx(fTone);
  ok(cz.length === 295 && same(cz.subarray(0, 6), [0xF0, 0x44, 0, 0, 0x70, 0x30]) && cz[294] === 0xF7 &&
     cz[6] === (fTone[0] & 15) && cz[7] === fTone[0] >> 4 && cz[6 + 2 * 143] === (fTone[143] & 15) && cz.subarray(6, 294).every((x) => x < 16),
     "syx: czToneSyx: 295 bytes, F0 44 00 00 70 30, the low nibble first, F7");
  const pc = parseSyx(cz);
  ok(pc.kind === "cz" && pc.tones.length === 1 && same(pc.tones[0], fTone), "syx: parseSyx(CZ-1 tone) -> the tone back");
  const cz101 = Uint8Array.from([0xF0, 0x44, 0, 0, 0x73, 0x30, ...new Array(256).fill(1), 0xF7]);
  ok(throwsMsg(() => parseSyx(cz101), /CZ-101 \/ 1000 tone: the FM-1 takes only CZ-1 tones from files/), "syx: a 128-byte CZ-101 tone refused");
  ok(throwsMsg(() => parseSyx(Uint8Array.from([...one, ...cz])), /mixes/), "syx: DX7 and CZ-1 in one file refused");

  // the sounds: the template record with the name, the patch; imported as the doc's operations
  const sf = soundFromSyx(p1), sc = soundFromSyx(pc);
  const tf = Buffer.from(SOUND_TEMPLATES.fm6, "base64"), tc = Buffer.from(SOUND_TEMPLATES.cz, "base64");
  ok(recordValid(sf.record) && sf.engine === 12 && sf.kind === "fm6" && sf.record[3] === tf[3] && sf.record[3] >= 8 && sf.name === "TINE EP" &&
     same(sf.record.subarray(4, 16), [..."TINE EP"].map((c) => c.charCodeAt(0)).concat([0, 0, 0, 0, 0])) && same(sf.record.subarray(16), tf.subarray(16)) &&
     same(sf.patch, fBlob), "syx: soundFromSyx(DX7) -> the FM6 template (engine 12, np), named TINE EP, the blob");
  ok(recordValid(sc.record) && sc.engine === 14 && sc.kind === "cz" && sc.record[3] === tc[3] && sc.name === "BRASS 1" && same(sc.patch, fTone) &&
     same(sc.record.subarray(16), tc.subarray(16)), "syx: soundFromSyx(CZ-1) -> the CZ-1 template (engine 14, np), named BRASS 1, the tone");
  const noname = fVced.slice(); noname.fill(32, 145);
  ok(soundFromSyx({ kind: "fm6", voices: [noname] }).name === "FM6 VOICE" &&
     soundFromSyx({ kind: "cz", tones: [Uint8Array.from(fTone, (x, i) => (i >= 128 ? 0 : x))] }).name === "CZ TONE" &&
     soundFromSyx({ kind: "cz", tones: [Uint8Array.from(fTone, (x, i) => (i >= 128 ? 65 + (i - 128) : x))] }).name === "ABCDEFGHIJKL",
     "syx: names: blank -> FM6 VOICE / CZ TONE, a 16-character CZ name cut to 12");
  const rf = importSound(src, 7, sf), rc = importSound(src, 20, sc);
  ok(rf.changed.map((c) => c.id).join() === "9,10,6" && parseSoundObjects(rf.objs).slots[6].name === "TINE EP" &&
     parseSoundObjects(rf.objs).slots[6].patch === "fm6" && same(rf.objs.get(10).subarray(16 + 6 * 128, 16 + 7 * 128), fBlob),
     "syx: DX7 voice into U07: its stale VA blob cleared, the FM6 half 0, then bank 0");
  ok(rc.changed.map((c) => c.id).join() === "13,7" && parseSoundObjects(rc.objs).slots[19].engineName === "CZ-1" &&
     parseSoundObjects(rc.objs).slots[19].patch === "cz" && same(rc.objs.get(13).subarray(16 + 3 * 144, 16 + 4 * 144), fTone),
     "syx: import of the CZ-1 tone into U20 (was FM6): the CZ-1 half 1 created, then bank 1");

  // export: the slot's patch as .syx; other slots refused
  const ef = exportSyx(rf.objs, 7), ec = exportSyx(src, 3);
  ok(same(ef.bytes, one) && ef.name === "TINE EP" && ef.fileName === "choralroot-sound-U07-TINE_EP.syx", "syx: exportSyx of the imported FM6 slot = the voice's .syx");
  ok(ec.bytes.length === 295 && same(parseSyx(ec.bytes).tones[0], blobs.cz3), "syx: exportSyx of a CZ-1 slot -> its tone");
  ok(throwsMsg(() => exportSyx(src, 1), /only FM6 and CZ-1/) && throwsMsg(() => exportSyx(src, 20), /no FM6 patch/) &&
     throwsMsg(() => exportSyx(src, 5), /U05 is empty/), "syx: exportSyx refuses VA, an FM6 slot without its blob, an empty slot");
  ok(syxFileName(5, "MY PAD") === "choralroot-sound-U05-MY_PAD.syx", "syx: file names");
}

console.log(fails ? `SOUNDS TESTS FAILED (${fails})` : "sounds tests passed");
process.exit(fails ? 1 : 0);
