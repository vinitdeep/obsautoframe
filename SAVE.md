# SAVE - V Media AI AutoFrame (state as of 2026-11-13, v1.1.2)

## v1.1.2: webcam fix - person cut off at bottom (close to camera) now zooms in to centre them (zoom-controller compose, single mode only). Verified on user Iriun cam: zoom 2.00x centred. Installed in user OBS. NOT yet committed/pushed to GitHub (28dd452 is 1.1.1).

## v1.1.1: debug graphics ONLY in preview windows (gs_get_render_target()==NULL), never in program/stream/recording - user requirement, do not add an option to draw into the program output (OBS queues frames before an output starts, so it always leaks). Non-framing settings no longer re-centre the shot. Installed in user OBS. Leak scanner: tests/check-overlay-leak.py.

## v1.1.0 (same day): vertical 1080x1920
Output resolution setting (Auto = 1080x1920 for 9:16), vertical group framing (lead + whole
neighbours that fit, hysteresis), detail guard (maxEnlarge 2.0x caps zoom), sharpening, status
shows enlargement. Tested: `vertical` phase (1080x1920 canvas, exact fill, 30 s encode 0 dropped)
and `scenefilter` phase (horizontal + vertical from one CLEAN camera: AutoFrame on two scenes -
never put AutoFrame on the camera itself for dual output, it would crop twice).

## Where things are
* Source: this folder. `third_party/`, `build/`, `release/`, `tests/media/`, `tests/out/` are git-ignored and regenerated.
* Release files: `release/VMedia-AI-AutoFrame-Setup-x64.exe` (35.5 MB), `release/VMedia-AI-AutoFrame-1.0.0-x64.zip`, `SHA256SUMS.txt`.
* Staged plugin folder: `build/stage/vmedia-ai-autoframe/`.

## Rebuild from nothing
```powershell
scripts\setup.ps1; tests\fetch-test-media.ps1
scripts\build.ps1 -Integration     # everything incl. real-OBS GUI smoke test (~6 min)
scripts\package.ps1
```

## Key decisions (do not undo without reason)
* Plugin compiled against **OBS 30.0.0 headers** (libobs rejects plugins built with newer headers) -> runs on OBS 30-33. `scripts\abi-check.ps1` proves ABI identity. The integration-test host uses 33.0.1 headers (`include-host`).
* Import libs generated from installed `obs.dll` (dumpbin + lib), no OBS source build.
* ONNX Runtime **DirectML** build, loaded at run time by FULL path from `data\runtime` (never link onnxruntime.lib; ORT objects only created after the runtime loaded - destroying an ORT wrapper without a loaded API crashes).
* YOLOX (Apache-2.0), not Ultralytics (AGPL).
* Overlay immediate drawing: `gs_render_start(false)` (true corrupts libobs' immediate VB -> OBS crash).
* libobs output texture is BGRA (harness requests VIDEO_FORMAT_BGRA).
* Stage area tests the person's FEET (box bottom), not the box center.
* OBS 33 scene teardown in the harness: `obs_source_remove(scene)` before releasing.

## Verified (see docs/TESTING.md)
Unit 44/44, real-model detector test, libobs integration (8 phases), 1080p30 x264 recording (0 dropped), no-runtime regression, real OBS GUI smoke test 8/8, 0 memory leaks everywhere. Measured on Ryzen 5 3600 + GTX 1650.

## Open / next
1. Run the installer on a machine (needs admin; not executed here) and check OBS loads from `C:\ProgramData\obs-studio\plugins`.
2. Real church test with the HDMI capture card + the i7-10750H laptop (checklist in docs/TESTING.md section 7).
3. Try OBS 30/31/32 once (ABI-verified only).
4. Not committed to git yet (repo is GitHub-ready: README, CI workflow, .gitignore). CI workflow not yet run on GitHub.
5. Ideas: pose/head keypoints for headroom independent of raised arms; NVIDIA Broadcast-free face model for Face preset; code signing for the installer.
