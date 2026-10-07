// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the installer page, web/index_pkg.html (no browser, no hardware). Run from the repo root:
//   node web/test_installer.mjs
// - the page's shape: /*LIB*/ and /*META*/ once, one module script, no editor link, no Felucca status, English only,
//   no backup step (ChoralRoot has no backup SysEx)
// - the texts: every data-t key and every error code fm1ota.js throws has an English text
// - the page as make_site.py inlines it (the libraries make_site.py names, META for 0.1 / FM-1_920) runs against a
//   DOM stub and a simulated FM-1 (after test_web.mjs's FakeFM1): no Web MIDI, package load, install, resume,
//   errors, and the return to official V15 (confirm first, nothing written when it is declined)

import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf, STOCK_V15_SIZE } from "./fm1pkg.js";
import { pack7, unpack7 } from "./fm1ota.js";

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
ok(!/archiveInfo|captureBackup|BackupConnection|saveArchive|stock-recovery/.test(html), "page: no backup step (archiveInfo, captureBackup, BackupConnection)");
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
const sayKeys = [...scriptOf(html).matchAll(/(?:sayK|t)\("([A-Za-z]+)"/g)].map((m) => m[1]);
ok(sayKeys.every((k) => typeof en[k] === "string"), "texts: every key the script names has a text");

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
  const confirms = [];
  const document = {
    documentElement: { lang: "" },
    getElementById: (id) => byId[id] || null,
    querySelectorAll: (sel) => { if (sel !== "[data-t]") throw new Error(`querySelectorAll(${sel})`); return withT; },
  };
  const ctx = {
    document, window: win, navigator, fetch: fetch || (async () => { throw new Error("fetch not expected"); }),
    confirm: (msg) => { confirms.push(msg); return confirm(msg); },
    URL, Blob, crypto: globalThis.crypto, TextEncoder, TextDecoder, console,
    setTimeout: fastTimeout, clearTimeout, setInterval, clearInterval, ...extra,
  };
  const src = prelude ? code.replace(lib, () => lib + "\n" + prelude) : code;
  let error = null;
  try { vm.runInNewContext(src, ctx, { filename: "installer.js" }); } catch (e) { error = e; }
  return { $: byId, withT, win, document, confirms, error };
}

/* a simulated FM-1 on WebMIDI (test_web.mjs's FakeFM1 with the identities as parameters) */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
class FakeFM1 {
  constructor(image, { identity = "FM-1_015", loader = "ota-FM-1_920", final = "FM-1_920", onServe = null } = {}) {
    this.image = image; this.served = 0; this.bad = 0; this.upgrades = 0;
    this.loader = loader; this.final = final; this.onServe = onServe;
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
  ok(/no backup/.test(confirmText) && /erased/.test(confirmText) && /no backup/.test(en.stockWarn || ""), "stock: the warning and the confirm say there is no backup");
  ok(!/stockUnsupported|backup:/.test(Object.keys(en).join()) && !("stockRecovery" in en), "stock: no backup texts left");

  let dev = new FakeFM1(stockImage, { identity: "FM-1_920", final: "FM-1_015", loader: "ota-FM-1_015" });
  let r = await stockRun(dev, false);
  ok(r.wrong && r.valid, "stock: a file of the wrong size is refused, the V15 (stub) accepted");
  ok(r.p.confirms.length === 1 && r.p.confirms[0] === confirmText && dev.upgrades === 0 && dev.served === 0 && !r.p.$["stock-go"].disabled,
    "stock: declined -> confirm asked once, nothing written, unlocked");
  dev = new FakeFM1(stockImage, { identity: "FM-1_920", final: "FM-1_015", loader: "ota-FM-1_015" });
  r = await stockRun(dev, true);
  ok(r.p.confirms.length === 1 && r.p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === "FM-1_015",
    "stock: confirmed -> ChoralRoot -> loader -> FM-1_015, status Done");

  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRun(dev, false);
  ok(r.p.confirms.length === 1 && dev.upgrades === 0, "stock resume (in update mode): declined -> nothing written");
  dev = new FakeFM1(stockImage, { identity: "ota-FM-1_920", final: "FM-1_015" });
  r = await stockRun(dev, true);
  ok(r.p.confirms.length === 1 && r.p.$.status.textContent === en.done && dev.identity === "FM-1_015", "stock resume: confirmed -> FM-1_015, status Done");

  const none = runPage({ navigator: midiOf({ access: { inputs: new Map(), outputs: new Map() } }), fetch: pkgFetch(raw), prelude, extra: { __stockImage: stockImage } });
  await settle();
  none.$["stock-file"].files = [v15];
  await none.$["stock-file"].fire("change");
  await none.$["stock-go"].fire("click");
  ok(none.$.status.textContent === en.notfound && none.confirms.length === 0, "stock: no FM-1 -> notfound before any confirm");
}

console.log(failed ? `INSTALLER TESTS FAILED (${failed})` : "installer tests passed");
process.exit(failed ? 1 : 0);
