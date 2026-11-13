// V Media AI AutoFrame - frame-rate independent smoothing.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>

namespace vmaf {

/* One axis of a virtual camera.
 *
 * Critically damped spring ("smooth damp") with a speed cap and an acceleration cap.
 * The spring is integrated in fixed sub-steps of at most 1/240 s, so the trajectory is
 * the same whether OBS renders at 24, 30, 60 or 144 fps (verified by unit tests).
 *
 *  smoothTime : roughly the time to cover most of the distance to the target (seconds)
 *  maxSpeed   : units per second
 *  maxAccel   : units per second^2 (0 = unlimited) - prevents sudden starts */
struct SmoothAxis {
	float pos = 0.0f;
	float vel = 0.0f;

	void snap(float p)
	{
		pos = p;
		vel = 0.0f;
	}

	void step(float target, float smoothTime, float maxSpeed, float maxAccel, float dt)
	{
		if (!(dt > 0.0f))
			return;
		dt = std::min(dt, 0.5f); // a stalled render thread must not cause a jump
		const float kMaxStep = 1.0f / 240.0f;
		int n = (int)std::ceil(dt / kMaxStep);
		const float h = dt / (float)n;
		for (int i = 0; i < n; ++i)
			substep(target, smoothTime, maxSpeed, maxAccel, h);
	}

	bool settled(float target, float posEps, float velEps) const
	{
		return std::fabs(pos - target) <= posEps && std::fabs(vel) <= velEps;
	}

private:
	void substep(float target, float smoothTime, float maxSpeed, float maxAccel, float dt)
	{
		smoothTime = std::max(0.05f, smoothTime); // keeps omega*h small => stable
		const float omega = 2.0f / smoothTime;
		// critically damped spring acceleration
		float acc = omega * omega * (target - pos) - 2.0f * omega * vel;
		if (maxAccel > 0.0f)
			acc = std::clamp(acc, -maxAccel, maxAccel);
		float v = vel + acc * dt;
		if (maxSpeed > 0.0f)
			v = std::clamp(v, -maxSpeed, maxSpeed);
		pos += 0.5f * (vel + v) * dt;
		vel = v;
	}
};

} // namespace vmaf
