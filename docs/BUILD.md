# Building from source

## Requirements

* Windows 10/11 x64
* Visual Studio 2022 or Build Tools 2022 with *Desktop development with C++* (MSVC v143,
  Windows 11 SDK)
* CMake 3.24+
* Git
* Inno Setup 6 (only for the installer)
* ffmpeg on `PATH` (only for the offline harness / tests)
* OBS Studio 33.x installed (only for the libobs integration test; the build itself can use
  the official OBS release zip instead)

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
winget install Kitware.CMake JRSoftware.InnoSetup Gyan.FFmpeg Git.Git
```

## 1. Dependencies

```powershell
scripts\setup.ps1
```

Creates `third_party\` (git-ignored):

| Folder | Content |
|---|---|
| `obs-studio-30.0.0\` | OBS 30.0.0 headers - the **plugin API** (oldest supported OBS) |
| `obs-studio\` | OBS 33.0.1 headers - used by the integration-test host |
| `obs-sdk\` | `include\` (30.0 API), `include-host\` (33.0.1), `lib\obs.lib`, `lib\w32-pthreads.lib` |
| `ort-1.24.4\` | ONNX Runtime, DirectML build (NuGet `Microsoft.ML.OnnxRuntime.DirectML`) |
| `directml-1.15.4\` | DirectML redistributable (NuGet `Microsoft.AI.DirectML`) |
| `models\` | YOLOX-Nano / YOLOX-Tiny, SHA-256 verified |

**Why OBS 30.0 headers?** libobs refuses to load a plugin compiled against a newer libobs
than itself. Building against 30.0 makes one binary work on OBS 30, 31, 32 and 33.
`scripts\abi-check.ps1` compiles a probe against both header versions and proves that every
struct offset and enum value the plugin uses is identical.

**Import libraries** are generated from `obs.dll` with `dumpbin /exports` + `lib /def`
(symbols are resolved by name). Without a local OBS 33.0.1 the script downloads the official
OBS release zip for this.

## 2. Build and test

```powershell
scripts\build.ps1                # Release build + unit tests + real-model detector test
scripts\build.ps1 -Integration   # + libobs integration test and 1080p30 recording test
```

Or manually:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The ready-to-install plugin folder is staged at `build\stage\vmedia-ai-autoframe\`.

CMake options: `VMAF_BUILD_PLUGIN`, `VMAF_BUILD_TESTS`, `VMAF_BUILD_TOOLS` (all ON).

## 3. Package

```powershell
scripts\package.ps1
```

Produces in `release\`: `VMedia-AI-AutoFrame-Setup-x64.exe`,
`VMedia-AI-AutoFrame-<version>-x64.zip`, `SHA256SUMS.txt`, `RELEASE_NOTES.md`.

## macOS build (.pkg inside a .dmg)

Needs a Mac (macOS 13+) with Xcode command line tools (`xcode-select --install`), CMake 3.24+,
Git and, for the detector test, ffmpeg (`brew install cmake ffmpeg`).

```bash
bash scripts/setup-macos.sh      # OBS 30.0 API headers, OBS 33.0.1 (Apple + Intel) libobs,
                                 # ONNX Runtime 1.23.2 universal2, YOLOX models
bash scripts/package-macos.sh    # universal build, unit + detector tests, load test inside
                                 # the real OBS.app (Apple + Intel via Rosetta), .pkg, .dmg
```

Output: `release/VMedia-AI-AutoFrame-<version>-macOS.dmg` (+ `.sha256`). The build is ad-hoc
signed. To sign for distribution set `MACOS_SIGN_IDENTITY` ("Developer ID Application: ...") and
`MACOS_INSTALLER_IDENTITY` ("Developer ID Installer: ..."), then notarize the DMG with
`xcrun notarytool submit ... --wait` and `xcrun stapler staple`.

Without a Mac, run the **build** workflow on GitHub (Actions > build > Run workflow): the
`macos-universal` job uploads the DMG as an artifact.

## 4. Developer tools

| Tool | Purpose |
|---|---|
| `build\Release\vmaf-tests.exe [filter]` | 44 unit/scenario tests (tracking, framing, zoom, recovery, YOLOX decode) |
| `build\Release\vmaf-detector-test.exe [--bench]` | real ONNX Runtime + model on real footage; benchmark CPU/GPU |
| `build\Release\autoframe-sim.exe in.mp4 out.mp4 [options]` | offline harness: the plugin's detector + tracker + framing on a video file, writes a side-by-side review video + CSV (options in `tools\autoframe-sim.cpp`) |
| `build\Release\obs-headless-test.exe <obs bin> <stage> <media> <out> [phases]` | boots real libobs (D3D11), loads the plugin, renders footage through it |
| `scripts\abi-check.ps1` | libobs ABI comparison between header versions |
| `tests\fetch-test-media.ps1` | downloads the test footage (Mixkit free license - not redistributed in the repo) |

## 5. Project layout

```
src/
  plugin-main.cpp          module entry points
  autoframe-filter.*       OBS filter: lifecycle, tick, render, hotkeys, commands
  obs-settings.*           settings <-> config, properties UI
  obs-renderer.*           GPU: source capture, letterbox + staged readback, crop shader, overlay
  detection-worker.*       background inference thread (newest-frame mailbox)
  person-detector.*        ONNX Runtime session (runtime-loaded), SHA-256 model check
  yolox.*                  YOLOX pre/post-processing, NMS
  person-tracker.*         ByteTrack-style multi-person tracker
  framing-controller.*     target selection, lock, dead zones, pulpit, group, recovery, overrides
  zoom-controller.*        composition -> crop height, zoom limits
  motion.hpp               frame-rate independent smoothing with speed/acceleration limits
  geometry.hpp settings.*  shared types and presets
  performance-monitor.*    measured timings
data/  effects/autoframe.effect  locale/en-US.ini
tests/ tools/ scripts/ installer/ docs/
```
