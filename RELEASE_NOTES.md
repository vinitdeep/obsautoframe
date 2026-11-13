# V Media AI AutoFrame - release notes

## 1.1.2 - webcam / phone camera close to the person

* **New: macOS.** `VMedia-AI-AutoFrame-1.1.2-macOS.dmg` - one universal build for Apple silicon and
  Intel Macs (macOS 13.4+, OBS 30+), with a .pkg installer. *Use GPU* runs through Core ML. Tested
  on GitHub's Mac machines: unit + real-model detector tests, and the plugin loaded inside the real
  OBS 33.0.1 (Apple silicon and Intel builds). Not yet signed/notarized by Apple (see INSTALLATION.md).

* **Fixed:** with a webcam or phone camera (e.g. Iriun) close to the person, the shot stayed at
  zoom 1.00x and never moved. The person runs off the bottom of the picture, so a waist-up shot
  was "bigger than the picture". Now, when the person is cut off at the bottom, AutoFrame zooms
  in just enough to bring them toward the centre (within *Maximum zoom*). Stage setups with the
  full body visible are unchanged. Tip: for desk / webcam use set *Maximum zoom* to 1.8-2.0 so a
  person sitting at the edge can be fully centred.

## 1.1.1 - tracking graphics can never reach the stream or recording

* **Fixed:** the first ~0.8 s of every recording / stream could contain the debug boxes and the
  stats panel (the old "hide while live" check polled once per second).
* Debug graphics are now drawn **only into preview windows** (the filter's Filters window,
  Studio Mode's preview, source projectors). The program output - stream, recording, virtual
  camera, replay buffer, normal main preview - never contains them, whatever the settings.
  The old "Hide debug graphics while streaming or recording" option is gone (always enforced).
* Verified in OBS: with boxes/stats/guides on, the program output is pixel-identical to off;
  every frame of a recording is clean (including the first), also when the graphics are
  toggled on and off during the recording.
* **Fixed:** changing a setting that does not affect framing (debug graphics, detection rate,
  threads, GPU, sharpening) no longer makes the shot re-centre - safe to change on air.

## 1.1.0 - vertical 1080x1920 for YouTube Shorts / Instagram / Facebook / TikTok live

* **Output resolution** setting. *Auto* (default) gives exactly **1080x1920** for 9:16 (1080 on
  the short side for every non-camera aspect: 1:1 = 1080x1080, 4:5 = 1080x1350), so the filtered
  camera fills a 1080x1920 canvas 1:1 with no manual transform. Also Native / 720 / 1080 / 1440 /
  2160.
* **Vertical group framing fixed**: when a group is wider than the frame (a worship team in a
  9:16 shot), the shot frames a lead person plus every neighbour who fits *completely*, instead
  of centering on the empty middle and cutting everyone in half. Hysteresis prevents flicker.
* **Detail guard** (*Maximum enlargement*, default 2.0x): zoom is capped automatically when the
  camera cannot deliver the detail (9:16 from a 1080p camera -> max ~1.12x; a 4K camera keeps
  the full range).
* **Detail sharpening** (0-100 %, bicubic): restores edge contrast on enlarged vertical output.
* Status line shows output size, camera size, enlargement factor and the effective max zoom.
* Verified in OBS 33.0.1 on a 1080x1920 canvas: exact fill, tracking, 30 s x264 encode with
  0 dropped frames; horizontal + vertical from one camera (filters on scenes) verified too.

## 1.0.0

First release by V Media Production.

## What it does

An OBS Studio video filter that finds people in your camera feed with an on-device AI model
and keeps the chosen person framed with smooth digital pan, tilt and zoom - built for church
streams (preacher walking the stage), worship teams, conferences, classes and interviews.

## Features

* Person detection with YOLOX (Apache-2.0) through ONNX Runtime - fully offline
* CPU inference by default; optional GPU inference via DirectML on any DirectX 12 card
  (GTX 1650/1660 included - no RTX/CUDA/TensorRT)
* Inference on a background thread at a configurable rate (0.5-30 detections/s), independent
  of the video frame rate; the video never waits for the AI
* Multi-person tracker with temporary IDs (ByteTrack-style, geometry only - no face recognition)
* Person lock, automatic or manual person selection, next/previous person
* Stage area (feet must stand inside it) and "ignore people cut off by the bottom edge" to keep
  the camera off the congregation in the foreground
* 10 framing presets: Wide, Full Body, Medium, Upper Body, Close-Up, Face, Church Preacher,
  Worship Leader, Interview, Group
* Church Preacher Mode with pulpit anchor (perfectly still shot while at the pulpit)
* Dead zones, speed limit, acceleration limit, smoothing - frame-rate independent motion
* Automatic zoom with min/max limits (conservative 1.4x default), zoom dead band
* Group framing with background-person filter and max group size
* Lost-subject recovery: hold, timeout, glide back to a safe wide shot, re-acquire
* Manual override: pause, force wide, lock position, zoom in/out, reset zoom, reset framing,
  next/previous person, single/group - as buttons and OBS hotkeys
* Output aspect ratio: same as camera, 16:9, 9:16 (vertical), 1:1, 4:5, 4:3
* Sub-pixel crop with bicubic (Catmull-Rom) scaling; the crop never leaves the camera image
* Debug overlays (boxes, IDs, stats, dead zone, pulpit, stage area) - shown in preview
  windows only (see 1.1.1)
* Graceful failure: if the AI cannot start, the video passes through unchanged and the reason
  is shown in the filter properties and the OBS log
* Works with OBS Studio 30, 31, 32 and 33 (built against the OBS 30.0 API)

## Measured performance

Measured inside OBS Studio 33.0.1 (libobs, Direct3D 11) on an AMD Ryzen 5 3600 + GeForce GTX
1650, 1280x720 source at 30 fps, YOLOX-Nano at 5 detections/s:

| | value |
|---|---|
| Inference, CPU 2 threads | 15 ms avg (p95 17 ms) |
| Inference, DirectML GPU | 4.4 ms avg |
| Extra process CPU, CPU mode | ~2 % of all 12 threads (~0.25 core) |
| Extra process CPU, GPU mode | not measurable (< 0.5 %) |
| Graphics-thread cost of the filter | < 0.2 ms per frame |
| GPU-to-CPU readback per analysed frame | 0.1 ms |
| Capture-to-result latency | ~100 ms |
| Frames lagged by OBS | 0 of 2,179 |
| 1080p30 x264 recording, AI on CPU | 1,022 frames encoded, 0 dropped |
| Memory leaks reported by libobs at shutdown | 0 |

These are measurements on the machine above, not guarantees. The target laptop class
(i7-10750H + GTX 1650/1660) has not been measured yet - see `docs/TESTING.md`.

## Known limitations

* Digital zoom enlarges camera pixels. On a 1080p camera keep the maximum zoom near 1.4x; a 4K
  camera allows ~2x without visible softness.
* The person box includes raised arms, so very expressive gestures can nudge vertical framing;
  the generous vertical dead zone in the Worship Leader preset reduces this.
* HDR sources are framed correctly but not tone-mapped by the filter.
* Verified: real OBS 33.0.1 GUI (portable), libobs integration incl. a 30 s 1080p30 x264
  recording with the AI running (0 dropped frames). Not yet verified on: a real HDMI capture
  card, the i7-10750H target laptop, OBS 30/31/32 (ABI-verified only), live streaming to a
  service, and the installer on a clean machine. See `docs/TESTING.md`.
