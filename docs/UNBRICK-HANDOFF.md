# Handoff: recover a dark M-VAVE FM-1 from a Windows PC (2026-10-07)

Paste everything below the line into a Claude session on the Windows PC.

---

Help me recover my M-VAVE FM-1 (a small synth; JieLi WL82 / AC791N chip) that stopped responding after an
interrupted firmware update. I am the owner; the firmware is my own GPL-3.0 project, ChoralRoot FM-1.

**What happened.** On the Mac, `tools/fm1_install.py build/choralroot.fwsc --yes` (the ChoralRoot installer, which
drives the Felucca update loader over USB-MIDI SysEx) stopped with "the loader was disconnected after 274 requests"
(of about 1167) while writing the application area. Since then the FM-1 shows a black screen and does not enumerate
on USB at all on the Mac: no MIDI port (neither "Felucca" / "ChoralRoot FM-1" nor the loader's "ota-FM-1"), no USB
device with vendor id 0x1209, and no "WL82 UBOOT1.00" disk (vendor 0x4C4A, product 0x8057). Power cycling and
holding OCT- and OCT+ while switching on changed nothing. The update loader lives in its own flash area
(0xE0000..0xE4FFF) with a resume record at 0xE4F00 and was meant to come back as "ota-FM-1" after a power cycle; it
did not. The Mac cannot run the recovery tool (no SCSI back end), which is why we are on Windows.

**Repos** (clone both from GitHub; run from an administrator PowerShell):

- https://github.com/Quixotic7/MvaveFM1Unbricker : `fm1_unbrick.py` (MIT) recovers a soft-bricked FM-1 that shows
  the `WL82 UBOOT1.00` ROM-boot disk, using jl-uboot-tool. **Read its README.md ("Troubleshooting: The FM-1 does not
  show up as WL82 UBOOT1.00 at all") and UNBRICK-GUIDE.md first** and follow them; they are the authority.
- https://github.com/Quixotic7/ChoralRootFM1 : the firmware. `docs/INSTALL-COMPAT.md` has the boot-guard notes
  and the "If an FM-1 is dark" checklist; `tools/fm1_install.py` is the normal installer (needs `pip install mido
  python-rtmidi`); the current package `build/choralroot.fwsc` is not in git: I will download it from my Claude
  conversation (the Mac session sent it as a file), or take `choralroot-0.13.fwsc` from the GitHub Releases page.
  The official M-VAVE V15 `FM-1.fwsc` (from https://www.m-vave.com/download) is the safest thing to write first.

**Do, in order, and tell me what you see at each step** (FM-1 plugged straight into the PC, no hub, a data cable):

1. Device discovery, all three ways: `Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_4C4A*' -or
   $_.InstanceId -like '*VID_1209*' } | Format-Table Status, Class, FriendlyName, InstanceId` and
   `Get-CimInstance Win32_DiskDrive | Format-Table Index, Model, InterfaceType`, plus a MIDI port list
   (`python -c "import mido; print(mido.get_input_names())"` after `pip install mido python-rtmidi`). A device with an
   error status still counts: report it. Repeat after each of the following.
2. If a MIDI port **ota-FM-1** (or any FM-1 port) exists: the loader is alive. Run
   `python tools\fm1_install.py path\to\choralroot.fwsc --yes` from the ChoralRootFM1 clone; it resumes the write.
   Done when it prints "done: the FM-1 runs FM-1_920".
3. If **WL82 UBOOT1.00** exists (cancel any "format disk" prompt): `py fm1_unbrick.py setup`, then
   `py fm1_unbrick.py backup backups\` (keep it), then `py fm1_unbrick.py restore FM-1.fwsc` (official V15), or
   `py fm1_unbrick.py restore choralroot.fwsc` for ChoralRoot directly. Follow the tool's exit codes: 3 means do not
   power off, run restore again.
4. If neither: leave it switched on two full minutes (the boot guard enters ROM boot after two crashing boots), then
   three quick restarts (on, 10 s, off; on, 10 s, off; on), checking after each. If vendor 4C4A never appears, the
   remaining route is the FM-1 Transporter board (https://github.com/kurogedelic/FM-1-transporter), and say so.

**Facts that matter.** The user data on the FM-1 (user sounds U01 SHIMMER, U02 TINE EP, settings) is already backed
up on the Mac, so erasing data areas is acceptable if the guide calls for it. The boot head 0x0000..0x3FFF must never be
written. Never run anything that formats the WL82 disk. Report what each command printed, not a summary.
