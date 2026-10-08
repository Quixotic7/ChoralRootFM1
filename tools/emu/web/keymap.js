// SPDX-License-Identifier: GPL-3.0-only
// The computer-key map of the browser emulator: tools/emu/keymap.c's, by KeyboardEvent.code (physical US-layout
// positions). kind: key (firmware note key idx: MIDI note - 53), btn (EMU_B_* label), both (OCT- + OCT+ = panic),
// sel (select the knob EMU_E_* idx for Up / Down), turn (the selected knob, idx detents). Shared by emu.js and
// test_web_emu.mjs (which also reads tools/emu/scripts with it: cap = the script's key name).
globalThis.FM1 = {
  KEYMAP: [
    ["F1", "key", 1, "F1"], ["F2", "key", 3, "F2"], ["F3", "key", 5, "F3"], ["F4", "key", 8, "F4"],
    ["Digit2", "key", 0, "2"], ["Digit3", "key", 2, "3"], ["Digit4", "key", 4, "4"], ["Digit5", "key", 7, "5"],
    ["Tab", "key", 6, "TAB"],
    ["KeyA", "key", 7, "A"], ["KeyS", "key", 9, "S"], ["KeyD", "key", 11, "D"], ["KeyF", "key", 12, "F"],
    ["KeyG", "key", 14, "G"], ["KeyH", "key", 16, "H"], ["KeyJ", "key", 18, "J"], ["KeyK", "key", 19, "K"],
    ["KeyL", "key", 21, "L"], ["Semicolon", "key", 23, ";"], ["Quote", "key", 24, "'"], ["BracketRight", "key", 26, "]"],
    ["KeyW", "key", 8, "W"], ["KeyE", "key", 10, "E"], ["KeyT", "key", 13, "T"], ["KeyY", "key", 15, "Y"],
    ["KeyU", "key", 17, "U"], ["KeyO", "key", 20, "O"], ["KeyP", "key", 22, "P"], ["BracketLeft", "key", 25, "["],
    ["KeyZ", "btn", 12, "Z"], ["KeyC", "btn", 13, "C"], ["ArrowLeft", "btn", 12, "LEFT"], ["ArrowRight", "btn", 13, "RIGHT"],
    ["Escape", "both", 0, "ESC"],
    ["F5", "btn", 0, "F5"], ["F6", "btn", 1, "F6"], ["F7", "btn", 2, "F7"], ["F8", "btn", 3, "F8"],
    ["F9", "btn", 4, "F9"], ["F10", "btn", 5, "F10"], ["Digit7", "btn", 6, "7"], ["Digit8", "btn", 7, "8"],
    ["Digit9", "btn", 8, "9"], ["Digit0", "btn", 9, "0"], ["Minus", "btn", 10, "-"], ["Equal", "btn", 11, "="],
    ["KeyQ", "sel", 7, "Q"], ["KeyR", "sel", 0, "R"], ["KeyM", "sel", 2, "M"], ["Comma", "sel", 1, ","],
    ["KeyX", "sel", 3, "X"], ["KeyV", "sel", 4, "V"], ["KeyB", "sel", 5, "B"], ["KeyN", "sel", 6, "N"],
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
