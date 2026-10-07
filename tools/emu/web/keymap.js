// SPDX-License-Identifier: GPL-3.0-only
// The computer-key map of the browser emulator: tools/emu/keymap.c's, by KeyboardEvent.code (physical US-layout
// positions). kind: key (firmware note key idx: MIDI note - 53), btn (EMU_B_* label), both (OCT- + OCT+ = panic),
// sel (select the knob EMU_E_* idx for Up / Down), turn (the selected knob, idx detents). Shared by emu.js and
// test_web_emu.mjs (which also reads tools/emu/scripts with it: cap = the script's key name).
globalThis.FM1 = {
  KEYMAP: [
    ["KeyA", "key", 9, "A"], ["KeyS", "key", 11, "S"], ["KeyD", "key", 12, "D"], ["KeyF", "key", 14, "F"],
    ["KeyG", "key", 16, "G"], ["KeyH", "key", 18, "H"], ["KeyJ", "key", 19, "J"], ["KeyK", "key", 21, "K"],
    ["KeyL", "key", 23, "L"], ["Semicolon", "key", 24, ";"], ["Quote", "key", 26, "'"],
    ["KeyW", "key", 10, "W"], ["KeyE", "key", 13, "E"], ["KeyT", "key", 15, "T"], ["KeyY", "key", 17, "Y"],
    ["KeyI", "key", 20, "I"], ["KeyO", "key", 22, "O"], ["BracketLeft", "key", 25, "["],
    ["F1", "key", 1, "F1"], ["F2", "key", 3, "F2"], ["F3", "key", 5, "F3"], ["F4", "key", 8, "F4"],
    ["Digit2", "key", 0, "2"], ["Digit3", "key", 2, "3"], ["Digit4", "key", 4, "4"], ["Digit5", "key", 7, "5"],
    ["Digit1", "key", 6, "1"],
    ["KeyZ", "btn", 0, "Z"], ["KeyX", "btn", 1, "X"], ["KeyC", "btn", 2, "C"], ["KeyV", "btn", 3, "V"],
    ["KeyB", "btn", 4, "B"], ["KeyN", "btn", 5, "N"], ["Comma", "btn", 6, ","], ["Period", "btn", 7, "."],
    ["Slash", "btn", 8, "/"], ["ShiftRight", "btn", 9, "RSHIFT"], ["ShiftLeft", "btn", 9, "SHIFT"],
    ["Enter", "btn", 10, "RETURN"], ["Backspace", "btn", 11, "BKSP"],
    ["ArrowLeft", "btn", 12, "LEFT"], ["ArrowRight", "btn", 13, "RIGHT"], ["Escape", "both", 0, "ESC"],
    ["KeyQ", "sel", 7, "Q"], ["KeyR", "sel", 0, "R"], ["KeyU", "sel", 2, "U"], ["KeyP", "sel", 1, "P"],
    ["Digit6", "sel", 3, "6"], ["Digit7", "sel", 4, "7"], ["Digit8", "sel", 5, "8"], ["Digit9", "sel", 6, "9"],
    ["ArrowUp", "turn", 1, "UP"], ["ArrowDown", "turn", -1, "DOWN"],
  ].map(([code, kind, idx, cap]) => ({ code, kind, idx, cap })),
  // EMU_B_* labels and their ChoralRoot roles (the sticker)
  BTN: ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"],
  BTN_ROLE: ["FX", "KEY", "BASS", "LATCH", "EDIT", "OPT", "HOME", "SAVE", "PERF", "METRO", "LOOP", "REC", "OCT−", "OCT+"],
  // EMU_E_* knobs and their roles
  ENC: ["SELECT", "ALGORITHM", "PRESETS", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "MASTER"],
  ENC_ROLE: ["BPM", "BASS SND", "SOUND", "VOICING", "BASS VOICE", "PERFORM", "FX", "VOLUME"],
  // the chord block's sticker: firmware key -> label
  CHORD: { 0: "6", 1: "DIM", 2: "m7", 3: "MIN", 4: "M7", 5: "MAJ", 6: "LOCK", 7: "9", 8: "SUS" },
  noteName(k) {
    const N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], m = 53 + k;
    return N[m % 12] + (Math.floor(m / 12) - 1);
  },
};
