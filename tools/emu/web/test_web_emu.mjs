// SPDX-License-Identifier: GPL-3.0-only
// Headless checks of the browser emulator (node >= 20, no browser needed for the functional part):
//   node tools/emu/web/test_web_emu.mjs [--no-native] [--no-page]
// 1. build/emu-web/choralroot.wasm instantiated with the worklet's stub imports: boots, draws, plays a script of
//    tools/emu/scripts (cr_dmaj.txt: MAJ + D4, KNOB 1, MAJ + M7 + D4) through the same exports the worklet uses,
//    checks its LED expectations, the sound, the chord screen, the MIDI out, the flash dirty counter, the speed.
// 2. Determinism against the native emulator: build/host/emu --headless on the same script (shots removed) with
//    --wav; the samples must be identical (same input at the same millisecond, the firmware is fixed-point).
// 3. The page: build/emu-web served by python3 -m http.server (else file://), screenshot by headless Chromium.
import fs from "node:fs";
import path from "node:path";
import { execFileSync, spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "../../..");
const OUT = path.join(ROOT, "build/emu-web");
const TMP = process.env.TEST_TMP || path.join(ROOT, "build/emu-web-test");
const args = new Set(process.argv.slice(2));
fs.mkdirSync(TMP, { recursive: true });
await import(path.join(HERE, "keymap.js"));
const { KEYMAP, BTN, ENC, noteName } = globalThis.FM1;

let fails = 0;
const ok = (c, msg) => { console.log(`  ${c ? "ok  " : "FAIL"}  ${msg}`); if (!c) fails++; return c; };

// ---------------------------------------------------------------- the module, as worklet.js loads it ---
let stdout = "";
async function load() {
  const bytes = fs.readFileSync(path.join(OUT, "choralroot.wasm"));
  let mem;
  const wasi = {
    clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
    fd_write: (fd, iov, n, out) => {   // the worklet drops the text; here it is kept (web_dump's lines)
      const dv = new DataView(mem.buffer);
      let len = 0;
      for (let i = 0; i < n; i++) {
        const p = dv.getUint32(iov + 8 * i, true), l = dv.getUint32(iov + 8 * i + 4, true);
        stdout += Buffer.from(new Uint8Array(mem.buffer, p, l)).toString("latin1");
        len += l;
      }
      dv.setUint32(out, len, true);
      return 0;
    },
    proc_exit: () => {},
  };
  const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
  const env = new Proxy({}, { get: () => () => 0 });
  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: stub, env });
  const ex = instance.exports;
  mem = ex.memory;
  if (ex._initialize) ex._initialize();
  return ex;
}

// ------------------------------------------------------- a tools/emu/scripts runner on the exports ---
const PANEL = {};   // panel name -> {kind, idx}
for (let k = 0; k < 27; k++) PANEL[noteName(k)] = { kind: "key", idx: k };
BTN.forEach((b, i) => (PANEL[b] = { kind: "btn", idx: i }));
const COMPUTER = Object.fromEntries(KEYMAP.filter((m) => m.code !== "ShiftLeft").map((m) => [m.cap, m]));
COMPUTER.ENTER = COMPUTER.RETURN; COMPUTER.BACKSPACE = COMPUTER.BKSP; COMPUTER.ESCAPE = COMPUTER.ESC;

function parseMs(s) {
  if (/s$/.test(s) && !/ms$/.test(s)) return Math.round(parseFloat(s) * 1000);
  return Math.round(parseFloat(s));
}
function parseScript(text) {   // -> [{ms, op, ...}] (the subset the checks use; unknown lines are an error)
  const ev = [];
  let t = 0;
  for (const raw of text.split("\n")) {
    const words = [];
    for (const w of raw.trim().split(/\s+/)) { if (w.startsWith("#")) break; if (w) words.push(w); }
    if (!words.length) continue;
    const [c, a, b] = words;
    const at = (o) => ev.push({ ms: t, ...o });
    if (c === "wait") t += parseMs(a);
    else if (c === "frames") t += 15 * +a;
    else if (c === "key" || c === "btn") {
      const m = c === "key" ? COMPUTER[a.toUpperCase()] || PANEL[a] : PANEL[a] || COMPUTER[a.toUpperCase()];
      if (!m) throw new Error(`script: unknown ${c} ${a}`);
      if (b === "down" || b === "up") at({ op: b, m });
      else { const d = b ? parseMs(b) : 100; at({ op: "down", m }); t += d; at({ op: "up", m }); }
    } else if (c === "knob") at({ op: "turn", role: ENC.indexOf(a), n: parseInt(b, 10) });
    else if (c === "master") at({ op: "master", v: +a });
    else if (c === "shot") at({ op: "shot", name: path.basename(a).replace(/\.(ppm|png)$/, "") });
    else if (c === "expect" && a === "led") at({ op: "led", name: b, want: words[3] });
    else if (c === "expect" && (a === "sound" || a === "silence")) at({ op: a });
    else if (c === "dump") at({ op: "dump" });
    else if (c === "quit") at({ op: "quit" });
    else throw new Error(`script: ${raw}`);
  }
  return ev;
}

// the page's input state: what emu.js keeps and hands over (web_keys / web_buttons masks, the selected knob)
function makePanel(ex) {
  let keys = 0, btns = 0, sel = 2;
  return {
    apply(op, m) {
      const down = op === "down";
      if (m.kind === "key") { keys = down ? keys | (1 << m.idx) : keys & ~(1 << m.idx); ex.web_keys(keys >>> 0); }
      else if (m.kind === "btn") { btns = down ? btns | (1 << m.idx) : btns & ~(1 << m.idx); ex.web_buttons(btns >>> 0); }
      else if (m.kind === "both") { const b = 3 << 12; btns = down ? btns | b : btns & ~b; ex.web_buttons(btns >>> 0); }
      else if (m.kind === "sel" && down) sel = m.idx;
      else if (m.kind === "turn" && down) ex.web_enc(sel, sel === 7 ? 2 * m.idx : m.idx);
    },
  };
}

const ledState = (ex, name) => {
  const L = new Uint8Array(ex.memory.buffer, ex.web_leds(), 42);
  const p = PANEL[name];
  const v = name === "GREEN" ? L[41] : p.kind === "key" ? L[p.idx] : L[27 + p.idx];
  return ["off", "dim", "on"][v];
};
const fbCopy = (ex) => new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240).slice();
const fbLit = (fb) => fb.reduce((n, v) => n + (v !== 0), 0);
const fbDiff = (a, b) => a.reduce((n, v, i) => n + (v !== b[i]), 0);

// run a script: events at ms T are applied after the device's millisecond T (emu.c: tick(T), run_script(T), ...)
function runScript(ex, events, log) {
  const panel = makePanel(ex);
  const end = events.length ? events[events.length - 1].ms + 15 : 9000;
  const pcm = [];   // int16 interleaved, as emu.c writes its WAV
  const midi = [];
  const shots = {};
  let nz = 0, nzMark = 0, i = 0;
  const drain = () => {
    const n = ex.web_avail();
    for (let o = 0; o < n; o += 1024) {
      const k = Math.min(1024, n - o);
      ex.web_render(k);
      const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), k), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), k);
      for (let j = 0; j < k; j++) {
        const a = Math.round(l[j] * 32768), b = Math.round(r[j] * 32768);
        pcm.push(a, b);
        nz += (a !== 0) + (b !== 0);
      }
    }
    for (let p; (p = ex.web_midi_out_take()); ) midi.push(p >>> 0);
  };
  for (let ms = 0; ms <= end; ms++) {
    ex.web_step(1);
    drain();
    while (i < events.length && events[i].ms <= ms) {
      const e = events[i++];
      if (e.op === "down" || e.op === "up") panel.apply(e.op, e.m);
      else if (e.op === "turn") ex.web_enc(e.role, e.n);
      else if (e.op === "master") ex.web_master(e.v);
      else if (e.op === "shot") shots[e.name] = fbCopy(ex);
      else if (e.op === "led") {
        const got = ledState(ex, e.name);
        log.push([`expect led ${e.name} ${e.want} at ${ms} ms`, got === e.want, got]);
      } else if (e.op === "sound" || e.op === "silence") {
        const got = nz - nzMark; nzMark = nz;
        log.push([`expect ${e.op} at ${ms} ms`, e.op === "sound" ? got > 0 : got === 0, `${got} non-zero samples`]);
      } else if (e.op === "dump") { stdout = ""; ex.web_dump(); log.push(["dump", true, stdout]); }
      else if (e.op === "quit") { ms = end; break; }
    }
  }
  return { pcm: Int16Array.from(pcm), midi, shots, end };
}

const rms = (pcm) => Math.sqrt(pcm.reduce((s, v) => s + (v / 32768) ** 2, 0) / Math.max(1, pcm.length));

// ===================================================================================================== 1 ===
console.log("browser emulator: build/emu-web/choralroot.wasm");
const ex = await load();
console.log(`  version: ${(() => { const m = new Uint8Array(ex.memory.buffer, ex.web_version()); return Buffer.from(m.subarray(0, m.indexOf(0))).toString(); })()}`);
const w0 = ex.web_lcd_writes();
ex.web_boot();
ok(ex.web_lcd_writes() > w0, "boot: the LCD was written");
const script = fs.readFileSync(path.join(ROOT, "tools/emu/scripts/cr_dmaj.txt"), "utf8");
const events = parseScript(script);
const log = [];
const t0 = performance.now();
const run = runScript(ex, events, log);
const secs = run.pcm.length / 2 / 44100, took = (performance.now() - t0) / 1000;
ok(fbLit(run.shots.cr_boot) > 1000, `after boot (500 ms): ${fbLit(run.shots.cr_boot)} non-black pixels`);
const dChord = fbDiff(run.shots.cr_boot, run.shots.cr_dmaj);
ok(dChord > 500, `MAJ + D4: the chord screen (${dChord} pixels changed from the boot screen)`);
ok(fbDiff(run.shots.cr_dmaj, run.shots.cr_voicing) > 0, "KNOB 1 +2: the screen changed (the voicing line)");
for (const [what, good, got] of log.filter((l) => l[0] !== "dump")) ok(good, `${what} (${got})`);
const dumps = log.filter((l) => l[0] === "dump").map((l) => l[2]).join("\n");
ok(/chord D sounding 1 notes 62 66 69/.test(dumps), "the engine: D major, notes 62 66 69 (web_dump)");
ok(/part 0: .* voices 3/.test(dumps), "part 0 plays the three notes");
const lastBusy = [...dumps.matchAll(/parts busy (\d+)/g)].pop();
ok(lastBusy && lastBusy[1] === "0", "every part silent 2 s after the last release (no stuck note)");
const finite = run.pcm.every(Number.isFinite), peak = run.pcm.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
ok(finite && peak > 300, `audio: ${secs.toFixed(2)} s, peak ${(peak / 32768).toFixed(3)}, rms ${rms(run.pcm).toFixed(4)}`);
const noteOn = run.midi.filter((p) => (p & 0xf) === 9 && ((p >> 8) & 0xff) === 0x90 && ((p >> 24) & 0xff) > 0);
const notes = [...new Set(noteOn.map((p) => (p >> 16) & 0xff))].sort((a, b) => a - b);
ok(noteOn.length >= 3 && [62, 66, 69].every((n) => notes.includes(n)),
   `MIDI out: ${run.midi.length} packets, ${noteOn.length} note-ons on channel 1 (notes ${notes.join(" ")})`);
ok(true, `speed: ${secs.toFixed(2)} s of device time in ${took.toFixed(2)} s (${(secs / took).toFixed(1)}x real time)`);

// the flash: a setting changed (BPM) is saved by cr_settings_poll once things are quiet
{
  const d0 = ex.web_flash_dirty();
  ex.web_enc(0, 3);   // SELECT = BPM
  for (let s = 0; s < 12000 && ex.web_flash_dirty() === d0; s += 10) { ex.web_step(10); ex.web_render(Math.min(1024, ex.web_avail())); while (ex.web_avail()) ex.web_render(Math.min(1024, ex.web_avail())); }
  const d1 = ex.web_flash_dirty();
  ok(d1 > d0, `flash: BPM +3 saved (dirty counter ${d0} -> ${d1}); web_flash_size ${ex.web_flash_size()}`);
  // and it comes back: a second module booted from the saved image keeps the setting (the IndexedDB round trip)
  const img = new Uint8Array(ex.memory.buffer, ex.web_flash(), ex.web_flash_size()).slice();
  const ex2 = await load();
  new Uint8Array(ex2.memory.buffer, ex2.web_flash_stage(), img.length).set(img);
  ex2.web_flash_stage_len(img.length);
  ex2.web_boot();
  ex2.web_step(300);
  stdout = ""; ex.web_dump(); const bpm1 = /bpm (\d+)/.exec(stdout)?.[1];
  stdout = ""; ex2.web_dump(); const bpm2 = /bpm (\d+)/.exec(stdout)?.[1];
  ok(bpm1 && bpm1 === bpm2, `flash round trip: BPM ${bpm1} after a reboot from the saved image (${bpm2})`);
}

// the worklet's view: 128-frame quanta while a chord plays (a UI frame lands in some of them)
{
  ex.web_keys((1 << 5) | (1 << 4) | (1 << 9));   // MAJ + M7 + D4
  const ts = [];
  for (let q = 0; q < 3 * 44100 / 128; q++) { const t = performance.now(); ex.web_render(128); ts.push(performance.now() - t); }
  ex.web_keys(0);
  ts.sort((a, b) => a - b);
  const avg = ts.reduce((a, b) => a + b, 0) / ts.length, budget = 128 / 44.1;
  ok(ts[ts.length - 1] < budget * 4, `render quantum (128 frames, budget ${budget.toFixed(2)} ms): avg ${avg.toFixed(3)} ms, ` +
     `99th ${ts[Math.floor(ts.length * 0.99)].toFixed(3)} ms, max ${ts[ts.length - 1].toFixed(3)} ms (node, this machine)`);
}

// ===================================================================================================== 2 ===
if (!args.has("--no-native")) {
  console.log("determinism against the native emulator (build/host/emu --headless)");
  const emu = path.join(ROOT, "build/host/emu");
  if (!fs.existsSync(emu)) console.log("  skip  bit-exact comparison: build/host/emu missing (sh tools/emu/build.sh, macOS)");
  else {
    const scr = path.join(TMP, "web_cr_dmaj.txt"), wav = path.join(TMP, "web_cr_dmaj_native.wav");
    fs.writeFileSync(scr, script.split("\n").filter((l) => !/^\s*shot\b/.test(l)).join("\n"));
    try {
      execFileSync(emu, ["--headless", "--script", scr, "--wav", wav], { cwd: TMP, env: { ...process.env, SDL_VIDEODRIVER: "dummy", SDL_AUDIODRIVER: "dummy" }, stdio: "pipe" });
    } catch (e) { /* exit 1 on a failed expect: still compare */ }
    const b = fs.readFileSync(wav);
    const nat = new Int16Array(b.buffer.slice(b.byteOffset + 44, b.byteOffset + b.length));
    let first = -1;
    const n = Math.min(nat.length, run.pcm.length);
    for (let i = 0; i < n; i++) if (nat[i] !== run.pcm[i]) { first = i; break; }
    if (nat.length === run.pcm.length && first < 0) ok(true, `bit-exact: ${nat.length / 2} stereo frames identical to the native emulator's WAV`);
    else {
      ok(false, `samples differ: native ${nat.length / 2} frames, web ${run.pcm.length / 2}; first difference at frame ${first >> 1}`);
      ok(Math.abs(rms(nat) - rms(run.pcm)) < 0.25 * rms(nat), `loosely: rms native ${rms(nat).toFixed(4)}, web ${rms(run.pcm).toFixed(4)}`);
    }
  }
}

// ===================================================================================================== 3 ===
if (!args.has("--no-page")) {
  console.log("the page (headless Chromium screenshot)");
  const chrome = "/Applications/Chromium.app/Contents/MacOS/Chromium";
  if (!fs.existsSync(chrome)) console.log("  skip  screenshot: no " + chrome);
  else {
    const port = 18000 + Math.floor(Math.random() * 2000);
    const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1"], { cwd: OUT, stdio: "ignore" });
    await new Promise((r) => setTimeout(r, 800));
    const shoot = async (url, png) => {
      try { fs.unlinkSync(png); } catch {}
      const c = spawn(chrome, ["--headless=new", "--use-mock-keychain", "--password-store=basic", "--disable-gpu", "--no-sandbox", "--no-first-run", "--no-default-browser-check",
        `--user-data-dir=${path.join(TMP, "chrome-profile-emu")}`, "--hide-scrollbars", "--allow-file-access-from-files",
        "--window-size=1280,1400", "--timeout=20000", "--virtual-time-budget=4000", `--screenshot=${png}`, url], { stdio: "ignore" });
      for (let i = 0; i < 60 && !(fs.existsSync(png) && fs.statSync(png).size > 0); i++) await new Promise((r) => setTimeout(r, 500));
      await new Promise((r) => setTimeout(r, 500));
      c.kill();
      try { execFileSync("pkill", ["-f", "chrome-profile-emu"]); } catch {}
      return fs.existsSync(png) && fs.statSync(png).size > 20000;   // (a blank error page is small)
    };
    const png = path.join(TMP, "page.png");
    let good = await shoot(`http://127.0.0.1:${port}/index.html?demo`, png);
    srv.kill();
    if (!good) good = await shoot(`file://${path.join(OUT, "index.html")}?demo`, png);
    ok(good, `screenshot: ${png}`);
  }
}

console.log(fails ? `FAIL (${fails})` : "PASS");
process.exit(fails ? 1 : 0);
