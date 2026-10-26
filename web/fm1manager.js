// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// The device manager's model (docs/DEVICE-MANAGER.md, web/manager.html): the FM-1's objects (the user sounds' bank and
// stores, the ten loop slots) read and written over the backup protocol; the pools per engine as PRESETS turns through
// them (cr_bank.c pool_entry); Place / Keep / Swap / Rename / Delete as new object bytes; undo (the object set before
// every write); the library (IndexedDB in a browser, a Map in node); files and packs read into library items; the
// simulated FM-1 of ?sim=1 (web/test_installer.mjs's backup side, with sounds and loops in it).
// No DOM: the page (manager.html) draws, this decides. Every name here is unique across fm1backup.js / fm1sounds.js:
// make_site.py inlines them (and fm1dnsnd.js) into one script.
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, objectName, readBackup, CR_LOOP_IDS, CR_BACKUP_IDS, loopHeader } from "./fm1backup.js";
import { nameValid, SOUND_IDS, POOL_IDS, ENGINE_NAMES, FACTORY_PRESETS, FACTORY_FIRST, FACTORY_IDS, SOUND_TEMPLATES, SOUND_FORMAT, PATCH_SIZE, poolOf, engineRetired, patchKindOf, parseSoundObjects, exportSound, readSoundFile, renameSound, deleteSound, writeSounds, placeSound, bindSound, swapSounds, firstFreeSlot, rawBindings, recordValid, slotLabel, isPool, parseSyx, syxCount, soundFromSyx, soundFromDnsnd } from "./fm1sounds.js";
import { parseDnsnd } from "./fm1dnsnd.js";

// the engine picker's order (docs/PRESETS.md; the EDIT picker since 0.14; five engines since 0.18: PHASE, LOFI, VOICE,
// TRIO, NOISE and PHYS retired, their records rows of the VA's pool with recEngine = the engine they were saved on;
// six in 0.22: P5, engine 16, after CZ-1 (engines.c ENGINE_ORDER), its pool INIT P5 and the shipped programs)
export const P5_ENGINE = 16;
export const MANAGER_ENGINES = [12, 15, 13, 14, P5_ENGINE, 7];
// the pools an FM-1 has: P5 only from 0.22 on (the sound pool's objects; a 0.21 FM-1 has no P5 and no P5 store);
// nothing read yet: all of them
export const managerEngines = (objs) => (objs && !isPool(objs) ? MANAGER_ENGINES.filter((e) => e !== P5_ENGINE) : MANAGER_ENGINES);
export const FAVS_ID = 23;                       // the favourites (0.22, cr_favs.c; a firmware before it does not list 23)
export const MANAGER_IDS = [...SOUND_IDS, ...POOL_IDS, FAVS_ID, ...CR_LOOP_IDS];   // (0.22: the pool's banks, 50..62)
export const UNDO_DEPTH = 20;
export const LOOP_FORMAT = "choralroot-loop";
export const PACK_FORMAT = "choralroot-pack";
// the stores before the banks, the pool's banks (0.22: record and patch in one), the favourites after the sounds they
// mark (the backup's order), loops last
const MG_WRITE_ORDER = [9, 10, 11, 12, 13, 22, 6, 7, ...POOL_IDS, FAVS_ID, ...CR_LOOP_IDS];
const mgEmpty = () => new Uint8Array(0);
const mgSame = (a, b) => (a || mgEmpty()).length === (b || mgEmpty()).length && (a || mgEmpty()).every((x, i) => x === b[i]);
const mgB64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
const mgUnB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
export const poolLabel = (engine, pos) => `${ENGINE_NAMES[engine]} ${String(pos).padStart(2, "0")}`;
// a row's (or a library item's) note when its record is of a retired engine: "PHASE · retired", else ""
export const retiredNote = (engine) => (engineRetired(engine) ? `${ENGINE_NAMES[engine]} · retired` : "");

// ---- the pools: objs (Map id -> bytes) -> {engine: [row]} in the firmware's order: 00 INIT, the factory presets
// (each replaced by the first user slot bound to it: overwritten), then the added user sounds in slot order.
// row: {pos, name, type: "init" | "factory" | "user", engine, f (factory index or -1), slot (0: none), label,
//       overwrites (the factory name a user row replaces, or null), kind (patch kind), patch (stored or not)}
export function managerPools(objs, opts = {}) {
  const { slots } = parseSoundObjects(objs), out = {};
  for (const e of MANAGER_ENGINES) {
    const names = FACTORY_PRESETS[e] || [], f0 = FACTORY_FIRST[e] || 0, kind = patchKindOf(e);
    const rows = [{ pos: 0, name: "INIT", type: "init", engine: e, f: -1, slot: 0, label: "", overwrites: null, kind, patch: false }];
    for (let f = f0; f < names.length; f++) {
      const pos = f - f0 + 1, s = slots.find((x) => x.used && x.bound && poolOf(x.engine) === e && x.bound.index === f);
      rows.push(s ? { pos, name: s.name, type: "user", engine: e, f, slot: s.slot, label: s.label, overwrites: names[f], kind: s.kind, patch: !!s.patch, recEngine: s.engine }
        : { pos, name: names[f], type: "factory", engine: e, f, slot: 0, label: "", overwrites: null, kind, patch: true });
    }
    for (const s of slots)
      if (s.used && s.added && poolOf(s.engine) === e)
        rows.push({ pos: rows.length, name: s.name, type: "user", engine: e, f: -1, slot: s.slot, label: s.label, overwrites: null, kind: s.kind, patch: !!s.patch, recEngine: s.engine });
    if (opts.empty !== false && slots.some((x) => !x.used))   // "Place here": a new added sound's place, while a slot is free
      rows.push({ pos: rows.length, name: "", type: "empty", engine: e, f: -1, slot: 0, label: "", overwrites: null, kind, patch: false });
    out[e] = rows;
  }
  return out;
}
// the ten loop slots: [{slot 1..10, id, used, header (loopHeader) | null}]
export function managerLoops(objs) {
  const get = (id) => (objs instanceof Map ? objs.get(id) : objs[id]) || mgEmpty();
  return CR_LOOP_IDS.map((id, i) => { const h = loopHeader(get(id)); return { slot: i + 1, id, used: !!h, header: h }; });
}
export const loopSummary = (h) => (!h ? "empty" : !h.valid ? "unreadable record" :
  `${+h.bars.toFixed(2)} bar${h.bars === 1 ? "" : "s"} ${h.sigName} · ${h.nlayers} layer${h.nlayers === 1 ? "" : "s"} · ${h.nev} events`);

// ---- edits: each -> {changed: [{id, bytes}] in the write order, objs: the Map after}
function mgResult(before, after) {
  const changed = MG_WRITE_ORDER.filter((id) => !mgSame(before.get(id), after.get(id))).map((id) => ({ id, bytes: after.get(id) || mgEmpty() }));
  return { changed, objs: after };
}
const mgMerge = (objs, res) => { const m = new Map(objs); for (const [id, b] of res.objs) m.set(id, b); return m; };
// a sound into the FM-1. target: the row selected on the left (or null). The sound goes to its own engine's pool;
// over a factory row of that pool: a new slot bound to it; over a user row of that pool: that slot (keeping the row's
// binding); otherwise (INIT, another pool, nothing selected) the next free slot as an added sound.
// -> {changed, objs, slot, where: "new" | "over-factory" | "over-user", notes: on a firmware before the pool, what of
// the patch it will not have (fm1sounds.js importSound)}
export function managerPlace(objs, sound, target = null) {
  if (!sound || !(sound.record instanceof Uint8Array)) sound = { ...readSoundFile(sound), file: sound };
  const pool = poolOf(sound.engine);
  let slot, f = -1, where = "new";
  const file = sound.file || null;
  if (!target && file && file.factory && file.engine === pool && Number.isInteger(file.position)) {   // a kept factory preset: back to its place
    const r = managerPools(objs, { empty: false })[pool][file.position];
    if (r && r.type !== "init") target = r;
  }
  if (target && target.engine === pool && target.type === "user") { slot = target.slot; f = target.f; where = "over-user"; }
  else {
    slot = firstFreeSlot(objs);
    if (!slot) throw new Error(`All ${parseSoundObjects(objs).slots.length} user slots are used: delete or overwrite one first`);
    if (target && target.engine === pool && target.type === "factory") { f = target.f; where = "over-factory"; }
  }
  const res = placeSound(objs, slot, sound, f), m = mgMerge(objs, res);
  return { ...mgResult(new Map(objs), m), slot, where, notes: res.notes || [] };
}
// a sound's notes for the confirm / the status line, with its name: ["NAME: note", ..]
const mgNotes = (sound, res) => (res.notes || []).map((n) => `${sound && sound.name ? sound.name : "a sound"}: ${n}`);
// several sounds (a pack): each as a new sound (added, or back over its factory position when the file names one); stops at the last free slot (-> placed, skipped counts)
export function managerPlaceAll(objs, sounds) {
  let m = new Map(objs), placed = 0;
  const notes = [];
  for (const s of sounds) {
    const slot = firstFreeSlot(m);
    if (!slot) break;
    let f = -1;                                         // a kept overwrite / factory preset: back to its position when free
    if (s && Number.isInteger(s.position) && s.engine !== undefined) {
      const r = managerPools(m, { empty: false })[poolOf(s.engine)]?.[s.position];
      if (r && r.type === "factory") f = r.f;
    }
    const res = placeSound(m, slot, s, f);
    m = mgMerge(m, res); placed++; notes.push(...mgNotes(s, res));
  }
  return { ...mgResult(new Map(objs), m), placed, skipped: sounds.length - placed, notes };
}
// Replace factory (DN-3): sounds of one engine's pool over its factory presets in pool order. The pool's model needs no
// new firmware: a user record bound to a factory index (flags[15]) IS that place of the pool and the factory preset is
// not shown while it is there (cr_bank.c pool_entry); the factory sound stays in the firmware and comes back when the
// record is deleted or unbound (Restore factory order). Places already holding a user sound are left as they are (they
// are skipped, not counted). from: the first factory position to take (a drop row's pos; 1 for the button).
// -> plan {items (of this pool), other (of other pools: left out), places (factory rows from `from`), replace, extra, refused}
export function managerReplacePlan(objs, engine, sounds, from = 1) {
  const items = [], other = [];
  for (const s of sounds) { const e = s && s.engine !== undefined ? s.engine : readSoundFile(s).engine; (poolOf(e) === engine ? items : other).push(s); }
  const places = managerPools(objs, { empty: false })[engine].filter((r) => r.type === "factory" && r.pos >= from);
  const free = parseSoundObjects(objs).slots.filter((x) => !x.used).length;
  const replace = Math.min(items.length, places.length, free), extra = Math.min(items.length - replace, free - replace);
  return { items, other, places, replace, extra, refused: items.length - replace - extra, free };
}
export function managerReplaceFactory(objs, engine, sounds, from = 1) {
  const p = managerReplacePlan(objs, engine, sounds, from);
  let m = new Map(objs);
  const notes = [];
  p.items.slice(0, p.replace + p.extra).forEach((s, k) => {
    const res = placeSound(m, firstFreeSlot(m), s, k < p.replace ? p.places[k].f : -1);
    m = mgMerge(m, res); notes.push(...mgNotes(s, res));
  });
  return { ...mgResult(new Map(objs), m), replaced: p.replace, added: p.extra, refused: p.refused, other: p.other.length, notes };
}
// the confirm's text for a plan
export function replaceFactoryText(plan, engine, total) {
  const en = ENGINE_NAMES[engine];
  const parts = [`${plan.replace} factory ${en} sound${plan.replace === 1 ? "" : "s"} will be replaced by the first ${plan.replace} of ${total} selected`];
  if (plan.extra) parts.push(`${plan.extra} more go into free user slots`);
  if (plan.refused) parts.push(`${plan.refused} will not fit`);
  if (plan.other.length) parts.push(`${plan.other.length} of other engines are left out`);
  return `${parts.join("; ")}. The factory sounds are not deleted from the firmware: "Restore factory order" (or Delete / Reset to factory on a row) brings them back. A favourite mark on a replaced factory preset stays with its place: the FM-1 shows it on the imported sound, and it is the factory sound's again after Restore.`;
}
// Restore factory order: every user sound of the engine's pool bound to a factory preset becomes an added sound (kept,
// after the factory presets, slot order); the factory presets are back in their places. No slot is needed. A later slot
// whose binding names a preset an earlier one holds (an added sound already) is unbound too: else it would take the place
export function managerRestoreFactory(objs, engine) {
  let m = new Map(objs), n = 0;
  const bound = new Set(managerPools(objs, { empty: false })[engine].filter((r) => r.type === "user" && r.f >= 0).map((r) => r.slot));
  const { slots } = parseSoundObjects(objs);
  rawBindings(objs).forEach((f, i) => {
    if (f < 0 || !slots[i].used || poolOf(slots[i].engine) !== engine) return;
    m = mgMerge(m, bindSound(m, i + 1, -1)); if (bound.has(i + 1)) n++;
  });
  return { ...mgResult(new Map(objs), m), restored: n };
}

// ---- the favourites (object 23, cr_favs.c crf_pack): version 1 / 2, the number of sections, then per section a tag, a
// length and its bytes. The user slots' sections: 0x80 (slot 1 = bit 0; ids up to 127 in 0.22) and 0x81 (slots 33..128);
// in version 2 a tag | 0x40 holds the sounds typed Chord, | 0x20 those typed Bass; 0xFF the options (the filter). A
// factory preset's mark is under its engine's tag, and a user record over a factory preset has that preset's key: the
// mark is the place's, so it is the factory preset's again once the record is deleted (nothing dangles there). A user
// slot's mark is the slot's: the FM-1's own Delete clears it (crf_slot_new), so the manager does too
const FAV_USER = 0x80, FAV_USER2 = 0x81, FAV_OPT = 0xFF, FAV_CHORD = 0x40, FAV_BASS = 0x20;
function favSections(bytes) {           // -> {ver, secs: [{tag, base, data}]} or null (none, another version, cut short)
  if (!(bytes instanceof Uint8Array) || bytes.length < 2 || (bytes[0] !== 1 && bytes[0] !== 2)) return null;
  const secs = [];
  for (let k = 0, p = 2; k < bytes[1]; k++) {
    if (p + 2 > bytes.length || p + 2 + bytes[p + 1] > bytes.length) return null;   // (the FM-1 reads it as no marks)
    const tag = bytes[p], w = bytes[0] !== 1 && tag !== FAV_OPT && tag & (FAV_CHORD | FAV_BASS) ? (tag & FAV_CHORD ? FAV_CHORD : FAV_BASS) : 0;
    secs.push({ tag, base: tag & ~w, data: bytes.subarray(p + 2, p + 2 + bytes[p + 1]) });
    p += 2 + bytes[p + 1];
  }
  return { ver: bytes[0], secs };
}
const favFirst = (base) => (base === FAV_USER ? 0 : base === FAV_USER2 ? 32 : -1);   // the section's first slot - 1
// the user slots marked or typed (1..128) in the object
export function favUserSlots(bytes) {
  const f = favSections(bytes), out = new Set();
  if (f) for (const s of f.secs) if (favFirst(s.base) >= 0) for (let i = 0; i < s.data.length * 8; i++) if (s.data[i >> 3] >> (i & 7) & 1) out.add(favFirst(s.base) + i + 1);
  return [...out].sort((a, b) => a - b);
}
// the object with the marks and types of the user slots `slots` (1..128; null: every user slot) taken off, the other
// sections (the factory presets', P5's, the filter) byte for byte; packed as crf_pack packs (trailing zero bytes and an
// empty section left out). An object the FM-1 cannot read (another version, cut short) is left as it is -> the bytes
// (the same array when nothing changes)
export function favsClearSlots(bytes, slots = null) {
  const f = favSections(bytes);
  if (!f) return bytes;
  const kill = slots ? new Set(slots) : null, out = [];
  let changed = false;
  for (const s of f.secs) {
    const first = favFirst(s.base);
    if (first < 0) { out.push(s); continue; }
    const d = s.data.slice();
    for (let i = 0; i < d.length * 8; i++) if (!kill || kill.has(first + i + 1)) d[i >> 3] &= ~(1 << (i & 7));
    let n = d.length;
    while (n && !d[n - 1]) n--;
    if (n !== s.data.length || d.some((x, i) => x !== s.data[i])) changed = true;
    if (n) out.push({ tag: s.tag, data: d.subarray(0, n) });
  }
  if (!changed) return bytes;
  return Uint8Array.from([f.ver, out.length, ...out.flatMap((s) => [s.tag, s.data.length, ...s.data])]);
}
// the marks and types as the pane shows them (read only: the manager never sets a mark) -> {marks, chord, bass}: each a
// Map tag -> Set of ids; the user slots under FAV_USER with ids 0..127 (0x81's from 32). An object the FM-1 cannot read:
// no marks, as the FM-1 reads it
export function favMarks(bytes) {
  const f = favSections(bytes), out = { marks: new Map(), chord: new Map(), bass: new Map() };
  if (f) for (const s of f.secs) {
    if (s.tag === FAV_OPT) continue;
    const dst = s.tag === s.base ? out.marks : s.tag & FAV_CHORD ? out.chord : out.bass;
    const first = favFirst(s.base), tag = first >= 0 ? FAV_USER : s.base;
    if (!dst.has(tag)) dst.set(tag, new Set());
    for (let i = 0; i < s.data.length * 8; i++) if (s.data[i >> 3] >> (i & 7) & 1) dst.get(tag).add(Math.max(0, first) + i);
  }
  return out;
}
// a pool row's favourites key (cr_ui.c cu_fav_key): a factory preset and a user sound over one: the engine and the
// preset's id (its index in the engine's table; P5: the program's Sequential number, FACTORY_IDS), so the mark is the
// place's; an added user sound: its slot; INIT, an empty row: none (null)
export function favKeyOf(row) {
  if (!row || (row.type !== "factory" && row.type !== "user")) return null;
  if (row.type === "user" && !(row.f >= 0)) return row.slot ? { tag: FAV_USER, id: row.slot - 1 } : null;
  const ids = FACTORY_IDS[row.engine], id = ids ? ids[row.f] : row.f;
  return Number.isInteger(id) && (!ids || id > 0) ? { tag: row.engine, id } : null;
}
// a row's mark: null (none), "both", "chord" or "bass" (its type; typed both ways, as a newer object might: both)
export function favOf(favs, row) {
  const k = favKeyOf(row), on = (m) => !!(m && m.get(k.tag) && m.get(k.tag).has(k.id));
  if (!k || !favs || !on(favs.marks)) return null;
  const c = on(favs.chord), b = on(favs.bass);
  return c && !b ? "chord" : b && !c ? "bass" : "both";
}

// ---- deleting user sounds: each pool bank (or 0.21 object) written once, whatever the number of its slots
const POOL_PER_BANK = 10, POOL_SLOT_BYTES = 384, POOL_USED = 0xA5;
// a pool bank holding no used slot (byte 0 of a slot 0xA5: what crb_bank_fill keeps) is sent as a PUT of length 0: the
// FM-1 writes it empty (cr_backup.c crb_commit: a bank never written stays unwritten), so no 3840 bytes go over USB
const mgBankEmpty = (b) => !b || !b.length || Array.from({ length: POOL_PER_BANK }, (_, j) => b[j * POOL_SLOT_BYTES]).every((x) => x !== POOL_USED);
function mgEmptyBanks(before, m) {
  for (const id of POOL_IDS) if (!mgSame(before.get(id), m.get(id)) && mgBankEmpty(m.get(id))) m.set(id, mgEmpty());
  return m;
}
// the user rows deleted at once (Delete with several selected, Restore factory presets): each slot as deleteSound
// empties it; a row over a factory preset brings that preset back (a later slot whose binding names the same preset,
// an added sound so far, is unbound, so that the preset itself is back, not that sound); the deleted slots' own
// favourite marks and types go with them. -> {changed, objs, deleted: [slot], restored: [{engine, pos, name, slot}]}
export function managerDeleteRows(objs, rows) {
  const users = rows.filter((r) => r && r.type === "user" && r.slot);
  const del = new Set(users.map((r) => r.slot)), restored = users.filter((r) => r.f >= 0)
    .map((r) => ({ engine: r.engine, pos: r.pos, name: r.overwrites, slot: r.slot, f: r.f }));
  let m = new Map(objs);
  for (const s of del) m = mgMerge(m, deleteSound(m, s));
  const { slots } = parseSoundObjects(objs), raw = rawBindings(objs);
  raw.forEach((f, i) => {
    if (f >= 0 && !del.has(i + 1) && slots[i].used && restored.some((r) => r.engine === poolOf(slots[i].engine) && r.f === f))
      m = mgMerge(m, bindSound(m, i + 1, -1));
  });
  if (parseSoundObjects(objs).pool) mgEmptyBanks(objs, m);
  if (objs.has(FAVS_ID)) m.set(FAVS_ID, favsClearSlots(objs.get(FAVS_ID), [...del]));
  return { ...mgResult(new Map(objs), m), deleted: [...del].sort((a, b) => a - b), restored };
}
// the user rows over factory presets (the presets they replaced) among rows, or of every pool of `engines`
export const replacedRows = (rows) => rows.filter((r) => r && r.type === "user" && r.f >= 0);
export const replacedOf = (objs, engines = MANAGER_ENGINES) => {
  const pools = managerPools(objs, { empty: false });
  return engines.flatMap((e) => replacedRows(pools[e] || []));
};
// Restore factory presets (the selected, an engine's, every engine's): the user sounds over them deleted, so each factory
// preset is back in its place, as Delete / Reset to factory does on one row. Restore factory order keeps them instead
export const managerRestorePresets = (objs, rows) => managerDeleteRows(objs, replacedRows(rows));
// Delete all user sounds / Reset sounds to factory: every slot of every engine (P5 and the retired engines' records
// too) emptied, each bank that holds a used slot written once as a PUT of length 0 (the 0.21 objects: each once, as
// deleteSound empties a slot), the banks already empty not written; the marks and types of every user slot taken off
// (the factory presets' marks stay). With no user record every pool is INIT and the factory presets in their order.
// -> {changed, objs, deleted: the number of sounds, restored: the factory presets that come back, banks: objects written}
export function managerDeleteAll(objs) {
  const { slots, pool } = parseSoundObjects(objs), used = slots.filter((s) => s.used);
  const restored = replacedOf(objs).map((r) => ({ engine: r.engine, pos: r.pos, name: r.overwrites, slot: r.slot, f: r.f }));
  let m = new Map(objs);
  if (pool) { for (const id of POOL_IDS) if (!mgBankEmpty(objs.get(id))) m.set(id, mgEmpty()); }
  else for (const s of used) m = mgMerge(m, deleteSound(m, s.slot));
  if (objs.has(FAVS_ID)) m.set(FAVS_ID, favsClearSlots(objs.get(FAVS_ID), null));
  const res = mgResult(new Map(objs), m);
  return { ...res, deleted: used.length, restored, banks: res.changed.filter((c) => c.id !== FAVS_ID).length };
}

// ---- the confirms' words (the page shows them in its dialog: a title, paragraphs, the button)
const mgList = (items, max = 6) => (items.length <= max ? items.join(", ") : `${items.slice(0, max).join(", ")} and ${items.length - max} more`);
const mgPl = (n, one, many = `${one}s`) => `${n} ${n === 1 ? one : many}`;
const undoLine = (n) => `Undo puts ${n === 1 ? "it" : "them"} back in one step while this page stays open (it keeps the sounds in its memory; ` +
  `a reload forgets them). To keep ${n === 1 ? "it" : "them"} for good, back up first:`;
// Restore factory presets: rows = the user rows over the presets (replacedRows); selected: how many rows were selected
export function restorePresetsText(rows, { selected = rows.length } = {}) {
  const n = rows.length, one = n === 1;
  const presets = rows.map((r) => `${poolLabel(r.engine, r.pos)} ${r.overwrites}`), mine = rows.map((r) => `${r.label || slotLabel(r.slot)} ${r.name}`);
  const paras = [
    `${mgList(presets)} ${one ? "comes" : "come"} back in ${one ? "its place" : "their places"}.`,
    `The ${one ? "user sound" : mgPl(n, "user sound")} over ${one ? "it is" : "them are"} deleted from the FM-1: ${mgList(mine)}. ` +
      `To keep ${one ? "it" : "them"} as ${one ? "an added sound" : "added sounds"} after the factory presets instead, cancel and use Restore factory order.`,
  ];
  if (selected > n) paras.push(`${selected - n} of the ${selected} selected ${selected - n === 1 ? "is" : "are"} not over a factory preset: left as ${selected - n === 1 ? "it is" : "they are"}.`);
  paras.push("A favourite mark on a factory preset stays with it. Loops, settings and your other sounds are not touched.", undoLine(n));
  return { title: one ? `Restore ${presets[0]}?` : `Restore ${n} factory presets?`, paras, ok: one ? "Restore it" : `Restore ${n} presets` };
}
// Delete all user sounds (reset: false) / Reset sounds to factory (reset: true)
export function deleteAllText(objs, { reset = false } = {}) {
  const { slots, pool } = parseSoundObjects(objs), used = slots.filter((s) => s.used), n = used.length;
  const by = new Map(), order = MANAGER_ENGINES;   // (the picker's order, then a retired engine's records)
  for (const s of used) by.set(s.engine, (by.get(s.engine) || 0) + 1);
  const rank = (e) => (order.includes(e) ? order.indexOf(e) : order.length + e);
  const mix = [...by].sort((a, b) => rank(a[0]) - rank(b[0])).map(([e, k]) => `${ENGINE_NAMES[e]} ${k}`).join(", "), range = pool ? "U001 to U128" : "U01 to U32";
  const back = replacedOf(objs).length, marks = favUserSlots(objs.get(FAVS_ID)).length, w = managerDeleteAll(objs).changed.length;
  const writes = w ? ` The FM-1 writes ${mgPl(w, "object")} for it (each bank of ten slots once), its sound pausing about 45 ms at each.` : "";
  const favLine = `${marks ? `The favourite marks and types of your own sounds (${marks}) are taken off` : "No sound of yours is marked as a favourite"}; ` +
    "marks on factory presets stay (each is the preset's own). Loops, settings and the favourites filter are not touched.";
  const backLine = back ? `${mgPl(back, "factory preset")} that ${back === 1 ? "one" : "they"} replaced ${back === 1 ? "comes" : "come"} back in ${back === 1 ? "its place" : "their places"}.` : "";
  const paras = reset
    ? [`The FM-1's sounds go back to how they came: all ${mgPl(n, "user sound")} ${n === 1 ? "is" : "are"} deleted (${range}, every engine${mix ? `: ${mix}` : ""}), ` +
        `every factory preset of every engine is in its place again, in the factory order, with nothing added after them.${writes}`, favLine, undoLine(n)]
    : [`Every user sound on this FM-1 is deleted: ${mgPl(n, "sound")} in the slots ${range}, of every engine${mix ? ` (${mix})` : ""}.${writes}`,
       ...(backLine ? [`${backLine} The factory sounds themselves are never deleted.`] : []), favLine, undoLine(n)];
  return { title: reset ? "Reset the sounds to factory?" : `Delete all ${mgPl(n, "user sound")}?`, paras, ok: reset ? "Reset to factory" : `Delete ${mgPl(n, "sound")}` };
}
// swap two rows of the left pane: two user rows (their sounds exchange places), or a user row with a factory row of
// its pool (the user sound takes the factory position: its binding moves; the factory preset it overwrote, if any,
// comes back)
export function managerSwap(objs, a, b) {
  if (a.type === "user" && b.type === "user") return mgResult(new Map(objs), mgMerge(objs, swapSounds(objs, a.slot, b.slot)));
  const [u, fct] = a.type === "user" ? [a, b] : [b, a];
  if (u.type !== "user" || fct.type !== "factory") throw new Error("Swap takes two user sounds, or a user sound and a factory preset");
  if (u.engine !== fct.engine) throw new Error("A user sound swaps with a factory preset of its own engine only");
  return mgResult(new Map(objs), mgMerge(objs, bindSound(objs, u.slot, fct.f)));
}
export const managerRename = (objs, row, name) => {
  if (row.type !== "user") throw new Error("Only a user sound has a name to change (save a factory preset first)");
  return mgResult(new Map(objs), mgMerge(objs, renameSound(objs, row.slot, name)));
};
// a user sound deleted (an overwritten factory preset: the factory sound comes back)
export const managerDelete = (objs, row) => {
  if (row.type !== "user") throw new Error("Only a user sound can be deleted (a factory preset is built in)");
  return mgResult(new Map(objs), mgMerge(objs, deleteSound(objs, row.slot)));
};
// Keep: a user row as a choralroot-sound file
// Keep: a user row as a choralroot-sound file; a factory row as its pack entry (opts.packs: {engine: [file]} in the
// factory order), marked factory: true with its engine and position (a later Place puts it back there by default)
export const managerKeep = (objs, row, opts = {}) => {
  if (row.type === "factory") return factorySoundFile(opts.packs, row.engine, row.pos);
  if (row.type !== "user") throw new Error("Keep takes a user sound or a factory preset");
  const file = exportSound(objs, row.slot, opts);
  if (row.f >= 0) { file.factory = false; file.position = row.pos; }
  return file;
};
export function factorySoundFile(packs, engine, pos) {
  const list = packs && packs[engine], f = list && list[pos - 1];
  if (!f && engine === P5_ENGINE) throw new Error(P5_NO_PACK);
  if (!f) throw new Error(`No factory pack entry for ${poolLabel(engine, pos)}`);
  return { ...f, factory: true, engine, position: pos };
}
// P5's factory programs are Sequential's (LICENSING.md): the firmware carries the shipped ones, the site offers no pack
// of them, so a P5 factory row has no file to keep (tools/make_packs.py NO_PACK)
export const P5_NO_PACK = "P5's factory programs are Sequential's and the site has no file of them: to keep one, save it " +
  "on the FM-1 (SAVE > Save as new), then Keep that user sound";
// a row Keep / Save as a file can take: a user sound, or a factory preset whose pack entry is there (none for P5's)
export const keepable = (packs, row) => !!row && (row.type === "user" || (row.type === "factory" && !!(packs && packs[row.engine] && packs[row.engine][row.pos - 1])));
// the packs' files by engine: [{engine, sounds}] or pack objects -> {engine: [file]}
export function factoryFiles(packList) {
  const out = {};
  for (const p of packList) if (p && p.engine !== undefined) out[p.engine] = p.sounds;
  return out;
}
// used user slots
export const usedSlots = (objs) => parseSoundObjects(objs).slots.filter((x) => x.used).length;
// several files -> one choralroot-pack (the site's format)
export function makePack(files, { name = "My sounds", author = "", created = new Date().toISOString() } = {}) {
  if (!files.length) throw new Error("Nothing selected to save");
  const sounds = files.map((f) => { readSoundFile(f); return f; });
  return { format: PACK_FORMAT, version: 1, name, author, created, licence: null, sounds };
}
export const packFileName = (name, created = new Date()) =>
  `choralroot-pack-${String(name).replace(/[^A-Za-z0-9-]/g, "_")}-${created.toISOString().slice(0, 10).replace(/-/g, "")}.json`;
// every user record on the device as one pack (pool order: engine by engine)
export function userSoundsPack(objs, opts = {}) {
  const pools = managerPools(objs, { empty: false }), files = [];
  for (const e of MANAGER_ENGINES) for (const r of pools[e]) if (r.type === "user") files.push(managerKeep(objs, r, opts));
  return makePack(files, { name: "User sounds", ...opts });
}
// a library sound file renamed (the 12-character rule): a new file object
export function renameSoundFile(file, name) {
  if (!nameValid(name)) throw new Error("A name is 1 to 12 characters (ASCII letters, digits, symbols, spaces)");
  const s = readSoundFile(file), rec = s.record.slice();
  rec.fill(0, 4, 16); for (let i = 0; i < name.length; i++) rec[4 + i] = name.charCodeAt(i);
  return { ...file, name, record: mgB64(rec) };
}
// drag and drop within a pool: from row -> to row (both of one engine). Two user rows: their slots' sounds exchange
// (an added onto an added: the pool order follows the slot order); a user row onto a factory row: bound there (its
// old place freed); a factory row onto a user row: the same, as Swap does
export function managerDrop(objs, from, to) {
  if (from.engine !== to.engine) throw new Error("Drag within one engine's pool");
  if (from === to || (from.pos === to.pos)) return { changed: [], objs };
  return managerSwap(objs, from, to);
}

// ---- loops: a slot as a file, a file into a slot, delete, swap
export function keepLoop(objs, slot, { firmware = null, created } = {}) {
  const id = 39 + slot, bytes = (objs.get(id) || mgEmpty()), h = loopHeader(bytes);
  if (!h) throw new Error(`Loop slot ${slot} is empty`);
  if (!h.valid) throw new Error(`Loop slot ${slot} does not hold a readable loop`);
  return { format: LOOP_FORMAT, version: 1, firmware, created: created || new Date().toISOString(), slot, sig: h.sigName,
    len: h.len, ppqn: h.ppqn, bars: h.bars, nlayers: h.nlayers, nev: h.nev, data: mgB64(bytes) };
}
export const loopFileName = (slot, created = new Date()) => `${LOOP_FORMAT}-${slot}-${created.toISOString().slice(0, 10).replace(/-/g, "")}.json`;
export function readLoopFile(file) {
  if (typeof file === "string") { try { file = JSON.parse(file); } catch (_) { throw new Error("Not a loop file (not JSON)"); } }
  if (!file || file.format !== LOOP_FORMAT) throw new Error("Not a ChoralRoot loop file");
  if (file.version !== 1 || typeof file.data !== "string") throw new Error("Unsupported loop file");
  const bytes = mgUnB64(file.data), h = loopHeader(bytes);
  if (!h || !h.valid || bytes.length > 3664) throw new Error("The loop file's record is not a valid loop");
  return { bytes, header: h };
}
export function placeLoop(objs, slot, loop) {
  if (!Number.isInteger(slot) || slot < 1 || slot > 10) throw new Error("A loop slot is 1..10");
  const { bytes } = loop && loop.bytes instanceof Uint8Array ? loop : readLoopFile(loop);
  const m = new Map(objs); m.set(39 + slot, bytes.slice());
  return mgResult(new Map(objs), m);
}
export function deleteLoop(objs, slot) {
  const m = new Map(objs); m.set(39 + slot, mgEmpty());
  return mgResult(new Map(objs), m);
}
export function swapLoops(objs, a, b) {
  if (a === b) throw new Error("Swap needs two different loop slots");
  const m = new Map(objs), x = objs.get(39 + a) || mgEmpty(), y = objs.get(39 + b) || mgEmpty();
  m.set(39 + a, y); m.set(39 + b, x);
  return mgResult(new Map(objs), m);
}

// ---- the FM-1: read the manager's objects (what LIST names of them), write the changed ones
export async function readManagerObjects(request, onProgress = () => {}) {
  const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
  const listed = new Map(manifest.map((o) => [o.id, o]));
  const pool = POOL_IDS.some((id) => listed.has(id));   // (0.22: the sound pool; the legacy objects are not listed then)
  if (!pool && [6, 7, 9, 10, 11, 12, 13].some((id) => !listed.has(id))) throw new Error("This firmware does not hold ChoralRoot's user sounds (ChoralRoot needed)");
  const want = MANAGER_IDS.filter((id) => (pool ? !SOUND_IDS.includes(id) : !POOL_IDS.includes(id)));
  const ids = want.filter((id) => listed.has(id)), objs = new Map(want.map((id) => [id, mgEmpty()]));
  const total = ids.reduce((n, id) => n + listed.get(id).size, 0); let done = 0;
  for (const id of ids) {
    const o = listed.get(id), bytes = new Uint8Array(o.size);
    for (let off = 0; off < o.size; off += 256) {
      const size = Math.min(256, o.size - off);
      const a = await request([BACKUP_CMD.GET, [id, ...bkU32(off), size & 127, size >>> 7]], { timeout: 1000, retries: 1 });
      if (a[1]) throw new Error(`The FM-1 could not read the ${objectName(id)} (rc ${a[1]})`);
      if (a[0] !== id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== size) throw new Error("Unexpected reply from the FM-1");
      bytes.set(bkUnpack(a.slice(9), size), off);
      done += size; onProgress(done, total);
    }
    if ((o.size ? bkCrc(bytes) : 0) !== o.crc) throw new Error("The FM-1 changed during the read: try again (stop the loop if one is saving)");
    objs.set(id, bytes);
  }
  return { objs, listed: ids };
}
export const BUSY_TEXT = "Stop the loop on the FM-1 first";
export const writeErrorText = (e) => (e && e.rc === 3 ? `${BUSY_TEXT} (it is playing or recording)` : e && e.message || String(e));

// ---- the session: the objects as last read, undo, every write followed by a read-back
export class ManagerSession {
  constructor(request, opts = {}) {
    this.request = request; this.objs = null; this.history = []; this.busyTries = opts.busyTries ?? 0; this.onBusy = opts.onBusy || null;
  }
  async read(onProgress) { const r = await readManagerObjects(this.request, onProgress); this.objs = r.objs; this.listed = r.listed; return this.objs; }
  get canUndo() { return this.history.length > 0; }
  async writeChanged(changed, opts = {}) {
    const list = changed.filter((c) => !this.listed || this.listed.includes(c.id));
    await writeSounds(this.request, list, { busyTries: this.busyTries, onBusy: this.onBusy, onObject: opts.onWrite });
    return list.map((c) => c.id);
  }
  // an edit's result -> written, the set before kept for undo, read back; -> the ids written. opts.onWrite(k, n, id):
  // object k of n begins; opts.onRead(done, total): the read-back. A write that stops after some objects were written
  // (busy, refused) keeps the set before as an undo step all the same: Undo puts back what was written
  async apply(res, opts = {}) {
    if (!res.changed.length) return [];
    const before = new Map(this.objs);
    const keep = () => { this.history.push(before); if (this.history.length > UNDO_DEPTH) this.history.shift(); };
    let ids;
    try { ids = await this.writeChanged(res.changed, opts); }
    catch (e) {                                 // (a partial write: show the unit's state)
      if (e && e.written && e.written.length) keep();
      await this.read(opts.onRead).catch(() => {});
      throw e;
    }
    keep();
    await this.read(opts.onRead);
    return ids;
  }
  // the previous set's changed objects written back, read back; -> the ids written
  async undo(opts = {}) {
    const prev = this.history.pop();
    if (!prev) throw new Error("Nothing to undo");
    const changed = MG_WRITE_ORDER.filter((id) => !mgSame(prev.get(id), this.objs.get(id))).map((id) => ({ id, bytes: prev.get(id) || mgEmpty() }));
    try { await this.writeChanged(changed, opts); }
    catch (e) { this.history.push(prev); await this.read(opts.onRead).catch(() => {}); throw e; }
    await this.read(opts.onRead);
    return changed.map((c) => c.id);
  }
}

// ---- the library: items {key, type: "sound" | "loop", name, engine, source, file (the choralroot-sound / -loop object)}
// a sound read with readSoundFile -> a choralroot-sound object (for the library and for Save as a file)
export function soundToFile(sound, extra = {}) {
  return { format: SOUND_FORMAT, version: 1, firmware: null, created: new Date().toISOString(), slot: null, name: sound.name,
    engine: sound.engine, engineName: ENGINE_NAMES[sound.engine], binding: null, record: mgB64(sound.record),
    patch: sound.patch ? { kind: sound.kind, data: mgB64(sound.patch) } : null, ...extra };
}
const mgItemOfSound = (file, source) => {
  const s = readSoundFile(file);
  return { type: "sound", name: s.name, engine: s.engine, pool: poolOf(s.engine), kind: s.kind, patch: !!s.patch, source, file };
};
// a file's content (text for JSON, a Uint8Array for .syx) -> library items; a pack, a backup and a .syx bank give many
export function libraryItemsFromFile(name, content) {
  const source = name;
  if (/\.syx$/i.test(name) || content instanceof Uint8Array && content[0] === 0xF0) {
    const parsed = parseSyx(content instanceof Uint8Array ? content : new TextEncoder().encode(content));
    return Array.from({ length: syxCount(parsed) }, (_, i) => mgItemOfSound(soundToFile(soundFromSyx(parsed, i + 1)), source));
  }
  const text = content instanceof Uint8Array ? new TextDecoder().decode(content) : content;
  let j;
  try { j = JSON.parse(text); } catch (_) { throw new Error(`${name}: not a sound, loop, pack, backup or .syx file`); }
  if (j && j.format === SOUND_FORMAT) return [mgItemOfSound(j, source)];
  if (j && j.format === LOOP_FORMAT) { const l = readLoopFile(j); return [{ type: "loop", name: `loop ${j.slot || ""}`.trim(), header: l.header, source, file: j }]; }
  if (j && j.format === PACK_FORMAT) return readPack(j).map((f) => mgItemOfSound(f, j.name || source));
  if (j && j.format === "felucca-backup") {
    const b = readBackup(j), objs = new Map(b.objects.map((o) => [o.id, o.bytes])), items = [];
    for (const s of parseSoundObjects(objs).slots) if (s.used) items.push(mgItemOfSound(exportSound(objs, s.slot, { firmware: j.firmware }), `${source} ${s.label}`));
    for (const l of managerLoops(objs)) if (l.used && l.header.valid) items.push({ type: "loop", name: `loop ${l.slot}`, header: l.header, source, file: keepLoop(objs, l.slot, { firmware: j.firmware }) });
    return items;
  }
  throw new Error(`${name}: not a sound, loop, pack, backup or .syx file`);
}
// a Digitone .dnsnd's bytes -> one library item (an FM TONE sound) with `notes`: what was lost or approximated
// (docs/SOUNDS.md ".dnsnd import"); a bad file throws with the reason
export async function libraryItemsFromDnsnd(name, bytes) {
  let parsed;
  try { parsed = await parseDnsnd(bytes); } catch (e) { throw new Error(`${name}: ${e.message}`); }
  const s = soundFromDnsnd(parsed), notes = [...parsed.warnings, ...s.notes];
  const item = mgItemOfSound(soundToFile(s, { dnsnd: { name: parsed.name, tags: parsed.tags, notes } }), name);
  return [{ ...item, notes }];
}
export const isDnsndName = (name) => /\.dnsnd$/i.test(name);
export function readPack(pack) {
  if (typeof pack === "string") pack = JSON.parse(pack);
  if (!pack || pack.format !== PACK_FORMAT || pack.version !== 1 || !Array.isArray(pack.sounds)) throw new Error("Not a ChoralRoot pack");
  for (const s of pack.sounds) readSoundFile(s);
  return pack.sounds;
}
// the packs the site serves: index.json -> [{file, name, engine, count}]; fetchJson(url) -> the parsed JSON
export async function loadPackIndex(fetchJson, base = "packs/") {
  const idx = await fetchJson(base + "index.json");
  if (!idx || idx.format !== "choralroot-pack-index" || !Array.isArray(idx.packs)) throw new Error("Not a pack index");
  return idx.packs.map((p) => ({ ...p, url: base + p.file }));
}
export async function loadPack(fetchJson, entry) {
  const pack = await fetchJson(entry.url);
  return readPack(pack).map((f) => mgItemOfSound(f, pack.name));
}

// the library store "This browser": IndexedDB in a browser (db "choralroot-manager", store "items"), a Map elsewhere
export class LibraryStore {
  constructor(idb = (typeof indexedDB !== "undefined" ? indexedDB : null)) { this.idb = idb; this.mem = new Map(); this.next = 1; this.db = null; }
  async open() {
    if (!this.idb || this.db) return;
    this.db = await new Promise((resolve) => {        // (no IndexedDB, blocked, or no answer in 2 s: kept in memory)
      const timer = setTimeout(() => resolve(null), 2000);
      try {
        const r = this.idb.open("choralroot-manager", 1);
        r.onupgradeneeded = () => r.result.createObjectStore("items", { keyPath: "key", autoIncrement: true });
        r.onsuccess = () => { clearTimeout(timer); resolve(r.result); };
        r.onerror = r.onblocked = () => { clearTimeout(timer); resolve(null); };
      } catch (_) { clearTimeout(timer); resolve(null); }
    });
    if (!this.db) this.idb = null;
  }
  tx(mode, f) {
    return new Promise((resolve, reject) => {
      const t = this.db.transaction("items", mode), st = t.objectStore("items"), r = f(st);
      t.oncomplete = () => resolve(r && r.result); t.onerror = () => reject(t.error);
    });
  }
  async all() { await this.open(); return this.db ? this.tx("readonly", (s) => s.getAll()) : [...this.mem.values()]; }
  async add(item) {
    await this.open();
    const clean = { ...item }; delete clean.key;
    if (this.db) return this.tx("readwrite", (s) => s.add(clean));
    const key = this.next++; this.mem.set(key, { ...clean, key }); return key;
  }
  async put(item) {                                   // an item changed in place (renamed): its key kept
    await this.open();
    if (this.db) return this.tx("readwrite", (s) => s.put(item));
    this.mem.set(item.key, { ...item }); return item.key;
  }
  async remove(key) { await this.open(); if (this.db) return this.tx("readwrite", (s) => s.delete(key)); this.mem.delete(key); }
}

// ---- audition. ChoralRoot answers no editor command (PRESET 8, UP_LOAD 20: no reply, docs EDITOR_PROTOCOL.md
// "ChoralRoot: backup and restore"); what it takes over MIDI is a program change: on the chord channel the position in
// the chord part's CURRENT engine's pool (0 INIT), on the bass channel 0 OFF then position + 1 (cr_ui.c
// cu_midi_poll). So a row is auditioned by its pool position, which plays that row only when the unit is on its engine.
export const AUDITION_NOTE = "The FM-1 takes a program change as a position in its current engine's pool: set the FM-1 to this engine (OPT + PRESETS) to hear this row.";
export function auditionMessages(row, { bass = false } = {}) {
  if (!row || row.pos === undefined) return [];
  return bass ? [[0xC1, Math.min(127, row.pos + 1)]] : [[0xC0, Math.min(127, row.pos)]];
}
// SysEx 76 SELECT (ChoralRoot, firmware in progress 2026-10-10): part (0 chord, 1 bass), engine, pool position -> rc (0 ok,
// 1 bad engine, 2 bad position, 3 busy): the part switches to that engine and loads the position live. An older firmware
// does not answer: -> {unsupported: true} (the page falls back to the program change)
export const SELECT_CMD = 76;
export const SELECT_RC = { 1: "the FM-1 does not have that engine", 2: "no such position in the pool", 3: "busy: stop the loop on the FM-1" };
// a position past 127 (P5's 115 programs and added sounds, CZ-1's 64 and added ones) with its high bits in a fourth
// byte (cr_backup.c crb_select: pos | pos_hi << 7)
export const selectRequest = (row, { bass = false } = {}) =>
  [SELECT_CMD, [bass ? 1 : 0, row.engine & 127, row.pos & 127, ...(row.pos > 127 ? [row.pos >> 7] : [])]];
export async function selectOnUnit(request, row, opts = {}) {
  let a;
  try { a = await request(selectRequest(row, opts), { timeout: opts.timeout || 600, retries: 0 }); }
  catch (_) { return { ok: false, unsupported: true }; }
  const rc = a && a.length ? a[0] : 0;
  return rc ? { ok: false, rc, error: SELECT_RC[rc] || `rc ${rc}` } : { ok: true };
}
export const playingText = (row) => `${ENGINE_NAMES[row.engine]} · ${String(row.pos).padStart(2, "0")} ${row.name} on the unit`;
export const noteMessage = (on, note, { bass = false, velocity = 100 } = {}) => [(on ? 0x90 : 0x80) | (bass ? 1 : 0), note & 127, on ? velocity : 0];
export const midiText = (m) => m.map((b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");

// ---- the simulated FM-1 (?sim=1 and web/test_manager.mjs): web/test_installer.mjs's backup side, ChoralRoot 0.15,
// with a few sounds and two loops; busy (rc 3 at a PUT begin / commit) while sim.playing
export function makeLoopRecord({ sig = 0, bars = 2, layers = 2, events = 8 } = {}) {
  const len = bars * (sig === 0 ? 4 : 3) * 96, n = 16 + 2 * layers + 7 * events, b = new Uint8Array(n), v = new DataView(b.buffer);
  v.setUint32(0, 0x314C5243, true); b[4] = 1; b[5] = sig; b[6] = layers; v.setUint32(8, len, true); v.setUint16(12, events, true); v.setUint16(14, 96, true);
  for (let l = 0; l < layers; l++) v.setUint16(16 + 2 * l, Math.floor(events / layers) + (l < events % layers ? 1 : 0), true);
  for (let i = 0, o = 16 + 2 * layers; i < events; i++, o += 7) {
    v.setUint16(o, Math.floor(i * len / events), true); v.setUint16(o + 2, 48, true); b[o + 4] = 48 + (i * 5) % 24; b[o + 5] = 100; b[o + 6] = 0;
  }
  return b;
}
const mgSimSound = (tpl, engine, name, patch = null) => {
  const record = mgUnB64(SOUND_TEMPLATES[tpl]); record[2] = engine;
  return { record, name, engine, kind: patchKindOf(engine), patch };
};
// a P5 program as the slot keeps it (prophet_patch.c p5_patch_t, 138 bytes: the 133 raw bytes, size, model 0x32, command
// (2 a program dump, 3 the edit buffer), group, program): id, Sequential's number 1..200 (0: none, an edit buffer's)
export function simP5Patch(id, seed = 0) {
  const p = Uint8Array.from({ length: 138 }, (_, i) => (i * 5 + seed) & 63);
  p[133] = 133; p[134] = 0x32; p[135] = id ? 2 : 3; p[136] = id ? Math.floor((id - 1) / 40) : 0; p[137] = id ? (id - 1) % 40 : 0;
  return p;
}
export function simObjects(count = 6, pool = false) {   // (pool: the 0.22 sound pool's objects, else 0.21's)
  let m = new Map(MANAGER_IDS.filter((id) => (pool ? !SOUND_IDS.includes(id) : !POOL_IDS.includes(id))).map((id) => [id, mgEmpty()]));
  const czTone = Uint8Array.from({ length: 144 }, (_, i) => (i * 7) & 127);
  const put = (slot, s, f) => { m = mgMerge(m, placeSound(m, slot, s, f)); };
  put(1, mgSimSound("fm6", 12, "MY TINE"), 1);          // overwrites FM6 02 FM BELL
  put(2, mgSimSound("cz", 14, "CZ GLASS", czTone), -1);
  put(3, mgSimSound("fm6", 13, "WARM VA"), -1);
  if (pool) {                                           // (0.22: P5, its programs in the pool's slots)
    put(4, mgSimSound("fm6", P5_ENGINE, "MY PROPHET", simP5Patch(FACTORY_IDS[P5_ENGINE][3], 1)), 3);   // overwrites P5 03
    put(7, mgSimSound("fm6", P5_ENGINE, "P5 ADDED", simP5Patch(0, 2)), -1);
  }
  put(5, mgSimSound("fm6", 12, "BELL TOO"), -1);
  put(6, mgSimSound("fm6", 7, "WHEEL BOW"), 0);          // overwrites WHEEL 01 FULL ORGAN (PHASE 01 BRASS until 0.18)
  put(9, mgSimSound("cz", 14, "CZ BASS 2", czTone), 13); // overwrites CZ-1 13 SYNTH.BASS
  const fillEngines = [12, 13, 15, 7];
  for (let k = 0; parseSoundObjects(m).slots.filter((x) => x.used).length < count; k++) {
    const e = fillEngines[k % fillEngines.length];
    put(firstFreeSlot(m), mgSimSound(e === 14 ? "cz" : "fm6", e, `USER ${String(k + 1).padStart(2, "0")}`), -1);
  }
  m.set(40, makeLoopRecord({ bars: 4, layers: 3, events: 24 }));
  m.set(43, makeLoopRecord({ sig: 1, bars: 2, layers: 1, events: 6 }));
  if (pool) {                                           // (0.22: a few favourites, cr_favs.c's version 2: U003 WARM VA, FM6 02 (the
    const bits = (ids) => {                             // place U001 holds), P5 01..05 by their Sequential numbers, P5 03 typed Chord)
      const b = new Uint8Array(1 + (Math.max(...ids) >> 3));
      for (const i of ids) b[i >> 3] |= 1 << (i & 7);
      return [...b];
    };
    const p5 = FACTORY_IDS[P5_ENGINE], secs = [[FAV_USER, bits([2])], [12, bits([1])], [P5_ENGINE, bits(p5.slice(1, 6))], [P5_ENGINE | FAV_CHORD, bits([p5[3]])]];
    m.set(FAVS_ID, Uint8Array.from([2, secs.length, ...secs.flatMap(([t, d]) => [t, d.length, ...d])]));
  }
  return m;
}
// ---- several FM-1s (docs/DEVICE-MANAGER.md "Several FM-1s", docs/INSTALL-COMPAT.md): the manager opens one, by its unit ID
// the port pairs whose name holds `port`: each input with the output of its name, the k-th input of a name with the
// k-th output of it (two FM-1s may have ports of one name) -> [{input, output}]
// the port names an FM-1 running ChoralRoot can have (GitHub issue 32): the firmware's own "ChoralRoot FM-1", and the
// name macOS keeps for a USB device it saw first with another firmware ("FM-1": CoreMIDI remembers a device's name by its
// location, so a unit updated from the stock firmware, or swapped for another FM-1 on the same port, keeps it); the
// page then asks the INFO it needs and refuses anything that does not answer as ChoralRoot (deviceInfo)
export const MANAGER_PORT_RE = /choralroot|fm-1|felucca|sloop|melodee/i;
export function managerPorts(access, port) {
  const hit = (n) => (port instanceof RegExp ? port.test(n) : n.includes(port));
  const live = (m) => [...m.values()].filter((p) => p.name && hit(p.name) && p.state !== "disconnected");
  const ins = live(access.inputs), outs = live(access.outputs);
  return ins.map((input) => {
    const k = ins.filter((i) => i.name === input.name).indexOf(input), output = outs.filter((o) => o.name === input.name)[k];
    return output ? { input, output } : null;
  }).filter(Boolean);
}
// which FM-1 to open: units [{unitId, version, ...}] as INFO answered. One, and none opened before: it, as always. The
// one opened before (remembered, its ID), when it is there again: it, without a question. Else asked (ask(text) -> the
// answer: a number of the list or a unit ID; null: none): the page never takes the first of several, and never moves
// to another FM-1 by itself when the one it had leaves -> the unit, or null
export function pickUnit(units, { remembered = null, ask = () => null } = {}) {
  if (!units.length) return null;
  const known = remembered && units.find((u) => u.unitId === remembered);
  if (known) return known;
  if (units.length === 1 && !remembered) return units[0];
  const lines = units.map((u, k) => `${k + 1}   ${u.unitId ? "unit ID " + u.unitId : "no unit ID"}   ${u.version || "no answer (not ChoralRoot?)"}`);
  const head = remembered && !units.some((u) => u.unitId === remembered)
    ? `The FM-1 with unit ID ${remembered} is not connected. ` : "";
  const a = ask(`${head}${units.length === 1 ? "One FM-1 is" : `${units.length} FM-1s are`} connected. Which one should the device ` +
    `manager open? Type its number or its unit ID (Options > Version on the FM-1):\n${lines.join("\n")}`);
  if (a == null) return null;
  const s = String(a).trim().toUpperCase();
  return units.find((u) => u.unitId && u.unitId === s) || (/^\d+$/.test(s) ? units[Number(s) - 1] : null) || null;
}

// the objects a 0.22 FM-1 (the sound pool) lists: the legacy sound objects replaced by the pool's banks
export const SIM_POOL_IDS = [...CR_BACKUP_IDS.filter((id) => !SOUND_IDS.includes(id)), ...POOL_IDS];
// a pool bank's PUT as cr_backup.c takes it (3y78): each used slot (byte 0 0xA5) a valid record whose patch header is
// none or an engine's with that engine's length (FM TONE also 0.21's 80); unused slots are not read
function simBankOk(v) {
  for (let j = 0; j < POOL_PER_BANK; j++) {
    const s = v.subarray(j * POOL_SLOT_BYTES, (j + 1) * POOL_SLOT_BYTES);
    if (s[0] !== POOL_USED) continue;
    if (!recordValid(s.subarray(0, 192))) return false;
    if (!s[192]) continue;
    const e = s[192] - 1, n = s[193], len = PATCH_SIZE[patchKindOf(e)] || 0;
    if (e >= ENGINE_NAMES.length || !n || (n !== len && !(e === 15 && n === 80))) return false;
  }
  return true;
}
// .. and stores it (crb_bank_fill): the used slots as sent, every other slot 384 zeros; a PUT of length 0 writes the
// bank empty (it stays a written bank: 3840 zeros), or nothing when the bank was never written
const simBankFill = (v) => {
  const out = new Uint8Array(POOL_PER_BANK * POOL_SLOT_BYTES);
  for (let j = 0; j < POOL_PER_BANK && v.length; j++) if (v[j * POOL_SLOT_BYTES] === POOL_USED) out.set(v.subarray(j * POOL_SLOT_BYTES, (j + 1) * POOL_SLOT_BYTES), j * POOL_SLOT_BYTES);
  return out;
};
export function makeSimFM1(objs = simObjects(), { version = "ChoralRoot 0.15", ids = CR_BACKUP_IDS, unitId = null } = {}) {
  // log: the ids each commit wrote; puts: every PUT begin taken ({id, size}), written or not
  const sim = { objs: new Map(objs), log: [], puts: [], staged: null, playing: false, sent: [], requests: 0 };
  sim.objs.set(1, sim.objs.get(1) || new Uint8Array(764));
  sim.handle = (cmd, a) => {
    if (cmd === 1) return [...new TextEncoder().encode(version), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3, 0x43, 1, 2,
      ...(unitId ? [0x49, 8, ...new TextEncoder().encode(unitId)] : [])];   // (3y66: the unit's ID)
    if (cmd === 65) {
      const out = [1, 0, ids.length];
      for (const id of ids) { const v = sim.objs.get(id) || mgEmpty(); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === 66) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = sim.objs.get(id) || mgEmpty();
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === 67) {
      const [op, id] = a;
      if ((op === 0 || op === 2) && sim.playing) return [op, id, 3];
      if (op === 0) {
        const size = bkR32(a, 2), pool = POOL_IDS.includes(id);
        sim.staged = { id, size, crc: bkR32(a, 7), bytes: [] };
        const rc = ids.includes(id) && id !== 8 && (!pool || !size || size === POOL_PER_BANK * POOL_SLOT_BYTES) ? 0 : 1;
        if (!rc) sim.puts.push({ id, size });
        return [op, id, rc];
      }
      if (!sim.staged || sim.staged.id !== id) return [op, id, 5];
      if (op === 1) { sim.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, sim.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) {
        const v = Uint8Array.from(sim.staged.bytes), ok = v.length === sim.staged.size && (v.length ? bkCrc(v) : 0) === sim.staged.crc &&
          (!CR_LOOP_IDS.includes(id) || !v.length || loopHeader(v).valid);
        sim.staged = null;
        if (!ok) return [op, id, 2];
        if (POOL_IDS.includes(id)) {                    // (cr_backup.c crb_commit, the pool's branch)
          if (v.length && !simBankOk(v)) return [op, id, 2];
          if (!v.length && !(sim.objs.get(id) || mgEmpty()).length) return [op, id, 0];   // (an empty bank onto none: nothing to write)
          sim.objs.set(id, simBankFill(v)); sim.log.push(id); return [op, id, 0];
        }
        sim.objs.set(id, v); sim.log.push(id); return [op, id, 0];
      }
      sim.staged = null; return [op, id, 0];
    }
    if (cmd === 72) return [0];
    if (cmd === SELECT_CMD && sim.select !== false) {   // (crb_select: part, engine, pos [pos_hi])
      const [part, e] = a, pos = a[2] | (a[3] || 0) << 7, pools = managerPools(sim.objs, { empty: false });
      if ((a.length !== 3 && a.length !== 4) || part > 1) return null;
      if (!pools[e] || !managerEngines(sim.objs).includes(e)) return [1];
      if (pos >= pools[e].length) return [2];
      if (sim.playing) return [3];
      sim.selected = { part, engine: e, pos, name: pools[e][pos].name }; return [0];
    }
    return null;
  };
  // the request function of BackupConnection / captureBackup: [cmd, args] -> the reply's bytes
  sim.request = async ([cmd, args]) => {
    sim.requests++;
    const r = sim.handle(cmd, args);
    if (!r) throw new Error("Backup timed out. Keep the FM-1 connected and stopped.");
    return r;
  };
  sim.send = (bytes) => { sim.sent.push(Array.from(bytes)); };
  return sim;
}
