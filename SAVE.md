# SAVE - V Media AI AutoFrame (state as of 2026-11-13, v1.1.2, Windows + macOS)

Read this first when you come back to the project.

## Current state

| What | Where / status |
|---|---|
| Project folder | `C:\Users\vinit\Documents\Claude\Projects\VMedia-AI-AutoFrame` (this folder) |
| GitHub | https://github.com/vinitdeep/obsautoframe (public) |
| `main` branch | 28dd452 = v1.1.1 (Windows only) |
| `macos-port` branch | **current work**: v1.1.2 + macOS port + docs. Everything is committed and pushed here. NOT merged into `main` yet. |
| Windows installer | `release/VMedia-AI-AutoFrame-Setup-x64.exe` (+ `-1.1.2-x64.zip`), built locally, all tests pass |
| Mac installer | `release/VMedia-AI-AutoFrame-1.1.2-macOS.dmg`, built by GitHub Actions (run 43393629951) |
| GitHub Release | NOT created (the user approves `gh release create` themselves; it was denied once) |
| User's own OBS | v1.1.2 installed in `C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe`, tested with an Iriun webcam |

Checksums: `release/SHA256SUMS.txt`.

## Change something -> rebuild -> test (Windows, this PC)

```powershell
cd C:\Users\vinit\Documents\Claude\Projects\VMedia-AI-AutoFrame
git checkout macos-port            # the current branch
# edit src/... (plugin), data/locale/en-US.ini (UI text), data/effects/autoframe.effect (shader)
scripts\build.ps1                  # build + 53 unit tests + real-model detector test (~2 min)
scripts\build.ps1 -Integration     # + every libobs phase + real OBS GUI smoke test (~6 min)
scripts\package.ps1                # -> release\ Setup .exe + ZIP + SHA256SUMS.txt
```

Version number: `CMakeLists.txt` `project(... VERSION x.y.z)` (also used by the installer, the Mac
Info.plist and the DMG name). Add a section to `RELEASE_NOTES.md` for every version.

From nothing (new PC): `scripts\setup.ps1` then `tests\fetch-test-media.ps1` first (downloads OBS
headers/libs, ONNX Runtime, DirectML, models, test clips into git-ignored folders). Tools: VS 2022
Build Tools (C++), CMake, Git, Inno Setup 6, ffmpeg - see `docs/BUILD.md`.

## Make a new Mac .dmg (no Mac needed)

```powershell
git add -A; git commit -m "..."; git push                 # on macos-port
gh workflow run build.yml --ref macos-port                # GitHub's Mac builds + tests it (~10 min)
gh run list --branch macos-port --limit 1                 # get the run id
gh run download <id> --name VMedia-AI-AutoFrame-macOS --dir release
```

The Mac job (`.github/workflows/build.yml`, `macos-latest`; the old `macos-14` image is retired)
runs `scripts/setup-macos.sh` + `scripts/package-macos.sh`: universal arm64 + x86_64 build, unit
tests (also the Intel slice via Rosetta), detector test, `tools/macos-load-check.c` (boots the REAL
OBS 33.0.1 libobs from OBS.app, Apple + Intel, loads the plugin, checks filter/defaults/UI/0 leaks),
then `.pkg` (installs to `/Library/Application Support/obs-studio/plugins`) inside a `.dmg`.
On a real Mac the same two scripts work locally.

## Where things are

* `src/` plugin + core (pure C++ framing/tracking in `settings/zoom-controller/framing-controller/person-tracker/yolox`; OBS glue in `autoframe-filter/obs-settings/obs-renderer/plugin-main`; AI in `person-detector/detection-worker`; `sha256.*` portable hash)
* `data/` effect shader + locale (copied into the plugin's data / Mac `Contents/Resources`)
* `tests/` unit tests, detector test, `fetch-test-media.ps1`, `gui-smoke-test.ps1`, `check-overlay-leak.py`
* `tools/` `obs-headless-test.cpp` (Windows libobs harness), `autoframe-sim.cpp` (offline video harness), `macos-load-check.c`
* `scripts/` Windows: `setup.ps1 build.ps1 package.ps1 abi-check.ps1`; Mac: `setup-macos.sh package-macos.sh`
* `installer/installer.iss` (Inno Setup), `cmake/macos/Info.plist.in`
* `docs/` USER-GUIDE, INSTALLATION (Windows + Mac), TROUBLESHOOTING, TESTING, ARCHITECTURE, BUILD; `docs/guide/` illustrated quick guide (`build-guide.py` regenerates the PNG)
* git-ignored, regenerated: `third_party/ build/ build-macos/ release/ tests/media/ tests/out/`

## Key decisions (do not undo without reason)

* Plugin compiled against **OBS 30.0.0 headers** (libobs rejects plugins built with newer headers) -> runs on OBS 30-33. `scripts\abi-check.ps1` proves ABI identity. Windows integration host uses 33.0.1 headers (`include-host`). Mac links the real OBS 33.0.1 libobs.framework (arm64 + x86_64 lipo'd).
* ONNX Runtime is **loaded at run time by full path** from the plugin's `runtime` folder (never linked). Windows: 1.24.4 DirectML build (CPU + any DX12 GPU). Mac: 1.23.2 universal2 (newest with arm64+x86_64; needs **macOS 13.4+**), GPU = Core ML. ORT objects only created after the runtime loaded (destroying an ORT wrapper without a loaded API crashes).
* YOLOX (Apache-2.0), not Ultralytics (AGPL). Models SHA-256 checked at load.
* **Tracking graphics only in preview windows** (`gs_get_render_target()==nullptr`), never in program/stream/recording - user requirement. Do not add an option to draw into the program output (OBS queues frames before an output starts, so it always leaks).
* Non-framing setting changes must not re-centre the shot (`sameFraming`).
* Webcam close-up: when the person is cut off at the bottom, zoom in to centre them (single mode only).
* Vertical 9:16: output size Auto = 1080x1920; group fit = lead + whole neighbours; detail guard maxEnlarge 2.0. For horizontal + vertical from one camera, put AutoFrame on two scenes, never on the camera itself.
* Overlay drawing: `gs_render_start(false)` (true corrupts libobs' immediate VB -> crash). libobs output texture is BGRA. Stage area tests the person's FEET. OBS 33 harness teardown: `obs_source_remove(scene)` before release.
* Windows test etiquette: never touch the user's OBS profile (GUI test uses an isolated portable copy); never kill all obs64 processes; screenshots only of the OBS window (PrintWindow).

## Verified

Windows: 53 unit test cases (1674 checks), real-model detector test, libobs integration phases (baseline, fidelity, overlay, preacher, stress, failure, multi, resize, gpu, record, vertical, scenefilter, noruntime, scenectl) with 0 render lag + 0 leaks, real OBS GUI smoke test, user's own OBS with Iriun webcam (1080x1920, zoom 2.00x centred, recordings clean).
Mac (GitHub runner): unit tests arm64 + Intel, detector test, load test in real OBS 33.0.1 Apple + Intel.

## Open / next

1. Try the Mac DMG on a real Mac with OBS + a camera (never GUI-tested on a Mac).
2. Merge `macos-port` into `main` and publish a GitHub Release with the .exe, .zip, .dmg (ask the user first).
3. Apple Developer ID signing + notarization (needs the user's paid Apple account): set `MACOS_SIGN_IDENTITY` / `MACOS_INSTALLER_IDENTITY`, then `xcrun notarytool`. Windows code signing for the .exe likewise.
4. Real church test with the HDMI capture card + the i7-10750H laptop (checklist in docs/TESTING.md section 7); try OBS 30/31/32 once (ABI-verified only).
5. Ideas: pose/head keypoints for headroom independent of raised arms; face model for the Face preset.
