# Troubleshooting

First look at the filter's **General** status line (press *Refresh status*) and the OBS log
(*Help > Log Files > View Current Log*, search for `vmedia-autoframe`).

### The filter is not in the list

* Check the log for `V Media AI AutoFrame 1.1.2 loaded`. If it is missing, check the folder
  layout in `docs/INSTALLATION.md` (the DLL must be in `...\vmedia-ai-autoframe\bin\64bit\`).
* OBS older than 30.0 is not supported. 32-bit OBS is not supported.
* If the log says `compiled with newer libobs`, you have an OBS older than the plugin's API
  (30.0) - update OBS.
* Plugin Manager (OBS 32+): make sure the plugin is not disabled under *Tools > Plugin Manager*.

### "AI unavailable - video passes through unchanged"

The video keeps working; only the automatic framing is off. The reason is shown after the
message:

| Reason | Fix |
|---|---|
| `Could not load ONNX Runtime ... (Windows error 126)` | `data\runtime\onnxruntime.dll` or `DirectML.dll` missing - reinstall. |
| `Windows error 193` | a 32-bit or damaged DLL - reinstall. |
| `Model file failed integrity check` | the model file is damaged or was replaced - reinstall. |
| `Model file not found` | custom model path is wrong, or reinstall. |
| `model input must be [1,3,H,W]...` / `does not match YOLOX layout` | the custom model is not a YOLOX export. |

### "GPU (DirectML) unavailable, using CPU"

DirectML needs Windows 10 1903+ and a DirectX 12 capable GPU with a current driver. Update the
graphics driver. On laptops the high-performance GPU is requested automatically; if Windows
still picks the integrated GPU, set OBS to "High performance" in *Windows Settings > Display >
Graphics*.

### The shot follows the wrong person

* Set the **Stage area** (Subject Tracking) so the congregation's feet are outside it, or
  enable **Ignore people cut off by the bottom of the picture**.
* Use **Lock onto the selected person** and pick the person with **Next person** or the
  **Select person** list.
* For a group on stage use **Group Mode**.

### The shot moves too much / jitters

* Increase **Horizontal / Vertical dead zone** and **Movement smoothing**.
* Use **Church Preacher Mode** with the pulpit anchor.
* Raise the **Zoom dead band** if the zoom "breathes".
* The person box includes raised arms; the *Worship Leader* preset has a tall vertical dead
  zone for that.

### The shot is too slow / loses fast walkers

* Raise **Maximum tracking speed**, lower **Movement smoothing**, use a smaller dead zone.
* Use the **Performance** processing mode or GPU acceleration for more detections per second.

### The picture looks soft

That is the digital zoom. Lower **Maximum zoom** (1.3-1.4x on a 1080p camera), keep
**Zoom scaling quality** on *Bicubic*, or use a 4K camera/capture card.

### Stream stutters / "rendering lag" or "encoding overloaded"

* Use **Low CPU** mode or enable **GPU acceleration**.
* Lower the detection rate (2-3/s is plenty for a preacher).
* Make sure only one AutoFrame filter runs per camera (each instance runs its own AI).
* The filter itself costs well under 1 ms of the OBS render time per frame; if OBS lags, the
  encoder or other filters are usually the bottleneck (*View > Docks > Stats*).

### I don't see the boxes / stats in the main preview

That is intended (since 1.1.1): debug graphics are drawn only into preview windows and can
never reach the stream or a recording. To see them, open the filter's **Filters** window, turn
on **Studio Mode** (left preview), or right-click the source > **Open Source Projector**.

### Debug boxes appeared on a stream or recording

Only versions before 1.1.1 could do this (the first second of a recording). Update the plugin.

### Hotkeys are not listed

They appear in *Settings > Hotkeys* under the **filter's name** (not the source's) after the
filter has been added at least once.

### Reporting a problem

Enable **Debug > Write diagnostic logs**, reproduce for a minute, and send the OBS log file.
Logs contain timing and status only - never images.
