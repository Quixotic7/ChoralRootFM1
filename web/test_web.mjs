// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer, the v5 drum grid in steps and user presets), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf } from "./fm1pkg.js";
import { Updater, pack7, unpack7, classifyFirmware, refusalText, RECOVERY_URL } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, parseWav, resample, normalize, takeSample, autoTrim, zoomView, rootFromName, buildSlot, makeMockDevice, CMD, SMP,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, parseNotes, parseHits, hitsText, gridFromSteps, LANE_NOTE, LANE_OF, readDevicePreferences, devicePresetRows, engineOrder, ENGINE_ORDER, aliasOf, fmtValue, FM6,
   FM4, fromDigital })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.nengines === 14 && info.engines[1] === "-" && info.engines[12] === "FM6" && info.engines[13] === "SLICE"
 && info.engines[5] === "VOICE" && info.engines[6] === "TRIO" && info.engines[7] === "WHEEL" && info.engines[8] === "GRAIN" && info.engines[9] === "PHYS" && info.engines[10] === "DRUM" && info.engines[11] === "NOISE" && info.pcount === 91 && info.pe0 === 83 && info.engines[4] === "SAMPLE",
    "editor: INFO");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0; its descriptors: mockTables) is the
       track's: a factory preset keeps it, as ui.c apply_preset_to does (param_kept) */
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLCR,PAT,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT",
      "editor: SLICER parameters 45..48 over DESC");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 2 && off.p[46] === 7 && off.preset === 1, "editor: a factory preset keeps the SLICER (the track's)");
    await rq(E.req.set(0, 45, 0));
  }
  {
    /* the modulation matrix (core.h P_M1SRC..P_M4AMT = 49..60, just before P_E0 61): SRC / DST / AMT x 4, the
       DST names (20: more than 16) over DESC, every slot OFF after a factory preset (ui.c apply_preset_to) */
    const md = [];
    for (let i = 49; i < 61; i++) md.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(md.map((d) => d.label).join() === "SRC1,DST1,AMT1,SRC2,DST2,AMT2,SRC3,DST3,AMT3,SRC4,DST4,AMT4"
      && md[0].names.join() === "OFF,LFO,ENV,VEL,KEY,RAND,MODW,AT,EXPR" && md[1].names.length === 20
      && md[1].names[11] === "VIB" && md[1].names[19] === "E8" && md[2].min === -64 && md[2].max === 63 && md.every((d) => d.def === 0),
      "editor: matrix parameters 49..60 over DESC (20 DST names)");
    await rq(E.req.set(0, 52, 1));
    await rq(E.req.set(0, 53, 12));
    await rq(E.req.set(0, 54, -30));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 2));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[52] === 1 && on.p[53] === 12 && on.p[54] === -30 && off.p.slice(49, 61).every((v) => v === 0),
      "editor: a factory preset turns every matrix slot off");
  }
  {
    /* the chord keys (core.h P_CHRD, P_VOIC = 81, 82, just before P_E0 83): the track's, a factory preset keeps them */
    const c0 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 81)));
    const c1 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 82)));
    ok(c0.label === "CHRD" && c0.names.join() === "OFF,DIA3,DIA7,MAJ,MIN,DOM7,MAJ7,MIN7,SUS4,POW" && c0.def === 0 &&
       c1.label === "VOIC" && c1.names.join() === "CLOSE,OPEN,INV1,INV2,+OCT" && c1.def === 0,
      "editor: the chord keys 81, 82 over DESC");
    await rq(E.req.set(0, 81, 2));
    await rq(E.req.set(0, 82, 4));
    await rq(E.req.preset(0, 3));
    const kept = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(kept.p[81] === 2 && kept.p[82] === 4 && kept.preset === 3, "editor: a factory preset keeps CHRD and VOIC (the track's)");
    await rq(E.req.set(0, 81, 0));
    await rq(E.req.set(0, 82, 0));
  }
  ok(E.CMD.SONG === 33 && E.CMD.UI_STATE === 34 && E.CMD.FAV_SET === 38, "editor: preferences preserve the existing SONG command");
  const baseInfo = [116,101,115,116,0, 0, 69, 27, 64, 61, 4];
  const untagged = E.parse[E.CMD.INFO]([...baseInfo, 15]);
  ok(!untagged.chainRows && !untagged.uiCaps, "editor: an untagged PR capability cannot masquerade as SONG or preferences");
  const oldSong = E.parse[E.CMD.INFO]([...baseInfo, 16]);
  ok(oldSong.chainRows === 16 && !oldSong.uiCaps, "editor: existing SONG firmware remains compatible without preference probes");
  const tagged = E.parse[E.CMD.INFO]([...baseInfo, 16, 0x55, 1, 11]);
  ok(tagged.chainRows === 16 && tagged.uiCaps === 11, "editor: tagged preference extension advertises its supported controls");
  const pending = E.parse[E.CMD.FAV_SET]([4, 0, 0, 64, 1]);
  ok(pending.rc === 4 && pending.on && pending.engine === 0, "editor: queued favorite response is applied while waiting for STOP");
  const names = [];
  for (let e = 0; e < info.nengines; e++) names.push(E.parse[E.CMD.NAMES](await rq(E.req.names(e))).names);
  let prefs = await E.readDevicePreferences(rq, info, names);
  ok(info.uiCaps === 9 && prefs.palettes.length === 8 && prefs.palettes[0] === "MONO" && prefs.palettes.includes("HI-CON"),
     "editor: preference capabilities (palette, favorites) and palette names");
  const pal = E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 2)));
  ok(pal.rc === 0 && pal.palette === 2, "editor: display preference 0 (palette) round trip");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 8))).rc === 1, "editor: out-of-range palette refused");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(1, 1))).rc === 2, "editor: the retired font weight answers not supported");
  ok(E.parse[E.CMD.FAV_SET](await rq(E.req.favSet(info.nengines, 31, true))).rc === 1, "editor: empty user slot cannot be favorited");
  await rq(E.req.favSet(0, 0, true));
  await rq(E.req.uiSet(3, 1));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).length === 1 && prefs.favorites[0][0], "editor: favorites filter follows device state");
  m.state.favorites[0][0] = false; m.state.favorites[2][0] = true;
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[0][0] && prefs.favorites[2][0], "editor: panel-side favorite changes refresh");
  await rq(E.req.upStore(31, "FAVORITE"));
  await rq(E.req.favSet(info.nengines, 31, true));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).some((r) => r.user && r.preset === 31), "editor: saved user slot appears as favorite");
  await rq(E.req.upStore(31, "RENAMED"));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(prefs.favorites[info.nengines][31] && prefs.slots.slots[31].name === "RENAMED", "editor: overwrite retains star and refreshes name");
  await rq(E.req.upErase(31));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[info.nengines][31] && !E.devicePresetRows(info, names, prefs).some((r) => r.user), "editor: erased slot disappears and loses star");
  {   /* the lists in the device's order (engines.c ENGINE_ORDER): FM6 second, DRUM last, "-" never; the numbers stay */
    const shown = E.engineOrder(info.engines).map((i) => info.engines[i]);
    ok(shown.join() === "ANALOG,FM6,PHASE,LOFI,SAMPLE,VOICE,TRIO,WHEEL,GRAIN,PHYS,NOISE,SLICE,DRUM" &&
       E.engineOrder(info.engines)[1] === 12 && E.engineOrder(info.engines)[12] === 10,
       "editor: engines listed FM6 second, DRUM last (indices kept)");
    ok(E.engineOrder(["ANALOG", "X", "-", "DRUM", "FM6"]).join() === "0,4,3,1", "editor: an unknown engine follows the known ones");
    m.state.favorites[10][0] = m.state.favorites[12][0] = true;
    await rq(E.req.uiSet(3, 0));
    prefs = await E.readDevicePreferences(rq, info, names, prefs);
    const rows = E.devicePresetRows(info, names, prefs).filter((r) => !r.user), eng = [...new Set(rows.map((r) => r.engine))];
    ok(eng[0] === 0 && eng[1] === 12 && eng[eng.length - 1] === 10, "editor: device presets in the device's engine order");
  }
  const none = await E.readDevicePreferences(() => { throw new Error("unexpected request"); }, { uiCaps: 0 }, []);
  ok(none === null, "editor: old firmware receives no unsupported preference requests");
  await rq(E.req.uiSet(3, 0));
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"];
  ok(scale.label === "SCL" && scale.max === 15 && eq(scale.names, scaleNames), "editor: all 16 scale names exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 15)));
  ok(scaleSet.value === 15, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  {
    /* GLO > SYSTEM ROUT (G_ROUTE 14): MIDI IN CH1-4 (0, the default) / SEL (1); the id and G_COUNT unchanged */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 14)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 5)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 0)));
    ok(rd.label === "ROUT" && rd.def === 0 && eq(rd.names, ["CH1-4", "SEL"]) && r1.value === 1 && r0.value === 0 && info.gcount === 27,
      "editor: MIDI IN routing (ROUT CH1-4 / SEL, global id 14)");
  }
  {
    /* FX > REVERB TYPE (G_RTYPE 24, the old drum channel's id): ROOM (0, the default) / SPRING (1); G_COUNT unchanged */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 24)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 10)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 0)));
    const inert = [25, 26].map(async (id) => E.parse[E.CMD.DESC](await rq(E.req.desc(1, id))));
    const [d25, d26] = await Promise.all(inert);
    ok(rd.label === "TYPE" && rd.def === 0 && eq(rd.names, ["ROOM", "SPRING"]) && r1.value === 1 && r0.value === 0 &&
       d25.label === "-" && d26.label === "-" && d25.max === 0 && info.gcount === 27,
      "editor: REVERB TYPE (ROOM / SPRING, global id 24; 25, 26 still inert)");
  }
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

async function editorSamplePresets() {
  const C = E.CMD;
  const { m, rq, done } = attachMock();
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const names = E.parse[C.NAMES](await rq(E.req.names(4)));
  await rq(E.req.preset(4, 0));
  const set = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  ok(eq(names.names, ["PIANO", "PIANO", "FLUTE", "SAX"]) && eq(set.names.slice(0, 4), ["PIANO", "PIANO", "FLUTE", "SAX"])
    && set.names[4] === "PERC" && eq(set.names.slice(5), ["USR1", "USR2", "USR3"]),
    "SAMPLE: TRANH removed, its preset and SET 1 kept as PIANO aliases, indices unchanged");
  ok(E.aliasOf(names.names, 1) === 0 && E.aliasOf(names.names, 2) === 2 && E.aliasOf(set.names, 5) === 5,
    "SAMPLE: an entry named like an earlier one is an alias of it");
  const alias = E.parse[C.PRESET](await rq(E.req.preset(4, 1)));
  const setAlias = E.parse[C.SET](await rq(E.req.set(0, info.pe0, 1)));
  ok(alias.preset === 0 && setAlias.value === 0, "SAMPLE: preset 1 and SET 1 (once TRANH) land on PIANO");
  const removed = E.parse[C.PRESET](await rq(E.req.preset(4, 4)));
  const piano = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(removed.preset === 0 && piano.p[info.pe0] === 0, "SAMPLE: factory preset 4 is outside public browsing");
  await rq(E.req.set(0, info.pe0, 4));
  await rq(E.req.set(0, info.pe0 + 3, 0));          /* an old PERC sound: SET 4, no loop */
  await rq(E.req.stepSet(5, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 1, vel: 99 }));
  m.state.preset = 4;                             /* a project written before the factory preset was removed */
  const sound = [...m.state.p], steps = JSON.stringify(m.state.step);
  await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 });
  await rq(E.req.preset(4, 0));
  await rq(E.req.stepSet(5, emptyStep));
  await rq(E.req.project(0, 2), { timeout: 4000, retries: 0 });
  const loaded = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const tracks = E.parse[C.TRACK](await rq(E.req.track()));
  ok(loaded.engine === 4 && loaded.preset === 0 && tracks.tracks[0].preset === 0
    && eq(loaded.p, sound) && JSON.stringify(m.state.step) === steps,
    "SAMPLE: old PERC project keeps all sound parameters and steps, display index becomes 0");
  done();
}

/* the mock's tables == the firmware's (build/host/desc.json from tests/descdump.c, written by run_tests.sh):
   parameter descriptors, engines (titles, EDIT, presets), factory patterns, power-on sounds */
function mockTables() {
  {
    const m0 = E.makeMockDevice({ auto: false }), ph = m0.tables.ENG[9], dr = m0.tables.ENG[10];
    m0.stop();
    ok(ph.name === "PHYS" && ph.edit[0].names.join() === "MODAL,STRNG,MEMB,SYMP" && ph.edit[0].max === 3 &&
       ph.presets.length === 9 && !ph.presets.some((p) => p.name === "RAIN" || p.name === "DRUM KIT"),
       "editor: PHYS models MODAL STRNG MEMB SYMP (no DUST, no DRUM), 9 presets");
    ok(dr.name === "DRUM" && dr.edit.map((d) => d.label).join() === "KIT,TUNE,TONE,DECY,SNAP,ACC,KICK,DRV" &&
       dr.presets.length === 1 && dr.presets.every((p) => p.pat === 12),
       "editor: DRUM engine 10 (KIT TUNE TONE DECY SNAP ACC KICK DRV), one kit suggesting BEAT");
  }
  const dj = join(HERE, "../build/host/desc.json");
  if (!existsSync(dj)) { console.log("editor: mock tables == firmware (no build/host/desc.json)        skip"); return; }
  const fw = JSON.parse(readFileSync(dj, "utf8"));
  const m = E.makeMockDevice({ auto: false }), T = m.tables;
  m.stop();
  const norm = (d) => ({ label: d.label, fmt: d.fmt, min: d.min, max: d.max, def: d.def, names: d.names || null, unit: d.unit || "" });
  const diffs = [];
  const cmp = (what, a, b) => { if (JSON.stringify(a) !== JSON.stringify(b)) diffs.push(`${what}: mock ${JSON.stringify(a)} != firmware ${JSON.stringify(b)}`); };
  for (const k of ["P_COUNT", "G_COUNT", "NSTEP", "P_E0", "G_ENGSEL", "P_SLCR", "NTRK"]) cmp(k, T[k], fw[k]);
  cmp("TP length", T.TP.length, fw.TP.length);
  fw.TP.forEach((d, i) => cmp(`TP[${i}]`, T.TP[i] && norm(T.TP[i]), d));
  cmp("GP length", T.GP.length, fw.GP.length);
  fw.GP.forEach((d, i) => cmp(`GP[${i}]`, T.GP[i] && norm(T.GP[i]), d));
  cmp("engines", T.ENG.map((e) => e.name), fw.ENG.map((e) => e.name));
  cmp("engine order (ENGINE_ORDER)", E.ENGINE_ORDER.filter((n) => fw.ENG.some((e) => e.name === n)), fw.ORDER);
  fw.ENG.forEach((fe, i) => {
    const me = T.ENG[i];
    if (!me) return;
    cmp(`${fe.name} titles`, me.titles, fe.titles);
    fe.edit.forEach((d, k) => cmp(`${fe.name} edit[${k}]`, norm(me.edit[k]), d));
    cmp(`${fe.name} presets`, me.presets.map((p) => p.name), fe.presets.map((p) => p.name));
    fe.presets.forEach((p, k) => {
      const mp = me.presets[k];
      if (mp) cmp(`${fe.name} ${p.name}`, { e: mp.e, env: mp.env, mono: mp.mono, pat: mp.pat }, { e: p.e, env: p.env, mono: p.mono, pat: p.pat });
    });
  });
  cmp("PATTERNS", T.PATTERNS, fw.PATTERNS);
  const m2 = E.makeMockDevice({ auto: false });   /* the mock's power-on sounds: its parts 1..4, track 4's beat */
  cmp("power-on sounds", m2.state.tracks.map((t) => [t.engine, t.preset]), fw.TRK_DEF.map((x) => x.slice(0, 2)));
  fw.TRK_DEF.forEach((x, k) => {
    if (x[2]) cmp(`power-on pattern of track ${k + 1}`, m2.state.tracks[k].step.slice(0, 16).map((s) => (s.n ? s.notes[0] : 0)),
      fw.PATTERNS[x[2] - 1][0]);
  });
  m2.stop();
  cmp("firmware: G_ENGSEL names == engine names", fw.GP[fw.G_ENGSEL].names, fw.ENG.map((e) => e.name));
  cmp("FM6 patches (init, factory, bank size)", T.FM6, fw.FM6);
  cmp("DRUM grid: the lanes' GM notes", [...E.LANE_NOTE], fw.LANE_NOTE);
  cmp("DRUM grid: the lane of GM 35..81", E.LANE_OF, fw.LANE_OF);
  diffs.slice(0, 20).forEach((d) => console.log("  " + d));
  ok(!diffs.length, `editor: mock tables == firmware (${diffs.length} differences)`);
  /* #31: percent values as the firmware formats them (param_format), SWG 0..100 shows its value */
  const pd = (fw.PCT || []).filter(([sc, i, v, , txt]) => E.fmtValue((sc ? T.GP : T.TP)[i], v)[0] !== txt);   /* [scope, index, value, max, text] */
  pd.slice(0, 5).forEach((x) => console.log("  PCT " + JSON.stringify(x)));
  ok(fw.PCT && fw.PCT.length && !pd.length, `editor: percent values == firmware (${fw.PCT ? fw.PCT.length : 0} values)`);
  const swg = [T.TP, T.GP].flatMap((tb) => tb.filter((d) => d.label === "SWG"));
  ok(swg.length === 3 && swg.every((d) => E.fmtValue(d, 100)[0] === "100" && E.fmtValue(d, 50)[0] === "50") &&
     E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 127)[0] === "100" && E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 64)[0] === "50",
     "editor: SWG 100 shows 100 % (not 79), 0..127 percents unchanged");
}

/* ------------------------------------------------------------------ DIGITAL retired --- */
/* DIGITAL (engine 1, four-operator FM) was replaced by FM6 (src/fm4_convert.c): the editor's conversion == the
   firmware's (build/host/desc.json "FM4": its presets and 48 sounds), engine 1 is "-" with no presets, G_ENGSEL 1 and
   PRESET 1 k load the converted sound (FM6, its own patch), library files of DIGITAL sounds (91, 89 and 69 parameters,
   single-patch files too) import as FM6 with the converted patch, a converted sound put to the device keeps its
   DIGITAL values in an engine 1 slot (UP_LOAD converts it), the device's DIGITAL slot reads back as FM6 */
async function editorFm4() {
  const dj = join(HERE, "../build/host/desc.json");
  if (existsSync(dj)) {
    const fw = JSON.parse(readFileSync(dj, "utf8")).FM4;
    let bad = 0;
    for (const c of fw.cases) {
      const r = E.FM4.convert(c.p, 83);
      if (r.preset !== c.preset || !eq(r.p, c.out) || !eq(Array.from(r.voice), c.voice)) {
        if (bad++ < 3) console.log("  FM4: E", js(c.p.slice(83)), "voice", js(Array.from(r.voice).map((x, i) => (x !== c.voice[i] ? `${i}:${x}/${c.voice[i]}` : "")).filter(Boolean)));
      }
    }
    ok(fw.cases.length === 48 && !bad, `DIGITAL -> FM6: the editor's conversion == the firmware's (${fw.cases.length} sounds, ${bad} differ)`);
    ok(eq(E.FM4.TO_FM6, fw.to_fm6) && fw.presets.length === E.FM4.PRESETS.length && fw.presets.every((p, k) => {
      const q = E.FM4.PRESETS[k];
      return q.name === p.name && eq(q.e, p.e) && eq(q.env, p.env) && q.fenv === p.fenv && q.mono === p.mono &&
        eq(q.fx, p.fx.map((x) => x - 1)) && q.pat === p.pat;
    }), "DIGITAL -> FM6: its presets and the FM6 presets covering them == the firmware's");
  } else {
    console.log("DIGITAL -> FM6: == the firmware's (no build/host/desc.json)        skip");
  }
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const n1 = E.parse[C.NAMES](await rq(E.req.names(1)));
  ok(info.engines[1] === "-" && !n1.names.length && n1.titles.join() === "-,-", "DIGITAL retired: engine 1 reserved (\"-\", no presets)");
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  const fm6Of = async () => E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0))).packed;
  /* the DIGITAL values of preset k on top of the track's: what the device converts */
  const digital = (k, base) => E.FM4.presetValues(base.map((v, i) => (i < info.pe0 && ![0, 39, 40].includes(i) && !(i >= 17 && i <= 32) &&
    !(i >= 45 && i <= 48) && i !== 81 && i !== 82 ? pdesc[i].def : v)), k, info.pe0);
  let d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(1, 20, 1));
  let d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  let want = E.FM4.convert(digital(0, d0.p), info.pe0);
  ok(d.engine === 12 && d.preset === 0 && eq(d.p, want.p) && eq(await fm6Of(), E.FM6.pack(want.voice)),
    "DIGITAL retired: SET G_ENGSEL 1 -> FM6 with E.PIANO converted (its own patch, PTCH TINE EP)");
  d0 = d;
  await rq(E.req.preset(1, 5));
  d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  want = E.FM4.convert(digital(5, d0.p), info.pe0);
  ok(d.engine === 12 && d.preset === 4 && eq(d.p, want.p) && E.FM6.name(E.FM6.unpack(await fm6Of())) === "PAD",
    "DIGITAL retired: PRESET 1 5 (its PAD) -> FM6, the converted patch named PAD, PTCH / preset FM6 PAD");
  /* library files of DIGITAL sounds: today's 91 parameters, 89 (P_E0 81), 69 (P_E0 61, no OP ENV) */
  const base = Array.from({ length: 91 }, (_, i) => (i < 83 ? pdesc[i].def : 0));
  const pad = E.FM4.presetValues(base.slice(), 5, 83);
  pad[61] = 20; pad[64] = 100;                      /* op 1 ATK, LVL: an OP ENV edit */
  const keys89 = [...keys.slice(0, 81), ...keys.slice(83)], keys69 = [...keys.slice(0, 61), ...keys.slice(83)];
  const p89 = [...pad.slice(0, 81), ...pad.slice(83)], p69 = [...pad.slice(0, 61), ...pad.slice(83)];
  const engines = info.engines.map((n, i) => (i === 1 ? "DIGITAL" : n));   /* (the files' firmware had DIGITAL) */
  const file = (pc, labels, p) => ({ format: "felucca-library", version: 1, kind: "library", pCount: pc, pE0: pc - 8, paramLabels: labels,
    engines, patches: [{ name: "OLD PAD", engine: 1, engineName: "DIGITAL", params: p, pattern: null, tags: [] }] });
  const ctx = { keys, engines: info.engines, pe0: info.pe0 };
  const r91 = E.readLibraryFile(file(91, keys, pad), ctx).patches[0];
  const r89 = E.readLibraryFile(file(89, keys89, p89), ctx).patches[0];
  const r69 = E.readLibraryFile(file(69, keys69, p69), ctx).patches[0];
  const one = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 1, engineName: "DIGITAL", p: p89 }, ctx).patches[0];
  const conv = (p) => E.FM4.convert(p, 83);
  const flat = pad.slice(); for (let i = 61; i <= 80; i++) flat[i] = i % 5 === 3 || i % 5 === 0 ? 127 : 0;   /* (61: ATK) */
  const isFm6 = (r, p) => r && r.engine === 12 && r.engineName === "FM6" && eq(r.fm6, E.FM6.pack(conv(p).voice)) &&
    eq(r.p.map((v) => v ?? 0), conv(p).p.map((v) => v ?? 0));
  ok(isFm6(r91, pad), "library file: a DIGITAL patch (91 parameters) imports as FM6 with the converted patch");
  ok(isFm6(r89, pad.map((v, i) => (i === 81 || i === 82 ? null : v))) && isFm6(one, pad.map((v, i) => (i === 81 || i === 82 ? null : v))),
    "library file: .. of 89 parameters (labelled, and a single-patch file): the OP ENV edit carried");
  ok(isFm6(r69, flat.map((v, i) => (i >= 61 && i <= 82 ? null : v))), "library file: .. of 69 parameters (no OP ENV then: the defaults)");
  const back = E.readLibraryFile(JSON.parse(js(E.libraryFile("library", [r91], ctx))), ctx).patches[0];
  ok(eq(back.fm6, r91.fm6) && eq(back.fm4, r91.fm4) && back.engineName === "FM6", "library file: the converted patch and its DIGITAL values round trip");
  /* a library stored on DIGITAL firmware, then a device without it: libAdopt converts its DIGITAL sounds (the layout
     changed or not), the others keep their engine */
  {
    const adopt = html.slice(html.indexOf("async function libAdopt(d)"), html.indexOf("const engineOk"));
    const L = vm.runInNewContext(proto + `
      let lib = [], libMeta = {}; const written = [];
      const store = { setMeta: async () => {} }, renderLib = () => {};
      async function libWrite(a) { written.push(...a); }
      ${adopt}
      ;({ libAdopt, set: (l, m) => { lib = l; libMeta = m; written.length = 0; }, lib: () => lib, written: () => written })`,
    { setTimeout, clearTimeout, setInterval, clearInterval, console });
    const dv = { keys, info: { engines: info.engines, pe0: info.pe0 } };
    const entry = () => [{ id: "a", name: "OLD PAD", engine: 1, engineName: "DIGITAL", p: pad.slice(), pattern: null, tags: ["x"],
      created: "2026-01-01T00:00:00.000Z", modified: "2026-01-01T00:00:00.000Z" },
      { id: "b", name: "BASS", engine: 0, engineName: info.engines[0], p: base.slice(), pattern: null, tags: [] }];
    L.set(entry(), { keys, engines });
    await L.libAdopt(dv);
    const [a1, b1] = L.lib();
    ok(isFm6(a1, pad) && a1.id === "a" && eq(a1.fm4, pad) && a1.tags.join() === "x" && b1.engine === 0 &&
      L.written().includes(a1), "library: libAdopt on a device without DIGITAL converts its DIGITAL sounds to FM6");
    L.set(entry(), { keys, engines: info.engines });       /* (adopted before: the layout is the device's already) */
    await L.libAdopt(dv);
    ok(isFm6(L.lib()[0], pad) && L.written().length === 1, "library: .. also when the layout is unchanged (only those written)");
  }
  /* put to the device: its DIGITAL values as engine 1 (the device converts them on load); read back as FM6 */
  let rc = await E.bank.put(rq, 9, { ...r91, engine: 1, p: r91.fm4 });
  const u = await E.bank.get(rq, info, 9);
  await rq(E.req.upLoad(9));
  d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && u.engine === 1 && eq(u.p, pad) && d.engine === 12 && eq(await fm6Of(), r91.fm6),
    "device slot of a DIGITAL sound (engine 1, its values kept): UP_LOAD plays the converted FM6 patch");
  done();
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 0 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 0 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, an empty sequencer, then UP_LOAD brings the sound back, not the stored pattern */
  await rq(E.req.preset(2, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  await rq(E.req.set(0, 17, 3));                 /* the track's own: ARP MODE */
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 0 && eq(d2.p.filter((_, i) => i !== 17), cap.p.filter((_, i) => i !== 17)) && d2.p[17] === 3
    && st.every((x) => !x.n) && cap.pattern, "librarian: UP_LOAD applies the sound only (steps and the track's ARP stay)");
  await rq(E.req.set(0, 17, 0));
  for (let i = 0; i < 16; i++) await rq(E.req.stepSet(i, E.stepsFromPattern(cap.pattern)[i]));   /* (for the captures below) */

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a PHASE patch that holds a pattern; the sound only, the sequence (with notes) stays */
  const bass = await E.bank.get(rq, info, 2);   /* PHASE RESO, with the ACID pattern */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await rq(E.req.set(0, 29, 24));               /* the track's own: LEN 24, ARP MODE 2 */
  await rq(E.req.set(0, 17, 2));
  await rq(E.req.set(0, 81, 1));                /* CHRD DIA3: the track's too */
  const seqBefore = [];
  for (let i = 0; i < 24; i++) seqBefore.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const flashBefore = js(m.state.bank);
  await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 24; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const own = new Set([0, 39, 40, ...Array.from({ length: 16 }, (_, i) => 17 + i), 45, 46, 47, 48, 81, 82]);
  ok(d3.engine === bass.engine && eq(d3.p.filter((_, i) => !own.has(i)), bass.p.filter((_, i) => !own.has(i)))
    && d3.p[29] === 24 && d3.p[81] === 1 && d3.p[17] === 2 && js(st3) === js(seqBefore) && st3[20].notes[0] === 50 && js(m.state.bank) === flashBefore,
    "librarian: audition = G_ENGSEL + SETs of the sound; steps, LEN and ARP stay; no flash write");
  /* the next audition: the same rule */
  const bell = await E.bank.get(rq, info, 1);
  await E.auditionPatch(rq, info, bell, { gEng: 20 });
  const s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(d4.engine === bell.engine && bell.engine === 12 && s20.notes[0] === 50 && d4.p[29] === 24 && eq(d4.p.filter((_, i) => !own.has(i)), bell.p.filter((_, i) => !own.has(i))),
    "librarian: a second audition keeps the sequence too");
  await rq(E.req.set(0, 29, 16));
  await rq(E.req.set(0, 17, 0));
  await rq(E.req.set(0, 81, 0));

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === 91 && file.paramLabels.length === 91 && file.paramLabels[81] === "CHRD" && file.paramLabels[82] === "VOIC" && file.engines.length === 14,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "PHASE",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["PHASE", "ANALOG", "SAMPLE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === 92 && p0[5] === null && p0[6] === cap.p[5] && p0[91] === cap.p[90]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "ANALOG", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  {   /* files from the 69-parameter firmware (P_E0 61): the engine's 8 land on E0..E7 (83..90), FM op ENV and the
         chord keys stay unset */
    const p69 = [...cap.p.slice(0, 61), ...cap.p.slice(83, 91)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p69 }, ctx).patches[0].p;
    const keys69 = [...keys.slice(0, 61), ...keys.slice(83)];
    const lp = E.readLibraryFile({ ...file, pCount: 69, paramLabels: keys69, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const good = (q) => q.length === 91 && eq(q.slice(0, 61), cap.p.slice(0, 61)) && eq(q.slice(83), cap.p.slice(83)) && q.slice(61, 83).every((v) => v === null);
    ok(good(sp) && good(lp) && good(nk), "library file: 69-parameter files (patch, labelled, unlabelled) map the engine's 8 to 83..90");
  }
  {   /* files from the 89-parameter firmware (P_E0 81, before the chord keys): the engine's 8 land on 83..90, the FM
         op ENV in place, CHRD VOIC unset (left as the track has them) */
    const p89 = [...cap.p.slice(0, 81), ...cap.p.slice(83, 91)];
    const keys89 = [...keys.slice(0, 81), ...keys.slice(83)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p89 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 89, pE0: 81, paramLabels: keys89, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const good = (q) => q.length === 91 && eq(q.slice(0, 81), cap.p.slice(0, 81)) && q[81] === null && q[82] === null && eq(q.slice(83), cap.p.slice(83));
    ok(keys89[81] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 89-parameter files (patch, labelled, unlabelled) map the engine's 8 to 83..90, the chord keys unset");
  }
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 0, preset: 4, engineName: "ANALOG", presetName: "ACID", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

/* v5: the DRUM grid. Steps carry lane hits and accents (3 bytes after vel); a user preset of a DRUM track holds a
   16-step grid (UP_GET / UP_PUT kind 1); the steps page writes them as lane names */
async function editorGrid() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const h = E.parseHits("kick >snare HATCL cym");
  ok(h && h.hit === (1 | 2 | 8 | 128) && h.acc === 2 && E.hitsText(h.hit, h.acc) === "KICK >SNARE HATCL BELL" &&
     E.parseHits("KICK FOO") === null && E.parseHits(" ").hit === 0, "grid: hits as text (lanes, > accent, CYM / CONGA aliases)");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [41, 0, 0, 0], time: 0, flags: 0, vel: 100, hit: 0x81, acc: 0x80 })));
  ok(s.hit === 0x81 && s.acc === 0x80 && s.notes[0] === 41, "grid: STEP_SET / STEP_GET carry the hits (lane 8 too)");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 0, vel: 100 })));
  ok(s.hit === 0x81 && s.notes[0] === 42, "grid: a STEP_SET without hits (an older editor) keeps them");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 4, { n: 0, notes: [0, 0, 0, 0], time: 0, flags: 0, vel: 0, hit: 0x12, acc: 0x02 })));
  ok(w.hit === 0x12 && w.acc === 0x02 && E.parse[C.TRACK_STEP]([2, 4, 0, 0, 0, 0, 0, 0, 0, 0]).hit === 0,
    "grid: TRACK_STEP carries the hits (an older firmware's reply: none)");
  await rq(E.req.preset(10, 0));                   /* the selected track a DRUM kit, its 16 steps a grid */
  for (let i = 0; i < 16; i++) {
    await rq(E.req.stepSet(i, { n: i === 2 ? 1 : 0, notes: [i === 2 ? 49 : 0, 0, 0, 0], time: i % 4 === 0 || i === 2 ? 0 : 2, flags: 0,
      vel: 0, hit: i % 4 === 0 ? 1 | (i === 0 ? 128 : 0) : 0, acc: i === 0 ? 128 : 0 }));
  }
  const cap = await E.capturePatch(rq, info, "MY BEAT");
  ok(cap.patch.grid && !cap.patch.pattern && cap.patch.grid[0][0] === 0x81 && cap.patch.grid[0][1] === 0x80 &&
     cap.patch.grid[2][0] === 0x80 && cap.patch.grid[1][0] === 0 && cap.patch.grid[4][0] === 1,
     "grid: capture of a DRUM track = its grid (a crash 49 on BELL)");
  const rc = await E.bank.put(rq, 14, cap.patch);
  const g = await E.bank.get(rq, info, 14);
  ok(rc === 0 && g.grid && !g.pattern && JSON.stringify(g.grid) === JSON.stringify(cap.patch.grid), "grid: UP_PUT kind 1 -> UP_GET round trip");
  const put = E.req.upPut(14, cap.patch), saved = JSON.stringify(m.state.bank[14]);
  const base = put[1].length - 17;
  for (const bytes of [put[1].slice(0, -1), [...put[1], 0], [...put[1].slice(0, base), 2, ...Array(16).fill(0)]]) {
    const rejected = E.parse[C.UP_PUT](await rq([C.UP_PUT, bytes]));
    ok(rejected.rc === 1 && JSON.stringify(m.state.bank[14]) === saved,
       "grid: malformed preset extension rejected without changing saved data");
  }
  const ctx = { keys: null, engines: info.engines };
  const back = E.readLibraryFile(JSON.parse(JSON.stringify(E.libraryFile("bank", [{ ...g, slot: 14 }], ctx))), ctx);
  ok(JSON.stringify(back.patches[0].grid) === JSON.stringify(g.grid) && !back.patches[0].pattern, "grid: a library file keeps the grid");
  await rq(E.req.upStore(15, "STORED"));
  const g2 = await E.bank.get(rq, info, 15);
  ok(g2.grid && g2.grid[0][0] === 0x81 && g2.grid[0][1] === 0x80, "grid: UP_STORE of a DRUM track stores its grid");
  const p3 = await E.bank.get(rq, info, 2);
  ok(p3.pattern && p3.grid === null, "grid: a note pattern stays a pattern (kind 0)");
  done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 91 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PRESET too, nothing after its STEP_SET");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

async function preferenceReplies() {
  const C = E.CMD, unknown = [];
  const link = new E.Link(() => {}, { onUnknown: f => unknown.push(f) });
  for (const [request, stale, reply] of [
    [E.req.uiSet(0, 4), [0, 1, 1], [0, 0, 4]],
    [E.req.uiSet(0, 4), [0, 0, 3], [0, 0, 4]],
    [E.req.favGet(0, 2, 1), [0, 0, 3, 64, 1, 1], [0, 0, 2, 64, 1, 1]],
    [E.req.favSet(0, 2, true), [4, 0, 2, 64, 0], [4, 0, 2, 64, 1]],
  ]) {
    const pending = link.request(request);
    link.receive(E.frame(request[0], stale));
    ok(link.cur !== null, 'preferences: a reply for another setting or favorite cannot finish the request');
    link.receive(E.frame(request[0], reply));
    ok(eq(await pending, reply), 'preferences: the matching reply completes the request');
  }
  ok(unknown.length === 4 && link.idle, 'preferences: stale replies are routed away without leaving requests busy');
  link.close();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === 0 && tr.tracks[1].engine === 12
    && tr.tracks[3].engine === 10 && tr.tracks[3].preset === 0, "tracks: INFO NTRK, TRACK lists 4 parts (track 4: DRUM KIT)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === 12 && d0.engine === 0 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute, track 4 as any other */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.tracks[3].p[0] === 64 && tr2.tracks[3].level === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (track 4: its own LEVEL)");
  /* track 4 has a sound: stored and loaded as any other */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  const dl = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(dd.engine === 10 && us.rc === 0 && ul.rc === 0 && dl.engine === 12, "tracks: track 4 selected -> DUMP engine DRUM, UP_STORE / UP_LOAD rc 0");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === 10
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of tracks that are not selected (clamped) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.tracks[3].p[0] === 127 && m1.tracks[3].level === 127
    && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 0 && chd.id === 0 && chd.value === 90, "mixer: track 4's level on the device pushes its LEVEL (P_LEVEL)");
  /* track 4's demo beat: the DRUM kit's lane hits and accents */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  ok(st.every((s) => s.n === 0) && st[0].hit === 1 && st[0].acc === 1 && st[1].hit === 8
    && st[4].hit === 2 && st[4].acc === 2 && st[14].hit === 16,
    "mixer: the mock's track 4 holds the BEAT grid (KICK CHH ... SNARE ... OHH)");
  ok(JSON.stringify(E.parseNotes("C4 E4 G4")) === "[60,64,67]" && JSON.stringify(E.parseNotes("36 38")) === "[36,38]" && E.parseNotes("KICK") === null,
    "mixer: notes parse as numbers or note names (no GM drum names)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is FM6 PAD: ALG 0..32 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 32 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------------------------ FM6 patches --- */
/* the 6-operator patch formats (FM6 in editor.html == eng_fm6.c / fm6_test.c), the SysEx files, the protocol
   (cmds 68..71) against the mock, PTCH, the macros at the pe0 INFO gives */
async function editorFm6() {
  const F6 = E.FM6;
  ok(F6.FACTORY_PK.every((pk) => eq(F6.pack(F6.unpack(pk)), pk)) && eq(F6.pack(F6.unpack(F6.INIT_PK)), F6.INIT_PK),
    "FM6: pack(unpack(x)) == x for the factory patches and the init voice");
  let inrange = true, seed = 7;
  for (let k = 0; k < 500; k++) {
    const pk = Array.from({ length: 128 }, () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16 & 255);
    const v = F6.unpack(pk);
    for (let i = 0; i < F6.SIZE; i++) inrange &&= i >= F6.NAME ? v[i] >= 32 && v[i] <= 126 : v[i] <= F6.max(i);
  }
  ok(inrange, "FM6: any 128 bytes unpack into range");
  const voices = F6.FACTORY_PK.map((pk) => F6.unpack(pk));
  const bank = F6.bankSysex(voices), one = F6.singleSysex(voices[3]);
  ok(bank.length === 4104 && bank[0] === 0xF0 && bank[3] === 9 && bank[4103] === 0xF7 && one.length === 163 && one[5] === 0x1B,
    "FM6: a 32-voice bank SysEx is 4104 bytes, a single voice 163");
  let r = F6.parseSysex(bank);
  ok(r.voices.length === 32 && !r.badSum && r.voices.slice(0, 8).every((x, k) => eq(F6.pack(x.v), F6.FACTORY_PK[k])) &&
     r.voices[8].name === "INIT VOICE" && r.voices[1].name === "GLASS BELL", "FM6: bank SysEx round trip (names, checksum)");
  r = F6.parseSysex(Uint8Array.from([...one, ...one]));
  ok(r.voices.length === 2 && eq(F6.pack(r.voices[1].v), F6.FACTORY_PK[3]), "FM6: two single-voice messages in one file");
  const bad = Uint8Array.from(one); bad[161] ^= 1;
  ok(F6.parseSysex(bad).badSum === 1, "FM6: a wrong checksum is reported (the voice still read)");
  ok(F6.parseSysex(bank.subarray(6, 4102)).voices.length === 32 && F6.parseSysex(one.subarray(6, 161)).voices.length === 1,
    "FM6: raw 4096 / 155-byte files read too");
  ok(F6.carriers(0).join() === "1,3" && F6.carriers(31).length === 6 && F6.feedbackOp(0) === 6 && F6.feedbackOp(1) === 2,
    "FM6: carriers and the feedback operator of an algorithm");

  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 200 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (x) => link.request(x), C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.fm6 && info.fm6.factory === 8 && info.fm6.bank === 27, "FM6: INFO tag (8 factory, 27 bank slots)");
  let list = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(list.slots.length === 35 && list.slots[0].name === "TINE EP" && !list.slots[8].used, "FM6: LIST names the factory patches, the bank empty");
  const mine = F6.setName(F6.factory(2), "my bass");
  let p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(1, 4, F6.pack(mine))));
  list = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(!p.rc && list.slots[12].used && list.slots[12].name === "MY BASS", "FM6: PUT into bank B5, listed by name");
  let g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(1, 4)));
  ok(!g.rc && eq(g.packed, F6.pack(mine)), "FM6: GET bank B5 as stored");
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(1, 5)));
  ok(g.rc === 2 && !g.packed, "FM6: GET of an empty slot: rc 2");
  /* the selected track to FM6, PTCH B5 (at the pe0 INFO gives): the track plays that patch */
  const eng = info.engines.indexOf("FM6");
  await rq(E.req.set(1, 20, eng));
  await rq(E.req.set(0, info.pe0 + 7, 8 + 4));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  ok(!g.rc && F6.name(F6.unpack(g.packed)) === "MY BASS", `FM6: PTCH (P_E0 + 7 = ${info.pe0 + 7}) B5 loads the bank patch into the track`);
  const edited = F6.unpack(g.packed); edited[F6.VI.ALG] = 31;
  p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(0, 0, F6.pack(edited))));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  ok(!p.rc && F6.unpack(g.packed)[F6.VI.ALG] === 31, "FM6: PUT to the track: its own patch changed");
  const e = E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4)));
  ok(!e.rc && !E.parse[C.FM6_LIST](await rq(E.req.fm6List())).slots[12].used, "FM6: ERASE empties B5");
  p = E.parse[C.FM6_PUT](await rq([C.FM6_PUT, [0, 9, 1, 2, 3]]));
  ok(p.rc === 1, "FM6: a short record or a fifth track: rc 1");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, noFm6: true });
  const l2 = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (ev) => l2.receive(ev.data);
  ok(E.parse[C.INFO](await l2.request(E.req.info())).fm6 === null, "FM6: firmware without it: no tag, no FM6 tab");
  l2.close(); old.stop();
}

/* ------------------------------------------------- editor tabs and strings --- */
async function editorSong() {
  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 100 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (r) => link.request(r);
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.chainRows === 16, "SONG: INFO capability appended");
  const ask = async (op = 0, rows = []) => E.parse[C.SONG](await rq(E.req.song(op, rows)));
  const rows = [{ slot: 0, repeat: 4 }, { slot: 1, repeat: 2 }, { slot: 0, repeat: 1 }];
  let r = await ask(1, rows);
  ok(r.rc === 0 && JSON.stringify(r.rows) === JSON.stringify(rows), "SONG: rows and repeats round trip");
  r = await ask(2);
  ok(r.rc === 4 && !r.playing, "SONG: missing source rejected before PLAY");
  await rq(E.req.project(1, 1));
  r = await ask(2);
  ok(!r.rc && r.playing && r.row === 0 && r.remaining === 4, "SONG: PLAY starts from row 1");
  r = await ask(1, [{ slot: 0, repeat: 1 }]);
  ok(r.rc === 2 && r.count === 3, "SONG: editing during PLAY refused, rows kept");
  await ask(3);
  r = E.parse[C.SONG](await rq([C.SONG, [1, 1, 0, 0]]));
  ok(r.rc === 1 && r.count === 3, "SONG: zero repeats rejected without changing rows");
  r = E.parse[C.SONG](await rq([C.SONG, [1, 2, 0, 1]]));
  ok(r.rc === 1 && r.count === 3, "SONG: short row list rejected");
  await rq(E.req.project(1, 2));
  await ask(1, []);
  await rq(E.req.project(0, 2));
  r = await ask();
  ok(JSON.stringify(r.rows) === JSON.stringify(rows) && !r.playing, "SONG: project save/load recalls the chain");
  await ask(1, []);
  r = await ask(2);
  ok(r.rc === 1 && !r.playing, "SONG: empty chain does not PLAY");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, v5: true });
  const l = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (e) => l.receive(e.data);
  ok(E.parse[C.INFO](await l.request(E.req.info())).chainRows === 0, "SONG: older firmware has no capability");
  l.close(); old.stop();
}

/* Run the page's actual session helpers and connect-load path with delayed replies. */
async function editorSessions() {
  const state = html.slice(html.indexOf("let busy = 0;"), html.indexOf("let dragging", html.indexOf("let busy = 0;")));
  const requests = html.slice(html.indexOf("function assertSession(d)"), html.indexOf("/* the whole connect sequence */"));
  const loading = html.slice(html.indexOf("async function load(d)"), html.indexOf("async function getDescOrNull"));
  const library = html.slice(html.indexOf("async function libOp(fn)"), html.indexOf("const checkRc", html.indexOf("async function libOp(fn)")));
  let shown = 0, adopted, adoptStarted;
  const nodes = new Map();
  const noop = () => {};
  const S = vm.runInNewContext(`let dev = null, lastDump = 0, libBusy = false; ${state} ${requests} ${loading} ${library}
    ;({ beginBusy, resetBusy, sessionRequest, load, libOp,
       setDevice: d => { dev = d; }, count: () => busy })`, {
    Date, Error,
    $: id => { if (!nodes.has(id)) nodes.set(id, {}); return nodes.get(id); },
    CMD: { INFO: 1, PROJECT: 2, SMP_INFO: 3, DUMP: 4 },
    parse: { 1: r => r, 2: () => ({ used: false }), 3: () => ({}), 4: r => r },
    req: { info: () => "info", project: () => "project", smpInfo: () => "sample", dump: () => "dump" },
    paramKeys: noop, libAdopt: () => { adoptStarted?.(); return adopted || Promise.resolve(); },
    buildUI: () => { shown++; }, renderPanels: noop, loadSteps: noop,
    startWatch: async () => false, renderLive: noop, renderLib: noop,
    renderBank: noop, renderSysinfo: noop, sayK: noop, report: noop, syncPreferences: noop,
  });
  const oldRelease = S.beginBusy();
  S.resetBusy();
  const newRelease = S.beginBusy();
  oldRelease();
  ok(S.count() === 1, "editor: old completion cannot release new connection's busy count");
  newRelease(); newRelease(); oldRelease();
  ok(S.count() === 0, "editor: operation release is idempotent and never goes negative");

  let resolve;
  const d = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(d);
  const stale = S.sessionRequest(d, "dump").then(() => null, e => e);
  S.setDevice({}); resolve({});
  ok((await stale)?.message === "closed", "editor: late reply from an unplugged device is rejected");
  let sent = 0;
  d.link.request = async () => { sent++; };
  const wrong = await S.sessionRequest(d, "write").then(() => null, e => e);
  ok(wrong?.message === "closed" && sent === 0, "editor: captured request cannot write through a replacement connection");

  const old = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(old);
  const loadingOld = S.load(old).then(() => null, e => e);
  S.resetBusy(); S.setDevice({});
  const currentRelease = S.beginBusy();
  resolve({});
  ok((await loadingOld)?.message === "closed" && !shown && S.count() === 1,
     "editor: interrupted initial load cannot paint or unlock a new session");
  currentRelease();

  adopted = new Promise(r => { resolve = r; });
  const reachedAdoption = new Promise(r => { adoptStarted = r; });
  const next = { pdesc: [], gdesc: [], names: [], slotUsed: [], link: { request: async kind =>
    kind === "info" ? { pcount: 0, gcount: 0, nengines: 0 } : {} } };
  S.setDevice(next);
  const loadingNext = S.load(next).then(() => null, e => e);
  await reachedAdoption;
  S.resetBusy(); S.setDevice({}); resolve();
  ok((await loadingNext)?.message === "closed" && !shown && S.count() === 0,
     "editor: unplug during library adoption aborts the old load");
  adopted = null;
  S.setDevice(next);
  await S.load(next);
  ok(shown === 1 && S.count() === 0, "editor: a current connection still completes its initial load");

  const savedDevice = { link: { request: async () => { sent++; } } };
  sent = 0;
  S.setDevice(savedDevice);
  const storageWait = new Promise(r => { resolve = r; });
  const copying = S.libOp(async (device, request) => { await storageWait; await request("save"); });
  S.resetBusy(); S.setDevice({}); resolve();
  await copying;
  ok(!sent && S.count() === 0, "editor: library storage delay cannot send a save to a new device");
}

function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 8 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync("python3", [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
  /* recording / trimming: takeSample (a cut of the raw input, faded at the cuts, normalised), autoTrim */
  const R = E.SMP.RATE, raw = new Float64Array(R);       /* 1 s: silence, a tone from 0.25 to 0.6 s, silence */
  for (let i = Math.round(R * 0.25); i < Math.round(R * 0.6); i++) raw[i] = Math.sin(i * 0.2) * 0.3;
  ok(eq(E.takeSample(raw, 0, raw.length), E.normalize(raw)) && eq(E.takeSample(raw, -9, 1e9), E.normalize(raw)),
    "samples: the whole input == normalize (files load as before); the ends are clamped");
  const [a, b] = E.autoTrim(raw), on = Math.round(R * 0.25), off = Math.round(R * 0.6);
  ok(a <= on && on - a <= Math.round(R * 0.005) + 2 && b >= off - 2 && b - off <= Math.round(R * 0.02) + 1,
    `samples: autoTrim finds the sound (${a}..${b} for ${on}..${off}: 5 ms before, 20 ms after)`);
  ok(E.autoTrim(new Float64Array(500)).join() === "0,500", "samples: autoTrim of silence keeps all");
  const cut = E.takeSample(raw, on + 1000, on + 3000);
  let pk = 0;
  for (const v of cut) pk = Math.max(pk, Math.abs(v));
  ok(cut.length === 2000 && cut[0] === 0 && Math.abs(cut[1999]) < 1000 && pk === 30000 &&
     Math.abs(cut[22]) < Math.abs(cut[22 + 63]) + 30000 * 0.6,
    "samples: a cut: its length, faded to 0 at both cut ends, peak normalised");

  /* the trimming view: zoom at the pointer, at least 256 samples, inside the input; pan; out to all */
  {
    const n = 22050;
    let v = E.zoomView(n, 0, n, 0.5, 0.5, 0);
    const z1 = v[1] - v[0] === 11025 && v[0] === 5513;                     /* x2 around the middle */
    v = E.zoomView(n, 0, n, 0.1, 0.25, 0);
    const under = Math.abs((v[0] + 0.1 * (v[1] - v[0])) - 0.1 * n) <= 1;    /* the pointer's sample stays */
    let w = [0, n];
    for (let i = 0; i < 60; i++) w = E.zoomView(n, w[0], w[1], 0.97, 0.8, 0);
    const minOk = w[1] - w[0] === 256 && w[1] <= n;
    const p = E.zoomView(n, 1000, 3000, 0.5, 1, 0.1), edge = E.zoomView(n, 21000, 22000, 0.5, 1, 1);
    const out = E.zoomView(n, 1000, 3000, 0.5, 100, 0);
    ok(z1 && under && minOk && p[0] === 1200 && p[1] === 3200 && edge[1] === n && out.join() === `0,${n}`,
      "samples: trim view: zoom at the pointer, 256 samples at least, inside the input, pan, out to all");
  }
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_9\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot("FM-1_015", "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot("FM-1_900", "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");
}

/* which firmware ChoralRoot is installed over (docs/INSTALL-COMPAT.md; tests/install_test.py GUARD_CASES, the same) */
function firmwareGuard() {
  const cases = [
    ["FM-1_015", null, "allow", "stock"], ["FM-1_014", null, "allow", "stock"],
    ["FM-1_920", "ChoralRoot 0.12", "allow", "choralroot"], ["FM-1_920", "ChoralRoot 1.0-rc1", "allow", "choralroot"],
    ["FM-1_910", "FELUCCA v1.0", "allow", "felucca"], ["FM-1_910", "FELUCCA v1.0.1", "allow", "felucca"],
    ["FM-1_910", "FELUCCA 1.0", "allow", "felucca"], ["FM-1_911", "FELUCCA v1.1-rc1", "allow", "felucca"],
    ["FM-1_90111", "MELODEE v0.11.1", "allow", "melodee"], ["FM-1_910", "MELODEE v1.0", "allow", "melodee"],
    ["FM-1_909", "FELUCCA 0.9-BETA", "refuse", "felucca-beta"], ["FM-1_905", "FELUCCA 0.5 BETA", "refuse", "felucca-beta"],
    ["FM-1_900", "FELUCCA SLOOP 2.2", "refuse", "sloop"], ["FM-1_922", "FELUCCA 2.2 BETA", "refuse", "felucca-beta"],
    ["FM-1_920", "FELUCCA 2.0 BETA", "refuse", "felucca-beta"], ["FM-1_000", null, "refuse", "sloop"],
    ["FM-1_900", null, "refuse", "unknown"], ["FM-1_920", null, "refuse", "unknown"], ["FM-1_500", null, "refuse", "unknown"],
    ["FM-1_910", "SOMETHING 3.0", "refuse", "unknown"], ["ota-FM-1_920", null, "loader", "loader"],
  ];
  const wrong = cases.filter(([i, v, verdict, kind]) => { const r = classifyFirmware(i, v); return r.verdict !== verdict || r.kind !== kind; });
  ok(!wrong.length, `fm1ota.js classifyFirmware: ${cases.length} identity / version cases${wrong.length ? " wrong: " + JSON.stringify(wrong) : ""}`);
  ok(classifyFirmware("FM-1_900", "FELUCCA SLOOP 2.2").name === "Sloop" && classifyFirmware("FM-1_910", "FELUCCA v1.0.1").name === "Felucca v1.0.1" &&
     classifyFirmware("FM-1_90111", "MELODEE v0.11.1").name === "Melodee (v0.11.1)", "fm1ota.js classifyFirmware: names (Sloop, Felucca v1.0.1, Melodee)");
  const t = refusalText("Sloop");
  ok(t.startsWith("Installing over Sloop is not supported: ") && t.includes("with the installer you used for Sloop first, then install ChoralRoot.") &&
     t.endsWith(RECOVERY_URL) && RECOVERY_URL === "https://github.com/Quixotic7/MvaveFM1Unbricker", "fm1ota.js refusalText: the message and the recovery link");
}

firmwareGuard();
await editorMock();
await editorSamplePresets();
mockTables();
await editorLibrarian();
await editorGrid();
await editorLive();
await preferenceReplies();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorSong();
await editorSessions();
await editorFm6();
await editorFm4();
editorTabs();
editorIcons();
samplesMatch();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
