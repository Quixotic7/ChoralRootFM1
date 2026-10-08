// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// fm1backup.js against simulated devices: 7-bit packing, CRC, manifest checks, a full capture,
// validation before the first write, restore order (live music last) and abort on a failed chunk;
// ChoralRoot's objects (9 the VA patches, 40..49 the loops), a ChoralRoot round trip, busy retries, RESTART,
// and the archives across firmwares (Felucca -> ChoralRoot, ChoralRoot -> Felucca: the settings record cut back).
import { BACKUP_IDS, CR_BACKUP_IDS, BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, readBackup, captureBackup,
  restoreBackup, deviceInfo, restartDevice, backupFileName, backupFamily, objectName } from "./fm1backup.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throws = (f) => { try { f(); return false; } catch { return true; } };
const athrows = async (f) => { try { await f(); return false; } catch { return true; } };

const rnd = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 131 + seed * 17 + (i >> 3)) & 255);
ok(bkR32(bkU32(0xdeadbeef)) === 0xdeadbeef && bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "backup: numbers and CRC-32");
const b = rnd(1000, 1);
ok(bkUnpack(bkPack(b), b.length).every((v, i) => v === b[i]), "backup: 7-bit pack / unpack round trip");
ok(throws(() => bkUnpack([...bkPack(b.subarray(0, 14)), 0], 14)), "backup: trailing bytes refused");

/* a device: objects by id, sample slots, the order of writes */
function device(objs, opt = {}) {
  const ids = opt.ids || BACKUP_IDS;
  const d = { objs: new Map(objs), log: [], staged: null, busy: opt.busy || 0, restarts: 0 };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.INFO) return [...new TextEncoder().encode(opt.version || "FELUCCA 1.0"), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3];
    if (cmd === BACKUP_CMD.RESTART && opt.version?.startsWith("ChoralRoot")) { d.restarts++; return [0]; }
    if (cmd === BACKUP_CMD.LIST) {
      const out = [1, 0, ids.length];
      for (const id of ids) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      if (opt.changeOnGet === id && off === 0) v[0] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0) {
        if (!ids.includes(id) || id >= 32 && id <= 34 || (opt.sizes?.[id] !== undefined && bkR32(a, 2) !== opt.sizes[id])) return [op, id, 1];
        if (d.busy) { d.busy--; d.log.push(`busy ${id}`); return [op, id, 3]; }
        d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0];
      }
      if (op === 1) {
        if (opt.failChunk === id) return [op, id, 2];
        const off = bkR32(a, 2), p = a.slice(7), n = Math.min(256, d.staged.size - off);
        d.staged.bytes.push(...bkUnpack(p, n)); return [op, id, 0];
      }
      if (op === 2) { const s = Uint8Array.from(d.staged.bytes); const rc = bkCrc(s) === d.staged.crc ? 0 : 2; if (!rc) { d.objs.set(id, s); d.log.push(id); } return [op, id, rc]; }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    if (cmd === 11 || cmd === 12 || cmd === 13 || cmd === 14) { if (cmd === 13 || cmd === 14) d.log.push(32 + a[0]); return [a[0], 0]; }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const objs = [[0, rnd(3388, 2)], [1, rnd(1200, 3)], [2, rnd(3388, 4)], [6, rnd(3080, 5)]];
const dev = device(objs);
const file = await captureBackup(dev.request, "TEST");
ok(file.objects.length === BACKUP_IDS.length && file.objects[0].size === 3388 && file.objects[3].size === 0, "backup: capture lists every object, empty ones as 0");
ok(readBackup(JSON.stringify(file)).objects[2].bytes.every((v, i) => v === objs[2][1][i]), "backup: capture -> file -> bytes round trip");
ok(await athrows(() => captureBackup(device(objs.map(([i, v]) => [i, v.slice()]), { changeOnGet: 2 }).request, "TEST")), "backup: a device that changes during capture fails the capture");

const bad = JSON.parse(JSON.stringify(file)); bad.objects[2].crc ^= 1;
ok(throws(() => readBackup(bad)), "backup: a damaged file is refused");
const noRun = JSON.parse(JSON.stringify(file)); noRun.objects[0] = { ...noRun.objects[0], size: 0, crc: 0, data: "" };
noRun.firmware = "FELUCCA 1.0";
ok(throws(() => readBackup(noRun)), "backup: a file without the current music is refused");
ok(throws(() => bkManifest([1, 0, 3])), "backup: a short manifest is refused");

const target = device([]);
const damaged = JSON.parse(JSON.stringify(file)); damaged.objects[6].crc ^= 1;
ok(await athrows(() => restoreBackup(target.request, damaged)) && target.log.length === 0, "backup: restore validates every byte before the first write");
await restoreBackup(target.request, file);
const order = target.log.filter((x) => typeof x === "number");
ok(order.at(-1) === 0 && order.at(-2) === 1 && order.indexOf(2) < order.indexOf(1), "backup: restore order: projects, banks, samples, settings, live music last");
ok([0, 1, 2, 6].every((id) => target.objs.get(id).every((v, i) => v === objs.find((o) => o[0] === id)[1][i])), "backup: restored objects equal the source");
{   /* the FM6 patch bank (id 8) and the archives of firmware before it (11 objects, no id 8) */
  ok(BACKUP_IDS.includes(8) && file.objects.some((o) => o.id === 8), "backup: the FM6 bank (id 8) is part of the archive");
  const old = JSON.parse(JSON.stringify(file)); old.objects = old.objects.filter((o) => o.id !== 8);
  ok(readBackup(old).objects.length === 11, "backup: an archive of the 11 objects before FM6 still reads");
  const odd = JSON.parse(JSON.stringify(file)); odd.objects = odd.objects.filter((o) => o.id !== 33); odd.firmware = "FELUCCA 1.0";
  ok(throws(() => readBackup(odd)), "backup: a Felucca archive missing another object is refused");
  const dup = JSON.parse(JSON.stringify(file)); dup.objects[3] = { ...dup.objects[2] };
  ok(throws(() => readBackup(dup)), "backup: an archive with an object twice is refused");
}
const failing = device([], { failChunk: 2 });
ok(await athrows(() => restoreBackup(failing.request, file)) && failing.log.includes("abort 2") && !failing.log.includes(0), "backup: a refused chunk aborts that object and stops before the live music");

/* ------------------------------------------------------------------ ChoralRoot --- */
// the settings records: Felucca's PER4 (magic "PER4", 572 bytes here) and ChoralRoot's PER5 (+ its 192-byte block)
const per = (size, magic, seed) => { const b = rnd(size, seed); new DataView(b.buffer).setUint32(0, magic, true); return b; };
const PER4 = 0x50455234, PER5 = 0x50455235, S4 = 572, S5 = S4 + 192;
const crOpt = { ids: CR_BACKUP_IDS, version: "ChoralRoot 0.1", sizes: { 1: undefined } };
const crObjs = [[1, per(S5, PER5, 7)], [6, rnd(3080, 8)], [8, rnd(3612, 9)], [9, rnd(3536, 10)], [10, rnd(2064, 14)], [11, rnd(2064, 15)],
                [12, rnd(2320, 16)], [13, rnd(2320, 17)], [14, rnd(2332, 18)], [21, rnd(2332, 19)], [22, rnd(2576, 20)],
                [40, rnd(46, 11)], [44, rnd(2118, 12)], [49, rnd(3602, 13)]];
{
  const cr = device(crObjs, crOpt);
  const info = await deviceInfo(cr.request);
  ok(info && info.version === "ChoralRoot 0.1" && info.family === "choralroot", "choralroot: INFO -> version, family");
  const crFile = await captureBackup(cr.request, info.version);
  ok(crFile.objects.map((o) => o.id).join() === CR_BACKUP_IDS.join() && crFile.objects.find((o) => o.id === 9).size === 3536 &&
     crFile.objects.find((o) => o.id === 49).size === 3602 && crFile.objects.find((o) => o.id === 41).size === 0 &&
     crFile.objects.find((o) => o.id === 10).size === 2064 && crFile.objects.find((o) => o.id === 11).size === 2064,
     "choralroot: capture lists settings, banks, FM6, VA (9), FM6 patches (10, 11), loops 40..49 (no samples)");
  ok(crFile.objects.find((o) => o.id === 12).size === 2320 && crFile.objects.find((o) => o.id === 14).size === 2332 &&
     crFile.objects.find((o) => o.id === 15).size === 0 && crFile.objects.find((o) => o.id === 21).size === 2332,
     "choralroot: capture lists the CZ-1 tones (12, 13) and the CZ-1 banks (14..21; a bank never saved: none)");
  ok(crFile.objects.find((o) => o.id === 22).size === 2576, "choralroot: capture lists the QUAD patches (22)");
  ok(readBackup(JSON.stringify(crFile)).objects.length === 28, "choralroot: the archive reads back (28 objects)");
  ok(objectName(9) === "VA patches" && objectName(10) === "FM6 patches 1-16" && objectName(11) === "FM6 patches 17-32" &&
     objectName(12) === "CZ-1 tones 1-16" && objectName(13) === "CZ-1 tones 17-32" && objectName(14) === "CZ-1 bank A" &&
     objectName(21) === "CZ-1 bank H" && objectName(22) === "FM TONE patches" && objectName(44) === "loop slot 5" && objectName(33) === "sample slot 2", "choralroot: object names");
  ok(backupFileName(info.version, new Date("2026-10-06T12:00:00Z")) === "choralroot-backup-20261006.json" &&
     backupFileName("FELUCCA 1.0", new Date("2026-10-06T12:00:00Z")) === "felucca-backup-20261006.json", "choralroot: file names");

  // round trip: an erased ChoralRoot restores everything, settings after the rest; busy answers are retried
  const blank = device([], { ...crOpt, busy: 1 });
  let busies = 0;
  const r = await restoreBackup(blank.request, crFile, () => {}, { onBusy: () => busies++ });
  const order = blank.log.filter((x) => typeof x === "number");
  ok(crObjs.every(([id, v]) => blank.objs.get(id) && blank.objs.get(id).every((b, i) => b === v[i])) && !r.skipped.length,
     "choralroot: round trip: every object restored byte for byte, none skipped");
  ok(order.at(-1) === 1 && order.includes(9) && order.includes(10) && order.includes(11) && order.includes(49),
     "choralroot: the settings last, the VA and FM6 patches and loops before");
  ok(busies === 1 && blank.log[0] === "busy 6", "choralroot: a busy answer (a loop plays) is retried after a second");
  await restartDevice(blank.request);
  ok(blank.restarts === 1, "choralroot: RESTART");
  const stubborn = device([], { ...crOpt, busy: 99 });
  ok(await athrows(() => restoreBackup(stubborn.request, crFile, () => {}, { busyTries: 0 })), "choralroot: still busy after the retries: the restore stops");

  // a Felucca archive on ChoralRoot: its settings, banks and FM6 bank; the music, projects and samples stay in the file
  const fel = await captureBackup(device([[0, rnd(3584, 20)], [1, per(S4, PER4, 21)], [3, rnd(3584, 22)], [6, rnd(3080, 23)], [8, rnd(3472, 24)]]).request, "FELUCCA 1.0");
  const onCr = device([], crOpt);
  const r2 = await restoreBackup(onCr.request, fel);
  ok(r2.restored.join() === "6,7,8,1", "felucca -> choralroot: banks, FM6 bank, then the settings");
  ok(onCr.objs.get(1).length === S4 && new DataView(onCr.objs.get(1).buffer).getUint32(0, true) === PER4 && onCr.objs.get(6).length === 3080 &&
     onCr.objs.get(8).length === 3472 && !onCr.objs.has(0) && !onCr.objs.has(3), "felucca -> choralroot: settings (PER4 as it is), banks, FM6 bank; no music, no projects");
  ok([0, 2, 3, 4, 5, 32, 33, 34].every((id) => r2.skipped.includes(id)) && !r2.skipped.includes(6), "felucca -> choralroot: the music, projects and the sample slots are reported skipped");

  // an older ChoralRoot archive (0.1, with sample slots 1-2) on this firmware: everything but the samples
  const old = JSON.parse(JSON.stringify(crFile));
  const smp = (id) => ({ id, size: 0, crc: 0, data: "" });
  old.objects.splice(5, 0, smp(32), smp(33));
  const onNew = device([], crOpt);
  const r4 = await restoreBackup(onNew.request, old);
  ok(r4.skipped.join() === "32,33" && r4.restored.includes(9) && r4.restored.at(-1) === 1, "choralroot 0.1 archive: its sample slots reported skipped, the rest restored");

  // a ChoralRoot archive on Felucca: the settings record cut back to PER4, the VA patches and loops skipped
  const onFel = device([[1, per(S4, PER4, 30)]], { sizes: { 1: S4 } });
  const r3 = await restoreBackup(onFel.request, crFile);
  const s = onFel.objs.get(1);
  ok(s.length === S4 && new DataView(s.buffer).getUint32(0, true) === PER4 && s.subarray(4).every((b, i) => b === crObjs[0][1][4 + i]),
     "choralroot -> felucca: the settings record becomes PER4 (Felucca's fields, the ChoralRoot block dropped)");
  ok(onFel.objs.get(6).length === 3080 && onFel.objs.get(8).length === 3612 && [9, 10, 11, 12, 14, 21, 40, 49].every((id) => r3.skipped.includes(id)) &&
     !onFel.objs.has(9) && !onFel.objs.has(10),
     "choralroot -> felucca: banks and FM6 bank sent (the device's own check takes or refuses its layout), VA / FM6 patches and loops skipped");
  ok(backupFamily("ChoralRoot 0.1") === "choralroot" && backupFamily("MELODEE 1") === "other", "families");
}

console.log(fails ? `BACKUP WEB TESTS FAILED (${fails})` : "backup web tests passed");
process.exit(fails ? 1 : 0);
