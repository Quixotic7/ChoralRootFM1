// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot on the SMK-37 in an AudioWorklet: choralroot-smk37.wasm is the emulator's firmware side built for the
// SMK-37 (tools/emu/web/emu_web.c with EMU_SMK37: the BOARD_SMK37 firmware on tools/emu/emu_smk_hal.h's panel). As
// the FM-1's worklet (../worklet.js): each render quantum runs the device clock; between quanta the page's input goes
// in; about 30 times a second the button LEDs, the pads' colours, K1's volume (and the LCD when it was written) go
// out; MIDI out as it comes; the flash a second after it last changed (the page keeps it in IndexedDB).

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();

class SMK37 extends AudioWorkletProcessor {
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
        fd_write: (fd, iov, n, out) => {
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
        const ex = instance.exports;
        this.mem = ex.memory;
        if (ex._initialize) ex._initialize();
        if (m.flash && m.flash.byteLength === ex.web_flash_size()) {
          new Uint8Array(this.mem.buffer, ex.web_flash_stage(), m.flash.byteLength).set(new Uint8Array(m.flash));
          ex.web_flash_stage_len(m.flash.byteLength);
        }
        ex.web_boot();
        if (m.panel) for (const p of m.panel) this.input(ex, p);   // (what the page holds: faders, wheels)
        this.dirty = ex.web_flash_dirty();
        const v = new Uint8Array(this.mem.buffer, ex.web_version(), 64);
        this.ex = ex;
        this.port.postMessage({ type: "ready", version: String.fromCharCode(...v.subarray(0, v.indexOf(0))) });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String(err) });
      }
      return;
    }
    if (this.ex) this.input(this.ex, m);
  }

  input(ex, m) {
    switch (m.type) {
      case "key": ex.web_smk_key(m.k | 0, m.vel | 0); break;
      case "button": ex.web_smk_button(m.b | 0, m.down ? 1 : 0); break;
      case "enc": ex.web_smk_enc(m.e | 0, m.n | 0); break;
      case "pad": ex.web_smk_pad(m.p | 0, m.vel | 0); break;
      case "fader": ex.web_smk_fader(m.i | 0, m.v | 0); break;
      case "wheel": ex.web_smk_wheel(m.i | 0, m.raw | 0); break;
      case "pedal": ex.web_smk_pedal(m.raw | 0); break;
      case "midi": for (const p of m.pkts) this.midiIn.push(p >>> 0); break;
    }
  }

  publish() {
    const ex = this.ex;
    const msg = { type: "frame", leds: new Uint8Array(this.mem.buffer, ex.web_smk_leds(), 19).slice(),
                  pads: new Uint32Array(this.mem.buffer, ex.web_smk_pads(), 16).slice(), master: ex.web_smk_master() };
    const transfer = [];
    if (this.frames >= 22050) {
      msg.load = Math.round((100 * this.busy) / ((this.frames / sampleRate) * 1000));
      this.busy = this.frames = 0;
    }
    const writes = ex.web_lcd_writes();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    const d = ex.web_flash_dirty();
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

registerProcessor("choralroot-smk37", SMK37);
