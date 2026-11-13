// V Media AI AutoFrame - virtual camera director.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "framing-controller.hpp"

#include <algorithm>
#include <cmath>

namespace vmaf {

const char *statusName(FramingStatus s)
{
	switch (s) {
	case FramingStatus::NoDetector:
		return "AI NOT READY";
	case FramingStatus::Searching:
		return "SEARCHING";
	case FramingStatus::Tracking:
		return "TRACKING";
	case FramingStatus::Holding:
		return "HOLDING";
	case FramingStatus::Returning:
		return "RETURNING WIDE";
	case FramingStatus::Paused:
		return "PAUSED";
	case FramingStatus::PositionLocked:
		return "POSITION LOCKED";
	case FramingStatus::ForcedWide:
		return "FORCED WIDE";
	}
	return "?";
}

/* Settings that change WHERE the shot should be. Anything else (debug graphics, detection
 * rate, threads, GPU, ...) must not move the camera - an operator may toggle those live. */
static bool sameFraming(const AutoFrameConfig &a, const AutoFrameConfig &b)
{
	return a.composition.bodyFraction == b.composition.bodyFraction &&
	       a.composition.subjectFill == b.composition.subjectFill &&
	       a.composition.headroom == b.composition.headroom && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
	       a.safeMargin == b.safeMargin && a.aspect == b.aspect && a.outputSize == b.outputSize &&
	       a.autoZoom == b.autoZoom && a.minZoom == b.minZoom && a.maxZoom == b.maxZoom &&
	       a.fixedZoom == b.fixedZoom && a.maxEnlarge == b.maxEnlarge && a.trackingMode == b.trackingMode &&
	       a.pulpitEnabled == b.pulpitEnabled && a.pulpitX == b.pulpitX && a.pulpitWidth == b.pulpitWidth &&
	       a.policy == b.policy && a.lockSubject == b.lockSubject && a.stageLeft == b.stageLeft &&
	       a.stageRight == b.stageRight && a.stageTop == b.stageTop && a.stageBottom == b.stageBottom &&
	       a.ignoreBottomEdge == b.ignoreBottomEdge && a.maxSubjects == b.maxSubjects &&
	       a.groupMinRelSize == b.groupMinRelSize && a.autoTracking == b.autoTracking;
}

void FramingController::configure(const AutoFrameConfig &cfg)
{
	const float oldAspect = aspect_;
	const bool reframe = !configured_ || !sameFraming(cfg_, cfg);
	configured_ = true;
	cfg_ = cfg;
	if (cfg.trackingMode != cfgMode_) {
		cfgMode_ = cfg.trackingMode;
		mode_ = cfgMode_;
	}
	if (!cfg.lockSubject && cfg.policy != TargetPolicy::Manual)
		manualSelection_ = false;
	if (W_ > 0.0f && H_ > 0.0f) {
		aspect_ = aspectValue(cfg_.aspect, W_, H_);
		lim_ = ZoomController::limits(aspect_, W_, H_, cfg_.minZoom, ZoomController::effectiveMaxZoom(cfg_, W_, H_));
		if (std::fabs(oldAspect - aspect_) > 1e-4f) {
			goal_ = wideShot();
			snapTo(goal_);
		} else {
			// new zoom limits: keep the goal legal, the motion will follow smoothly
			clampShot(goal_, forceWide_ || lockPos_ || paused_);
		}
		if (reframe)
			hx_.moving = hy_.moving = hz_.moving = true;
	}
}

bool FramingController::setSourceSize(float w, float h)
{
	if (w <= 0.0f || h <= 0.0f)
		return false;
	if (w == W_ && h == H_)
		return false;
	W_ = w;
	H_ = h;
	aspect_ = aspectValue(cfg_.aspect, W_, H_);
	lim_ = ZoomController::limits(aspect_, W_, H_, cfg_.minZoom, ZoomController::effectiveMaxZoom(cfg_, W_, H_));
	targetId_ = -1;
	lastSubjectValid_ = false;
	everHadSubject_ = false;
	missingTime_ = 0.0f;
	inPulpit_ = false;
	goal_ = wideShot();
	snapTo(goal_);
	hx_ = hy_ = hz_ = Hyst{};
	out_ = FramingOutput{};
	out_.crop = cropRect(goal_.cx, goal_.cy, goal_.h, aspect_);
	out_.zoom = lim_.zoomFor(goal_.h);
	return true;
}

void FramingController::snapTo(const Shot &s)
{
	ax_.snap(s.cx);
	ay_.snap(s.cy);
	az_.snap(std::log(std::max(1.0f, s.h)));
}

Shot FramingController::currentShot() const
{
	return {ax_.pos, ay_.pos, std::exp(az_.pos)};
}

void FramingController::clampShot(Shot &s, bool allowFullFrame) const
{
	if (W_ <= 0.0f || H_ <= 0.0f)
		return;
	const float hmax = allowFullFrame ? lim_.maxH : lim_.wideH;
	s.h = std::clamp(s.h, std::min(lim_.minH, hmax), hmax);
	clampCrop(s.cx, s.cy, s.h, aspect_, W_, H_);
}

Shot FramingController::wideShot() const
{
	Shot s;
	s.h = lim_.wideH;
	s.cx = (cfg_.pulpitEnabled && cfg_.returnToPulpit) ? cfg_.pulpitX * W_ : 0.5f * W_;
	s.cy = 0.5f * H_;
	clampShot(s, false);
	return s;
}

bool FramingController::isPresent(const Track &t, double now) const
{
	if (t.state == TrackState::Tentative)
		return false;
	const double grace = std::max(0.45, 2.2 / std::max(0.5, (double)cfg_.detectionFps));
	return (now - t.lastUpdate) <= grace;
}

RectF FramingController::stageRect() const
{
	const float l = std::clamp(cfg_.stageLeft, 0.0f, 1.0f), r = std::clamp(cfg_.stageRight, 0.0f, 1.0f);
	const float t = std::clamp(cfg_.stageTop, 0.0f, 1.0f), b = std::clamp(cfg_.stageBottom, 0.0f, 1.0f);
	return {std::min(l, r) * W_, std::min(t, b) * H_, std::fabs(r - l) * W_, std::fabs(b - t) * H_};
}

bool FramingController::isEligible(const Track &t) const
{
	// The person's FEET (bottom-center of the box) must stand inside the stage area. Feet are
	// what separates people on a stage from the congregation in front of the camera: a seated
	// foreground person's box runs to the bottom edge of the picture even though its center
	// is mid-frame.
	const RectF s = stageRect();
	const float fx = t.box.cx(), fy = t.box.bottom();
	if (fx < s.x || fx > s.right() || fy < s.y || fy > s.bottom() + 0.5f)
		return false;
	if (cfg_.ignoreBottomEdge && t.box.bottom() >= H_ * 0.985f)
		return false;
	return true;
}

float FramingController::policyScore(const Track &t) const
{
	switch (cfg_.policy) {
	case TargetPolicy::Centered: {
		const float d = std::fabs(t.box.cx() - 0.5f * W_) / std::max(1.0f, W_);
		return 1.0f / (1.0f + 4.0f * d);
	}
	case TargetPolicy::Confident:
		return t.score;
	case TargetPolicy::Largest:
	case TargetPolicy::Manual: // until the operator picks someone, behave like "largest"
		break;
	}
	return t.box.area();
}

int FramingController::pickByPolicy(const PersonTracker &tracker, double now, int exclude) const
{
	int best = -1;
	float bestScore = -1.0f;
	for (const Track &t : tracker.tracks()) {
		if (t.id == exclude || !isPresent(t, now) || !isEligible(t))
			continue;
		const float s = policyScore(t);
		if (s > bestScore) {
			bestScore = s;
			best = t.id;
		}
	}
	return best;
}

int FramingController::reacquire(const PersonTracker &tracker, double now, double newerThan) const
{
	if (!lastSubjectValid_)
		return -1;
	const float radius = std::max(0.02f, cfg_.reacquireRadius) * W_;
	int best = -1;
	float bestD = radius;
	for (const Track &t : tracker.tracks()) {
		if (!isPresent(t, now) || t.firstSeen < newerThan || !isEligible(t))
			continue;
		const float hr = std::min(t.box.h, lastSubject_.h) / std::max(1.0f, std::max(t.box.h, lastSubject_.h));
		if (hr < 0.5f)
			continue;
		const float d = std::hypot(t.box.cx() - lastSubject_.cx(), t.box.cy() - lastSubject_.cy());
		if (d < bestD) {
			bestD = d;
			best = t.id;
		}
	}
	return best;
}

bool FramingController::selectSingle(const PersonTracker &tracker, double now, float dt, RectF &subject)
{
	const Track *t = targetId_ >= 0 ? tracker.find(targetId_) : nullptr;

	if (t && isPresent(*t, now)) {
		// Without subject lock, switch to a clearly better candidate - but only after it
		// has been better for a full second, so the shot never flips back and forth.
		if (!cfg_.lockSubject && !manualSelection_) {
			const int best = pickByPolicy(tracker, now);
			const Track *b = best >= 0 ? tracker.find(best) : nullptr;
			if (b && best != targetId_ && policyScore(*b) > policyScore(*t) * 1.3f) {
				if (candidateId_ == best) {
					candidateTime_ += dt;
				} else {
					candidateId_ = best;
					candidateTime_ = 0.0f;
				}
				if (candidateTime_ >= 1.0f) {
					targetId_ = best;
					t = b;
					candidateId_ = -1;
				}
			} else {
				candidateId_ = -1;
			}
		}
		subject = t->predict(now, 0.3f); // compensates detection latency
		return true;
	}

	int id = -1;
	if (t) {
		// Locked person not seen right now. The tracker keeps the identity for a while
		// (occlusion). Only a person who APPEARED after the subject vanished, right where
		// the subject was, is accepted (the tracker re-created the same person under a new
		// ID). Whoever is hiding the subject was already on stage and is never adopted.
		if (missingTime_ > 0.5f) {
			const int r = reacquire(tracker, now, now - missingTime_ - 0.25);
			if (r >= 0 && r != targetId_)
				id = r;
		}
		if (id < 0 && missingTime_ >= cfg_.lostTimeout)
			id = pickByPolicy(tracker, now, targetId_);
	} else {
		id = reacquire(tracker, now);
		if (id < 0 && (!everHadSubject_ || !lastSubjectValid_ || missingTime_ >= cfg_.lostTimeout))
			id = pickByPolicy(tracker, now);
	}

	if (id < 0)
		return false;
	targetId_ = id;
	manualSelection_ = false;
	candidateId_ = -1;
	const Track *nt = tracker.find(id);
	if (!nt)
		return false;
	subject = nt->predict(now, 0.3f);
	return true;
}

bool FramingController::selectGroup(const PersonTracker &tracker, double now, RectF &subject)
{
	std::vector<const Track *> people;
	for (const Track &t : tracker.tracks())
		if (isPresent(t, now) && isEligible(t))
			people.push_back(&t);
	if (people.empty())
		return false;
	std::sort(people.begin(), people.end(), [](const Track *a, const Track *b) { return a->box.h > b->box.h; });
	// people much smaller than the main subjects are in the background (congregation,
	// audience) and must not pull the framing around
	const float minH = people.front()->box.h * std::clamp(cfg_.groupMinRelSize, 0.0f, 1.0f);
	std::vector<const Track *> chosen;
	RectF u;
	for (const Track *t : people) {
		if ((int)chosen.size() >= std::max(1, cfg_.maxSubjects))
			break;
		if (t->box.h < minH)
			continue;
		chosen.push_back(t);
		u = unite(u, t->predict(now, 0.3f));
	}
	if (chosen.empty())
		return false;

	// Does the whole group fit in the widest allowed crop (with the safe margin)?
	// A worship team spread across a 16:9 picture never fits a 9:16 vertical frame. Centering
	// on the group would then show the empty middle and cut everyone in half, so instead
	// frame a LEAD person plus as many WHOLE neighbours as fit.
	const float maxW = lim_.wideH * aspect_ / (1.0f + 2.0f * std::max(0.0f, cfg_.safeMargin));
	if (u.w > maxW && chosen.size() > 1) {
		const Track *anchor = nullptr;
		for (const Track *t : chosen) // keep the previous lead while they are still here
			if (t->id == groupAnchor_)
				anchor = t;
		if (!anchor) {
			float best = -1.0f;
			for (const Track *t : chosen) {
				const float s = policyScore(*t);
				if (s > best) {
					best = s;
					anchor = t;
				}
			}
		}
		groupAnchor_ = anchor->id;
		std::vector<const Track *> rest;
		for (const Track *t : chosen)
			if (t != anchor)
				rest.push_back(t);
		const float ax = anchor->box.cx();
		std::sort(rest.begin(), rest.end(), [ax](const Track *a, const Track *b) {
			return std::fabs(a->box.cx() - ax) < std::fabs(b->box.cx() - ax);
		});
		u = anchor->predict(now, 0.3f);
		std::vector<const Track *> fit{anchor};
		for (const Track *t : rest) {
			// hysteresis: people already in the shot may use a little more room than newcomers,
			// so nobody pops in and out when the group is right at the limit
			const bool was = std::find(prevGroup_.begin(), prevGroup_.end(), t->id) != prevGroup_.end();
			const RectF cand = unite(u, t->predict(now, 0.3f));
			if (cand.w <= maxW * (was ? 1.06f : 0.94f)) {
				u = cand;
				fit.push_back(t);
			}
		}
		chosen.swap(fit);
	} else {
		groupAnchor_ = -1;
	}
	prevGroup_.clear();
	for (const Track *t : chosen) {
		out_.groupIds.push_back(t->id);
		prevGroup_.push_back(t->id);
	}
	subject = u;
	return true;
}

void FramingController::applyDeadZones(const Shot &desired, bool pulpitAnchored)
{
	const Shot cur = currentShot();
	const float cropW = cur.h * aspect_;

	// Horizontal: the shot only moves once the subject leaves the dead zone, then follows
	// until the subject is re-centered, then holds still again (no constant micro-motion).
	if (pulpitAnchored) {
		goal_.cx = desired.cx; // fixed pulpit composition
		hx_.moving = false;
	} else {
		const float trig = 0.5f * std::max(0.0f, cfg_.deadZoneX) * cropW;
		const float settle = std::max(2.0f, 0.15f * trig + 0.01f * cropW);
		if (!hx_.moving && std::fabs(desired.cx - goal_.cx) > trig)
			hx_.moving = true;
		if (hx_.moving) {
			goal_.cx = desired.cx;
			if (std::fabs(desired.cx - cur.cx) < settle && std::fabs(ax_.vel) < 0.02f * W_)
				hx_.moving = false;
		}
	}

	{
		const float trig = 0.5f * std::max(0.0f, cfg_.deadZoneY) * cur.h;
		const float settle = std::max(2.0f, 0.15f * trig + 0.01f * cur.h);
		if (!hy_.moving && std::fabs(desired.cy - goal_.cy) > trig)
			hy_.moving = true;
		if (hy_.moving) {
			goal_.cy = desired.cy;
			if (std::fabs(desired.cy - cur.cy) < settle && std::fabs(ay_.vel) < 0.02f * H_)
				hy_.moving = false;
		}
	}

	{
		const float band = std::max(0.0f, cfg_.zoomDeadband);
		if (!hz_.moving && std::fabs(std::log(desired.h / std::max(1.0f, goal_.h))) > band)
			hz_.moving = true;
		if (hz_.moving) {
			goal_.h = desired.h;
			if (std::fabs(std::log(desired.h / std::max(1.0f, cur.h))) < std::max(0.01f, 0.25f * band) &&
			    std::fabs(az_.vel) < 0.02f)
				hz_.moving = false;
		}
	}
	clampShot(goal_, false);
}

void FramingController::update(const PersonTracker &tracker, double now, float dt, bool detectorReady)
{
	if (W_ <= 0.0f || H_ <= 0.0f)
		return;
	out_.groupIds.clear();
	out_.hasSubject = false;

	FramingStatus status = FramingStatus::Searching;
	bool returning = false;
	RectF subject;

	if (forceWide_) {
		status = FramingStatus::ForcedWide;
		goal_ = {0.5f * W_, 0.5f * H_, lim_.maxH};
		clampShot(goal_, true);
		returning = true;
	} else if (lockPos_) {
		status = FramingStatus::PositionLocked;
	} else if (paused_ || !cfg_.autoTracking) {
		status = FramingStatus::Paused;
	} else if (!detectorReady) {
		status = FramingStatus::NoDetector;
		if (cfg_.returnToWide) {
			goal_ = wideShot();
			returning = true;
		}
	} else {
		const bool have = (mode_ == TrackingMode::Group) ? selectGroup(tracker, now, subject)
								  : selectSingle(tracker, now, dt, subject);
		if (have && !subject.empty()) {
			missingTime_ = 0.0f;
			lastSubject_ = subject;
			lastSubjectValid_ = true;
			everHadSubject_ = true;
			const bool truncated = subject.bottom() >= H_ * 0.97f;
			Shot desired = ZoomController::compose(subject, truncated, cfg_, aspect_, lim_, zoomBias_);

			bool anchored = false;
			if (mode_ == TrackingMode::Single && cfg_.pulpitEnabled) {
				const RectF pz = pulpitRect();
				const float margin = 0.04f * W_; // hysteresis so the anchor does not flicker
				if (inPulpit_)
					inPulpit_ = subject.cx() >= pz.x - margin && subject.cx() <= pz.right() + margin;
				else
					inPulpit_ = subject.cx() >= pz.x && subject.cx() <= pz.right();
				if (inPulpit_) {
					desired.cx = pz.cx() - cfg_.offsetX * desired.h * aspect_;
					anchored = true;
				}
			} else {
				inPulpit_ = false;
			}
			clampShot(desired, false);
			applyDeadZones(desired, anchored);
			status = FramingStatus::Tracking;
			out_.hasSubject = true;
			out_.subject = subject;
		} else {
			missingTime_ += dt;
			const bool timedOut = !cfg_.holdPosition || missingTime_ >= cfg_.lostTimeout;
			if (!everHadSubject_) {
				status = FramingStatus::Searching;
			} else if (timedOut && cfg_.returnToWide) {
				goal_ = wideShot();
				returning = true;
				status = FramingStatus::Returning;
				hx_.moving = hy_.moving = hz_.moving = true;
			} else {
				status = FramingStatus::Holding;
			}
		}
	}
	returning_ = returning;

	const float smooth = returning ? cfg_.recoverySpeed : cfg_.smoothing;
	const float zsmooth = returning ? cfg_.recoverySpeed : cfg_.zoomSmoothing;
	const float maxV = std::max(0.01f, cfg_.trackingSpeed) * W_;
	const float maxA = cfg_.accelLimit > 0.0f ? cfg_.accelLimit * W_ : 0.0f;
	const float maxZ = std::max(0.01f, cfg_.zoomSpeed);

	ax_.step(goal_.cx, smooth, maxV, maxA, dt);
	ay_.step(goal_.cy, smooth, maxV, maxA, dt);
	az_.step(std::log(std::max(1.0f, goal_.h)), zsmooth, maxZ, maxZ * 3.0f, dt);

	// Hard guarantee: the crop never leaves the camera image (no black borders).
	Shot cur = currentShot();
	Shot c = cur;
	clampShot(c, true);
	if (c.h != cur.h) {
		az_.snap(std::log(c.h));
	}
	if (c.cx != cur.cx) {
		ax_.pos = c.cx;
		ax_.vel = 0.0f;
	}
	if (c.cy != cur.cy) {
		ay_.pos = c.cy;
		ay_.vel = 0.0f;
	}

	out_.crop = cropRect(c.cx, c.cy, c.h, aspect_);
	out_.zoom = lim_.zoomFor(c.h);
	out_.status = status;
	out_.targetId = mode_ == TrackingMode::Single ? targetId_ : -1;
	out_.inPulpitZone = inPulpit_;
}

// ------------------------------------------------------------------ operator commands

void FramingController::togglePause()
{
	setPaused(!paused_);
}

void FramingController::setPaused(bool p)
{
	if (p == paused_)
		return;
	paused_ = p;
	if (p)
		goal_ = currentShot(); // freeze exactly where the camera is
	else
		hx_.moving = hy_.moving = hz_.moving = true;
}

void FramingController::toggleForceWide()
{
	forceWide_ = !forceWide_;
	if (!forceWide_)
		hx_.moving = hy_.moving = hz_.moving = true;
}

void FramingController::toggleLockPosition()
{
	lockPos_ = !lockPos_;
	if (lockPos_)
		goal_ = currentShot();
	else
		hx_.moving = hy_.moving = hz_.moving = true;
}

void FramingController::zoomStep(float factor)
{
	if (factor <= 0.0f)
		return;
	if (forceWide_) {
		forceWide_ = false;
		lockPos_ = true;
		goal_ = currentShot();
	}
	if (paused_ || lockPos_ || !cfg_.autoTracking) {
		goal_.h /= factor;
		clampShot(goal_, true);
	} else {
		zoomBias_ = std::clamp(zoomBias_ / factor, 0.5f, 2.0f);
		hz_.moving = true;
	}
}

void FramingController::resetZoom()
{
	zoomBias_ = 1.0f;
	if (paused_ || lockPos_ || !cfg_.autoTracking) {
		goal_.h = lim_.maxH;
		clampShot(goal_, true);
	} else {
		hz_.moving = true;
	}
}

void FramingController::resetFraming()
{
	paused_ = lockPos_ = forceWide_ = false;
	zoomBias_ = 1.0f;
	targetId_ = -1;
	manualSelection_ = false;
	candidateId_ = -1;
	lastSubjectValid_ = false;
	everHadSubject_ = false;
	missingTime_ = 0.0f;
	inPulpit_ = false;
	groupAnchor_ = -1;
	prevGroup_.clear();
	mode_ = cfgMode_;
	goal_ = wideShot();
	hx_ = hy_ = hz_ = Hyst{};
}

void FramingController::selectTarget(int id)
{
	targetId_ = id;
	manualSelection_ = id >= 0;
	candidateId_ = -1;
	missingTime_ = 0.0f;
	if (id >= 0)
		mode_ = TrackingMode::Single;
	hx_.moving = hy_.moving = hz_.moving = true;
}

void FramingController::cycleTarget(const PersonTracker &tracker, double now, int dir)
{
	std::vector<const Track *> people;
	for (const Track &t : tracker.tracks())
		if (isPresent(t, now) && (isEligible(t) || t.id == targetId_))
			people.push_back(&t);
	if (people.empty())
		return;
	std::sort(people.begin(), people.end(), [](const Track *a, const Track *b) { return a->box.cx() < b->box.cx(); });
	const int n = (int)people.size();
	int idx = -1;
	for (int i = 0; i < n; ++i)
		if (people[i]->id == targetId_)
			idx = i;
	if (idx < 0)
		idx = dir > 0 ? 0 : n - 1;
	else
		idx = ((idx + (dir > 0 ? 1 : -1)) % n + n) % n;
	selectTarget(people[idx]->id);
}

void FramingController::toggleGroupMode()
{
	mode_ = mode_ == TrackingMode::Single ? TrackingMode::Group : TrackingMode::Single;
	hx_.moving = hy_.moving = hz_.moving = true;
}

RectF FramingController::deadZoneRect() const
{
	const Shot cur = currentShot();
	const float w = std::max(0.0f, cfg_.deadZoneX) * cur.h * aspect_;
	const float h = std::max(0.0f, cfg_.deadZoneY) * cur.h;
	return RectF::fromCenter(goal_.cx, goal_.cy, w, h);
}

RectF FramingController::pulpitRect() const
{
	if (!cfg_.pulpitEnabled || W_ <= 0.0f)
		return {};
	const float w = std::clamp(cfg_.pulpitWidth, 0.02f, 1.0f) * W_;
	return RectF::fromCenter(std::clamp(cfg_.pulpitX, 0.0f, 1.0f) * W_, 0.5f * H_, w, H_);
}

} // namespace vmaf
