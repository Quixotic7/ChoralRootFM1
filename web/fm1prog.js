// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Progressions (GitHub issue 64, Ripchord style): with KEY layer > Keyboard: Prog every key of the keybed plays the chord
// stored for it in the selected one of 16 progressions. This module is the web side of firmware/src/cr_prog.h (the
// contract; docs/PROGRESSIONS.md): the object of backup id 24 encoded / decoded with crp_valid's rules, Ripchord's
// .rpc presets read and written, the chord names, the capacity. No DOM: web/manager.html draws, this decides. Every
// top-level name is unique across fm1backup.js / fm1dnsnd.js / fm1sounds.js / fm1manager.js (make_site.py inlines them
// into one script).
//
// A progression here: {name (0..12 printable ASCII), base (the Ripchord trigger note of key 0, 0..127), keys: [[notes
// ascending, distinct, 0..127, at most 8] per key, [] = a silent key]} (at most 48 keys; no key = an empty slot).
// The object (cr_prog.h, little-endian): header 16 bytes (magic "CRP1", version 1, n 16, used u16 = the object's
// length, rsv[8] 0), 16 directory entries of 16 bytes (name[12] 0-padded, base, nkeys, off u16), then per used slot
// nkeys records [n][n notes]. crp_valid: length == used, 272 <= used <= 3840; an empty slot has off 0 and a name of
// zeros; a used slot's name bytes are 0x20..0x7E then zeros (an all-zero name is allowed), base <= 127, nkeys <= 48,
// 272 <= off < used, every record inside used, n <= 8, notes strictly ascending and <= 127; the used slots' data do
// not overlap (gaps are allowed). The encoder writes the used slots' data in slot order, contiguous from 272.
import { readBackup } from "./fm1backup.js";

export const PROG_ID = 24;                 // cr_prog.h CRPG_BACKUP_ID
export const PROG_MAGIC = 0x31505243;      // "CRP1"
export const PROG_VERSION = 1;
export const PROG_N = 16, PROG_KEYS = 48, PROG_NOTES = 8, PROG_NAME = 12;
export const PROG_HDR = 16, PROG_DIRENT = 16, PROG_DATA = PROG_HDR + PROG_N * PROG_DIRENT, PROG_SIZE = 3840;
export const PROG_WINDOWS = 4;             // OCT windows on the unit: 0, 12, 24, 36 keys along
export const PROG_NEW_BASE = 48;           // a new progression's trigger note of key 0 (C3, as the SMK-37's lowest key)
// the keybeds (only the drawing of the trigger keyboard and the keys per OCT window differ; the data is the same):
// key 0 is the lowest key; low: its MIDI note (60 = C4); first: the key that plays the window's first progression key
// (the FM-1's chord keys F3..B3 keep their jobs in Prog: the progression starts on C4, the owner 2026-11-19); triggers:
// the progression keys one OCT window shows
export const PROG_KEYBEDS = {
  fm1: { label: "FM-1", keys: 27, low: 53, first: 7, triggers: 20, range: "C4 to G5 (F3 to B3: the chord keys)" },
  smk: { label: "SMK-37", keys: 37, low: 48, first: 0, triggers: 37, range: "C3 to C6" },
};

const PROG_PC = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
// a MIDI note's name as Ripchord writes it (sharps, MIDI 60 = C4)
export const progNoteName = (m) => PROG_PC[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
export const progIsBlack = (m) => [1, 3, 6, 8, 10].includes(((m % 12) + 12) % 12);
export const progEmpty = () => ({ name: "", base: 0, keys: [] });
export const progIsEmpty = (p) => !p || !p.keys || !p.keys.length;
export const progCount = (p) => (progIsEmpty(p) ? 0 : p.keys.filter((c) => c && c.length).length);
// the bytes a progression takes in the object's data: one per key, one per note
export const progBytes = (p) => (progIsEmpty(p) ? 0 : p.keys.reduce((n, c) => n + 1 + (c ? c.length : 0), 0));
// the object's length for a set of 16
export const progUsedBytes = (set) => PROG_DATA + set.reduce((n, p) => n + progBytes(p), 0);
export const progNameOk = (s) => typeof s === "string" && s.length <= PROG_NAME && /^[\x20-\x7E]*$/.test(s);
// any text -> a name the unit takes: accents dropped, other characters out, cut to 12 (trailing spaces off)
export function progCleanName(s) {
  const t = String(s || "").normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/\s+/g, " ").replace(/[^\x20-\x7E]/g, "").trim();
  return t.slice(0, PROG_NAME).trimEnd();
}
const progCopy = (p) => ({ name: p.name, base: p.base, keys: p.keys.map((c) => (c ? c.slice() : [])) });
export const progCopySet = (set) => Array.from({ length: PROG_N }, (_, i) => (set[i] ? progCopy(set[i]) : progEmpty()));
export const progSame = (a, b) => (progIsEmpty(a) && progIsEmpty(b)) ||
  (!progIsEmpty(a) && !progIsEmpty(b) && a.name === b.name && a.base === b.base && a.keys.length === b.keys.length &&
   a.keys.every((c, i) => c.length === b.keys[i].length && c.every((x, k) => x === b.keys[i][k])));

// one progression's faults (slot k for the message) -> "" or the reason
function progFault(p, k = 0) {
  const at = `progression ${k + 1}`;
  if (!p || !Array.isArray(p.keys)) return `${at}: not a progression`;
  if (progIsEmpty(p)) return "";
  if (!progNameOk(p.name)) return `${at}: a name is at most 12 printable ASCII characters`;
  if (!Number.isInteger(p.base) || p.base < 0 || p.base > 127) return `${at}: the trigger note of key 1 is 0..127`;
  if (p.keys.length > PROG_KEYS) return `${at}: at most ${PROG_KEYS} keys`;
  for (const [i, c] of p.keys.entries()) {
    if (!Array.isArray(c) || c.length > PROG_NOTES) return `${at}, key ${i + 1}: a chord holds at most ${PROG_NOTES} notes`;
    if (c.some((n, j) => !Number.isInteger(n) || n < 0 || n > 127 || (j && n <= c[j - 1]))) return `${at}, key ${i + 1}: notes must be 0..127, ascending, distinct`;
  }
  return "";
}

// ---- the object: encode / decode / check (crp_valid)
// the set (16 progressions; missing ones empty) -> the object's bytes. Throws on a bad progression, or when it does
// not fit (progFitText says which to shorten)
export function progEncode(set) {
  const list = progCopySet(set || []);
  for (const [k, p] of list.entries()) { const f = progFault(p, k); if (f) throw new Error(f); }
  const used = progUsedBytes(list);
  if (used > PROG_SIZE) throw new Error(progFitText(list));
  const b = new Uint8Array(used), v = new DataView(b.buffer);
  v.setUint32(0, PROG_MAGIC, true); b[4] = PROG_VERSION; b[5] = PROG_N; v.setUint16(6, used, true);
  let off = PROG_DATA;
  list.forEach((p, k) => {
    if (progIsEmpty(p)) return;                       // (an empty slot: 16 zeros)
    const d = PROG_HDR + k * PROG_DIRENT;
    for (let i = 0; i < p.name.length; i++) b[d + i] = p.name.charCodeAt(i);
    b[d + 12] = p.base; b[d + 13] = p.keys.length; v.setUint16(d + 14, off, true);
    for (const c of p.keys) { b[off++] = c.length; for (const n of c) b[off++] = n; }
  });
  return b;
}
// why the object is not one the unit takes (crp_valid), or "" when it is
export function progInvalidReason(bytes) {
  if (!(bytes instanceof Uint8Array)) return "not bytes";
  const n = bytes.length;
  if (n < PROG_DATA || n > PROG_SIZE) return `length ${n} (272..3840)`;
  const v = new DataView(bytes.buffer, bytes.byteOffset, n), used = v.getUint16(6, true);
  if (v.getUint32(0, true) !== PROG_MAGIC) return "not a progressions object (magic)";
  if (bytes[4] !== PROG_VERSION) return `version ${bytes[4]}`;
  if (bytes[5] !== PROG_N) return `${bytes[5]} progressions (16)`;
  if (used !== n) return `used ${used}, length ${n}`;
  for (let i = 8; i < PROG_HDR; i++) if (bytes[i]) return "reserved header bytes not 0";
  const spans = [];
  for (let k = 0; k < PROG_N; k++) {
    const d = PROG_HDR + k * PROG_DIRENT, base = bytes[d + 12], nkeys = bytes[d + 13], off = v.getUint16(d + 14, true);
    const at = `progression ${k + 1}`;
    if (base > 127) return `${at}: base ${base}`;
    if (nkeys > PROG_KEYS) return `${at}: ${nkeys} keys`;
    if (!nkeys) {
      if (off || bytes.subarray(d, d + PROG_NAME).some((x) => x)) return `${at}: an empty slot with a name or data`;
      continue;
    }
    let z = false;
    for (let i = 0; i < PROG_NAME; i++) {
      const c = bytes[d + i];
      if (!c) z = true;
      else if (z || c < 0x20 || c > 0x7E) return `${at}: its name`;
    }
    if (off < PROG_DATA || off >= used) return `${at}: data offset ${off}`;
    let p = off;
    for (let i = 0; i < nkeys; i++) {
      if (p >= used) return `${at}: key ${i + 1} past the end`;
      const c = bytes[p++];
      if (c > PROG_NOTES || p + c > used) return `${at}: key ${i + 1} holds ${c} notes`;
      for (let j = 0; j < c; j++) if (bytes[p + j] > 127 || (j && bytes[p + j] <= bytes[p + j - 1])) return `${at}: key ${i + 1}: notes not ascending`;
      p += c;
    }
    spans.push([off, p, k]);
  }
  spans.sort((a, b) => a[0] - b[0]);
  for (let i = 1; i < spans.length; i++) if (spans[i][0] < spans[i - 1][1]) return `progressions ${spans[i - 1][2] + 1} and ${spans[i][2] + 1} overlap`;
  return "";
}
export const progValid = (bytes) => !progInvalidReason(bytes);
// the object's bytes -> the set of 16 (throws when the unit would not take it)
export function progDecode(bytes) {
  const why = progInvalidReason(bytes);
  if (why) throw new Error(`Not a valid progressions object: ${why}`);
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  return Array.from({ length: PROG_N }, (_, k) => {
    const d = PROG_HDR + k * PROG_DIRENT, nkeys = bytes[d + 13];
    if (!nkeys) return progEmpty();
    let name = "";
    for (let i = 0; i < PROG_NAME && bytes[d + i]; i++) name += String.fromCharCode(bytes[d + i]);
    const keys = [];
    for (let i = 0, p = v.getUint16(d + 14, true); i < nkeys; i++) { const c = bytes[p++]; keys.push(Array.from(bytes.subarray(p, p + c))); p += c; }
    return { name, base: bytes[d + 12], keys };
  });
}

// ---- capacity: "" when the set fits, else which progressions to shorten
export function progFitText(set) {
  const used = progUsedBytes(set), over = used - PROG_SIZE;
  if (over <= 0) return "";
  const big = set.map((p, k) => ({ k, p, n: progBytes(p) })).filter((x) => x.n).sort((a, b) => b.n - a.n), pick = [];
  for (let s = 0; s < over && big.length; ) { const x = big.shift(); pick.push(x); s += x.n; }
  const who = pick.map((x) => `${x.k + 1} ${x.p.name || "(no name)"} (${x.n} bytes)`);
  return `The progressions take ${used} bytes, ${over} more than the FM-1 holds (${PROG_SIZE}): shorten ${pick.length === 1 ? "progression" : "progressions"} ` +
    `${who.join(", ")} by ${over} bytes${pick.length > 1 ? " in all" : ""} (a key takes 1 byte and 1 per note), or empty one.`;
}

// ---- editing (each -> a new progression; trailing silent keys dropped)
export function progTidy(p) {
  const q = progCopy(p);
  while (q.keys.length && !q.keys[q.keys.length - 1].length) q.keys.pop();
  return q;
}
// the chord of key i set (notes sorted, distinct; at most 8)
export function progSetChord(p, i, notes) {
  if (!Number.isInteger(i) || i < 0 || i >= PROG_KEYS) throw new Error(`A progression has keys 1..${PROG_KEYS}`);
  const c = [...new Set(notes)].sort((a, b) => a - b);
  if (c.length > PROG_NOTES) throw new Error(`A chord holds at most ${PROG_NOTES} notes`);
  if (c.some((n) => !Number.isInteger(n) || n < 0 || n > 127)) throw new Error("Notes are 0..127");
  const q = progCopy(p);
  if (progIsEmpty(p)) q.base = p.base || PROG_NEW_BASE;   // (an empty slot's base is 0: a new one starts at C3)
  while (q.keys.length <= i) q.keys.push([]);
  q.keys[i] = c;
  return progTidy(q);
}
// a note added to / taken from the chord of key i
export function progToggleNote(p, i, note) {
  const c = (p.keys[i] || []).slice(), at = c.indexOf(note);
  if (at >= 0) c.splice(at, 1);
  else if (c.length >= PROG_NOTES) throw new Error(`A chord holds at most ${PROG_NOTES} notes: take one out first`);
  else c.push(note);
  return progSetChord(p, i, c);
}
export const progNew = (name = "NEW") => ({ name: progCleanName(name), base: PROG_NEW_BASE, keys: [] });
export const progRename = (p, name) => ({ ...progCopy(p), name: progCleanName(name) });
// a progression into slot k of a set -> the new set
export function progPlace(set, k, p) {
  if (!Number.isInteger(k) || k < 0 || k >= PROG_N) throw new Error("A progression slot is 1..16");
  const f = progFault(p, k);
  if (f) throw new Error(f);
  const out = progCopySet(set); out[k] = progCopy(p);
  return out;
}

// ---- a chord's name (the mockup's namer): the root found among the chord's pitch classes, the bass as a slash
const PROG_QUAL = { "0,4,7": "", "0,3,7": "m", "0,3,6": "dim", "0,4,8": "+", "0,5,7": "sus4", "0,2,7": "sus2", "0,7": "5",
  "0,4,7,9": "6", "0,3,7,9": "m6", "0,4,7,10": "7", "0,4,7,11": "maj7", "0,3,7,10": "m7", "0,3,6,10": "m7b5", "0,3,6,9": "dim7",
  "0,3,7,11": "mMaj7", "0,5,7,10": "7sus4", "0,2,4,7": "add9", "0,2,3,7": "madd9", "0,2,4,7,10": "9", "0,2,4,7,11": "maj9",
  "0,2,3,7,10": "m9", "0,1,4,7,10": "7b9", "0,3,4,7,10": "7#9", "0,2,4,5,7,10": "11", "0,2,3,5,7,10": "m11",
  "0,3,5,7,10": "m7(11)", "0,4,6,7,11": "maj7#11", "0,2,4,7,9,10": "13", "0,3,4,8,10": "7#5#9", "0,1,4,6,10": "7b5b9",
  "0,4,10": "7", "0,4,11": "maj7", "0,3,10": "m7", "0,2,4,9": "6/9", "0,2,4,7,9": "6/9", "0,4,7,10,14": "9" };
export function progChordName(notes) {
  if (!notes || !notes.length) return "—";
  const pcs = [...new Set(notes.map((m) => m % 12))], bass = Math.min(...notes) % 12;
  const tryset = (set) => {
    for (const r of [bass, ...set.filter((x) => x !== bass).sort((a, b) => a - b)]) {
      if (!set.includes(r)) continue;
      const k = set.map((x) => (x - r + 12) % 12).sort((a, b) => a - b).join(",");
      if (k in PROG_QUAL) return { r, q: PROG_QUAL[k] };
    }
    return null;
  };
  if (pcs.length === 1) return PROG_PC[bass];
  let f = tryset(pcs);
  if (f) return PROG_PC[f.r] + f.q + (f.r !== bass ? "/" + PROG_PC[bass] : "");
  f = tryset(pcs.filter((x) => x !== bass));
  if (f) return PROG_PC[f.r] + f.q + "/" + PROG_PC[bass];
  return notes.slice().sort((a, b) => a - b).map((m) => PROG_PC[m % 12]).join(" ");
}

// ---- Ripchord (.rpc): <ripchord><preset><input note="57"><chord name="E2 E3 G3" notes="40;52;55"/></input>...
// a chord of more than 8 notes cut to 8: notes whose pitch class is doubled elsewhere go first (from the middle
// outwards), then notes from the middle; the lowest and the highest always stay -> {notes, dropped}
export function progTrimChord(notes) {
  const c = [...new Set(notes)].sort((a, b) => a - b), dropped = [];
  while (c.length > PROG_NOTES) {
    const mid = (c.length - 1) / 2;
    const inner = Array.from({ length: c.length - 2 }, (_, j) => j + 1).sort((a, b) => Math.abs(a - mid) - Math.abs(b - mid) || a - b);
    const dbl = inner.find((j) => c.some((x, k) => k !== j && x % 12 === c[j] % 12));
    const j = dbl !== undefined ? dbl : inner[0];
    dropped.push(c[j]); c.splice(j, 1);
  }
  return { notes: c, dropped: dropped.sort((a, b) => a - b) };
}
// the XML's attributes -> {name: value}
const progAttrs = (s) => {
  const out = {}, re = /([A-Za-z_:][\w:.-]*)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;
  for (let m; (m = re.exec(s)); ) out[m[1].toLowerCase()] = m[2] !== undefined ? m[2] : m[3];
  return out;
};
// a .rpc's text (and its file name: the progression's name) -> {prog, notes: [what the import changed], chords, span}.
// Only the first <ripchord> element is read (anything after it is ignored); tags and attributes other than input
// note= and chord notes= are ignored. Throws when there is no input.
export function progParseRpc(text, fileName = "") {
  text = String(text || "").replace(/^\uFEFF/, "");
  const start = text.search(/<ripchord[\s>/]/i);
  if (start < 0) throw new Error(`${fileName || "the file"}: not a Ripchord preset (no <ripchord>)`);
  let end = text.slice(start).search(/<\/ripchord\s*>/i);
  end = end < 0 ? text.length : start + end;
  const body = text.slice(start, end), inputs = new Map(), notes = [];
  let cur = null, bad = 0, twice = 0, cut = 0;
  const re = /<\s*(\/?)\s*([A-Za-z_][\w:.-]*)([^>]*?)(\/?)\s*>/g;
  for (let m; (m = re.exec(body)); ) {
    const close = !!m[1], tag = m[2].toLowerCase(), self = !!m[4] || /\/\s*$/.test(m[3]);
    if (tag === "input" && close) { cur = null; continue; }
    if (tag === "input") {
      const t = Number((progAttrs(m[3]).note || "").trim());
      if (!Number.isInteger(t) || t < 0 || t > 127) { bad++; cur = null; continue; }
      if (inputs.has(t)) twice++;
      inputs.set(t, []); cur = self ? null : t;
      continue;
    }
    if (tag === "chord" && !close && cur !== null) {
      const raw = (progAttrs(m[3]).notes || "").split(/[;,\s]+/).filter((x) => x !== "");
      const ns = raw.map(Number).filter((n) => Number.isInteger(n) && n >= 0 && n <= 127);
      if (ns.length !== raw.length) cut += raw.length - ns.length;
      inputs.set(cur, [...new Set([...inputs.get(cur), ...ns])].sort((a, b) => a - b));
    }
  }
  if (!inputs.size) throw new Error(`${fileName || "the file"}: no <input> in the Ripchord preset`);
  if (bad) notes.push(`${bad} input${bad === 1 ? "" : "s"} with a note outside 0..127 left out`);
  if (twice) notes.push(`${twice} trigger note${twice === 1 ? "" : "s"} given twice: the last kept`);
  if (cut) notes.push(`${cut} chord note${cut === 1 ? "" : "s"} outside 0..127 left out`);
  const trig = [...inputs.keys()].sort((a, b) => a - b), base = trig[0], span = trig[trig.length - 1] - base + 1;
  if (span > PROG_KEYS) notes.push(`${span} keys: the first ${PROG_KEYS} kept`);
  const kept = trig.filter((t) => t - base < PROG_KEYS), keys = Array.from({ length: kept[kept.length - 1] - base + 1 }, () => []);
  for (const t of kept) {
    let c = inputs.get(t);
    if (c.length > PROG_NOTES) {
      const r = progTrimChord(c);
      notes.push(`key ${t - base + 1}: ${c.length} notes cut to ${PROG_NOTES} (${r.dropped.map(progNoteName).join(" ")} left out)`);
      c = r.notes;
    }
    keys[t - base] = c;
  }
  const name = progCleanName(String(fileName).split(/[\\/]/).pop().replace(/\.rpc$/i, "")) || "Ripchord";
  const prog = { name, base, keys };
  return { prog, notes, chords: progCount(prog), span };
}
// the import's notes in a few words for the library row: "18 chords · ready", "24 chords · 2 cut to 8 notes"
export function progNotesSummary(item) {
  const n = progCount(item.prog), notes = item.notes || [];
  const head = `${n} chord${n === 1 ? "" : "s"}`;
  if (!notes.length) return `${head} · ready`;
  const trimmed = notes.filter((x) => /notes cut to/.test(x)).length, rest = notes.filter((x) => !/notes cut to/.test(x));
  return [head, ...rest, ...(trimmed ? [`${trimmed} cut to ${PROG_NOTES} notes`] : [])].join(" · ");
}
const progXml = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&apos;" })[c]);
// a progression -> a .rpc text as Ripchord writes it: one input per key holding a chord (trigger base + i, the chord's
// note names in name=, the notes in notes=); a silent first or last key is written with an empty chord, so that an
// import gives the same keys back (silent keys between chords are left out: they import as gaps)
export function progToRpc(p) {
  if (progIsEmpty(p)) throw new Error("An empty progression has nothing to export");
  if (p.base + p.keys.length - 1 > 127) throw new Error("The keys would go past MIDI note 127: lower the trigger note");
  const lines = ['<?xml version="1.0" encoding="UTF-8"?>', "", "<ripchord>", "  <preset>"];
  p.keys.forEach((c, i) => {
    if (!c.length && i && i !== p.keys.length - 1) return;
    lines.push(`    <input note="${p.base + i}">`, `      <chord name="${progXml(c.map(progNoteName).join(" "))}" notes="${c.join(";")}"/>`, "    </input>");
  });
  lines.push("  </preset>", "</ripchord>", "");
  return lines.join("\n");
}
export const progRpcFileName = (p) => `${(p.name || "progression").replace(/[\\/:*?"<>|]/g, "_").trim() || "progression"}.rpc`;

// ---- the library's items: {type: "prog", name, prog, notes, source}
// a file's text -> items: a .rpc gives one; a ChoralRoot backup (felucca-backup JSON) holding id 24 gives its progressions
export function progItemsFromFile(name, text) {
  if (/\.rpc$/i.test(name) || /<ripchord[\s>/]/i.test(String(text).slice(0, 4096))) {
    const r = progParseRpc(text, name);
    return [{ type: "prog", name: r.prog.name, prog: r.prog, notes: r.notes, source: name }];
  }
  let j = null;
  try { j = JSON.parse(text); } catch (_) { /* below */ }
  if (j && j.format === "felucca-backup") {
    const o = readBackup(j).objects.find((x) => x.id === PROG_ID);
    if (!o || !o.size) throw new Error(`${name}: this backup holds no progressions`);
    return progDecode(o.bytes).map((p, k) => ({ p, k })).filter(({ p }) => !progIsEmpty(p))
      .map(({ p, k }) => ({ type: "prog", name: p.name, prog: p, notes: [], source: `${name} slot ${k + 1}` }));
  }
  throw new Error(`${name}: not a Ripchord preset (.rpc) or a backup`);
}

// ---- the factory set as the simulator lists it (?sim=1, the tests): a copy of firmware/src/cr_prog.c crp_factory's
// table (2026-11-18: six progressions in slots 1..6, 8 keys of 5 notes each, base 48; slots 7..16 empty). The unit
// lists its own (a PUT of 0 bytes brings it back), so the page never needs this copy
const PROG_FACTORY = [
  ["Pop Four", [[36,52,55,60,64],[47,55,59,62,67],[45,52,57,60,64],[41,53,57,60,65],[40,55,60,64,67],[43,55,59,62,67],[41,53,57,60,64],[43,55,60,62,67]]],
  ["Jazz ii-V-I", [[38,53,57,60,64],[43,53,59,64,69],[36,52,55,59,62],[45,55,58,61,64],[38,53,60,64,69],[43,53,56,59,62],[36,52,55,59,64],[36,52,57,62,67]]],
  ["Neo Soul", [[39,55,58,62,65],[41,56,60,63,67],[43,53,58,62,67],[44,55,60,63,68],[46,56,60,62,67],[36,55,58,63,65],[44,60,63,67,70],[46,56,58,63,65]]],
  ["Gospel Walk", [[36,52,55,60,64],[40,55,58,60,64],[41,53,57,60,65],[42,57,60,63,66],[43,55,60,64,67],[45,55,61,64,69],[38,53,57,60,65],[43,53,59,62,67]]],
  ["Minor Epic", [[45,52,57,60,64],[41,53,57,60,65],[36,52,55,60,64],[43,55,59,62,67],[38,53,57,62,65],[40,56,59,62,64],[40,57,60,64,69],[40,56,59,64,68]]],
  ["Dorian Days", [[38,53,57,60,65],[40,55,59,62,67],[41,53,57,60,64],[43,53,59,62,67],[45,55,60,64,67],[36,52,55,59,64],[38,53,57,60,64],[43,53,59,64,69]]],
];
export const progSimFactory = () => progEncode(PROG_FACTORY.map(([name, keys]) => ({ name, base: PROG_NEW_BASE, keys })));
