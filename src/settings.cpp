// V Media AI AutoFrame - presets.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "settings.hpp"

#include <algorithm>
#include <cmath>

namespace vmaf {

PresetDefaults presetDefaults(FramingPreset preset)
{
	//                         composition{body, fill, headroom}  smooth speed  dzX   dzY   maxZ  mode                  pulpit
	switch (preset) {
	case FramingPreset::Wide:
		return {{1.00f, 0.50f, 0.15f}, 0.9f, 0.35f, 0.30f, 0.35f, 1.4f, TrackingMode::Single, false};
	case FramingPreset::FullBody:
		return {{1.00f, 0.82f, 0.10f}, 0.6f, 0.45f, 0.20f, 0.25f, 1.4f, TrackingMode::Single, false};
	case FramingPreset::Medium:
		return {{0.55f, 0.88f, 0.10f}, 0.6f, 0.45f, 0.20f, 0.25f, 1.4f, TrackingMode::Single, false};
	case FramingPreset::UpperBody:
		return {{0.40f, 0.88f, 0.10f}, 0.6f, 0.45f, 0.20f, 0.25f, 1.6f, TrackingMode::Single, false};
	case FramingPreset::CloseUp:
		return {{0.25f, 0.85f, 0.10f}, 0.7f, 0.40f, 0.18f, 0.22f, 1.8f, TrackingMode::Single, false};
	case FramingPreset::Face:
		return {{0.16f, 0.80f, 0.12f}, 0.8f, 0.35f, 0.15f, 0.20f, 2.2f, TrackingMode::Single, false};
	case FramingPreset::Preacher:
		return {{0.60f, 0.85f, 0.10f}, 1.1f, 0.30f, 0.30f, 0.35f, 1.4f, TrackingMode::Single, true};
	case FramingPreset::WorshipLeader:
		// generous vertical dead zone: raised hands must not make the shot bob up and down
		return {{0.55f, 0.85f, 0.12f}, 0.8f, 0.35f, 0.25f, 0.40f, 1.4f, TrackingMode::Single, false};
	case FramingPreset::Interview:
		return {{0.40f, 0.88f, 0.10f}, 1.0f, 0.30f, 0.30f, 0.30f, 1.6f, TrackingMode::Single, false};
	case FramingPreset::Group:
		return {{1.00f, 0.80f, 0.10f}, 1.0f, 0.35f, 0.15f, 0.25f, 1.4f, TrackingMode::Group, false};
	case FramingPreset::Custom:
	case FramingPreset::Count:
		break;
	}
	return {{0.55f, 0.88f, 0.10f}, 0.6f, 0.45f, 0.20f, 0.25f, 1.4f, TrackingMode::Single, false};
}

void applyPreset(AutoFrameConfig &cfg, FramingPreset preset)
{
	cfg.preset = preset;
	if (preset == FramingPreset::Custom || preset == FramingPreset::Count)
		return;
	const PresetDefaults d = presetDefaults(preset);
	cfg.composition = d.composition;
	cfg.smoothing = d.smoothing;
	cfg.trackingSpeed = d.trackingSpeed;
	cfg.deadZoneX = d.deadZoneX;
	cfg.deadZoneY = d.deadZoneY;
	cfg.maxZoom = d.maxZoom;
	cfg.trackingMode = d.mode;
	cfg.pulpitEnabled = d.pulpit;
}

void applyPerformanceMode(AutoFrameConfig &cfg, PerformanceMode mode)
{
	cfg.performanceMode = mode;
	switch (mode) {
	case PerformanceMode::LowCPU:
		cfg.detectionFps = 2.5f;
		cfg.threads = 1;
		break;
	case PerformanceMode::Balanced:
		cfg.detectionFps = 5.0f;
		cfg.threads = 2;
		break;
	case PerformanceMode::Performance:
		cfg.detectionFps = 10.0f;
		cfg.threads = 4;
		break;
	case PerformanceMode::Custom:
		break;
	}
}

float aspectValue(OutputAspect aspect, float srcW, float srcH)
{
	switch (aspect) {
	case OutputAspect::A16x9:
		return 16.0f / 9.0f;
	case OutputAspect::A9x16:
		return 9.0f / 16.0f;
	case OutputAspect::A1x1:
		return 1.0f;
	case OutputAspect::A4x5:
		return 4.0f / 5.0f;
	case OutputAspect::A4x3:
		return 4.0f / 3.0f;
	case OutputAspect::Source:
		break;
	}
	return (srcW > 0.0f && srcH > 0.0f) ? srcW / srcH : 16.0f / 9.0f;
}

void outputDimensions(OutputAspect aspect, OutputSize size, uint32_t srcW, uint32_t srcH, uint32_t &outW,
		      uint32_t &outH)
{
	outW = srcW;
	outH = srcH;
	if (!srcW || !srcH)
		return;
	const double a = aspectValue(aspect, (float)srcW, (float)srcH);
	auto even = [](double v) { return std::max<uint32_t>(2, (uint32_t)std::lround(v / 2.0) * 2); };
	auto shortSide = [&](double s) {
		if (a >= 1.0) {
			outH = even(s);
			outW = even(s * a);
		} else {
			outW = even(s);
			outH = even(s / a);
		}
	};
	switch (size) {
	case OutputSize::Auto:
		if (aspect == OutputAspect::Source)
			return; // camera size, unchanged
		shortSide(1080.0);
		return;
	case OutputSize::Native: {
		if (aspect == OutputAspect::Source)
			return;
		// largest crop of this aspect that fits the camera image, 1:1 pixels
		const double h = std::min((double)srcH, (double)srcW / a);
		outW = even(h * a);
		outH = even(h);
		return;
	}
	case OutputSize::P720:
		shortSide(720.0);
		return;
	case OutputSize::P1080:
		shortSide(1080.0);
		return;
	case OutputSize::P1440:
		shortSide(1440.0);
		return;
	case OutputSize::P2160:
		shortSide(2160.0);
		return;
	}
}

const char *presetName(FramingPreset preset)
{
	switch (preset) {
	case FramingPreset::Wide:
		return "Wide Shot";
	case FramingPreset::FullBody:
		return "Full Body";
	case FramingPreset::Medium:
		return "Medium Shot";
	case FramingPreset::UpperBody:
		return "Upper Body";
	case FramingPreset::CloseUp:
		return "Close-Up";
	case FramingPreset::Face:
		return "Face Framing";
	case FramingPreset::Preacher:
		return "Church Preacher";
	case FramingPreset::WorshipLeader:
		return "Worship Leader";
	case FramingPreset::Interview:
		return "Interview";
	case FramingPreset::Group:
		return "Group";
	case FramingPreset::Custom:
	case FramingPreset::Count:
		break;
	}
	return "Custom";
}

} // namespace vmaf
