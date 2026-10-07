// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// ChoralRoot's user sounds as files (docs/SOUNDS.md): the 32 slots U01..U32 read from the backup objects that hold
// them (6 7 the banks, 9 the VA patch store, 10 11 the FM6 patch store, 12 13 the CZ-1 tone store), one slot exported
// to a "choralroot-sound" file, a file imported into a slot, a slot renamed or deleted, as new object bytes; only the
// objects that changed are written back (the stores first, the bank last), each as a restore writes it (fm1backup.js).
// Pure: no DOM, no MIDI (the requests go through the caller's request function, as captureBackup's).
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, objectName } from "./fm1backup.js";

export const SOUND_IDS = [6, 7, 9, 10, 11, 12, 13];
export const SOUND_SLOTS = 32;
export const ENGINE_NAMES = ["ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS",
  "DRUM", "NOISE", "FM6", "VA", "CZ-1"];
export const PATCH_SIZE = { va: 110, fm6: 128, cz: 144 };
export const SOUND_FORMAT = "choralroot-sound";
export const patchKindOf = (engine) => (engine === 13 ? "va" : engine === 12 ? "fm6" : engine === 14 ? "cz" : null);

// the layouts (docs/SOUNDS.md "The objects that hold the user sounds")
const SND_REC = 192, SND_PER_BANK = 16, SND_BANK_SIZE = 8 + SND_PER_BANK * SND_REC, SND_BANK_MAGIC = 0x31425055;
const SND_USED = 0xA5, SND_NAME = 12;
const SND_STORE = {   // per kind: the object ids (VA: one store of 32, FM6 / CZ-1: two halves of 16), header, size
  va: { ids: [9], magic: 0x31534156, ver: 3, nslot: 32, size: 16 + 32 * 110 },
  fm6: { ids: [10, 11], magic: 0x55364D46, ver: 1, nslot: 16, size: 16 + 16 * 128 },
  cz: { ids: [12, 13], magic: 0x55315A43, ver: 1, nslot: 16, size: 16 + 16 * 144 },
};
const SND_KINDS = ["va", "fm6", "cz"];
const sndView = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const sndSlotOk = (slot) => Number.isInteger(slot) && slot >= 1 && slot <= SOUND_SLOTS;
const sndLabel = (slot) => `U${String(slot).padStart(2, "0")}`;
export const slotLabel = sndLabel;
function sndCheckSlot(slot) {
  if (!sndSlotOk(slot)) throw new Error(`Slot ${slot} is not a user slot (1..32)`);
}
// objs: a Map or an object id -> Uint8Array (a missing id = size 0) -> a Map of the seven ids
function sndMap(objs) {
  const get = (id) => (objs instanceof Map ? objs.get(id) : objs && objs[id]);
  return new Map(SOUND_IDS.map((id) => [id, get(id) instanceof Uint8Array ? get(id) : new Uint8Array(0)]));
}

// a record (192 bytes) is valid as up_valid tests it
export function recordValid(r) {
  if (!r || r.length !== SND_REC) return false;
  const ver = r[1], np = r[3];
  if (r[0] !== SND_USED || ver < 1 || ver > 5 || r[2] >= 15 || np < 8 || np > (ver >= 4 ? 144 : 72) || !r[4]) return false;
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
  const s = SND_STORE[kind], h = kind === "va" ? 0 : slot > 16 ? 1 : 0, k = kind === "va" ? slot - 1 : (slot - 1) % 16;
  return { id: s.ids[h], half: h, bit: k, off: 16 + k * PATCH_SIZE[kind] };
}
function sndStoreOk(kind, b, half) {
  const s = SND_STORE[kind];
  if (b.length !== s.size) return false;
  const v = sndView(b), ver = v.getUint16(4, true);
  if (v.getUint32(0, true) !== s.magic || v.getUint16(6, true) !== s.nslot) return false;
  if (kind === "va") return (ver === 3 || ver === 2) && v.getUint16(12, true) === PATCH_SIZE.va;
  return ver === 1 && v.getUint16(8, true) === half * 16 && v.getUint16(10, true) === PATCH_SIZE[kind] && v.getUint32(12, true) >>> 16 === 0;
}
const sndUsedOff = (kind) => (kind === "va" ? 8 : 12);
function sndBlob(m, kind, slot) {        // the slot's blob in the store of that kind, or null (no store, bit clear)
  const p = sndPlace(kind, slot), b = m.get(p.id);
  if (!sndStoreOk(kind, b, p.half) || !(sndView(b).getUint32(sndUsedOff(kind), true) >>> p.bit & 1)) return null;
  return b.subarray(p.off, p.off + PATCH_SIZE[kind]);
}
const sndDecodeName = (r) => { let s = ""; for (let i = 4; i < 4 + SND_NAME && r[i]; i++) s += String.fromCharCode(r[i]); return s; };
export const nameValid = (name) => typeof name === "string" && /^[\x20-\x7e]{1,12}$/.test(name);

// the seven objects -> the slot table (docs/SOUNDS.md "The slot table")
export function parseSoundObjects(objs) {
  const m = sndMap(objs);
  const slots = Array.from({ length: SOUND_SLOTS }, (_, i) => {
    const slot = i + 1, r = sndRecord(m, slot);
    if (!r || !recordValid(r)) return { slot, label: sndLabel(slot), used: false, name: "", engine: null, engineName: "", kind: null, patch: null };
    const engine = r[2], kind = patchKindOf(engine);
    return { slot, label: sndLabel(slot), used: true, name: sndDecodeName(r), engine, engineName: ENGINE_NAMES[engine], kind,
      patch: kind && sndBlob(m, kind, slot) ? kind : null };
  });
  return { slots };
}

const sndB64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
const SND_B64_RE = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
function sndUnB64(s, what) {
  if (typeof s !== "string" || !SND_B64_RE.test(s)) throw new Error(`The sound file's ${what} is not base64`);
  return Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
}

// slot -> the sound file (an object; JSON.stringify it to save)
export function exportSound(objs, slot, { firmware, created } = {}) {
  sndCheckSlot(slot);
  const m = sndMap(objs), r = sndRecord(m, slot);
  if (!r || !recordValid(r)) throw new Error(`${sndLabel(slot)} is empty`);
  const engine = r[2], kind = patchKindOf(engine), blob = kind && sndBlob(m, kind, slot), name = sndDecodeName(r);
  return {
    format: SOUND_FORMAT, version: 1, firmware: firmware || null, created: created || new Date().toISOString(), slot,
    name: nameValid(name) ? name : null, engine, engineName: ENGINE_NAMES[engine], record: sndB64(r),
    patch: blob ? { kind, data: sndB64(blob) } : null,
  };
}
// choralroot-sound-U05-MY_PAD.json
export const soundFileName = (slot, name) => `${SOUND_FORMAT}-${sndLabel(slot)}-${String(name || "").replace(/[^A-Za-z0-9-]/g, "_")}.json`;

// a sound file (its text or the parsed object) -> {record: Uint8Array(192), name, engine, kind, patch: Uint8Array | null}
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
  if (kind === "va") v.setUint16(12, PATCH_SIZE.va, true);
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
  const ids = [9, 10, 11, 12, 13, 6, 7].filter((id) => !same(before.get(id), m.get(id)));
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

// ---- the FM-1: read the seven objects, write the changed ones (the backup protocol, as captureBackup / restoreBackup)
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
    if (SOUND_IDS.some((id) => !listed.has(id))) throw new Error("This firmware does not hold the user sounds' patch stores (ChoralRoot needed)");
    const total = SOUND_IDS.reduce((n, id) => n + listed.get(id).size, 0), objs = new Map();
    let done = 0, changed = false;
    for (const id of SOUND_IDS) {
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
    if (!changed) return { objs };
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
