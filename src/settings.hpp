// V Media AI AutoFrame - plain configuration model (no OBS dependency, unit-testable).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include <cstdint>
#include <string>

namespace vmaf {

enum class TrackingMode : int { Single = 0, Group = 1 };

enum class TargetPolicy : int {
	Largest = 0,   // biggest person in frame (usually the closest / main speaker)
	Centered = 1,  // the person nearest to the frame center
	Confident = 2, // highest detector confidence
	Manual = 3,    // operator picks a numbered person
};

enum class FramingPreset : int {
	Wide = 0,
	FullBody,
	Medium,
	UpperBody,
	CloseUp,
	Face,
	Preacher,
	WorshipLeader,
	Interview,
	Group,
	Custom,
	Count
};

enum class PerformanceMode : int { LowCPU = 0, Balanced = 1, Performance = 2, Custom = 3 };

enum class OutputAspect : int { Source = 0, A16x9, A9x16, A1x1, A4x5, A4x3 };

enum class DetectorModel : int { YoloxNano = 0, YoloxTiny = 1, Custom = 2 };

/* Filter output resolution.
 *  Auto   : camera size for "same as camera"; otherwise 1080 on the short side
 *           (9:16 -> 1080x1920, 1:1 -> 1080x1080, 4:5 -> 1080x1350, 16:9 -> 1920x1080)
 *  Native : the crop's own pixels at zoom 1.0 (e.g. 608x1080 for 9:16 from a 1080p camera)
 *  P720..P2160 : that many pixels on the short side */
enum class OutputSize : int { Auto = 0, Native, P720, P1080, P1440, P2160 };

/* Debug graphics (boxes, IDs, stats, guides) may only be drawn when the render pass goes
 * straight into a WINDOW - the Filters dialog preview, Studio Mode's preview, a source
 * projector. Stream, recording, virtual camera, replay buffer and the normal main preview all
 * come from OBS's program TEXTURE, so they can never contain tracking data - not even the
 * first frames of a recording (frames already queued for the encoder were never drawn with
 * graphics). There is deliberately no option to draw into the program output: an "only while
 * not live" check cannot be made safe because OBS queues frames before an output starts. */
inline bool overlayAllowed(bool renderingToWindow)
{
	return renderingToWindow;
}

/* How a person box becomes a shot.
 *  bodyFraction : portion of the person (measured from the top of the head) that the shot
 *                 is built around. 1.0 = full body, 0.55 = waist up, 0.25 = head & shoulders.
 *  subjectFill  : fraction of the crop height that portion should occupy.
 *  headroom     : space above the head, as a fraction of the crop height. */
struct Composition {
	float bodyFraction = 0.6f;
	float subjectFill = 0.85f;
	float headroom = 0.10f;
};

struct AutoFrameConfig {
	// ---- General
	bool autoTracking = true; // false = hold the current framing (operator control)
	TrackingMode trackingMode = TrackingMode::Single;
	PerformanceMode performanceMode = PerformanceMode::Balanced;

	// ---- Subject tracking
	TargetPolicy policy = TargetPolicy::Largest;
	bool lockSubject = true;
	float confidence = 0.40f; // detector score threshold
	// Stage area (fractions of the source). Only people whose feet are inside it are picked
	// up; a locked subject is still followed if they walk out of it. Keeps the camera off the
	// congregation / audience in the foreground.
	float stageLeft = 0.0f, stageRight = 1.0f, stageTop = 0.0f, stageBottom = 1.0f;
	bool ignoreBottomEdge = false; // ignore people cut off by the bottom edge (front-row heads)

	// ---- Framing
	FramingPreset preset = FramingPreset::Medium;
	Composition composition{};
	float offsetX = 0.0f;    // where the subject sits horizontally, fraction of crop width (+ = right)
	float offsetY = 0.0f;    // extra vertical shift, fraction of crop height (+ = subject lower)
	float safeMargin = 0.06f; // minimum space kept beside the subject, fraction of the subject width
	OutputAspect aspect = OutputAspect::Source;
	OutputSize outputSize = OutputSize::Auto;
	float sharpen = 0.0f; // 0..1 unsharp amount applied with bicubic scaling

	// ---- Zoom
	bool autoZoom = true;
	float minZoom = 1.0f;
	float maxZoom = 1.4f;
	float fixedZoom = 1.2f;    // used when auto zoom is off
	float zoomSpeed = 0.35f;   // max zoom change, natural-log units per second (0.35 ~ 42%/s)
	float zoomSmoothing = 0.9f; // seconds (smooth-damp time)
	float zoomDeadband = 0.10f; // relative size change ignored before zooming
	// Detail guard: never enlarge camera pixels more than this (output px per camera px).
	// Caps the zoom automatically, e.g. 9:16 vertical from a 1080p camera is already 1.78x
	// at zoom 1.0, so it may only zoom to ~1.12x; a 4K camera keeps its full zoom range.
	float maxEnlarge = 2.0f;

	// ---- Movement
	float trackingSpeed = 0.45f; // max pan/tilt speed, fraction of source width per second
	float smoothing = 0.6f;      // seconds (smooth-damp time); higher = more cinematic
	float deadZoneX = 0.20f;     // fraction of crop width the subject may wander without a move
	float deadZoneY = 0.25f;     // fraction of crop height
	float accelLimit = 1.2f;     // fraction of source width per second^2

	// ---- Church preacher mode
	bool pulpitEnabled = false;
	float pulpitX = 0.5f;     // pulpit center, fraction of source width
	float pulpitWidth = 0.25f; // pulpit zone width, fraction of source width
	bool returnToPulpit = true; // when the subject is lost, return to the pulpit instead of the center

	// ---- Group framing
	int maxSubjects = 6;
	float groupMinRelSize = 0.35f; // people smaller than this x the largest are ignored (background)

	// ---- Lost subject recovery
	float lostTimeout = 2.5f;   // seconds to hold the last framing
	bool holdPosition = true;   // hold during the timeout (otherwise start returning immediately)
	bool returnToWide = true;   // after the timeout, glide to a safe wide shot
	float recoverySpeed = 1.6f; // seconds (smooth-damp time) for the return
	float reacquireRadius = 0.25f; // fraction of source width to re-lock a returning subject

	// ---- Performance
	DetectorModel model = DetectorModel::YoloxNano;
	std::string customModelPath;
	float detectionFps = 5.0f;
	int threads = 2;
	bool useGpu = false; // DirectML (any DirectX 12 GPU incl. GTX 1650/1660)

	// ---- Debug
	bool showBoxes = false;
	bool showIds = true;
	bool showStats = false;
	bool showGuides = false;      // dead zone + pulpit zone
	bool diagnosticLogs = false;
};

/* Composition + motion defaults for each preset. */
struct PresetDefaults {
	Composition composition;
	float smoothing;
	float trackingSpeed;
	float deadZoneX;
	float deadZoneY;
	float maxZoom;
	TrackingMode mode;
	bool pulpit;
};

PresetDefaults presetDefaults(FramingPreset preset);

/* Writes the preset's values into cfg (composition, motion, mode). */
void applyPreset(AutoFrameConfig &cfg, FramingPreset preset);

/* Detection rate / threads for the performance modes. Custom leaves values untouched. */
void applyPerformanceMode(AutoFrameConfig &cfg, PerformanceMode mode);

float aspectValue(OutputAspect aspect, float srcW, float srcH);

/* Filter output size for a camera of srcW x srcH (even numbers, never 0 for a valid source). */
void outputDimensions(OutputAspect aspect, OutputSize size, uint32_t srcW, uint32_t srcH, uint32_t &outW,
		      uint32_t &outH);

const char *presetName(FramingPreset preset);

} // namespace vmaf
