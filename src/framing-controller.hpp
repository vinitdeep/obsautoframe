// V Media AI AutoFrame - virtual camera director.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
//
// Decides WHO to frame (target selection, subject lock, group framing, re-acquisition)
// and WHERE the virtual camera should be (composition, dead zones, pulpit anchor,
// lost-subject recovery, operator overrides), then moves the crop there smoothly.
// Runs on the OBS graphics thread once per video tick; costs microseconds.
#pragma once

#include "geometry.hpp"
#include "motion.hpp"
#include "person-tracker.hpp"
#include "settings.hpp"
#include "zoom-controller.hpp"

#include <vector>

namespace vmaf {

enum class FramingStatus : int {
	NoDetector = 0, // AI not running (loading/failed) - framing holds or stays wide
	Searching,      // no subject selected yet
	Tracking,       // following the subject
	Holding,        // subject temporarily lost - holding the last framing
	Returning,      // subject lost beyond the timeout - gliding to the safe wide shot
	Paused,         // operator paused auto tracking
	PositionLocked, // operator locked the current framing
	ForcedWide,     // operator forced the wide shot
};

const char *statusName(FramingStatus s);

struct FramingOutput {
	RectF crop;          // source pixels, always inside the source image
	float zoom = 1.0f;   // relative to the largest crop that fits the source
	FramingStatus status = FramingStatus::Searching;
	int targetId = -1;   // locked person (single mode)
	bool hasSubject = false;
	RectF subject;       // box being framed (person or group union)
	std::vector<int> groupIds;
	bool inPulpitZone = false;
};

class FramingController {
public:
	void configure(const AutoFrameConfig &cfg);
	const AutoFrameConfig &config() const { return cfg_; }

	/* Call whenever the source resolution may have changed. Resets to a centered wide shot
	 * if it did. Returns true on change. */
	bool setSourceSize(float w, float h);

	/* One tick. `now` = monotonic seconds (same clock as tracker timestamps). */
	void update(const PersonTracker &tracker, double now, float dt, bool detectorReady);

	// ---- operator commands (graphics thread)
	void togglePause();
	void setPaused(bool p);
	void toggleForceWide();
	void toggleLockPosition();
	void zoomStep(float factor); // > 1 zooms in
	void resetZoom();
	void resetFraming();
	void selectTarget(int id);   // -1 = automatic
	void cycleTarget(const PersonTracker &tracker, double now, int dir);
	void toggleGroupMode();

	bool paused() const { return paused_; }
	bool positionLocked() const { return lockPos_; }
	bool forcedWide() const { return forceWide_; }
	TrackingMode mode() const { return mode_; }

	const FramingOutput &output() const { return out_; }
	float aspect() const { return aspect_; }
	float sourceWidth() const { return W_; }
	float sourceHeight() const { return H_; }

	/* Dead-zone rectangle in source pixels around the current crop center (for overlays). */
	RectF deadZoneRect() const;
	/* Pulpit zone in source pixels (empty if disabled). */
	RectF pulpitRect() const;

	/* Presence = matched by the detector recently enough to trust its box. */
	bool isPresent(const Track &t, double now) const;
	/* May this person be picked up as a (new) subject? Stage area + bottom-edge rule. */
	bool isEligible(const Track &t) const;
	/* Stage area in source pixels. */
	RectF stageRect() const;

private:
	struct Hyst {
		bool moving = false;
	};

	void snapTo(const Shot &s);
	Shot wideShot() const;
	Shot currentShot() const;
	void clampShot(Shot &s, bool allowFullFrame) const;
	int pickByPolicy(const PersonTracker &tracker, double now, int exclude = -1) const;
	float policyScore(const Track &t) const;
	/* Present track near the last known subject position. With `newerThan`, only tracks
	 * first seen after that time qualify (people already on stage are never adopted). */
	int reacquire(const PersonTracker &tracker, double now, double newerThan = -1e300) const;
	bool selectSingle(const PersonTracker &tracker, double now, float dt, RectF &subject);
	bool selectGroup(const PersonTracker &tracker, double now, RectF &subject);
	void applyDeadZones(const Shot &desired, bool pulpitAnchored);

	AutoFrameConfig cfg_;
	float W_ = 0.0f, H_ = 0.0f, aspect_ = 16.0f / 9.0f;
	ZoomLimits lim_;

	SmoothAxis ax_, ay_, az_; // crop center x, y and ln(crop height)
	Shot goal_;
	Hyst hx_, hy_, hz_;

	TrackingMode mode_ = TrackingMode::Single;
	TrackingMode cfgMode_ = TrackingMode::Single;
	bool paused_ = false, lockPos_ = false, forceWide_ = false;
	float zoomBias_ = 1.0f;

	int targetId_ = -1;
	bool manualSelection_ = false;
	int candidateId_ = -1;
	float candidateTime_ = 0.0f;
	RectF lastSubject_;
	bool lastSubjectValid_ = false;
	float missingTime_ = 0.0f;
	bool everHadSubject_ = false;
	bool inPulpit_ = false;
	bool returning_ = false;
	bool configured_ = false;
	int groupAnchor_ = -1;        // lead person when the group is wider than the frame
	std::vector<int> prevGroup_;  // members framed last tick (hysteresis)

	FramingOutput out_;
};

} // namespace vmaf
