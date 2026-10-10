// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// The device manager's model (docs/DEVICE-MANAGER.md, web/manager.html): the FM-1's objects (the user sounds' bank and
// stores, the ten loop slots) read and written over the backup protocol; the pools per engine as PRESETS turns through
// them (cr_bank.c pool_entry); Place / Keep / Swap / Rename / Delete as new object bytes; undo (the object set before
// every write); the library (IndexedDB in a browser, a Map in node); files and packs read into library items; the
// simulated FM-1 of ?sim=1 (web/test_installer.mjs's backup side, with sounds and loops in it).
// No DOM: the page (manager.html) draws, this decides. Every name here is unique across fm1backup.js / fm1sounds.js:
// make_site.py inlines the three into one script.
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, objectName, readBackup, CR_LOOP_IDS, CR_BACKUP_IDS, loopHeader } from "./fm1backup.js";
import { SOUND_IDS, ENGINE_NAMES, FACTORY_PRESETS, FACTORY_FIRST, SOUND_TEMPLATES, SOUND_FORMAT, poolOf, patchKindOf, parseSoundObjects, exportSound, readSoundFile, renameSound, deleteSound, writeSounds, placeSound, bindSound, swapSounds, firstFreeSlot, parseSyx, syxCount, soundFromSyx } from "./fm1sounds.js";

// the engine picker's order (docs/PRESETS.md; the EDIT picker since 0.14)
export const MANAGER_ENGINES = [12, 15, 13, 14, 2, 3, 5, 6, 7, 9, 11];
export const MANAGER_IDS = [...SOUND_IDS, ...CR_LOOP_IDS];
export const UNDO_DEPTH = 20;
export const LOOP_FORMAT = "choralroot-loop";
export const PACK_FORMAT = "choralroot-pack";
const MG_WRITE_ORDER = [9, 10, 11, 12, 13, 22, 6, 7, ...CR_LOOP_IDS];   // the stores before the banks, loops last
const mgEmpty = () => new Uint8Array(0);
const mgSame = (a, b) => (a || mgEmpty()).length === (b || mgEmpty()).length && (a || mgEmpty()).every((x, i) => x === b[i]);
const mgB64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
const mgUnB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
export const poolLabel = (engine, pos) => `${ENGINE_NAMES[engine]} ${String(pos).padStart(2, "0")}`;

// ---- the pools: objs (Map id -> bytes) -> {engine: [row]} in the firmware's order: 00 INIT, the factory presets
// (each replaced by the first user slot bound to it: overwritten), then the added user sounds in slot order.
// row: {pos, name, type: "init" | "factory" | "user", engine, f (factory index or -1), slot (0: none), label,
//       overwrites (the factory name a user row replaces, or null), kind (patch kind), patch (stored or not)}
export function managerPools(objs) {
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
// -> {changed, objs, slot, where: "new" | "over-factory" | "over-user"}
export function managerPlace(objs, sound, target = null) {
  if (!sound || !(sound.record instanceof Uint8Array)) sound = readSoundFile(sound);
  const pool = poolOf(sound.engine);
  let slot, f = -1, where = "new";
  if (target && target.engine === pool && target.type === "user") { slot = target.slot; f = target.f; where = "over-user"; }
  else {
    slot = firstFreeSlot(objs);
    if (!slot) throw new Error("All 32 user slots are used: delete or overwrite one first");
    if (target && target.engine === pool && target.type === "factory") { f = target.f; where = "over-factory"; }
  }
  const res = placeSound(objs, slot, sound, f), m = mgMerge(objs, res);
  return { ...mgResult(new Map(objs), m), slot, where };
}
// several sounds (a pack): each as a new added sound; stops at the last free slot (-> placed, skipped counts)
export function managerPlaceAll(objs, sounds) {
  let m = new Map(objs), placed = 0;
  for (const s of sounds) {
    const slot = firstFreeSlot(m);
    if (!slot) break;
    m = mgMerge(m, placeSound(m, slot, s, -1)); placed++;
  }
  return { ...mgResult(new Map(objs), m), placed, skipped: sounds.length - placed };
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
export const managerKeep = (objs, row, opts = {}) => {
  if (row.type !== "user") throw new Error("Keep takes a user sound (the factory sounds are in the packs)");
  return exportSound(objs, row.slot, opts);
};

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
  if ([6, 7, 9, 10, 11, 12, 13].some((id) => !listed.has(id))) throw new Error("This firmware does not hold ChoralRoot's user sounds (ChoralRoot needed)");
  const ids = MANAGER_IDS.filter((id) => listed.has(id)), objs = new Map(MANAGER_IDS.map((id) => [id, mgEmpty()]));
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
  async writeChanged(changed) {
    const list = changed.filter((c) => !this.listed || this.listed.includes(c.id));
    await writeSounds(this.request, list, { busyTries: this.busyTries, onBusy: this.onBusy });
    return list.map((c) => c.id);
  }
  // an edit's result -> written, the set before kept for undo, read back; -> the ids written
  async apply(res) {
    if (!res.changed.length) return [];
    const before = new Map(this.objs);
    let ids;
    try { ids = await this.writeChanged(res.changed); }
    catch (e) { await this.read().catch(() => {}); throw e; }   // (a partial write: show the unit's state)
    this.history.push(before);
    if (this.history.length > UNDO_DEPTH) this.history.shift();
    await this.read();
    return ids;
  }
  // the previous set's changed objects written back, read back; -> the ids written
  async undo() {
    const prev = this.history.pop();
    if (!prev) throw new Error("Nothing to undo");
    const changed = MG_WRITE_ORDER.filter((id) => !mgSame(prev.get(id), this.objs.get(id))).map((id) => ({ id, bytes: prev.get(id) || mgEmpty() }));
    try { await this.writeChanged(changed); }
    catch (e) { this.history.push(prev); await this.read().catch(() => {}); throw e; }
    await this.read();
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
    this.db = await new Promise((resolve, reject) => {
      const r = this.idb.open("choralroot-manager", 1);
      r.onupgradeneeded = () => r.result.createObjectStore("items", { keyPath: "key", autoIncrement: true });
      r.onsuccess = () => resolve(r.result); r.onerror = () => reject(r.error);
    }).catch(() => null);
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
export function simObjects() {
  let m = new Map(MANAGER_IDS.map((id) => [id, mgEmpty()]));
  const czTone = Uint8Array.from({ length: 144 }, (_, i) => (i * 7) & 127);
  const put = (slot, s, f) => { m = mgMerge(m, placeSound(m, slot, s, f)); };
  put(1, mgSimSound("fm6", 12, "MY TINE"), 1);          // overwrites FM6 02 FM BELL
  put(2, mgSimSound("cz", 14, "CZ GLASS", czTone), -1);
  put(3, mgSimSound("fm6", 13, "WARM VA"), -1);
  put(5, mgSimSound("fm6", 12, "BELL TOO"), -1);
  put(6, mgSimSound("fm6", 2, "PHASE BOW"), 0);          // overwrites PHASE 01 BRASS
  put(9, mgSimSound("cz", 14, "CZ BASS 2", czTone), 13); // overwrites CZ-1 13 SYNTH.BASS
  m.set(40, makeLoopRecord({ bars: 4, layers: 3, events: 24 }));
  m.set(43, makeLoopRecord({ sig: 1, bars: 2, layers: 1, events: 6 }));
  return m;
}
export function makeSimFM1(objs = simObjects(), { version = "ChoralRoot 0.15", ids = CR_BACKUP_IDS } = {}) {
  const sim = { objs: new Map(objs), log: [], staged: null, playing: false, sent: [], requests: 0 };
  sim.objs.set(1, sim.objs.get(1) || new Uint8Array(764));
  sim.handle = (cmd, a) => {
    if (cmd === 1) return [...new TextEncoder().encode(version), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3, 0x43, 1, 2];
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
      if (op === 0) { sim.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, ids.includes(id) && id !== 8 ? 0 : 1]; }
      if (!sim.staged || sim.staged.id !== id) return [op, id, 5];
      if (op === 1) { sim.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, sim.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) {
        const v = Uint8Array.from(sim.staged.bytes), ok = v.length === sim.staged.size && (v.length ? bkCrc(v) : 0) === sim.staged.crc &&
          (id < 40 || !v.length || loopHeader(v).valid);
        sim.staged = null;
        if (!ok) return [op, id, 2];
        sim.objs.set(id, v); sim.log.push(id); return [op, id, 0];
      }
      sim.staged = null; return [op, id, 0];
    }
    if (cmd === 72) return [0];
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
