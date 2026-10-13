// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the installer page, web/index_pkg.html (no browser, no hardware). Run from the repo root:
//   node web/test_installer.mjs
// - the page's shape: /*LIB*/ and /*META*/ once, one module script, no editor link, no Felucca status, English only,
//   the backup step (Install backs up first, Skip backup, Back up / Restore buttons)
// - the texts: every data-t key and every error code fm1ota.js throws has an English text
// - the page as make_site.py inlines it (the libraries make_site.py names, META for 0.1 / FM-1_920) runs against a
//   DOM stub and a simulated FM-1 (after test_web.mjs's FakeFM1): no Web MIDI, package load, install, resume,
//   errors, the backup before an install (Felucca's data saved to a file, then offered back on ChoralRoot), Skip backup,
//   no backup possible (confirm), Back up and Restore, and the return to official V15 (backup, confirm first, nothing
//   written when it is declined)
// - the Sounds section (fm1sounds.js, docs/SOUNDS.md): Read sounds -> the table, Export -> a sound file, Rename (the
//   bank only), Import (the store, then the bank; a used slot asks first), Delete, a bad file, and Felucca refused;
//   .syx: Export .syx on FM6 / CZ-1 rows, a .syx imported into an empty slot, a two-voice file asks which voice
// - one page, Install at the top, and the versions of web/releases.json: Install is reachable without any other
//   click and its label names the version it writes; the newest is the default with its notes; "Other versions"
//   render from the JSON, an older one installs its own package (checked against the list's sha256 before any write)
//   with its going-back sentence beside the button and one click back to the newest; 0.14 / 0.15 not offered;
//   "What is on my FM-1?", an interrupted install's guidance, and demo mode (?demo=STATE), which must never reach
//   MIDI or the network

import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf, STOCK_V15_SIZE } from "./fm1pkg.js";
import { pack7, unpack7 } from "./fm1ota.js";
import { bkU32, bkR32, bkPack, bkUnpack, bkCrc, CR_BACKUP_IDS, BACKUP_IDS } from "./fm1backup.js";
import { readSoundFile, parseSoundObjects, parseSyx, fm6BlobToVced, fm6VcedSyx } from "./fm1sounds.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const same = eq;
const HERE = new URL(".", import.meta.url).pathname;
const count = (s, sub) => s.split(sub).length - 1;

const html = readFileSync(join(HERE, "index_pkg.html"), "utf8");
const ota = readFileSync(join(HERE, "fm1ota.js"), "utf8");
const sha256 = async (b) => Buffer.from(await crypto.subtle.digest("SHA-256", b)).toString("hex");
// a package with an identity: 20 blocks of 47 bytes + a marker byte each, then the rest (fm1pkg.js productOf); the
// seed makes packages that differ (one per version)
function makePackage(product, seed = 0) {
  const raw = Uint8Array.from({ length: 20 * 48 + 0x2000 }, (_, i) => (i * 13 + 5 + seed) & 0xFF);
  for (let i = 0; i < 20; i++) raw[i * 48 + 47] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  return raw;
}
const raw = makePackage("FM-1_920");
// the metadata as make_site.py writes it: the versions (here one, 0.1), each with its package and the package's sha256
const META = { version: "0.1", product: "FM-1_920", pkg: "../../firmware/choralroot-0.1.fwsc", repo: "https://github.com/Quixotic7/ChoralRootFM1",
  releases: [{ version: "0.1", date: "2026-10-06", tag: "v0.1", sha256: await sha256(raw), bullets: ["The first release."],
    pkg: "../../firmware/choralroot-0.1.fwsc" }] };

/* -------------------------------------------------------------- (a) shape --- */
ok(count(html, "/*LIB*/") === 1 && count(html, "/*META*/") === 1, "page: /*LIB*/ and /*META*/ exactly once");
ok(count(html, "<script") === 1 && count(html, '<script type="module">') === 1, "page: one <script type=\"module\">, no other script");
ok(!html.includes("../editor/"), "page: no link to ../editor/");
ok(!/say\(\s*`Felucca /.test(html) && !/`Felucca \$\{/.test(html), "page: the status does not name Felucca as the product");
ok(!/\bja\s*:/.test(html) && !html.includes('id="lang"'), "page: English only (no ja table, no language toggle)");
ok(["captureBackup", "restoreBackup", "BackupConnection", "saveArchive", "deviceInfo", "restartDevice"].every((f) => html.includes(f)) &&
   ["skip-backup", "backup-go", "restore-go", "restore-file", "stock-recovery"].every((id) => html.includes(`id="${id}"`)),
   "page: the backup step (capture, restore, restart; Skip backup, Back up, Restore, stock resume box)");
ok(!/no backup protocol|has no backup/i.test(html), "page: the \"no backup\" texts are gone");
ok(["snd-read", "snd-file", "snd-table", "snd-rows"].every((id) => html.includes(`id="${id}"`)) &&
   ["readSounds", "writeSounds", "parseSoundObjects", "exportSound", "importSound", "renameSound", "deleteSound", "readSoundFile",
    "soundFileName", "soundsRun", "renderSounds"].every((f) => html.includes(f)) &&
   html.indexOf('id="sounds"') > html.indexOf('data-t="bkTitle"'), "page: the Sounds section (after Back up and restore): Read sounds, the table, the file input");
ok(html.includes("<title>ChoralRoot FM-1 · installer</title>") && /<html lang="en">/.test(html), "page: title and lang");
ok(html.includes('href="../../"') && ["LICENSING.md", "LICENSE\"", "LICENSES/Apache-2.0.txt", "LICENSES/\""].every((p) => html.includes(`href="../../firmware/${p}`)),
  "page: link to the landing page and the licence links");
ok(html.includes("https://github.com/Quixotic7/ChoralRootFM1") && html.includes("https://github.com/hugelton/Felucca") &&
   html.includes("https://hugelton.com") && html.includes("https://www.m-vave.com/download"), "page: source, Felucca, Hügelton and M-VAVE links");
ok(!/https?:\/\/(?!github\.com|hugelton\.com|www\.m-vave\.com)[^"\s]*\.(css|js|woff2?|png|svg|jpg)/.test(html) && !/<link\s/i.test(html),
  "page: no external assets");

/* ------------------------------------------------------------- (b) texts --- */
const scriptOf = (h) => h.slice(h.indexOf('<script type="module">') + '<script type="module">'.length, h.lastIndexOf("</script>"));
const textSrc = scriptOf(html).match(/const TEXT = (\{[\s\S]*?\n\});/);
const TEXT = textSrc ? new Function(`return ${textSrc[1]}`)() : {};
ok(textSrc && Object.keys(TEXT).join() === "en", "texts: a single `en` table");
const en = TEXT.en || {};
const dataT = [...html.matchAll(/data-t="([^"]+)"/g)].map((m) => m[1]);
const missingT = dataT.filter((k) => typeof en[k] !== "string");
ok(dataT.length > 0 && !missingT.length, `texts: every data-t key has a text (${dataT.length} keys${missingT.length ? "; missing " + missingT : ""})`);
const codes = new Set();
for (const m of ota.matchAll(/\bfail\(([^,]+),/g)) for (const s of m[1].matchAll(/"([a-z]+)"/g)) codes.add(s[1]);
for (const c of ["nomidi", "loadfail", "badpkg", "denied", "leave", "error", "start", "verify", "loader", "write", "reboot", "done", "stopped"]) codes.add(c);
const missingC = [...codes].filter((c) => typeof en[c] !== "string");
ok(codes.size > 13 && !missingC.length, `texts: every fm1ota.js error code and status has a text (${codes.size}${missingC.length ? "; missing " + missingC : ""})`);
const sayKeys = [...scriptOf(html).matchAll(/(?:\bsayK|\bt|\bcoded)\("([A-Za-z]+)"/g)].map((m) => m[1]);
ok(sayKeys.every((k) => typeof en[k] === "string"), `texts: every key the script names has a text (${sayKeys.filter((k) => typeof en[k] !== "string")})`);
const tfKeys = [...scriptOf(html).matchAll(/\btf\("([A-Za-z]+)"/g)].map((m) => m[1]);
ok(tfKeys.length >= 4 && tfKeys.every((k) => typeof en[k] === "string"), `texts: every key tf() fills has a text (${tfKeys.length})`);
ok(["sndTitle", "sndText", "sndAll", "sndReadGo", "sndColSlot", "sndColName", "sndColEngine", "sndColPatch"].every((k) => dataT.includes(k) && en[k]) &&
   ["sndNeedCr", "sndExport", "sndImport", "sndRename", "sndDelete", "sndEmpty"].every((k) => en[k]) && /ChoralRoot/.test(en.sndNeedCr),
   "texts: the Sounds section's data-t keys and its statuses");

/* -------------------------------------------------------- (d) status line --- */
const tpl = html.match(/say\((`ChoralRoot \$\{meta\.version\} · \$\{meta\.product\}`)\)/);
ok(tpl && new Function("meta", `return ${tpl[1]}`)(META) === "ChoralRoot 0.1 · FM-1_920", "status: `ChoralRoot ${meta.version} · ${meta.product}` -> ChoralRoot 0.1 · FM-1_920");

/* ------------------------------------------- (c) the page as make_site inlines it --- */
// make_site.py's strip_module and the libraries it inlines (read from make_site.py, so the test follows it)
const stripModule = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
let libs = [...site.matchAll(/strip_module\(\(HERE \/ "([\w.]+\.js)"\)/g)].map((m) => m[1]);
if (!libs.length) libs = ["fm1pkg.js", "fm1ota.js", "fm1backup.js", "fm1sounds.js"];
ok(libs.join() === "fm1pkg.js,fm1ota.js,fm1backup.js,fm1sounds.js", `make_site.py inlines ${libs.join(", ")} (fm1sounds.js after fm1backup.js)`);
const lib = libs.map((f) => stripModule(readFileSync(join(HERE, f), "utf8"))).join("\n");
const built = html.split("/*LIB*/").join(lib).split("/*META*/").join(JSON.stringify(META));   // no $-patterns
const code = scriptOf(built);
let compiled = false;
try { new vm.Script(code, { filename: "installer.js" }); compiled = true; } catch (e) { console.log(String(e)); }
ok(compiled, `inlined page (${libs.join(", ")} + META) compiles`);
ok(!/^\s*(import|export)\s/m.test(code), "inlined page: no import / export left");

// the DOM the script uses: one stub per tag with an id or a data-t
class El {
  constructor(attrs) {
    this.id = attrs.id; this.textContent = ""; this.disabled = "disabled" in attrs; this.value = 0; this.files = [];
    this.checked = false; this.dataset = attrs.t ? { t: attrs.t } : {}; this.style = {}; this.l = {}; this.clicks = 0;
    this.hidden = "hidden" in attrs; this.innerHTML = ""; this.className = ""; this.attrs = {}; this.focused = 0;
    this.scrolled = 0;
  }
  getBoundingClientRect() { return { top: 5000, bottom: 5030 }; }   // (below the window: the stock section)
  scrollIntoView() { this.scrolled++; }
  setAttribute(k, v) { this.attrs[k] = v; }
  removeAttribute(k) { delete this.attrs[k]; }
  focus() { this.focused++; }
  click() { this.clicks++; }                    // (a file input opening its picker)
  addEventListener(type, f) { this.l[type] = f; }
  fire(type) { return this.l[type] ? this.l[type]({ type }) : undefined; }
}
function makeDom() {
  const byId = {}, withT = [];
  for (const m of html.matchAll(/<[a-z][a-z0-9]*\b([^>]*)>/g)) {   // (h2, h3 too)
    const a = m[1], attrs = {};
    const id = /\sid="([^"]+)"/.exec(a), t = /\sdata-t="([^"]+)"/.exec(a);
    if (!id && !t) continue;
    if (id) attrs.id = id[1];
    if (t) attrs.t = t[1];
    if (/\sdisabled(\s|=|$)/.test(a)) attrs.disabled = true;
    if (/\shidden(\s|=|$)/.test(a)) attrs.hidden = true;
    const el = new El(attrs);
    if (id) byId[id[1]] = el;
    if (t) withT.push(el);
  }
  return { byId, withT };
}
const realSleep = (ms) => new Promise((r) => setTimeout(r, ms));
// the Updater's waits (2 s, 3 s, 1 s polls) at a tenth: the simulated FM-1 answers in milliseconds
const fastTimeout = (f, ms = 0, ...a) => setTimeout(f, ms >= 100 ? ms / 10 : ms, ...a);

function runPage({ navigator = {}, fetch, confirm = () => true, prompt = () => null, prelude = "", extra = {}, page = code } = {}) {
  const { byId, withT } = makeDom();
  const win = { l: {}, innerHeight: 800, addEventListener(type, f) { this.l[type] = f; } };
  const confirms = [], downloads = [], prompts = [];
  const document = {
    documentElement: { lang: "" },
    body: { append() {} },
    createElement: (tag) => ({ tag, href: "", download: "", remove() {}, click() { downloads.push({ name: this.download, href: this.href }); } }),
    getElementById: (id) => byId[id] || null,
    querySelectorAll: (sel) => { if (sel !== "[data-t]") throw new Error(`querySelectorAll(${sel})`); return withT; },
  };
  const ctx = {
    document, window: win, navigator, fetch: fetch || (async () => { throw new Error("fetch not expected"); }),
    confirm: (msg) => { confirms.push(msg); return confirm(msg); },
    prompt: (msg, def) => { prompts.push(msg); return prompt(msg, def); },
    URL, Blob, crypto: globalThis.crypto, TextEncoder, TextDecoder, console, btoa, atob, DataView,
    setTimeout: fastTimeout, clearTimeout, setInterval, clearInterval, ...extra,
  };
  const src = prelude ? page.replace(lib, () => lib + "\n" + prelude) : page;
  let error = null;
  try { vm.runInNewContext(src, ctx, { filename: "installer.js" }); } catch (e) { error = e; }
  return { $: byId, withT, win, document, confirms, downloads, prompts, error };
}

/* a simulated FM-1 on WebMIDI (test_web.mjs's FakeFM1 with the identities as parameters) */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
class FakeFM1 {
  constructor(image, { identity = "FM-1_015", loader = "ota-FM-1_920", final = "FM-1_920", onServe = null, bk = null } = {}) {
    this.image = image; this.served = 0; this.bad = 0; this.upgrades = 0; this.bkFrames = 0;
    this.loader = loader; this.final = final; this.onServe = onServe; this.bk = bk;   // bk(identity): its backup side, or null
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, identity.startsWith("ota-") ? "Felucca Update" : "FM-1");
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
    if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x4C) {   // the backup protocol (fm1backup.js)
      this.bkFrames++;
      const side = this.bk && this.bk(this.identity), r = side && side.handle(d[4], d.slice(5, -1));
      if (r) this.tx([0xF0, 0x7D, 0x46, 0x4C, d[4], ...r, 0xF7]);
      return;
    }
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.upgrades++;
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
      if (this.onServe) this.onServe(this.served);
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.final, "FM-1"), 300);
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

// a firmware's backup side (cr_backup.c / Felucca's editor_backup.c, as the protocol says): INFO, LIST, GET, PUT, SMP, RESTART
function backupSide(version, ids, objs = []) {
  const b = { objs: new Map(objs), log: [], staged: null, restarts: 0 };
  b.handle = (cmd, a) => {
    if (cmd === 1) return [...new TextEncoder().encode(version), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3];
    if (cmd === 65) {
      const out = [1, 0, ids.length];
      for (const id of ids) { const v = b.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === 66) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = b.objs.get(id);
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === 67) {
      const [op, id] = a;
      if (op === 0) { b.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, ids.includes(id) ? 0 : 1]; }
      if (op === 1) { b.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, b.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) { const v = Uint8Array.from(b.staged.bytes); const rc = bkCrc(v) === b.staged.crc ? 0 : 2; if (!rc) { b.objs.set(id, v); b.log.push(id); } return [op, id, rc]; }
      return [op, id, 0];
    }
    if (cmd >= 11 && cmd <= 14) { if (cmd >= 13) b.log.push(32 + a[0]); return cmd === 12 ? [a[0], a[1], a[2], a[3], 0] : [a[0], 0]; }
    if (cmd === 72 && /^ChoralRoot/.test(version)) { b.restarts++; return [0]; }
    if (cmd === 76 && /^ChoralRoot/.test(version)) { b.selected = a.slice(0, 3); return a.length === 3 && a[0] <= 1 ? [0] : null; }   // SELECT (part, engine, position)
    return null;
  };
  return b;
}
const fill = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed) & 255);
const per = (n, magic, seed) => { const v = fill(n, seed); new DataView(v.buffer).setUint32(0, magic, true); return v; };
const feluccaData = () => [[0, fill(3584, 1)], [1, per(572, 0x50455234, 2)], [2, fill(3584, 3)], [6, fill(3080, 4)], [8, fill(3472, 5)]];
const crData = () => [[1, per(764, 0x50455235, 6)], [6, fill(3080, 7)], [9, fill(3536, 8)], [40, fill(46, 9)]];

const pkgFetch = (raw) => async (url) => (url === META.pkg
  ? { ok: true, status: 200, arrayBuffer: async () => raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length) }
  : { ok: false, status: 404, arrayBuffer: async () => new ArrayBuffer(0) });
const settle = () => realSleep(30);
const midiOf = (dev) => ({ requestMIDIAccess: async () => dev.access });

const image = logicalImage(raw);
ok(productOf(raw) === META.product, "test package: identity FM-1_920");
const built920 = join(HERE, "../build/choralroot.fwsc");
if (existsSync(built920)) ok(productOf(readFileSync(built920)) === META.product, "build/choralroot.fwsc: identity FM-1_920");

// no Web MIDI: the top level runs, the texts are applied, the status says why
{
  const p = runPage({ navigator: {} });
  await settle();
  ok(!p.error, `no Web MIDI: the page's top-level code runs${p.error ? " (" + p.error.message + ")" : ""}`);
  ok(p.$.status.textContent === en.nomidi, "no Web MIDI: status = the nomidi text");
  ok(p.withT.every((e) => e.textContent && !e.textContent.includes("{version}")) &&
     p.withT.find((e) => e.dataset.t === "beta").textContent.startsWith("ChoralRoot 0.1 is a first public beta"),
     "no Web MIDI: every data-t element has its text ({version} = 0.1)");
  ok(p.document.documentElement.lang === "en" && p.$.go.disabled && p.$["stock-go"].disabled, "no Web MIDI: lang en, Install and stock buttons disabled");
}

// package load: status, Install enabled; a foreign package and a missing one are refused
{
  const dev = new FakeFM1(image);
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  ok(!p.error && p.$.status.textContent === "ChoralRoot 0.1 · FM-1_920" && !p.$.go.disabled, "load: status ChoralRoot 0.1 · FM-1_920, Install enabled");

  let guarded = null;
  dev.onServe = (n) => { if (n === 2) { let prevented = false; p.win.l.beforeunload({ preventDefault: () => { prevented = true; }, returnValue: "" }); guarded = prevented && p.$.go.disabled; } };
  const t0 = Date.now();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && p.$.bar.value === 100 && dev.bad === 0 && dev.identity === "FM-1_920",
    `install: stock -> loader -> FM-1_920, status Done, bar 100 (${dev.served} reads, ${Date.now() - t0} ms)`);
  ok(guarded === true, "install: Install locked and the page guarded against closing while writing");
  let after = false; p.win.l.beforeunload({ preventDefault: () => { after = true; }, returnValue: "" });
  ok(!after && !p.$.go.disabled, "install: unlocked afterwards");
  ok(/^start /m.test(p.$.log.textContent) && /^write \d+/m.test(p.$.log.textContent), "install: the log lists the steps");
  ok(p.confirms.length === 1 && p.confirms[0].startsWith(en.noBackupConfirm) && !p.downloads.length && dev.bkFrames > 0,
    "install: the stock firmware does not answer the backup -> asked once to install without one");
}

// install over Felucca: the backup first (a file), then ChoralRoot, then the backup offered back and restored
{
  const fel = backupSide("FELUCCA 1.0", BACKUP_IDS, feluccaData()), cr = backupSide("ChoralRoot 0.1", CR_BACKUP_IDS);
  const dev = new FakeFM1(image, { identity: "FM-1_910", bk: (id) => (id === "FM-1_910" ? fel : id === "FM-1_920" ? cr : null) });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^felucca-backup-\d{8}\.json$/.test(p.downloads[0].name) && p.downloads[0].href.startsWith("blob:"),
    `over Felucca: the backup saved first (${p.downloads[0]?.name})`);
  ok(dev.identity === "FM-1_920" && dev.bad === 0 && p.confirms.length === 1 && p.confirms[0] === en.restoreOffer, "over Felucca: installed, then the restore offered");
  ok(cr.log.join() === "6,7,8,1" && cr.objs.get(1).length === 572 && cr.objs.get(6).every((v, i) => v === feluccaData()[3][1][i]) && cr.restarts === 1,
    "over Felucca: its settings, banks, FM6 bank restored on ChoralRoot (no samples: all-synth), then RESTART");
  ok(p.$.status.textContent.startsWith(en.restored) && p.$.status.textContent.includes("current music") && p.$.status.textContent.includes("project 1"),
    `over Felucca: the status lists what was restored and what stays in the file`);
}
// Skip backup: no backup request at all
{
  const fel = backupSide("FELUCCA 1.0", BACKUP_IDS, feluccaData());
  const dev = new FakeFM1(image, { identity: "FM-1_910", bk: (id) => (id === "FM-1_910" ? fel : null) });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  p.$["skip-backup"].checked = true;
  await p.$.go.fire("click");
  ok(dev.bkFrames === 1 && !p.downloads.length && !p.confirms.length && p.$.status.textContent === en.done,
    "Skip backup: installed with no backup taken (only INFO, to identify the firmware), no question");
}
// no backup possible, declined: nothing written
{
  const dev = new FakeFM1(image);
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw), confirm: () => false });
  await settle();
  await p.$.go.fire("click");
  ok(p.confirms.length === 1 && dev.upgrades === 0 && p.$.status.textContent === en.cancelled && !p.$.go.disabled, "no backup, declined: nothing written, unlocked");
}
// an update of ChoralRoot over ChoralRoot: backed up, no restore offered (its flash stays)
{
  const cr = backupSide("ChoralRoot 0.1", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_920", bk: (id) => (id.startsWith("ota-") ? null : cr) });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^choralroot-backup-/.test(p.downloads[0].name) && !p.confirms.length && !cr.log.length && p.$.status.textContent === en.done,
    "ChoralRoot over ChoralRoot: backed up, no restore offered");
}
// Back up and Restore buttons
{
  const cr = backupSide("ChoralRoot 0.1", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_920", bk: () => cr });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  ok(!p.$["backup-go"].disabled && p.$["restore-go"].disabled, "Back up enabled, Restore waits for a file");
  let blobText = null;
  const realCreate = URL.createObjectURL;
  URL.createObjectURL = (b) => { b.text().then((x) => { blobText = x; }); return "blob:test"; };
  await p.$["backup-go"].fire("click");
  await settle();
  URL.createObjectURL = realCreate;
  const file = blobText && JSON.parse(blobText);
  ok(p.downloads.length === 1 && p.$.status.textContent.startsWith(en.backupSaved) && file && file.firmware === "ChoralRoot 0.1" &&
     file.objects.length === CR_BACKUP_IDS.length, "Back up: the file holds ChoralRoot's 17 objects");
  const blank = backupSide("ChoralRoot 0.1", CR_BACKUP_IDS);
  dev.bk = () => blank;
  p.$["restore-file"].files = [{ text: async () => "{\"format\":\"other\"}" }];
  await p.$["restore-file"].fire("change");
  ok(p.$.status.textContent.startsWith(en.badBackup) && p.$["restore-go"].disabled, "Restore: a file that is not a backup is refused");
  p.$["restore-file"].files = [{ text: async () => blobText }];
  await p.$["restore-file"].fire("change");
  ok(!p.$["restore-go"].disabled, "Restore: a backup file enables Restore");
  await p.$["restore-go"].fire("click");
  ok(p.confirms.at(-1) === en.restoreConfirm && blank.log.at(-1) === 1 && crData().every(([id, v]) => blank.objs.get(id)?.every((b, i) => b === v[i])) &&
     blank.restarts === 1 && p.$.status.textContent.startsWith(en.restored), "Restore: confirmed, every object back, the settings last, RESTART");
}
{
  const p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: pkgFetch(makePackage("FM-1_900")) });
  await settle();
  ok(p.$.status.textContent === en.badpkg && p.$.go.disabled, "load: a package for another identity -> badpkg, Install stays disabled");
}
{
  const p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: async () => ({ ok: false, status: 404 }) });
  await settle();
  ok(p.$.status.textContent.startsWith(en.loadfail) && p.$.status.textContent.includes("HTTP 404"), "load: HTTP 404 -> loadfail with the reason");
}

// resume: the FM-1 is already in update mode
{
  const dev = new FakeFM1(image, { identity: "ota-FM-1_920" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === "FM-1_920", "resume: loader -> FM-1_920, status Done");
  ok(dev.bkFrames === 0 && !p.confirms.length && p.$["force-box"].hidden !== false, "resume: no firmware check, no question (the loader only writes)");
}

/* ------------------------- the firmware check before an install (docs/INSTALL-COMPAT.md) --- */
ok(html.includes('id="force-box" hidden') && html.includes('id="force-risk"'), "guard: the override box is in the page, hidden");
ok(/<details id="dark">/.test(html) && html.includes('href="https://github.com/Quixotic7/MvaveFM1Unbricker"') &&
   html.includes('href="https://github.com/kurogedelic/FM-1-transporter"') && en.darkTitle === "Device not found?" &&
   /WL82 UBOOT1\.00/.test(en.darkText) && /black/.test(en.darkText), "help: \"Device not found?\": the symptom, the recovery repo, the Transporter");
ok(html.includes('data-t="darkRescue"') && /hold OCT- alone and switch it on/.test(en.darkRescue) && /boot stub/.test(en.darkRescue) &&
   /from 0\.16 on; 0\.15 and older do not have it/.test(en.darkRescue), "help: the rescue key (OCT- at power-on), marked as true only from the boot stub on (0.16)");
{
  const refusal = (name) => new Function(`${lib}\nreturn refusalText(${JSON.stringify(name)});`)();
  const over = async (identity, version, { force = false, answer = true, skip = false } = {}) => {
    const side = version ? backupSide(version, BACKUP_IDS, feluccaData()) : null, cr = backupSide("ChoralRoot 0.1", CR_BACKUP_IDS);
    const dev = new FakeFM1(image, { identity, bk: (id) => (id === identity ? side : id === "FM-1_920" ? cr : null) });
    const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw), confirm: () => answer });
    await settle();
    p.$["skip-backup"].checked = skip;
    await p.$.go.fire("click");
    if (force) { p.$["force-risk"].checked = true; await p.$.go.fire("click"); }
    return { p, dev };
  };
  let { p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2");
  const text = refusal("Sloop");
  ok(p.$.status.textContent === text && text.startsWith("Installing over Sloop is not supported: ") &&
     text.includes("Return to the official V15 firmware with the installer you used for Sloop first, then install ChoralRoot.") &&
     text.includes("https://github.com/Quixotic7/MvaveFM1Unbricker"), "guard: over Sloop -> refused with the message and the recovery link");
  ok(dev.upgrades === 0 && !p.downloads.length && !p.confirms.length && p.$["force-box"].hidden === false && !p.$.go.disabled,
    "guard: ... nothing written, no backup, no question; the override box appears");
  ({ p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2", { force: true, answer: false }));
  ok(p.confirms.length === 1 && p.confirms[0] === en.forceConfirm.replace("{name}", "Sloop") && dev.upgrades === 0 &&
     p.$.status.textContent === en.cancelled, "guard: override ticked, the second confirm declined -> nothing written");
  ({ p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2", { force: true }));
  ok(p.confirms[0] === en.forceConfirm.replace("{name}", "Sloop") && dev.identity === "FM-1_920" && dev.bad === 0 &&
     p.$.log.textContent.includes("override: installing over Sloop"), "guard: override ticked and confirmed -> installed over Sloop");
  for (const [identity, version, name] of [["FM-1_909", "FELUCCA 0.9-BETA", "a Felucca beta or a firmware based on one (FELUCCA 0.9-BETA)"],
    ["FM-1_922", "FELUCCA 2.2 BETA", "a Felucca beta or a firmware based on one (FELUCCA 2.2 BETA)"],
    ["FM-1_000", null, "Sloop's rescue mode (FM-1_000)"], ["FM-1_900", null, "an unknown Felucca-based firmware (FM-1_900)"]]) {
    ({ p, dev } = await over(identity, version));
    ok(p.$.status.textContent === refusal(name) && dev.upgrades === 0, `guard: over ${identity} ${version || "(no INFO)"} -> refused`);
  }
  for (const [identity, version] of [["FM-1_910", "FELUCCA v1.0.1"], ["FM-1_90111", "MELODEE v0.11.1"], ["FM-1_920", "ChoralRoot 0.12"], ["FM-1_015", null]]) {
    ({ p, dev } = await over(identity, version, { skip: true }));
    ok(dev.identity === "FM-1_920" && p.$.status.textContent === en.done && p.$["force-box"].hidden !== false,
      `guard: over ${version || "the stock " + identity} -> installed, no override shown`);
  }
}

// errors map to their texts
{
  const p = runPage({ navigator: midiOf({ access: { inputs: new Map(), outputs: new Map() } }), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.notfound && !p.$.go.disabled, "error: no FM-1 -> notfound text, Install unlocked");
}
{
  const p = runPage({ navigator: midiOf(new FakeFM1(image, { identity: "FM-2_001" })), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.model, "error: another device -> model text");
}
{
  const dev = new FakeFM1(image, { final: "FM-1_015" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.mismatch + "FM-1_015", "error: the FM-1 reports another identity -> mismatch text + identity");
}
{
  const p = runPage({ navigator: { requestMIDIAccess: async (o) => { if (o && o.sysex) throw new Error("SecurityError"); return {}; } }, fetch: pkgFetch(raw) });
  await settle();
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.denied && !p.$.go.disabled, "error: SysEx refused -> denied text, Install unlocked");
}

// return to official V15: validateStockPackage stands in for the real file (the official V15 is not in the repo).
// The section reports in its own status line, bar, panels and log (stock-status, stock-bar, stock-done,
// stock-interrupted, stock-log); the install card at the top (status, bar, done, interrupted) is not written to.
{
  const stockImage = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const prelude = "validateStockPackage = async (b) => ({ product: \"FM-1_015\", image: __stockImage, sha256: \"test\" });";
  const v15 = { size: STOCK_V15_SIZE, arrayBuffer: async () => new ArrayBuffer(STOCK_V15_SIZE) };
  const TOP = "ChoralRoot 0.1 · FM-1_920";       // what the install card says when the package is loaded
  const sst = (p) => p.$["stock-status"].textContent;
  const stockPage = (dev, answer) => runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage }, confirm: () => answer });
  const stockRun = async (dev, answer) => {
    const p = stockPage(dev, answer);
    await settle();
    p.$["stock-file"].files = [{ size: 123, arrayBuffer: async () => new ArrayBuffer(123) }];
    await p.$["stock-file"].fire("change");
    const wrong = sst(p).startsWith(en.stockError) && p.$["stock-go"].disabled && p.$.stock.open === true && p.$.status.textContent === TOP;
    p.$.stock.open = false;
    p.$["stock-file"].files = [v15];
    await p.$["stock-file"].fire("change");
    const valid = sst(p) === en.stockValid && !p.$["stock-go"].disabled && p.$.stock.open === true && p.$.status.textContent === TOP;
    await p.$["stock-go"].fire("click");
    return { p, wrong, valid };
  };
  // a probe of the install card: a file that is not a backup, chosen under "Back up and restore", is answered there
  const topAnswers = async (p) => {
    const was = sst(p);
    p.$["restore-file"].files = [{ text: async () => "not json" }];
    await p.$["restore-file"].fire("change");
    return p.$.status.textContent.startsWith(en.badBackup) && sst(p) === was;
  };
  // the bar as the FM-1's answers sample it (one sample per answer; the two "finished" answers move nothing):
  // never back, up at least `min` times, under 100 until the end
  const rises = (b) => b.filter((v, i) => v > (i ? b[i - 1] : 0)).length;
  const climbs = (b, min) => b.every((v, i) => i === 0 || v >= b[i - 1]) && rises(b) >= min && b[b.length - 1] < 100;
  const shown = (b) => b.map((v) => Math.round(v * 10) / 10).join(" ");
  const confirmText = en.stockConfirm || "";
  ok(/backup/.test(confirmText) && /Restore/.test(en.stockWarn || ""), "stock: the warning and the confirm speak of the backup");
  ok(html.indexOf('id="stock-go"') < html.indexOf('id="stock-bar"') && html.indexOf('id="stock-bar"') < html.indexOf('id="stock-status" role="status"') &&
     html.indexOf('id="stock-status"') < html.indexOf('id="stock-done" role="status" hidden') &&
     html.indexOf('id="stock-done"') < html.indexOf('id="stock-interrupted" role="alert" hidden') &&
     html.indexOf('<details class="more" id="stock">') < html.indexOf('id="stock-go"') &&
     html.indexOf('id="stock-log"') < html.indexOf("</details>", html.indexOf('id="stock-go"')) && html.includes("#status, #stock-status {"),
     "stock: its own bar, status line, done panel, guidance and log, in the section, under the button");
  const crSide = () => backupSide("ChoralRoot 0.1", CR_BACKUP_IDS, crData());
  const crDev = () => new FakeFM1(stockImage, { identity: "FM-1_920", final: "FM-1_015", loader: "ota-FM-1_015", bk: (id) => (id === "FM-1_920" ? crSide() : null) });

  let dev = crDev();
  let r = await stockRun(dev, false);
  ok(r.wrong && r.valid, "stock: a file of the wrong size is refused, the V15 (stub) accepted: said in the section, which is opened");
  ok(r.p.downloads.length === 1 && /^choralroot-backup-/.test(r.p.downloads[0].name), "stock: the backup is saved first");
  ok(r.p.confirms.length === 1 && r.p.confirms[0] === confirmText && dev.upgrades === 0 && dev.served === 0 && !r.p.$["stock-go"].disabled,
    "stock: declined -> confirm asked once, nothing written, unlocked");
  ok(sst(r.p) === en.cancelled && r.p.$.status.textContent === TOP && r.p.$["stock-done"].hidden && r.p.$["stock-interrupted"].hidden && r.p.$["stock-bar"].value === 0,
    "stock: declined -> Cancelled in the section, no panel, the install card as it was");
  ok(await topAnswers(r.p), "stock: after a declined confirm the install card answers again (the section's line stays)");

  // the whole run: every message in the section, the bar moving, the install card left idle
  dev = crDev();
  const seen = [];
  let pRun = null;
  dev.onServe = () => seen.push({ loader: dev.identity.startsWith("ota-"), bar: pRun.$["stock-bar"].value, st: sst(pRun), top: pRun.$.status.textContent, topBar: pRun.$.bar.value, open: pRun.$.stock.open });
  pRun = stockPage(dev, true);
  await settle();
  pRun.$["stock-file"].files = [v15];
  await pRun.$["stock-file"].fire("change");
  pRun.$.done.hidden = false; pRun.$.bar.value = 100;   // as after an install of ChoralRoot in this page
  await pRun.$["stock-go"].fire("click");
  r = { p: pRun };
  ok(r.p.confirms.length === 1 && r.p.downloads.length === 1 && sst(r.p) === en.done && dev.bad === 0 && dev.identity === "FM-1_015",
    "stock: confirmed -> backup, ChoralRoot -> loader -> FM-1_015, status Done (in the section)");
  const wr = seen.filter((s) => s.loader && s.st === en.write), bars = seen.map((s) => s.bar);
  ok(wr.length >= 6 && rises(wr.map((s) => s.bar)) >= 5 && seen.some((s) => !s.loader && s.st === en.verify && s.bar > 0) && climbs(bars, 9) &&
     r.p.$["stock-bar"].value === 100, `stock: the bar moves through the check and the write, never back, 100 only at the end (${shown(bars)} 100)`);
  ok(seen.length > 0 && seen.every((s) => s.top === TOP && s.topBar === 0 && s.open === true) && r.p.$.status.textContent === TOP && r.p.$.bar.value === 0 &&
     r.p.$.done.hidden === true && r.p.$.interrupted.hidden === true,
     "stock: during the run nothing is said in the install card, which goes back to idle (its bar 0, its panels hidden)");
  ok(r.p.$["stock-done"].hidden === false && r.p.$["stock-interrupted"].hidden === true && r.p.$.stock.open === true &&
     r.p.$["stock-done-line"].textContent === en.stockDnLine.replace("{identity}", "FM-1_015") &&
     r.p.$["stock-done-data"].textContent === en.stockDnSaved.replace("{file}", r.p.downloads[0].name) &&
     /top of this page/.test(en.stockDnBack) && /Restore/.test(en.stockDnBack) && html.includes('<p data-t="stockDnBack">'),
     "stock: the done panel: the identity the FM-1 answers with, the backup file saved, the way back");
  ok(r.p.$["stock-status"].scrolled === 1 && /^write \d+/m.test(r.p.$["stock-log"].textContent) && /^backup ChoralRoot/m.test(r.p.$["stock-log"].textContent) &&
     r.p.$.log.textContent.includes(r.p.$["stock-log"].textContent),
     "stock: the status line is scrolled into view at the start; the section shows the run's log lines (the main log has them too)");
  r.p.$.stock.open = false; r.p.$.stock.fire("toggle");
  ok(r.p.$.stock.open === true, "stock: the section cannot be folded over its done panel");
  // an install of ChoralRoot after the return reports in the install card again
  dev.onServe = null; dev.image = image; dev.loader = "ota-FM-1_920"; dev.final = "FM-1_920";
  await r.p.$.go.fire("click");
  ok(r.p.$.status.textContent === en.done && r.p.$.bar.value === 100 && r.p.$.done.hidden === false && dev.identity === "FM-1_920" && dev.bad === 0 &&
     sst(r.p) === en.done && r.p.$["stock-bar"].value === 100 && !/^verify/m.test(r.p.$["stock-log"].textContent.split("done FM-1_015")[1] || "x"),
     "stock: an install of ChoralRoot afterwards reports in the install card (status, bar, done panel), not in the section");

  // interrupted while writing: the guidance in the section; the resume box and the same file finish it
  dev = crDev();
  dev.onServe = () => { if (dev.identity.startsWith("ota-") && dev.served >= 6) dev.input.state = dev.output.state = "disconnected"; };
  r = await stockRun(dev, true);
  const barAt = r.p.$["stock-bar"].value;
  ok(sst(r.p) === en.lost && r.p.$["stock-interrupted"].hidden === false && r.p.$["stock-done"].hidden === true && !r.p.$["stock-go"].disabled && barAt > 6 && barAt < 100 &&
     r.p.$.status.textContent === TOP && r.p.$.interrupted.hidden === true && r.p.$.stock.open === true,
     "stock interrupted: the cable lost while writing -> said in the section with its guidance; the install card's guidance stays hidden");
  ok(/nothing was written/.test(en.stockIntEarly) && /update mode/.test(en.stockIntOn) && en.stockIntOn.includes(en.stockRecovery.split(":")[0]) && /same official file/.test(en.stockIntOn) &&
     html.includes('<span data-t="stockIntOff"></span> <a href="#dark" data-t="darkTitle">'),
     "stock interrupted: the guidance names the resume box and the same file, and the way to the help");
  ok(await topAnswers(r.p), "stock: after an error the install card answers again");
  dev.onServe = null; dev.boot("ota-FM-1_015", "Felucca Update");
  r.p.$["stock-recovery"].checked = true;
  await r.p.$["stock-go"].fire("click");
  ok(sst(r.p) === en.done && dev.identity === "FM-1_015" && r.p.$["stock-interrupted"].hidden === true && r.p.$["stock-done"].hidden === false &&
     r.p.$["stock-bar"].value === 100 && r.p.$["stock-done-data"].textContent === en.stockDnEarlier,
     "stock interrupted: the resume box ticked, the button again -> FM-1_015, the done panel (no backup in this run: it says so)");

  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRun(dev, true);
  ok(r.p.confirms.length === 0 && dev.upgrades === 0 && sst(r.p) === en.stockNeedBackup && r.p.$["stock-interrupted"].hidden, "stock resume (in update mode) without the resume box: nothing written");
  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  const stockRunR = async (d, answer) => {
    const p = stockPage(d, answer);
    await settle();
    p.$["stock-file"].files = [v15];
    await p.$["stock-file"].fire("change");
    p.$["stock-recovery"].checked = true;
    d.onServe = () => { (p.bars = p.bars || []).push(p.$["stock-bar"].value); };
    await p.$["stock-go"].fire("click");
    return { p };
  };
  r = await stockRunR(dev, false);
  ok(r.p.confirms.length === 1 && dev.upgrades === 0 && sst(r.p) === en.cancelled, "stock resume (box ticked): declined -> nothing written, Cancelled");
  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRunR(dev, true);
  ok(r.p.confirms.length === 1 && sst(r.p) === en.done && dev.identity === "FM-1_015" && r.p.$.status.textContent === TOP, "stock resume (box ticked): confirmed -> FM-1_015, status Done");
  ok(climbs(r.p.bars, 6) && r.p.$["stock-bar"].value === 100 && r.p.$["stock-done"].hidden === false,
     `stock resume: the bar moves the same way (${shown(r.p.bars)} 100), and the done panel`);

  const none = runPage({ navigator: midiOf({ access: { inputs: new Map(), outputs: new Map() } }), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage } });
  await settle();
  none.$["stock-file"].files = [v15];
  await none.$["stock-file"].fire("change");
  await none.$["stock-go"].fire("click");
  ok(sst(none) === en.notfound && none.confirms.length === 0 && none.$.status.textContent === TOP && none.$["stock-interrupted"].hidden === true,
    "stock: no FM-1 -> notfound before any confirm, in the section, without the guidance (nothing was started)");
  ok(await topAnswers(none), "stock: after notfound the install card answers again");
  const refused = runPage({ navigator: { requestMIDIAccess: async () => { throw new Error("SecurityError"); } }, fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage } });
  await settle();
  refused.$["stock-file"].files = [v15];
  await refused.$["stock-file"].fire("change");
  await refused.$["stock-go"].fire("click");
  ok(sst(refused) === en.denied && refused.$.status.textContent === TOP && !refused.$["stock-go"].disabled && await topAnswers(refused),
    "stock: MIDI refused -> denied in the section, unlocked, and the install card answers again");
}

/* ----------------------------------------------- the Sounds section (docs/SOUNDS.md) --- */
{
  const dv = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
  const rec = (engine, name, seed) => {
    const r = new Uint8Array(192); r[0] = 0xA5; r[1] = 4; r[2] = engine; r[3] = 40;
    for (let i = 0; i < name.length; i++) r[4 + i] = name.charCodeAt(i);
    for (let i = 0; i < 144; i++) r[16 + i] = (i * 7 + seed) % 192;
    return r;
  };
  const bank = (recs) => {
    const b = new Uint8Array(3080); dv(b).setUint32(0, 0x31425055, true); dv(b).setUint16(4, 192, true); dv(b).setUint16(6, 16, true);
    for (const [i, r] of Object.entries(recs)) b.set(r, 8 + Number(i) * 192);
    return b;
  };
  const store = (magic, ver, nslot, size, first, blob, blobs) => {   // VA (first null) or an FM6 / CZ-1 half
    const b = new Uint8Array(size), v = dv(b); let used = 0;
    v.setUint32(0, magic, true); v.setUint16(4, ver, true); v.setUint16(6, nslot, true);
    if (first === null) v.setUint16(12, blob, true); else { v.setUint16(8, first, true); v.setUint16(10, blob, true); }
    for (const [k, data] of Object.entries(blobs)) { b.set(data, 16 + Number(k) * blob); used |= 1 << Number(k); }
    v.setUint32(first === null ? 8 : 12, used >>> 0, true);
    return b;
  };
  const vaBlob = Uint8Array.from({ length: 110 }, (_, i) => (i ? (i * 5) & 127 : 0x56)); vaBlob[1] = 3;
  const fm6Blob = Uint8Array.from({ length: 128 }, (_, i) => (i * 3) & 127); fm6Blob[112] = 0x46; fm6Blob[113] = 1;
  const czBlob = Uint8Array.from({ length: 144 }, (_, i) => (i * 11) & 255);
  const soundData = () => [
    [1, per(764, 0x50455235, 6)], [6, bank({ 0: rec(13, "MY PAD", 1), 2: rec(0, "BASS", 2) })], [7, bank({ 1: rec(14, "CZ BELL", 3) })],
    [9, store(0x31534156, 3, 32, 3536, null, 110, { 0: vaBlob })], [10, store(0x55364D46, 1, 16, 2064, 0, 128, {})],
    [11, store(0x55364D46, 1, 16, 2064, 16, 128, { 5: fm6Blob })], [12, store(0x55315A43, 1, 16, 2320, 0, 144, {})],
    [13, store(0x55315A43, 1, 16, 2320, 16, 144, { 1: czBlob })],
  ];
  const cr = backupSide("ChoralRoot 0.14", CR_BACKUP_IDS, soundData());
  const dev = new FakeFM1(image, { identity: "FM-1_920", bk: () => cr });
  let answer = true, name = null;
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw), confirm: () => answer, prompt: () => name });
  await settle();
  const act = (a, slot) => p.$["snd-rows"].l.click({ target: { closest: (sel) => (sel === "button[data-act]" ? { dataset: { act: a, slot: String(slot) }, disabled: false } : null) } });
  ok(!p.$["snd-read"].disabled && p.$["snd-table"].hidden === true, "sounds: Read sounds enabled, the table hidden until a read");

  await p.$["snd-read"].fire("click");
  const rows = () => p.$["snd-rows"].innerHTML;
  ok(p.$["snd-table"].hidden === false && (rows().match(/<tr/g) || []).length === 32 && rows().includes(">U01<") && rows().includes(">U32<") &&
     rows().includes("MY PAD") && rows().includes("BASS") && rows().includes("CZ BELL") && rows().includes(en.sndEmpty) && !cr.log.length,
     "sounds: Read sounds -> 32 rows (U01..U32) with the names, empty slots marked, nothing written");
  ok(p.$.status.textContent === en.sndRead.replace("{n}", "3") && !p.$["snd-read"].disabled && !/disabled/.test(rows().split("</tr>")[0]),
     `sounds: status "${p.$.status.textContent}", unlocked`);

  let blobText = null;
  const realCreate = URL.createObjectURL;
  URL.createObjectURL = (b) => { b.text().then((x) => { blobText = x; }); return "blob:sound"; };
  await act("export", 1);
  await settle();
  URL.createObjectURL = realCreate;
  let sound = null;
  try { sound = readSoundFile(blobText); } catch (e) { console.log(String(e)); }
  ok(p.downloads.at(-1)?.name === "choralroot-sound-U01-MY_PAD.json" && sound && sound.name === "MY PAD" && sound.engine === 13 &&
     sound.patch && sound.patch.length === 110 && p.$.status.textContent === en.sndExported + "choralroot-sound-U01-MY_PAD.json" && !cr.log.length,
     "sounds: Export U01 -> a download readSoundFile accepts (VA with its patch), nothing written");

  name = "NEW BASS";
  await act("rename", 3);
  ok(cr.log.join() === "6" && rows().includes("NEW BASS") && p.prompts.at(-1) === en.sndRenamePrompt.replace("{slot}", "U03") &&
     p.$.status.textContent === en.sndRenamed + "U03: NEW BASS", "sounds: Rename U03 -> only bank 0 written, the table read again");
  name = "THIS IS TOO LONG"; cr.log.length = 0;
  await act("rename", 3);
  ok(!cr.log.length && p.$.status.textContent === en.sndBadName, "sounds: a 16-character name refused, nothing written");

  const confirms0 = p.confirms.length;
  await act("import", 5);
  ok(p.$["snd-file"].clicks === 1, "sounds: Import opens the file picker for the row");
  p.$["snd-file"].files = [{ text: async () => blobText }];
  await p.$["snd-file"].fire("change");
  ok(cr.log.join() === "9,6" && p.confirms.length === confirms0 && parseSoundObjects(cr.objs).slots[4].name === "MY PAD" &&
     parseSoundObjects(cr.objs).slots[4].patch === "va" && p.$.status.textContent === en.sndImported + "U05: MY PAD",
     "sounds: Import into U05 (empty): no question, the VA store then bank 0 written");

  cr.log.length = 0; answer = false;
  await act("import", 18);
  p.$["snd-file"].files = [{ text: async () => blobText }];
  await p.$["snd-file"].fire("change");
  ok(!cr.log.length && p.confirms.at(-1).startsWith("U18 holds \"CZ BELL\"") && p.$.status.textContent === en.cancelled,
     "sounds: Import over U18 (used): asked, declined -> nothing written");
  answer = true;
  await p.$["snd-file"].fire("change");
  ok(cr.log.join() === "9,13,7" && parseSoundObjects(cr.objs).slots[17].name === "MY PAD" && parseSoundObjects(cr.objs).slots[17].patch === "va",
     "sounds: ... confirmed -> the VA store, the CZ-1 half cleared, then bank 1");

  cr.log.length = 0;
  p.$["snd-file"].files = [{ text: async () => JSON.stringify({ format: "felucca-backup", version: 1, objects: [] }) }];
  await p.$["snd-file"].fire("change");
  ok(!cr.log.length && p.$.status.textContent.startsWith(en.sndBadFile) && /whole backup/.test(p.$.status.textContent),
     "sounds: a backup file chosen for Import -> refused (a whole backup), nothing written");

  await act("delete", 1);
  ok(cr.log.join() === "9,6" && !parseSoundObjects(cr.objs).slots[0].used && p.confirms.at(-1).startsWith("Delete U01 \"MY PAD\"") &&
     p.$.status.textContent === en.sndDeleted + "U01" && same(cr.objs.get(1), soundData()[0][1]),
     "sounds: Delete U01 (confirmed) -> the VA store then the bank; the settings untouched");
  ok(!p.$.go.disabled && !p.$["backup-go"].disabled && !p.$["snd-read"].disabled, "sounds: the installer unlocked afterwards");
}
{
  const fel = backupSide("FELUCCA 1.0", BACKUP_IDS, feluccaData());
  const p = runPage({ navigator: midiOf(new FakeFM1(image, { identity: "FM-1_910", bk: () => fel })), fetch: pkgFetch(raw) });
  await settle();
  await p.$["snd-read"].fire("click");
  ok(p.$.status.textContent === en.sndNeedCr && p.$["snd-table"].hidden === true && !fel.log.length && !p.$["snd-read"].disabled,
     "sounds: on Felucca -> the status says the Sounds need ChoralRoot, nothing read or written");
}

{
  // .syx (docs/SOUNDS.md ".syx export and import"): the fixtures of tests/sound_templates.c (factory F1 TINE EP's blob,
  // Casio's A-1 BRASS 1)
  ok(html.includes('id="snd-file" type="file" accept=".json,.syx,application/json"') && html.includes('"export-syx"') &&
     ["exportSyx", "parseSyx", "soundFromSyx", "syxCount", "saveBytes"].every((f) => html.includes(f)),
     "page: Export .syx on the rows, the Import picker accepts .json and .syx");
  ok(["sndExportSyx", "sndSyxText", "sndSyxSaved", "sndSyxPick", "sndSyxBadPick"].every((k) => en[k]) && dataT.includes("sndSyxText") &&
     en.sndExportSyx === "Export .syx", "texts: the .syx button, its section text, the statuses and the voice prompt");
  const B = (x) => Uint8Array.from(Buffer.from(x, "base64"));
  const fBlob = B("XygePGNQACeAAAA4DDQAXx4UPGNaAKeAgAAwhAIA4b6ovGMAACeAgAA7xJwAXxSUsl+AACcAAIAI2oKAXzKjY0sAACcAALscOoKAYBlDY0sAACcAgLsIYgKA4+PjMrIyMgSiIQCAKRjUTkUgRVCgIEYBMwBgHAAAAEAAAAAAAAA="), fTone = B("CgEUAAgAAAAy4AkAAQCgIAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQAQV0h1wBAAEAAQABAAEAAQACgAAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQA8V0h1wBAAEAAQABAAEAAQAAgICAgQlJBU1MgMSAgICAg");
  const dv = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
  const rec = (engine, name) => {
    const r = new Uint8Array(192); r[0] = 0xA5; r[1] = 4; r[2] = engine; r[3] = 40;
    for (let i = 0; i < name.length; i++) r[4 + i] = name.charCodeAt(i);
    return r;
  };
  const bank = (recs) => {
    const b = new Uint8Array(3080); dv(b).setUint32(0, 0x31425055, true); dv(b).setUint16(4, 192, true); dv(b).setUint16(6, 16, true);
    for (const [i, r] of Object.entries(recs)) b.set(r, 8 + Number(i) * 192);
    return b;
  };
  const half = (magic, first, blob, blobs) => {
    const b = new Uint8Array(16 + 16 * blob), v = dv(b); let used = 0;
    v.setUint32(0, magic, true); v.setUint16(4, 1, true); v.setUint16(6, 16, true); v.setUint16(8, first, true); v.setUint16(10, blob, true);
    for (const [k, data] of Object.entries(blobs)) { b.set(data, 16 + Number(k) * blob); used |= 1 << Number(k); }
    v.setUint32(12, used, true);
    return b;
  };
  const va = new Uint8Array(3536); dv(va).setUint32(0, 0x31534156, true); dv(va).setUint16(4, 3, true); dv(va).setUint16(6, 32, true); dv(va).setUint16(12, 110, true);
  const data = [[1, per(764, 0x50455235, 6)], [6, bank({ 1: rec(12, "TINE 2"), 2: rec(14, "HORNS"), 3: rec(0, "BASS") })], [7, new Uint8Array(0)],
    [9, va], [10, half(0x55364D46, 0, 128, { 1: fBlob })], [11, new Uint8Array(0)],
    [12, half(0x55315A43, 0, 144, { 2: fTone })], [13, new Uint8Array(0)]];
  const cr = backupSide("ChoralRoot 0.14", CR_BACKUP_IDS, data);
  let pickAnswer = "2";
  const p = runPage({ navigator: midiOf(new FakeFM1(image, { identity: "FM-1_920", bk: () => cr })), fetch: pkgFetch(raw),
    prompt: () => pickAnswer });
  await settle();
  await p.$["snd-read"].fire("click");
  const rowOf = (n) => p.$["snd-rows"].innerHTML.split("</tr>")[n - 1];
  ok(/data-act="export-syx"[^>]*>Export \.syx</.test(rowOf(2)) && rowOf(3).includes("export-syx") && !rowOf(4).includes("export-syx") &&
     !rowOf(5).includes("export-syx"), "syx: Export .syx on the FM6 and CZ-1 rows with a patch only (not ANALOG, not empty)");
  const act = (a, slot) => p.$["snd-rows"].l.click({ target: { closest: (sel) => (sel === "button[data-act]" ? { dataset: { act: a, slot: String(slot) }, disabled: false } : null) } });
  let blob = null;
  const realCreate = URL.createObjectURL;
  URL.createObjectURL = (b) => { blob = b; return "blob:syx"; };
  await act("export-syx", 2);
  await settle();
  URL.createObjectURL = realCreate;
  const got = blob ? new Uint8Array(await blob.arrayBuffer()) : new Uint8Array(0);
  ok(p.downloads.at(-1)?.name === "choralroot-sound-U02-TINE_2.syx" && got.length === 163 && got[0] === 0xF0 && got[1] === 0x43 && got[162] === 0xF7 &&
     eq(parseSyx(got).voices[0], fm6BlobToVced(fBlob)) && p.$.status.textContent === en.sndSyxSaved + "choralroot-sound-U02-TINE_2.syx" && !cr.log.length,
     "syx: Export .syx on U02 (FM6) -> a 163-byte DX7 voice download, nothing written");
  blob = null;
  URL.createObjectURL = (b) => { blob = b; return "blob:syx"; };
  await act("export-syx", 3);
  await settle();
  URL.createObjectURL = realCreate;
  const gotCz = blob ? new Uint8Array(await blob.arrayBuffer()) : new Uint8Array(0);
  ok(gotCz.length === 295 && eq(parseSyx(gotCz).tones[0], fTone) && p.downloads.at(-1)?.name === "choralroot-sound-U03-HORNS.syx",
     "syx: Export .syx on U03 (CZ-1) -> a 295-byte tone dump");

  const syxFile = (bytes) => [{ text: async () => { throw new Error("text() of a .syx"); }, arrayBuffer: async () => bytes.slice().buffer }];
  const confirms0 = p.confirms.length;
  await act("import", 5);
  p.$["snd-file"].files = syxFile(got);
  await p.$["snd-file"].fire("change");
  const t5 = parseSoundObjects(cr.objs).slots[4];
  ok(cr.log.join() === "10,6" && p.confirms.length === confirms0 && t5.name === "TINE EP" && t5.engine === 12 && t5.patch === "fm6" &&
     eq(cr.objs.get(10).subarray(16 + 4 * 128, 16 + 5 * 128), fBlob) && p.$.status.textContent === en.sndImported + "U05: TINE EP",
     "syx: a .syx imported into U05 (empty): the FM6 store, then bank 0; named from the voice");

  // two voices in one file: the prompt asks which (2: the renamed one)
  cr.log.length = 0;
  const v2 = fm6BlobToVced(fBlob); "SECOND".padEnd(10).split("").forEach((c, i) => { v2[145 + i] = c.charCodeAt(0); });
  const two = Uint8Array.from([...got, ...fm6VcedSyx(v2)]), prompts0 = p.prompts.length;
  await act("import", 6);
  p.$["snd-file"].files = syxFile(two);
  await p.$["snd-file"].fire("change");
  ok(p.prompts.length === prompts0 + 1 && p.prompts.at(-1) === en.sndSyxPick.split("{n}").join("2").replace("{slot}", "U06") &&
     cr.log.join() === "10,6" && parseSoundObjects(cr.objs).slots[5].name === "SECOND", "syx: a two-voice file asks which voice (2 -> SECOND)");
  cr.log.length = 0; pickAnswer = "3";
  await p.$["snd-file"].fire("change");
  ok(!cr.log.length && p.$.status.textContent === en.sndSyxBadPick.replace("{n}", "2"), "syx: a voice number out of range -> nothing written");
  const cz101 = Uint8Array.from([0xF0, 0x44, 0, 0, 0x70, 0x30, ...new Array(256).fill(1), 0xF7]);
  p.$["snd-file"].files = syxFile(cz101);
  await p.$["snd-file"].fire("change");
  ok(!cr.log.length && p.$.status.textContent.startsWith(en.sndBadFile) && /CZ-101/.test(p.$.status.textContent),
     "syx: a CZ-101 tone refused with the message, nothing written");
  ok(!p.$["snd-read"].disabled && !p.$["backup-go"].disabled, "syx: the installer unlocked afterwards");
}

/* ------- one page: Install at once, what is new, the other versions of web/releases.json, going back, demo mode --- */
{
  // the page with the metadata make_site.py writes for the real list: every version of web/releases.json, each with a
  // package of its own here (same identity, different bytes) and that package's sha256
  const list = JSON.parse(readFileSync(join(HERE, "releases.json"), "utf8"));
  const vers = list.releases.map((r) => r.version), newest = vers[0], older = vers[2] || vers[vers.length - 1];
  const RAW = Object.fromEntries(vers.map((v, i) => [v, makePackage("FM-1_920", 17 * (i + 1))]));
  const pkgOf = (v) => `../../firmware/choralroot-${v}.fwsc`;
  const metaFor = async (tamper = null) => ({ version: newest, product: "FM-1_920", pkg: pkgOf(newest), repo: list.repo, minimum: list.minimum,
    releases: await Promise.all(list.releases.map(async (r) => ({ ...r, pkg: pkgOf(r.version), size: RAW[r.version].length,
      sha256: r.version === tamper ? "0".repeat(64) : await sha256(RAW[r.version]) }))) });
  const pageFor = (meta) => scriptOf(html.split("/*LIB*/").join(lib).split("/*META*/").join(JSON.stringify(meta)));
  const page = pageFor(await metaFor());
  const fetched = [];
  const listFetch = (over = {}) => async (url) => {
    fetched.push(url);
    const v = vers.find((x) => pkgOf(x) === url), b = v && (over[v] || RAW[v]);
    return b ? { ok: true, status: 200, arrayBuffer: async () => b.buffer.slice(b.byteOffset, b.byteOffset + b.length) }
      : { ok: false, status: 404, arrayBuffer: async () => new ArrayBuffer(0) };
  };
  // a click on the "Other versions" button of v (the page finds it with closest("[data-ver]"))
  const choose = (p, v) => p.$.versions.l.click({ target: { closest: (sel) => (sel === "[data-ver]" ? { dataset: { ver: v }, disabled: false } : null) } });
  const unesc = (s) => s.replace(/&quot;/g, "\"").replace(/&gt;/g, ">").replace(/&lt;/g, "<").replace(/&amp;/g, "&");
  const crSide = (v) => backupSide("ChoralRoot " + v, CR_BACKUP_IDS, crData());
  const label = (v) => en.installV.replace("{version}", v);
  const rel = (v) => list.releases.find((r) => r.version === v);
  const chips = (p) => [...p.$.versions.innerHTML.matchAll(/data-ver="([^"]+)" aria-pressed="(true|false)"/g)].map((m) => m[1] + (m[2] === "true" ? "*" : "")).join();

  // the page's shape: no steps; the button in the page itself, before the notes and every other section
  ok(!/id="step-|id="tab-|data-go=|class="strip"|class="step |id="flow"|class="nav"|kicker/.test(html) &&
     !/showStep|stepDone|curStep|\bseen\b|tab\d|kick\d|next\d/.test(scriptOf(html)), "page: no step strip, no steps, no Next buttons, no step machinery in the script");
  ok(count(html, 'id="go"') === 1 && html.indexOf('id="go"') < html.indexOf('id="notes"') && html.indexOf('id="rel"') < html.indexOf('id="plan-back"') && html.indexOf('id="plan-back"') < html.indexOf('id="go"') &&
     html.indexOf('id="go"') < html.indexOf('id="versions"') && html.indexOf('id="versions"') < html.indexOf('id="dark"') &&
     html.indexOf('id="dark"') < html.indexOf('data-t="bkTitle"') && html.indexOf('id="sounds"') < html.indexOf('data-t="stockTitle"') &&
     html.indexOf('data-t="stockTitle"') < html.indexOf('id="files"'),
     "page: the version, Install, Other versions, then Device not found?, Back up and restore, Sounds, Return to official V15, The files");
  ok(["rel", "others", "versions", "not-newest", "plan-back", "newest-go", "check-go", "unit", "notes", "interrupted", "done", "done-line", "files", "demo-note", "nomidi"]
     .every((id) => html.includes(`id="${id}"`)) && html.includes('id="interrupted" role="alert" hidden') && html.includes('id="demo-note" hidden') &&
     html.includes('id="not-newest" hidden') && html.includes('id="done" role="status" hidden') && html.includes('id="nomidi" hidden'),
     "page: the going-back note, the interrupted box, the done box and the browser notice are in the page, hidden");
  ok(/\[hidden\]\s*\{\s*display:\s*none\s*!important/.test(html), "page: [hidden] wins over a class that sets display (the override box)");
  const src = scriptOf(html);
  ok(count(src, "requestMIDIAccess({") === 1 && count(src, "await midiAccess()") === 6 && /if \(DEMO\) throw new Error/.test(src) &&
     count(src, "fetch(") === 1 && src.includes("await fetch(rel.pkg)") && !/XMLHttpRequest|sendBeacon|WebSocket|EventSource|importScripts|import\(/.test(src),
     "page: one way to the browser's MIDI (refused in demo mode), one fetch (the chosen version's package)");
  const tvKeys = [...src.matchAll(/\btv\(([^)]*?)[,)]/g)].flatMap((m) => [...m[1].matchAll(/"([A-Za-z0-9]+)"/g)].map((x) => x[1]));
  ok(tvKeys.length >= 12 && tvKeys.every((k) => typeof en[k] === "string"), `texts: every key tv() fills has a text (${tvKeys.length}${tvKeys.filter((k) => typeof en[k] !== "string")})`);
  const used = new Set([...dataT, ...sayKeys, ...tfKeys, ...tvKeys, ...codes, "restoreSkipped", "noBackup", "install"]);
  const unused = Object.keys(en).filter((k) => !used.has(k) && !new RegExp(`\\b${k}\\b`).test(src.replace(/const TEXT = \{[\s\S]*?\n\};/, "")));
  ok(!unused.length, `texts: no text is left that the page does not use (${unused})`);
  ok(/saved sounds, loops and settings/.test(en.kept) && /not saved is not kept/.test(en.kept) && /hold SAVE/.test(en.kept) && html.includes('data-t="kept"') &&
     /backup/.test(en.backupFirst) && /SAFE MODE/.test(en.darkSafe) && html.includes('data-t="darkSafe"') && /OCT-, OCT\+ and HOME/.test(en.darkGesture) &&
     html.includes('data-t="darkGesture"'), "texts: what an update keeps and what it does not; the SAFE MODE and update-gesture hints");
  ok(/Chrome or Edge/.test(en.nomidiNote) && /Safari and Firefox cannot/.test(en.nomidiNote) && /Android/.test(en.nomidiNote) && /not tested/.test(en.nomidiNote),
     "texts: the notice for a browser that cannot install names the ones that can (Android as untested)");

  // Install at once: no click before it; the newest is what it writes, with its notes beside it
  {
    fetched.length = 0;
    const before = crSide(vers[1]), after = crSide(newest);
    let done = false;
    const dev = new FakeFM1(logicalImage(RAW[newest]), { identity: "FM-1_920", bk: (id) => (id.startsWith("ota-") ? null : done ? after : before) });
    const boot = dev.boot.bind(dev);
    dev.boot = (id, name) => { if (!id.startsWith("ota-")) done = true; boot(id, name); };
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    ok(!p.error, `versions: the page runs with the real list${p.error ? " (" + p.error.stack + ")" : ""}`);
    const n = unesc(p.$.notes.innerHTML), v = p.$.versions.innerHTML;
    ok(!p.$.go.disabled && p.$.go.textContent === label(newest) && p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920` && fetched.join() === pkgOf(newest) &&
       p.$.nomidi.hidden === true && p.$["not-newest"].hidden === true && p.$.done.hidden === true && p.$.unit.hidden === true,
       `at once: Install is enabled with no click before it and says "${p.$.go.textContent}"; only the newest package was fetched`);
    ok(p.$.rel.innerHTML.includes(`<b>ChoralRoot ${newest}</b> · ${dateOf(rel(newest).date)}`) && p.$.rel.innerHTML.includes(en.chNewest) && !p.$.rel.innerHTML.includes(en.chOld),
       `what is new: "ChoralRoot ${newest} · ${dateOf(rel(newest).date)}" over the button, marked Newest`);
    ok(n.includes(`<h2>${en.chNew.replace("{version}", newest)}</h2>`) && rel(newest).bullets.every((b) => n.includes(b)) && rel(newest).notes.every((b) => n.includes(b)) &&
       n.includes(`/docs/releases/${newest}.md"`) && n.includes(en.chNotes) && !n.includes("<details") && !n.includes(en.chBackFull),
       `what is new: "New in ${newest}" with every bullet of web/releases.json at once, and the link to the full notes`);
    ok(chips(p) === vers.slice(1).join() && vers.slice(1).every((x) => v.includes(`<b>${x}</b><span>${dateOf(rel(x).date)}</span>`)) && p.$.others.hidden === false &&
       !v.includes(`data-ver="${newest}"`) && html.includes('data-t="chOthers"') && en.chOthers === "Other versions",
       `other versions: ${vers.slice(1).join(", ")} from web/releases.json, each with its date; none chosen`);
    ok(!/0\.1[45]/.test(v) && !/choralroot-0\.1[45]\.fwsc/.test(p.$.files.innerHTML + n) && vers.every((x) => verOk(x)) &&
       p.$["older-note"].textContent === list.minimum.note && /0\.15 and older are not offered/.test(list.minimum.note) &&
       html.includes('<a href="https://github.com/Quixotic7/ChoralRootFM1/releases" target="_blank" rel="noopener" data-t="chOlder">') && en.chOlder === "0.15 and older are on GitHub.",
       "other versions: 0.14 and 0.15 are not offered; one line links the releases on GitHub");
    const f = p.$.files.innerHTML;
    ok(vers.every((x) => f.includes(`href="${pkgOf(x)}" download`) && f.includes(`/tree/v${x}"`) && f.includes(`/docs/releases/${x}.md"`)) &&
       count(f, "<code>sha256 ") === vers.length && p.$["safe-note"].textContent === list.minimum.boot_safe,
       "the files: a download link, the sha256, the full notes and the source at its tag for each version");
    await p.$.go.fire("click");
    ok(dev.identity === "FM-1_920" && dev.bad === 0 && dev.served > 8 && p.$.status.textContent === en.done &&
       p.$.log.textContent.includes(`install: ChoralRoot ${newest} (choralroot-${newest}.fwsc)`), `at once: one click on Install wrote ${newest} (${dev.served} reads)`);
    ok(p.$.done.hidden === false && p.$["done-line"].textContent === en.dnLine.replace("{version}", newest) && p.$.interrupted.hidden === true && !p.$.go.disabled &&
       p.$.go.textContent === label(newest), "done: the page names the version installed");
    await p.$["check-go"].fire("click");
    ok(p.$.unit.hidden === false && p.$.unit.textContent === en.dnMatch.replace("{name}", "ChoralRoot " + newest) && p.$.unit.className === "unit" &&
       p.$.rel.innerHTML.includes(en.chHas), `done: "What is on my FM-1?" reads the version back -> "${p.$.unit.textContent}"`);
  }

  // "What is on my FM-1?" before an install: read only; the version found is marked in the list
  {
    const side = crSide(vers[1]);
    const dev = new FakeFM1(logicalImage(RAW[newest]), { identity: "FM-1_920", bk: () => side });
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    await p.$["check-go"].fire("click");
    ok(p.$.unit.textContent === en.cnCr.replace("{name}", "ChoralRoot " + vers[1]) && p.$.unit.className === "unit" && dev.upgrades === 0 && !side.log.length &&
       !p.$.go.disabled && !p.$["check-go"].disabled, `check: finds ChoralRoot ${vers[1]}, writes nothing`);
    ok(count(p.$.versions.innerHTML, en.chHas) === 1 && new RegExp(`data-ver="${vers[1].replace(".", "\\.")}"[^>]*>.*?${en.chHas}`).test(p.$.versions.innerHTML.split("</button>")[0]) &&
       !p.$.rel.innerHTML.includes(en.chHas) && p.$.go.textContent === label(newest), `check: ${vers[1]} is marked "${en.chHas}" in the other versions; Install still says ${newest}`);
    choose(p, vers[1]);
    await settle();
    ok(p.$.unit.textContent === en.cnSame.replace("{name}", "ChoralRoot " + vers[1]) && p.$.rel.innerHTML.includes(en.chHas) && !p.$.go.disabled &&
       p.$["plan-back"].textContent === en.planOld.replace("{version}", vers[1]).replace("{newest}", newest),
       "check: the version the FM-1 already runs, chosen -> said so (no going-back note: it is not going back), Install still enabled");
  }

  // an older version: one click chooses it; the button, the going-back sentence and the notes follow; one click back
  {
    fetched.length = 0;
    const dev = new FakeFM1(logicalImage(RAW[newest]));
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    const labels = [p.$.go.textContent];
    for (const x of vers.slice(1)) {
      choose(p, x);
      labels.push(p.$.go.textContent);            // (the label follows the click at once, before the package is there)
      await settle();
      const n = unesc(p.$.notes.innerHTML), r = rel(x);
      ok(p.$.go.textContent === label(x) && !p.$.go.disabled && p.$.status.textContent === `ChoralRoot ${x} · FM-1_920` && fetched[fetched.length - 1] === pkgOf(x) &&
         chips(p) === vers.slice(1).map((y) => (y === x ? y + "*" : y)).join() && p.$.rel.innerHTML.includes(`<b>ChoralRoot ${x}</b> · ${dateOf(r.date)}`) &&
         p.$.rel.innerHTML.includes(en.chOld) && !p.$.rel.innerHTML.includes(en.chNewest),
         `older: one click on ${x} -> "${p.$.go.textContent}", its package loaded and checked, ${x} marked as chosen and as an older version`);
      ok(p.$["not-newest"].hidden === false && p.$["plan-back"].textContent === en.planBack.replace("{version}", x) + r.going_back.short &&
         p.$["newest-go"].textContent === en.toNewest.replace("{version}", newest), `older: the going-back sentence of ${x} is shown by the button, with the way back to ${newest}`);
      ok(n.includes(`<h2>${en.chWhat.replace("{version}", x)}</h2>`) && n.includes(dateOf(r.date)) && r.bullets.every((b) => n.includes(b)) && n.includes(r.going_back.short) &&
         count(n, "<details>") === 1 && !n.includes("<details open") && n.indexOf("<details>") < n.indexOf(r.going_back.points[0]) &&
         r.going_back.points.every((b) => n.includes(b)) && n.includes(`/docs/releases/${x}.md"`) && n.includes(`href="${pkgOf(x)}" download`) && n.includes(`/tree/v${x}"`),
         `older: the notes are ${x}'s: date, bullets, the going-back sentence; the points in full behind a disclosure; notes, file, source`);
    }
    ok(labels.join("|") === vers.map(label).join("|"), `label: Install always names the version it writes (${labels.join(", ")})`);
    await p.$["newest-go"].fire("click");
    await settle();
    ok(p.$.go.textContent === label(newest) && p.$["not-newest"].hidden === true && p.$["plan-back"].textContent === "" && chips(p) === vers.slice(1).join() &&
       unesc(p.$.notes.innerHTML).includes(`<h2>${en.chNew.replace("{version}", newest)}</h2>`) && !p.$.go.disabled && fetched[fetched.length - 1] === pkgOf(newest),
       `older: one click on "${p.$["newest-go"].textContent}" -> ${newest} again`);
    choose(p, "0.15"); choose(p, "9.9");
    await settle();
    ok(p.$.go.textContent === label(newest) && fetched[fetched.length - 1] === pkgOf(newest), "older: a version that is not in the list cannot be chosen");
  }

  // choosing an older version installs THAT package: the unit accepts only the older image and reports that version
  {
    const img = logicalImage(RAW[older]);
    const run = async (pick) => {
      let after = false;
      const before = crSide(newest), then = crSide(older);
      const dev = new FakeFM1(img, { identity: "FM-1_920", bk: (id) => (id.startsWith("ota-") ? null : after ? then : before) });
      const boot = dev.boot.bind(dev);
      dev.boot = (id, name) => { if (!id.startsWith("ota-")) after = true; boot(id, name); };
      const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
      await settle();
      if (pick) { choose(p, pick); await settle(); }
      const said = p.$.go.textContent;
      await p.$.go.fire("click");
      return { p, dev, said };
    };
    let { p, dev, said } = await run(older);
    ok(said === label(older) && dev.identity === "FM-1_920" && dev.bad === 0 && dev.served > 8 && p.$.status.textContent === en.done &&
       p.$.log.textContent.includes(`install: ChoralRoot ${older} (choralroot-${older}.fwsc)`),
       `older: "${said}" wrote the ${older} package, byte for byte (${dev.served} reads)`);
    ok(p.$.done.hidden === false && p.$["done-line"].textContent === en.dnLine.replace("{version}", older) && p.$.interrupted.hidden === true &&
       p.$.go.textContent === label(older) && unesc(p.$.notes.innerHTML).includes(rel(older).bullets[0]), "older: the page says which version is installed, beside its notes");
    await p.$["check-go"].fire("click");
    ok(p.$.unit.textContent === en.dnMatch.replace("{name}", "ChoralRoot " + older) && p.$.unit.className === "unit" && count(p.$.versions.innerHTML, en.chHas) === 1 &&
       p.$["plan-back"].textContent === en.planOld.replace("{version}", older).replace("{newest}", newest), `older: the read-back -> "${p.$.unit.textContent}"`);
    ({ p, dev, said } = await run(null));
    ok(said === label(newest) && dev.bad > 0, `older: (the control) the default installs another package: the same unit sees ${dev.bad} reads that are not ${older}'s`);
    await p.$["check-go"].fire("click");
    ok(p.$.unit.textContent === en.dnDiffer.replace("{name}", "ChoralRoot " + older).replace("{version}", newest) && p.$.unit.className === "unit bad",
       "read-back: a unit that reports another version than the one installed is told so");
  }

  // a package that is not the released one: refused before any write
  for (const [what, opts] of [["the file differs from the list's sha256", { over: { [older]: makePackage("FM-1_920", 99) } }],
    ["the list's sha256 differs from the file", { tamper: older }]]) {
    const dev = new FakeFM1(logicalImage(RAW[older]), { identity: "FM-1_920", bk: () => crSide(newest) });
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(opts.over), page: opts.tamper ? pageFor(await metaFor(opts.tamper)) : page });
    await settle();
    const first = !p.$.go.disabled;
    choose(p, older);
    await settle();
    const refused = p.$.status.textContent === en.badhash.replace("{version}", older) && p.$.go.disabled && p.$.go.textContent === label(older) &&
      /sha256 of /.test(p.$.log.textContent);
    await p.$.go.fire("click");
    await settle();
    ok(first && refused && dev.upgrades === 0 && dev.served === 0 && dev.bkFrames === 0 && dev.identity === "FM-1_920" && p.$.done.hidden === true,
       `hash: ${what} -> refused with the reason, Install disabled, nothing sent to the FM-1`);
    await p.$["newest-go"].fire("click");
    await settle();
    ok(!p.$.go.disabled && p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920`, "hash: ... back to the newest, Install is enabled again");
  }
  {
    const p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: listFetch({ [newest]: makePackage("FM-1_920", 5) }), page });
    await settle();
    ok(p.$.status.textContent === en.badhash.replace("{version}", newest) && p.$.go.disabled, "hash: the newest version's file is checked the same way at load");
  }
  {
    const p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: listFetch(), page });
    await settle();
    choose(p, older); choose(p, vers[1]); choose(p, older);      // quick clicks: the last one wins, whatever arrives when
    const mid = p.$.go.disabled;
    await settle();
    ok(mid && !p.$.go.disabled && p.$.go.textContent === label(older) && p.$.status.textContent === `ChoralRoot ${older} · FM-1_920`,
       "label: Install is disabled while a chosen package loads, and the last choice is the one loaded");
    const dev = new FakeFM1(logicalImage(RAW[older]), { identity: "FM-1_920", bk: () => crSide(newest) });
    const q = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    choose(q, vers[1]); choose(q, older);
    await settle();
    await q.$.go.fire("click");
    ok(dev.bad === 0 && dev.identity === "FM-1_920" && q.$.status.textContent === en.done, "label: ... and the one written");
  }

  // an interrupted install: the guidance appears; the unit in update mode is finished by Install
  {
    const dev = new FakeFM1(logicalImage(RAW[newest]), { identity: "FM-1_920", bk: (id) => (id.startsWith("ota-") ? null : crSide(vers[1])) });
    dev.onServe = () => { if (dev.identity.startsWith("ota-") && dev.served >= 6) dev.input.state = dev.output.state = "disconnected"; };
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    ok(p.$.interrupted.hidden === true, "interrupted: the guidance is hidden before an install");
    await p.$.go.fire("click");
    ok(p.$.status.textContent === en.lost && p.$.interrupted.hidden === false && !p.$.go.disabled && p.$.done.hidden === true && p.$.go.textContent === label(newest),
       "interrupted: the cable lost while writing -> the status says so and the guidance appears; Install can be pressed again");
    ok(/not lost/.test(en.intTitle) && /nothing was written/.test(en.intEarly) && /update mode/.test(en.intOn) && /Three short flashes/.test(en.intOff) &&
       /0\.16 or later/.test(en.intOff) && /WL82 UBOOT1\.00/.test(en.intDark) && html.includes('<a href="#dark" data-t="darkTitle">'),
       "interrupted: the three cases (nothing written, still on, switched off) and the way to the help");
    dev.onServe = null; dev.boot("ota-FM-1_920", "Felucca Update");
    await p.$["check-go"].fire("click");
    ok(p.$.unit.textContent === en.cnLoader.replace("{identity}", "ota-FM-1_920") && p.$.unit.className === "unit", "check: a unit in update mode is recognised (Install will finish it)");
    await p.$.go.fire("click");
    ok(p.$.status.textContent === en.done && dev.identity === "FM-1_920" && p.$.interrupted.hidden === true && p.$.done.hidden === false,
       "interrupted: Install again on the unit in update mode finishes the write -> installed");
  }
  {
    const fel = backupSide("FELUCCA SLOOP 2.2", BACKUP_IDS, feluccaData());
    const dev = new FakeFM1(logicalImage(RAW[newest]), { identity: "FM-1_900", bk: () => fel });
    const p = runPage({ navigator: midiOf(dev), fetch: listFetch(), page });
    await settle();
    await p.$["check-go"].fire("click");
    ok(p.$.unit.className === "unit bad" && p.$.unit.textContent.startsWith("Installing over Sloop is not supported") && dev.upgrades === 0,
       "check: a firmware the installer refuses is named with the refusal");
    await p.$.go.fire("click");
    ok(p.$.interrupted.hidden === true && p.$.done.hidden === true && dev.upgrades === 0 && p.$["force-box"].hidden === false,
       "interrupted: a refusal is not an interruption (no guidance, nothing written)");
    const none = runPage({ navigator: midiOf({ access: { inputs: new Map(), outputs: new Map() } }), fetch: listFetch(), page });
    await settle();
    await none.$["check-go"].fire("click");
    ok(none.$.unit.textContent === en.notfound && none.$.unit.className === "unit bad" && !none.$["check-go"].disabled && !none.$.go.disabled, "check: no FM-1 -> the notfound text");
    const stockDev = new FakeFM1(logicalImage(RAW[newest]));
    const ps = runPage({ navigator: midiOf(stockDev), fetch: listFetch(), page });
    await settle();
    await ps.$["check-go"].fire("click");
    ok(ps.$.unit.className === "unit" && ps.$.unit.textContent.startsWith("On your FM-1: ") && /can be installed over it/.test(ps.$.unit.textContent) && stockDev.upgrades === 0,
       `check: the stock firmware -> "${ps.$.unit.textContent.slice(0, 60)}..."`);
  }

  // a browser without Web MIDI: the notice, Install disabled, and no package fetched
  {
    let fetches = 0;
    const p = runPage({ navigator: {}, fetch: async () => { fetches++; throw new Error("no fetch without Web MIDI"); }, page });
    await settle();
    ok(!p.error && p.$.nomidi.hidden === false && p.$.nomidi.textContent === en.nomidiNote && p.$.status.textContent === en.nomidi && p.$.go.disabled &&
       p.$["check-go"].disabled && p.$["backup-go"].disabled && fetches === 0 && p.$.go.textContent === label(newest),
       "no Web MIDI: a notice says which browsers can install; Install, the check and Back up are disabled; nothing is fetched");
  }

  // demo mode: a state shown with made-up data; nothing reaches MIDI or the network, whatever is clicked
  const DEMOS = ["idle", "older", "writing", "interrupted", "done", "nomidi", "stockwriting", "stockdone"];
  ok(DEMOS.every((d) => new RegExp(`\\b${d}\\b`).test(/const DEMO = [\s\S]*?\n\}\)\(\);/.exec(src)[0])), `demo: the states ${DEMOS.join(", ")}`);
  for (const q of [...DEMOS.map((d) => "?demo=" + d), "?x=1&demo=writing"]) {
    let midiCalls = 0, fetches = 0;
    const state = q.split("demo=")[1];
    const nav = { requestMIDIAccess: async () => { midiCalls++; return new FakeFM1(image).access; } };
    const p = runPage({ navigator: nav, fetch: async () => { fetches++; throw new Error("no fetch in demo mode"); }, page, extra: { location: { search: q } } });
    await settle();
    const before = [p.$.go.textContent, p.$.status.textContent, p.$.done.hidden, p.$.interrupted.hidden, p.$.unit.textContent].join("|");
    for (const id of ["go", "check-go", "backup-go", "snd-read", "restore-go", "stock-go"]) await p.$[id].fire("click");
    await settle();
    const after = [p.$.go.textContent, p.$.status.textContent, p.$.done.hidden, p.$.interrupted.hidden, p.$.unit.textContent].join("|");
    ok(!p.error && before === after && midiCalls === 0 && fetches === 0 && p.$["demo-note"].hidden === false && !p.confirms.length && !p.downloads.length,
       `demo ${q}: marked as a demo; Install, the check, Back up, Read sounds, Restore, Return to V15: no MIDI request, no fetch, nothing changes${p.error ? " (" + p.error.stack + ")" : ""}`);
    const want = state === "older" ? older : newest;
    ok(p.$.go.textContent === label(want) && p.$.go.disabled === (state === "writing" || state === "nomidi") &&
       p.$["not-newest"].hidden === (state !== "older") && p.$.interrupted.hidden === (state !== "interrupted") && p.$.done.hidden === (state !== "done") &&
       p.$.nomidi.hidden === (state !== "nomidi") && p.$["stock-done"].hidden === (state !== "stockdone") && p.$["stock-interrupted"].hidden === true &&
       !!p.$.stock.open === state.startsWith("stock"), `demo ${state}: "${p.$.go.textContent}"${p.$.go.disabled ? " (disabled)" : ""}, and only this state's boxes`);
    if (state === "idle") ok(p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920` && p.$.unit.textContent === en.cnCr.replace("{name}", "ChoralRoot " + vers[1]) &&
      count(p.$.versions.innerHTML, en.chHas) === 1, "demo idle: the newest selected, the FM-1 shown as running the version before it");
    if (state === "older") ok(chips(p).includes(older + "*") && p.$["plan-back"].textContent === en.planBack.replace("{version}", older) + rel(older).going_back.short &&
      unesc(p.$.notes.innerHTML).includes(rel(older).bullets[0]), "demo older: an older version chosen, its going-back sentence and its notes");
    if (state === "writing") ok(p.$.status.textContent === en.write && p.$.bar.value === 58, "demo writing: the bar part way, the status Writing");
    if (state === "interrupted") ok(p.$.status.textContent === en.lost, "demo interrupted: the lost-connection status with the guidance");
    if (state === "done") ok(p.$["done-line"].textContent === en.dnLine.replace("{version}", newest) && p.$.unit.textContent === en.dnMatch.replace("{name}", "ChoralRoot " + newest) &&
      p.$.bar.value === 100, "demo done: installed, and the read-back");
    if (state === "stockwriting") ok(p.$["stock-status"].textContent === en.write && p.$["stock-bar"].value === 58 && p.$.bar.value === 0 &&
      p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920`, "demo stockwriting: the section open, its bar part way, its status Writing; the install card idle");
    if (state === "stockdone") ok(p.$["stock-status"].textContent === en.done && p.$["stock-bar"].value === 100 && p.$["stock-done-line"].textContent === en.stockDnLine.replace("{identity}", "FM-1_015") &&
      /choralroot-backup-\d+\.json/.test(p.$["stock-done-data"].textContent) && p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920`, "demo stockdone: the section's done panel; the install card idle");
    if (state === "nomidi") ok(p.$.status.textContent === en.nomidi && p.$["check-go"].disabled, "demo nomidi: the notice of a browser that cannot install");
  }
  for (const q of ["?demo=9", "?demo=1", "?demo=", "?demo=idleX", "?nodemo=idle"]) {
    const p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: listFetch(), page, extra: { location: { search: q } } });
    await settle();
    ok(p.$["demo-note"].hidden === true && !p.$.go.disabled && p.$.status.textContent === `ChoralRoot ${newest} · FM-1_920`, `demo: ${q} is not demo mode (the page works as usual)`);
  }
}
function dateOf(iso) {
  const [y, m, d] = iso.split("-").map(Number);
  return `${d} ${["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"][m - 1]} ${y}`;
}
function verOk(v) { const [a, b] = v.split(".").map(Number); return a > 0 || b >= 16; }

console.log(failed ? `INSTALLER TESTS FAILED (${failed})` : "installer tests passed");
process.exit(failed ? 1 : 0);
