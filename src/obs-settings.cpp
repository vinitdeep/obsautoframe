// V Media AI AutoFrame - OBS settings binding and the properties (settings) UI.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "obs-settings.hpp"
#include "autoframe-filter.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#define T(x) obs_module_text(x)

namespace vmaf {

namespace key {
// General
constexpr const char *AutoTracking = "auto_tracking";
constexpr const char *TrackingMode = "tracking_mode";
constexpr const char *PerfMode = "performance_mode";
constexpr const char *AppliedPerf = "applied_performance_mode";
// Subject
constexpr const char *Policy = "target_policy";
constexpr const char *ManualPerson = "manual_person";
constexpr const char *LockSubject = "lock_subject";
constexpr const char *Confidence = "confidence";
constexpr const char *StageLeft = "stage_left";
constexpr const char *StageRight = "stage_right";
constexpr const char *StageTop = "stage_top";
constexpr const char *StageBottom = "stage_bottom";
constexpr const char *IgnoreBottom = "ignore_bottom_edge";
// Framing
constexpr const char *Preset = "framing_preset";
constexpr const char *AppliedPreset = "applied_framing_preset";
constexpr const char *BodyFraction = "body_fraction";
constexpr const char *SubjectFill = "subject_fill";
constexpr const char *Headroom = "headroom";
constexpr const char *OffsetX = "offset_x";
constexpr const char *OffsetY = "offset_y";
constexpr const char *SafeMargin = "safe_margin";
constexpr const char *Aspect = "output_aspect";
constexpr const char *Scaling = "scaling";
constexpr const char *OutputSizeK = "output_size";
constexpr const char *Sharpen = "sharpen";
// Zoom
constexpr const char *AutoZoom = "auto_zoom";
constexpr const char *MinZoom = "min_zoom";
constexpr const char *MaxZoom = "max_zoom";
constexpr const char *FixedZoom = "fixed_zoom";
constexpr const char *ZoomSpeed = "zoom_speed";
constexpr const char *ZoomSmoothing = "zoom_smoothing";
constexpr const char *ZoomDeadband = "zoom_deadband";
constexpr const char *MaxEnlarge = "max_enlarge";
// Movement
constexpr const char *TrackingSpeed = "tracking_speed";
constexpr const char *Smoothing = "smoothing";
constexpr const char *DeadZoneX = "dead_zone_x";
constexpr const char *DeadZoneY = "dead_zone_y";
constexpr const char *AccelLimit = "accel_limit";
// Preacher
constexpr const char *PulpitEnabled = "pulpit_enabled";
constexpr const char *PulpitX = "pulpit_x";
constexpr const char *PulpitWidth = "pulpit_width";
constexpr const char *ReturnToPulpit = "return_to_pulpit";
// Group
constexpr const char *MaxSubjects = "max_subjects";
constexpr const char *GroupMinSize = "group_min_size";
// Recovery
constexpr const char *LostTimeout = "lost_timeout";
constexpr const char *HoldPosition = "hold_position";
constexpr const char *ReturnToWide = "return_to_wide";
constexpr const char *RecoverySpeed = "recovery_speed";
constexpr const char *ReacquireRadius = "reacquire_radius";
// Performance
constexpr const char *Model = "model";
constexpr const char *CustomModel = "custom_model_path";
constexpr const char *DetectionFps = "detection_fps";
constexpr const char *Threads = "threads";
constexpr const char *UseGpu = "use_gpu";
// Debug
constexpr const char *ShowBoxes = "show_boxes";
constexpr const char *ShowIds = "show_ids";
constexpr const char *ShowStats = "show_stats";
constexpr const char *ShowGuides = "show_guides";
constexpr const char *DiagLogs = "diagnostic_logs";
} // namespace key

// ------------------------------------------------------------------ defaults / read

static void writePresetValues(obs_data_t *s, const PresetDefaults &d, bool asDefault)
{
	auto setD = [&](const char *k, double v) {
		if (asDefault)
			obs_data_set_default_double(s, k, v);
		else
			obs_data_set_double(s, k, v);
	};
	auto setB = [&](const char *k, bool v) {
		if (asDefault)
			obs_data_set_default_bool(s, k, v);
		else
			obs_data_set_bool(s, k, v);
	};
	auto setI = [&](const char *k, long long v) {
		if (asDefault)
			obs_data_set_default_int(s, k, v);
		else
			obs_data_set_int(s, k, v);
	};
	setD(key::BodyFraction, d.composition.bodyFraction * 100.0);
	setD(key::SubjectFill, d.composition.subjectFill * 100.0);
	setD(key::Headroom, d.composition.headroom * 100.0);
	setD(key::Smoothing, d.smoothing);
	setD(key::TrackingSpeed, d.trackingSpeed * 100.0);
	setD(key::DeadZoneX, d.deadZoneX * 100.0);
	setD(key::DeadZoneY, d.deadZoneY * 100.0);
	setD(key::MaxZoom, d.maxZoom);
	setI(key::TrackingMode, (int)d.mode);
	setB(key::PulpitEnabled, d.pulpit);
}

void writeDefaults(obs_data_t *s)
{
	AutoFrameConfig c;
	applyPreset(c, FramingPreset::Medium);
	applyPerformanceMode(c, PerformanceMode::Balanced);

	obs_data_set_default_bool(s, key::AutoTracking, c.autoTracking);
	obs_data_set_default_int(s, key::PerfMode, (int)c.performanceMode);
	obs_data_set_default_int(s, key::AppliedPerf, (int)c.performanceMode);

	obs_data_set_default_int(s, key::Policy, (int)c.policy);
	obs_data_set_default_int(s, key::ManualPerson, -1);
	obs_data_set_default_bool(s, key::LockSubject, c.lockSubject);
	obs_data_set_default_double(s, key::Confidence, c.confidence * 100.0);
	obs_data_set_default_double(s, key::StageLeft, c.stageLeft * 100.0);
	obs_data_set_default_double(s, key::StageRight, c.stageRight * 100.0);
	obs_data_set_default_double(s, key::StageTop, c.stageTop * 100.0);
	obs_data_set_default_double(s, key::StageBottom, c.stageBottom * 100.0);
	obs_data_set_default_bool(s, key::IgnoreBottom, c.ignoreBottomEdge);

	obs_data_set_default_int(s, key::Preset, (int)c.preset);
	obs_data_set_default_int(s, key::AppliedPreset, (int)c.preset);
	writePresetValues(s, presetDefaults(c.preset), true);
	obs_data_set_default_double(s, key::OffsetX, c.offsetX * 100.0);
	obs_data_set_default_double(s, key::OffsetY, c.offsetY * 100.0);
	obs_data_set_default_double(s, key::SafeMargin, c.safeMargin * 100.0);
	obs_data_set_default_int(s, key::Aspect, (int)c.aspect);
	obs_data_set_default_int(s, key::Scaling, 1);
	obs_data_set_default_int(s, key::OutputSizeK, (int)c.outputSize);
	obs_data_set_default_double(s, key::Sharpen, c.sharpen * 100.0);

	obs_data_set_default_bool(s, key::AutoZoom, c.autoZoom);
	obs_data_set_default_double(s, key::MinZoom, c.minZoom);
	obs_data_set_default_double(s, key::FixedZoom, c.fixedZoom);
	obs_data_set_default_double(s, key::ZoomSpeed, c.zoomSpeed * 100.0);
	obs_data_set_default_double(s, key::ZoomSmoothing, c.zoomSmoothing);
	obs_data_set_default_double(s, key::ZoomDeadband, c.zoomDeadband * 100.0);
	obs_data_set_default_double(s, key::MaxEnlarge, c.maxEnlarge);

	obs_data_set_default_double(s, key::AccelLimit, c.accelLimit * 100.0);

	obs_data_set_default_double(s, key::PulpitX, c.pulpitX * 100.0);
	obs_data_set_default_double(s, key::PulpitWidth, c.pulpitWidth * 100.0);
	obs_data_set_default_bool(s, key::ReturnToPulpit, c.returnToPulpit);

	obs_data_set_default_int(s, key::MaxSubjects, c.maxSubjects);
	obs_data_set_default_double(s, key::GroupMinSize, c.groupMinRelSize * 100.0);

	obs_data_set_default_double(s, key::LostTimeout, c.lostTimeout);
	obs_data_set_default_bool(s, key::HoldPosition, c.holdPosition);
	obs_data_set_default_bool(s, key::ReturnToWide, c.returnToWide);
	obs_data_set_default_double(s, key::RecoverySpeed, c.recoverySpeed);
	obs_data_set_default_double(s, key::ReacquireRadius, c.reacquireRadius * 100.0);

	obs_data_set_default_int(s, key::Model, (int)c.model);
	obs_data_set_default_string(s, key::CustomModel, "");
	obs_data_set_default_double(s, key::DetectionFps, c.detectionFps);
	obs_data_set_default_int(s, key::Threads, c.threads);
	obs_data_set_default_bool(s, key::UseGpu, c.useGpu);

	obs_data_set_default_bool(s, key::ShowBoxes, c.showBoxes);
	obs_data_set_default_bool(s, key::ShowIds, c.showIds);
	obs_data_set_default_bool(s, key::ShowStats, c.showStats);
	obs_data_set_default_bool(s, key::ShowGuides, c.showGuides);
	obs_data_set_default_bool(s, key::DiagLogs, c.diagnosticLogs);
}

static float pct(obs_data_t *s, const char *k, float lo, float hi)
{
	return std::clamp((float)obs_data_get_double(s, k), lo, hi) / 100.0f;
}

static float num(obs_data_t *s, const char *k, float lo, float hi)
{
	return std::clamp((float)obs_data_get_double(s, k), lo, hi);
}

template<typename E> static E enumv(obs_data_t *s, const char *k, int maxValue)
{
	return (E)std::clamp((int)obs_data_get_int(s, k), 0, maxValue);
}

AutoFrameConfig readConfig(obs_data_t *s, bool &bicubic)
{
	AutoFrameConfig c;
	c.autoTracking = obs_data_get_bool(s, key::AutoTracking);
	c.trackingMode = enumv<TrackingMode>(s, key::TrackingMode, 1);
	c.performanceMode = enumv<PerformanceMode>(s, key::PerfMode, 3);

	c.policy = enumv<TargetPolicy>(s, key::Policy, 3);
	c.lockSubject = obs_data_get_bool(s, key::LockSubject);
	c.confidence = pct(s, key::Confidence, 5.0f, 95.0f);
	c.stageLeft = pct(s, key::StageLeft, 0.0f, 100.0f);
	c.stageRight = pct(s, key::StageRight, 0.0f, 100.0f);
	c.stageTop = pct(s, key::StageTop, 0.0f, 100.0f);
	c.stageBottom = pct(s, key::StageBottom, 0.0f, 100.0f);
	c.ignoreBottomEdge = obs_data_get_bool(s, key::IgnoreBottom);

	c.preset = enumv<FramingPreset>(s, key::Preset, (int)FramingPreset::Custom);
	c.composition.bodyFraction = pct(s, key::BodyFraction, 5.0f, 100.0f);
	c.composition.subjectFill = pct(s, key::SubjectFill, 20.0f, 100.0f);
	c.composition.headroom = pct(s, key::Headroom, 0.0f, 40.0f);
	c.offsetX = pct(s, key::OffsetX, -40.0f, 40.0f);
	c.offsetY = pct(s, key::OffsetY, -40.0f, 40.0f);
	c.safeMargin = pct(s, key::SafeMargin, 0.0f, 50.0f);
	c.aspect = enumv<OutputAspect>(s, key::Aspect, 5);
	bicubic = obs_data_get_int(s, key::Scaling) != 0;
	c.outputSize = enumv<OutputSize>(s, key::OutputSizeK, (int)OutputSize::P2160);
	c.sharpen = pct(s, key::Sharpen, 0.0f, 100.0f);

	c.autoZoom = obs_data_get_bool(s, key::AutoZoom);
	c.minZoom = num(s, key::MinZoom, 1.0f, 4.0f);
	c.maxZoom = std::max(c.minZoom, num(s, key::MaxZoom, 1.0f, 4.0f));
	c.fixedZoom = num(s, key::FixedZoom, 1.0f, 4.0f);
	c.zoomSpeed = pct(s, key::ZoomSpeed, 2.0f, 200.0f);
	c.zoomSmoothing = num(s, key::ZoomSmoothing, 0.05f, 5.0f);
	c.zoomDeadband = pct(s, key::ZoomDeadband, 0.0f, 50.0f);
	c.maxEnlarge = num(s, key::MaxEnlarge, 1.0f, 8.0f);

	c.trackingSpeed = pct(s, key::TrackingSpeed, 2.0f, 300.0f);
	c.smoothing = num(s, key::Smoothing, 0.05f, 5.0f);
	c.deadZoneX = pct(s, key::DeadZoneX, 0.0f, 80.0f);
	c.deadZoneY = pct(s, key::DeadZoneY, 0.0f, 80.0f);
	c.accelLimit = pct(s, key::AccelLimit, 0.0f, 1000.0f);

	c.pulpitEnabled = obs_data_get_bool(s, key::PulpitEnabled);
	c.pulpitX = pct(s, key::PulpitX, 0.0f, 100.0f);
	c.pulpitWidth = pct(s, key::PulpitWidth, 2.0f, 100.0f);
	c.returnToPulpit = obs_data_get_bool(s, key::ReturnToPulpit);

	c.maxSubjects = std::clamp((int)obs_data_get_int(s, key::MaxSubjects), 1, 20);
	c.groupMinRelSize = pct(s, key::GroupMinSize, 0.0f, 100.0f);

	c.lostTimeout = num(s, key::LostTimeout, 0.0f, 30.0f);
	c.holdPosition = obs_data_get_bool(s, key::HoldPosition);
	c.returnToWide = obs_data_get_bool(s, key::ReturnToWide);
	c.recoverySpeed = num(s, key::RecoverySpeed, 0.1f, 10.0f);
	c.reacquireRadius = pct(s, key::ReacquireRadius, 1.0f, 100.0f);

	c.model = enumv<DetectorModel>(s, key::Model, 2);
	const char *custom = obs_data_get_string(s, key::CustomModel);
	c.customModelPath = custom ? custom : "";
	c.detectionFps = num(s, key::DetectionFps, 0.5f, 30.0f);
	c.threads = std::clamp((int)obs_data_get_int(s, key::Threads), 1, 16);
	c.useGpu = obs_data_get_bool(s, key::UseGpu);

	c.showBoxes = obs_data_get_bool(s, key::ShowBoxes);
	c.showIds = obs_data_get_bool(s, key::ShowIds);
	c.showStats = obs_data_get_bool(s, key::ShowStats);
	c.showGuides = obs_data_get_bool(s, key::ShowGuides);
	c.diagnosticLogs = obs_data_get_bool(s, key::DiagLogs);
	return c;
}

int manualPersonSetting(obs_data_t *s)
{
	return (int)obs_data_get_int(s, key::ManualPerson);
}

// ------------------------------------------------------------------ properties UI

static std::string statusText(AutoFrameFilter *f, bool &failed)
{
	failed = false;
	if (!f)
		return T("Status.Unknown");
	const WorkerStatus ws = f->worker.status();
	std::string msg = f->worker.statusMessage();
	failed = ws == WorkerStatus::Failed;
	std::string text;
	switch (ws) {
	case WorkerStatus::Loading:
		text = T("Status.Loading");
		break;
	case WorkerStatus::Ready:
		text = msg;
		break;
	case WorkerStatus::Failed:
		text = std::string(T("Status.Failed")) + " " + msg;
		break;
	case WorkerStatus::Stopped:
		text = T("Status.Stopped");
		break;
	}
	if (ws == WorkerStatus::Ready) {
		std::lock_guard<std::mutex> lock(f->mutex);
		char buf[256];
		snprintf(buf, sizeof(buf), "\n%s | %.1f %s | %.1f ms | %s %.2fx", statusName(f->framing.output().status),
			 f->perf.detectionFps(), T("Status.DetPerSec"), f->perf.inferenceMs(), T("Status.Zoom"),
			 f->framing.output().zoom);
		text += buf;
	}
	if (ws != WorkerStatus::Stopped) {
		// output size + how much the camera pixels are enlarged (quality hint for 9:16 etc.)
		std::lock_guard<std::mutex> lock(f->mutex);
		const float W = f->framing.sourceWidth(), H = f->framing.sourceHeight();
		if (W > 0 && H > 0) {
			uint32_t ow = 0, oh = 0;
			outputDimensions(f->cfg.aspect, f->cfg.outputSize, (uint32_t)W, (uint32_t)H, ow, oh);
			const float effMax = ZoomController::effectiveMaxZoom(f->cfg, W, H);
			const float upMin = ZoomController::enlargement(f->cfg, W, H, std::max(1.0f, f->cfg.minZoom));
			const float upMax = ZoomController::enlargement(f->cfg, W, H, effMax);
			char buf[400];
			snprintf(buf, sizeof(buf), "\n%s %ux%u %s %.0fx%.0f | %s %.2fx - %.2fx | %s %.2fx%s%s",
				 T("Status.Output"), ow, oh, T("Status.Camera"), W, H, T("Status.Upscale"), upMin, upMax,
				 T("Status.MaxZoom"), effMax, effMax + 0.01f < f->cfg.maxZoom ? T("Status.Guarded") : "",
				 upMax > 1.6f ? T("Status.Soft") : "");
			text += buf;
		}
	}
	return text;
}

static void fillPeopleList(AutoFrameFilter *f, obs_property_t *list)
{
	obs_property_list_clear(list);
	obs_property_list_add_int(list, T("Person.Automatic"), -1);
	if (!f)
		return;
	std::lock_guard<std::mutex> lock(f->mutex);
	const double now = monotonicSeconds();
	std::vector<const Track *> people;
	for (const Track &t : f->tracker.tracks())
		if (f->framing.isPresent(t, now))
			people.push_back(&t);
	std::sort(people.begin(), people.end(), [](const Track *a, const Track *b) { return a->box.cx() < b->box.cx(); });
	const float W = std::max(1.0f, f->framing.sourceWidth());
	for (const Track *t : people) {
		const float x = t->box.cx() / W;
		const char *where = x < 0.36f ? T("Person.Left") : (x > 0.64f ? T("Person.Right") : T("Person.Center"));
		char buf[128];
		snprintf(buf, sizeof(buf), "%s #%d  (%s, %.0f%%)%s", T("Person.Person"), t->id, where, t->score * 100.0f,
			 t->id == f->framing.output().targetId ? "  *" : "");
		obs_property_list_add_int(list, buf, t->id);
	}
}

static bool presetModified(obs_properties_t *, obs_property_t *, obs_data_t *s)
{
	const int p = (int)obs_data_get_int(s, key::Preset);
	const int applied = (int)obs_data_get_int(s, key::AppliedPreset);
	if (p == applied)
		return false;
	if (p >= 0 && p < (int)FramingPreset::Custom)
		writePresetValues(s, presetDefaults((FramingPreset)p), false);
	obs_data_set_int(s, key::AppliedPreset, p);
	return true;
}

static bool perfModified(obs_properties_t *, obs_property_t *, obs_data_t *s)
{
	const int p = (int)obs_data_get_int(s, key::PerfMode);
	const int applied = (int)obs_data_get_int(s, key::AppliedPerf);
	if (p == applied)
		return false;
	if (p >= 0 && p < (int)PerformanceMode::Custom) {
		AutoFrameConfig c;
		applyPerformanceMode(c, (PerformanceMode)p);
		obs_data_set_double(s, key::DetectionFps, c.detectionFps);
		obs_data_set_int(s, key::Threads, c.threads);
	}
	obs_data_set_int(s, key::AppliedPerf, p);
	return true;
}

static bool autoZoomModified(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const bool az = obs_data_get_bool(s, key::AutoZoom);
	obs_property_set_visible(obs_properties_get(props, key::FixedZoom), !az);
	return true;
}

static bool modelModified(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
	const bool custom = obs_data_get_int(s, key::Model) == (int)DetectorModel::Custom;
	obs_property_set_visible(obs_properties_get(props, key::CustomModel), custom);
	return true;
}

#define CMD_BUTTON(fn, cmd)                                                \
	static bool fn(obs_properties_t *, obs_property_t *, void *data)   \
	{                                                                  \
		if (data)                                                  \
			queueCommand((AutoFrameFilter *)data, Command::cmd); \
		return false;                                              \
	}
CMD_BUTTON(btnPause, TogglePause)
CMD_BUTTON(btnWide, ForceWide)
CMD_BUTTON(btnLock, LockPosition)
CMD_BUTTON(btnReset, ResetFraming)
CMD_BUTTON(btnZoomIn, ZoomIn)
CMD_BUTTON(btnZoomOut, ZoomOut)
CMD_BUTTON(btnZoomReset, ResetZoom)
CMD_BUTTON(btnNext, NextPerson)
CMD_BUTTON(btnPrev, PrevPerson)
CMD_BUTTON(btnGroup, ToggleGroup)
#undef CMD_BUTTON

static bool btnRefreshStatus(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *f = (AutoFrameFilter *)data;
	bool failed = false;
	obs_property_t *st = obs_properties_get(props, "status_info");
	if (st) {
		obs_property_set_description(st, statusText(f, failed).c_str());
		obs_property_text_set_info_type(st, failed ? OBS_TEXT_INFO_ERROR : OBS_TEXT_INFO_NORMAL);
	}
	obs_property_t *list = obs_properties_get(props, key::ManualPerson);
	if (list)
		fillPeopleList(f, list);
	return true;
}

static bool btnSetPulpit(obs_properties_t *, obs_property_t *, void *data)
{
	auto *f = (AutoFrameFilter *)data;
	if (!f)
		return false;
	float x = -1.0f;
	{
		std::lock_guard<std::mutex> lock(f->mutex);
		const FramingOutput &o = f->framing.output();
		if (o.hasSubject && f->framing.sourceWidth() > 0.0f)
			x = o.subject.cx() / f->framing.sourceWidth();
	}
	if (x < 0.0f)
		return false; // nobody tracked right now
	obs_data_t *s = obs_source_get_settings(f->context);
	obs_data_set_double(s, key::PulpitX, x * 100.0);
	obs_data_set_bool(s, key::PulpitEnabled, true);
	obs_source_update(f->context, nullptr);
	obs_data_release(s);
	return true;
}

static obs_properties_t *group(obs_properties_t *root, const char *name, const char *title)
{
	obs_properties_t *g = obs_properties_create();
	obs_properties_add_group(root, name, title, OBS_GROUP_NORMAL, g);
	return g;
}

static obs_property_t *pctSlider(obs_properties_t *p, const char *k, const char *desc, double lo, double hi,
				 double step = 1.0)
{
	obs_property_t *s = obs_properties_add_float_slider(p, k, desc, lo, hi, step);
	obs_property_float_set_suffix(s, " %");
	return s;
}

static obs_property_t *secSlider(obs_properties_t *p, const char *k, const char *desc, double lo, double hi,
				 double step = 0.05)
{
	obs_property_t *s = obs_properties_add_float_slider(p, k, desc, lo, hi, step);
	obs_property_float_set_suffix(s, " s");
	return s;
}

static obs_property_t *zoomSlider(obs_properties_t *p, const char *k, const char *desc)
{
	obs_property_t *s = obs_properties_add_float_slider(p, k, desc, 1.0, 4.0, 0.05);
	obs_property_float_set_suffix(s, "x");
	return s;
}

obs_properties_t *buildProperties(AutoFrameFilter *f)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	// ---------------- General
	obs_properties_t *g = group(props, "grp_general", T("Group.General"));
	bool failed = false;
	p = obs_properties_add_text(g, "status_info", statusText(f, failed).c_str(), OBS_TEXT_INFO);
	obs_property_text_set_info_type(p, failed ? OBS_TEXT_INFO_ERROR : OBS_TEXT_INFO_NORMAL);
	obs_properties_add_button2(g, "btn_refresh", T("Button.Refresh"), btnRefreshStatus, f);
	obs_properties_add_bool(g, key::AutoTracking, T("Prop.AutoTracking"));
	p = obs_properties_add_list(g, key::TrackingMode, T("Prop.TrackingMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("TrackingMode.Single"), (int)TrackingMode::Single);
	obs_property_list_add_int(p, T("TrackingMode.Group"), (int)TrackingMode::Group);
	p = obs_properties_add_list(g, key::PerfMode, T("Prop.PerfMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("PerfMode.Low"), (int)PerformanceMode::LowCPU);
	obs_property_list_add_int(p, T("PerfMode.Balanced"), (int)PerformanceMode::Balanced);
	obs_property_list_add_int(p, T("PerfMode.Performance"), (int)PerformanceMode::Performance);
	obs_property_list_add_int(p, T("PerfMode.Custom"), (int)PerformanceMode::Custom);
	obs_property_set_modified_callback(p, perfModified);
	p = obs_properties_add_list(g, key::Preset, T("Prop.Preset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i <= (int)FramingPreset::Custom; ++i) {
		char k[32];
		snprintf(k, sizeof(k), "Preset.%d", i);
		obs_property_list_add_int(p, T(k), i);
	}
	obs_property_set_long_description(p, T("Prop.Preset.Desc"));
	obs_property_set_modified_callback(p, presetModified);

	// ---------------- Subject tracking
	g = group(props, "grp_subject", T("Group.Subject"));
	p = obs_properties_add_list(g, key::Policy, T("Prop.Policy"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Policy.Largest"), (int)TargetPolicy::Largest);
	obs_property_list_add_int(p, T("Policy.Centered"), (int)TargetPolicy::Centered);
	obs_property_list_add_int(p, T("Policy.Confident"), (int)TargetPolicy::Confident);
	obs_property_list_add_int(p, T("Policy.Manual"), (int)TargetPolicy::Manual);
	p = obs_properties_add_list(g, key::ManualPerson, T("Prop.ManualPerson"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_long_description(p, T("Prop.ManualPerson.Desc"));
	fillPeopleList(f, p);
	obs_properties_add_button2(g, "btn_people", T("Button.RefreshPeople"), btnRefreshStatus, f);
	obs_properties_add_button2(g, "btn_prev", T("Button.PrevPerson"), btnPrev, f);
	obs_properties_add_button2(g, "btn_next", T("Button.NextPerson"), btnNext, f);
	p = obs_properties_add_bool(g, key::LockSubject, T("Prop.LockSubject"));
	obs_property_set_long_description(p, T("Prop.LockSubject.Desc"));
	pctSlider(g, key::Confidence, T("Prop.Confidence"), 10, 90);
	p = obs_properties_add_text(g, "stage_info", T("Stage.Info"), OBS_TEXT_INFO);
	pctSlider(g, key::StageLeft, T("Prop.StageLeft"), 0, 100, 0.5);
	pctSlider(g, key::StageRight, T("Prop.StageRight"), 0, 100, 0.5);
	pctSlider(g, key::StageTop, T("Prop.StageTop"), 0, 100, 0.5);
	pctSlider(g, key::StageBottom, T("Prop.StageBottom"), 0, 100, 0.5);
	p = obs_properties_add_bool(g, key::IgnoreBottom, T("Prop.IgnoreBottom"));
	obs_property_set_long_description(p, T("Prop.IgnoreBottom.Desc"));

	// ---------------- Framing
	g = group(props, "grp_framing", T("Group.Framing"));
	p = pctSlider(g, key::BodyFraction, T("Prop.BodyFraction"), 10, 100);
	obs_property_set_long_description(p, T("Prop.BodyFraction.Desc"));
	pctSlider(g, key::SubjectFill, T("Prop.SubjectFill"), 30, 100);
	pctSlider(g, key::Headroom, T("Prop.Headroom"), 0, 35);
	pctSlider(g, key::OffsetX, T("Prop.OffsetX"), -35, 35);
	pctSlider(g, key::OffsetY, T("Prop.OffsetY"), -35, 35);
	pctSlider(g, key::SafeMargin, T("Prop.SafeMargin"), 0, 50);
	p = obs_properties_add_list(g, key::Aspect, T("Prop.Aspect"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Aspect.Source"), (int)OutputAspect::Source);
	obs_property_list_add_int(p, "16:9", (int)OutputAspect::A16x9);
	obs_property_list_add_int(p, T("Aspect.9x16"), (int)OutputAspect::A9x16);
	obs_property_list_add_int(p, "1:1", (int)OutputAspect::A1x1);
	obs_property_list_add_int(p, "4:5", (int)OutputAspect::A4x5);
	obs_property_list_add_int(p, "4:3", (int)OutputAspect::A4x3);
	p = obs_properties_add_list(g, key::Scaling, T("Prop.Scaling"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Scaling.Bilinear"), 0);
	obs_property_list_add_int(p, T("Scaling.Bicubic"), 1);
	p = obs_properties_add_list(g, key::OutputSizeK, T("Prop.OutputSize"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("OutputSize.Auto"), (int)OutputSize::Auto);
	obs_property_list_add_int(p, T("OutputSize.Native"), (int)OutputSize::Native);
	obs_property_list_add_int(p, "720p (720 short side)", (int)OutputSize::P720);
	obs_property_list_add_int(p, "1080p (1080 short side)", (int)OutputSize::P1080);
	obs_property_list_add_int(p, "1440p (1440 short side)", (int)OutputSize::P1440);
	obs_property_list_add_int(p, "4K (2160 short side)", (int)OutputSize::P2160);
	obs_property_set_long_description(p, T("Prop.OutputSize.Desc"));
	p = pctSlider(g, key::Sharpen, T("Prop.Sharpen"), 0, 100, 5);
	obs_property_set_long_description(p, T("Prop.Sharpen.Desc"));

	// ---------------- Zoom
	g = group(props, "grp_zoom", T("Group.Zoom"));
	p = obs_properties_add_bool(g, key::AutoZoom, T("Prop.AutoZoom"));
	obs_property_set_modified_callback(p, autoZoomModified);
	zoomSlider(g, key::MinZoom, T("Prop.MinZoom"));
	p = zoomSlider(g, key::MaxZoom, T("Prop.MaxZoom"));
	obs_property_set_long_description(p, T("Prop.MaxZoom.Desc"));
	zoomSlider(g, key::FixedZoom, T("Prop.FixedZoom"));
	p = pctSlider(g, key::ZoomSpeed, T("Prop.ZoomSpeed"), 5, 150);
	obs_property_float_set_suffix(p, " %/s");
	secSlider(g, key::ZoomSmoothing, T("Prop.ZoomSmoothing"), 0.1, 4.0);
	pctSlider(g, key::ZoomDeadband, T("Prop.ZoomDeadband"), 0, 40);
	p = obs_properties_add_float_slider(g, key::MaxEnlarge, T("Prop.MaxEnlarge"), 1.0, 8.0, 0.1);
	obs_property_float_set_suffix(p, "x");
	obs_property_set_long_description(p, T("Prop.MaxEnlarge.Desc"));

	// ---------------- Movement
	g = group(props, "grp_movement", T("Group.Movement"));
	p = pctSlider(g, key::TrackingSpeed, T("Prop.TrackingSpeed"), 5, 200);
	obs_property_float_set_suffix(p, " %/s");
	obs_property_set_long_description(p, T("Prop.TrackingSpeed.Desc"));
	secSlider(g, key::Smoothing, T("Prop.Smoothing"), 0.1, 4.0);
	p = pctSlider(g, key::DeadZoneX, T("Prop.DeadZoneX"), 0, 70);
	obs_property_set_long_description(p, T("Prop.DeadZone.Desc"));
	pctSlider(g, key::DeadZoneY, T("Prop.DeadZoneY"), 0, 70);
	p = pctSlider(g, key::AccelLimit, T("Prop.AccelLimit"), 0, 500, 5);
	obs_property_float_set_suffix(p, " %/s\xC2\xB2");

	// ---------------- Church preacher mode
	g = group(props, "grp_preacher", T("Group.Preacher"));
	p = obs_properties_add_bool(g, key::PulpitEnabled, T("Prop.PulpitEnabled"));
	obs_property_set_long_description(p, T("Prop.PulpitEnabled.Desc"));
	pctSlider(g, key::PulpitX, T("Prop.PulpitX"), 0, 100, 0.5);
	pctSlider(g, key::PulpitWidth, T("Prop.PulpitWidth"), 5, 80, 0.5);
	obs_properties_add_bool(g, key::ReturnToPulpit, T("Prop.ReturnToPulpit"));
	obs_properties_add_button2(g, "btn_pulpit", T("Button.SetPulpit"), btnSetPulpit, f);

	// ---------------- Group framing
	g = group(props, "grp_group", T("Group.Group"));
	obs_properties_add_int_slider(g, key::MaxSubjects, T("Prop.MaxSubjects"), 1, 12, 1);
	p = pctSlider(g, key::GroupMinSize, T("Prop.GroupMinSize"), 0, 90);
	obs_property_set_long_description(p, T("Prop.GroupMinSize.Desc"));
	obs_properties_add_button2(g, "btn_group", T("Button.ToggleGroup"), btnGroup, f);

	// ---------------- Recovery
	g = group(props, "grp_recovery", T("Group.Recovery"));
	secSlider(g, key::LostTimeout, T("Prop.LostTimeout"), 0.0, 15.0, 0.1);
	obs_properties_add_bool(g, key::HoldPosition, T("Prop.HoldPosition"));
	obs_properties_add_bool(g, key::ReturnToWide, T("Prop.ReturnToWide"));
	secSlider(g, key::RecoverySpeed, T("Prop.RecoverySpeed"), 0.2, 6.0, 0.1);
	pctSlider(g, key::ReacquireRadius, T("Prop.ReacquireRadius"), 5, 60);

	// ---------------- Manual control
	g = group(props, "grp_manual", T("Group.Manual"));
	obs_properties_add_text(g, "manual_info", T("Manual.Info"), OBS_TEXT_INFO);
	obs_properties_add_button2(g, "btn_pause", T("Button.Pause"), btnPause, f);
	obs_properties_add_button2(g, "btn_wide", T("Button.Wide"), btnWide, f);
	obs_properties_add_button2(g, "btn_lock", T("Button.Lock"), btnLock, f);
	obs_properties_add_button2(g, "btn_zoomin", T("Button.ZoomIn"), btnZoomIn, f);
	obs_properties_add_button2(g, "btn_zoomout", T("Button.ZoomOut"), btnZoomOut, f);
	obs_properties_add_button2(g, "btn_zoomreset", T("Button.ZoomReset"), btnZoomReset, f);
	obs_properties_add_button2(g, "btn_reset", T("Button.Reset"), btnReset, f);

	// ---------------- Performance
	g = group(props, "grp_perf", T("Group.Performance"));
	p = obs_properties_add_list(g, key::Model, T("Prop.Model"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Model.Nano"), (int)DetectorModel::YoloxNano);
	obs_property_list_add_int(p, T("Model.Tiny"), (int)DetectorModel::YoloxTiny);
	obs_property_list_add_int(p, T("Model.Custom"), (int)DetectorModel::Custom);
	obs_property_set_modified_callback(p, modelModified);
	obs_properties_add_path(g, key::CustomModel, T("Prop.CustomModel"), OBS_PATH_FILE, "ONNX model (*.onnx)", nullptr);
	p = obs_properties_add_float_slider(g, key::DetectionFps, T("Prop.DetectionFps"), 0.5, 30.0, 0.5);
	obs_property_float_set_suffix(p, " /s");
	obs_property_set_long_description(p, T("Prop.DetectionFps.Desc"));
	obs_properties_add_int_slider(g, key::Threads, T("Prop.Threads"), 1, 8, 1);
	p = obs_properties_add_bool(g, key::UseGpu, T("Prop.UseGpu"));
	obs_property_set_long_description(p, T("Prop.UseGpu.Desc"));

	// ---------------- Debug
	g = group(props, "grp_debug", T("Group.Debug"));
	obs_properties_add_bool(g, key::ShowBoxes, T("Prop.ShowBoxes"));
	obs_properties_add_bool(g, key::ShowIds, T("Prop.ShowIds"));
	obs_properties_add_bool(g, key::ShowStats, T("Prop.ShowStats"));
	obs_properties_add_bool(g, key::ShowGuides, T("Prop.ShowGuides"));
	obs_properties_add_bool(g, key::DiagLogs, T("Prop.DiagLogs"));
	p = obs_properties_add_text(g, "debug_info", T("Debug.Info"), OBS_TEXT_INFO);
	obs_property_text_set_info_type(p, OBS_TEXT_INFO_WARNING);

	obs_properties_add_text(props, "about_info", T("About"), OBS_TEXT_INFO);
	return props;
}

} // namespace vmaf
