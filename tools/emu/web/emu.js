// SPDX-License-Identifier: GPL-3.0-only
// The page of the browser emulator: the LCD, the FM-1 panel (tools/emu/emu.c's drawing, in the geometry of
// ChoralRootFM1Designer), the input (pointer, touch, keyboard, wheel, Web MIDI) and the flash in IndexedDB.
// The device runs in worklet.js; before Start a copy boots here, without sound, to show its screen.
"use strict";
(() => {
  const { KEYMAP, BTN_ROLE, ENC_ROLE, CHORD, noteName } = globalThis.FM1;
  const $ = (id) => document.getElementById(id);
  const NS = "http://www.w3.org/2000/svg";
  const C = { cream: "#F6F2EE", red: "#E63829", orange: "#F67918", yellow: "#F6B600", blue: "#2950CD", green: "#29B27B",
              grey: "#6A696A", hint: "#8C8B8C", body: "#1B1B1B", bed: "#101010", cap: "#2E2D2E", capOn: "#4A494A", edge: "#000", led: "#0C0C0C" };
  const status = (t) => ($("status").textContent = t);

  // ------------------------------------------------------------------------------------- geometry ---
  const WHITE_X = [71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794];
  const BLACK_X = [95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769];
  const black = (k) => (0x54a >> ((k + 5) % 12)) & 1;
  const KEYR = [];
  for (let k = 0, w = 0, b = 0; k < 27; k++) KEYR.push(black(k) ? [BLACK_X[b++], 326, 42, 76] : [WHITE_X[w++], 408, 42, 76]);
  const BTNR = [];
  for (let i = 0; i < 12; i++) BTNR.push([[500, 554, 608, 661, 715, 768][i % 6], [177, 231][(i / 6) | 0], 36, 36]);
  BTNR.push([87, 250, 42, 22], [153, 250, 42, 22]);
  const ENC_CX = [186.5, 186.5, 95, 508, 604, 701, 797, 95], ENC_CY = [101, 191, 191, 101, 101, 101, 101, 101];
  const SCREEN_A = [272, 84, 150, 150], VIEW = [14, 14, 876, 538];
  const hint = (kind, idx) => (KEYMAP.find((m) => m.kind === kind && m.idx === idx) || {}).cap;
  // role colours: white chords, blue voicing, orange bass and 7ths, red loop / REC, yellow key, green FX
  const CHORD_COL = { DIM: C.cream, MIN: C.cream, MAJ: C.cream, SUS: C.cream, 6: C.orange, m7: C.orange, M7: C.orange, 9: C.orange, LOCK: C.blue };
  const BTN_COL = { FX: C.green, KEY: C.yellow, BASS: C.orange, LOOP: C.red, REC: C.red };
  const ENC_COL = ["#F6F2EE", C.orange, C.cream, C.blue, C.orange, C.cream, C.green, C.cream];

  // ------------------------------------------------------------------------------------ the panel ---
  const svg = $("panel");
  const el = (tag, attrs, parent = svg) => {
    const e = document.createElementNS(NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    parent.appendChild(e);
    return e;
  };
  const rect = (r, rad, fill, p, extra = {}) => el("rect", { x: r[0], y: r[1], width: r[2], height: r[3], rx: rad, fill, ...extra }, p);
  const text = (x, y, s, size, fill, p, weight = 800) =>
    Object.assign(el("text", { x, y, "font-size": size, fill, "text-anchor": "middle", "font-weight": weight }, p), { textContent: s });
  rect([22, 22, 860, 522], 38, C.body, svg, { stroke: "#2A2A2A", "stroke-width": 2 });
  rect([52, 306, 800, 198], 20, C.bed);
  rect([484, 162, 338, 124], 8, C.bed);
  rect([77, 243, 127, 36], 6, C.bed);
  rect([247, 59, 201, 201], 10, "#000");
  text(150, 300, "CHORDS", 9, C.grey, svg, 900);
  const keyEl = [], btnEl = [], encEl = [];
  for (let k = 0; k < 27; k++) {
    const r = KEYR[k], cx = r[0] + 21, g = el("g", { "data-c": "k" + k });
    const cap = rect(r, 21, C.cap, g, { stroke: C.edge, "stroke-width": 1.5 });
    const glow = rect([cx - 9, r[1] + 9, 18, 40], 9, C.cream, g, { opacity: 0 });
    const led = rect([cx - 4, r[1] + 14, 8, 30], 4, C.led, g);
    const lab = CHORD[k];
    text(cx, r[1] + 58, lab || noteName(k), lab ? (lab.length > 3 ? 9 : 11) : 8.5, lab ? CHORD_COL[lab] : "#9A999A", g);
    const h = hint("key", k);
    if (h) text(cx, r[1] + 69, h, 8, C.hint, g, 700);
    keyEl.push({ cap, glow, led });
  }
  for (let i = 0; i < 14; i++) {
    const r = BTNR[i], oct = i >= 12, g = el("g", { "data-c": "b" + i });
    const role = BTN_ROLE[i];
    const cap = rect(r, oct ? 6 : 5, C.cap, g, { stroke: C.edge, "stroke-width": 1.5 });
    const t = text(r[0] + r[2] / 2, r[1] + (oct ? 15 : 20), role, oct ? 9 : role.length > 4 ? 8.5 : 10, BTN_COL[role] || C.cream, g);
    const h = hint("btn", i);
    const ht = h ? text(r[0] + r[2] / 2, oct ? r[1] + r[3] + 10 : r[1] + 31, h === "RSHIFT" ? "SHIFT" : h === "RETURN" ? "\u23ce" : h === "BKSP" ? "⌫" : h, 6.5, C.grey, g, 700) : null;
    const dot = i === 10 ? el("circle", { cx: r[0] + 6, cy: r[1] + r[3] - 6, r: 3.2, fill: C.led }, g) : null;
    btnEl.push({ cap, t, ht, dot, col: i === 11 ? C.red : i === 10 ? C.orange : C.cream, tcol: BTN_COL[role] || C.cream });
  }
  for (let i = 0; i < 8; i++) {
    const cx = ENC_CX[i], cy = ENC_CY[i], g = el("g", { "data-c": "e" + i });
    const sel = el("circle", { cx, cy, r: 25, fill: "none", stroke: C.yellow, "stroke-width": 2.5, opacity: 0 }, g);
    el("circle", { cx, cy, r: 21, fill: "#262526" }, g);
    el("circle", { cx, cy, r: 18, fill: "none", stroke: "#3E3D3E", "stroke-width": 2.5, "stroke-dasharray": "2 2.2" }, g);
    el("circle", { cx, cy, r: 12, fill: "#3A393A" }, g);
    const ptr = el("line", { x1: cx, y1: cy - 4, x2: cx, y2: cy - 11, stroke: i === 7 ? "#fff" : C.cream, "stroke-width": 2.4, "stroke-linecap": "round" }, g);
    text(cx, cy - 28, ENC_ROLE[i], 9, ENC_COL[i], g, 900);
    text(cx, cy + 35, hint("sel", i), 8, C.hint, g, 700);
    encEl.push({ sel, ptr, a: 0 });
  }

  // the panel's own little screen: a second canvas over SCREEN_A
  const mini = $("mini");
  const placeMini = () => {
    const s = svg.getBoundingClientRect(), k = s.width / VIEW[2];
    Object.assign(mini.style, { left: (SCREEN_A[0] - VIEW[0]) * k + "px", top: (SCREEN_A[1] - VIEW[1]) * k + "px",
                                width: SCREEN_A[2] * k + "px", height: SCREEN_A[3] * k + "px" });
  };
  addEventListener("resize", placeMini);
  placeMini();

  // ------------------------------------------------------------------------------- LCD and LEDs ---
  const lcd = $("lcd"), g2 = lcd.getContext("2d"), m2 = mini.getContext("2d");
  const img = g2.createImageData(240, 240), px = new Uint32Array(img.data.buffer);
  const LUT = new Uint32Array(65536);
  for (let u = 0; u < 65536; u++) {   // a big-endian RGB565 word as read little-endian -> RGBA
    const c = ((u >> 8) | (u << 8)) & 0xffff;
    const r = (((c >> 11) & 31) * 255 + 15) / 31, g = (((c >> 5) & 63) * 255 + 31) / 63, b = ((c & 31) * 255 + 15) / 31;
    LUT[u] = 0xff000000 | (b << 16) | (g << 8) | r;
  }
  const drawFb = (fb) => {
    for (let i = 0; i < 57600; i++) px[i] = LUT[fb[i]];
    g2.putImageData(img, 0, 0);
    m2.drawImage(lcd, 0, 0);
  };
  const LEVEL = [0, 0.38, 1];
  const drawLeds = (L) => {
    for (let k = 0; k < 27; k++) {
      const e = keyEl[k], v = L[k];
      e.led.setAttribute("fill", v ? C.cream : C.led);
      e.led.setAttribute("opacity", v ? LEVEL[v] : 1);
      e.glow.setAttribute("opacity", v === 2 ? 0.16 : 0);
    }
    for (let i = 0; i < 14; i++) {
      const e = btnEl[i], v = L[27 + i];
      e.cap.setAttribute("fill", v === 2 ? e.col : v === 1 ? mix(e.col, 0.38) : C.cap);
      e.t.setAttribute("fill", v === 2 ? "#111" : v === 1 ? C.cream : e.tcol);
      if (e.ht) e.ht.setAttribute("fill", v === 2 ? "#333" : C.hint);
    }
    btnEl[10].dot.setAttribute("fill", L[41] ? C.green : C.led);
  };
  const mix = (hex, a) => {   // the colour at a over the cap
    const p = (h, i) => parseInt(h.slice(i, i + 2), 16), cap = C.cap;
    const ch = (i) => Math.round(p(cap, i) + (p(hex, i) - p(cap, i)) * a).toString(16).padStart(2, "0");
    return "#" + ch(1) + ch(3) + ch(5);
  };
  let master = 724;
  const drawKnobs = () => encEl.forEach((e, i) => {
    const a = i === 7 ? (master / 1023 - 0.5) * 1.5 * Math.PI : e.a;
    const cx = ENC_CX[i], cy = ENC_CY[i], s = Math.sin(a), c = Math.cos(a);
    e.ptr.setAttribute("x1", cx + 4 * s); e.ptr.setAttribute("y1", cy - 4 * c);
    e.ptr.setAttribute("x2", cx + 11 * s); e.ptr.setAttribute("y2", cy - 11 * c);
    e.sel.setAttribute("opacity", i === selKnob ? 1 : 0);
  });

  // ----------------------------------------------------------------------------------- the input ---
  const keySrc = Array.from({ length: 27 }, () => new Set()), btnSrc = Array.from({ length: 14 }, () => new Set());
  let selKnob = 2, node = null, sentK = -1, sentB = -1;
  const post = (m) => node && node.port.postMessage(m);
  const publish = () => {
    let k = 0, b = 0;
    keySrc.forEach((s, i) => s.size && (k |= 1 << i));
    btnSrc.forEach((s, i) => s.size && (b |= 1 << i));
    if (k !== sentK) post({ type: "keys", mask: (sentK = k) >>> 0 });
    if (b !== sentB) post({ type: "buttons", mask: (sentB = b) >>> 0 });
    keyEl.forEach((e, i) => e.cap.setAttribute("stroke", keySrc[i].size ? C.yellow : C.edge));
    btnEl.forEach((e, i) => e.cap.setAttribute("stroke", btnSrc[i].size ? C.yellow : C.edge));
    keyEl.forEach((e, i) => e.cap.setAttribute("fill", keySrc[i].size ? C.capOn : C.cap));
  };
  const hold = (c, src, down) => {   // c: "k5" / "b10"
    const set = (c[0] === "k" ? keySrc : btnSrc)[+c.slice(1)];
    if (!set) return;
    down ? set.add(src) : set.delete(src);
    publish();
  };
  const releaseSrc = (test) => {
    for (const s of [...keySrc, ...btnSrc]) for (const x of [...s]) if (test(x)) s.delete(x);
    publish();
  };
  const turn = (role, n) => {
    if (!n) return;
    if (role === 7) master = Math.max(0, Math.min(1023, master + 16 * n));
    else encEl[role].a += (n * Math.PI) / 12;
    post({ type: "enc", role, n });
    drawKnobs();
  };

  // pointer: keys and buttons press while held (multi-touch), a right-click / ctrl-click latches; knobs: drag / wheel
  const ptrs = new Map();
  const ctlAt = (e) => { const g = e.target.closest && e.target.closest("[data-c]"); return g ? g.getAttribute("data-c") : null; };
  svg.addEventListener("contextmenu", (e) => e.preventDefault());
  svg.addEventListener("pointerdown", (e) => {
    const c = ctlAt(e);
    if (!c) return;
    e.preventDefault();
    if (c[0] === "e") {
      selKnob = +c.slice(1);
      drawKnobs();
      ptrs.set(e.pointerId, { c, y: e.clientY, acc: 0 });
      svg.setPointerCapture(e.pointerId);
      return;
    }
    if (e.button === 2 || e.ctrlKey) {   // latch
      const set = (c[0] === "k" ? keySrc : btnSrc)[+c.slice(1)];
      hold(c, "latch", !set.has("latch"));
      return;
    }
    ptrs.set(e.pointerId, { c });
    hold(c, "p" + e.pointerId, true);
  });
  svg.addEventListener("pointermove", (e) => {
    const p = ptrs.get(e.pointerId);
    if (!p || p.c[0] !== "e") return;
    const k = svg.getBoundingClientRect().width / VIEW[2];
    p.acc += (p.y - e.clientY) / k;
    p.y = e.clientY;
    const n = Math.trunc(p.acc / 7);
    if (n) { p.acc -= n * 7; turn(+p.c.slice(1), n); }
  });
  const up = (e) => {
    const p = ptrs.get(e.pointerId);
    if (!p) return;
    ptrs.delete(e.pointerId);
    if (p.c[0] !== "e") hold(p.c, "p" + e.pointerId, false);
  };
  for (const t of ["pointerup", "pointercancel"]) addEventListener(t, up);
  svg.addEventListener("wheel", (e) => {
    const c = ctlAt(e);
    if (!c || c[0] !== "e") return;
    e.preventDefault();
    turn(+c.slice(1), e.deltaY < 0 ? 1 : -1);
  }, { passive: false });

  // the computer keyboard (tools/emu/keymap.c)
  const byCode = Object.fromEntries(KEYMAP.map((m) => [m.code, m]));
  const typing = (e) => /^(SELECT|INPUT|TEXTAREA)$/.test(e.target.tagName) || e.metaKey || e.altKey;
  addEventListener("keydown", (e) => {
    const m = byCode[e.code];
    if (!m || typing(e) || (e.ctrlKey && m.kind !== "key")) return;
    e.preventDefault();
    if (e.repeat && m.kind !== "turn") return;
    if (m.kind === "key") hold("k" + m.idx, "kb", true);
    else if (m.kind === "btn") hold("b" + m.idx, "kb" + m.code, true);
    else if (m.kind === "both") { hold("b12", "esc", true); hold("b13", "esc", true); }
    else if (m.kind === "sel") { selKnob = m.idx; drawKnobs(); }
    else if (m.kind === "turn") turn(selKnob, selKnob === 7 ? 2 * m.idx : m.idx);
  });
  addEventListener("keyup", (e) => {
    const m = byCode[e.code];
    if (!m) return;
    if (m.kind === "key") hold("k" + m.idx, "kb", false);
    else if (m.kind === "btn") hold("b" + m.idx, "kb" + m.code, false);
    else if (m.kind === "both") { hold("b12", "esc", false); hold("b13", "esc", false); }
  });
  addEventListener("blur", () => releaseSrc((s) => s.startsWith("kb") || s === "esc"));   // no stuck notes

  // ------------------------------------------------------------------------- the flash (IndexedDB) ---
  const db = () => new Promise((ok, no) => {
    const r = indexedDB.open("choralroot-fm1", 1);
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
  const flashLoad = () => Promise.race([   // (storage can be blocked or hang: then a fresh flash)
    kv("readonly", (s) => s.get("flash")).catch(() => null), new Promise((ok) => setTimeout(() => ok(null), 1500))]);
  const flashSave = (buf) => kv("readwrite", (s) => s.put(buf, "flash")).catch((e) => status("Could not save the flash: " + e));
  $("reset").onclick = async () => {
    if (!confirm("Forget the settings, sounds and loops saved in this browser?")) return;
    await kv("readwrite", (s) => s.delete("flash")).catch(() => {});
    location.reload();
  };

  // ------------------------------------------------------------------------------------- the wasm ---
  const getBytes = (url) => location.protocol === "file:"
    ? new Promise((ok, no) => {   // (fetch cannot read file://; XHR can with --allow-file-access-from-files)
        const x = new XMLHttpRequest();
        x.open("GET", url); x.responseType = "arraybuffer";
        x.onload = () => (x.response ? ok(x.response) : no(new Error("no " + url)));
        x.onerror = () => no(new Error("cannot load " + url));
        x.send();
      })
    : fetch(url).then((r) => { if (!r.ok) throw new Error(url + ": " + r.status); return r.arrayBuffer(); });
  const wasmP = getBytes("choralroot.wasm");

  // before Start: the device boots here, silently, to show its screen (?demo: MAJ + D4 held, for screenshots)
  (async () => {
    try {
      const bytes = await wasmP;
      status("Booting\u2026");
      let mem;
      const wasi = { fd_write: (fd, iov, n, out) => { const d = new DataView(mem.buffer); let l = 0; for (let i = 0; i < n; i++) l += d.getUint32(iov + 8 * i + 4, true); d.setUint32(out, l, true); return 0; } };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      // (synchronous: a headless screenshot's virtual time does not wait for an asynchronous compile)
      const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), { wasi_snapshot_preview1: stub, env: new Proxy({}, { get: () => () => 0 }) });
      const ex = instance.exports;
      mem = ex.memory;
      if (ex._initialize) ex._initialize();
      status("Reading the saved flash\u2026");
      const f = await flashLoad();
      if (f && f.byteLength === ex.web_flash_size()) {
        new Uint8Array(mem.buffer, ex.web_flash_stage(), f.byteLength).set(new Uint8Array(f));
        ex.web_flash_stage_len(f.byteLength);
      }
      ex.web_boot();
      ex.web_step(600);
      if (/demo/.test(location.search)) { ex.web_keys((1 << 5) | (1 << 9)); ex.web_step(700); }
      if (node) return;
      drawFb(new Uint16Array(mem.buffer, ex.web_fb(), 57600));
      drawLeds(new Uint8Array(mem.buffer, ex.web_leds(), 42));
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
      node = new AudioWorkletNode(ctx, "choralroot-fm1", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
      node.connect(ctx.destination);
      node.port.onmessage = (e) => onWorklet(e.data);
      const [bytes, flash] = await Promise.all([wasmP, flashLoad()]);
      node.port.postMessage({ type: "load", wasm: bytes.slice(0), flash, master });
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
      drawLeds(m.leds);
      if (m.master !== master) { master = m.master; drawKnobs(); }
      if (m.load != null) {
        $("load").style.width = Math.min(100, m.load) + "%";
        $("load").style.background = m.load > 80 ? C.red : m.load > 50 ? C.orange : C.green;
        $("loadv").textContent = m.load + "%";
      }
    } else if (m.type === "ready") {
      $("gate").hidden = true;
      sentK = sentB = -1;
      publish();
      status(m.version + " is running. Hold a chord key and press a root.");
    } else if (m.type === "midi") midiOut(m.pkts);
    else if (m.type === "flash") flashSave(m.data);
    else if (m.type === "error") status("The emulator failed: " + m.message);
  };

  // --------------------------------------------------------------------------------------- Web MIDI ---
  let access = null, out = null;
  const LEN = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1];   // bytes per USB-MIDI code index
  const midiOut = (pkts) => {
    if (!out) return;
    for (const p of pkts) {
      const cin = p & 15, n = LEN[cin];
      if (!n || (cin >= 4 && cin <= 7)) continue;                   // (no SysEx without its permission)
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

  drawKnobs();
  g2.fillStyle = "#000";
  g2.fillRect(0, 0, 240, 240);
})();
