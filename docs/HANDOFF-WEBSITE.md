# Handoff: the ChoralRoot FM-1 website and web installer

Paste this into a fresh session (or hand it to an agent) to build the public landing page and the browser
installer for ChoralRoot, in the shape of https://charlesvestal.github.io/fm1-omnichord/ (OMNI, another
Felucca-based FM-1 firmware: a one-page site with screenshots, "Install" / "Download .fwsc" / "Source"
buttons, a playing guide, credits), using what the repo already has.

---

## The prompt

You are working in `/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/ChoralRootFM1`, the repo of
**ChoralRoot FM-1**: a Telepathic Orchid-style chord instrument firmware for the M-VAVE FM-1, a GPL-3.0 fork of
Felucca (Leo Kuroshita, Hügelton Instruments). The firmware is finished enough for a first public beta
(`PLAN.md` §8, M7). Your job: the landing page and the web installer, deployed on GitHub Pages from this repo.

Working rules (the user's):
- Never launch a browser window, the emulator, or any GUI in the foreground. Verify headlessly (python, node,
  the shell suites). If a browser is needed to test the installer, use the app's built-in browser pane or ask.
- Coding is done by Opus subagents (Agent tool, model "opus", run_in_background); you design, brief, verify.
- Do not `git commit` or `git push` unless the user says so. **Publishing the Pages site is public: ask before
  enabling Pages, pushing a workflow, or creating a release.**
- Read first: `README.md`, `BUILDING.md` (the "Install" section), `PLAN.md` (§1, §3, §5, §8), `docs/EDITOR.md`,
  `docs/VA.md`, `LICENSING.md`, `web/make_site.py`, `web/index_pkg.html`, `web/fm1pkg.js`, `web/fm1ota.js`,
  `tests/install_test.py`, `tools/fm1_install.py`, and for the shape of the page
  https://charlesvestal.github.io/fm1-omnichord/ (and its repo https://github.com/charlesvestal/fm1-omnichord:
  `web/` folder, release asset for the `.fwsc`, Pages site with `/install/` and `/emu/`).

What exists already:
- `web/make_site.py build/choralroot.fwsc VERSION OUT_DIR` builds a static site: `webapp/installer/index.html`
  (Felucca's Web MIDI installer, self-contained: `fm1pkg.js` + `fm1ota.js` + `fm1backup.js` inlined, the
  package path and identity `FM-1_920` in `/*META*/`), `webapp/editor/` (Felucca's FM6 web editor, optional),
  `firmware/choralroot-VER.fwsc` with `LICENSE`, `LICENSING.md`, `LICENSES/`, and an `index.html` that redirects
  to the installer. It refuses a package without Felucca's own loader marker (never ships vendor files).
- `./build.sh --release X.Y` makes `build/choralroot-X.Y.fwsc` (identity stays FM-1_920, version string
  "ChoralRoot X.Y") and `build/release-X.Y/` with the package, the app, `SHA256SUMS` and the licences.
- `tests/install_test.py` and `tests/run_tests.sh` check the package against `fm1pkg.js` (node).
- `tools/fm1_install.py` is the command-line installer (`--info` reads the device's identity).
- Screens: `design/choralroot-fm1-screens.png` (24 states), `design/choralroot-fm1-sound-editor-screens.png`
  (16), the device renders `build/cr_screens/*.png` and the emulator shots `build/emu/test/*.ppm` after
  `sh tests/run_cr_draw.sh` and `sh tools/emu/test_cr.sh` (convert with Pillow). The mod look: black, white,
  red, blue, yellow, orange, green; Inter Tight; "one big thing per screen" (see `PLAN.md` §5).

Deliverables:
1. **`web/site/`**: the landing page source (one `index.html` + `site.css`, no build step beyond make_site.py;
   vanilla HTML/CSS/JS). Sections, in order: title + one line ("a chord instrument for the M-VAVE FM-1, after
   the Telepathic Orchid"), three screenshots (idle stripes, a chord, the sound editor), the four buttons
   **Install** (→ `webapp/installer/`, with the note "From Chrome or Edge, with the FM-1 connected by USB.
   Nothing to install on the computer."), **Download choralroot-X.Y.fwsc**, **Source** (the GitHub repo),
   **Manual** (→ `manual.html` or the README section), then **Playing** (the keybed: chord block on
   F#3 G#3 A#3 C#4 / F3 G3 A3 C4, B3 = LOCK, roots D4–G5; the buttons by their ChoralRoot roles; the knobs;
   from `PLAN.md` §3 and §4), **The sound editor** (from `docs/EDITOR.md`: groups, SELECT, SHIFT, mapping),
   **Sounds** (the bank, the VA in two sentences), **MIDI**, **Recovery** ("the web installer can put the stock
   firmware back" only if that is true for Felucca's installer; else link FM-1-transporter as `BUILDING.md`
   does), **Credits and licence**: GPL-3.0; built on Felucca by Leo Kuroshita / Hügelton Instruments; the
   chord logic after the Telepathic Instruments Orchid; CC0 samples (Versilian, `assets/samples-cc0/
   ATTRIBUTION.txt`); "not affiliated with Telepathic Instruments, M-VAVE or Cuvave". The page in the mod look
   (big type, flat colour blocks, the stripes as a motif), responsive, no external assets except Google Fonts
   (or self-hosted Inter Tight), dark.
2. **`web/make_site.py`** extended: copies `web/site/` to the root of the output (the landing page becomes
   `index.html`; the old redirect goes), fills the version and package name into it, keeps `webapp/installer/`
   and `firmware/`. Keep its refusal rules.
3. **The installer page** (`web/index_pkg.html`): restyle its header/footer to ChoralRoot (name, identity
   FM-1_920, a link back to the landing page); the logic (Web MIDI handshake, the OTA write, the "put the stock
   firmware back" path if Felucca's page has one) unchanged. Test headlessly: `node web/test_web.mjs` (or
   whatever `tests/run_tests.sh` runs for the web), `python3 tests/install_test.py`, `make_site.py` on a
   release package, and a local `python3 -m http.server` only for a manual check by the user (Web MIDI needs
   Chrome/Edge and the device).
4. **GitHub Actions**: `.github/workflows/pages.yml`: on a tag `v*` (or manual dispatch with a version), build
   the device package in the JieLi toolchain (Docker `debian:bookworm-slim` + `tools/get_toolchain.sh` + the
   AC79 SDK clone as `BUILDING.md` describes; if the toolchain download cannot run in CI, fall back to a
   workflow that takes the `.fwsc` from the release assets of that tag), run `make_site.py`, deploy to Pages
   (`actions/deploy-pages`). A `release.yml` that attaches `choralroot-X.Y.fwsc`, the app `.bin`, `SHA256SUMS`
   and the licences to the GitHub release for the tag. Do not enable Pages or push: produce the files and
   the exact steps for the user.
5. **README.md**: an "Install" section at the top pointing at the Pages URL
   (`https://<user>.github.io/ChoralRootFM1/`), the `.fwsc` download, the command-line installer, the recovery
   note; the licence paragraph.
6. Optional, after the above: **"Try it in the browser"**: an Emscripten build of `tools/emu/` (SDL2 is
   supported by Emscripten; CoreMIDI → Web MIDI; the file-backed flash → IndexedDB) at `/emu/`. Scope it,
   estimate, and ask before starting: it is a separate project.

Acceptance: `make_site.py` output opens as a static site with the landing page at `/`, the installer at
`/webapp/installer/` showing "ChoralRoot X.Y · FM-1_920", the package downloadable, all internal links
resolving (check with a python link walker), `tests/install_test.py` passing on the package, the workflow YAML
validated (`actionlint` if available, else `python -c yaml`), and screenshots of the landing page at phone
and desktop widths taken headlessly (Playwright/Chromium headless, or the app's browser pane) for the user
to review before anything is published.

Report: the files, the deploy steps for the user (enable Pages for the `gh-pages`/Actions source, tag
`v0.1`, what the workflow does), the screenshots, and what was left out.
