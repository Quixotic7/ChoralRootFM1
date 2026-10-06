// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;
export const STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a";
export const STOCK_APP_SIZE = 0x8DFBC;
export const STOCK_V15_SIZE = 699956;

const T = new Uint16Array(256);
for (let i = 0; i < 256; i++) {
  let c = i << 8;
  for (let k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
  T[i] = c & 0xFFFF;
}

export function crc16(d, crc = 0) {
  for (let i = 0; i < d.length; i++) crc = ((crc << 8) & 0xFFFF) ^ T[((crc >> 8) ^ d[i]) & 0xFF];
  return crc;
}

function enc(buf, off, size, key = 0xFFFF) {
  for (let i = 0; i < size; i++) {
    buf[off + i] ^= key & 0xFF;
    key = ((key * 2) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
}

function sfc(buf, off, size, base, key) {
  for (let i = 0; i < size; i += 32) enc(buf, off + i, Math.min(size - i, 32), key ^ ((off + i - base) >> 2));
}

function chipkeyDecode(d) {
  let sum = 0;
  for (let i = 0; i < 16; i++) sum += d[i];
  sum &= 0xFF;
  sum = sum >= 0xE0 ? 0xAA : sum <= 0x10 ? 0x55 : sum;
  let k = 0;
  for (let i = 0; i < 16; i++) if ((d[16 + i] ^ d[15 - i]) < sum) k |= 1 << i;
  return k;
}

const align32 = (v) => (v + 31) & ~31;
const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w16(b, o, v) { b[o] = v & 0xFF; b[o + 1] = (v >> 8) & 0xFF; }
function w32(b, o, v) { w16(b, o, v & 0xFFFF); w16(b, o + 2, (v >>> 16) & 0xFFFF); }

class Entry {                                   // JLFS head, 32 bytes
  constructor(raw, off) {
    this.hdrOff = off;
    this.hcrc = u16(raw, off);
    this.dcrc = u16(raw, off + 2);
    this.offset = u32(raw, off + 4);
    this.size = u32(raw, off + 8);
    this.flags = raw[off + 12];
    this.resvd = raw[off + 13];
    this.index = u16(raw, off + 14);
    this.rawName = raw.slice(off + 16, off + 32);
    const z = this.rawName.indexOf(0);
    this.name = String.fromCharCode(...this.rawName.slice(0, z < 0 ? 16 : z));
    if (crc16(raw.subarray(off + 2, off + 32)) !== this.hcrc) throw new Error(`JLFS header CRC mismatch @${off.toString(16)}`);
  }
  packInto(buf) {
    const o = this.hdrOff;
    w16(buf, o + 2, this.dcrc); w32(buf, o + 4, this.offset); w32(buf, o + 8, this.size);
    buf[o + 12] = this.flags; buf[o + 13] = this.resvd; w16(buf, o + 14, this.index);
    buf.set(this.rawName, o + 16);
    this.hcrc = crc16(buf.subarray(o + 2, o + 32));
    w16(buf, o, this.hcrc);
  }
}

export class Package {
  constructor(bytes) {
    this.raw = new Uint8Array(bytes);
    this.parseUfw();
    this.flash = this.raw.slice(this.flashFileOff, this.flashFileOff + this.flashSize);
    this.parseFlash();
  }

  get product() { return productOf(this.raw); }

  setProduct(name) {
    if (name.length > BLOCKS) throw new Error("product name too long");
    for (let i = 0; i < BLOCKS; i++) {
      const m = i < name.length ? (name.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
      if (i < name.length && m === 0x7D) throw new Error("product name encodes to the unused marker");
      this.raw[i * BLK + KEEP] = m;
    }
  }

  headerBytes() {
    const h = new Uint8Array(BLOCKS * KEEP);
    for (let i = 0; i < BLOCKS; i++) h.set(this.raw.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
    return h;
  }

  hdrFileOff(i) { return Math.floor(i / KEEP) * BLK + (i % KEEP); }

  parseUfw() {
    const h = this.headerBytes();
    enc(h, 0, 0x40);
    const hcrc = u16(h, 0), lcrc = u16(h, 2), nent = u16(h, 8);
    if (crc16(h.subarray(2, 0x40)) !== hcrc) throw new Error("not an FM-1 .fwsc (UFW header CRC mismatch)");
    this.skew = BLOCKS * BLK - h.length;
    this.ufw = h;
    this.nent = nent;
    const hsize = 0x40 + nent * 0x50;
    if (crc16(h.subarray(0x40, hsize)) !== lcrc) throw new Error("UFW entry list CRC mismatch");
    this.entries = [];
    for (let off = 0x40; off < hsize; off += 0x50) {
      const e = h.slice(off, off + 0x50);
      enc(e, 0, 0x50);
      const z = e.subarray(0x40, 0x50).indexOf(0);
      this.entries.push({ off, type: u16(e, 0), dcrc: u16(e, 4), dataOff: u32(e, 8), size: u32(e, 12),
        name: String.fromCharCode(...e.subarray(0x40, z < 0 ? 0x50 : 0x40 + z)), plain: e });
    }
    const fl = this.entries.find((e) => e.type === 0);
    if (!fl) throw new Error("no flash.bin in the package");
    this.flashEntry = fl;
    this.flashFileOff = fl.dataOff + this.skew;
    this.flashSize = fl.size;
    if (crc16(this.raw.subarray(this.flashFileOff, this.flashFileOff + fl.size)) !== fl.dcrc) throw new Error("flash.bin CRC mismatch");
  }

  parseFlash() {
    const dec = this.flash.slice();
    const h = dec.slice(0, 32);
    enc(h, 0, 32);
    if (crc16(h.subarray(2, 32)) !== u16(h, 0)) throw new Error("flash header CRC mismatch");
    this.chipKey = null; this.appBase = null;
    for (let off = 32; ; off += 32) {
      enc(dec, off, 32);
      const e = new Entry(dec, off);
      if (e.name === "isd_config.ini") {
        const blob = dec.subarray(e.offset, e.offset + 34);
        if (crc16(blob.subarray(0, 32)) === u16(blob, 32)) this.chipKey = chipkeyDecode(blob.subarray(0, 32));
      }
      if (e.flags === 0x81 && e.name === "app_dir_head" && this.appBase === null) this.appBase = e.offset;
      if (e.index) break;
    }
    if (this.chipKey === null || this.appBase === null) throw new Error("chip key or app_dir_head not found");
    const b = this.appBase, k = this.chipKey, end = align32(b + 32);
    sfc(dec, b, end - b, b, k);
    const head = new Entry(dec, b);
    const blkEnd = b + head.size;
    if (blkEnd > end) sfc(dec, end, blkEnd - end, b, k);
    this.sfcEnd = Math.max(end, blkEnd);
    this.cfgLen = 0;
    const probe = this.flash.slice(b, align32(blkEnd) + 32);
    sfc(probe, 0, probe.length, 0, k);
    try {
      const cfg = new Entry(probe, blkEnd - b);
      if (cfg.name === "cfg" && blkEnd + cfg.size <= this.flash.length) this.cfgLen = cfg.size;
    } catch (_) { /* no cfg block */ }
    if (this.cfgLen) {
      const region = this.flash.slice(b, blkEnd + this.cfgLen);
      sfc(region, 0, region.length, 0, k);
      dec.set(region, b);
      this.sfcEnd = blkEnd + this.cfgLen;
    }
    this.head = head;
    this.app = null;
    for (let off = b + 32; ; off += 32) {
      const e = new Entry(dec, off);
      if (e.flags === 0x82 && e.name === "app.bin") this.app = e;
      if (e.index) break;
    }
    if (!this.app) throw new Error("app.bin not found in app area");
    this.appOff = b + this.app.offset;
    this.dec = dec;
  }

  // the new app, padded to the stock size (Felucca never changes the layout)
  setApp(app) {
    if (app.length > this.app.size) throw new Error(`app is ${app.length} B, the slot is ${this.app.size} B`);
    const nw = new Uint8Array(this.app.size).fill(0xFF);
    nw.set(app);
    const a = this.app, h = this.head, b = this.appBase;
    this.dec.set(nw, this.appOff);
    a.dcrc = crc16(nw);
    a.packInto(this.dec);
    h.dcrc = crc16(this.dec.subarray(h.hdrOff + 32, h.hdrOff + h.size));
    h.packInto(this.dec);
    this.sfcEnd = Math.max(align32(b + 32), b + h.size + this.cfgLen);
    const region = this.dec.slice(b, this.sfcEnd);
    sfc(region, 0, region.length, 0, this.chipKey);
    this.flash.set(region, b);
  }

  serialize() {
    const out = this.raw.slice();
    out.set(this.flash, this.flashFileOff);
    const fl = this.flashEntry, e = fl.plain.slice();
    w16(e, 4, crc16(this.flash));
    enc(e, 0, 0x50);
    const h = this.ufw.slice();
    h.set(e, fl.off);
    const hsize = 0x40 + this.nent * 0x50;
    w16(h, 2, crc16(h.subarray(0x40, hsize)));
    w16(h, 0, crc16(h.subarray(2, 0x40)));
    enc(h, 0, 0x40);
    for (let i = 0; i < hsize; i++) out[this.hdrFileOff(i)] = h[i];
    return out;
  }
}

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS);
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP);
  return out;
}

export async function sha256hex(bytes) {
  const d = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// Only the user's unmodified official V15 is accepted for a return to stock.
// The digest pins every file, loader and flash layout; filenames are not evidence.
export async function validateStockPackage(bytes) {
  if (bytes.length !== STOCK_V15_SIZE || await sha256hex(bytes) !== STOCK_V15_SHA256)
    throw new Error("Select the unmodified official FM-1 V15 file (FM-1.fwsc).");
  const pkg = new Package(bytes);
  if (pkg.product !== "FM-1_015" || pkg.flashFileOff !== 1044 || pkg.flashSize !== 602112 ||
      pkg.appBase !== 16384 || pkg.appOff !== 16672 || pkg.app.size !== STOCK_APP_SIZE ||
      !pkg.entries.some((e) => e.type === 100 && e.name === "ota.bin" && e.size === 19969))
    throw new Error("The official firmware has an unsupported FM-1 layout.");
  return { product: pkg.product, image: logicalImage(bytes), sha256: STOCK_V15_SHA256 };
}

// official V15 + Felucca app -> installable package (Uint8Array)
export async function buildFeluccaPackage(stockBytes, appBytes, product) {
  const sha = await sha256hex(stockBytes);
  if (sha !== STOCK_V15_SHA256) throw new Error("this is not the official FM-1 V15 firmware file (FM-1.fwsc)");
  const pkg = new Package(stockBytes);
  pkg.setApp(appBytes);
  pkg.setProduct(product);
  return pkg.serialize();
}
