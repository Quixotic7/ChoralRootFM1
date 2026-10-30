// SPDX-License-Identifier: GPL-3.0-only
// The page of the SMK-37 browser emulator (the FM-1's ../emu.js, for the SMK-37 Pro's panel; its arrangement after
// genkigenki's Vector37 simulator, GPL-3.0): the screen, the 37 keys with velocity, the 16 pads lit as the firmware
// lights them, K1..K8, the faders, the wheels, the 18 buttons by their SMK names (and their ChoralRoot role, SMK37.md),
// the computer keyboard, Web MIDI and the flash in IndexedDB. The device runs in worklet.js; before Start a copy boots
// here, without sound, to show its screen. Hardware ids throughout (firmware/hal/smk_input.h): keys 0..36 = C3..C6,
// buttons 0..17, knobs 0..7 = K1..K8, pads 0..7 the top row, 8..15 the bottom row.
"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  const status = (t) => ($("status").textContent = t);
  const mk = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html) e.innerHTML = html; return e; };

  // ------------------------------------------------------------------------------- the panel's parts ---
  const BTN = [["KNOB BANK", "view menu"], ["FADER BANK", "sends A / B"], ["&lt;", "back"], ["&gt;", "forward"],
    ["OCTAVE&minus;", "OCT&minus;"], ["OCTAVE+", "OCT+"], ["ARP", "PERF"], ["NOTE REPEAT", "LATCH"], ["SCALE", "KEY"],
    ["CHORD", "BASS"], ["GLOBE", "OPT"], ["BT", "SAVE"], ["PATCH", "HOME"], ["PARA", "EDIT"], ["FX", "FX"], ["SEQ", "SEQ"],
    ["SEQ PLAY", "LOOP"], ["SEQ REC", "REC"]];
  const KNOB = [["K1", "VOLUME", "#F6F2EE"], ["K2", "SELECT &middot; BPM", "#F6B600"], ["K3", "ALGORITHM &middot; BASS SND", "#F67918"],
    ["K4", "PRESETS &middot; SOUND", "#F6F2EE"], ["K5", "KNOB 1 &middot; VOICING", "#2950CD"], ["K6", "KNOB 2 &middot; BASS VOICE", "#F67918"],
    ["K7", "KNOB 3 &middot; PERFORM", "#F6F2EE"], ["K8", "KNOB 4 &middot; FX", "#29B27B"]];
  const PAD = ["DIM", "MIN", "MAJ", "SUS", "Strum", "Strum 2 Oct", "Slop", "Harp", "6", "m7", "M7", "9", "LOCK", "Arp", "Arp 2 Oct", "Pattern"];
  const FADER = ["DRIVE", "CHORUS", "DELAY", "REVERB"];
  const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  // the computer keyboard (the FM-1 emulator's map, ../keymap.js, on the SMK's parts): kind, hardware id
  const KEYS = [["F1", "pad", 0], ["F2", "pad", 1], ["F3", "pad", 2], ["F4", "pad", 3], ["Digit2", "pad", 8], ["Digit3", "pad", 9],
    ["Digit4", "pad", 10], ["Digit5", "pad", 11], ["Tab", "pad", 12],
    ["KeyA", "key", 12], ["KeyS", "key", 14], ["KeyD", "key", 16], ["KeyF", "key", 17], ["KeyG", "key", 19], ["KeyH", "key", 21],
    ["KeyJ", "key", 23], ["KeyK", "key", 24], ["KeyL", "key", 26], ["Semicolon", "key", 28], ["Quote", "key", 29], ["BracketRight", "key", 31],
    ["KeyW", "key", 13], ["KeyE", "key", 15], ["KeyT", "key", 18], ["KeyY", "key", 20], ["KeyU", "key", 22], ["KeyO", "key", 25], ["KeyP", "key", 27],
    ["KeyZ", "btn", 4], ["KeyX", "btn", 5], ["Escape", "btn", 4], ["Enter", "btn", 5], ["End", "both", 0],
    ["F5", "btn", 14], ["F6", "btn", 8], ["F7", "btn", 9], ["F8", "btn", 7], ["F9", "btn", 13], ["F10", "btn", 10],
    ["Digit7", "btn", 12], ["Digit8", "btn", 11], ["Digit9", "btn", 6], ["Digit0", "btn", 15], ["Minus", "btn", 16], ["Equal", "btn", 17],
    ["Comma", "btn", 2], ["Period", "btn", 3], ["Backquote", "btn", 0], ["Slash", "btn", 1],
    ["PageDown", "cycle", 1], ["PageUp", "cycle", -1], ["ArrowUp", "turn", 1], ["ArrowDown", "turn", -1]]
    .map(([code, kind, id]) => ({ code, kind, id }));
  const capOf = (code) => ({ Digit2: "2", Digit3: "3", Digit4: "4", Digit5: "5", Digit7: "7", Digit8: "8", Digit9: "9", Digit0: "0",
    Minus: "-", Equal: "=", Semicolon: ";", Quote: "'", BracketRight: "]", Comma: ",", Period: ".", Backquote: "`", Slash: "/",
    Escape: "", Enter: "", Tab: "TAB" }[code] ?? code.replace(/^Key/, ""));
  const hint = (kind, id) => { const m = KEYS.find((x) => x.kind === kind && x.id === id && capOf(x.code)); return m ? capOf(m.code) : ""; };

  let node = null, selKnob = 4, master = 724;
  const post = (m) => node && node.port.postMessage(m);
  const SOFT = 40;

  // buttons: under the screen (two columns, three rows: KNOB BANK FADER BANK, < >, OCTAVE- OCTAVE+), under the faders (2 x 6)
  const btnEl = [];
  const button = (b, parent) => {
    const e = mk("div", "b", `${BTN[b][0]}<small>${BTN[b][1]}</small>` + (hint("btn", b) ? `<kbd>${hint("btn", b)}</kbd>` : ""));
    e.dataset.c = "b" + b;
    btnEl[b] = e;
    parent.appendChild(e);
  };
  [0, 1, 2, 3, 4, 5].forEach((b) => button(b, $("nav")));
  [6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17].forEach((b) => button(b, $("grid6")));
  // knobs over the pads, one column each
  const knobEl = [], knobA = new Array(8).fill(0);
  KNOB.forEach(([n, role, c], e) => {
    const k = mk("div", "knob", `<div class="dial"></div><b>${n}</b><small>${role}</small>`);
    k.style.setProperty("--c", c);
    k.dataset.c = "e" + e;
    knobEl[e] = k;
    $("mid").appendChild(k);
  });
  const padEl = [];
  for (let p = 0; p < 16; p++) {
    const e = mk("div", "pad", `<i>${PAD[p]}</i>` + (hint("pad", p) ? `<kbd>${hint("pad", p)}</kbd>` : ""));
    e.dataset.c = "p" + p;
    padEl[p] = e;
    $("mid").appendChild(e);
  }
  // faders and wheels
  const faderEl = [];
  FADER.forEach((n, i) => {
    const d = mk("div", "", `<input class="v" type="range" min="0" max="127" value="0" aria-label="fader ${i + 1}"><div class="lab">${i + 1} ${n}</div>`);
    d.firstChild.addEventListener("input", (ev) => post({ type: "fader", i, v: +ev.target.value }));
    faderEl[i] = d.firstChild;
    $("faders").appendChild(d);
  });
  const pitch = $("pitch"), mod = $("mod");
  let pitchHeld = false, pitchAnim = 0;
  pitch.addEventListener("input", () => post({ type: "wheel", i: 0, raw: +pitch.value }));
  pitch.addEventListener("pointerdown", () => { pitchHeld = true; cancelAnimationFrame(pitchAnim); });
  const pitchHome = () => {                                // let go: the spring pulls it back to the middle
    pitchHeld = false;
    cancelAnimationFrame(pitchAnim);
    const step = () => {
      const v = +pitch.value, d = 535 - v;
      pitch.value = Math.abs(d) < 2 ? 535 : v + d * 0.35;
      post({ type: "wheel", i: 0, raw: Math.round(+pitch.value) });
      if (+pitch.value !== 535 && !pitchHeld) pitchAnim = requestAnimationFrame(step);
    };
    pitchAnim = requestAnimationFrame(step);
  };
  ["change", "pointerup", "pointercancel", "lostpointercapture"].forEach((t) => pitch.addEventListener(t, () => { if (+pitch.value !== 535) pitchHome(); }));
  addEventListener("pointerup", () => { if (pitchHeld) pitchHome(); });
  mod.addEventListener("input", () => post({ type: "wheel", i: 1, raw: 629 - +mod.value }));   // up = lower readings
  // the pedal jack: a switch pedal, down while the button is held (it shorts the jack: ~0; let go: 985, as empty)
  const pedal = $("pedal");
  pedal.addEventListener("pointerdown", (ev) => { pedal.setPointerCapture(ev.pointerId); post({ type: "pedal", raw: 0 }); });
  ["pointerup", "pointercancel", "lostpointercapture"].forEach((t) => pedal.addEventListener(t, () => post({ type: "pedal", raw: 985 })));
  // the keys: 37, C3..C6, across the width
  const keyEl = [], WHITE = [0, 2, 4, 5, 7, 9, 11];
  for (let k = 0, wx = 0; k < 37; k++) {
    const white = WHITE.includes(k % 12), e = mk("div", "key " + (white ? "w" : "b"));
    if (white) { wx++; if (k % 12 === 0) e.innerHTML = `<span>C${3 + k / 12}</span>`; }
    else { e.style.left = ((wx - 0.31) / 22) * 100 + "%"; e.style.width = (0.62 / 22) * 100 + "%"; }
    if (hint("key", k)) e.insertAdjacentHTML("beforeend", `<kbd>${hint("key", k)}</kbd>`);
    e.dataset.c = "k" + k;
    keyEl[k] = e;
    $("keys").appendChild(e);
  }

  // ----------------------------------------------------------------------------- held parts -> device ---
  // each key, pad or button is down while any source holds it (a pointer, a computer key, a latch); a key or pad
  // starts at the velocity its first source gave it
  const srcs = new Map();                                    // "k12" -> Map(source -> velocity)
  const set = (c, src, down, vel = 100) => {
    let s = srcs.get(c);
    if (!s) srcs.set(c, (s = new Map()));
    const was = s.size;
    if (down) s.set(src, vel); else s.delete(src);
    if (!!was === !!s.size) return;
    const id = +c.slice(1), on = s.size > 0, v = on ? [...s.values()][0] : 0;
    if (c[0] === "k") { post({ type: "key", k: id, vel: v }); keyEl[id].classList.toggle("down", on); keyEl[id].classList.toggle("held", on); }
    else if (c[0] === "p") { post({ type: "pad", p: id, vel: v }); padEl[id].classList.toggle("held", on); }
    else { post({ type: "button", b: id, down: on ? 1 : 0 }); btnEl[id].classList.toggle("held", on); }
  };
  const releaseSrc = (test) => { for (const [c, s] of srcs) for (const x of [...s.keys()]) if (test(x)) set(c, x, false); };
  const turn = (e, n) => {
    if (!n) return;
    knobA[e] += n * 15;
    knobEl[e].firstChild.style.setProperty("--a", (e === 0 ? (master / 1023 - 0.5) * 270 : knobA[e]) + "deg");
    post({ type: "enc", e, n });
  };
  const drawSel = () => knobEl.forEach((k, e) => k.classList.toggle("sel", e === selKnob));
  // velocity from where a key or pad is struck: low on it loud, high soft; Shift soft
  const velAt = (ev, el) => {
    if (ev.shiftKey) return SOFT;
    const r = el.getBoundingClientRect(), y = r.height ? (ev.clientY - r.top) / r.height : 0.75;
    return Math.max(20, Math.min(127, Math.round(20 + 107 * Math.max(0, Math.min(1, y)))));
  };
  const ctlAt = (ev) => { const g = ev.target.closest && ev.target.closest("[data-c]"); return g ? g.dataset.c : null; };
  const ptrs = new Map();
  const smk = $("smk");
  smk.addEventListener("contextmenu", (ev) => ev.preventDefault());
  smk.addEventListener("pointerdown", (ev) => {
    const c = ctlAt(ev);
    if (!c) return;
    ev.preventDefault();
    if (c[0] === "e") {
      selKnob = +c.slice(1); drawSel();
      ptrs.set(ev.pointerId, { c, y: ev.clientY, acc: 0 });
      smk.setPointerCapture(ev.pointerId);
      return;
    }
    const el = c[0] === "k" ? keyEl[+c.slice(1)] : c[0] === "p" ? padEl[+c.slice(1)] : btnEl[+c.slice(1)];
    const vel = velAt(ev, el);
    if (ev.button === 2 || ev.ctrlKey) {                     // latch
      const s = srcs.get(c);
      set(c, "latch", !(s && s.has("latch")), vel);
      return;
    }
    ptrs.set(ev.pointerId, { c });
    set(c, "p" + ev.pointerId, true, vel);
  });
  smk.addEventListener("pointermove", (ev) => {
    const p = ptrs.get(ev.pointerId);
    if (!p || p.c[0] !== "e") return;
    p.acc += p.y - ev.clientY;
    p.y = ev.clientY;
    const n = Math.trunc(p.acc / 7);
    if (n) { p.acc -= n * 7; turn(+p.c.slice(1), n); }
  });
  const up = (ev) => {
    const p = ptrs.get(ev.pointerId);
    if (!p) return;
    ptrs.delete(ev.pointerId);
    if (p.c[0] !== "e") set(p.c, "p" + ev.pointerId, false);
  };
  for (const t of ["pointerup", "pointercancel"]) addEventListener(t, up);
  smk.addEventListener("wheel", (ev) => {
    const c = ctlAt(ev);
    if (!c || c[0] !== "e") return;
    ev.preventDefault();
    turn(+c.slice(1), ev.deltaY < 0 ? 1 : -1);
  }, { passive: false });
  const byCode = Object.fromEntries(KEYS.map((m) => [m.code, m]));
  const typing = (ev) => /^(SELECT|INPUT|TEXTAREA)$/.test(ev.target.tagName) && ev.target.type !== "range" || ev.metaKey || ev.altKey;
  addEventListener("keydown", (ev) => {
    const m = byCode[ev.code];
    if (!m || typing(ev) || ev.ctrlKey) return;
    ev.preventDefault();
    if (ev.repeat && m.kind !== "turn") return;
    const vel = ev.shiftKey ? SOFT : 100;
    if (m.kind === "key") set("k" + m.id, "kb", true, vel);
    else if (m.kind === "pad") set("p" + m.id, "kb", true, vel);
    else if (m.kind === "btn") set("b" + m.id, "kb" + m.code, true);
    else if (m.kind === "both") { set("b4", "esc", true); set("b5", "esc", true); }
    else if (m.kind === "cycle") { selKnob = (selKnob + m.id + 8) % 8; drawSel(); }
    else if (m.kind === "turn") turn(selKnob, m.id);
  });
  addEventListener("keyup", (ev) => {
    const m = byCode[ev.code];
    if (!m) return;
    if (m.kind === "key") set("k" + m.id, "kb", false);
    else if (m.kind === "pad") set("p" + m.id, "kb", false);
    else if (m.kind === "btn") set("b" + m.id, "kb" + m.code, false);
    else if (m.kind === "both") { set("b4", "esc", false); set("b5", "esc", false); }
  });
  addEventListener("blur", () => releaseSrc((s) => s.startsWith("kb") || s === "esc"));

  // ------------------------------------------------------------------------------------ the lights ---
  const lcd = $("lcd"), g2 = lcd.getContext("2d");
  const img = g2.createImageData(240, 240), px = new Uint32Array(img.data.buffer);
  const LUT = new Uint32Array(65536);
  for (let u = 0; u < 65536; u++) {                           // a big-endian RGB565 word as read little-endian -> RGBA
    const c = ((u >> 8) | (u << 8)) & 0xffff;
    const r = (((c >> 11) & 31) * 255 + 15) / 31, g = (((c >> 5) & 63) * 255 + 31) / 63, b = ((c & 31) * 255 + 15) / 31;
    LUT[u] = 0xff000000 | (b << 16) | (g << 8) | r;
  }
  const drawFb = (fb) => { for (let i = 0; i < 57600; i++) px[i] = LUT[fb[i]]; g2.putImageData(img, 0, 0); };
  const drawLights = (leds, pads) => {
    for (let b = 0; b < 18; b++) {
      const e = btnEl[b], red = b === 16 && leds[18];          // SEQ PLAY: green 16, red 18
      e.classList.toggle("lit", !!leds[b] && b !== 16);
      e.classList.toggle("green", b === 16 && !!leds[16]);
      e.classList.toggle("red", !!red && !leds[16]);
    }
    for (let p = 0; p < 16; p++) {                           // the LEDs' colours through the cream pads
      const c = pads[p];
      let r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
      const lvl = Math.max(r, g, b), k = 255 / Math.max(1, lvl);   // (the firmware's levels: 4 faint .. 110 lit)
      r = Math.round(r * k); g = Math.round(g * k); b = Math.round(b * k);
      const e = padEl[p], bright = lvl >= 80, lum = 0.3 * r + 0.59 * g + 0.11 * b;
      e.style.setProperty("--pc", `${r}, ${g}, ${b}`);
      e.style.setProperty("--pa", lvl ? (bright ? (lum > 235 ? 0.55 : 0.92) : Math.min(0.45, 0.12 + lvl / 110)) : 0);
      e.style.boxShadow = bright ? `0 0 18px 3px rgba(${r},${g},${b},0.9), 0 3px 5px #0005` : "";
      e.style.borderColor = bright ? `rgb(${r},${g},${b})` : "";
      e.firstChild.style.color = bright && lum < 140 ? "#fff" : "";   // (light print on a dark lit colour)
    }
  };
  // the screen's size (index.html's #lcd): the unit's share of the panel, or "Big screen" (remembered in this browser)
  const BIG_KEY = "choralroot-smk37-bigscreen";
  const sizeScreen = () => {
    const dpr = window.devicePixelRatio || 1, big = $("big").checked;
    let css, crisp = true;
    if (big) css = innerWidth >= 2400 ? 480 : 240;
    else {
      const want = Math.max(120, 0.066 * $("smk").clientWidth);   // (the photo: the square under the glass, 6.6 % of the panel)
      const n = Math.floor((want * dpr) / 240);                    // whole device pixels: n of them a firmware pixel
      if (n >= 1) css = (n * 240) / dpr;
      else { css = Math.round(want); crisp = false; }
    }
    lcd.style.width = lcd.style.height = css + "px";
    lcd.classList.toggle("smooth", !crisp);
  };
  try { $("big").checked = localStorage.getItem(BIG_KEY) === "1"; } catch {}
  if (/[?&]big/.test(location.search)) $("big").checked = true;   // (?big: for screenshots)
  $("big").addEventListener("change", () => {
    try { localStorage.setItem(BIG_KEY, $("big").checked ? "1" : "0"); } catch {}
    sizeScreen();
  });
  addEventListener("resize", sizeScreen);
  const drawMaster = () => knobEl[0].firstChild.style.setProperty("--a", (master / 1023 - 0.5) * 270 + "deg");

  // ------------------------------------------------------------------------- the flash (IndexedDB) ---
  const db = () => new Promise((ok, no) => {
    const r = indexedDB.open("choralroot-smk37", 1);
    r.onupgradeneeded = () => r.result.createObjectStore("kv");
    r.onsuccess = () => ok(r.result);
    r.onerror = () => no(r.error);
  });
  const kv = async (mode, f) => {
    const d = await db();
    return new Promise((ok, no) => {
      const t = d.transaction("kv", mode), r = f(t.objectStore("kv"));
      t.oncomplete = () => { d.close(); ok(r.result); };
      t.onerror = () => { d.close(); no(t.error); };
    });
  };
  const flashLoad = () => Promise.race([kv("readonly", (s) => s.get("flash")).catch(() => null), new Promise((ok) => setTimeout(() => ok(null), 1500))]);
  const flashSave = (buf) => kv("readwrite", (s) => s.put(buf, "flash")).catch((e) => status("Could not save the flash: " + e));
  $("reset").onclick = async () => {
    if (!confirm("Forget the settings, sounds and loops saved in this browser?")) return;
    await kv("readwrite", (s) => s.delete("flash")).catch(() => {});
    location.reload();
  };

  // ------------------------------------------------------------------------------------- the wasm ---
  const getBytes = (url) => location.protocol === "file:"
    ? new Promise((ok, no) => {
        const x = new XMLHttpRequest();
        x.open("GET", url); x.responseType = "arraybuffer";
        x.onload = () => (x.response ? ok(x.response) : no(new Error("no " + url)));
        x.onerror = () => no(new Error("cannot load " + url));
        x.send();
      })
    : fetch(url).then((r) => { if (!r.ok) throw new Error(url + ": " + r.status); return r.arrayBuffer(); });
  const wasmP = getBytes("choralroot-smk37.wasm");
  // before Start: the device boots here, silently, to show its screen (?demo: MAJ's pad and C4 held, for screenshots)
  (async () => {
    try {
      const bytes = await wasmP;
      status("Booting…");
      let mem;
      const wasi = { fd_write: (fd, iov, n, out) => { const d = new DataView(mem.buffer); let l = 0; for (let i = 0; i < n; i++) l += d.getUint32(iov + 8 * i + 4, true); d.setUint32(out, l, true); return 0; } };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), { wasi_snapshot_preview1: stub, env: new Proxy({}, { get: () => () => 0 }) });
      const ex = instance.exports;
      mem = ex.memory;
      if (ex._initialize) ex._initialize();
      status("Reading the saved flash…");
      const f = await flashLoad();
      if (f && f.byteLength === ex.web_flash_size()) {
        new Uint8Array(mem.buffer, ex.web_flash_stage(), f.byteLength).set(new Uint8Array(f));
        ex.web_flash_stage_len(f.byteLength);
      }
      ex.web_boot();
      ex.web_step(600);
      if (/demo/.test(location.search)) {          // (and the page shows them held: the pressed look in screenshots)
        ex.web_smk_pad(2, 100); ex.web_step(30); ex.web_smk_key(12, 100); ex.web_step(700);
        keyEl[12].classList.add("down", "held"); padEl[2].classList.add("held");
      }
      if (node) return;
      drawFb(new Uint16Array(mem.buffer, ex.web_fb(), 57600));
      drawLights(new Uint8Array(mem.buffer, ex.web_smk_leds(), 19), new Uint32Array(mem.buffer, ex.web_smk_pads(), 16));
      status("Ready. Press Start for sound.");
    } catch (err) {
      status("Could not load the emulator: " + err.message);
    }
  })();
  $("start").onclick = async () => {
    $("start").disabled = true;
    try {
      if (!window.AudioWorkletNode) throw new Error("this browser has no AudioWorklet (try a current Chrome, Firefox or Safari)");
      const ctx = new AudioContext({ sampleRate: 44100, latencyHint: "playback" });
      await ctx.audioWorklet.addModule("worklet.js");
      node = new AudioWorkletNode(ctx, "choralroot-smk37", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
      node.connect(ctx.destination);
      node.port.onmessage = (e) => onWorklet(e.data);
      const [bytes, flash] = await Promise.all([wasmP, flashLoad()]);
      const panel = [...faderEl.map((e, i) => ({ type: "fader", i, v: +e.value })), { type: "wheel", i: 1, raw: 629 - +mod.value }];
      node.port.postMessage({ type: "load", wasm: bytes.slice(0), flash, panel });
      await ctx.resume();
      status("Starting…");
    } catch (err) {
      $("start").disabled = false;
      status("Could not start: " + err.message);
    }
  };
  const onWorklet = (m) => {
    if (m.type === "frame") {
      if (m.fb) drawFb(m.fb);
      drawLights(m.leds, m.pads);
      if (m.master !== master) { master = m.master; drawMaster(); }
      if (m.load != null) {
        $("load").style.width = Math.min(100, m.load) + "%";
        $("load").style.background = m.load > 80 ? "#E63829" : m.load > 50 ? "#F67918" : "#29B27B";
        $("loadv").textContent = m.load + "%";
      }
    } else if (m.type === "ready") {
      $("gate").hidden = true;
      for (const [c, s] of srcs) if (s.size) { const v = [...s.values()][0], id = +c.slice(1);   // (held before Start)
        post(c[0] === "k" ? { type: "key", k: id, vel: v } : c[0] === "p" ? { type: "pad", p: id, vel: v } : { type: "button", b: id, down: 1 }); }
      status(m.version + " (SMK-37) is running. Hold a chord pad and play a key.");
    } else if (m.type === "midi") midiOut(m.pkts);
    else if (m.type === "flash") flashSave(m.data);
    else if (m.type === "error") status("The emulator failed: " + m.message);
  };

  // --------------------------------------------------------------------------------------- Web MIDI ---
  let access = null, out = null;
  const LEN = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1];
  const midiOut = (pkts) => {
    if (!out) return;
    for (const p of pkts) {
      const cin = p & 15, n = LEN[cin];
      if (!n || (cin >= 4 && cin <= 7)) continue;
      try { out.send([(p >>> 8) & 255, (p >>> 16) & 255, (p >>> 24) & 255].slice(0, n)); } catch {}
    }
  };
  const midiIn = (e) => {
    const [s, a = 0, b = 0] = e.data;
    let cin;
    if (s >= 0x80 && s < 0xf0) cin = s >> 4;
    else if (s >= 0xf8) cin = 0xf;
    else if (s === 0xf2) cin = 3;
    else if (s === 0xf3) cin = 2;
    else return;
    post({ type: "midi", pkts: [(cin | (s << 8) | (a << 16) | (b << 24)) >>> 0] });
  };
  const fill = (sel, ports) => {
    const v = sel.value;
    sel.length = 1;
    for (const p of ports.values()) sel.add(new Option(p.name, p.id));
    sel.value = [...sel.options].some((o) => o.value === v) ? v : "";
  };
  const refresh = () => { fill($("mout"), access.outputs); fill($("min"), access.inputs); };
  $("midi").onclick = async () => {
    if (!navigator.requestMIDIAccess) return status("This browser has no Web MIDI (Chrome, Edge, Firefox and Opera have it).");
    try {
      access = await navigator.requestMIDIAccess();
      access.onstatechange = refresh;
      refresh();
      $("midi").hidden = true;
      $("mout").parentNode.hidden = $("min").parentNode.hidden = false;
    } catch (err) { status("MIDI: " + err.message); }
  };
  $("mout").onchange = (e) => { out = e.target.value ? access.outputs.get(e.target.value) : null; };
  let inPort = null;
  $("min").onchange = (e) => {
    if (inPort) inPort.onmidimessage = null;
    inPort = e.target.value ? access.inputs.get(e.target.value) : null;
    if (inPort) inPort.onmidimessage = midiIn;
  };

  drawSel();
  drawMaster();
  sizeScreen();
  g2.fillStyle = "#000";
  g2.fillRect(0, 0, 240, 240);
})();
