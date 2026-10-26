// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the device manager (web/fm1manager.js, web/manager.html; docs/DEVICE-MANAGER.md), no browser, no
// hardware: the simulated FM-1 of ?sim=1 (fm1manager.js makeSimFM1, web/test_installer.mjs's backup side) read and
// written through the session. Run from the repo root: node web/test_manager.mjs
// - the pools per engine in the firmware's order (INIT, factory, overwritten, added); the loop slots' headers
// - Place new / over a factory preset / over a user sound / of another engine; Place all; Keep; Swap (two user sounds,
//   a user sound and a factory preset, two loops); Rename; Delete (a factory preset comes back); loops placed, kept,
//   deleted; Undo restores each step (written back, read back); busy (rc 3) refused with "Stop the loop" and no undo step
// - the library: files (sound, loop, backup, .syx, pack), the store (in memory in node), the pack index and every pack
// - audition: program change and notes; the page's shape and its script as make_site.py inlines it
// - 3y79: Restore factory presets (selected, one engine's, every engine's), Delete all user sounds, Reset sounds to
//   factory against the pool simulator (which takes a bank as cr_backup.c does): each bank written once, the empty ones
//   not at all, a bank of 0.21's FM TONE slots (16 / 80) kept as read, the favourites without the deleted user slots,
//   the confirms' words, one Undo step (also after a write stopped part-way)

import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { captureBackup, loopHeader, deviceInfo, readBackup, restoreBackup, backupFileName } from "./fm1backup.js";
import { FACTORY_PRESETS, FACTORY_FIRST, FACTORY_IDS, readSoundFile, exportSound, exportSyx, parseSoundObjects, recordValid, withBinding, rawBindings, placeSound, writeSounds, POOL_IDS } from "./fm1sounds.js";
import { quadToV3 } from "./fm1dnsnd.js";
import {
  selectOnUnit, selectRequest, playingText, managerDrop, factorySoundFile, factoryFiles, usedSlots, makePack, packFileName, userSoundsPack, renameSoundFile, MANAGER_ENGINES, managerPools, managerLoops, loopSummary, managerPlace, managerPlaceAll, managerSwap, managerRename, managerDelete, managerReplacePlan, managerReplaceFactory, managerRestoreFactory, replaceFactoryText,
  managerKeep, keepLoop, readLoopFile, placeLoop, deleteLoop, swapLoops, ManagerSession, makeSimFM1, simObjects, makeLoopRecord,
  libraryItemsFromFile, libraryItemsFromDnsnd, SIM_POOL_IDS, LibraryStore, loadPackIndex, loadPack, auditionMessages, noteMessage, midiText, writeErrorText, UNDO_DEPTH, retiredNote,
  managerPorts, MANAGER_PORT_RE, pickUnit, favUserSlots, favsClearSlots, replacedRows, replacedOf, managerRestorePresets, managerDeleteAll, managerDeleteRows,
  restorePresetsText, deleteAllText, managerEngines, favMarks, favOf, favKeyOf, keepable, P5_NO_PACK, simP5Patch,
} from "./fm1manager.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(76)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const HERE = new URL(".", import.meta.url).pathname;
const sameObjs = (a, b) => [...a.keys()].every((id) => { const x = a.get(id), y = b.get(id) || new Uint8Array(0); return x.length === y.length && x.every((v, i) => v === y[i]); });
const names = (rows) => rows.map((r) => `${r.pos}:${r.name}:${r.type}${r.slot ? ":U" + r.slot : ""}`);

const P = (o) => managerPools(o, { empty: false });   // (the pools without the "Place here" row)
/* ------------------------------------------------------------- the pools --- */
const objs0 = simObjects();
const pools = P(objs0);
ok(MANAGER_ENGINES.every((e) => pools[e] && pools[e][0].type === "init" && pools[e][0].pos === 0), "pools: every engine's pool starts with 00 INIT");
ok(MANAGER_ENGINES.every((e) => pools[e].filter((r) => r.type !== "init" && r.f >= 0).length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e]),
  "pools: the factory positions of each engine (CZ-1 without INIT TONE)");
const fm6 = pools[12];
ok(fm6[1].type === "factory" && fm6[1].name === "TINE EP" && fm6[2].type === "user" && fm6[2].name === "MY TINE" && fm6[2].overwrites === "FM BELL" &&
   fm6[2].slot === 1, "pools: FM6 02 is U01 MY TINE over FM BELL (overwritten)");
ok(fm6.length === 27 && fm6[26].name === "BELL TOO" && fm6[26].slot === 5 && fm6[26].f === -1, "pools: FM6 26 is the added U05 BELL TOO, after the 25 factory");
ok(pools[14][13].slot === 9 && pools[14][13].overwrites === "SYNTH.BASS" && pools[14].at(-1).name === "CZ GLASS", "pools: CZ-1 13 over SYNTH.BASS, CZ GLASS added at the end");
ok(pools[13].at(-1).name === "WARM VA" && pools[7][1].name === "WHEEL BOW" && pools[7][1].overwrites === "FULL ORGAN", "pools: VA's added, WHEEL 01 overwritten");
ok(pools.loops === undefined && pools[15].every((r) => r.type !== "user"), "pools: FM TONE has no user sound");

/* ------------------------------------------------------------- the loops --- */
const loops = managerLoops(objs0);
ok(loops.length === 10 && loops.filter((l) => l.used).map((l) => l.slot).join() === "1,4", "loops: ten slots, 1 and 4 hold loops");
const h1 = loops[0].header, h4 = loops[3].header;
ok(h1.valid && h1.len === 4 * 384 && h1.bars === 4 && h1.nlayers === 3 && h1.nev === 24 && h1.ppqn === 96 && h1.sigName === "4/4",
  "loops: slot 1 header (len 1536 ticks = 4 bars at 96 ppqn, 3 layers, 24 events)");
ok(h4.valid && h4.bars === 2 && h4.sigName === "3/4" && loopSummary(h4) === "2 bars 3/4 · 1 layer · 6 events", "loops: slot 4 in 3/4, its summary");
ok(loopHeader(new Uint8Array(0)) === null && !loopHeader(new Uint8Array(20)).valid && loopSummary(null) === "empty", "loops: empty and unreadable records");
const lf = keepLoop(objs0, 1, { created: "2026-10-10T00:00:00Z" });
ok(lf.format === "choralroot-loop" && lf.bars === 4 && lf.nlayers === 3 && readLoopFile(JSON.stringify(lf)).bytes.length === h1.size, "loops: Keep -> a choralroot-loop file, read back");

/* --------------------------------------------- the session on the simulator --- */
const sim = makeSimFM1();
const s = new ManagerSession(sim.request);
await s.read();
ok(sameObjs(objs0, s.objs) && !s.canUndo, "session: the simulated FM-1 read (sounds and loops), no undo yet");
const snaps = [new Map(s.objs)];
const step = async (what, res, check) => {
  const before = sim.log.length;
  await s.apply(res);
  snaps.push(new Map(s.objs));
  ok(sim.log.length > before && check(s.objs), `${what} (written: ${sim.log.slice(before).join(" ")}; read back)`);
};
const packIdx = await loadPackIndex(async (url) => JSON.parse(readFileSync(join(HERE, url), "utf8")));
const packs = {};
for (const p of packIdx) packs[p.engine] = await loadPack(async (url) => JSON.parse(readFileSync(join(HERE, url), "utf8")), p);
const vaPad = packs[13][0], fmBass = packs[12][2], czBrass = packs[14][0];

await step("place new: a VA pack sound with nothing selected -> U04, added to VA", managerPlace(s.objs, vaPad.file, null), (o) => {
  const r = P(o)[13].at(-1); return r.slot === 4 && r.name === "LUSH PAD" && r.f === -1;
});
await step("place over factory: FM BASS onto FM6 04 BRASS -> U07 bound to BRASS", managerPlace(s.objs, fmBass.file, P(s.objs)[12][4]), (o) => {
  const r = P(o)[12][4]; return r.type === "user" && r.slot === 7 && r.name === "FM BASS" && r.overwrites === "BRASS";
});
await step("place over user: a CZ-1 sound onto CZ-1 13 (U09) -> U09 replaced, still over SYNTH.BASS", managerPlace(s.objs, czBrass.file, P(s.objs)[14][13]), (o) => {
  const r = P(o)[14][13]; return r.slot === 9 && r.name === "BRASS 1" && r.overwrites === "SYNTH.BASS";
});
await step("place of another engine: a CZ-1 sound with FM6 02 selected -> its own pool, added", managerPlace(s.objs, packs[14][5].file, P(s.objs)[12][2]), (o) => {
  const p = P(o); return p[14].at(-1).name === packs[14][5].name && p[14].at(-1).f === -1 && p[12][2].name === "MY TINE";
});
const keep = managerKeep(s.objs, P(s.objs)[12][2], { firmware: "ChoralRoot 0.15" });
ok(readSoundFile(keep).name === "MY TINE" && keep.binding && keep.binding.overwrites === 2, "keep: FM6 02 -> a choralroot-sound file (binding FM6 02)");
await step("swap two user sounds: FM6 02 (U01) and FM6 26 (U05) exchange places", managerSwap(s.objs, P(s.objs)[12][2], P(s.objs)[12].find((r) => r.slot === 5)), (o) => {
  const p = P(o)[12]; return p[2].name === "BELL TOO" && p[2].overwrites === "FM BELL" && p.at(-1).name === "MY TINE";
});
await step("swap a user sound with a factory preset: the added MY TINE onto FM6 06 MARIMBA", managerSwap(s.objs, P(s.objs)[12].at(-1), P(s.objs)[12][6]), (o) => {
  const p = P(o)[12]; return p[6].name === "MY TINE" && p[6].overwrites === "MARIMBA" && p.length === 26;
});
await step("rename: FM6 06 -> SOFT TINE", managerRename(s.objs, P(s.objs)[12][6], "SOFT TINE"), (o) => P(o)[12][6].name === "SOFT TINE");
await step("delete an overwriting sound: FM6 06 resets to MARIMBA", managerDelete(s.objs, P(s.objs)[12][6]), (o) => {
  const r = P(o)[12][6]; return r.type === "factory" && r.name === "MARIMBA";
});
await step("loops: the kept loop placed into slot 2", placeLoop(s.objs, 2, lf), (o) => managerLoops(o)[1].used && managerLoops(o)[1].header.nev === 24);
await step("loops: swap slots 1 and 4", swapLoops(s.objs, 1, 4), (o) => managerLoops(o)[0].header.sigName === "3/4" && managerLoops(o)[3].header.bars === 4);
await step("loops: delete slot 2", deleteLoop(s.objs, 2), (o) => !managerLoops(o)[1].used && sim.objs.get(41).length === 0);
const free = parseSoundObjects(s.objs).slots.filter((x) => !x.used).length;
const all = managerPlaceAll(s.objs, packs[12].map((it) => it.file));
ok(all.placed === free && all.skipped === packs[12].length - free, `place all: ${all.placed} placed into the free slots, ${all.skipped} skipped`);
await step("place all: written, every user slot used", all, (o) => parseSoundObjects(o).slots.every((x) => x.used));
let fullErr = null; try { managerPlace(s.objs, vaPad.file, null); } catch (e) { fullErr = e; }
ok(fullErr && /All 32 user slots/.test(fullErr.message), "place new with 32 slots used: refused");

// busy: a write while the loop plays is refused, nothing kept for undo
sim.playing = true;
const depth = s.history.length;
let busy = null; try { await s.apply(managerRename(s.objs, P(s.objs)[12][2], "BUSY")); } catch (e) { busy = e; }
ok(busy && busy.rc === 3 && /Stop the loop on the FM-1 first/.test(writeErrorText(busy)) && s.history.length === depth, "busy (rc 3): refused, \"Stop the loop on the FM-1 first\", no undo step");
sim.playing = false;

// undo every step back to the start
let undoOk = true;
for (let k = snaps.length - 2; k >= 0; k--) {
  await s.undo();
  if (!sameObjs(snaps[k], s.objs) || !sameObjs(snaps[k], sim.objs)) { undoOk = false; console.log(`  undo to step ${k} differs`); }
}
ok(undoOk && !s.canUndo && sameObjs(objs0, sim.objs), `undo: ${snaps.length - 1} steps undone one by one, the FM-1 back to its first state`);
const s2 = new ManagerSession(makeSimFM1().request); await s2.read();
for (let k = 0; k < UNDO_DEPTH + 3; k++) await s2.apply(managerRename(s2.objs, P(s2.objs)[12][2], `N${k}`));
ok(s2.history.length === UNDO_DEPTH, `undo: at most ${UNDO_DEPTH} steps kept`);

/* ------------------------------------- capacity, packs of one's own, factory keep, reorder --- */
const PACKED = MANAGER_ENGINES.filter((e) => e !== 16);   // (P5: no pack, Sequential's programs: tools/make_packs.py NO_PACK)
const fpacks = factoryFiles(PACKED.map((e) => ({ engine: e, sounds: packs[e].map((it) => it.file) })));
const o30 = simObjects(30);
ok(usedSlots(o30) === 30 && MANAGER_ENGINES.every((e) => managerPools(o30)[e].at(-1).type === "empty"), "capacity: the 30-sound simulator; every pool ends with an empty \"Place here\" row");
const fit = managerPlaceAll(o30, packs[13].slice(0, 5).map((it) => it.file));
ok(fit.placed === 2 && fit.skipped === 3 && usedSlots(fit.objs) === 32, "capacity: Place all 5 into 2 free slots -> 2 placed, 3 did not fit");
let capErr = null; try { managerPlace(fit.objs, packs[7][0].file, managerPools(fit.objs)[7][2]); } catch (e) { capErr = e; }
ok(MANAGER_ENGINES.every((e) => managerPools(fit.objs)[e].at(-1).type !== "empty") && capErr && /All 32 user slots/.test(capErr.message),
  "capacity: 32 used -> no empty rows, Place over a factory preset refused (no free slot)");
ok(managerPlace(o30, packs[7][0].file, managerPools(o30)[7].at(-1)).where === "new", "capacity: Place onto the empty row -> a new added sound");
const tine = managerKeep(objs0, P(objs0)[12][1], { packs: fpacks });
ok(tine.factory === true && tine.engine === 12 && tine.position === 1 && readSoundFile(tine).name === "TINE EP", "factory keep: FM6 01 TINE EP -> its pack entry, factory: true, engine 12, position 1");
const back = managerPlace(objs0, tine, null);
ok(back.where === "over-factory" && P(back.objs)[12][1].type === "user" && P(back.objs)[12][1].overwrites === "TINE EP", "factory keep: placed with nothing selected -> back over its own position");
const sel = [P(objs0)[12][1], P(objs0)[12][2], P(objs0)[14][13]].map((r) => managerKeep(objs0, r, { packs: fpacks }));
const mine = makePack(sel, { name: "Picks", author: "me", created: "2026-10-10T00:00:00Z" });
const mineItems = libraryItemsFromFile(packFileName("Picks"), JSON.stringify(mine));
ok(mine.format === "choralroot-pack" && mineItems.length === 3 && mineItems.map((i) => i.name).join() === "TINE EP,MY TINE,CZ BASS 2", "pack: three selected rows (factory + user) saved as ONE pack, read back as 3 items");
const all6 = userSoundsPack(objs0, { created: "2026-10-10T00:00:00Z" });
const blank = new Map([...objs0.keys()].map((id) => [id, new Uint8Array(0)]));
const reloaded = managerPlaceAll(blank, libraryItemsFromFile("u.json", JSON.stringify(all6)).map((i) => i.file));
ok(all6.sounds.length === 6 && reloaded.placed === 6 && P(reloaded.objs)[12][2].name === "MY TINE" && P(reloaded.objs)[14][13].name === "CZ BASS 2",
  "pack: Save all user sounds -> 6 in one file; loaded back, overwrites return to their positions");
let rn = renameSoundFile(tine, "MY EP"); let longErr = null; try { renameSoundFile(tine, "THIRTEEN CHAR"); } catch (e) { longErr = e; }
ok(readSoundFile(rn).name === "MY EP" && longErr, "library rename: a file renamed (12-character rule)");
const two = managerPlace(objs0, packs[12][3].file, null).objs;          // FM6: U04 BRASS (added), U05 BELL TOO (added)
const pre = P(two)[12], a1 = pre.findIndex((r) => r.slot === 4), a2 = pre.findIndex((r) => r.slot === 5);
const d1 = managerDrop(two, pre[a2], pre[a1]).objs;
ok(P(d1)[12][a1].name === "BELL TOO" && P(d1)[12][a1].slot === 4 && P(d1)[12][a2].name === "BRASS", "reorder: an added sound dragged onto another -> their slots swap, the order follows");
const d2 = managerDrop(objs0, P(objs0)[12][2], P(objs0)[12][5]).objs;
ok(P(d2)[12][5].name === "MY TINE" && P(d2)[12][5].overwrites === "FM PAD" && P(d2)[12][2].type === "factory", "reorder: a user sound dragged onto a factory position -> bound there, its old place back to factory");
const d3 = managerDrop(objs0, P(objs0)[12][7], P(objs0)[12].at(-1)).objs;
ok(P(d3)[12][7].name === "BELL TOO" && P(d3)[12][7].overwrites === "FM ORGAN", "reorder: a factory row dragged onto an added sound -> the sound takes that position");
const st2 = new LibraryStore(null); const k2 = await st2.add(items0()); const it2 = (await st2.all())[0];
await st2.put({ ...it2, name: "MY EP", file: rn }); 
ok((await st2.all())[0].name === "MY EP" && (await st2.all()).length === 1, "library rename: This browser's entry renamed in place");
function items0() { return libraryItemsFromFile("k.json", JSON.stringify(tine))[0]; }

/* ---------------------------------------------------------- the library --- */
const items = libraryItemsFromFile("k.json", JSON.stringify(keep));
ok(items.length === 1 && items[0].type === "sound" && items[0].pool === 12 && items[0].name === "MY TINE", "library: a sound file -> one FM6 item");
ok(libraryItemsFromFile("l.json", JSON.stringify(lf))[0].header.bars === 4, "library: a loop file -> a loop item");
const bsim = makeSimFM1();
const backup = await captureBackup(bsim.request, "ChoralRoot 0.15");
const bItems = libraryItemsFromFile("b.json", JSON.stringify(backup));
ok(bItems.filter((i) => i.type === "sound").length === 6 && bItems.filter((i) => i.type === "loop").length === 2, "library: a whole backup -> its 6 sounds and 2 loops");
const syx = exportSyx(objs0, 2);
const sItems = libraryItemsFromFile("t.syx", syx.bytes);
ok(sItems.length === 1 && sItems[0].engine === 14 && sItems[0].patch, "library: a CZ-1 .syx -> a CZ-1 item with its tone");
let bad = null; try { libraryItemsFromFile("x.json", "{}"); } catch (e) { bad = e; }
ok(bad && /not a sound, loop, pack, backup or \.syx/.test(bad.message), "library: an unknown file is refused");
const store = new LibraryStore(null);
const key = await store.add(items[0]); await store.add(sItems[0]);
const got = await store.all();
await store.remove(key);
ok(got.length === 2 && (await store.all()).length === 1, "library: the store (in memory in node): add, list, remove");

/* ------------------------------------------------------------- the packs --- */
ok(packIdx.length === PACKED.length && packIdx.map((p) => p.engine).join() === PACKED.join() && !packs[16],
  "packs: the index lists one pack per engine, in the picker's order, none for P5 (Sequential's programs)");
ok(PACKED.every((e) => packs[e].length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e] &&
   packs[e].every((it, k) => it.name === FACTORY_PRESETS[e][k + FACTORY_FIRST[e]] && it.engine === e && recordValid(readSoundFile(it.file).record))),
  "packs: every pack holds its engine's factory presets, named and valid");
ok([12, 13, 14, 15].every((e) => packs[e].every((it) => it.patch)) && [7].every((e) => packs[e].every((it) => !it.patch)),
  "packs: VA, FM6, CZ-1 and FM TONE carry their patches; the others are records (macros) only");
{ // 0.18: PHASE, LOFI, VOICE, TRIO, NOISE, then PHYS retired: no pool, no pack; a record of one is an added row of the VA's pool
  const old = { record: Uint8Array.from(readSoundFile(packs[7][0].file).record), name: "PD BRASS", engine: 2, kind: null, patch: null };
  old.record[2] = 2;
  const withOld = managerPlace(objs0, old, null), row = P(withOld.objs)[13].at(-1);
  ok(MANAGER_ENGINES.join() === "12,15,13,14,16,7" && [2, 3, 5, 6, 9, 11].every((e) => !(e in pools) && !(e in packs)) && Object.keys(packs).length === 5,
    "0.18: five engines, 0.22 six (FM6, FM TONE, VA, CZ-1, P5, WHEEL), five packs (none for P5)");
  { // .. and a PHYS sound (a 0.17 slot, an old pack's file): the same
    const ph = { record: Uint8Array.from(readSoundFile(packs[7][0].file).record), name: "HARP", engine: 9, kind: null, patch: null };
    ph.record[2] = 9;
    const withPh = managerPlace(objs0, ph, null), prow = P(withPh.objs)[13].at(-1);
    ok(withPh.where === "new" && prow.name === "HARP" && prow.engine === 13 && prow.recEngine === 9 && prow.f === -1 &&
       retiredNote(prow.recEngine) === "PHYS · retired" && parseSoundObjects(withPh.objs).slots[prow.slot - 1].engine === 9 &&
       managerKeep(withPh.objs, prow).engineName === "PHYS",
      "0.18: a PHYS sound: an added row of the VA's pool, \"PHYS · retired\", its record kept");
  }
  ok(withOld.where === "new" && row.name === "PD BRASS" && row.type === "user" && row.engine === 13 && row.recEngine === 2 && row.f === -1 &&
     retiredNote(row.recEngine) === "PHASE · retired" && retiredNote(13) === "" && retiredNote(undefined) === "" &&
     parseSoundObjects(withOld.objs).slots[row.slot - 1].engine === 2 && managerKeep(withOld.objs, row).engineName === "PHASE",
    "0.18: a PHASE sound: an added row of the VA's pool, \"PHASE · retired\", its record kept (Keep gives the PHASE file back)");
}

/* ------------------------------------------------------------ audition --- */
ok(midiText(auditionMessages(fm6[2])[0]) === "C0 02" && midiText(auditionMessages(fm6[2], { bass: true })[0]) === "C1 03",
  "audition: program change of the pool position (bass: channel 2, position + 1)");
ok(midiText(noteMessage(true, 60)) === "90 3C 64" && midiText(noteMessage(false, 60, { bass: true })) === "81 3C 00", "keyboard: note on ch 1 velocity 100, note off ch 2");

const ssim = makeSimFM1();
const pad = P(objs0)[12][5];
ok(midiText(selectRequest(pad)[1]) === "00 0C 05" && midiText(selectRequest(pad, { bass: true })[1]) === "01 0C 05", "select: SysEx 76 part, engine, position (chord 0, bass 1)");
const sel1 = await selectOnUnit(ssim.request, pad);
ok(sel1.ok && ssim.selected.engine === 12 && ssim.selected.pos === 5 && playingText(pad) === "FM6 · 05 FM PAD on the unit", "select: the unit switches to FM6 05 FM PAD; the page says what plays");
ok((await selectOnUnit(ssim.request, { engine: 4, pos: 0 })).rc === 1 && (await selectOnUnit(ssim.request, { engine: 12, pos: 99 })).rc === 2, "select: bad engine rc 1, bad position rc 2");
ssim.playing = true; const selBusy = await selectOnUnit(ssim.request, pad); ssim.playing = false;
ok(selBusy.rc === 3, "select: busy rc 3");
ssim.select = false;
ok((await selectOnUnit(ssim.request, pad)).unsupported === true, "select: an older firmware (no reply) -> unsupported (the program change fallback)");
ok(/manager_select|selectOnUnit/.test(readFileSync(join(HERE, "manager.html"), "utf8")) && readFileSync(join(HERE, "manager.html"), "utf8").includes("reselect("), "page: rows select on the unit, writes re-select the affected row");

/* ------------- several FM-1s (docs/DEVICE-MANAGER.md "Several FM-1s"): the unit's ID, which one to open --- */
{
  const withId = makeSimFM1(new Map(), { version: "ChoralRoot 0.22", unitId: "A1B2C3D4" });
  const info = await deviceInfo(withId.request), old = await deviceInfo(makeSimFM1(new Map()).request);
  ok(info.unitId === "A1B2C3D4" && info.version === "ChoralRoot 0.22" && info.family === "choralroot" && old.unitId === null && old.version === "ChoralRoot 0.15",
     "deviceInfo: the unit's ID from INFO's tag 49 08 (a firmware before it: null; the version as before)");
  const P = "ChoralRoot FM-1", port = (name, state = "connected") => ({ name, state });
  const ins = [port(P), port("IAC Bus 1"), port(P), port(P, "disconnected")], outs = [port("IAC Bus 1"), port(P), port(P)];
  const acc = { inputs: new Map(ins.map((x, i) => [i, x])), outputs: new Map(outs.map((x, i) => [i, x])) };
  const pairs = managerPorts(acc, P);
  ok(pairs.length === 2 && pairs[0].input === ins[0] && pairs[0].output === outs[1] && pairs[1].input === ins[2] && pairs[1].output === outs[2],
     "managerPorts: two FM-1s with ports of one name: two pairs, the k-th input with the k-th output (a disconnected port left out)");
  {   // issue 32: a Mac keeps the name it first saw for the device ("FM-1"): the manager's match takes it, and only FM-1 names
    const ins2 = [port("FM-1"), port("IAC Bus 1"), port("ChoralRoot FM-1")], outs2 = [port("FM-1"), port("IAC Bus 1"), port("ChoralRoot FM-1")];
    const acc2 = { inputs: new Map(ins2.map((x, i) => [i, x])), outputs: new Map(outs2.map((x, i) => [i, x])) };
    const pairs2 = managerPorts(acc2, MANAGER_PORT_RE);
    ok(pairs2.length === 2 && pairs2[0].input === ins2[0] && pairs2[1].input === ins2[2] && !managerPorts(acc2, MANAGER_PORT_RE).some((p) => p.input === ins2[1]),
       "managerPorts: MANAGER_PORT_RE takes \"FM-1\" (the name macOS kept) and \"ChoralRoot FM-1\", never IAC");
  }
  const A = { unitId: "A1B2C3D4", version: "ChoralRoot 0.22" }, B = { unitId: "0F0E0D0C", version: "ChoralRoot 0.22" }, C = { unitId: null, version: "ChoralRoot 0.21" };
  const asked = [];
  const ask = (answer) => (text) => { asked.push(text); return answer; };
  ok(pickUnit([A], { ask: ask("2") }) === A && !asked.length, "pickUnit: one FM-1, none opened before: it, nothing asked (as always)");
  ok(pickUnit([A, B], { remembered: "0F0E0D0C", ask: ask(null) }) === B && !asked.length, "pickUnit: the one opened before, there again: it, nothing asked");
  ok(pickUnit([A, B, C], { ask: ask("0f0e0d0c") }) === B && asked.at(-1).includes("3 FM-1s are connected") && asked.at(-1).includes("1   unit ID A1B2C3D4") &&
     asked.at(-1).includes("3   no unit ID   ChoralRoot 0.21"), "pickUnit: several: asked with the list, answered by an ID (any case)");
  ok(pickUnit([A, B], { ask: ask("2") }) === B && pickUnit([A, B], { ask: ask(null) }) === null && pickUnit([A, B], { ask: ask("7") }) === null,
     "pickUnit: answered by a number; cancelled or no such entry: none (the page opens nothing, never the first by itself)");
  ok(pickUnit([A], { remembered: "0F0E0D0C", ask: ask(null) }) === null && asked.at(-1).startsWith("The FM-1 with unit ID 0F0E0D0C is not connected. One FM-1 is connected."),
     "pickUnit: the FM-1 opened before left and another is there: asked, not moved to it by itself");
}

/* -------------------------------------------------------------- the page --- */
const html = readFileSync(join(HERE, "manager.html"), "utf8");
const count = (sub) => html.split(sub).length - 1;
ok(count("/*LIB*/") === 1 && count("/*PACKS*/null") === 1 && count('<script type="module">') === 1, "page: /*LIB*/ and /*PACKS*/null once, one module script");
ok(["eng-tabs", "dev-rows", "lib-rows", "cap", "dev-fw", "keep-all", "sel-all", "save-pack", "save-users", "lib-src", "place", "keep", "place-all", "rename", "swap", "delete", "save-file", "backup", "restore", "undo", "kb", "bass"]
  .every((id) => html.includes(`id="${id}"`)), "page: the panes, Place / Keep, the bottom bar, the keyboard");
ok(html.indexOf('id="lib"') < html.indexOf('id="dev"') && html.includes("Place →") && html.includes("← Keep") && html.includes("draggable") && html.includes("shiftKey") && html.includes("metaKey") && html.includes("simObjects(30)"),
  "page: Library left, FM-1 right (Place →, ← Keep), multi-select, drag and drop, the 30-sound simulator");
ok(html.includes("managerPorts(midiAccess, PORT)") && html.includes("const PORT = MANAGER_PORT_RE") && html.includes("pickUnit(units, { remembered: unitId") && html.includes("unit ID ${unitId}"),
   "page: several FM-1s: the pairs of the port name, which one asked by its unit ID, the ID in the header");
ok(html.includes('"ChoralRoot FM-1"') && html.includes("onstatechange") && html.includes('get("sim")') && html.includes('SIM_ARG === "1"') && html.includes('SIM_ARG === "pool"'), "page: the port name, hot-plug, ?sim=1, ?sim=pool");
ok(html.includes('id="replace"') && html.includes('id="restore-factory"') && html.includes("replaceFactory(libSel(), devRows()[to].pos)"), "page: Replace factory, Restore factory order, several dropped onto a factory row");
ok(!/<link\s/i.test(html) && !/src="http/.test(html), "page: no external assets");
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = ["fm1backup.js", "fm1dnsnd.js", "fm1sounds.js", "fm1manager.js"].map((f) => strip(readFileSync(join(HERE, f), "utf8"))).join("\n");
const script = html.slice(html.indexOf('<script type="module">') + 22, html.lastIndexOf("</script>")).replace("/*LIB*/", lib);
let compiled = null; try { new vm.Script(`(async () => {${script}\n})`); compiled = true; } catch (e) { compiled = e; }
ok(compiled === true, `page: the script as make_site.py inlines it compiles (no duplicate names)${compiled === true ? "" : ": " + compiled.message}`);
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
ok(site.includes('"manager.html"') && site.includes("fm1manager.js") && site.includes('"packs"'), "make_site.py: emits manager.html with fm1manager.js and the packs");

/* -------------------- the single-core package's version string (docs/DUALCORE.md "Two packages") --- */
// "ChoralRoot 0.22 1-core": the manager connects (family choralroot), reads and writes; a backup taken on it restores on
// the standard package and back, every object the same (the two packages share every format)
{
  const one = makeSimFM1(simObjects(), { version: "ChoralRoot 0.22 1-core" }), std = makeSimFM1(new Map(), { version: "ChoralRoot 0.22" });
  const info = await deviceInfo(one.request);
  const s1 = new ManagerSession(one.request);
  await s1.read();
  ok(info && info.family === "choralroot" && info.version === "ChoralRoot 0.22 1-core" && sameObjs(simObjects(), s1.objs),
     "1-core: the manager connects to \"ChoralRoot 0.22 1-core\" (family choralroot) and reads it");
  const noFm6 = (a) => ({ ...a, objects: a.objects.filter((o) => o.id !== 8) });   // (the simulator refuses a PUT of object 8)
  const arc = noFm6(readBackup(JSON.stringify(await captureBackup(one.request, info.version))));
  const r = await restoreBackup(std.request, arc);
  const s2 = new ManagerSession(std.request);
  await s2.read();
  const back = makeSimFM1(new Map(), { version: "ChoralRoot 0.22 1-core" });
  await restoreBackup(back.request, noFm6(readBackup(JSON.stringify(await captureBackup(std.request, "ChoralRoot 0.22")))));
  const s3 = new ManagerSession(back.request);
  await s3.read();
  ok(arc.firmware === "ChoralRoot 0.22 1-core" && r.restored.length > 0 && !r.skipped.length && sameObjs(s1.objs, s2.objs) && sameObjs(s1.objs, s3.objs) &&
     backupFileName(info.version).startsWith("choralroot-backup-"),
     `1-core: its backup restores on the standard package (${r.restored.length} objects, none skipped) and back, every object the same`);
}

/* ---------------------- 0.22, the sound pool (128 slots, ids 50..62): Place of .dnsnd items --- */
{
  // a self-made .dnsnd (as web/test_dnsnd.mjs builds them): a stored ZIP, the manifest and an LZ4 literal block
  const le = (n, v) => Array.from({ length: n }, (_, i) => v >>> 8 * i & 255);
  const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ c >>> 1 : c >>> 1; return c >>> 0; });
  const crc32 = (b) => { let c = ~0; for (const x of b) c = crcT[(c ^ x) & 255] ^ c >>> 8; return ~c >>> 0; };
  const zip = (entries) => {
    const parts = [], cd = []; let off = 0;
    for (const [name, data] of entries) {
      const nm = new TextEncoder().encode(name);
      const head = [...le(4, 0x04034b50), 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, data.length), ...le(4, data.length), ...le(2, nm.length), 0, 0];
      parts.push(Uint8Array.from(head), nm, data);
      cd.push(Uint8Array.from([...le(4, 0x02014b50), 20, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, data.length), ...le(4, data.length), ...le(2, nm.length), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, off)]), nm);
      off += head.length + nm.length + data.length;
    }
    const cdLen = cd.reduce((n, p) => n + p.length, 0);
    const all = [...parts, ...cd, Uint8Array.from([...le(4, 0x06054b50), 0, 0, 0, 0, ...le(2, entries.length), ...le(2, entries.length), ...le(4, cdLen), ...le(4, off), 0, 0])];
    const out = new Uint8Array(all.reduce((n, p) => n + p.length, 0)); let p = 0; for (const x of all) { out.set(x, p); p += x.length; }
    return out;
  };
  const firstFree = (objs) => parseSoundObjects(objs).slots.find((x) => !x.used).slot;
  const dnsnd = (name) => {
    const d = new Uint8Array(302); d.set([0xbe, 0xef, 0xba, 0xce, 0, 0, 0, 1], 0);
    for (let i = 0; i < name.length; i++) d[12 + i] = name.charCodeAt(i);
    const lit = [0xF0]; let r = d.length - 15; while (r >= 255) { lit.push(255); r -= 255; } lit.push(r);   // (one LZ4 sequence of literals)
    const hdr = new Uint8Array(0x23); hdr.set([0xac, 0x11, 0xd3, 0x03], 0); hdr.set([0x30, 0x30, 0x39, 0x37], 9); hdr.set(le(2, 302), 0x1c);
    const tr = Uint8Array.from([0, 0, 1, 2, 3, 4, 0, 0, 0, 0x2a, 0xaa, 0xa1, 0xda, 0xaa]);
    const pl = Uint8Array.from([...hdr, ...lit, ...d, ...tr]);
    const man = { FormatVersion: 1, ProductType: ["24", "30"], Payload: "SND", FileType: "Sound", FirmwareVersion: "1.42A", MetaInfo: { Tags: ["test"] } };
    return zip([["manifest.json", new TextEncoder().encode(JSON.stringify(man))], ["SND", pl]]);
  };
  const lib = [];
  for (let k = 0; k < 130; k++) lib.push(...await libraryItemsFromDnsnd(`T${k}.dnsnd`, dnsnd(`DN ${String(k).padStart(3, "0")}`)));
  ok(lib.length === 130 && lib.every((it) => it.type === "sound" && it.pool === 15), "pool: 130 self-made .dnsnd files -> 130 FM TONE library items");
  const sim = makeSimFM1(simObjects(30, true), { version: "ChoralRoot 0.22", ids: SIM_POOL_IDS });
  const s = new ManagerSession(sim.request); await s.read();
  const fresh = async () => { const t = new ManagerSession(sim.request); await t.read(); return t.objs; };
  const slotOf = (objs, n) => parseSoundObjects(objs).slots[n - 1];
  ok(parseSoundObjects(s.objs).pool && usedSlots(s.objs) === 30, "pool: the simulated 0.22 FM-1 reads as the pool (30 of 128 used)");
  // (a) a factory row selected: the sound takes its place, in the first free slot
  const fac = P(s.objs)[15][1], free1 = firstFree(s.objs);
  const a = managerPlace(s.objs, lib[0].file, fac);
  ok(a.changed.length > 0 && a.changed.every((c) => c.id >= 50 && c.id <= 62), "pool: Place over a factory row -> pool bank writes (was: nothing changed)");
  await s.apply(a);
  let rb = await fresh();
  ok(a.where === "over-factory" && a.slot === free1 && P(rb)[15][1].type === "user" && P(rb)[15][1].name === "DN 000" && slotOf(rb, free1).patch === "quad",
     `pool: (a) DN 000 over ${fac.name}: U${free1}, bound, its FM TONE patch read back`);
  // (b) a user slot selected: that slot
  const u = P(s.objs)[15][1], b = managerPlace(s.objs, lib[1].file, u); await s.apply(b);
  rb = await fresh();
  ok(b.where === "over-user" && b.slot === u.slot && slotOf(rb, u.slot).name === "DN 001" && usedSlots(rb) === 31, "pool: (b) onto a user row: its slot overwritten, read back");
  // (c) nothing selected: the next free slot, added
  const free2 = firstFree(s.objs), c = managerPlace(s.objs, lib[2].file, null); await s.apply(c);
  rb = await fresh();
  ok(c.where === "new" && c.slot === free2 && slotOf(rb, free2).name === "DN 002" && slotOf(rb, free2).added && P(rb)[15].at(-1).name === "DN 002",
     "pool: (c) nothing selected: the next free slot, added at the pool's end, read back");
  // Place all: more items than free slots
  const nfree = 128 - usedSlots(s.objs), all = managerPlaceAll(s.objs, lib.slice(3).map((it) => it.file)); await s.apply(all);
  rb = await fresh();
  ok(all.placed === nfree && all.skipped === 127 - nfree && usedSlots(rb) === 128 && slotOf(rb, 128).patch === "quad" && sameObjs(s.objs, rb),
     `pool: Place all 127 into ${nfree} free slots -> ${nfree} placed, ${all.skipped} did not fit, read back the same`);
  let full = null; try { managerPlace(rb, lib[0].file, null); } catch (e) { full = e; }
  ok(full && /All 128 user slots/.test(full.message), "pool: 128 used -> Place refused, \"All 128 user slots\"");
  // Replace factory (DN-3): an FM-1 with its 128 slots free, FM TONE's 16 factory presets replaced in pool order
  {
    let o0 = simObjects(30, true);
    for (const x of parseSoundObjects(o0).slots) if (x.used) o0 = managerDelete(o0, { type: "user", slot: x.slot }).objs;
    const lib200 = [];
    for (let k = 0; k < 200; k++) lib200.push(...await libraryItemsFromDnsnd(`R${k}.dnsnd`, dnsnd(`RP ${String(k).padStart(3, "0")}`)));
    const files = lib200.map((it) => it.file), nf = P(o0)[15].filter((r) => r.type === "factory").length;
    const facNames = P(o0)[15].filter((r) => r.type === "factory").map((r) => r.name);
    ok(usedSlots(o0) === 0 && nf === 16, "replace: the FM-1 has 128 free slots and FM TONE 16 factory presets");
    const fresh2 = async (sim2) => { const t = new ManagerSession(sim2.request); await t.read(); return t.objs; };
    const run2 = async (fs, from = 1) => {
      const sim2 = makeSimFM1(o0, { version: "ChoralRoot 0.22", ids: SIM_POOL_IDS }), t = new ManagerSession(sim2.request); await t.read();
      const res = managerReplaceFactory(t.objs, 15, fs, from); await t.apply(res);
      return { res, rb: await fresh2(sim2), t, sim2 };      // rb: read back by a new session (the unit after a power cycle: the bank)
    };
    const factoryLeft = (o) => P(o)[15].filter((r) => r.type === "factory").length;
    { const { res, rb } = await run2(files.slice(0, 16));
      ok(res.replaced === 16 && !res.added && factoryLeft(rb) === 0 && P(rb)[15].slice(1, 17).every((r, k) => r.name === `RP ${String(k).padStart(3, "0")}` && r.overwrites === facNames[k]),
        "replace: 16 of 16 -> FM TONE 01..16 are the imports in order, no factory preset left in the pool, read back"); }
    { const { res, rb } = await run2(files.slice(0, 5));
      ok(res.replaced === 5 && factoryLeft(rb) === 11 && P(rb)[15][5].name === "RP 004" && P(rb)[15][6].type === "factory" && P(rb)[15][6].name === facNames[5],
        "replace: 5 items -> 01..05 replaced, 06..16 still factory"); }
    { const plan = managerReplacePlan(o0, 15, files, 1), text = replaceFactoryText(plan, 15, 200);
      ok(plan.replace === 16 && plan.extra === 112 && plan.refused === 72 && /16 factory FM TONE sounds will be replaced by the first 16 of 200 selected; 112 more go into free user slots; 72 will not fit/.test(text) && /Restore factory order/.test(text),
        "replace: 200 items -> the confirm says 16 replaced, 112 more, 72 will not fit, and how to restore");
      const { res, rb, t, sim2 } = await run2(files);
      ok(res.replaced === 16 && res.added === 112 && res.refused === 72 && usedSlots(rb) === 128 && P(rb)[15].length === 1 + 16 + 112 && P(rb)[15][17].name === "RP 016" && sameObjs(t.objs, rb),
        "replace: 200 -> 16 replaced, 112 placed after them, 72 refused, read back the same");
      const back = managerRestoreFactory(t.objs, 15); await t.apply(back);
      const rb2 = await fresh2(sim2), rows = P(rb2)[15];
      ok(back.restored === 16 && factoryLeft(rb2) === 16 && rows.slice(1, 17).every((r, k) => r.name === facNames[k]) && usedSlots(rb2) === 128 && rows.length === 1 + 16 + 128 &&
        rows.slice(17).every((r) => r.type === "user" && r.f === -1),
        "replace: Restore factory order -> the 16 factory presets back in 01..16, all 128 imports kept as added sounds, read back"); }
    { const { res, rb } = await run2(files.slice(0, 3), 4);
      ok(res.replaced === 3 && P(rb)[15][3].type === "factory" && P(rb)[15][4].name === "RP 000" && P(rb)[15][6].name === "RP 002" && P(rb)[15][7].type === "factory",
        "replace: dropped onto 04 -> 04..06 replaced, the rows before and after stay factory"); }
  }
  // FM6 / CZ-1 .syx and a P5 sound file in pool mode
  const s2 = new ManagerSession(makeSimFM1(simObjects(6, true), { version: "ChoralRoot 0.22", ids: SIM_POOL_IDS }).request); await s2.read();
  const withFm6 = managerPlace(objs0, packs[12][3].file, null);
  const fm6Syx = libraryItemsFromFile("f.syx", exportSyx(withFm6.objs, withFm6.slot).bytes)[0].file, czSyx = libraryItemsFromFile("c.syx", exportSyx(objs0, 2).bytes)[0].file;
  const p5rec = readSoundFile(fm6Syx).record.slice(); p5rec[2] = 16;
  const p5file = { format: "choralroot-sound", version: 1, name: "MY P5", engine: 16, record: Buffer.from(p5rec).toString("base64"),
    patch: { kind: "p5", data: Buffer.from(Uint8Array.from({ length: 138 }, (_, i) => i & 63)).toString("base64") } };
  const others = [fm6Syx, czSyx, p5file];
  for (const f of others) {
    const r = managerPlace(s2.objs, f, null); await s2.apply(r);
    ok(r.changed.length && slotOf(s2.objs, r.slot).used && slotOf(s2.objs, r.slot).patch, `pool: Place ${readSoundFile(f).engineName} ${readSoundFile(f).name} -> U${r.slot}, with its patch`);
  }
}

/* ---- 3y79: Restore factory presets (selected, an engine's, every engine's), Delete all user sounds, Reset to factory --- */
{
  const POOL = (o) => makeSimFM1(o, { version: "ChoralRoot 0.22", ids: SIM_POOL_IDS });
  const open = async (sim) => { const t = new ManagerSession(sim.request); await t.read(); return t; };
  const putsOf = async (sim, f) => { const k = sim.puts.length; await f(); return sim.puts.slice(k); };
  const same = (a, b) => (a || new Uint8Array(0)).length === (b || new Uint8Array(0)).length && (a || []).every((v, i) => v === b[i]);
  const factoryOrder = (o, e) => P(o)[e].slice(1, 1 + FACTORY_PRESETS[e].length - FACTORY_FIRST[e])
    .every((r, k) => r.type === "factory" && r.name === FACTORY_PRESETS[e][k + FACTORY_FIRST[e]]);
  // the FM-1: the 30-sound pool simulator (U001 FM6 02, U006 WHEEL 01, U009 CZ-1 13 over factory presets), bank 51
  // (U011..U020) as the owner's unit has it: 0.21's FM TONE sounds as migrated, each patch the 80-byte version 3 (header
  // 16 / 80): U011..U013 over FM TONE 01..03, U020 naming FM TONE 01 too (an added sound while U011 holds it), the
  // rest added; bank 53 written once and empty (3840 zeros); U100 a VA sound (bank 59); the favourites: user slots 3, 5,
  // 9 (stale: U009 is over a factory preset), 40, 45 marked, slot 5 typed Chord; FM6 02 FM BELL marked and typed Bass,
  // P5 program 12 marked, the filter on
  let o = simObjects(30, true);
  const q3 = quadToV3(readSoundFile(packs[15][0].file).patch), b51 = new Uint8Array(3840);
  ok(q3.length === 80 && q3[0] === 0x51 && q3[1] === 3, "3y79 setup: an 80-byte version-3 FM TONE patch (0.21's)");
  for (let j = 0; j < 10; j++) {
    const r = withBinding(readSoundFile(packs[15][j].file).record, j < 3 ? j : j === 9 ? 0 : -1);
    b51.set(r, j * 384); b51[j * 384 + 192] = 16; b51[j * 384 + 193] = 80; b51.set(q3, j * 384 + 196);
  }
  o.set(51, b51); o.set(53, new Uint8Array(3840));
  o = new Map([...o, ...placeSoundAt(o, 100, packs[13][1].file)]);
  const FAVS = Uint8Array.from([2, 8, 0x80, 2, 0x14, 0x01, 0x81, 2, 0x80, 0x10, 0xC0, 1, 0x10, 12, 1, 0x02, 16, 2, 0x00, 0x10, 0x2C, 1, 0x02, 0xFF, 1, 1, 0x0F, 1, 0x01]);
  o.set(23, FAVS);
  function placeSoundAt(objs, slot, file) { return placeSound(objs, slot, file, -1).objs; }
  const slots0 = parseSoundObjects(o).slots;
  ok(slots0.slice(10, 20).every((x) => x.used && x.engine === 15 && x.patch === "quad") && slots0[99].used && usedSlots(o) === 31 &&
     P(o)[15].slice(1, 4).every((r, k) => r.slot === 11 + k && r.overwrites === FACTORY_PRESETS[15][k]) && P(o)[15].some((r) => r.slot === 20 && r.f === -1),
     "3y79 setup: U011..U020 0.21 FM TONE sounds (16 / 80), U011..U013 over FM TONE 01..03, U020 added (naming 01 too), U100");

  // the favourites object: the user slots' marks and types, the factory ones kept byte for byte
  ok(favUserSlots(FAVS).join() === "3,5,9,40,45" && favUserSlots(new Uint8Array(0)).length === 0 && favUserSlots(Uint8Array.from([3, 0])).length === 0,
     "favourites: the user slots marked or typed (3, 5, 9, 40, 45); none, another version: none");
  ok(same(favsClearSlots(FAVS, [5, 9]), [2, 7, 0x80, 1, 0x04, 0x81, 2, 0x80, 0x10, 12, 1, 0x02, 16, 2, 0x00, 0x10, 0x2C, 1, 0x02, 0xFF, 1, 1, 0x0F, 1, 0x01]) &&
     same(favsClearSlots(FAVS, null), [2, 5, 12, 1, 0x02, 16, 2, 0x00, 0x10, 0x2C, 1, 0x02, 0xFF, 1, 1, 0x0F, 1, 0x01]) && favsClearSlots(FAVS, [7, 100]) === FAVS,
     "favourites: slots 5 and 9 cleared (5's Chord type too, the section gone), all user slots cleared; factory, P5, the filter kept; nothing to clear: the same object");
  const v1 = Uint8Array.from([1, 2, 0x80, 1, 0x05, 13, 1, 0x01]), cut = Uint8Array.from([2, 2, 0x80, 1, 0x05, 12, 4, 1]);
  ok(same(favsClearSlots(v1, [1]), [1, 2, 0x80, 1, 0x04, 13, 1, 0x01]) && favsClearSlots(cut, null) === cut && favsClearSlots(Uint8Array.from([3, 1, 0x80, 1, 1]), null).length === 5,
     "favourites: a version-1 object kept version 1; a cut-short object or another version left as it is (the FM-1 reads no marks)");

  // the simulator takes a pool bank as the firmware does (3y78's check): 16 / 80 yes, 16 / 81 no
  { const sim = POOL(o), bad = b51.slice(); bad[384 + 193] = 81;
    let rc = 0; try { await writeSounds(sim.request, [{ id: 51, bytes: bad }]); } catch (e) { rc = e.rc; }
    const good = await writeSounds(sim.request, [{ id: 51, bytes: b51 }]);
    ok(rc === 2 && good.join() === "51" && same(sim.objs.get(51), b51), "simulator: a bank with a 16 / 81 slot refused (rc 2), with 0.21's 16 / 80 slots taken (crb_bank_ok)"); }

  // (1) Restore selected: FM6 02 (U001), FM TONE 02 (U012, a 16 / 80 slot), CZ-1 13 (U009), with U005 (added) and FM6 01 (factory)
  { const sim = POOL(o), s = await open(sim), p = P(s.objs);
    const sel = [p[12][1], p[12][2], p[12].find((r) => r.slot === 5), p[15][2], p[14][13]], over = replacedRows(sel);
    const text = restorePresetsText(over, { selected: sel.length }), all = text.paras.join(" ");
    ok(over.length === 3 && text.title === "Restore 3 factory presets?" && text.ok === "Restore 3 presets" &&
       /^FM6 02 FM BELL, FM TONE 02 BELL, CZ-1 13 SYNTH\.BASS come back in their places\./.test(all) &&
       /The 3 user sounds over them are deleted from the FM-1: U001 MY TINE, U012 BELL, U009 CZ BASS 2\./.test(all) &&
       /Restore factory order/.test(all) && /2 of the 5 selected are not over a factory preset/.test(all) && /Undo puts them back/.test(all) && /back up first/.test(all),
       "restore selected: the confirm (what comes back, the 3 user sounds deleted, Restore factory order keeps them, 2 left, Undo, back up)");
    const res = managerRestorePresets(s.objs, sel);
    const puts = await putsOf(sim, () => s.apply(res)), rb = (await open(sim)).objs, after = sim.objs.get(51);
    ok(res.deleted.join() === "1,9,12" && res.restored.length === 3 && puts.map((x) => `${x.id}:${x.size}`).join() === "50:3840,51:3840,23:28",
       `restore selected: banks 50 and 51 each written once (U001 + U009 in one), then the favourites (${puts.map((x) => x.id).join(" ")})`);
    ok(P(rb)[12][2].type === "factory" && P(rb)[12][2].name === "FM BELL" && P(rb)[15][2].type === "factory" && P(rb)[15][2].name === "BELL" &&
       P(rb)[14][13].type === "factory" && P(rb)[14][13].name === "SYNTH.BASS" && P(rb)[12].some((r) => r.slot === 5) && P(rb)[7][1].slot === 6 &&
       usedSlots(rb) === 28, "restore selected: the 3 factory presets back in their places, read back; U005 (added) and WHEEL 01's U006 stay");
    ok(after.subarray(384, 768).every((x) => !x) && [0, 2, 3, 4, 5, 6, 7, 8, 9].every((j) => same(after.subarray(j * 384, (j + 1) * 384), b51.subarray(j * 384, (j + 1) * 384))),
       "restore selected (0.21's FM TONE slots): U012 empty, the other nine slots of bank 51 as read (their 16 / 80 headers and patches)");
    ok(favUserSlots(rb.get(23)).join() === "3,5,40,45" && same(rb.get(23).subarray(-16), FAVS.subarray(-16)),
       "restore selected: U009's stale mark taken off with it; the factory marks, P5's and the filter as they were"); }

  // (2) Restore all of one engine (FM TONE): U011, U013 and U012 deleted, U020 (naming FM TONE 01 too) unbound, not taking its place
  { const sim = POOL(o), s = await open(sim), res = managerRestorePresets(s.objs, P(s.objs)[15]);
    const puts = await putsOf(sim, () => s.apply(res)), rb = (await open(sim)).objs, raw = rawBindings(rb);
    ok(res.deleted.join() === "11,12,13" && puts.map((x) => x.id).join() === "51" && factoryOrder(rb, 15) &&
       P(rb)[15].some((r) => r.slot === 20 && r.f === -1) && raw[19] === -1 && P(rb)[12][2].slot === 1 && P(rb)[14][13].slot === 9,
       "restore all FM TONE: 01..16 factory in order, bank 51 written once, U020 unbound (stays an added sound), FM6 / CZ-1 untouched");
    ok(sim.objs.get(51).subarray(9 * 384 + 192, 9 * 384 + 196).join() === "16,80,0,0" && parseSoundObjects(rb).slots[19].patch === "quad",
       "restore all FM TONE: U020 keeps its 0.21 FM TONE patch (16 / 80)"); }
  { const r = managerRestoreFactory(o, 15);
    ok(r.restored === 3 && factoryOrder(r.objs, 15) && rawBindings(r.objs).slice(10, 20).every((f) => f === -1) && usedSlots(r.objs) === 31,
       "Restore factory order (fixed): U020's binding to FM TONE 01 taken off too, so the factory preset is back, all 31 kept"); }

  // (3) Restore all engines: the 7 replaced presets back (P5 03's too, 3y80), two bank writes
  { const sim = POOL(o), s = await open(sim), rows = replacedOf(s.objs), res = managerRestorePresets(s.objs, rows);
    const puts = await putsOf(sim, () => s.apply(res)), rb = (await open(sim)).objs;
    ok(rows.length === 7 && rows.some((r) => r.engine === 16 && r.pos === 3) && puts.map((x) => x.id).join() === "50,51,23" &&
       MANAGER_ENGINES.every((e) => factoryOrder(rb, e)) && usedSlots(rb) === 24,
       "restore all engines: FM6 02, FM TONE 01..03, CZ-1 13, P5 03, WHEEL 01 back; banks 50, 51 once each; 24 sounds stay as added"); }

  // (4) Delete all user sounds: the banks with a sound (50, 51, 52, 59) each once as a PUT of length 0, bank 53 (empty) and
  // the never-written banks not at all; the favourites without the user slots; one Undo step puts it all back
  { const sim = POOL(o), s = await open(sim), text = deleteAllText(s.objs), all = text.paras.join(" ");
    ok(text.title === "Delete all 31 user sounds?" && text.ok === "Delete 31 sounds" && /31 sounds in the slots U001 to U128, of every engine \(FM6 \d+, /.test(all) &&
       /7 factory presets that they replaced come back/.test(all) && /The FM-1 writes 5 objects for it \(each bank of ten slots once\), its sound pausing about 45 ms at each\./.test(all) && /favourite marks and types of your own sounds \(5\) are taken off/.test(all) &&
       /marks on factory presets stay/.test(all) && /Undo puts them back in one step/.test(all) && /back up first/.test(all) && /CZ-1 \d+, P5 2, WHEEL/.test(all),
       "delete all: the confirm (31 sounds, every engine (P5 2), 5 writes, 7 presets back, 5 marks off, factory marks stay, Undo, back up)");
    const res = managerDeleteAll(s.objs), steps = [];
    const puts = await putsOf(sim, () => s.apply(res, { onWrite: (k, n, id) => steps.push(`${k}/${n}:${id}`) }));
    const rb = (await open(sim)).objs;
    ok(res.deleted === 31 && res.banks === 4 && res.restored.length === 7 && puts.map((x) => `${x.id}:${x.size}`).join() === "50:0,51:0,52:0,59:0,23:18" &&
       steps.join() === "0/5:50,1/5:51,2/5:52,3/5:59,4/5:23", `delete all: 4 banks written once each as length 0, then the favourites; bank 53 and the unwritten ones skipped (${steps.join(" ")})`);
    ok(usedSlots(rb) === 0 && MANAGER_ENGINES.every((e) => factoryOrder(rb, e) && P(rb)[e].length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e] + 1) &&
       same(rb.get(23), favsClearSlots(FAVS, null)) && sim.objs.get(50).length === 3840 && sim.objs.get(50).every((x) => !x) && sim.objs.get(54).length === 0,
       "delete all: read back no user sound, every pool INIT + its factory presets in order; the factory marks and the filter kept; the banks written empty");
    ok(s.history.length === 1, "delete all: one Undo step");
    const undone = await putsOf(sim, () => s.undo());
    ok(POOL_IDS.every((id) => same(sim.objs.get(id), o.get(id))) && same(sim.objs.get(23), FAVS) && undone.map((x) => x.id).join() === "50,51,52,59,23" &&
       same(sim.objs.get(51), b51), "delete all, Undo: the 4 banks and the favourites written back, every bank byte for byte (0.21's 16 / 80 slots too)"); }

  // (5) Reset sounds to factory: the same writes, its own words
  { const sim = POOL(o), s = await open(sim), text = deleteAllText(s.objs, { reset: true }), all = text.paras.join(" ");
    ok(text.title === "Reset the sounds to factory?" && text.ok === "Reset to factory" && /all 31 user sounds are deleted \(U001 to U128, every engine/.test(all) &&
       /every factory preset of every engine is in its place again, in the factory order/.test(all) && /\(5\) are taken off/.test(all) && /back up first/.test(all),
       "reset to factory: the confirm (every user sound deleted, the factory presets in the factory order, the user marks off, back up)");
    await s.apply(managerDeleteAll(s.objs));
    const rb = (await open(sim)).objs;
    ok(usedSlots(rb) === 0 && MANAGER_ENGINES.every((e) => factoryOrder(rb, e)) && favUserSlots(rb.get(23)).length === 0 && same(rb.get(23).subarray(-3), [0x0F, 1, 0x01]),
       "reset to factory: no user sound, every engine's factory presets in their order, no user mark, the factory marks kept");
    ok(!managerDeleteAll(rb).changed.length, "reset to factory again: nothing to write (every bank already empty)"); }

  // (6) a 0.21 FM-1 (no pool, no favourites): every object once, as deleteSound empties a slot
  { const sim = makeSimFM1(simObjects(30)), s = await open(sim), res = managerDeleteAll(s.objs);
    const puts = await putsOf(sim, () => s.apply(res)), rb = (await open(sim)).objs;
    ok(usedSlots(rb) === 0 && new Set(puts.map((x) => x.id)).size === puts.length && puts.every((x) => [6, 7, 9, 10, 11, 12, 13, 22].includes(x.id)) &&
       MANAGER_ENGINES.every((e) => factoryOrder(rb, e)), `0.21: delete all writes each of its objects once (${puts.map((x) => x.id).join(" ")}), no user sound left`); }

  // (7) a write that stops part-way (the loop started after the first bank): an Undo step all the same, Undo puts it back
  { const sim = POOL(o), req = async (r, op) => { const a = await sim.request(r, op); if (r[0] === 67 && r[1][0] === 2 && r[1][1] === 50) sim.playing = true; return a; };
    const s = new ManagerSession(req); await s.read();
    let err = null; try { await s.apply(managerDeleteAll(s.objs)); } catch (e) { err = e; }
    sim.playing = false;
    ok(err && err.rc === 3 && err.written.join() === "50" && s.history.length === 1 && usedSlots(s.objs) === 21, "a write stopped by the loop after bank 50: rc 3, .written [50], an Undo step kept");
    await s.undo();
    ok(POOL_IDS.every((id) => same(sim.objs.get(id), o.get(id))), "  .. Undo puts bank 50 back"); }

  // (8) Delete with several selected: each bank once, the factory presets back, U020 not taking FM TONE 01
  { const p = P(o), res = managerDeleteRows(o, [p[15][1], p[15].find((r) => r.slot === 5) || p[12].find((r) => r.slot === 5), p[12][2]]);
    ok(res.changed.map((c) => c.id).join() === "50,51,23" && res.restored.length === 2 && P(res.objs)[15][1].type === "factory" && P(res.objs)[12][2].type === "factory" &&
       rawBindings(res.objs)[19] === -1 && favUserSlots(res.objs.get(23)).join() === "3,9,40,45",
       "Delete 3 (U011, U005, U001): banks 50, 51 once, FM TONE 01 and FM6 02 back (U020 unbound), U005's mark and type off"); }
}

/* ---- 3y80: the P5 tab (engine 16): its pool, binding over a program, the edits, restore, delete all, the favourites --- */
{
  const POOL = (o) => makeSimFM1(o, { version: "ChoralRoot 0.22", ids: SIM_POOL_IDS });
  const open = async (sim) => { const t = new ManagerSession(sim.request); await t.read(); return t; };
  const same = (a, b) => (a || new Uint8Array(0)).length === (b || new Uint8Array(0)).length && (a || []).every((v, i) => v === b[i]);
  const N = FACTORY_PRESETS[16].length - 1, IDS = FACTORY_IDS[16];
  const facOrder = (o) => P(o)[16].slice(1, 1 + N).every((r, k) => r.type === "factory" && r.name === FACTORY_PRESETS[16][k + 1] && r.f === k + 1);
  // the favourites as the owner's unit has them: every shipped P5 program marked (by Sequential's number, tag 16), the
  // program at P5 03 typed Chord (16 | 0x40); user slots 4 (stale: U004 is over P5 03, its key is the program's) and 7
  // (U007 P5 ADDED) marked; FM6 02 marked; the filter on
  const bits = (ids) => { const b = new Uint8Array(1 + (Math.max(...ids) >> 3)); for (const i of ids) b[i >> 3] |= 1 << (i & 7); return b; };
  const secs = [[0x80, bits([3, 6])], [12, bits([1])], [16, bits(IDS.slice(1))], [16 | 0x40, bits([IDS[3]])], [0xFF, Uint8Array.of(1)]];
  const FAV5 = Uint8Array.from([2, secs.length, ...secs.flatMap(([t, d]) => [t, d.length, ...d])]);
  const p5part = (b) => { const f = favMarks(b); return [f.marks.get(16), f.chord.get(16), f.bass.get(16)].map((s) => [...(s || [])].sort((x, y) => x - y).join()).join("|"); };
  const P5REF = p5part(FAV5);
  const p5bytes = (b) => { const out = []; for (let k = 0, p = 2; b && k < b[1]; k++, p += 2 + b[p + 1]) if ((b[p] & ~0x60) === 16) out.push(...b.subarray(p, p + 2 + b[p + 1])); return out.join(); };
  let o = simObjects(30, true);
  { const f = favMarks(o.get(23)), q = P(o)[16];   // (?sim=pool's own marks: the page shows stars there)
    ok(q.slice(1, 6).map((r) => favOf(f, r)).join() === "both,both,chord,both,both" && !favOf(f, q[6]) && favOf(f, P(o)[12][2]) === "both" &&
       favOf(f, P(o)[13].find((r) => r.slot === 3)) === "both", "simulator (?sim=pool): favourites on P5 01..05 (03 Chord), FM6 02, U003"); }
  o.set(23, FAV5);
  const p5 = P(o)[16];

  ok(MANAGER_ENGINES.includes(16) && managerEngines(o).join() === "12,15,13,14,16,7" && managerEngines(simObjects(30)).join() === "12,15,13,14,7" &&
     managerEngines(null).includes(16), "P5: a tab of its own on a 0.22 FM-1 (after CZ-1), none on 0.21 (no P5 there)");
  ok(p5[0].type === "init" && p5.length === 1 + N + 1 && facOrder(new Map([...o].filter(([id]) => id !== 50))) &&
     p5[1].name === FACTORY_PRESETS[16][1] && p5[N].name === FACTORY_PRESETS[16][N] && p5[2].type === "factory",
     `P5: the pool as the unit turns it: 00 INIT, the ${N} shipped programs (the firmware's names), then the added`);
  ok(p5[3].type === "user" && p5[3].slot === 4 && p5[3].name === "MY PROPHET" && p5[3].overwrites === FACTORY_PRESETS[16][3] && p5[3].kind === "p5" && p5[3].patch &&
     p5.at(-1).slot === 7 && p5.at(-1).name === "P5 ADDED" && p5.at(-1).f === -1 && retiredNote(p5[3].recEngine) === "" && retiredNote(16) === "" &&
     P(o)[13].every((r) => r.recEngine !== 16), "P5: U004 MY PROPHET over P5 03 (bound), U007 added at the end; not \"retired\", not in the VA's tab");
  { const fv = favMarks(FAV5), init = favOf(fv, p5[0]);
    ok(p5.slice(1, 1 + N).filter((r) => r.type === "factory").every((r) => favOf(fv, r) === "both") && favOf(fv, p5[3]) === "chord" &&
       favOf(fv, p5.at(-1)) === "both" && init === null && favKeyOf(p5[3]).tag === 16 && favKeyOf(p5[3]).id === IDS[3] &&
       favKeyOf(p5.at(-1)).tag === 0x80 && favKeyOf(p5.at(-1)).id === 6 && favOf(fv, P(o)[12][2]) === "both" && favOf(fv, P(o)[12][1]) === null &&
       P(o)[13].every((r) => favOf(fv, r) === null), "P5 favourites: every program's star (by its Sequential number), U004 shows its place's (Chord), U007 its slot's");
  }
  // Place: a P5 sound file over a program (bound: "over P5 05 .."), its program byte for byte; the favourites not written
  { const sim = POOL(o), s = await open(sim), file = managerKeep(s.objs, P(s.objs)[16][3]);
    const it = libraryItemsFromFile("p5.json", JSON.stringify(file))[0];
    ok(file.patch.kind === "p5" && file.position === 3 && file.engine === 16 && it.pool === 16 && it.engine === 16 && !retiredNote(it.engine),
       "P5 keep: U004 -> a sound file with its program (patch kind p5), position 3; read back as a library item of the P5 tab");
    const k = sim.puts.length, res = managerPlace(s.objs, file, P(s.objs)[16][5]);
    await s.apply(res);
    const rb = (await open(sim)).objs, r = P(rb)[16][5], slot = parseSoundObjects(rb).slots[res.slot - 1];
    ok(res.where === "over-factory" && r.type === "user" && r.slot === res.slot && r.overwrites === FACTORY_PRESETS[16][5] && r.name === "MY PROPHET" &&
       slot.patch === "p5" && same(exportSound(rb, res.slot).patch && Uint8Array.from(Buffer.from(exportSound(rb, res.slot).patch.data, "base64")), simP5Patch(IDS[3], 1)) &&
       sim.puts.slice(k).map((x) => x.id).join() === String(50 + Math.floor((res.slot - 1) / 10)) && same(rb.get(23), FAV5) && favOf(favMarks(rb.get(23)), r) === "both",
       `P5 place over a program: P5 05 is now MY PROPHET (U${String(res.slot).padStart(3, "0")}), its program as kept; one bank written, the favourites not`);
    const sel = await selectOnUnit(sim.request, r);
    ok(sel.ok && sim.selected.engine === 16 && sim.selected.pos === 5 && midiText(selectRequest(r)[1]) === "00 10 05" &&
       midiText(selectRequest({ engine: 16, pos: 130 })[1]) === "00 10 02 01", "P5 audition: SELECT engine 16, P5 05; a place past 127 with its high bits (pos_hi)");
    // Rename, Swap (drag the added onto a program), Replace factory, then Restore: the P5 marks byte for byte all the way
    await s.apply(managerRename(s.objs, P(s.objs)[16][5], "MINE TOO"));
    await s.apply(managerDrop(s.objs, P(s.objs)[16].find((x) => x.slot === 7), P(s.objs)[16][10]));
    let q = P(s.objs)[16];
    ok(q[5].name === "MINE TOO" && q[10].slot === 7 && q[10].overwrites === FACTORY_PRESETS[16][10] && !q.some((x) => x.f === -1 && x.slot === 7) &&
       p5bytes(s.objs.get(23)) === p5bytes(FAV5), "P5 rename and drag: P5 05 renamed, the added U007 dragged onto P5 10 takes it; the P5 marks as they were");
    const two = [managerKeep(s.objs, q[3]), managerKeep(s.objs, q[5])].map((f) => ({ ...f, position: undefined }));
    const rep = managerReplaceFactory(s.objs, 16, two);
    await s.apply(rep);
    q = P(s.objs)[16];
    ok(rep.replaced === 2 && q[1].type === "user" && q[2].type === "user" && q[1].overwrites === FACTORY_PRESETS[16][1] && q[4].type === "factory",
       "P5 Replace factory: two P5 sounds over P5 01 and 02 (03 and 05, user sounds already, skipped)");
    const over = replacedRows(q), text = restorePresetsText(over);
    ok(over.length === 5 && /P5 01 .*P5 02 .*P5 03 .*P5 05 .*P5 10 /.test(text.paras[0]) && /favourite mark on a factory preset stays/.test(text.paras.join(" ")),
       "P5 Restore all P5: the 5 programs replaced, named in the dialog");
    await s.apply(managerRestorePresets(s.objs, over));
    const rb2 = (await open(sim)).objs;
    ok(facOrder(rb2) && P(rb2)[16].length === 1 + N && p5bytes(rb2.get(23)) === p5bytes(FAV5) && p5part(rb2.get(23)) === P5REF &&
       !favUserSlots(rb2.get(23)).length && P(rb2)[16].slice(1).every((x) => favOf(favMarks(rb2.get(23)), x)),
       "P5 restore: every program back in its place, every star on it; the P5 marks and types byte for byte; U004's and U007's own marks gone with them");
    ok(P(rb2)[12][2].slot === 1 && P(rb2)[14][13].slot === 9, "P5 restore: the other engines' sounds untouched");
  }
  // Restore factory order (keeps U004 as an added sound), Delete of the added one (its slot's mark off), Delete all, Undo
  { const r = managerRestoreFactory(o, 16), q = P(r.objs)[16];
    ok(r.restored === 1 && facOrder(r.objs) && q.slice(-2).map((x) => x.slot).join() === "4,7" && same(r.objs.get(23), FAV5) &&
       favOf(favMarks(FAV5), q[3]) === "chord" && favOf(favMarks(FAV5), q.at(-2)) === "both",
       "P5 Restore factory order: P5 03 back with its Chord star, MY PROPHET kept as an added sound (its slot's own mark, as the FM-1 keys it)"); }
  { const sim = POOL(o), s = await open(sim), row = P(s.objs)[16].at(-1), k = sim.puts.length;
    await s.apply(managerDeleteRows(s.objs, [row]));
    const rb = (await open(sim)).objs;
    ok(!P(rb)[16].some((x) => x.slot === 7) && sim.puts.slice(k).map((x) => x.id).join() === "50,23" && favUserSlots(rb.get(23)).join() === "4" &&
       p5bytes(rb.get(23)) === p5bytes(FAV5), "P5 delete U007: bank 50 and the favourites once; its slot's mark off, the P5 marks byte for byte"); }
  { const sim = POOL(o), s = await open(sim), text = deleteAllText(s.objs), res = managerDeleteAll(s.objs);
    ok(/P5 2/.test(text.paras.join(" ")) && res.restored.some((x) => x.engine === 16 && x.pos === 3), "P5 delete all: the dialog counts the P5 sounds, P5 03 comes back");
    await s.apply(res);
    const rb = (await open(sim)).objs;
    ok(usedSlots(rb) === 0 && facOrder(rb) && p5bytes(rb.get(23)) === p5bytes(FAV5) && p5part(rb.get(23)) === P5REF && favMarks(rb.get(23)).marks.get(16).size === N &&
       !favUserSlots(rb.get(23)).length && favOf(favMarks(rb.get(23)), P(rb)[12][2]) === "both",
       `P5 delete all: no user sound, P5's ${N} programs in their order; the ${N} P5 marks and the Chord type byte for byte, FM6 02's too; the user marks off`);
    await s.undo();
    ok(POOL_IDS.every((id) => same(sim.objs.get(id), o.get(id))) && same(sim.objs.get(23), FAV5), "P5 delete all, Undo: the P5 slots and the favourites back byte for byte"); }
  // Keep / Save of a P5 factory program: none (no pack: Sequential's), said so; the others' factory rows as before
  { let e = null; try { managerKeep(o, p5[1], { packs: fpacks }); } catch (x) { e = x; }
    ok(e && e.message === P5_NO_PACK && /Sequential/.test(P5_NO_PACK) && !keepable(fpacks, p5[1]) && keepable(fpacks, p5[3]) && keepable(fpacks, P(o)[12][1]) &&
       !keepable(fpacks, p5[0]), "P5 keep of a program: refused with the reason (Sequential's, no pack); a P5 user sound and other engines' presets keep"); }
  // a 0.21 FM-1: no P5 there
  { let e = null; try { managerPlace(simObjects(6), managerKeep(o, p5[3]), null); } catch (x) { e = x; }
    const sim = makeSimFM1(simObjects(6)), rc = (await selectOnUnit(sim.request, { engine: 16, pos: 1 })).rc;
    ok(e && /0\.22/.test(e.message) && rc === 1, "P5 on a 0.21 FM-1: a P5 sound refused (0.21 would lose it), SELECT of P5 rc 1"); }
}

/* ---- the page's new parts (3y79) --- */
ok(["restore-sel", "restore-engine", "restore-every", "delete-all", "reset-factory", "dlg", "dlg-backup-go", "dlg-ok", "dlg-cancel"].every((id) => html.includes(`id="${id}"`)) &&
   html.indexOf('id="dev-tools"') < html.indexOf('id="restore-factory"') && html.indexOf('id="restore-factory"') < html.indexOf('id="dev-rows"') &&
   html.includes("askDialog(deleteAllText(session.objs, { reset }))") && html.includes("askDialog(restorePresetsText(over") && html.includes("managerDeleteRows(session.objs, d)") &&
   html.includes("backupEverything()") && html.includes("showModal()"),
   "page: the factory presets' row (Restore selected / all / all engines / factory order), Delete all, Reset to factory, the dialog with the backup");
ok(html.includes("managerEngines(connected() ? session.objs : null)") && html.includes("favMarks(session.objs.get(FAVS_ID))") && html.includes("favOf(favs, r)") &&
   html.includes("keepable(ui.fpacks, r)") && html.includes("P5_NO_PACK") && /16: "#[0-9A-F]{6}"/.test(html) && !html.includes("P5 · retired"),
   "page (3y80): the P5 tab where the FM-1 has P5, the favourites' stars, Keep / Save of a P5 program said why not, P5's colour");

if (failed) { console.log(`${failed} manager test(s) FAILED`); process.exit(1); }
console.log("manager tests passed");
