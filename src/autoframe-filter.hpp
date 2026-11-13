// V Media AI AutoFrame - the OBS video filter.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
//
// Threads:
//   UI thread       : create / update / properties / button callbacks
//   hotkey thread   : hotkey callbacks (queue commands only)
//   graphics thread : video_tick (tracking + framing) and video_render (GPU)
//   AI worker       : model load + inference (DetectionWorker)
// `mutex` guards everything shared between them except the worker, which has its own.
#pragma once

#include "detection-worker.hpp"
#include "framing-controller.hpp"
#include "obs-renderer.hpp"
#include "performance-monitor.hpp"
#include "person-tracker.hpp"
#include "settings.hpp"

#include <obs-module.h>

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace vmaf {

enum class Command : int {
	TogglePause = 0,
	ForceWide,
	LockPosition,
	ResetFraming,
	ZoomIn,
	ZoomOut,
	ResetZoom,
	NextPerson,
	PrevPerson,
	ToggleGroup,
	SelectPerson, // arg = track id (-1 = automatic)
	Count_
};

struct AutoFrameFilter;

struct HotkeyBinding {
	AutoFrameFilter *filter = nullptr;
	Command cmd = Command::TogglePause;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

struct AutoFrameFilter {
	obs_source_t *context = nullptr;

	std::mutex mutex;
	AutoFrameConfig cfg;
	bool bicubic = true;
	FramingController framing;
	PersonTracker tracker;
	PerformanceMonitor perf;
	std::vector<std::pair<Command, int>> commands;
	int lastManualPerson = -2;

	DetectionWorker worker; // own locking
	std::string workerKey;  // model/threads/GPU combination the worker was started with
	WorkerStatus loggedStatus = WorkerStatus::Stopped;

	// graphics thread only
	ObsRenderer renderer;
	bool rendererInit = false;
	double lastCapture = -1.0;
	uint64_t frameCounter = 0;
	uint32_t srcW = 0, srcH = 0;
	double lastDiagLog = -1.0;

	std::array<HotkeyBinding, (size_t)Command::SelectPerson> hotkeys{};
};

void registerAutoFrameFilter();

// shared with obs-settings.cpp
void queueCommand(AutoFrameFilter *f, Command c, int arg = 0);
double monotonicSeconds();

} // namespace vmaf
