// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Local, complete archives. Requests name whitelisted objects, never flash addresses (web/EDITOR_PROTOCOL.md).
// One file format ("felucca-backup" version 1) for every firmware that speaks the protocol; each firmware lists its
// own objects (BACKUP_LIST) and a restore writes the objects of the file that the connected firmware lists:
//   Felucca     0 the music now, 1 settings, 2..5 projects, 6 7 user preset banks, 8 the FM6 bank, 32..34 samples
//   ChoralRoot  1 settings, 6 7 user sound banks, 8 the FM6 bank, 9 the VA patches, 10 11 the FM6 patches (user sounds
//               1-16, 17-32: voice + function settings), 12 13 the CZ-1 tones (user sounds 1-16, 17-32), 14..21 the
//               CZ-1 banks A..H, 22 the QUAD patches (0.14), 40..49 loop slots (no samples: the
//               all-synth firmware has no SAMPLE engine; ChoralRoot 0.1 archives with 32 33 restore without them)
// so a Felucca archive restores its settings, banks and FM6 bank on ChoralRoot (its samples are reported skipped), and
// a ChoralRoot archive those (its settings record cut back to Felucca's) on Felucca; the rest stays in the file.
export const BACKUP_IDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34];   // Felucca's archive (8: the FM6 patch bank)
const BACKUP_IDS_V1 = BACKUP_IDS.filter((id) => id !== 8);              // Felucca before FM6, and its archives
const idsOf = (n) => (n === BACKUP_IDS.length ? BACKUP_IDS : n === BACKUP_IDS_V1.length ? BACKUP_IDS_V1 : null);
export const CR_LOOP_IDS = Array.from({ length: 10 }, (_, k) => 40 + k);
export const CR_CZ_BANK_IDS = Array.from({ length: 8 }, (_, k) => 14 + k);
export const CR_BACKUP_IDS = [1, 6, 7, 8, 9, 10, 11, 12, 13, ...CR_CZ_BANK_IDS, 22, ...CR_LOOP_IDS];  // ChoralRoot's (9:
                                         // the VA patches, 10 11: the FM6 patches, 12 13: the CZ-1 tones, 14..21: the
                                         // CZ-1 banks, 22: the QUAD patches, 40..49: loops)
const KNOWN = new Set([...BACKUP_IDS, ...CR_BACKUP_IDS]);
export const BACKUP_CMD = { INFO: 1, LIST: 65, GET: 66, PUT: 67, RESTART: 72 };
const BACKUP_CHUNK = 256;
const isSample = (id) => id >= 32 && id <= 34;
const maxSize = (id) => (isSample(id) || !KNOWN.has(id) ? 81920 : 3840);
export function objectName(id) {
  if (id === 0) return "current music";
  if (id === 1) return "settings";
  if (id >= 2 && id <= 5) return `project ${id - 1}`;
  if (id === 6 || id === 7) return `user sounds ${id === 6 ? "1-16" : "17-32"}`;
  if (id === 8) return "FM6 patch bank";
  if (id === 9) return "VA patches";
  if (id === 10 || id === 11) return `FM6 patches ${id === 10 ? "1-16" : "17-32"}`;
  if (id === 12 || id === 13) return `CZ-1 tones ${id === 12 ? "1-16" : "17-32"}`;
  if (id >= 14 && id <= 21) return `CZ-1 bank ${String.fromCharCode(51 + id)}`;
  if (id === 22) return "QUAD patches";
  if (isSample(id)) return `sample slot ${id - 31}`;
  if (id >= 40 && id <= 49) return `loop slot ${id - 39}`;
  return `object ${id}`;
}
export const bkU32 = (n) => Array.from({ length: 5 }, (_, i) => (n >>> (i * 7)) & (i === 4 ? 15 : 127));
export const bkR32 = (a, off = 0) => {
  if (a.length < off + 5 || a[off + 4] > 15) throw new Error("Invalid archive number");
  return (a[off] | a[off + 1] << 7 | a[off + 2] << 14 | a[off + 3] << 21 | a[off + 4] << 28) >>> 0;
};
export function bkPack(bytes) {
  const a = [];
  for (let off = 0; off < bytes.length; off += 7) {
    const chunk = bytes.subarray(off, off + 7);
    a.push(chunk.reduce((m, b, i) => m | (b >>> 7) << i, 0), ...chunk.map((b) => b & 127));
  }
  return a;
}
export function bkUnpack(a, size) {
  const out = new Uint8Array(size); let i = 0, j = 0;
  while (j < size) {
    const n = Math.min(7, size - j), mask = a[i++];
    if (mask == null || mask >>> n) throw new Error("Invalid archive bytes");
    for (let k = 0; k < n; k++) {
      if (a[i] == null || a[i] > 127) throw new Error("Short archive chunk");
      out[j++] = a[i++] | (mask >>> k & 1) << 7;
    }
  }
  if (i !== a.length) throw new Error("Trailing archive bytes");
  return out;
}
export function bkCrc(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c >>> 1 ^ (c & 1 ? 0xedb88320 : 0);
  }
  return (~c) >>> 0;
}
const BK_RC = { 1: "Invalid archive object", 2: "Archive data failed validation", 3: "Stop playback first (the loop on the FM-1)",
  4: "Flash write failed", 5: "Start a fresh backup" };
function bkCheck(rc) {
  if (rc) { const e = new Error(BK_RC[rc] || `Archive error ${rc}`); e.rc = rc; throw e; }
}
// BACKUP_LIST's reply -> [{id, size, crc}]: any firmware's list (distinct ids; Felucca's, ChoralRoot's or unknown ones)
export function bkManifest(a) {
  if (a[0] !== 1) throw new Error("Unsupported archive protocol");
  bkCheck(a[1]);
  const n = a[2];
  if (!n || a.length !== 3 + n * 11) throw new Error("Incomplete archive manifest");
  const seen = new Set();
  return Array.from({ length: n }, (_, i) => {
    const p = 3 + i * 11, id = a[p];
    if (seen.has(id)) throw new Error("Unexpected archive object");
    seen.add(id);
    const size = bkR32(a, p + 1), crc = bkR32(a, p + 6);
    if (size > maxSize(id) || (!size && crc)) throw new Error("Archive object too large");
    return { id, size, crc };
  });
}
const bkBase64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
export const backupFamily = (firmware) => (/^choralroot/i.test(firmware || "") ? "choralroot" : /^felucca/i.test(firmware || "") ? "felucca" : "other");
export function readBackup(file) {
  if (typeof file === "string") file = JSON.parse(file);
  if (!file || file.format !== "felucca-backup" || file.version !== 1 || !Array.isArray(file.objects) || !file.objects.length)
    throw new Error("Not a complete FM-1 backup");
  const felucca = backupFamily(file.firmware) === "felucca", ids = idsOf(file.objects.length), seen = new Set();
  if (felucca && (!ids || file.objects.some((o, i) => !o || o.id !== ids[i])))   // Felucca writes its whole set, in order
    throw new Error("Not a complete Felucca backup");
  const objects = file.objects.map((o) => {
    if (!o || !Number.isInteger(o.id) || o.id < 0 || o.id > 127 || seen.has(o.id) || !Number.isInteger(o.size) || o.size < 0 ||
        o.size > maxSize(o.id) || !Number.isInteger(o.crc) || o.crc < 0 || o.crc > 0xffffffff || typeof o.data !== "string" ||
        o.data.length !== 4 * Math.ceil(o.size / 3) || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(o.data))
      throw new Error("Invalid backup object");
    seen.add(o.id);
    const bytes = Uint8Array.from(atob(o.data), (c) => c.charCodeAt(0));
    if (bytes.length !== o.size || bkCrc(bytes) !== o.crc) throw new Error("Backup checksum mismatch");
    if (isSample(o.id) && o.size) {
      if (o.size < 512) throw new Error("Short sample backup");
      const view = new DataView(bytes.buffer), length = view.getUint32(16, true), count = bytes[6];
      if (view.getUint32(0, true) !== 0x504d5346 || view.getUint16(4, true) !== 1 || !count || count > 16 ||
          length !== o.size - 512 || bkCrc(bytes.subarray(512)) !== view.getUint32(20, true)) throw new Error("Invalid sample backup");
      for (let z = 0; z < count; z++) {
        const off = 32 + z * 28, start = view.getUint32(off,true), n = view.getUint32(off+4,true),
          ls = view.getUint32(off+8,true), le = view.getUint32(off+12,true), rate = view.getUint32(off+16,true);
        if (!n || start > length || Math.ceil(n/2) > length - start || n > 163840 || ls > le || le >= n ||
            !rate || rate > 262144 || bytes[off+24] > 88 || bytes[off+25] > bytes[off+26]) throw new Error("Invalid sample zone");
      }
    }
    return { ...o, bytes };
  });
  const by = new Map(objects.map((o) => [o.id, o]));
  if ((by.has(0) && !by.get(0).size) || (felucca && !by.get(1).size)) throw new Error("Backup is missing the current music or settings");
  if (!objects.some((o) => o.size)) throw new Error("The backup holds nothing");
  return { ...file, objects };
}
export async function captureBackup(request, firmware, onProgress = () => {}) {
  const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
  const objects = [], total = manifest.reduce((n, o) => n + o.size, 0); let done = 0;
  for (const o of manifest) {
    const bytes = new Uint8Array(o.size);
    for (let off = 0; off < o.size; off += BACKUP_CHUNK) {
      const size = Math.min(BACKUP_CHUNK, o.size - off);
      const a = await request([BACKUP_CMD.GET, [o.id, ...bkU32(off), size & 127, size >>> 7]], { timeout: 1000, retries: 1 });
      bkCheck(a[1]);
      if (a[0] !== o.id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== size) throw new Error("Unexpected backup reply");
      bytes.set(bkUnpack(a.slice(9), size), off);
      done += size; onProgress(done, total);
    }
    if (bkCrc(bytes) !== o.crc) throw new Error("The device changed during backup. Try again while stopped.");
    objects.push({ ...o, data: bkBase64(bytes) });
  }
  const file = { format: "felucca-backup", version: 1, firmware, created: new Date().toISOString(), objects };
  readBackup(file); return file;
}
// the settings record across firmwares: ChoralRoot's (PER5) is Felucca's (PER4) + its own 192-byte block at the end
const PER4 = 0x50455234, PER5 = 0x50455235, CR_BLOCK = 192;
function adaptObject(o, dev) {
  if (o.id !== 1 || !dev.size || o.size === dev.size || o.size !== dev.size + CR_BLOCK) return o;
  const view = new DataView(o.bytes.buffer, o.bytes.byteOffset, o.size);
  if (view.getUint32(0, true) !== PER5) return o;
  const bytes = o.bytes.slice(0, dev.size);
  new DataView(bytes.buffer).setUint32(0, PER4, true);
  return { ...o, bytes, size: bytes.length, crc: bkCrc(bytes), converted: true };
}
const bkSleep = (ms) => new Promise((r) => setTimeout(r, ms));
// restore the objects of `file` that the connected firmware lists; opts.onBusy() while the FM-1 answers busy (a loop
// plays: rc 3, retried each second, opts.busyTries times). -> the archive with restored / skipped ids
export async function restoreBackup(request, file, onProgress = () => {}, opts = {}) {
  const archive = readBackup(file); // Validate every byte before the first destructive request.
  const device = new Map(bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 })).map((o) => [o.id, o]));
  const plan = archive.objects.filter((o) => device.has(o.id)).map((o) => adaptObject(o, device.get(o.id)));
  const skipped = archive.objects.filter((o) => !device.has(o.id)).map((o) => o.id), restored = [];
  const total = plan.reduce((n, o) => n + o.size, 0); let done = 0;
  const busyTries = opts.busyTries ?? 120;
  const ask = async (r, o = {}) => request(r, { timeout: 4000, retries: 0, ...o });
  const retry = async (f) => {
    for (let i = 0; ; i++) {
      try { return await f(); }
      catch (e) { if (e.rc !== 3 || i >= busyTries) throw e; if (opts.onBusy) opts.onBusy(); await bkSleep(1000); }
    }
  };
  const put = async (args) => { const a = await ask([BACKUP_CMD.PUT, args]); bkCheck(a[2]); return a; };
  // Restore the live music last, the settings before it. Objects commit one by one; a disconnect can leave a partial restore.
  const order = [...plan.filter((o) => o.id > 1), ...plan.filter((o) => o.id === 1), ...plan.filter((o) => o.id === 0)];
  for (const o of order) {
    if (isSample(o.id)) {
      const slot = o.id - 32;
      const check = (a) => { if (a[0] !== slot) throw new Error("Unexpected sample reply"); bkCheck(a.at(-1)); };
      if (!o.size) await retry(async () => check(await ask([14, [slot]])));
      else {
        await retry(async () => check(await ask([11, [slot]])));
        for (let off = 512; off < o.size; off += BACKUP_CHUNK) {
          const chunk = o.bytes.subarray(off, off + BACKUP_CHUNK);
          await retry(async () => check(await ask([12, [slot, off & 127, off >>> 7 & 127, off >>> 14 & 127, ...bkPack(chunk)]])));
          done += chunk.length; onProgress(done, total);
        }
        await retry(async () => check(await ask([13, [slot, ...bkPack(o.bytes.subarray(0, 480))]])));
        done += 512; onProgress(done, total);
      }
    } else {
      try { await retry(() => put([0, o.id, ...bkU32(o.size), ...bkU32(o.crc)])); }
      catch (e) { if (o.converted && e.rc === 1) { skipped.push(o.id); continue; } throw e; }
      try {
        for (let off = 0; off < o.size; off += BACKUP_CHUNK) {
          const chunk = o.bytes.subarray(off, off + BACKUP_CHUNK);
          await put([1, o.id, ...bkU32(off), ...bkPack(chunk)]);
          done += chunk.length; onProgress(done, total);
        }
        await retry(() => put([2, o.id]));
      } catch (e) {
        await put([3, o.id]).catch(() => {});
        if (o.converted && e.rc === 2) { skipped.push(o.id); continue; }   // another firmware's settings: best effort
        throw e;
      }
    }
    restored.push(o.id);
  }
  return { ...archive, restored, skipped };
}
// INFO -> {version, family}; null when the firmware does not answer (stock, an update loader)
export async function deviceInfo(request) {
  let a;
  try { a = await request([BACKUP_CMD.INFO, []], { timeout: 1500, retries: 0 }); } catch (_) { return null; }
  const end = a.indexOf(0), version = String.fromCharCode(...a.slice(0, end < 0 ? a.length : end));
  return { version, family: backupFamily(version) };
}
// ChoralRoot: restart after a restore, so everything restored loads from flash (its settings wait for it)
export async function restartDevice(request) {
  const a = await request([BACKUP_CMD.RESTART, []], { timeout: 2000, retries: 0 });
  bkCheck(a[0]);
}
export const backupFileName = (firmware, date = new Date()) =>
  `${backupFamily(firmware) === "other" ? "fm1" : backupFamily(firmware)}-backup-${date.toISOString().slice(0, 10).replace(/-/g, "")}.json`;

// Installer connection: no editor page or watcher owns this input simultaneously.
export class BackupConnection {
  constructor(input, output) {
    this.input = input; this.output = output; this.pending = null;
    input.onmidimessage = (e) => {
      const d = e.data;
      if (d.length < 6 || d[0] !== 240 || d[1] !== 125 || d[2] !== 70 || d[3] !== 76 || d.at(-1) !== 247) return;
      if (this.pending?.cmd === d[4]) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.resolve(Array.from(d.slice(5, -1))); }
    };
  }
  async request([cmd, args], opt = {}) {
    if (this.pending || this.closed || this.input.state === "disconnected" || this.output.state === "disconnected") throw new Error("Backup connection closed or busy");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new Error("Backup timed out. Keep the FM-1 connected and stopped.")); }, opt.timeout || 1200);
      this.pending = { cmd, timer, resolve, reject };
      try { this.output.send([240, 125, 70, 76, cmd, ...args, 247]); }
      catch (e) { clearTimeout(timer); this.pending = null; reject(e); }
    });
  }
  close() {
    this.closed = true; this.input.onmidimessage = null;
    if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.reject(new Error("Backup connection closed")); }
  }
}
