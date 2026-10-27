// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The FM-1 USB-MIDI update protocol over WebMIDI (after tools/fm1_install.py).
// Step 1: the running firmware (official
// or Felucca) reads parts of the package and stages the update loader. Step 2:
// the loader reads the whole image and writes it. Both steps are "the device
// asks, we answer": cmd 0x30 read requests on the logical image.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_OTA = (id) => /^ota-/i.test(id.model);
// the SMK-37 Pro, SMK-37 Elite, MKE-P37, Starrykey 37 Play and the SMK-37+VA mod: one keyboard under several model
// names, so a package for one runs on all of them (also "SMK-37", the name other firmwares report); an FM-1 is
// another device (SMK37.md)
const SMK_FAMILY = /^(ota-)?(SMK-37([ +].*)?|MKE-P37|STARRYKEY-37.*)$/i;
export const sameKeyboard = (a, b) => a === b || (SMK_FAMILY.test(a) && SMK_FAMILY.test(b));   // two models
// the update loader of SLOOP / Felucca packages ("ota-FM-1_9XX", "ota-SMK-37 Pro_9XX"; the SMK ports also under the
// Elite's and the MKE-P37's model name, Makkuro37 _7XX). A device left in the update mode of another firmware (the
// stock updater's loader) is never written: its loader may lay the image out differently. The bare "SMK-37" name
// ("ota-SMK-37_931") is ChanceTheMaker's SMK firmwares' RAM updater: another package format, so not ours.
export const OUR_LOADER = (id) => /^ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_[789]\d\d$/i.test(id.text);
// ChanceTheMaker's SMK firmwares (Felucca SMK, Sloop SMK, Melodee SMK, Doom): "SMK-37_9NN", their own updater
export const OTHER_SMK = (id) => /^(ota-)?SMK-37_9\d\d$/i.test(id.text);
// the MIDI ports find() probes: the FM-1 / SMK-37 models' and the firmwares' names; other gear is never sent anything
export const PORT_RE = /fm-1|smk|elite|mke-p37|starry|felucca|sloop|clipper|melodee|fomni|x0x|makkuro|maku|rainbow|amb-1|choral|orbit|snoop|vector|vox|ota|composite|sinco|usb-midi/i;

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// mismatch (detail: the identity the device reports), badreq, other (an SMK firmware with its own updater),
// foreign (detail: the loader's identity)
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).split("\0")[0];   // (some firmwares end the text early)
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// ---- which firmware runs: may ChoralRoot be installed over it? (docs/INSTALL-COMPAT.md; tools/fm1_install.py
// classify_firmware is the same table). identity: the handshake text ("FM-1_015", "FM-1_910"); info: the version the
// backup protocol's INFO answers ("FELUCCA v1.0", "MELODEE v0.11.1", "ChoralRoot 0.12"), null when it does not answer.
// -> { verdict: "allow" | "refuse" | "loader", kind, name }. The 9xx identities are shared by every Felucca-based
// firmware (FM-1_9XY from the release number: Felucca 0.9-beta FM-1_909, Felucca 1.0 FM-1_910, Sloop 2.0 FM-1_920 =
// ChoralRoot's), so they are told apart by the INFO text only.
export const RECOVERY_URL = "https://github.com/Quixotic7/MvaveFM1Unbricker";
// port: the MIDI port's name, when known. Bubba Machine (github.com/Erbubar23/bubba-machine) answers FM-1_003, as an
// old official firmware would; its USB product name "FM-1 Bubba Machine" tells it apart. Its updater takes only a
// package with M-VAVE's own update loader, so it refuses ChoralRoot's (docs/INSTALL-COMPAT.md "Bubba Machine").
export const BUBBA_PORT = /bubba/i;
// RemoteFM1 0.2 on (github.com/Quixotic7/RemoteFM1; on fm1-base + felucca-bsp, felucca-bsp's updater): fm1-base's
// identity, its fm1.toml id_prefix "FM-1_52" + "00" (a development build) or + XXYYZZ (release X.Y.Z, two digits
// each: 0.2.1 = FM-1_52000201). No INFO answer. RemoteFM1 0.1 answered as the official V15 (FM-1_015). Only
// RemoteFM1's prefix: other fm1-base firmwares' update paths are not known here (docs/INSTALL-COMPAT.md "RemoteFM1")
export const REMOTEFM1_ID = /^52(?:00|(\d\d)(\d\d)(\d\d))$/;
export function classifyFirmware(identity, info, port) {
  const m = /^(ota-)?([^_]+)_(\d+)$/i.exec(identity || "");
  const v = (info || "").trim();
  if (!m) return { verdict: "refuse", kind: "unknown", name: `an unknown firmware (${identity || "no identity"})` };
  if (m[1]) return { verdict: "loader", kind: "loader", name: identity };            // update mode: resume
  const num = parseInt(m[3], 10);
  if (SMK_FAMILY.test(m[2])) return classifySmk(identity, m[2], num, v);   // the M-Vave SMK-37 (SMK37.md)
  if (m[2] !== "FM-1") return { verdict: "refuse", kind: "unknown", name: identity };
  if (BUBBA_PORT.test(port || "")) return { verdict: "refuse", kind: "bubba", name: `Bubba Machine (${identity})` };
  const r = REMOTEFM1_ID.exec(m[3]);
  if (r) return { verdict: "allow", kind: "remotefm1", name: r[1] ? `RemoteFM1 ${+r[1]}.${+r[2]}.${+r[3]}` : "RemoteFM1 (development build)" };
  if (num >= 1 && num < 100) return { verdict: "allow", kind: "stock", name: `the official M-VAVE firmware (${identity})` };
  if (num === 0) return { verdict: "refuse", kind: "sloop", name: "Sloop's rescue mode (FM-1_000)" };
  if (num < 900) return { verdict: "refuse", kind: "unknown", name: `an unknown firmware (${identity})` };
  if (/^choralroot\b/i.test(v)) return { verdict: "allow", kind: "choralroot", name: v };
  if (/^melodee\b/i.test(v)) return { verdict: "allow", kind: "melodee", name: `Melodee (${v.slice(8)})` };
  if (/sloop/i.test(v)) return { verdict: "refuse", kind: "sloop", name: "Sloop" };
  const f = /^felucca\s+(v)?(\d+)\.(\d+)(\S*)$/i.exec(v);
  // Felucca 1.0 and later: "v1.0", "v1.0.1", "v1.1-rc1" (build.py "v" + release); the 0.x betas: "0.9-BETA", "0.5 BETA"
  if (f && parseInt(f[2], 10) >= 1 && !/beta/i.test(v) && (f[1] || !f[4]))
    return { verdict: "allow", kind: "felucca", name: `Felucca ${f[1] || "v"}${f[2]}.${f[3]}${f[4]}` };
  if (/^felucca\b/i.test(v)) return { verdict: "refuse", kind: "felucca-beta", name: `a Felucca beta or a firmware based on one (${v})` };
  return { verdict: "refuse", kind: "unknown", name: v ? `an unknown firmware (${identity}, ${v})` : `an unknown Felucca-based firmware (${identity})` };
}
// an SMK-37 (any of its model names): the official firmware and the Felucca-family ports (SLOOP, Felucca and the ports
// on their SMK board layer: its owner tag keeps each firmware's data apart) are installed over. The bare name
// "SMK-37_9NN" is ChanceTheMaker's SMK firmwares': another package format and their own updater, so not ours
function classifySmk(identity, model, num, v) {
  if (model.toUpperCase() === "SMK-37" && num >= 900 && num < 1000)
    return { verdict: "refuse", kind: "other", name: `a firmware with its own updater (${identity})` };
  if (num >= 1 && num < 100) return { verdict: "allow", kind: "stock", name: `the official M-VAVE firmware (${identity})` };
  if (/^choralroot\b/i.test(v)) return { verdict: "allow", kind: "choralroot", name: v };
  if (num >= 700 && num < 1000) return { verdict: "allow", kind: "smk", name: `an SMK-37 port (${identity}${v ? ", " + v : ""})` };
  return { verdict: "refuse", kind: "unknown", name: `an unknown firmware (${identity})` };
}
export function refusalText(name) {
  return `Installing over ${name} is not supported: ${REFUSE_REASON} ` +
    `Return to the official V15 firmware with the installer you used for ${name} first, then install ChoralRoot. ` +
    `If an FM-1 is already dark (black screen, a "WL82 UBOOT1.00" USB disk): ${RECOVERY_URL}`;
}
export const REFUSE_REASON = "An install over it has left an FM-1 that no longer starts, and the data it leaves in the flash is not known to be safe for ChoralRoot.";

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + 1000;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

// an input's output: the output of the same name; when several ports have that name (two FM-1s, as CoreMIDI may list
// them), the k-th connected output of the name for the k-th connected input (tools/fm1_install.py port_labels)
export function pairOutput(access, input) {
  const live = (m) => [...m.values()].filter((p) => p.name === input.name && p.state !== "disconnected");
  const k = live(access.inputs).indexOf(input);
  return k < 0 ? null : live(access.outputs)[k] || null;
}
const FM1_PORT = PORT_RE;                                       // never probe other gear (the FM-1s and SMK-37s)

export class Updater {
  constructor(access, log) {
    this.access = access;
    this.log = log || (() => {});
  }

  // the input ports that may be an FM-1 (connected, a name that matches)
  candidates() {
    return [...this.access.inputs.values()].filter((p) => p.state !== "disconnected" && FM1_PORT.test(p.name || ""));
  }

  // every in/out pair with the same port name that answers the handshake (all: every one, their links closed)
  async find(filter, all = null) {
    for (const input of this.candidates()) {
      const output = pairOutput(this.access, input);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = new Link(input, output);
      const id = await handshake(link, 2);
      if (id && (!filter || filter(id))) {
        if (!all) return { link, id, name: input.name };
        all.push({ id, name: input.name, input, output });
      }
      link.close();
    }
    return null;
  }

  // several FM-1s (docs/INSTALL-COMPAT.md "Several FM-1s"): every pair that answers -> [{id, name, input, output}]
  async all(filter) {
    const got = [];
    await this.find(filter, got);
    return got;
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(1000);
    }
    return null;
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      if (onProgress) onProgress(served, r.addr);
    }
  }

  // full install: running firmware -> loader -> new firmware. anyModel: the package may be for another model of the
  // same keyboard (an SMK-37 Elite package on a unit that reports SMK-37 Pro); never an FM-1 one on an SMK, nor back
  async install(image, product, onStep, anyModel = false) {
    const step = onStep || (() => {});
    const dev = await this.find((id) => !IS_OTA(id));
    if (!dev) throw fail("notfound", "FM-1 not found (USB cable, and no other app using it?)");
    if (OTHER_SMK(dev.id)) { dev.link.close(); throw fail("other", `the keyboard runs a firmware with its own updater (${dev.id.text}): nothing was sent`, dev.id.text); }
    const [model] = product.split("_");
    if (dev.id.model !== model && !(anyModel && sameKeyboard(dev.id.model, model))) { dev.link.close(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`); }
    step("start", dev.id.text);
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n) => step("verify", n));
    dev.link.close();
    // some firmwares restart into their loader at once, without step 1: such a loader (ours, of this keyboard) goes on
    const jumped = !s1.finished && s1.lost && s1.served === 0;
    if (!s1.finished && !jumped) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(3000);
    const ota = await this.waitFor(jumped ? IS_OTA : (id) => id.model === "ota-" + model, jumped ? 15000 : 30000);
    if (jumped && !(ota && OUR_LOADER(ota.id) && sameKeyboard(ota.id.model.replace(/^ota-/i, ""), dev.id.model) &&
                    (ota.id.model === "ota-" + model || (anyModel && sameKeyboard(ota.id.model.replace(/^ota-/i, ""), model))))) {
      if (ota) ota.link.close();
      throw fail(ota ? "foreign" : "lost", ota ? `the device restarted into the update mode of another firmware (${ota.id.text}): nothing was written`
        : "the device was disconnected after 0 requests: nothing was written", ota ? ota.id.text : "");
    }
    if (!ota) throw fail("noloader", "the update loader did not appear. Replug the USB cable and press Install again: the device stays in update mode until it is finished.");
    const s2 = await this.write(ota, image, step);
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `installed, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step) {
    ota.link.send(UPGRADE);
    await sleep(2000);
    const s2 = await this.serve(ota.link, image, 0xF0000000, 180000, (n) => step("write", n));
    ota.link.close();
    return s2;
  }

  // resume: the device is already in update mode (loader) -> true when the write finished and the FM-1 is back with
  // the identity written. opts.package: the identity of the package being written; opts.product: the official
  // package's (the return to stock). The loader's model must match it by install()'s rule (the same model, or with
  // opts.anyModel another model of the same keyboard): an SMK's loader never gets an FM-1 image, nor back, and without
  // either identity nothing is written
  async resume(image, onStep, opts = {}) {
    const want = opts.product || opts.package || "";
    const model = want.split("_")[0];
    const ota = await this.find(IS_OTA);
    if (!ota) return false;
    if (!OUR_LOADER(ota.id) && !opts.product) {   // (the return to stock: the official loader's own image)
      ota.link.close();
      throw fail("foreign", `the device is in the update mode of another firmware (${ota.id.text}): finish that update with its own updater`, ota.id.text);
    }
    const lm = ota.id.model.replace(/^ota-/i, "");
    if (!want || (lm !== model && !(opts.anyModel && sameKeyboard(lm, model)))) {
      ota.link.close();
      throw fail("model", `the device is in the update mode of ${ota.id.text}, the package is for ${want || "an unknown model"}`, ota.id.text);
    }
    const step = onStep || (() => {});
    const result = await this.write(ota, image, step);
    if (!result.finished) return false;
    {                                             // (the firmware back with the identity written, as before)
      const product = want;
      step("reboot");
      await sleep(3000);
      const back = await this.waitFor((id) => !IS_OTA(id) && sameKeyboard(id.model, model), 40000);
      if (!back) throw fail("noreturn", "The FM-1 did not return after resuming the write.");
      back.link.close();
      if (back.id.text !== product) throw fail("mismatch", "The FM-1 reports a different firmware after resuming.", back.id.text);
      step("done", back.id.text);
    }
    return true;
  }
}
