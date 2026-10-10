#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure ChoralRoot In (the FM-1's USB recording) on a Mac: record it with ffmpeg while MIDI chords play the FM-1,
then report how much audio the computer actually got (docs/USB-AUDIO.md, "Measuring the recording on a Mac").

usage: tools/uac_check.py OUT.wav [SECONDS] [--nonotes] [--debug] [--device NAME]

  SECONDS     the capture's wall time (default 14)
  --nonotes   no chords: the stream's behaviour in silence
  --debug     tools/fm1_install.py --debug before and after: the device's counters (ua_opens, tx_packets, ...)
  --device    the capture device's name (default "ChoralRoot In")

Needs ffmpeg (avfoundation) and mido + python-rtmidi. The chords are C major, 0.6 s on and 0.4 s off, to the first
MIDI output whose name contains "ChoralRoot". A healthy run gives a WAV about as long as the wall time (the drain
ratio ~0.93 or more: ffmpeg's own start costs ~1 s) and no silent gap of 0.5 s or more while notes are sent.
Nothing here writes the FM-1 (no install, no settings)."""
import argparse
import math
import os
import struct
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
DEBUG_KEYS = ("ua_opens", "ua_open_ms", "ua_tx_packets", "ua_pk5", "ua_overruns", "ua_underruns", "ua_first_take",
              "ua_open_close", "ua_holds", "ua_missed", "ua_arm_gap", "usb_stalls", "last whole open")


def device_debug(label):
    r = subprocess.run([sys.executable, os.path.join(HERE, "fm1_install.py"), "--debug"], capture_output=True,
                       text=True, timeout=60)
    print(f"--- --debug {label}")
    for line in (r.stdout + r.stderr).splitlines():
        if any(k in line for k in DEBUG_KEYS) or line.startswith("error"):
            print("  " + line)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("seconds", nargs="?", type=float, default=14.0)
    ap.add_argument("--nonotes", action="store_true")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--device", default="ChoralRoot In")
    a = ap.parse_args()
    import mido
    secs = a.seconds
    if a.debug:
        device_debug("before")
    port_name = next((n for n in mido.get_output_names() if "ChoralRoot" in n), None)
    if port_name is None and not a.nonotes:
        sys.exit("no ChoralRoot MIDI output (use --nonotes to record without notes)")
    rec = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "avfoundation", "-i", f":{a.device}",
                            "-t", str(secs), "-ac", "2", "-ar", "44100", a.out])
    time.sleep(2.0)                                      # the stream settles before the notes
    sent = 0
    if not a.nonotes:
        with mido.open_output(port_name) as port:
            t0 = time.time()
            while time.time() - t0 < secs - 3.0:
                for n in (60, 64, 67):
                    port.send(mido.Message("note_on", note=n, velocity=100, channel=0))
                sent += 1
                time.sleep(0.6)
                for n in (60, 64, 67):
                    port.send(mido.Message("note_off", note=n, velocity=0, channel=0))
                time.sleep(0.4)
    rec.wait()

    w = wave.open(a.out, "rb")
    nch, sw, fr, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    data = w.readframes(nf)
    win = fr // 20                                       # 50 ms windows
    fmt = "<%dh" % (nch * win)
    rms = []
    for i in range(0, nf - win, win):
        s = struct.unpack(fmt, data[i * nch * sw:(i + win) * nch * sw])
        rms.append(math.sqrt(sum(x * x for x in s) / len(s)))
    loud = [r > 200 for r in rms]
    first = next((i for i, v in enumerate(loud) if v), None)
    print(f"{a.out}: {nf / fr:.2f} s, {nch} ch, {fr} Hz, loud 50 ms windows {sum(loud)} of {len(rms)}, first loud at "
          f"{first * 0.05 if first is not None else None} s")
    gaps, run, start = [], 0, None                       # silent runs >= 0.5 s while notes were being sent
    for i in range(first or 0, min(len(loud), int((secs - 3.5) * 20))):
        if not loud[i]:
            run += 1
            start = i if start is None else start
        else:
            if run >= 10:
                gaps.append((round(start * 0.05, 2), round(run * 0.05, 2)))
            run, start = 0, None
    if run >= 10:
        gaps.append((round(start * 0.05, 2), round(run * 0.05, 2)))
    print("silent gaps >= 0.5 s while notes were sent (start s, length s):", gaps if not a.nonotes else "(no notes)")
    print(f"WAV {nf / fr:.2f} s / wall {secs:.1f} s = drain ratio {nf / fr / secs:.3f}   (chords sent: {sent})")
    if a.debug:
        device_debug("after")


if __name__ == "__main__":
    main()
