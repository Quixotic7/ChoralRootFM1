// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 in an AudioWorklet: choralroot.wasm is the emulator's firmware side (tools/emu/web/emu_web.c).
// Each render quantum asks it for 128 frames, which runs the device clock forward (the 1 ms tick, a UI frame every
// 15 ms, the audio blocks). Between quanta the page's input goes in; about 30 times a second the LEDs (and the
// LCD when it was written) go out; MIDI out packets go out as they come; the flash goes out a second after it
// last changed (the page keeps it in IndexedDB).

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();

class FM1 extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sentWrites = -1;
    this.lastPub = 0;
    this.busy = 0;
    this.frames = 0;
    this.midiIn = [];
    this.dirty = 0;
    this.dirtyAt = -1;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      const wasi = {
        clock_time_get: (id, prec, out) => { new DataView(this.mem.buffer).setBigUint64(out, BigInt(Math.round(currentTime * 1e9)), true); return 0; },
        fd_write: (fd, iov, n, out) => {     // the firmware's printf: dropped, but counted as written
          const dv = new DataView(this.mem.buffer);
          let len = 0;
          for (let i = 0; i < n; i++) len += dv.getUint32(iov + 8 * i + 4, true);
          dv.setUint32(out, len, true);
          return 0;
        },
        proc_exit: () => {},
      };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      const env = new Proxy({}, { get: () => () => 0 });
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: stub, env });
        const ex = (this.ex0 = instance.exports);
        this.mem = ex.memory;
        if (ex._initialize) ex._initialize();
        if (m.flash && m.flash.byteLength === ex.web_flash_size()) {
          new Uint8Array(this.mem.buffer, ex.web_flash_stage(), m.flash.byteLength).set(new Uint8Array(m.flash));
          ex.web_flash_stage_len(m.flash.byteLength);
        }
        if (m.master != null) ex.web_master(m.master | 0);
        ex.web_boot();
        this.dirty = ex.web_flash_dirty();
        const v = new Uint8Array(this.mem.buffer, ex.web_version(), 64);
        this.ex = ex;
        this.port.postMessage({ type: "ready", version: String.fromCharCode(...v.subarray(0, v.indexOf(0))) });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String(err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    switch (m.type) {
      case "keys": ex.web_keys(m.mask >>> 0); break;
      case "buttons": ex.web_buttons(m.mask >>> 0); break;
      case "tap": ex.web_keys_tap(m.keys >>> 0); ex.web_buttons_tap(m.buttons >>> 0); break;
      case "enc": ex.web_enc(m.role | 0, m.n | 0); break;
      case "master": ex.web_master(m.value | 0); break;
      case "midi": for (const p of m.pkts) this.midiIn.push(p >>> 0); break;
    }
  }

  publish() {
    const ex = this.ex;
    const msg = { type: "frame", leds: new Uint8Array(this.mem.buffer, ex.web_leds(), 42).slice(), master: ex.web_master_get() };
    const transfer = [];
    if (this.frames >= 22050) {                      // the share of real time spent running the device
      msg.load = Math.round((100 * this.busy) / ((this.frames / sampleRate) * 1000));
      this.busy = this.frames = 0;
    }
    const writes = ex.web_lcd_writes();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    const d = ex.web_flash_dirty();                  // the flash: saved a second after the last erase / program
    if (d !== this.dirty) { this.dirty = d; this.dirtyAt = currentTime; }
    else if (this.dirtyAt >= 0 && currentTime - this.dirtyAt > 1) {
      this.dirtyAt = -1;
      const f = new Uint8Array(this.mem.buffer, ex.web_flash(), ex.web_flash_size()).slice();
      this.port.postMessage({ type: "flash", data: f.buffer }, [f.buffer]);
    }
    this.port.postMessage(msg, transfer);
  }

  process(inputs, outputs) {
    const ex = this.ex;
    if (!ex) return true;
    const out = outputs[0];
    const n = out[0].length;
    const t0 = clock();
    while (this.midiIn.length && ex.web_midi_in(this.midiIn[0])) this.midiIn.shift();
    ex.web_render(n);
    let pkts = null;
    for (let p; (p = ex.web_midi_out_take()); ) (pkts ||= []).push(p >>> 0);
    this.busy += clock() - t0;
    this.frames += n;
    out[0].set(new Float32Array(this.mem.buffer, ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.mem.buffer, ex.web_out_r(), n));
    if (pkts) this.port.postMessage({ type: "midi", pkts });
    if (currentTime - this.lastPub >= 1 / 30) {
      this.lastPub = currentTime;
      this.publish();
    }
    return true;
  }
}

registerProcessor("choralroot-fm1", FM1);
