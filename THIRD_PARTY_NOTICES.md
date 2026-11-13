# Third-party components

V Media AI AutoFrame is licensed under the GNU General Public License v2.0 or later
(see `LICENSE`). It ships or uses the following third-party components.

| Component | Version | License | How it is used |
|---|---|---|---|
| OBS Studio (libobs) | 33.0.1 headers / ABI | GPL-2.0-or-later | Plugin API. Not redistributed - provided by the user's OBS installation. |
| ONNX Runtime (DirectML build) | 1.24.4 | MIT | AI inference. `data/runtime/onnxruntime.dll`, unmodified, from the official NuGet package `Microsoft.ML.OnnxRuntime.DirectML`. Telemetry is disabled at start-up (`DisableTelemetryEvents`). |
| DirectML | 1.15.4 | Microsoft DirectML redistributable license (see `Microsoft.AI.DirectML` NuGet package, `LICENSE.txt`) | Optional GPU inference on DirectX 12 GPUs. `data/runtime/DirectML.dll`, unmodified. |
| YOLOX-Nano / YOLOX-Tiny ONNX models | 0.1.1rc0 release | Apache-2.0 (Megvii) | Person detection. `data/models/*.onnx`, unmodified official release files, SHA-256 verified before loading. |
| w32-pthreads headers (via OBS) | - | LGPL-2.1 | Build-time headers only. |

## Documentation images

Screenshots and the quick guide in `docs/` show the plugin working on stock footage from
Mixkit (free license, https://mixkit.co/license/). The source clips themselves are not part of
this repository; `tests/fetch-test-media.ps1` downloads them for testing.

## Why these choices

* **YOLOX instead of YOLOv5/v8/v11 (Ultralytics):** Ultralytics models are AGPL-3.0, which
  would impose AGPL terms on anyone redistributing the plugin. YOLOX is Apache-2.0.
* **ONNX Runtime DirectML build:** one runtime gives fast CPU inference *and* optional GPU
  inference on any DirectX 12 card (GTX 1650/1660 included) without CUDA, cuDNN, TensorRT
  or an RTX GPU.

## Upstream sources

* OBS Studio: https://github.com/obsproject/obs-studio
* ONNX Runtime: https://github.com/microsoft/onnxruntime
* DirectML: https://github.com/microsoft/DirectML
* YOLOX: https://github.com/Megvii-BaseDetection/YOLOX

## Model checksums (SHA-256)

```
c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d  yolox_nano.onnx
427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7  yolox_tiny.onnx
```
