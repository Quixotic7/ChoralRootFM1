// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the SMK-37 installer, web/smk37.html (no browser, no hardware). Run from the repo root:
//   node web/test_smk_installer.mjs
// - the page's shape: /*LIB*/ and /*META*/ once, one module script, indexable (the landing page links it), no link to the FM-1 installer's
//   pages; every data-t key and every fm1ota.js error code has a text
// - the page as make_site.py inlines it (its SMK_LIB, a META of three models' packages) against a DOM stub and a
//   simulated keyboard (web/test_installer.mjs's FakeFM1 with SMK names): nothing before the risk box is ticked; the
//   SMK-37 Pro, the Elite and the MKE-P37 each get their own package, the Starrykey 37 Play the Pro's; the single-core
//   variant fetches its own file; an FM-1, ChanceTheMaker's SMK firmware, an unknown firmware, another firmware's
//   update mode, a package whose sha256 or identity differ: refused with nothing sent; an interrupted install finished
//   with its loader's model's package; a keyboard running ChoralRoot backed up to a file first; two keyboards refused

import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf } from "./fm1pkg.js";
import { pack7, unpack7 } from "./fm1ota.js";
import { bkU32, bkR32, bkPack, bkCrc } from "./fm1backup.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const HERE = new URL(".", import.meta.url).pathname;
const count = (s, sub) => s.split(sub).length - 1;
const html = readFileSync(join(HERE, "smk37.html"), "utf8");
const ota = readFileSync(join(HERE, "fm1ota.js"), "utf8");
const sha256 = async (b) => Buffer.from(await crypto.subtle.digest("SHA-256", b)).toString("hex");
function makePackage(product, seed = 0) {          // (as test_installer.mjs: 20 identity blocks, then the rest)
  const raw = Uint8Array.from({ length: 20 * 48 + 0x2000 }, (_, i) => (i * 13 + 5 + seed) & 0xFF);
  for (let i = 0; i < 20; i++) raw[i * 48 + 47] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  return raw;
}

/* ---------------------------------------------------------------- the shape --- */
ok(count(html, "/*LIB*/") === 1 && count(html, "/*META*/") === 1 && count(html, "<!--EMU-->") === 1,
   "page: /*LIB*/, /*META*/ and <!--EMU--> (make_site.py's link to the emulator) exactly once");
ok(count(html, "<script") === 1 && count(html, '<script type="module">') === 1, "page: one <script type=\"module\">");
ok(!/<meta name="robots"/.test(html), "page: indexable, no robots meta (the landing page links it)");
ok(!html.includes("webapp/installer") && !html.includes("../installer/"), "page: no link to the FM-1 installer");
ok(html.includes("<title>ChoralRoot SMK-37 · installer</title>") && /<html lang="en">/.test(html), "page: title and lang");
ok(/tested on SMK-37s by their owners/i.test(html) && /the maintainer has none/.test(html) && /genkigenki/.test(html) && /M-UPGRADE/.test(html), "page: the status, the credit, the way back");
const scriptOf = (h) => h.slice(h.indexOf('<script type="module">') + '<script type="module">'.length, h.lastIndexOf("</script>"));
const textSrc = scriptOf(html).match(/const TEXT = (\{[\s\S]*?\n\});/);
const TEXT = textSrc ? new Function(`return ${textSrc[1]}`)() : {};
const en = TEXT.en || {};
const dataT = [...html.matchAll(/data-t="([^"]+)"/g)].map((m) => m[1]);
ok(dataT.length > 10 && dataT.every((k) => typeof en[k] === "string"), `texts: every data-t key has a text (${dataT.length})`);
const codes = new Set();
for (const m of ota.matchAll(/\bfail\(([^,]+),/g)) for (const s of m[1].matchAll(/"([a-z]+)"/g)) codes.add(s[1]);
const missingC = [...codes].filter((c) => typeof en[c] !== "string");
ok(codes.size > 8 && !missingC.length, `texts: every fm1ota.js error code has a text (${codes.size}${missingC.length ? "; missing " + missingC : ""})`);
const keys = [...scriptOf(html).matchAll(/\b(?:sayK|t|coded)\("([A-Za-z]+)"/g)].map((m) => m[1]);
ok(keys.every((k) => typeof en[k] === "string"), `texts: every key the script names has a text (${keys.filter((k) => typeof en[k] !== "string")})`);

/* ---------------------------------------------------- the page as make_site.py inlines it --- */
const stripModule = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
const libm = site.match(/SMK_LIB = \(([^)]*)\)/);
const libs = libm ? [...libm[1].matchAll(/"([\w.]+\.js)"/g)].map((m) => m[1]) : [];
ok(libs.join() === "fm1pkg.js,fm1ota.js,fm1backup.js", `make_site.py's SMK_LIB: ${libs.join(", ")}`);
const lib = libs.map((f) => stripModule(readFileSync(join(HERE, f), "utf8"))).join("\n");
const V = "0.24-beta2-smk37";
const PRODUCTS = { pro: "SMK-37 Pro_920", elite: "SMK-37 Elite_920", mke: "MKE-P37_920" };
const SFX = { pro: "", elite: "-elite", mke: "-mke" };
const RAW = {}, META = { repo: "https://github.com/Quixotic7/ChoralRootFM1",
  releases: [{ version: V, base: "0.24-beta2", date: "2026-10-16", tag: "v" + V, title: "test", bullets: ["b"], notes: ["n"], models: {} }] };
let seed = 1;
for (const [k, prod] of Object.entries(PRODUCTS)) {
  const std = makePackage(prod, seed++), one = makePackage(prod, seed++);
  const n = `../../firmware/smk37/choralroot-${V}${SFX[k]}.fwsc`, n1 = n.replace(".fwsc", "-1core.fwsc");
  RAW[n] = std; RAW[n1] = one;
  META.releases[0].models[k] = { product: prod, pkg: n, sha256: await sha256(std), size: std.length, pkg_1core: n1, sha256_1core: await sha256(one), size_1core: one.length };
}
const built = html.split("/*LIB*/").join(lib).split("/*META*/").join(JSON.stringify(META));
const code = scriptOf(built);
let compiled = false;
try { new vm.Script(code, { filename: "smk37.js" }); compiled = true; } catch (e) { console.log(String(e)); }
ok(compiled && !/^\s*(import|export)\s/m.test(code), "inlined page compiles, no import / export left");

class El {
  constructor(attrs) {
    this.id = attrs.id; this.textContent = ""; this.disabled = "disabled" in attrs; this.value = 0; this.checked = false;
    this.dataset = attrs.t ? { t: attrs.t } : {}; this.hidden = "hidden" in attrs; this.innerHTML = ""; this.l = {};
  }
  addEventListener(type, f) { this.l[type] = f; }
  fire(type, ev = {}) { return this.l[type] ? this.l[type]({ type, ...ev }) : undefined; }
}
function makeDom() {
  const byId = {}, withT = [];
  for (const m of html.matchAll(/<[a-z][a-z0-9]*\b([^>]*)>/g)) {
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
const fastTimeout = (f, ms = 0, ...a) => setTimeout(f, ms >= 100 ? ms / 10 : ms, ...a);
function runPage({ navigator = {}, fetch, confirm = () => true } = {}) {
  const { byId, withT } = makeDom();
  const win = { l: {}, addEventListener(type, f) { this.l[type] = f; } };
  const downloads = [], fetched = [];
  const document = {
    body: { append() {} },
    createElement: (tag) => ({ tag, href: "", download: "", remove() {}, click() { downloads.push(this.download); } }),
    getElementById: (id) => byId[id] || null,
    querySelectorAll: () => withT,
  };
  const ctx = {
    document, window: win, navigator, confirm,
    fetch: async (url) => { fetched.push(url); return fetch ? fetch(url) : { ok: false, status: 404 }; },
    URL, Blob, crypto: globalThis.crypto, TextEncoder, TextDecoder, console, btoa, atob, DataView,
    setTimeout: fastTimeout, clearTimeout, setInterval, clearInterval,
  };
  let error = null;
  try { vm.runInNewContext(code, ctx, { filename: "smk37.js" }); } catch (e) { error = e; }
  return { $: byId, withT, error, downloads, fetched };
}
const pkgFetch = (over = {}) => async (url) => {
  const raw = over[url] || RAW[url];
  return raw ? { ok: true, status: 200, arrayBuffer: async () => raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length) } : { ok: false, status: 404 };
};

/* a simulated keyboard (test_installer.mjs's FakeFM1, the port names as parameters) */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
class FakeKbd {
  constructor(image, { identity, loader, final, name = "SMK-37 Pro", bk = null } = {}) {
    this.image = image; this.served = 0; this.bad = 0; this.upgrades = 0; this.name = name;
    this.loader = loader; this.final = final; this.bk = bk;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, identity.startsWith("ota-") ? "Felucca Update" : name);
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
    if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x4C) {
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
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.final, this.name), 300);
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
function crSide(version) {                          // ChoralRoot's backup side: INFO, LIST, GET (one small object)
  const objs = new Map([[1, Uint8Array.from({ length: 300 }, (_, i) => i & 255)]]), ids = [1];
  return { handle(cmd, a) {
    if (cmd === 1) return [...new TextEncoder().encode(version), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3, 0x43, 1, 3];
    if (cmd === 65) { const out = [1, 0, ids.length]; for (const id of ids) { const v = objs.get(id); out.push(id, ...bkU32(v.length), ...bkU32(bkCrc(v))); } return out; }
    if (cmd === 66) { const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = objs.get(id); return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))]; }
    return null;
  } };
}
const midiOf = (...devs) => ({ requestMIDIAccess: async () => {
  if (devs.length === 1) return devs[0].access;
  const inputs = new Map(), outputs = new Map();
  for (const d of devs) { for (const [k, v] of d.access.inputs) inputs.set(k, v); for (const [k, v] of d.access.outputs) outputs.set(k, v); }
  return { inputs, outputs };
} });
const settle = () => realSleep(30);
const imgOf = (k, one = false) => logicalImage(RAW[META.releases[0].models[k][one ? "pkg_1core" : "pkg"]]);
const accept = (p) => { p.$.accept.checked = true; p.$.accept.fire("change"); };

{
  const p = runPage({ navigator: {} });
  await settle();
  ok(!p.error && p.$.status.textContent === en.nomidi && p.$.go.disabled, `no Web MIDI: the page runs, says so, Install off${p.error ? " (" + p.error.message + ")" : ""}`);
  ok(p.withT.every((e) => e.textContent), "texts applied to every data-t element");
}
{
  const dev = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_016", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle();
  ok(p.$.go.disabled && p.$.go.textContent === `Install ChoralRoot ${V}`, "Install disabled until the risk box is ticked; its label names the version");
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.acceptFirst && !p.fetched.length && dev.upgrades === 0, "Install before ticking: refused, nothing fetched or sent");
  accept(p);
  ok(!p.$.go.disabled, "ticked: Install enabled");
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && !p.$.done.hidden && dev.upgrades === 2 && dev.bad === 0 && dev.identity === "SMK-37 Pro_920" &&
     p.fetched.length === 1 && p.fetched[0].endsWith(`choralroot-${V}.fwsc`), "SMK-37 Pro (stock): the Pro package, verified, written, back as SMK-37 Pro_920");
}
for (const [k, id] of [["elite", "SMK-37 Elite_016"], ["mke", "MKE-P37_016"]]) {
  const prod = PRODUCTS[k], dev = new FakeKbd(imgOf(k), { identity: id, loader: "ota-" + prod, final: prod, name: id.split("_")[0] });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === prod && p.fetched[0].endsWith(`${SFX[k]}.fwsc`),
     `${id.split("_")[0]} (stock): its own package (${prod})`);
}
{
  const dev = new FakeKbd(imgOf("pro"), { identity: "STARRYKEY-37 PLAY_016", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920", name: "Starrykey 37" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === "SMK-37 Pro_920" && p.fetched[0].endsWith(`choralroot-${V}.fwsc`),
     "Starrykey 37 Play: the Pro's package (the same keyboard), back as SMK-37 Pro_920");
}
{
  const dev = new FakeKbd(imgOf("pro", true), { identity: "SMK-37 Pro_016", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  p.$.variants.fire("click", { target: { closest: () => ({ dataset: { o: "1" } }) } });
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && p.fetched[0].endsWith(`choralroot-${V}-1core.fwsc`), "Single core: the -1core package fetched, verified and written");
}
for (const [id, name, key, what] of [["FM-1_015", "FM-1", "fm1", "an FM-1"], ["FM-1_920", "ChoralRoot FM-1", "fm1", "an FM-1 on ChoralRoot"],
  ["SMK-37_931", "SMK-37", "other", "ChanceTheMaker's SMK firmware"], ["ota-FM-1_920", "Felucca Update", "fm1", "an FM-1 in update mode"]]) {
  const dev = new FakeKbd(imgOf("pro"), { identity: id, loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920", name });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent.startsWith(en[key].split("{")[0]) && dev.upgrades === 0 && dev.served === 0 && !p.fetched.length,
     `${what}: refused (${key}), nothing fetched or sent`);
}
{
  const dev = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_920", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920",
    bk: (id) => (id === "SMK-37 Pro_920" ? crSide("SLOOP 2.6") : null) });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(dev.upgrades === 2 && p.$.status.textContent === en.done, "an SMK-37 port (SMK-37 Pro_920, Sloop's INFO): installed over (the installers' SMK table)");
}
{
  const dev = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_920", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920",
    bk: (id) => (id === "SMK-37 Pro_920" ? crSide("ChoralRoot 0.22") : null) });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^choralroot-backup-\d{8}\.json$/.test(p.downloads[0]) && p.$.status.textContent === en.done && dev.bad === 0,
     "ChoralRoot on the SMK (0.22): backed up to a file first, then the install");
}
{
  const dev = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_016", loader: "ota-SMK-37 Pro_920", final: "SMK-37 Pro_920" });
  const bad = makePackage("SMK-37 Pro_920", 99);
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch({ [META.releases[0].models.pro.pkg]: bad }) });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.badpkg && dev.upgrades === 0, "a package whose sha256 differs from the list: refused, nothing sent");
  const p2 = runPage({ navigator: midiOf(new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_016" })), fetch: async () => ({ ok: false, status: 404 }) });
  await settle(); accept(p2);
  await p2.$.go.fire("click");
  ok(p2.$.status.textContent.startsWith(en.loadfail), "a package that cannot be downloaded: said so, nothing sent");
}
{
  const dev = new FakeKbd(imgOf("elite"), { identity: "ota-SMK-37 Elite_920", final: "SMK-37 Elite_920", name: "SMK-37 Elite" });
  const p = runPage({ navigator: midiOf(dev), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && dev.served === 7 && dev.identity === "SMK-37 Elite_920" && p.fetched[0].endsWith("-elite.fwsc"),
     "resume: an Elite in ChoralRoot's update mode finished with the Elite's package");
  const st = new FakeKbd(imgOf("pro"), { identity: "ota-SMK-37 Pro_016", final: "SMK-37 Pro_016" });
  const q = runPage({ navigator: midiOf(st), fetch: pkgFetch() });
  await settle(); accept(q);
  await q.$.go.fire("click");
  ok(q.$.status.textContent.startsWith(en.foreign) && st.served === 0 && !q.fetched.length, "resume: the stock updater's loader (ota-SMK-37 Pro_016): refused, nothing written");
}
{
  const a = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Pro_016" }), b = new FakeKbd(imgOf("pro"), { identity: "SMK-37 Elite_016", name: "SMK-37 Elite" });
  const p = runPage({ navigator: midiOf(a, b), fetch: pkgFetch() });
  await settle(); accept(p);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.several && a.upgrades === 0 && b.upgrades === 0, "two keyboards: refused, nothing sent to either");
}

console.log(failed ? `SMK INSTALLER TESTS FAILED (${failed})` : "smk installer tests passed");
process.exit(failed ? 1 : 0);
