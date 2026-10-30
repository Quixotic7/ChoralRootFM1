// SPDX-License-Identifier: GPL-3.0-only
// Headless checks of the SMK-37 browser emulator (node >= 20; no browser for the functional part):
//   node tools/emu/web/test_web_emu_smk.mjs [--no-page]
// 1. build/emu-web-smk37/choralroot-smk37.wasm (sh tools/emu/web/build_web.sh) instantiated with the worklet's stub
//    imports: boots and draws; MAJ's pad and C4 held: the chord C major sounds (notes 60 64 67), the screen's chord
//    name is "C M", the pad lights; a key struck soft and loud goes out at a lower and a higher velocity (the keys' two
//    contacts through the factory curve, hal/smk_input.h, Key Touch Dynamic); K1 turns the volume down; SCALE (KEY)
//    lights; the same input twice gives the same samples. K1's volume kept over a power cycle. The pedal jack: Sustain
//    holds a key's note past its release until the pedal comes up (CC 64 out); Expression sets CC 11 and the parts'
//    ex_off, nothing before the pedal moves, full again back on Sustain.
// 2. The page: build/emu-web-smk37 served by python3 -m http.server, a headless Chromium screenshot of ?demo (its
//    processes stopped by their PID).
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "../../..");
const OUT = path.join(ROOT, "build/emu-web-smk37");
const TMP = process.env.TEST_TMP || path.join(ROOT, "build/emu-web-smk37-test");
const args = new Set(process.argv.slice(2));
fs.mkdirSync(TMP, { recursive: true });
let fails = 0;
const ok = (c, msg) => { console.log(`  ${c ? "ok  " : "FAIL"}  ${msg}`); if (!c) fails++; return c; };

async function load() {
  const bytes = fs.readFileSync(path.join(OUT, "choralroot-smk37.wasm"));
  let mem;
  const out = { text: "" };
  const wasi = {
    clock_time_get: (id, prec, o) => { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; },
    fd_write: (fd, iov, n, o) => {
      const dv = new DataView(mem.buffer);
      let len = 0;
      for (let i = 0; i < n; i++) {
        const p = dv.getUint32(iov + 8 * i, true), l = dv.getUint32(iov + 8 * i + 4, true);
        out.text += Buffer.from(new Uint8Array(mem.buffer, p, l)).toString("latin1");
        len += l;
      }
      dv.setUint32(o, len, true);
      return 0;
    },
    proc_exit: () => {},
  };
  const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: stub, env: new Proxy({}, { get: () => () => 0 }) });
  const ex = instance.exports;
  mem = ex.memory;
  if (ex._initialize) ex._initialize();
  const str = (p) => { const b = new Uint8Array(mem.buffer, p, 1024); return Buffer.from(b.subarray(0, b.indexOf(0))).toString("latin1"); };
  const render = (n) => {                     // n frames: their energy and a hash of the samples
    let e = 0;
    const h = createHash("sha256");
    while (n > 0) {
      const k = Math.min(1024, n);
      ex.web_render(k);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), k), r = new Float32Array(mem.buffer, ex.web_out_r(), k);
      for (let i = 0; i < k; i++) e += l[i] * l[i] + r[i] * r[i];
      h.update(Buffer.from(l.slice().buffer)); h.update(Buffer.from(r.slice().buffer));
      n -= k;
    }
    return { e, hash: h.digest("hex") };
  };
  const midi = () => { const m = []; for (let p; (p = ex.web_midi_out_take()); ) m.push(p >>> 0); return m; };
  const dump = () => { out.text = ""; ex.web_dump(); return out.text; };
  return { ex, mem, str, render, midi, dump, ui: () => str(ex.web_ui_info()) };
}
const noteOns = (pkts) => pkts.filter((p) => (p & 15) === 9 && ((p >>> 8) & 0xf0) === 0x90 && (p >>> 24) & 127)
  .map((p) => ({ note: (p >>> 16) & 127, vel: (p >>> 24) & 127 }));
const FS = 44100, ms = (t) => Math.round((t * FS) / 1000);

console.log("the module (SMK-37)");
if (!ok(fs.existsSync(path.join(OUT, "choralroot-smk37.wasm")), "build/emu-web-smk37/choralroot-smk37.wasm (sh tools/emu/web/build_web.sh)")) process.exit(1);
async function run(script) {                  // the chord: MAJ's pad, then C4; returns what the checks need
  const d = await load();
  const { ex } = d;
  ok(/-smk37$/.test(d.str(ex.web_version())), `version ${d.str(ex.web_version())}`);
  ex.web_boot();
  const boot = d.render(ms(1500));
  ok(ex.web_lcd_writes() > 0 && /^stripes|^chord/.test(d.ui()), `boots and draws (${ex.web_lcd_writes()} screen writes, ${d.ui().split(" ")[0]})`);
  d.midi();
  ex.web_smk_pad(2, 100);                     // MAJ (pad 3 of the top row)
  d.render(ms(40));
  ex.web_smk_key(12, 100);                    // C4
  const chord = d.render(ms(1000));
  const r = { d, boot, chord, ui: d.ui(), dump: d.dump(), on: noteOns(d.midi()),
              pads: Array.from(new Uint32Array(d.mem.buffer, ex.web_smk_pads(), 16)) };
  if (script) await script(d, r);
  return r;
}
const a = await run(async (d, r) => {
  const { ex } = d;
  ex.web_smk_key(12, 0);
  ex.web_smk_pad(2, 0);
  d.render(ms(300));
  d.midi();
  ex.web_smk_key(19, 20);                     // G4 soft
  d.render(ms(200));
  ex.web_smk_key(19, 0);
  d.render(ms(200));
  r.soft = noteOns(d.midi());
  ex.web_smk_key(19, 127);                    // .. and hard
  d.render(ms(200));
  ex.web_smk_key(19, 0);
  d.render(ms(200));
  r.hard = noteOns(d.midi());
  const m0 = ex.web_smk_master();
  ex.web_smk_enc(0, -5);                      // K1: the volume down five detents
  d.render(ms(300));
  r.master = [m0, ex.web_smk_master()];
  ex.web_smk_button(8, 1);                    // SCALE: KEY
  d.render(ms(60));
  ex.web_smk_button(8, 0);
  d.render(ms(200));
  r.scale = new Uint8Array(d.mem.buffer, ex.web_smk_leds(), 19)[8];
});
ok(a.boot.e === 0, "silent until played");
ok(a.chord.e > 1, `MAJ pad + C4: the chord sounds (energy ${a.chord.e.toFixed(1)})`);
ok(/name 'C\|M\|'/.test(a.ui), `the screen draws the chord's name C M (${(a.ui.match(/name '[^']*'/) || ["?"])[0]})`);
ok(/chord CM sounding \d+ notes 60 64 67/.test(a.dump), `the engine: C major, notes 60 64 67 (${(a.dump.match(/chord \S+ sounding.*/) || ["?"])[0]})`);
ok(a.on.length >= 3 && a.on.some((n) => n.note === 60), `the chord's note-ons go out over MIDI (${a.on.map((n) => n.note + "/" + n.vel).join(" ")})`);
ok((a.pads[2] >> 16 & 255) >= 100 && (a.pads[0] >> 16 & 255) < 60, `MAJ's pad lit bright, DIM's dim (${a.pads[2].toString(16)} / ${a.pads[0].toString(16)})`);
const sv = a.soft.map((n) => n.vel), hv = a.hard.map((n) => n.vel);
ok(sv.length && hv.length && Math.max(...sv) < Math.min(...hv), `velocity: G4 soft ${sv.join(",")} < hard ${hv.join(",")} (the two contacts, Key Touch Dynamic)`);
ok(a.master[1] < a.master[0], `K1 turns the volume down (${a.master[0]} -> ${a.master[1]})`);
ok(a.scale === 1, "SCALE (KEY) lights its LED");
const b = await run();
ok(b.chord.hash === a.chord.hash && b.chord.e === a.chord.e, "the same input twice: the same samples (deterministic)");

console.log("K1, the volume (GitHub issue 30): its popup, and the level back after a power cycle");
{
  const d = await load(), ex = d.ex;
  ex.web_boot();
  d.render(ms(3000));                         // (past the splash)
  ok(ex.web_smk_master() === 724, `a fresh unit starts at 724 (${ex.web_smk_master()})`);
  ex.web_smk_enc(0, -10);                     // ten detents down (the model's quadrature lands nine, as the 724 -> 628
  d.render(ms(200));                          // of five above: four), 24 each
  const lv = ex.web_smk_master(), sr = (d.dump().match(/screen reads:.*/) || ["?"])[0];
  const pct = String(Math.floor((lv * 100 + 511) / 1023)).padStart(2, "0");
  ok(lv < 724 && (724 - lv) % 24 === 0, `K1 down: ${lv}, in detents of 24`);
  ok(new RegExp(`value '${pct}' sub '%' label 'volume'`).test(sr), `the popup: volume ${pct} % (${sr.trim()})`);
  ex.web_smk_button(11, 1);                   // BT = SAVE, held: the global save
  d.render(ms(2000));
  ex.web_smk_button(11, 0);
  d.render(ms(500));
  const img = new Uint8Array(d.mem.buffer, ex.web_flash(), ex.web_flash_size()).slice();
  const e = await load();                     // the power cycle: a new unit on the saved flash
  new Uint8Array(e.mem.buffer, e.ex.web_flash_stage(), img.length).set(img);
  e.ex.web_flash_stage_len(img.length);
  e.ex.web_boot();
  e.render(ms(3000));
  ok(e.ex.web_smk_master() === lv, `after SAVE held and a power cycle: ${lv} again (${e.ex.web_smk_master()})`);
  const sr2 = (e.dump().match(/screen reads:.*/) || [""])[0];
  ok(!/label 'volume'/.test(sr2), "no volume popup at power-on (the level set, K1 not turned)");
}

console.log("the pedal jack: Sustain holds a released key's note until it comes up; Expression sets CC 11");
{
  const d = await load(), ex = d.ex;
  const cc = (pkts, n) => pkts.filter((p) => (p & 15) === 11 && ((p >>> 8) & 0xf0) === 0xb0 && ((p >>> 16) & 127) === n)
    .map((p) => (p >>> 24) & 127);
  const gates = () => ex.web_smk_part(0) & 255, exOff = (p) => ex.web_smk_part(p) >>> 8;
  ex.web_boot();
  d.render(ms(3000));                         // (past the splash; the jack empty: 985)
  d.midi();
  ex.web_smk_key(19, 100);                    // G4 without the pedal: let go, it ends
  d.render(ms(200));
  const g0 = gates();
  ex.web_smk_key(19, 0);
  d.render(ms(200));
  ok(g0 > 0 && gates() === 0, `no pedal: G4 sounds (${g0} keyed voices) and ends when let go (${gates()})`);
  ok(cc(d.midi(), 64).length === 0, "the empty jack sends no CC 64");
  ex.web_smk_pedal(0);                        // a normally-open switch pedal pressed: the jack shorted
  d.render(ms(100));
  const down = cc(d.midi(), 64);
  ex.web_smk_key(19, 100);
  d.render(ms(200));
  ex.web_smk_key(19, 0);
  d.render(ms(300));
  const held = gates(), offs = d.midi().filter((p) => ((p >>> 8) & 0xf0) === 0x80 || (((p >>> 8) & 0xf0) === 0x90 && !((p >>> 24) & 127)));
  ex.web_smk_pedal(985);                      // up
  d.render(ms(100));
  const up = cc(d.midi(), 64);
  ok(down.length >= 1 && down.every((v) => v === 127), `pedal down: CC 64 127 out (${down.join(",")})`);
  ok(held > 0, `G4 let go under the pedal: it still sounds (${held} keyed voices)`);
  ok(offs.length >= 1, `.. its MIDI note-off went out at once (${offs.length})`);
  ok(gates() === 0 && up.length >= 1 && up.every((v) => v === 0), `pedal up: the note ends (${gates()}), CC 64 0 out (${up.join(",")})`);
  ex.web_smk_pedal_mode(1);                   // Options > Pedal: Expression; an expression pedal plugged in
  ex.web_smk_pedal(170);
  d.render(ms(600));
  const still = cc(d.midi(), 11);
  ok(still.length === 0 && exOff(0) === 0, `Expression, the pedal resting: nothing sent, the parts at full (${still.join(",")} ex_off ${exOff(0)})`);
  const sweep = (from, to, step) => {         // 15 ms a step, the MIDI out taken as it goes (its queue is short)
    const m = [];
    for (let r = from; step < 0 ? r >= to : r <= to; r += step) { ex.web_smk_pedal(r); d.render(ms(15)); m.push(...d.midi()); }
    d.render(ms(200));
    return cc(m.concat(d.midi()), 11);
  };
  sweep(170, 10, -10);                        // toe down (lower raw = more)
  const sw = sweep(10, 170, 10);              // heel down
  ok(sw.length > 2 && sw[sw.length - 1] === 0 && exOff(0) === 127 && exOff(1) === 127,
     `the pedal back to the heel: CC 11 down to 0 (${sw.slice(-4).join(",")}), both parts' ex_off 127 (${exOff(0)}, ${exOff(1)})`);
  const half = sweep(170, 90, -10), hv = half[half.length - 1];   // half way
  ok(hv > 40 && hv < 90 && exOff(0) === 127 - hv, `half way: CC 11 ${hv}, ex_off ${exOff(0)}`);
  ex.web_smk_pedal_mode(0);                   // back to Sustain: the parts at full again
  d.render(ms(100));
  const back = cc(d.midi(), 11);
  ok(back[back.length - 1] === 127 && exOff(0) === 0 && exOff(1) === 0, `back to Sustain: CC 11 127, ex_off 0 (${back.join(",")})`);
}

if (!args.has("--no-page")) {
  console.log("the page (headless Chromium screenshot)");
  const chrome = "/Applications/Chromium.app/Contents/MacOS/Chromium";
  if (!fs.existsSync(chrome)) console.log("  skip  screenshot: no " + chrome);
  else {
    const port = 20000 + Math.floor(Math.random() * 2000);
    const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1"], { cwd: OUT, stdio: "ignore" });
    await new Promise((r) => setTimeout(r, 800));
    const png = path.join(TMP, "page.png");
    try { fs.unlinkSync(png); } catch {}
    const c = spawn(chrome, ["--headless=new", "--use-mock-keychain", "--password-store=basic", "--disable-gpu", "--no-sandbox", "--no-first-run",
      "--no-default-browser-check", `--user-data-dir=${path.join(TMP, "chrome-profile-smk")}`, "--hide-scrollbars", "--window-size=1500,1000",
      "--timeout=20000", "--virtual-time-budget=4000", `--screenshot=${png}`, `http://127.0.0.1:${port}/index.html?demo`],
      { stdio: "ignore", detached: true });
    for (let i = 0; i < 60 && !(fs.existsSync(png) && fs.statSync(png).size > 0); i++) await new Promise((r) => setTimeout(r, 500));
    await new Promise((r) => setTimeout(r, 500));
    try { process.kill(-c.pid); } catch {}     // (its process group, by its PID)
    srv.kill();
    ok(fs.existsSync(png) && fs.statSync(png).size > 30000, `screenshot: ${png}`);
  }
}

console.log(fails ? `FAIL (${fails})` : "PASS");
process.exit(fails ? 1 : 0);
