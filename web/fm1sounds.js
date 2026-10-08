// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// ChoralRoot's user sounds as files (docs/SOUNDS.md): the 32 slots U01..U32 read from the backup objects that hold
// them (6 7 the banks, 9 the VA patch store, 10 11 the FM6 patch store, 12 13 the CZ-1 tone store, 22 the QUAD patch
// store: firmware 0.14 on, read when the FM-1 lists it), one slot exported
// to a "choralroot-sound" file, a file imported into a slot, a slot renamed or deleted, as new object bytes; only the
// objects that changed are written back (the stores first, the bank last), each as a restore writes it (fm1backup.js).
// Pure: no DOM, no MIDI (the requests go through the caller's request function, as captureBackup's).
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, objectName } from "./fm1backup.js";

export const SOUND_IDS = [6, 7, 9, 10, 11, 12, 13, 22];
const SOUND_IDS_OPTIONAL = [22];                 // (an older ChoralRoot has no QUAD store: read as empty)
export const SOUND_SLOTS = 32;
export const ENGINE_NAMES = ["ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS",
  "DRUM", "NOISE", "FM6", "VA", "CZ-1", "QUAD"];
export const PATCH_SIZE = { va: 110, fm6: 128, cz: 144, quad: 80 };
export const SOUND_FORMAT = "choralroot-sound";
export const patchKindOf = (engine) =>
  (engine === 13 ? "va" : engine === 12 ? "fm6" : engine === 14 ? "cz" : engine === 15 ? "quad" : null);

// the factory presets of each engine (ENGINES[e]->presets[k].name) and the first of them in the engine's pool (the
// CZ-1's preset 0, INIT TONE, is the pool's INIT): tests/sound_templates.c prints both from the firmware, and its
// --check fails until the two lines below hold its strings verbatim (docs/SOUNDS.md "The binding")
export const FACTORY_PRESETS = {"0":["SAW LEAD","SOFT PAD","SQR BASS","PWM STR","ACID","SINE KEY","RAVE","SUB BASS","PLUCK","BRASS","WIND","STRINGS"],"2":["BRASS","ORGAN","STRING","RESO","BELL","WIRE"],"3":["PULSE LD","WAVE BASS","ARP 8BIT","WAVE LEAD","STEP LEAD"],"5":["CHOIR AAH","VOX LEAD","WOW BASS","WHISPER"],"6":["FAT BASS","ARP LEAD","SYNC LEAD","RING BELL","CHIP CHOIR"],"7":["FULL ORGAN","JAZZ PERC","GOSPEL","SOFT FLUTE","ROCK DRIVE"],"9":["BELL TREE","MARIMBA","PLUCK","BOWED METAL","KALIMBA","HAND DRUM","TOMS","DRONE STRING","HARP"],"11":["WIND","RAIN","ARCADE","METAL"],"12":["TINE EP","FM BELL","FM BASS","BRASS","FM PAD","MARIMBA","FM ORGAN","FM PLUCK","DX TINE","BRASS SECT","SOLID BASS","BELLS","DX MARIMBA","CLAVINET","DRAWBARS","STRINGS","GLASS PAD","SYNC LEAD","HARP","KALIMBA","FLUTE","STEEL DRUM","SAW BASS","TUBULAR","PIANO"],"13":["LUSH PAD","WARM PAD","GLASS PAD","SLOW STRINGS","ENSEMBLE STR","SYNTH BRASS","SOFT BRASS","POLY KEYS","PWM KEYS","CLAV","SOFT LEAD","HOLLOW","BELLS","SWEEP PAD","SOFT AAH","ORGANISH","DEEP SUB","PUNCH BASS","RUBBER BASS","SYNC BASS","MORPH PAD","VINYL KEYS","WIDE STRINGS","CLOUD PAD","SHIMMER"],"14":["INIT TONE","BRASS 1","BRASS 2","BRASS 3","STRINGS 1","STRINGS 2","STRINGS 3","STRINGS 4","ORCHESTRA","ACO.GUITAR","JAZZ GUITAR","ELEC.GUITAR","SLAP BASS","SYNTH.BASS","ELEC.BASS 1","ELEC.BASS 2","HARP","BRASS 4","SAXOPHONE","CELLO","FLUTE","WHISTLE","HARMONICA","RECORDER","KOTO","PIANO 1","PIANO 2","PIANO 3","ELEC.PIANO","HONKY-TONK","FUNKY CLAV 1","FUNKY CLAV 2","HARPSICHORD","JAZZ ORGAN 1","JAZZ ORGAN 2","PIPE ORGAN 1","PIPE ORGAN 2","ACCORDION","VOICE 1","VOICE 2","VOICE 3","MUSIC BOX","VIBRAPHONE","XYLOPHONE","MARIMBA","MALLET LOG","AFRO PERC","BELLS","METALLIC","SYN STRINGS","FAT ENSEMBLE","SITAR","SYNTH.LEAD 1","SYNTH.LEAD 2","SYNTH.LEAD 3","SYNTH.LEAD 4","SWEEP 1","SYN DRUMS 1","SYN DRUMS 2","CONGA","STEEL DRUM","SWEEP 2","JET ROAR","MOTORCYCLE","TYPHOON"],"15":["EP","BELL","BASS","PLUCK","BRASS","GLASS PAD","HOLLOW","SQUARE LEAD","METAL","WOBBLE","CLAV","STRINGS","MARIMBA","DRONE","FEEDBACK","NOISE-ISH"]};
export const FACTORY_FIRST = {"0":0,"2":0,"3":0,"5":0,"6":0,"7":0,"9":0,"11":0,"12":0,"13":0,"14":1,"15":0};
const SND_BIND_MARK = 0xA6, SND_BIND_NOTE = 160 + 15, SND_BIND_FLAGS = 176 + 15;
// the pool a record is in (cr_bank.c cb_rec_engine): its engine if selectable, DIGITAL's FM6, another retired one ANALOG
export const poolOf = (engine) => (FACTORY_PRESETS[engine] ? engine : engine === 1 ? 12 : 0);
// the factory preset index a record's binding names (cr_bank.c cb_bind_raw), -1: none (added)
function sndBindRaw(r) {
  if (!r || !recordValid(r) || r[1] === 3 || r[1] === 5 || r[SND_BIND_NOTE] !== SND_BIND_MARK || !r[SND_BIND_FLAGS]) return -1;
  const e = poolOf(r[2]), f = r[SND_BIND_FLAGS] - 1;
  return f >= (FACTORY_FIRST[e] || 0) && f < (FACTORY_PRESETS[e] || []).length ? f : -1;
}
// the 32 records (null: empty) -> per slot null, or {index, pos, label, name} of the factory preset it overwrites
// (cr_bank.c cb_bound: the first slot bound to a factory preset wins, a later one is an added preset)
export function soundBindings(records) {
  const seen = new Set();
  return records.map((r) => {
    const f = sndBindRaw(r);
    if (f < 0) return null;
    const e = poolOf(r[2]), key = `${e}:${f}`;
    if (seen.has(key)) return null;
    seen.add(key);
    const pos = f - (FACTORY_FIRST[e] || 0) + 1;
    return { index: f, pos, label: `${ENGINE_NAMES[e]} ${String(pos).padStart(2, "0")}`, name: FACTORY_PRESETS[e][f] };
  });
}
// a slot's binding as a sound file carries it: {overwrites: the pool position, name} or null (added)
export const bindingOf = (bound) => (bound ? { overwrites: bound.pos, name: bound.name } : null);

// the layouts (docs/SOUNDS.md "The objects that hold the user sounds")
const SND_REC = 192, SND_PER_BANK = 16, SND_BANK_SIZE = 8 + SND_PER_BANK * SND_REC, SND_BANK_MAGIC = 0x31425055;
const SND_USED = 0xA5, SND_NAME = 12;
const SND_STORE = {   // per kind: the object ids (VA, QUAD: one store of 32, FM6 / CZ-1: two halves of 16), header, size
  va: { ids: [9], magic: 0x31534156, ver: 3, nslot: 32, size: 16 + 32 * 110 },
  fm6: { ids: [10, 11], magic: 0x55364D46, ver: 1, nslot: 16, size: 16 + 16 * 128 },
  cz: { ids: [12, 13], magic: 0x55315A43, ver: 1, nslot: 16, size: 16 + 16 * 144 },
  quad: { ids: [22], magic: 0x53445551, ver: 1, nslot: 32, size: 16 + 32 * 80 },
};
const SND_KINDS = ["va", "fm6", "cz", "quad"];
const sndWhole = (kind) => kind === "va" || kind === "quad";   // one store of 32: the used mask at 8, the blob size at 12
const sndView = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const sndSlotOk = (slot) => Number.isInteger(slot) && slot >= 1 && slot <= SOUND_SLOTS;
const sndLabel = (slot) => `U${String(slot).padStart(2, "0")}`;
export const slotLabel = sndLabel;
function sndCheckSlot(slot) {
  if (!sndSlotOk(slot)) throw new Error(`Slot ${slot} is not a user slot (1..32)`);
}
// objs: a Map or an object id -> Uint8Array (a missing id = size 0) -> a Map of the eight ids
function sndMap(objs) {
  const get = (id) => (objs instanceof Map ? objs.get(id) : objs && objs[id]);
  return new Map(SOUND_IDS.map((id) => [id, get(id) instanceof Uint8Array ? get(id) : new Uint8Array(0)]));
}

// a record (192 bytes) is valid as up_valid tests it
export function recordValid(r) {
  if (!r || r.length !== SND_REC) return false;
  const ver = r[1], np = r[3];
  if (r[0] !== SND_USED || ver < 1 || ver > 5 || r[2] >= ENGINE_NAMES.length || np < 8 || np > (ver >= 4 ? 144 : 72) || !r[4]) return false;
  if (ver >= 4) for (let i = 0; i < np; i++) if (r[16 + i] > 191) return false;
  return true;
}
const sndBankOk = (b) => b.length === SND_BANK_SIZE && sndView(b).getUint32(0, true) === SND_BANK_MAGIC &&
  sndView(b).getUint16(4, true) === SND_REC && sndView(b).getUint16(6, true) === SND_PER_BANK;
const sndBankId = (slot) => (slot <= 16 ? 6 : 7);
const sndRecOff = (slot) => 8 + ((slot - 1) % SND_PER_BANK) * SND_REC;
function sndRecord(m, slot) {           // the slot's record bytes, or null (the bank is empty / not a bank)
  const b = m.get(sndBankId(slot));
  return sndBankOk(b) ? b.subarray(sndRecOff(slot), sndRecOff(slot) + SND_REC) : null;
}
// the store object of a kind that holds `slot`, its blob offset and its used bit
function sndPlace(kind, slot) {
  const s = SND_STORE[kind], h = sndWhole(kind) ? 0 : slot > 16 ? 1 : 0, k = sndWhole(kind) ? slot - 1 : (slot - 1) % 16;
  return { id: s.ids[h], half: h, bit: k, off: 16 + k * PATCH_SIZE[kind] };
}
function sndStoreOk(kind, b, half) {
  const s = SND_STORE[kind];
  if (b.length !== s.size) return false;
  const v = sndView(b), ver = v.getUint16(4, true);
  if (v.getUint32(0, true) !== s.magic || v.getUint16(6, true) !== s.nslot) return false;
  if (kind === "va") return (ver === 3 || ver === 2) && v.getUint16(12, true) === PATCH_SIZE.va;
  if (kind === "quad") return ver === 1 && v.getUint16(12, true) === PATCH_SIZE.quad;
  return ver === 1 && v.getUint16(8, true) === half * 16 && v.getUint16(10, true) === PATCH_SIZE[kind] && v.getUint32(12, true) >>> 16 === 0;
}
const sndUsedOff = (kind) => (sndWhole(kind) ? 8 : 12);
function sndBlob(m, kind, slot) {        // the slot's blob in the store of that kind, or null (no store, bit clear)
  const p = sndPlace(kind, slot), b = m.get(p.id);
  if (!sndStoreOk(kind, b, p.half) || !(sndView(b).getUint32(sndUsedOff(kind), true) >>> p.bit & 1)) return null;
  return b.subarray(p.off, p.off + PATCH_SIZE[kind]);
}
const sndDecodeName = (r) => { let s = ""; for (let i = 4; i < 4 + SND_NAME && r[i]; i++) s += String.fromCharCode(r[i]); return s; };
export const nameValid = (name) => typeof name === "string" && /^[\x20-\x7e]{1,12}$/.test(name);

// the eight objects -> the slot table (docs/SOUNDS.md "The slot table")
// bound: null, or the factory preset the slot overwrites ({index, pos, label, name}: "over FM6 02 FM BELL");
// added: a used slot that is not bound (it follows the factory presets in its engine's pool)
export function parseSoundObjects(objs) {
  const m = sndMap(objs);
  const recs = Array.from({ length: SOUND_SLOTS }, (_, i) => { const r = sndRecord(m, i + 1); return r && recordValid(r) ? r : null; });
  const binds = soundBindings(recs);
  const slots = recs.map((r, i) => {
    const slot = i + 1;
    if (!r) return { slot, label: sndLabel(slot), used: false, name: "", engine: null, engineName: "", kind: null, patch: null, bound: null, added: false };
    const engine = r[2], kind = patchKindOf(engine);
    return { slot, label: sndLabel(slot), used: true, name: sndDecodeName(r), engine, engineName: ENGINE_NAMES[engine], kind,
      patch: kind && sndBlob(m, kind, slot) ? kind : null, bound: binds[i], added: !binds[i] };
  });
  return { slots };
}

const sndB64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
const SND_B64_RE = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
function sndUnB64(s, what) {
  if (typeof s !== "string" || !SND_B64_RE.test(s)) throw new Error(`The sound file's ${what} is not base64`);
  return Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
}

// slot -> the sound file (an object; JSON.stringify it to save). binding: what the record's binding bytes say on the
// FM-1 it came from (informative: an import keeps the record's bytes, the FM-1 reads the binding from them)
export function exportSound(objs, slot, { firmware, created } = {}) {
  sndCheckSlot(slot);
  const m = sndMap(objs), r = sndRecord(m, slot);
  if (!r || !recordValid(r)) throw new Error(`${sndLabel(slot)} is empty`);
  const bound = parseSoundObjects(objs).slots[slot - 1].bound;
  const engine = r[2], kind = patchKindOf(engine), blob = kind && sndBlob(m, kind, slot), name = sndDecodeName(r);
  return {
    format: SOUND_FORMAT, version: 1, firmware: firmware || null, created: created || new Date().toISOString(), slot,
    name: nameValid(name) ? name : null, engine, engineName: ENGINE_NAMES[engine], binding: bindingOf(bound), record: sndB64(r),
    patch: blob ? { kind, data: sndB64(blob) } : null,
  };
}
// choralroot-sound-U05-MY_PAD.json
export const soundFileName = (slot, name) => `${SOUND_FORMAT}-${sndLabel(slot)}-${String(name || "").replace(/[^A-Za-z0-9-]/g, "_")}.json`;

// a sound file (its text or the parsed object) -> {record: Uint8Array(192), name, engine, kind, patch: Uint8Array | null}
// (a "binding" field, present or not, is not read: the record's two binding bytes are the truth)
export function readSoundFile(file) {
  if (typeof file === "string") {
    try { file = JSON.parse(file); } catch (e) { throw new Error("Not a sound file (not JSON)"); }
  }
  if (file && file.format === "felucca-backup") throw new Error("This is a whole backup, not a sound file: use Restore for it");
  if (!file || typeof file !== "object" || file.format !== SOUND_FORMAT) throw new Error("Not a ChoralRoot sound file");
  if (file.version !== 1) throw new Error(`Unsupported sound file version ${file.version}`);
  const record = sndUnB64(file.record, "record");
  if (record.length !== SND_REC) throw new Error(`The sound file's record is ${record.length} bytes, not ${SND_REC}`);
  if (!recordValid(record)) throw new Error("The sound file's record is not a valid user sound");
  const engine = record[2], kind = patchKindOf(engine);
  if (file.engine !== undefined && file.engine !== null && file.engine !== engine)
    throw new Error(`The sound file says engine ${file.engine}, its record holds ${engine} (${ENGINE_NAMES[engine]})`);
  let name = sndDecodeName(record);
  if (file.name !== undefined && file.name !== null) {
    if (!nameValid(file.name)) throw new Error("The sound file's name must be 1 to 12 characters (ASCII letters, digits, symbols, spaces)");
    name = file.name;
  }
  let patch = null;
  if (file.patch !== undefined && file.patch !== null) {
    if (typeof file.patch !== "object") throw new Error("The sound file's patch is not an object");
    if (!kind || file.patch.kind !== kind)
      throw new Error(`The sound file's patch (${file.patch.kind}) is not the kind of its engine ${ENGINE_NAMES[engine]} (${kind || "none"})`);
    patch = sndUnB64(file.patch.data, "patch");
    if (patch.length !== PATCH_SIZE[kind]) throw new Error(`The sound file's ${kind} patch is ${patch.length} bytes, not ${PATCH_SIZE[kind]}`);
    if (kind === "va" && (patch[0] !== 0x56 || patch[1] < 1 || patch[1] > 3)) throw new Error("The sound file's VA patch is not a VA patch (magic or version)");
    if (kind === "fm6" && (patch[112] !== 0x46 || patch[113] !== 1)) throw new Error("The sound file's FM6 patch is not an FM6 patch (magic)");
    if (kind === "quad" && (patch[0] !== 0x51 || patch[1] !== 1)) throw new Error("The sound file's QUAD patch is not a QUAD patch (magic or version)");
  }
  return { record, name, engine, engineName: ENGINE_NAMES[engine], kind, patch };
}

// ---- edits: new object bytes; -> {changed: [{id, bytes}] (the stores first, the bank last), objs: the new Map}
function sndNewBank() {
  const b = new Uint8Array(SND_BANK_SIZE), v = sndView(b);
  v.setUint32(0, SND_BANK_MAGIC, true); v.setUint16(4, SND_REC, true); v.setUint16(6, SND_PER_BANK, true);
  return b;
}
function sndNewStore(kind, half) {
  const s = SND_STORE[kind], b = new Uint8Array(s.size), v = sndView(b);
  v.setUint32(0, s.magic, true); v.setUint16(4, s.ver, true); v.setUint16(6, s.nslot, true);
  if (sndWhole(kind)) v.setUint16(12, PATCH_SIZE[kind], true);
  else { v.setUint16(8, half * 16, true); v.setUint16(10, PATCH_SIZE[kind], true); }
  return b;
}
// the slot's blob of `kind` = blob (null: cleared). Clearing a slot whose bit is clear changes nothing (as va_store_put)
function sndPutBlob(m, kind, slot, blob) {
  const p = sndPlace(kind, slot), old = m.get(p.id), ok = sndStoreOk(kind, old, p.half), uo = sndUsedOff(kind);
  if (!blob) {
    if (!ok || !(sndView(old).getUint32(uo, true) >>> p.bit & 1)) return;
    const b = old.slice(), v = sndView(b);
    v.setUint32(uo, (v.getUint32(uo, true) & ~(1 << p.bit)) >>> 0, true);
    b.fill(0, p.off, p.off + PATCH_SIZE[kind]);
    m.set(p.id, b); return;
  }
  if (kind === "va" && ok && sndView(old).getUint16(4, true) === 2)   // (a version-2 store is converted at the FM-1's boot)
    throw new Error("The VA patch store on the FM-1 is an older version: save any VA sound on the FM-1 once, then try again");
  const b = ok ? old.slice() : sndNewStore(kind, p.half), v = sndView(b);
  b.set(blob, p.off);
  v.setUint32(uo, (v.getUint32(uo, true) | 1 << p.bit) >>> 0, true);
  m.set(p.id, b);
}
function sndPutRecord(m, slot, rec) {   // rec null: the record zeroed (an empty or foreign bank: nothing to clear)
  const id = sndBankId(slot), old = m.get(id), ok = sndBankOk(old);
  if (!rec && !ok) return;
  const b = ok ? old.slice() : sndNewBank();
  if (rec) b.set(rec, sndRecOff(slot)); else b.fill(0, sndRecOff(slot), sndRecOff(slot) + SND_REC);
  m.set(id, b);
}
function sndResult(before, m) {
  const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  const ids = [9, 10, 11, 12, 13, 22, 6, 7].filter((id) => !same(before.get(id), m.get(id)));
  return { changed: ids.map((id) => ({ id, bytes: m.get(id) })), objs: m };
}
const sndWriteName = (rec, name) => { rec.fill(0, 4, 4 + SND_NAME); for (let i = 0; i < name.length; i++) rec[4 + i] = name.charCodeAt(i); };

// a sound (readSoundFile's result, or the file / its text) into slot
export function importSound(objs, slot, sound) {
  sndCheckSlot(slot);
  if (!sound || !(sound.record instanceof Uint8Array)) sound = readSoundFile(sound);
  const before = sndMap(objs), m = new Map(before), rec = sound.record.slice();
  if (!recordValid(rec)) throw new Error("Not a valid user sound record");
  if (sound.name !== undefined && sound.name !== null) {
    if (!nameValid(sound.name)) throw new Error("A name is 1 to 12 characters (ASCII letters, digits, symbols, spaces)");
    sndWriteName(rec, sound.name);
  }
  const kind = patchKindOf(rec[2]);
  if (sound.patch && (!kind || sound.patch.length !== PATCH_SIZE[kind])) throw new Error("The patch is not of the sound's engine");
  for (const k of SND_KINDS) sndPutBlob(m, k, slot, k === kind && sound.patch ? sound.patch : null);
  sndPutRecord(m, slot, rec);
  return sndResult(before, m);
}
export function renameSound(objs, slot, name) {
  sndCheckSlot(slot);
  if (!nameValid(name)) throw new Error("A name is 1 to 12 characters (ASCII letters, digits, symbols, spaces)");
  const before = sndMap(objs), m = new Map(before), r = sndRecord(m, slot);
  if (!r || !recordValid(r)) throw new Error(`${sndLabel(slot)} is empty`);
  const rec = r.slice();
  sndWriteName(rec, name);
  sndPutRecord(m, slot, rec);
  return sndResult(before, m);
}
export function deleteSound(objs, slot) {
  sndCheckSlot(slot);
  const before = sndMap(objs), m = new Map(before);
  for (const k of SND_KINDS) sndPutBlob(m, k, slot, null);
  sndPutRecord(m, slot, null);
  return sndResult(before, m);
}

// ---- the FM-1: read the eight objects (22 when listed), write the changed ones (the backup protocol, as captureBackup / restoreBackup)
const SND_RC = { 1: "invalid object", 2: "the content failed the FM-1's check", 3: "busy: stop the loop on the FM-1",
  4: "flash write failed", 5: "the session ended: try again" };
function sndRcError(rc, what) {
  const e = new Error(rc === 2 ? `The FM-1 refused the ${what} (rc 2)` : `The FM-1 answered for the ${what}: ${SND_RC[rc] || "error"} (rc ${rc})`);
  e.rc = rc; return e;
}
const sndSleep = (ms) => new Promise((r) => setTimeout(r, ms));
const SND_CHUNK = 256;

// -> {objs: Map id -> Uint8Array}; a firmware without the stores (Felucca, an older ChoralRoot) is refused
export async function readSounds(request, onProgress = () => {}) {
  for (let attempt = 0; ; attempt++) {
    const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
    const listed = new Map(manifest.map((o) => [o.id, o]));
    if (SOUND_IDS.some((id) => !listed.has(id) && !SOUND_IDS_OPTIONAL.includes(id)))
      throw new Error("This firmware does not hold the user sounds' patch stores (ChoralRoot needed)");
    const ids = SOUND_IDS.filter((id) => listed.has(id));
    const total = ids.reduce((n, id) => n + listed.get(id).size, 0), objs = new Map();
    let done = 0, changed = false;
    for (const id of ids) {
      const o = listed.get(id), bytes = new Uint8Array(o.size);
      for (let off = 0; off < o.size; off += SND_CHUNK) {
        const size = Math.min(SND_CHUNK, o.size - off);
        const a = await request([BACKUP_CMD.GET, [id, ...bkU32(off), size & 127, size >>> 7]], { timeout: 1000, retries: 1 });
        if (a[1]) throw sndRcError(a[1], objectName(id));
        if (a[0] !== id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== size) throw new Error("Unexpected reply from the FM-1");
        bytes.set(bkUnpack(a.slice(9), size), off);
        done += size; onProgress(done, total);
      }
      if ((o.size ? bkCrc(bytes) : 0) !== o.crc) { changed = true; break; }
      objs.set(id, bytes);
    }
    if (!changed) {
      for (const id of SOUND_IDS) if (!objs.has(id)) objs.set(id, new Uint8Array(0));   // (an unlisted optional store)
      return { objs };
    }
    if (attempt) throw new Error("The FM-1 changed during the read. Try again (stop the loop if one is saving).");
  }
}

// changed: [{id, bytes}] in the order to write (importSound & co. put the stores first, the bank last). Each object:
// PUT begin / data / commit; rc 3 (busy) retried each second opts.busyTries times (opts.onBusy() each time); a failure
// aborts that object and stops: nothing after it is written. -> the ids written
export async function writeSounds(request, changed, opts = {}) {
  const onProgress = opts.onProgress || (() => {}), busyTries = opts.busyTries ?? 120;
  const ask = (r) => request(r, { timeout: 4000, retries: 0 });
  const put = async (args, what) => { const a = await ask([BACKUP_CMD.PUT, args]); if (a[2]) throw sndRcError(a[2], what); return a; };
  const retry = async (f) => {
    for (let i = 0; ; i++) {
      try { return await f(); }
      catch (e) { if (e.rc !== 3 || i >= busyTries) throw e; if (opts.onBusy) opts.onBusy(); await sndSleep(1000); }
    }
  };
  const total = changed.reduce((n, o) => n + o.bytes.length, 0), written = [];
  let done = 0;
  for (const { id, bytes } of changed) {
    const what = objectName(id);
    await retry(() => put([0, id, ...bkU32(bytes.length), ...bkU32(bkCrc(bytes))], what));
    try {
      for (let off = 0; off < bytes.length; off += SND_CHUNK) {
        const chunk = bytes.subarray(off, off + SND_CHUNK);
        await put([1, id, ...bkU32(off), ...bkPack(chunk)], what);
        done += chunk.length; onProgress(done, total);
      }
      await retry(() => put([2, id], what));
    } catch (e) {
      await ask([BACKUP_CMD.PUT, [3, id]]).catch(() => {});
      throw e;
    }
    written.push(id);
  }
  return written;
}

// ---- .syx export and import (docs/SOUNDS.md ".syx export and import"): an FM6 slot's voice as a DX7 single voice, a
// CZ-1 slot's tone as Casio's tone dump; an import builds the rest of the record from the engine's template
// (tests/sound_templates.c prints these from the firmware; its --check fails until they are pasted again)
export const SOUND_TEMPLATES = {
  fm6: "pQQMW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQEBAQEBAQEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  cz: "pQQOW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQEBAQEBAQEIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
};
// the FM6 function settings an imported voice gets (eng_fm6.c FM6_FNDEF bit-packed as fm6_blob_make packs them: the
// fixture fm6_fn, = bytes 114..121 of a blob the firmware makes)
export const FM6_FN_DEFAULTS = Uint8Array.from([0x33, 0x00, 0x60, 0x1C, 0x00, 0x00, 0x00, 0x40]);
const FM6_VCED = 155, FM6_VMEM = 128, FM6_BANK = 32 * FM6_VMEM, CZ_TONE = 144;
const FV = { LC: 11, RC: 12, RS: 13, AMS: 14, KVS: 15, OL: 16, MODE: 17, FC: 18, FF: 19, DET: 20, OP: 21,
  PR1: 126, ALG: 134, FB: 135, OKS: 136, LFS: 137, LKS: 141, LFW: 142, LPMS: 143, TRNSP: 144, NAME: 145 };
const FM6_OPMAX = [99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14];
const FM6_VMAX = [99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48];
const fm6Max = (i) => (i < 126 ? FM6_OPMAX[i % 21] : i < FV.NAME ? FM6_VMAX[i - 126] : 126);
function fm6Sanitize(v) {               // eng_fm6.c fm6_sanitize: every value in its range, a bad name byte -> space
  for (let i = 0; i < FM6_VCED; i++) {
    if (i >= FV.NAME) { if (v[i] < 32 || v[i] > 126) v[i] = 32; }
    else if (v[i] > fm6Max(i)) v[i] = fm6Max(i);
  }
  return v;
}
function fm6Unpack(b) {                 // VMEM 128 -> VCED 155 (eng_fm6.c fm6_unpack, then sanitized)
  const v = new Uint8Array(FM6_VCED);
  for (let k = 0; k < 6; k++) {
    const o = b.subarray(k * 17, k * 17 + 17), d = k * FV.OP;
    for (let i = 0; i < 11; i++) v[d + i] = o[i] & 0x7F;
    v[d + FV.LC] = o[11] & 3; v[d + FV.RC] = o[11] >> 2 & 3;
    v[d + FV.RS] = o[12] & 7; v[d + FV.DET] = o[12] >> 3 & 15;
    v[d + FV.AMS] = o[13] & 3; v[d + FV.KVS] = o[13] >> 2 & 7;
    v[d + FV.OL] = o[14] & 0x7F;
    v[d + FV.MODE] = o[15] & 1; v[d + FV.FC] = o[15] >> 1 & 31;
    v[d + FV.FF] = o[16] & 0x7F;
  }
  for (let i = 0; i < 9; i++) v[FV.PR1 + i] = b[102 + i] & 0x7F;
  v[FV.ALG] &= 31;
  v[FV.FB] = b[111] & 7; v[FV.OKS] = b[111] >> 3 & 1;
  for (let i = 0; i < 4; i++) v[FV.LFS + i] = b[112 + i] & 0x7F;
  v[FV.LKS] = b[116] & 1; v[FV.LFW] = b[116] >> 1 & 7; v[FV.LPMS] = b[116] >> 4 & 7;
  v[FV.TRNSP] = b[117] & 0x7F;
  for (let i = 0; i < 10; i++) v[FV.NAME + i] = b[118 + i] & 0x7F;
  return fm6Sanitize(v);
}
function fm6Pack(v) {                   // VCED 155 -> VMEM 128 (eng_fm6.c fm6_pack)
  const b = new Uint8Array(FM6_VMEM);
  for (let k = 0; k < 6; k++) {
    const o = k * FV.OP, d = k * 17;
    for (let i = 0; i < 11; i++) b[d + i] = v[o + i] & 0x7F;
    b[d + 11] = (v[o + FV.LC] & 3) | (v[o + FV.RC] & 3) << 2;
    b[d + 12] = (v[o + FV.RS] & 7) | (v[o + FV.DET] & 15) << 3;
    b[d + 13] = (v[o + FV.AMS] & 3) | (v[o + FV.KVS] & 7) << 2;
    b[d + 14] = v[o + FV.OL] & 0x7F;
    b[d + 15] = (v[o + FV.MODE] & 1) | (v[o + FV.FC] & 31) << 1;
    b[d + 16] = v[o + FV.FF] & 0x7F;
  }
  for (let i = 0; i < 9; i++) b[102 + i] = v[FV.PR1 + i] & 0x7F;
  b[110] &= 31;
  b[111] = (v[FV.FB] & 7) | (v[FV.OKS] & 1) << 3;
  for (let i = 0; i < 4; i++) b[112 + i] = v[FV.LFS + i] & 0x7F;
  b[116] = (v[FV.LKS] & 1) | (v[FV.LFW] & 7) << 1 | (v[FV.LPMS] & 7) << 4;
  b[117] = v[FV.TRNSP] & 0x7F;
  for (let i = 0; i < 10; i++) b[118 + i] = v[FV.NAME + i] & 0x7F;
  return b;
}
// eight 7-bit bytes <-> seven (eng_fm6.c fm6_pack7 / fm6_unpack7): 128 <-> 112
function fm6Pack7(s) {
  const d = new Uint8Array(s.length / 8 * 7);
  for (let i = 0, o = 0; i < s.length; i += 8, o += 7)
    for (let j = 0; j < 7; j++) d[o + j] = (s[i + j] & 0x7F) | (s[i + 7] >> j & 1) << 7;
  return d;
}
function fm6Unpack7(s, n) {
  const d = new Uint8Array(n);
  for (let i = 0, o = 0; i < n; i += 8, o += 7) {
    for (let j = 0; j < 7; j++) { d[i + j] = s[o + j] & 0x7F; d[i + 7] |= (s[o + j] >> 7) << j; }
  }
  return d;
}
// an FM6 blob (128) -> the DX7 voice (VCED, 155 bytes); the function settings (114..121) are left out
export function fm6BlobToVced(blob) {
  if (!(blob instanceof Uint8Array) || blob.length !== PATCH_SIZE.fm6 || blob[112] !== 0x46 || blob[113] !== 1)
    throw new Error("Not an FM6 patch (128 bytes, 'F' 1 at 112)");
  return fm6Unpack(fm6Unpack7(blob.subarray(0, 112), FM6_VMEM));
}
// a DX7 voice (VCED, 155) -> an FM6 blob (128): every value clamped into its range, the function defaults
export function vcedToFm6Blob(vced) {
  if (!vced || vced.length !== FM6_VCED) throw new Error(`A DX7 voice is ${FM6_VCED} bytes, not ${vced ? vced.length : 0}`);
  const b = new Uint8Array(PATCH_SIZE.fm6);
  b.set(fm6Pack7(fm6Pack(fm6Sanitize(Uint8Array.from(vced)))), 0);
  b[112] = 0x46; b[113] = 1;
  b.set(FM6_FN_DEFAULTS, 114);
  return b;
}
const syxSum = (d) => (128 - (d.reduce((s, x) => s + x, 0) & 127)) & 127;
// a DX7 single voice: F0 43 00 00 01 1B <155> <checksum> F7 (163 bytes)
export function fm6VcedSyx(vced) {
  if (!vced || vced.length !== FM6_VCED) throw new Error(`A DX7 voice is ${FM6_VCED} bytes`);
  const d = Uint8Array.from(vced, (x) => x & 0x7F);
  return Uint8Array.from([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B, ...d, syxSum(d), 0xF7]);
}
// a CZ-1 tone: F0 44 00 00 70 30 <288 nibbles, the low first> F7 (295 bytes)
export function czToneSyx(tone) {
  if (!tone || tone.length !== CZ_TONE) throw new Error(`A CZ-1 tone is ${CZ_TONE} bytes`);
  const out = new Uint8Array(7 + 2 * CZ_TONE);
  out.set([0xF0, 0x44, 0x00, 0x00, 0x70, 0x30]);
  for (let j = 0; j < CZ_TONE; j++) { out[6 + 2 * j] = tone[j] & 15; out[7 + 2 * j] = tone[j] >> 4; }
  out[out.length - 1] = 0xF7;
  return out;
}
const SYX_WHAT = "a .syx for the FM-1 holds a DX7 voice or 32-voice bank (F0 43 ..) or a CZ-1 tone (F0 44 ..); " +
  "a raw file is 155 (a DX7 voice), 4096 (a DX7 bank) or 144 bytes (a CZ-1 tone)";
function syxFrame(f, n) {               // one F0 .. F7 frame -> {kind, voices | tones}
  const at = `SysEx message ${n} (${f.length} bytes)`;
  if (f[1] === 0x43 && (f[2] & 0xF0) === 0 && f[3] === 0 && f[4] === 0x01 && f[5] === 0x1B) {
    if (f.length !== 163) throw new Error(`${at}: a DX7 single voice is 163 bytes`);
    const d = f.subarray(6, 161);
    if (syxSum(d) !== f[161]) throw new Error(`${at}: the DX7 voice's checksum is wrong (the file is damaged)`);
    return { kind: "fm6", voices: [d.slice()] };
  }
  if (f[1] === 0x43 && (f[2] & 0xF0) === 0 && f[3] === 0x09 && f[4] === 0x20 && f[5] === 0x00) {
    if (f.length !== 4104) throw new Error(`${at}: a DX7 32-voice bank is 4104 bytes`);
    const d = f.subarray(6, 4102);
    if (syxSum(d) !== f[4102]) throw new Error(`${at}: the DX7 bank's checksum is wrong (the file is damaged)`);
    return { kind: "fm6", voices: Array.from({ length: 32 }, (_, k) => fm6Unpack(d.subarray(k * FM6_VMEM, (k + 1) * FM6_VMEM))) };
  }
  if (f[1] === 0x44 && f[2] === 0 && f[3] === 0 && (f[4] & 0xF0) === 0x70 && f[5] === 0x30) {
    const nib = f.subarray(6, f.length - 1);
    if (nib.length === 256) throw new Error("This is a CZ-101 / 1000 tone: the FM-1 takes only CZ-1 tones from files (send it to the FM-1 over MIDI to convert it)");
    if (nib.length !== 2 * CZ_TONE) throw new Error(`${at}: a CZ-1 tone dump holds 288 nibbles, this one ${nib.length}`);
    if (nib.some((x) => x > 15)) throw new Error(`${at}: not a CZ-1 tone dump (a nibble above 15)`);
    return { kind: "cz", tones: [Uint8Array.from({ length: CZ_TONE }, (_, j) => nib[2 * j] | nib[2 * j + 1] << 4)] };
  }
  if (f[1] === 0x43) throw new Error(`${at}: a Yamaha message that is not a DX7 voice or bank`);
  if (f[1] === 0x44) throw new Error(`${at}: a Casio message that is not a CZ-1 tone dump`);
  throw new Error(`${at}: not a DX7 voice or bank, nor a CZ-1 tone (${SYX_WHAT})`);
}
// a .syx (or raw) file's bytes -> {kind: "fm6", voices: [VCED 155 ..]} | {kind: "cz", tones: [144 ..]}
export function parseSyx(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes || []);
  if (!bytes.length) throw new Error("The file is empty");
  if (bytes[0] !== 0xF0) {
    const raw7 = () => bytes.every((x) => x < 0x80);
    if (bytes.length === FM6_VCED && raw7()) return { kind: "fm6", voices: [bytes.slice()] };
    if (bytes.length === FM6_BANK && raw7())
      return { kind: "fm6", voices: Array.from({ length: 32 }, (_, k) => fm6Unpack(bytes.subarray(k * FM6_VMEM, (k + 1) * FM6_VMEM))) };
    if (bytes.length === CZ_TONE) return { kind: "cz", tones: [bytes.slice()] };
    throw new Error(`Not a DX7 or CZ-1 file (${bytes.length} bytes): ${SYX_WHAT}`);
  }
  const frames = [];
  for (let i = 0; i < bytes.length;) {
    if (bytes[i] !== 0xF0) throw new Error(`Not a SysEx file: byte ${i} is outside a message`);
    let e = i + 1;
    while (e < bytes.length && bytes[e] !== 0xF7) {
      if (bytes[e] & 0x80) throw new Error(`SysEx message ${frames.length + 1} is cut short (byte ${e})`);
      e++;
    }
    if (e >= bytes.length) throw new Error(`SysEx message ${frames.length + 1} has no end (F7)`);
    frames.push(bytes.subarray(i, e + 1));
    i = e + 1;
  }
  const parts = frames.map((f, n) => syxFrame(f, n + 1)), kind = parts[0].kind;
  if (parts.some((p) => p.kind !== kind)) throw new Error("The file mixes DX7 voices and CZ-1 tones: keep one kind per file");
  return kind === "fm6" ? { kind, voices: parts.flatMap((p) => p.voices) } : { kind, tones: parts.flatMap((p) => p.tones) };
}
export const syxCount = (parsed) => (parsed.kind === "fm6" ? parsed.voices : parsed.tones).length;
// the name an imported voice / tone gets: printable ASCII, trimmed, cut to 12; empty -> the engine's default
function syxName(bytes, fallback) {
  const s = Array.from(bytes, (c) => (c < 32 || c > 126 ? " " : String.fromCharCode(c))).join("").trim().slice(0, SND_NAME).trimEnd();
  return s || fallback;
}
// a voice / tone of parseSyx's result (index 1..N) -> a sound as readSoundFile returns it (the template's record)
export function soundFromSyx(parsed, index = 1) {
  const list = parsed && (parsed.kind === "fm6" ? parsed.voices : parsed.kind === "cz" ? parsed.tones : null);
  if (!list) throw new Error("Not a parsed .syx");
  if (!Number.isInteger(index) || index < 1 || index > list.length) throw new Error(`Voice ${index} is not in the file (1..${list.length})`);
  const item = list[index - 1], fm6 = parsed.kind === "fm6";
  const record = sndUnB64(SOUND_TEMPLATES[parsed.kind], "template");
  const name = fm6 ? syxName(item.subarray(FV.NAME, FV.NAME + 10), "FM6 VOICE") : syxName(item.subarray(128, 144), "CZ TONE");
  sndWriteName(record, name);
  record[SND_BIND_NOTE] = SND_BIND_MARK; record[SND_BIND_FLAGS] = 0;   // bound to nothing: an added preset of its pool
  if (!recordValid(record)) throw new Error("The template record is not valid");
  const engine = record[2], kind = patchKindOf(engine);
  const patch = fm6 ? vcedToFm6Blob(item) : Uint8Array.from(item);
  return { record, name, engine, engineName: ENGINE_NAMES[engine], kind, patch };
}
// choralroot-sound-U05-NAME.syx
export const syxFileName = (slot, name) => soundFileName(slot, name).replace(/\.json$/, ".syx");
// an FM6 / CZ-1 slot with its patch -> {bytes: the .syx, name: the sound's name, fileName}
export function exportSyx(objs, slot) {
  sndCheckSlot(slot);
  const m = sndMap(objs), r = sndRecord(m, slot);
  if (!r || !recordValid(r)) throw new Error(`${sndLabel(slot)} is empty`);
  const engine = r[2], kind = patchKindOf(engine);
  if (kind !== "fm6" && kind !== "cz") throw new Error(`${sndLabel(slot)} is ${ENGINE_NAMES[engine]}: only FM6 and CZ-1 sounds export as .syx`);
  const blob = sndBlob(m, kind, slot);
  if (!blob) throw new Error(`${sndLabel(slot)} has no ${ENGINE_NAMES[engine]} patch stored (it plays the engine's defaults)`);
  const name = sndDecodeName(r);
  return { bytes: kind === "fm6" ? fm6VcedSyx(fm6BlobToVced(blob)) : czToneSyx(blob), name, fileName: syxFileName(slot, name) };
}
