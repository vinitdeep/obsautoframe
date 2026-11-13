# Architecture

## Pipeline

```
                         OBS graphics thread (every frame)                       AI worker thread
 camera / capture card                                                        (below-normal priority)
        |                                                                               |
        v                                                                               |
 [render target into texture] --every 1/rate s--> [area-downscale + letterbox 416x416]  |
        |                                           [copy to staging surface]           |
        |                                                  | (mapped 2 frames later:    |
        |                                                  |  no GPU stall, 0.1 ms)     |
        |                                                  v                            |
        |                                          newest-frame mailbox  ------------>  [YOLOX via ONNX Runtime]
        |                                          (older frame replaced = drop)        [decode + NMS]
        |                                                                               |
        |                         video_tick: <---------- detections (source px) -------+
        |                           tracker.update()  (ByteTrack-style IDs)
        |                           framing.update()  (who + where + smooth motion)
        v
 [crop shader: sub-pixel UV crop, bicubic] --> filter output (same size or chosen aspect)
        |
 [debug overlay: boxes, IDs, stats, guides - hidden while any output is active]
```

## Threads and locking

| Thread | Runs | Touches |
|---|---|---|
| UI | create, update, properties, buttons | config (under `mutex`), worker restart |
| hotkey | hotkey callbacks | command queue (under `mutex`) |
| graphics | `video_tick`, `video_render` | tracker, framing, renderer (tick/render under `mutex` for the shared parts) |
| AI worker | model load, inference | its own mailbox/result slot (own mutex) |

The graphics thread **never waits for the AI**: it only drops a frame into a one-slot mailbox
and picks up a finished result on a later tick. If inference is slower than the requested
rate, the older pending frame is replaced (counted as "dropped") - the AI always works on the
newest picture. The model is loaded on the worker thread, so adding the filter or changing the
model never freezes OBS.

## Why these choices

* **GPU readback of a small letterboxed frame** instead of reading the full video frame:
  the downscale happens on the GPU (area filter, no aliasing), only 416x416x4 bytes are
  copied, and the copy is mapped two frames after it was issued so the CPU never waits for the
  GPU. Works for every source type (capture cards, NDI, browser, nested scenes) - not only
  async sources.
* **YOLOX** (Apache-2.0) over Ultralytics YOLOv5/v8/v11 (AGPL-3.0, which would bind every
  redistributor of the plugin to AGPL terms). YOLOX-Nano is 3.6 MB and runs in ~15 ms on 2 CPU
  threads.
* **ONNX Runtime, DirectML build, loaded at run time** from the plugin's own `data\runtime`
  folder by full path:
  * one runtime for CPU and for any DirectX 12 GPU (GTX 1650/1660 included) - no CUDA,
    cuDNN, TensorRT or RTX;
  * never clashes with another plugin's `onnxruntime.dll` and never touches OBS's folder;
  * if it is missing or broken, the plugin still loads and the video passes through.
  * Busy-wait spinning of ORT threads is disabled so the AI does not steal CPU from encoders.
* **Detections drive a tracker, the tracker drives the director.** Detection at 2-10 Hz,
  motion at video rate. The virtual camera is a critically-damped spring integrated in fixed
  1/240 s sub-steps with speed and acceleration caps - identical motion at 24/30/60/144 fps.
* **Dead-zone hysteresis**: a move starts only when the subject leaves the dead zone, then
  follows until the subject is re-centered, then the shot is still again. This is how a
  camera operator works and avoids constant micro-motion.
* **No biometrics**: identity across frames is position + motion + box size only. IDs are
  in-memory counters that reset with OBS.
* **Built against the OBS 30.0 API** so one binary runs on OBS 30-33+
  (`scripts/abi-check.ps1` verifies the ABI).

## Failure behaviour

| Failure | Behaviour |
|---|---|
| runtime / model missing or corrupt | status "AI unavailable", log warning, video unchanged, framing goes to the safe wide shot |
| GPU requested but unavailable | falls back to CPU, reason shown in the status |
| inference slower than requested | newest-frame policy, effective rate adapts, video unaffected |
| source resolution changes | tracker reset, framing re-initialised centered wide |
| source missing / 0x0 | filter skips itself (OBS renders nothing as it would without the filter) |
| crop shader fails to compile | whole-pixel fallback crop with OBS's stock shader |
| filter removed / OBS closes | hotkeys unregistered first, worker joined, GPU objects freed in the graphics context |
