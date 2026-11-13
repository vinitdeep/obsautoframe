# Models

The plugin ships the official, unmodified YOLOX ONNX exports (Apache-2.0, Megvii):

| File | Input | Size | SHA-256 |
|---|---|---|---|
| `yolox_nano.onnx` | 416x416 | 3.6 MB | `c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d` |
| `yolox_tiny.onnx` | 416x416 | 20 MB | `427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7` |

Source: https://github.com/Megvii-BaseDetection/YOLOX/releases/tag/0.1.1rc0

They are downloaded and hash-checked by `scripts/setup.ps1` into `third_party/models/` and
copied into the plugin's `data/models/` at build time. The plugin verifies the SHA-256 again
before loading them and never downloads anything at run time.

## Custom models

*Performance > AI model > Custom YOLOX ONNX model* accepts any YOLOX export with one input
`[1,3,H,W]` (fixed H, W divisible by 32, BGR 0-255, no normalisation, top-left letterbox)
and raw head output `[1, N, 5+classes]` with class 0 = person (COCO). Custom files are not
hash-checked. Check the model's license before distributing it.
