// V Media AI AutoFrame - the OBS video filter.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "autoframe-filter.hpp"
#include "obs-settings.hpp"

#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#define T(x) obs_module_text(x)
#define LOG(level, fmt, ...) blog(level, "[vmedia-autoframe] " fmt, ##__VA_ARGS__)

namespace vmaf {

namespace {

// SHA-256 of the models shipped with the plugin (verified before loading).
constexpr const char *kNanoSha = "c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d";
constexpr const char *kTinySha = "427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7";

std::wstring widen(const char *utf8)
{
	if (!utf8 || !*utf8)
		return {};
	wchar_t *w = nullptr;
	os_utf8_to_wcs_ptr(utf8, 0, &w);
	std::wstring out = w ? w : L"";
	bfree(w);
	return out;
}

std::string moduleFile(const char *rel)
{
	char *p = obs_module_file(rel);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

/* Worker options for a config + a key identifying them (restart only when it changes). */
DetectorOptions detectorOptions(const AutoFrameConfig &c, std::string &key)
{
	DetectorOptions o;
	o.runtimeDir = widen(moduleFile("runtime").c_str());
	std::string model;
	switch (c.model) {
	case DetectorModel::YoloxTiny:
		model = moduleFile("models/yolox_tiny.onnx");
		o.expectedSha256 = kTinySha;
		break;
	case DetectorModel::Custom:
		model = c.customModelPath;
		break;
	case DetectorModel::YoloxNano:
		model = moduleFile("models/yolox_nano.onnx");
		o.expectedSha256 = kNanoSha;
		break;
	}
	o.modelPath = widen(model.c_str());
	o.threads = c.threads;
	o.useGpu = c.useGpu;
	key = model + "|" + std::to_string(c.threads) + "|" + (c.useGpu ? "gpu" : "cpu");
	return o;
}

void outputSize(OutputAspect a, OutputSize s, uint32_t w, uint32_t h, uint32_t &ow, uint32_t &oh)
{
	outputDimensions(a, s, w, h, ow, oh);
}

const char *workerStatusName(WorkerStatus s)
{
	switch (s) {
	case WorkerStatus::Stopped:
		return "stopped";
	case WorkerStatus::Loading:
		return "loading";
	case WorkerStatus::Ready:
		return "ready";
	case WorkerStatus::Failed:
		return "FAILED";
	}
	return "?";
}

struct HotkeyDef {
	Command cmd;
	const char *name;
	const char *text;
};
const HotkeyDef kHotkeys[] = {
	{Command::TogglePause, "VMediaAutoFrame.TogglePause", "Hotkey.TogglePause"},
	{Command::ForceWide, "VMediaAutoFrame.ForceWide", "Hotkey.ForceWide"},
	{Command::LockPosition, "VMediaAutoFrame.LockPosition", "Hotkey.LockPosition"},
	{Command::ResetFraming, "VMediaAutoFrame.ResetFraming", "Hotkey.ResetFraming"},
	{Command::ZoomIn, "VMediaAutoFrame.ZoomIn", "Hotkey.ZoomIn"},
	{Command::ZoomOut, "VMediaAutoFrame.ZoomOut", "Hotkey.ZoomOut"},
	{Command::ResetZoom, "VMediaAutoFrame.ResetZoom", "Hotkey.ResetZoom"},
	{Command::NextPerson, "VMediaAutoFrame.NextPerson", "Hotkey.NextPerson"},
	{Command::PrevPerson, "VMediaAutoFrame.PrevPerson", "Hotkey.PrevPerson"},
	{Command::ToggleGroup, "VMediaAutoFrame.ToggleGroup", "Hotkey.ToggleGroup"},
};
static_assert(sizeof(kHotkeys) / sizeof(kHotkeys[0]) == (size_t)Command::SelectPerson, "hotkey table");

const char *commandName(Command c)
{
	switch (c) {
	case Command::TogglePause:
		return "toggle pause";
	case Command::ForceWide:
		return "force wide";
	case Command::LockPosition:
		return "lock position";
	case Command::ResetFraming:
		return "reset framing";
	case Command::ZoomIn:
		return "zoom in";
	case Command::ZoomOut:
		return "zoom out";
	case Command::ResetZoom:
		return "reset zoom";
	case Command::NextPerson:
		return "next person";
	case Command::PrevPerson:
		return "previous person";
	case Command::ToggleGroup:
		return "toggle group mode";
	case Command::SelectPerson:
		return "select person";
	case Command::Count_:
		break;
	}
	return "?";
}

void hotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *b = static_cast<HotkeyBinding *>(data);
	queueCommand(b->filter, b->cmd);
}

} // namespace

double monotonicSeconds()
{
	return (double)os_gettime_ns() / 1.0e9;
}

void queueCommand(AutoFrameFilter *f, Command c, int arg)
{
	if (!f)
		return;
	std::lock_guard<std::mutex> lock(f->mutex);
	f->commands.emplace_back(c, arg);
}

// ------------------------------------------------------------------ OBS callbacks

static const char *af_get_name(void *)
{
	return T("FilterName");
}

static void af_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	bool bicubic = true;
	const AutoFrameConfig c = readConfig(settings, bicubic);
	const int manual = manualPersonSetting(settings);
	std::string key;
	const DetectorOptions opts = detectorOptions(c, key);

	TrackerConfig tc;
	tc.highThresh = c.confidence;
	tc.lowThresh = std::max(0.08f, c.confidence * 0.4f);
	tc.maxLostTime = std::max(4.0f, c.lostTimeout + 2.0f);
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		f->cfg = c;
		f->bicubic = bicubic;
		f->framing.configure(c);
		f->tracker.configure(tc);
		if (manual != f->lastManualPerson) {
			if (f->lastManualPerson != -2) // ignore the value restored from a saved scene
				f->commands.emplace_back(Command::SelectPerson, manual);
			f->lastManualPerson = manual;
		}
	}
	f->worker.setMinScore(tc.lowThresh);
	if (key != f->workerKey) {
		// Model / threads / GPU changed: (re)start the AI. Video keeps flowing meanwhile;
		// framing holds or goes wide until the new model is ready.
		f->workerKey = key;
		LOG(LOG_INFO, "'%s': starting detector (%s, %d thread(s), %s)", obs_source_get_name(f->context),
		    c.model == DetectorModel::YoloxTiny ? "YOLOX-Tiny"
		    : c.model == DetectorModel::Custom  ? "custom model"
							: "YOLOX-Nano",
		    c.threads, c.useGpu ? "GPU/DirectML requested" : "CPU");
		f->worker.start(opts, tc.lowThresh);
	}
}

static void *af_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new AutoFrameFilter();
	f->context = source;
	obs_data_set_int(settings, "manual_person", -1); // tracking IDs never survive a restart

	for (size_t i = 0; i < f->hotkeys.size(); ++i) {
		HotkeyBinding &b = f->hotkeys[i];
		b.filter = f;
		b.cmd = kHotkeys[i].cmd;
		b.id = obs_hotkey_register_source(source, kHotkeys[i].name, T(kHotkeys[i].text), hotkeyPressed, &b);
	}
	af_update(f, settings);
	return f;
}

static void af_destroy(void *data)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	for (HotkeyBinding &b : f->hotkeys) // no hotkey callback may run after this point
		if (b.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(b.id);
	f->worker.stop();
	f->renderer.destroy();
	delete f;
}

static void af_defaults(obs_data_t *settings)
{
	writeDefaults(settings);
}

static obs_properties_t *af_properties(void *data)
{
	return buildProperties(static_cast<AutoFrameFilter *>(data));
}

static void applyCommand(AutoFrameFilter *f, Command c, int arg, double now)
{
	FramingController &fr = f->framing;
	switch (c) {
	case Command::TogglePause:
		fr.togglePause();
		break;
	case Command::ForceWide:
		fr.toggleForceWide();
		break;
	case Command::LockPosition:
		fr.toggleLockPosition();
		break;
	case Command::ResetFraming:
		fr.resetFraming();
		break;
	case Command::ZoomIn:
		fr.zoomStep(1.1f);
		break;
	case Command::ZoomOut:
		fr.zoomStep(1.0f / 1.1f);
		break;
	case Command::ResetZoom:
		fr.resetZoom();
		break;
	case Command::NextPerson:
		fr.cycleTarget(f->tracker, now, +1);
		break;
	case Command::PrevPerson:
		fr.cycleTarget(f->tracker, now, -1);
		break;
	case Command::ToggleGroup:
		fr.toggleGroupMode();
		break;
	case Command::SelectPerson:
		fr.selectTarget(arg);
		break;
	case Command::Count_:
		break;
	}
	LOG(LOG_INFO, "'%s': operator command: %s%s", obs_source_get_name(f->context), commandName(c),
	    c == Command::TogglePause ? (fr.paused() ? " (paused)" : " (resumed)") : "");
}

static void af_tick(void *data, float seconds)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	obs_source_t *target = obs_filter_get_target(f->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	const double now = monotonicSeconds();

	const WorkerStatus ws = f->worker.status();
	if (ws != f->loggedStatus) {
		f->loggedStatus = ws;
		const std::string msg = f->worker.statusMessage();
		LOG(ws == WorkerStatus::Failed ? LOG_WARNING : LOG_INFO, "'%s': detector %s - %s",
		    obs_source_get_name(f->context), workerStatusName(ws), msg.c_str());
	}


	std::lock_guard<std::mutex> lock(f->mutex);
	if (w && h && f->framing.setSourceSize((float)w, (float)h)) {
		f->tracker.reset();
		LOG(LOG_INFO, "'%s': source resolution %ux%u", obs_source_get_name(f->context), w, h);
	}

	DetectionResult r;
	if (f->worker.takeResult(r) && r.srcW == (int)w && r.srcH == (int)h) {
		f->tracker.update(r.dets, r.timestamp);
		f->perf.addInference(r.inferenceMs);
		f->perf.addDetection(now);
		f->perf.addLatency((float)((now - r.timestamp) * 1000.0));
	}

	for (const auto &c : f->commands)
		applyCommand(f, c.first, c.second, now);
	f->commands.clear();

	f->framing.update(f->tracker, now, seconds, f->worker.ready());

	if (f->cfg.diagnosticLogs && now - f->lastDiagLog >= 10.0) {
		f->lastDiagLog = now;
		const FramingOutput &o = f->framing.output();
		LOG(LOG_INFO, "'%s': %s target #%d zoom %.2fx | %s", obs_source_get_name(f->context),
		    statusName(o.status), o.targetId, o.zoom,
		    f->perf.summary(f->worker.framesProcessed(), f->worker.framesDropped()).c_str());
	}
}

namespace {

struct OverlaySnapshot {
	bool boxes = false, ids = false, stats = false, guides = false;
	std::vector<Track> tracks;
	FramingOutput out;
	RectF deadZone, pulpit, stage;
	TrackingMode mode = TrackingMode::Single;
	float detFps = 0, inferMs = 0, latencyMs = 0;
	uint64_t dropped = 0;
	std::string provider;
	WorkerStatus ws = WorkerStatus::Stopped;
	double now = 0;
};

void drawOverlay(AutoFrameFilter *f, const OverlaySnapshot &s, const RectF &crop, uint32_t outW, uint32_t outH)
{
	ObsRenderer &r = f->renderer;
	if (crop.empty())
		return;
	const float sx = (float)outW / crop.w, sy = (float)outH / crop.h;
	auto map = [&](const RectF &b) { return RectF{(b.x - crop.x) * sx, (b.y - crop.y) * sy, b.w * sx, b.h * sy}; };
	const float th = std::max(2.0f, std::round((float)outH / 360.0f));
	const float px = std::max(2.0f, std::round((float)outH / 360.0f));

	r.beginOverlay();
	if (s.guides) {
		{
			const RectF st = map(s.stage);
			r.strokeRect(st.x, st.y, st.w, st.h, std::max(1.0f, th * 0.5f), 0xC0FFFFFF);
			r.drawText("STAGE AREA", st.x + 2 * px, st.bottom() - 9 * px, px, 0xC0FFFFFF);
		}
		if (!s.pulpit.empty()) {
			const RectF p = map(s.pulpit);
			r.fillRect(p.x, p.y, p.w, p.h, 0x2A1E90FF);
			r.strokeRect(p.x, p.y, p.w, p.h, th, 0xFF1E90FF);
			r.drawText("PULPIT", p.x + 2 * px, p.y + 2 * px, px, 0xFF1E90FF);
		}
		const RectF d = map(s.deadZone);
		r.strokeRect(d.x, d.y, d.w, d.h, std::max(1.0f, th * 0.5f), 0xC0FF40FF);
	}
	if (s.boxes) {
		for (const Track &t : s.tracks) {
			if (t.state == TrackState::Tentative)
				continue;
			const bool present = f->framing.isPresent(t, s.now);
			const bool target = s.mode == TrackingMode::Single && t.id == s.out.targetId;
			const bool member = std::find(s.out.groupIds.begin(), s.out.groupIds.end(), t.id) !=
					    s.out.groupIds.end();
			const uint32_t col = !present ? 0xFF8A8A8A
					     : target ? 0xFF00E676
					     : member ? 0xFF00E5FF
						      : 0xFFFFC400;
			const RectF b = map(t.box);
			r.strokeRect(b.x, b.y, b.w, b.h, target ? th * 1.5f : th, col);
			if (s.ids) {
				char label[48];
				snprintf(label, sizeof(label), "#%d %d%%%s", t.id, (int)std::lround(t.score * 100.0f),
					 target ? " LOCK" : "");
				const float tw = ObsRenderer::textWidth(label, px);
				const float ly = b.y > 10 * px ? b.y - 10 * px : b.y + th;
				r.fillRect(b.x, ly, tw + 4 * px, 9 * px, 0xB0000000);
				r.drawText(label, b.x + 2 * px, ly + 1 * px, px, col);
			}
		}
	}
	if (s.stats) {
		char lines[6][96];
		int n = 0;
		snprintf(lines[n++], 96, "V MEDIA AI AUTOFRAME");
		if (s.mode == TrackingMode::Group)
			snprintf(lines[n++], 96, "%s  GROUP (%d)", statusName(s.out.status), (int)s.out.groupIds.size());
		else if (s.out.targetId >= 0)
			snprintf(lines[n++], 96, "%s  PERSON #%d", statusName(s.out.status), s.out.targetId);
		else
			snprintf(lines[n++], 96, "%s", statusName(s.out.status));
		snprintf(lines[n++], 96, "ZOOM %.2fX%s", s.out.zoom, s.out.inPulpitZone ? "  AT PULPIT" : "");
		if (s.ws == WorkerStatus::Ready) {
			snprintf(lines[n++], 96, "AI %s  %.1f DET/S  %.1f MS",
				 s.provider.find("GPU") != std::string::npos ? "GPU" : "CPU", s.detFps, s.inferMs);
			snprintf(lines[n++], 96, "LATENCY %.0f MS  DROPPED %llu", s.latencyMs,
				 (unsigned long long)s.dropped);
		} else if (s.ws == WorkerStatus::Failed) {
			snprintf(lines[n++], 96, "AI ERROR - SEE OBS LOG");
		} else {
			snprintf(lines[n++], 96, "AI LOADING...");
		}
		float wmax = 0;
		for (int i = 0; i < n; ++i)
			wmax = std::max(wmax, ObsRenderer::textWidth(lines[i], px));
		const float x0 = 6 * px, y0 = 6 * px, lh = 10 * px;
		r.fillRect(x0 - 3 * px, y0 - 3 * px, wmax + 6 * px, lh * n + 4 * px, 0xB0000000);
		for (int i = 0; i < n; ++i)
			r.drawText(lines[i], x0, y0 + lh * i, px, i == 0 ? 0xFFFFC400 : 0xFFFFFFFF);
	}
	r.endOverlay();
}

} // namespace

static void af_render(void *data, gs_effect_t *)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	const uint64_t t0 = os_gettime_ns();
	obs_source_t *target = obs_filter_get_target(f->context);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (!target || !parent || !w || !h) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (!f->rendererInit) {
		f->rendererInit = true;
		std::string err;
		if (!f->renderer.init(err))
			LOG(LOG_WARNING, "'%s': %s - using whole-pixel fallback crop", obs_source_get_name(f->context),
			    err.c_str());
	}

	const enum gs_color_space preferred[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	const enum gs_color_space space = obs_source_get_color_space(target, OBS_COUNTOF(preferred), preferred);
	gs_texture_t *tex = f->renderer.renderTarget(f->context, w, h, space);
	if (!tex) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	const double now = monotonicSeconds();
	RectF crop;
	bool bicubic = true;
	float detFps = 5.0f;
	OutputAspect aspect = OutputAspect::Source;
	OutputSize osize = OutputSize::Auto;
	float sharpen = 0.0f;
	bool overlay = false;
	OverlaySnapshot snap;
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		crop = f->framing.output().crop;
		bicubic = f->bicubic;
		detFps = f->cfg.detectionFps;
		aspect = f->cfg.aspect;
		osize = f->cfg.outputSize;
		sharpen = f->cfg.sharpen;
		const AutoFrameConfig &c = f->cfg;
		overlay = c.showBoxes || c.showStats || c.showGuides;
		if (overlay) {
			snap.boxes = c.showBoxes;
			snap.ids = c.showIds;
			snap.stats = c.showStats;
			snap.guides = c.showGuides;
			snap.tracks = f->tracker.tracks();
			snap.out = f->framing.output();
			snap.deadZone = f->framing.deadZoneRect();
			snap.pulpit = f->framing.pulpitRect();
			snap.stage = f->framing.stageRect();
			snap.mode = f->framing.mode();
			snap.detFps = f->perf.detectionFps();
			snap.inferMs = f->perf.inferenceMs();
			snap.latencyMs = f->perf.latencyMs();
			snap.now = now;
		}
	}
	if (crop.empty() || crop.w > (float)w + 0.5f || crop.h > (float)h + 0.5f)
		crop = RectF{0.0f, 0.0f, (float)w, (float)h}; // before the first tick / after a resize

	// ---- AI frame pipeline (never blocks: capture, then map two frames later)
	if (f->renderer.captureInFlight()) {
		if (f->renderer.stagedFrameReady()) {
			FramePacket pkt = f->worker.takeSpare();
			float ms = 0.0f;
			if (f->renderer.collectDetectionFrame(pkt, ms)) {
				f->worker.submit(std::move(pkt));
				std::lock_guard<std::mutex> lock(f->mutex);
				f->perf.addReadback(ms);
			}
		}
	} else if (f->worker.ready() && f->worker.idle() && now - f->lastCapture >= 1.0 / std::max(0.5f, detFps)) {
		f->renderer.captureForDetection(tex, w, h, f->worker.inputWidth(), f->worker.inputHeight(), now,
						++f->frameCounter);
		f->lastCapture = now;
	}

	uint32_t outW, outH;
	outputSize(aspect, osize, w, h, outW, outH);
	f->renderer.drawCrop(tex, crop, w, h, outW, outH, bicubic, sharpen);

	// Debug graphics must never reach a stream / recording / virtual camera. Those all come
	// from OBS's program texture: while OBS renders it, the current render target is a texture.
	// The Filters window, Studio Mode preview and source projectors draw straight into a window
	// (render target = the window's swap chain -> gs_get_render_target() returns NULL), and a
	// window is never encoded. So the overlay is drawn only into windows - never into the
	// program texture, so not even the first queued frames of a recording can contain it.
	if (overlay)
		overlay = overlayAllowed(gs_get_render_target() == nullptr);

	if (overlay) {
		snap.ws = f->worker.status();
		snap.dropped = f->worker.framesDropped();
		if (snap.ws == WorkerStatus::Ready)
			snap.provider = f->worker.statusMessage();
		drawOverlay(f, snap, crop, outW, outH);
	}

	const float ms = (float)((os_gettime_ns() - t0) / 1.0e6);
	std::lock_guard<std::mutex> lock(f->mutex);
	f->perf.addRender(ms);
}

static uint32_t af_width(void *data)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	obs_source_t *target = obs_filter_get_target(f->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	OutputAspect a;
	OutputSize sz;
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		a = f->cfg.aspect;
		sz = f->cfg.outputSize;
	}
	uint32_t ow, oh;
	outputSize(a, sz, w, h, ow, oh);
	return ow;
}

static uint32_t af_height(void *data)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	obs_source_t *target = obs_filter_get_target(f->context);
	const uint32_t w = target ? obs_source_get_base_width(target) : 0;
	const uint32_t h = target ? obs_source_get_base_height(target) : 0;
	OutputAspect a;
	OutputSize sz;
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		a = f->cfg.aspect;
		sz = f->cfg.outputSize;
	}
	uint32_t ow, oh;
	outputSize(a, sz, w, h, ow, oh);
	return oh;
}

static enum gs_color_space af_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces)
{
	auto *f = static_cast<AutoFrameFilter *>(data);
	const enum gs_color_space potential[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target)
		return count ? preferred_spaces[0] : GS_CS_SRGB;
	return obs_source_get_color_space(target, OBS_COUNTOF(potential), potential);
}

void registerAutoFrameFilter()
{
	struct obs_source_info info = {};
	info.id = "vmedia_ai_autoframe_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = af_get_name;
	info.create = af_create;
	info.destroy = af_destroy;
	info.update = af_update;
	info.get_defaults = af_defaults;
	info.get_properties = af_properties;
	info.video_tick = af_tick;
	info.video_render = af_render;
	info.get_width = af_width;
	info.get_height = af_height;
	info.video_get_color_space = af_color_space;
	obs_register_source(&info);
}

} // namespace vmaf
