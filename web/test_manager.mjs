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

import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { captureBackup, loopHeader } from "./fm1backup.js";
import { FACTORY_PRESETS, FACTORY_FIRST, readSoundFile, exportSyx, parseSoundObjects, recordValid } from "./fm1sounds.js";
import {
  MANAGER_ENGINES, managerPools, managerLoops, loopSummary, managerPlace, managerPlaceAll, managerSwap, managerRename, managerDelete,
  managerKeep, keepLoop, readLoopFile, placeLoop, deleteLoop, swapLoops, ManagerSession, makeSimFM1, simObjects, makeLoopRecord,
  libraryItemsFromFile, LibraryStore, loadPackIndex, loadPack, auditionMessages, noteMessage, midiText, writeErrorText, UNDO_DEPTH,
} from "./fm1manager.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(76)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const HERE = new URL(".", import.meta.url).pathname;
const sameObjs = (a, b) => [...a.keys()].every((id) => { const x = a.get(id), y = b.get(id) || new Uint8Array(0); return x.length === y.length && x.every((v, i) => v === y[i]); });
const names = (rows) => rows.map((r) => `${r.pos}:${r.name}:${r.type}${r.slot ? ":U" + r.slot : ""}`);

/* ------------------------------------------------------------- the pools --- */
const objs0 = simObjects();
const pools = managerPools(objs0);
ok(MANAGER_ENGINES.every((e) => pools[e] && pools[e][0].type === "init" && pools[e][0].pos === 0), "pools: every engine's pool starts with 00 INIT");
ok(MANAGER_ENGINES.every((e) => pools[e].filter((r) => r.type !== "init" && r.f >= 0).length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e]),
  "pools: the factory positions of each engine (CZ-1 without INIT TONE)");
const fm6 = pools[12];
ok(fm6[1].type === "factory" && fm6[1].name === "TINE EP" && fm6[2].type === "user" && fm6[2].name === "MY TINE" && fm6[2].overwrites === "FM BELL" &&
   fm6[2].slot === 1, "pools: FM6 02 is U01 MY TINE over FM BELL (overwritten)");
ok(fm6.length === 27 && fm6[26].name === "BELL TOO" && fm6[26].slot === 5 && fm6[26].f === -1, "pools: FM6 26 is the added U05 BELL TOO, after the 25 factory");
ok(pools[14][13].slot === 9 && pools[14][13].overwrites === "SYNTH.BASS" && pools[14].at(-1).name === "CZ GLASS", "pools: CZ-1 13 over SYNTH.BASS, CZ GLASS added at the end");
ok(pools[13].at(-1).name === "WARM VA" && pools[2][1].name === "PHASE BOW" && pools[2][1].overwrites === "BRASS", "pools: VA's added, PHASE 01 overwritten");
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
  const r = managerPools(o)[13].at(-1); return r.slot === 4 && r.name === "LUSH PAD" && r.f === -1;
});
await step("place over factory: FM BASS onto FM6 04 BRASS -> U07 bound to BRASS", managerPlace(s.objs, fmBass.file, managerPools(s.objs)[12][4]), (o) => {
  const r = managerPools(o)[12][4]; return r.type === "user" && r.slot === 7 && r.name === "FM BASS" && r.overwrites === "BRASS";
});
await step("place over user: a CZ-1 sound onto CZ-1 13 (U09) -> U09 replaced, still over SYNTH.BASS", managerPlace(s.objs, czBrass.file, managerPools(s.objs)[14][13]), (o) => {
  const r = managerPools(o)[14][13]; return r.slot === 9 && r.name === "BRASS 1" && r.overwrites === "SYNTH.BASS";
});
await step("place of another engine: a CZ-1 sound with FM6 02 selected -> its own pool, added", managerPlace(s.objs, packs[14][5].file, managerPools(s.objs)[12][2]), (o) => {
  const p = managerPools(o); return p[14].at(-1).name === packs[14][5].name && p[14].at(-1).f === -1 && p[12][2].name === "MY TINE";
});
const keep = managerKeep(s.objs, managerPools(s.objs)[12][2], { firmware: "ChoralRoot 0.15" });
ok(readSoundFile(keep).name === "MY TINE" && keep.binding && keep.binding.overwrites === 2, "keep: FM6 02 -> a choralroot-sound file (binding FM6 02)");
await step("swap two user sounds: FM6 02 (U01) and FM6 26 (U05) exchange places", managerSwap(s.objs, managerPools(s.objs)[12][2], managerPools(s.objs)[12].find((r) => r.slot === 5)), (o) => {
  const p = managerPools(o)[12]; return p[2].name === "BELL TOO" && p[2].overwrites === "FM BELL" && p.at(-1).name === "MY TINE";
});
await step("swap a user sound with a factory preset: the added MY TINE onto FM6 06 MARIMBA", managerSwap(s.objs, managerPools(s.objs)[12].at(-1), managerPools(s.objs)[12][6]), (o) => {
  const p = managerPools(o)[12]; return p[6].name === "MY TINE" && p[6].overwrites === "MARIMBA" && p.length === 26;
});
await step("rename: FM6 06 -> SOFT TINE", managerRename(s.objs, managerPools(s.objs)[12][6], "SOFT TINE"), (o) => managerPools(o)[12][6].name === "SOFT TINE");
await step("delete an overwriting sound: FM6 06 resets to MARIMBA", managerDelete(s.objs, managerPools(s.objs)[12][6]), (o) => {
  const r = managerPools(o)[12][6]; return r.type === "factory" && r.name === "MARIMBA";
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
let busy = null; try { await s.apply(managerRename(s.objs, managerPools(s.objs)[12][2], "BUSY")); } catch (e) { busy = e; }
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
for (let k = 0; k < UNDO_DEPTH + 3; k++) await s2.apply(managerRename(s2.objs, managerPools(s2.objs)[12][2], `N${k}`));
ok(s2.history.length === UNDO_DEPTH, `undo: at most ${UNDO_DEPTH} steps kept`);

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
ok(packIdx.length === MANAGER_ENGINES.length && packIdx.map((p) => p.engine).join() === MANAGER_ENGINES.join(), "packs: the index lists one pack per engine, in the picker's order");
ok(MANAGER_ENGINES.every((e) => packs[e].length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e] &&
   packs[e].every((it, k) => it.name === FACTORY_PRESETS[e][k + FACTORY_FIRST[e]] && it.engine === e && recordValid(readSoundFile(it.file).record))),
  "packs: every pack holds its engine's factory presets, named and valid");
ok([12, 13, 14, 15].every((e) => packs[e].every((it) => it.patch)) && [2, 3, 5, 6, 7, 9, 11].every((e) => packs[e].every((it) => !it.patch)),
  "packs: VA, FM6, CZ-1 and FM TONE carry their patches; the others are records (macros) only");

/* ------------------------------------------------------------ audition --- */
ok(midiText(auditionMessages(fm6[2])[0]) === "C0 02" && midiText(auditionMessages(fm6[2], { bass: true })[0]) === "C1 03",
  "audition: program change of the pool position (bass: channel 2, position + 1)");
ok(midiText(noteMessage(true, 60)) === "90 3C 64" && midiText(noteMessage(false, 60, { bass: true })) === "81 3C 00", "keyboard: note on ch 1 velocity 100, note off ch 2");

/* -------------------------------------------------------------- the page --- */
const html = readFileSync(join(HERE, "manager.html"), "utf8");
const count = (sub) => html.split(sub).length - 1;
ok(count("/*LIB*/") === 1 && count("/*PACKS*/null") === 1 && count('<script type="module">') === 1, "page: /*LIB*/ and /*PACKS*/null once, one module script");
ok(["eng-tabs", "left-rows", "right-rows", "lib-src", "place", "keep", "place-all", "rename", "swap", "delete", "save-file", "backup", "restore", "undo", "kb", "bass"]
  .every((id) => html.includes(`id="${id}"`)), "page: the panes, Place / Keep, the bottom bar, the keyboard");
ok(html.includes('"ChoralRoot FM-1"') && html.includes("onstatechange") && html.includes('get("sim") === "1"'), "page: the port name, hot-plug, ?sim=1");
ok(!/<link\s/i.test(html) && !/src="http/.test(html), "page: no external assets");
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = ["fm1backup.js", "fm1sounds.js", "fm1manager.js"].map((f) => strip(readFileSync(join(HERE, f), "utf8"))).join("\n");
const script = html.slice(html.indexOf('<script type="module">') + 22, html.lastIndexOf("</script>")).replace("/*LIB*/", lib);
let compiled = null; try { new vm.Script(`(async () => {${script}\n})`); compiled = true; } catch (e) { compiled = e; }
ok(compiled === true, `page: the script as make_site.py inlines it compiles (no duplicate names)${compiled === true ? "" : ": " + compiled.message}`);
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
ok(site.includes('"manager.html"') && site.includes("fm1manager.js") && site.includes('"packs"'), "make_site.py: emits manager.html with fm1manager.js and the packs");

if (failed) { console.log(`${failed} manager test(s) FAILED`); process.exit(1); }
console.log("manager tests passed");
