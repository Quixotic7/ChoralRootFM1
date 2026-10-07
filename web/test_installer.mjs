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

import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf, STOCK_V15_SIZE } from "./fm1pkg.js";
import { pack7, unpack7 } from "./fm1ota.js";
import { bkU32, bkR32, bkPack, bkUnpack, bkCrc, CR_BACKUP_IDS, BACKUP_IDS } from "./fm1backup.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const HERE = new URL(".", import.meta.url).pathname;
const count = (s, sub) => s.split(sub).length - 1;

const html = readFileSync(join(HERE, "index_pkg.html"), "utf8");
const ota = readFileSync(join(HERE, "fm1ota.js"), "utf8");
const META = { version: "0.1", product: "FM-1_920", pkg: "../../firmware/choralroot-0.1.fwsc" };

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

/* -------------------------------------------------------- (d) status line --- */
const tpl = html.match(/say\((`ChoralRoot \$\{meta\.version\} · \$\{meta\.product\}`)\)/);
ok(tpl && new Function("meta", `return ${tpl[1]}`)(META) === "ChoralRoot 0.1 · FM-1_920", "status: `ChoralRoot ${meta.version} · ${meta.product}` -> ChoralRoot 0.1 · FM-1_920");

/* ------------------------------------------- (c) the page as make_site inlines it --- */
// make_site.py's strip_module and the libraries it inlines (read from make_site.py, so the test follows it)
const stripModule = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
let libs = [...site.matchAll(/strip_module\(\(HERE \/ "([\w.]+\.js)"\)/g)].map((m) => m[1]);
if (!libs.length) libs = ["fm1pkg.js", "fm1ota.js", "fm1backup.js"];
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
    this.checked = false; this.dataset = attrs.t ? { t: attrs.t } : {}; this.style = {}; this.l = {};
  }
  addEventListener(type, f) { this.l[type] = f; }
  fire(type) { return this.l[type] ? this.l[type]({ type }) : undefined; }
}
function makeDom() {
  const byId = {}, withT = [];
  for (const m of html.matchAll(/<[a-z]+\b([^>]*)>/g)) {
    const a = m[1], attrs = {};
    const id = /\sid="([^"]+)"/.exec(a), t = /\sdata-t="([^"]+)"/.exec(a);
    if (!id && !t) continue;
    if (id) attrs.id = id[1];
    if (t) attrs.t = t[1];
    if (/\sdisabled(\s|=|$)/.test(a)) attrs.disabled = true;
    const el = new El(attrs);
    if (id) byId[id[1]] = el;
    if (t) withT.push(el);
  }
  return { byId, withT };
}
const realSleep = (ms) => new Promise((r) => setTimeout(r, ms));
// the Updater's waits (2 s, 3 s, 1 s polls) at a tenth: the simulated FM-1 answers in milliseconds
const fastTimeout = (f, ms = 0, ...a) => setTimeout(f, ms >= 100 ? ms / 10 : ms, ...a);

function runPage({ navigator = {}, fetch, confirm = () => true, prelude = "", extra = {} } = {}) {
  const { byId, withT } = makeDom();
  const win = { l: {}, addEventListener(type, f) { this.l[type] = f; } };
  const confirms = [], downloads = [];
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
    URL, Blob, crypto: globalThis.crypto, TextEncoder, TextDecoder, console, btoa, atob, DataView,
    setTimeout: fastTimeout, clearTimeout, setInterval, clearInterval, ...extra,
  };
  const src = prelude ? code.replace(lib, () => lib + "\n" + prelude) : code;
  let error = null;
  try { vm.runInNewContext(src, ctx, { filename: "installer.js" }); } catch (e) { error = e; }
  return { $: byId, withT, win, document, confirms, downloads, error };
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

// a package with an identity: 20 blocks of 47 bytes + a marker byte each, then the rest (fm1pkg.js productOf)
function makePackage(product) {
  const raw = Uint8Array.from({ length: 20 * 48 + 0x2000 }, (_, i) => (i * 13 + 5) & 0xFF);
  for (let i = 0; i < 20; i++) raw[i * 48 + 47] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  return raw;
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

const raw = makePackage(META.product), image = logicalImage(raw);
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
  ok(dev.bkFrames === 0 && !p.downloads.length && !p.confirms.length && p.$.status.textContent === en.done, "Skip backup: installed with no backup request, no question");
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

// return to official V15: validateStockPackage stands in for the real file (the official V15 is not in the repo)
{
  const stockImage = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const prelude = "validateStockPackage = async (b) => ({ product: \"FM-1_015\", image: __stockImage, sha256: \"test\" });";
  const v15 = { size: STOCK_V15_SIZE, arrayBuffer: async () => new ArrayBuffer(STOCK_V15_SIZE) };
  const stockRun = async (dev, answer) => {
    const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage }, confirm: () => answer });
    await settle();
    p.$["stock-file"].files = [{ size: 123, arrayBuffer: async () => new ArrayBuffer(123) }];
    await p.$["stock-file"].fire("change");
    const wrong = p.$.status.textContent.startsWith(en.stockError) && p.$["stock-go"].disabled;
    p.$["stock-file"].files = [v15];
    await p.$["stock-file"].fire("change");
    const valid = p.$.status.textContent === en.stockValid && !p.$["stock-go"].disabled;
    await p.$["stock-go"].fire("click");
    return { p, wrong, valid };
  };
  const confirmText = en.stockConfirm || "";
  ok(/backup/.test(confirmText) && /Restore/.test(en.stockWarn || ""), "stock: the warning and the confirm speak of the backup");
  const crSide = () => backupSide("ChoralRoot 0.1", CR_BACKUP_IDS, crData());

  let dev = new FakeFM1(stockImage, { identity: "FM-1_920", final: "FM-1_015", loader: "ota-FM-1_015", bk: (id) => (id === "FM-1_920" ? crSide() : null) });
  let r = await stockRun(dev, false);
  ok(r.wrong && r.valid, "stock: a file of the wrong size is refused, the V15 (stub) accepted");
  ok(r.p.downloads.length === 1 && /^choralroot-backup-/.test(r.p.downloads[0].name), "stock: the backup is saved first");
  ok(r.p.confirms.length === 1 && r.p.confirms[0] === confirmText && dev.upgrades === 0 && dev.served === 0 && !r.p.$["stock-go"].disabled,
    "stock: declined -> confirm asked once, nothing written, unlocked");
  dev = new FakeFM1(stockImage, { identity: "FM-1_920", final: "FM-1_015", loader: "ota-FM-1_015", bk: (id) => (id === "FM-1_920" ? crSide() : null) });
  r = await stockRun(dev, true);
  ok(r.p.confirms.length === 1 && r.p.downloads.length === 1 && r.p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === "FM-1_015",
    "stock: confirmed -> backup, ChoralRoot -> loader -> FM-1_015, status Done");

  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRun(dev, true);
  ok(r.p.confirms.length === 0 && dev.upgrades === 0 && r.p.$.status.textContent === en.stockNeedBackup, "stock resume (in update mode) without the resume box: nothing written");
  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  const stockRunR = async (d, answer) => {
    const p = runPage({ navigator: midiOf(d), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage }, confirm: () => answer });
    await settle();
    p.$["stock-file"].files = [v15];
    await p.$["stock-file"].fire("change");
    p.$["stock-recovery"].checked = true;
    await p.$["stock-go"].fire("click");
    return { p };
  };
  r = await stockRunR(dev, false);
  ok(r.p.confirms.length === 1 && dev.upgrades === 0, "stock resume (box ticked): declined -> nothing written");
  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRunR(dev, true);
  ok(r.p.confirms.length === 1 && r.p.$.status.textContent === en.done && dev.identity === "FM-1_015", "stock resume (box ticked): confirmed -> FM-1_015, status Done");

  const none = runPage({ navigator: midiOf({ access: { inputs: new Map(), outputs: new Map() } }), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage } });
  await settle();
  none.$["stock-file"].files = [v15];
  await none.$["stock-file"].fire("change");
  await none.$["stock-go"].fire("click");
  ok(none.$.status.textContent === en.notfound && none.confirms.length === 0, "stock: no FM-1 -> notfound before any confirm");
}

console.log(failed ? `INSTALLER TESTS FAILED (${failed})` : "installer tests passed");
process.exit(failed ? 1 : 0);
