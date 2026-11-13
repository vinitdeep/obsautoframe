# Testing

Everything below marked **PASS** was actually executed. Anything not executed is listed under
*Not yet tested* - nothing there is claimed to work.

Test machine: Windows 11 Pro (build 26300), **AMD Ryzen 5 3600** (6 cores / 12 threads),
**NVIDIA GeForce GTX 1650 4 GB** (driver 617.42), OBS Studio **33.0.1**, MSVC 14.44.
The intended target laptop (Intel i7-10750H + GTX 1650/1660) is similar in core count and was
**not** available; its numbers are expected to be in the same range but are not measured.

## 1. Unit and scenario tests - `vmaf-tests.exe` (no OBS)

49 test cases, 1,664 checks, **all PASS**. Scenarios are scripted worlds sampled by a simulated
detector at the configured rate while the framing runs at video rate - exactly like in OBS.

| Area | What is verified |
|---|---|
| Geometry | IoU/union, letterbox mapping 1080p/4K/portrait round-trip, boxes clipped to the image, crop clamp never leaves the source, max crop for every output aspect |
| Tracking | one stable ID for a walker at 2.5 detections/s, tentative->confirmed, one-frame false positives dropped, low-confidence detections keep a partly hidden person (ByteTrack stage 2), **two people crossing keep their IDs**, lost tracks expire, re-appearance gets the old ID |
| Framing | walker always inside the crop + crop always inside the source, zoom limits, **dead zone ignores sways**, no per-frame jump above the speed limit, **30/60/144 fps trajectories agree**, 9:16 aspect, **person lock when someone walks in front**, group framing ignores background people, **stage area / bottom-edge rule ignore the congregation**, **pulpit anchor holds still and follows when leaving**, resolution change, pause / force wide / lock / zoom / reset zoom / reset, manual selection + cycling |
| Zoom | composition math (body fraction, fill, headroom), min/max zoom, wide-shot definition, wide subjects kept whole, truncated-body estimate, fixed zoom, zoom rate limit |
| Recovery | hold during the timeout (no jump, no zoom), then monotonic glide to wide; re-acquire after occlusion without jumping to another person; re-acquire when the tracker issued a new ID; long timeouts keep the same ID; detector failure -> wide; no-return mode holds |
| YOLOX | grid/stride decoding, class filtering, wrong tensor rejected, other input sizes, NMS, BGR planar layout |

## 2. Real detector - `vmaf-detector-test.exe` (ONNX Runtime 1.24.4 + real models + real footage)

**PASS.** A tampered model (wrong SHA-256) is rejected. On five real frames:

| Clip / time | Scene | Nano finds | Tiny finds |
|---|---|---|---|
| 23117 @ 2 s | presenter at whiteboard | 1 (0.90) | 2 |
| 23117 @ 12 s | presenter + audience | 3 | 3 |
| 23521 @ 11 s | two presenters + audience | 3 | 3 |
| 36706 @ 3 s | 4-piece band | 3 | 4 |
| 24068 @ 4 s | stage + foreground audience | 4 | 4 |

Annotated frames are written to `tests\out\det_*.png`.

### Inference benchmark (1280x720 frame, 40 runs after warm-up)

| Model | Provider | avg ms | p95 ms |
|---|---|---|---|
| YOLOX-Nano | CPU 1 thread | 24.0 | 28.1 |
| YOLOX-Nano | CPU 2 threads | 15.1 | 17.3 |
| YOLOX-Nano | CPU 4 threads | 16.8 | 18.6 |
| YOLOX-Nano | DirectML (GTX 1650) | 4.4 | 5.1 |
| YOLOX-Tiny | CPU 1 thread | 72.7 | 75.1 |
| YOLOX-Tiny | CPU 2 threads | 45.2 | 51.2 |
| YOLOX-Tiny | CPU 4 threads | 34.4 | 41.6 |
| YOLOX-Tiny | DirectML (GTX 1650) | 7.6 | 8.8 |

## 3. Offline harness on real footage - `autoframe-sim.exe`

| Run | Result |
|---|---|
| 23117, Church Preacher, AI blinded 6.0-9.5 s | TRACKING -> HOLDING (6.3 s) -> RETURNING WIDE (8.8 s, after the 2.5 s timeout) -> TRACKING **same person #1** (9.6 s). Subject inside the crop in 100 % of frames that had a subject; max pan 4.7 px/frame. |
| 36706, Group, max zoom 1.6 | all band members framed in every frame |
| 24068, Church Preacher, no stage area | **follows a foreground audience member** (the problem the stage area exists for) |
| 24068, Church Preacher, stage area bottom 72 % | follows the **presenter on stage** |
| 23521, Medium, 9:16 output | subject inside the vertical crop in 100 % of frames |

Review videos: `tests\out\sim_*.mp4` (left: camera with detections and the red virtual-camera
rectangle; right: framed output).

## 4. Inside real OBS - `obs-headless-test.exe` (libobs 33.0.1, Direct3D 11, GTX 1650)

The plugin binary built against the OBS **30.0** API is loaded into OBS **33.0.1**'s libobs.
**All PASS:**

| Phase | Result |
|---|---|
| load | module loads (`built for libobs 30.0.0`), filter registered and created |
| preacher (21 s, 720p30, overlays on) | detector ready in < 1 s, status TRACKING at 1.40x, 637 frames, **max frame gap 33.3 ms (= one frame)** |
| fidelity | filter at zoom 1.0 reproduces the input **bit-exactly** (max channel difference 0) - proves correct color, sRGB handling and no half-pixel shift |
| stress | 60 live settings changes incl. 3 model / thread restarts during playback; video kept flowing at 30 fps |
| failure | missing model -> logged, shown in properties, **video passes through unchanged** |
| multi | two filter instances in one scene, one removed while rendering |
| resize | camera switches 1280x720 -> 1920x1080 live; filter adapts, video keeps flowing |
| gpu | DirectML inference inside OBS, TRACKING |
| totals | **0 of 2,179 frames lagged**, **0 memory leaks** reported by libobs after shutdown, clean teardown |

### 1080p30 recording test (`record` phase)

1920x1080 canvas, 30 fps, Church Preacher filter on a 1080p camera feed, OBS's own **x264**
encoder (veryfast, CBR 6 Mbps) + MP4 muxer, 30 s while the AI runs on the CPU:

* **1,022 frames encoded, 0 dropped**, render lag 0 of 939 frames
* the recorded file probes as **1920x1080, 30 fps, 1,020 frames**
* (overlay safety is tested separately - see the `overlay` phase)
* whole-process CPU 21.5 % of all cores (x264 + AI + video decoding)

### Measured cost of the filter

| | value |
|---|---|
| OBS render time per frame, no filter / with filter | 1.02 ms / 1.09 ms (720p) |
| CPU time in the filter's render callback | 0.04-0.4 ms per frame |
| GPU->CPU readback of the 416x416 detector frame | 0.09-0.15 ms |
| Capture -> result latency | ~100 ms at 5 detections/s |
| Process CPU, no filter / filter on CPU / filter on GPU | 1.6-1.9 % / 3.9-4.0 % / 1.9-2.0 % of all cores |

"Latency" is the age of the newest detection, not added video delay: the video itself is not
delayed by the filter at all (it renders in the same frame).

### Vertical 1080x1920 (`vertical` and `scenefilter` phases)

*Vertical canvas:* OBS canvas 1080x1920, 1080p camera feed, AutoFrame at 9:16 (output resolution
Auto), Church Preacher preset, 30 s x264 CBR 6 Mbps encode. **PASS:** the camera+filter source
is exactly **1080x1920** at position 0,0 / scale 1 (fills the canvas, no black bars - all four
edges carry picture), tracking active, zoom capped at **1.12x** by the detail guard, **0 dropped,
0 lagged** frames (902 encoded), file probes as 1080x1920 / 901 frames, 20.9 % CPU.

*Horizontal + vertical from one camera:* clean camera source inside two scenes, AutoFrame 16:9
on one scene and AutoFrame 9:16 on the other. **PASS:** the vertical scene outputs 1080x1920,
both instances track the presenter, 0 lagged frames, 0 leaks.

*Real footage (offline harness):* presenter clip in 9:16 - subject inside the vertical crop in
100 % of frames. Band clip in 9:16 - **before the fix** the crop sat on the empty middle and cut
the bass player and singer in half; **after** it frames the lead singer whole plus whoever fits.
Unit tests `framing_vertical_group_never_cuts_people_in_half` (no framed person cut, no
membership flicker over 10 s), `output_dimensions_for_streaming_formats` and the detail-guard
tests lock this in.

### ONNX Runtime missing (`noruntime` phase, fresh process)

The plugin's `data\runtime` folder is removed and the plugin is loaded with a **relative**
data path, exactly as OBS passes it for portable installs. **PASS:** the filter reports
"the plugin's data\runtime folder (ONNX Runtime) is missing - please reinstall the plugin",
video passes through, no crash, 0 leaks.

## 5. Real OBS GUI - `tests\gui-smoke-test.ps1`

Runs the real OBS 33.0.1 frontend (Qt UI) in an isolated portable copy, with the plugin in the
portable layout and a scene collection containing a Media Source with the filter.
**8/8 PASS:** OBS starts, the frontend loads the plugin, the filter is restored from the scene
collection, the AI becomes ready (0.2 s), it tracks a person, no crash report, OBS exits
normally, and the frontend reports **"Number of memory leaks: 0"**.

Additionally verified by hand in the GUI (screenshots of the OBS window only, in
`tests\out\obs_gui_*.png`): the live preview with overlays, OBS's status bar at
30.00 / 30.00 FPS and 3.3 % CPU, the filter listed under *Effect Filters*, and the properties
dialog (status line, groups, all labels from the locale file).

**Bug found and fixed by this test:** the first GUI run crashed OBS. In portable / in-program
-folder installs OBS gives plugins a *relative* data path; ONNX Runtime's loader rejected it,
and the clean-up after that failure called into the never-initialised ONNX Runtime API. Fixed
by making the paths absolute and by only creating ONNX Runtime objects after the runtime has
loaded; the worker thread also catches every exception now. The `noruntime` phase and the
relative-path handling in the libobs harness are the regression tests for it.

## 5b. The user's own installed OBS (2026-11-13)

* **Installer executed** (`VMedia-AI-AutoFrame-Setup-x64.exe /VERYSILENT`, elevated): exit 0,
  files in `C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe`, listed in Windows Apps as
  "V Media AI AutoFrame (OBS plugin)" 1.1.0.
* Installed OBS 33.0.1 (normal, non-portable) loaded it from ProgramData, on a separate test
  profile (1080x1920 canvas) and test scene collection:
  * *Vertical - Preacher*: tracking + person lock, zoom 1.12x (detail guard), fills the canvas at
    position 0,0 / scale 1, OBS status bar CPU 4.6 %, 30.00 / 30.00 FPS.
  * **Recording** started/stopped through the OBS UI: 1080x1920, 30 fps, 700 frames; debug
    overlays vanished from the preview and the file while recording; 16 % CPU, 30.00 FPS.
  * *Vertical - Worship team*: "TRACKING GROUP (1)" - lead singer framed whole.
  * *Vertical - Iriun camera (live)*: real DirectShow camera device opened and processed by the
    filter (phone app not running, so no person to track).
  * OBS exited normally, "Number of memory leaks: 0", no crash report.

## 6. Not yet tested (needs the hardware / an operator)

* A real HDMI capture card source (async source path is the same one the media source uses,
  but not run on real hardware)
* The i7-10750H laptop target
* OBS Studio 30.x, 31.x, 32.x (ABI identity verified by `scripts\abi-check.ps1`, not run)
* Live streaming to a service (recording was tested; the streaming path uses the same render)
* 4K camera input at 4K canvas
* Camera physically unplugged and re-plugged (resolution change and source removal were tested)
* Changing every setting by hand in the properties dialog (exercised programmatically: 60
  random settings changes in the stress phase)
* Hotkeys pressed through OBS's hotkey system (the commands themselves are unit-tested and
  exercised via the properties buttons' code path)
* The installer on a *clean* machine (it was executed on the development PC - section 5b).
* A live person in front of a real camera (the camera device path was exercised without a person).

## 7. Manual OBS checklist (for the first real church setup)

1. [ ] Install with the installer; OBS log shows `V Media AI AutoFrame 1.1.2 loaded`.
2. [ ] Add the filter to the capture card source; status shows `Ready: 416x416 input, CPU`.
3. [ ] Choose Church Preacher Mode; enable boxes + guides; the preacher has a green LOCK box.
4. [ ] Set the stage area bottom edge above the congregation; only stage people get boxes picked.
5. [ ] Press *Set pulpit...* with the preacher at the pulpit; the shot is still while they preach there.
6. [ ] Preacher walks left, right, back: the shot follows smoothly and returns to the pulpit.
7. [ ] Someone crosses in front of the preacher: the lock stays on the preacher.
8. [ ] Preacher leaves the stage: shot holds ~2.5 s, then glides wide; resumes on return.
9. [ ] Assign hotkeys; test pause, force wide, lock, zoom in/out, reset, next person.
10. [ ] Start a recording: debug graphics disappear; stop: they return.
11. [ ] Stream/record 30 minutes at 1080p30: *Stats* dock shows no rendering lag / skipped frames.
12. [ ] Try *Use GPU acceleration*; status shows `DirectML (GPU)`; compare CPU usage in Task Manager.
13. [ ] Unplug and re-plug the capture card: video and tracking recover.
14. [ ] Close and reopen OBS: settings are kept.

## 8. Tracking graphics never in stream / recording (1.1.1, user-reported bug)

*Bug:* a recording made with 1.1.0 showed the stats panel and boxes during its first 0.87 s
(the old "hide while live" check polled once per second). `tests\check-overlay-leak.py` scans
every frame for the overlay's colours: it flags exactly frames 0-26 of that recording.

*Fix:* graphics are drawn only when OBS renders into a window (Filters dialog, Studio Mode
preview, projector); never into the program texture that feeds stream / recording / virtual
camera. No option can change this.

*Verified:* libobs `overlay` phase - program output pixel-identical with graphics on/off; all
frames of a recording clean incl. the first; graphics toggled during a recording - all frames
clean. In the user's installed OBS (1.1.1 via the installer): recording made while the Filters
window showed boxes and stats - **665 of 665 frames clean**; main preview clean at the same
moment; 0 leaks, no crash.

## 9. Webcam / phone camera close to the person (1.1.2, user-reported)

*Bug:* in the user's OBS with an Iriun phone webcam the filter reported `TRACKING ... zoom 1.00x`
and the shot never moved: the person sat close to the camera (cut off at the bottom) at ~85 % of
the width; a waist-up shot of such a close person is "bigger than the picture", so the crop
stayed at the whole picture with no room to move.

*Fix:* when the person is cut off at the bottom, zoom in just enough for a crop centred on them
(within Maximum zoom). Unit test `zoom_webcam_close_person_at_edge_gets_centred` (was 27 % off
centre at 1.00x; now < 20 % at max zoom 1.4, < 7 % at 2.0); stage behaviour unchanged (all other
tests pass). Real footage close-up clip: 1.2-1.4x, 5-17 % off centre.

*Verified in the user's OBS* (1.1.2 via installer, live Iriun camera 1920x1080):
`TRACKING | 8.9 detections/s | 25.4 ms | zoom 2.00x`, person centred, head in shot.
