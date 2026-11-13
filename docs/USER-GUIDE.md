# V Media AI AutoFrame - User Guide

V Media AI AutoFrame watches your camera with an AI person detector and moves a *virtual*
camera - a crop of the real picture - so the person you care about stays well framed. The
real camera never moves; pan, tilt and zoom are digital.

Everything runs on your computer. No video leaves the PC, there is no internet connection, no
face recognition and nothing to subscribe to.

---

## 1. Quick start (2 minutes)

1. In OBS, right-click your camera / capture card source > **Filters**.
2. Under **Effect Filters** click **+** > **V Media AI AutoFrame**.
3. Open **General > Framing preset** and choose one, e.g. **Church Preacher Mode**.
4. Wait 1-2 seconds for "Ready" in the status line. The shot starts following the person.

Tip: while setting up, enable **Debug > Show bounding boxes** and **Show dead zone, pulpit zone
and stage area** to see exactly what the AI sees. They are drawn **only in preview windows** - the
filter's own Filters window, Studio Mode's preview and source projectors - and **never** in your
stream, recording or virtual camera (not even for one frame). OBS's normal main preview shows
exactly what goes on air, so it shows them neither.

## 2. Camera and capture card setup

* Add your HDMI capture card as a **Video Capture Device** source as usual and set its
  resolution to the camera's resolution (1920x1080 or 3840x2160) and 30 or 60 fps.
* Frame the *real* camera **wider** than the shot you want: digital zoom can only crop. Leave
  room around the area where the preacher walks.
* Resolution and zoom: the default maximum zoom is **1.4x**. On a 1080p camera a 1.4x crop is
  about 1370x770 pixels scaled to 1080p - still sharp. Higher zoom on a 1080p camera becomes
  visibly soft. A **4K camera** lets you use 2x or more with full 1080p detail.
* The filter outputs the same resolution as the camera (or the aspect you choose under
  *Framing > Output aspect ratio*). Use OBS's normal Output (Scaled) Resolution as usual.
* Put the filter **first** in the filter list if you also use color correction etc. (any
  order works; first is cheapest).

## 3. Presets

| Preset | Shot | Typical use |
|---|---|---|
| Wide Shot | whole person small in frame | stage overview that still follows |
| Full Body | head to toe | dancers, walking presenters |
| Medium Shot | waist up | default, most speakers |
| Upper Body | chest up | teachers, podcasts |
| Close-Up | head and shoulders | interviews (needs a 4K camera for quality) |
| Face Framing | face | 4K camera only |
| **Church Preacher Mode** | waist up, slow, large dead zone, pulpit anchor | sermons |
| **Worship Leader Mode** | waist up, tall vertical dead zone (raised hands) | singers |
| Interview Mode | chest up, calm | sit-down interviews |
| **Group Mode** | everyone in front together | worship team, panels |

Choosing a preset fills in the values in *Framing*, *Zoom* and *Movement*; adjust them
afterwards if you like. The preset list then simply shows what you started from.

## 4. Church Preacher Mode

1. Choose **Church Preacher Mode**.
2. **Set the pulpit:** ask the preacher to stand at the pulpit, then press
   **Church Preacher Mode > Set pulpit to the preacher's current position**. Adjust the
   **Pulpit zone width** so it covers where they normally move while preaching.
3. Behaviour:
   * Inside the pulpit zone the shot stays **perfectly still**, centered on the pulpit - no
     drifting while they gesture or turn.
   * When they walk out of the zone the virtual camera follows smoothly, never faster than
     *Maximum tracking speed*.
   * Small movements inside the **dead zone** never move the camera.
   * When they return to the pulpit the shot glides back to the pulpit composition.
   * If they leave the stage (or are hidden) the shot **holds** for the *Lost person timeout*,
     then glides to a safe wide shot centered on the pulpit, and resumes as soon as they
     reappear.

### Keep the camera off the congregation: the Stage area

A camera at the back of the church sees heads and shoulders of the congregation in the
foreground - often *bigger* than the preacher. Tell the filter where the stage is:

* **Subject Tracking > Stage area bottom edge**: move it up until it is just below the stage
  floor (turn on *Show dead zone, pulpit zone and stage area* to see the box). Only people
  whose **feet** are inside the stage area are picked up.
* Or enable **Ignore people cut off by the bottom of the picture**.

A preacher who is already being followed keeps being followed even if they step down from
the stage (e.g. to pray with someone at the front).

## 5. Choosing who to follow

* **Who to follow:** *Largest* (usually the main speaker), *Nearest the center*, *Most clearly
  visible*, or *Operator choice*.
* **Lock onto the selected person** (on by default): once chosen, the filter keeps following
  that person even when someone else walks across the stage. Identification uses only
  position and motion over the last seconds - no face recognition.
* **Pick a person manually:** turn on bounding boxes and IDs, press **Refresh people list**,
  choose *Person #N* in **Select person** - or use **Next person / Previous person**
  (buttons or hotkeys) to step through the people from left to right.

The numbers are temporary and start again when OBS restarts.

## 6. Group framing (worship team)

Choose **Group Mode** (or *Tracking mode: Group*). The shot frames everyone together.

* **Maximum people in the group shot** - the largest N people are framed.
* **Ignore people smaller than ... of the largest** - filters out people in the background.
* The stage area applies here too.
* The **Switch single person / group** hotkey toggles live (e.g. during worship -> sermon).

## 7. Manual override during a live service

All of these are buttons in the filter and **hotkeys** (OBS *Settings > Hotkeys*, listed under
the filter's name - assign keys or Stream Deck buttons):

| Hotkey | Effect |
|---|---|
| Pause / resume tracking | freeze the virtual camera where it is |
| Force wide shot (toggle) | glide to the full camera picture |
| Lock current position (toggle) | hold this framing; zoom keys still work |
| Zoom in / Zoom out | 10 % steps (manual zoom while paused/locked, zoom bias while tracking) |
| Reset zoom to 1.0x | full picture |
| Reset framing | clear everything and start automatic tracking again |
| Follow next / previous person | change the subject |
| Switch single person / group | |

Nothing is ever cut abruptly: every override is a smooth movement.

## 8. Fine-tuning

* **Movement smoothing** (seconds): higher = more cinematic, slower to settle.
* **Maximum tracking speed** (% of the picture width per second): caps how fast the shot pans.
* **Acceleration limit**: prevents sudden starts.
* **Dead zones**: how far the person may move before the shot follows.
* **Zoom dead band**: ignores small size changes (e.g. leaning forward).
* **Headroom / Horizontal offset**: composition. A small horizontal offset gives "lead room".
* **Output aspect ratio 9:16** makes a vertical 1080x1920 shot for Shorts / Reels / vertical
  live from a landscape camera - see section 9.

All motion is frame-rate independent: it looks the same at 24, 30 or 60 fps.

## 9. Vertical 1080x1920 (YouTube Shorts / Instagram / Facebook / TikTok live)

AutoFrame turns a normal landscape camera into a vertical 9:16 shot that follows the person -
the crop simply moves left and right with them.

### A. Vertical stream only (simplest)

1. *Settings > Video*: **Base (Canvas) Resolution 1080x1920**, **Output (Scaled) Resolution
   1080x1920**, 30 fps.
2. Add the camera / capture card source and the **V Media AI AutoFrame** filter.
3. *Framing > Output aspect ratio*: **9:16 vertical**. Leave *Output resolution* on **Auto** -
   the source then becomes exactly **1080x1920** and fills the canvas with no black bars and no
   manual resizing (if you placed it earlier, right-click it > *Transform > Reset Transform*).
4. Pick a preset: *Church Preacher*, *Worship Leader* or *Medium Shot*.
5. Stream as usual with your platform's stream key (for vertical, 4,500-6,000 kbps and a
   2 s keyframe interval are common choices - check your platform's current requirements).

### B. Horizontal AND vertical at the same time from one camera

Used with a vertical-canvas plugin (for example Aitum Vertical) or a second output:

1. Keep the camera source itself **without** AutoFrame (a filter on a source applies
   everywhere the source is used).
2. Make a scene **"Camera 16:9"** containing the camera (fit to screen) and add AutoFrame to
   the **scene** (right-click the scene > *Filters*) with *Output aspect: Same as camera*.
3. Make a scene **"Camera 9:16"** containing the same camera and add a second AutoFrame to that
   scene with *Output aspect: 9:16* - it outputs 1080x1920.
4. Use "Camera 16:9" in your normal scenes and "Camera 9:16" in your vertical scenes.

Each AutoFrame runs its own AI. Two instances cost about twice the CPU - turn on *GPU
acceleration* for one or both, or use *Low CPU* mode.

### Picture quality in vertical

A 9:16 slice of a 1920x1080 camera is only 608x1080 pixels; filling 1080x1920 enlarges it
**1.78x** even before any zoom. Therefore:

* The **detail guard** (*Zoom > Detail guard: maximum enlargement*, default 2.0x) automatically
  limits the zoom (to ~1.12x for vertical from a 1080p camera): the shot pans to follow, but
  never zooms into mush. The status line shows the real numbers, e.g.
  `Output 1080x1920 from camera 1920x1080 | enlarged 1.78x - 2.00x | max zoom 1.12x`.
* Set **Detail sharpening** to 20-40% (with *Bicubic* scaling) to restore edge contrast.
* A **4K camera / capture card** gives a 1215x2160 vertical slice - full 1080x1920 detail with
  room to zoom (the guard then allows the preset's full zoom).

### Groups in vertical

A worship team spread across a landscape picture cannot fit in a narrow 9:16 frame. Instead of
centering on the empty middle and cutting everyone in half, Group mode then frames a **lead
person** (chosen by *Who to follow* - use *Person nearest the center* for a lead singer in the
middle) **plus every neighbour who fits completely**. Membership has hysteresis, so people do not
pop in and out of the shot.

## 10. Performance

| Processing mode | Detections / s | CPU threads | When |
|---|---|---|---|
| Low CPU | 2.5 | 1 | older laptops, heavy x264 encoding |
| Balanced (default) | 5 | 2 | most systems |
| Performance | 10 | 4 | fast walkers, spare CPU |

* **Use GPU acceleration (DirectML)** moves the AI to the graphics card - on a GTX 1650 the
  model runs in ~4-5 ms and costs almost no CPU. If you encode with NVENC on the same GPU this
  is still usually the best choice; watch OBS's *Stats* dock for "rendering lag".
* **YOLOX-Tiny** is a little more accurate (finds partly hidden people more often) but needs
  ~3x the CPU time; with GPU acceleration it is cheap.
* The detection rate does **not** limit smoothness: the virtual camera moves every frame.

Measured numbers are in `docs/TESTING.md`.

## 11. Debug

* **Show bounding boxes**: green = followed person (LOCK), cyan = group members,
  yellow = other people, grey = person lost a moment ago.
* **Show detection FPS / inference time / zoom**: live status panel.
* **Where they appear:** only in windows that preview the source directly - the Filters window
  of this filter, **Studio Mode's** left preview, and *right-click the source > Open Source
  Projector*. They never appear in the stream, recording, virtual camera, replay buffer or OBS's
  normal main preview (which shows exactly what goes on air). There is no setting that changes
  this.
* **Write diagnostic logs**: every 10 s a status line in the OBS log (*Help > Log Files*).
