// Scenario simulator for the tracking/framing tests: a scripted "world" produces person
// boxes over time, the detector samples it at the configured detection rate, the tracker
// and framing controller run at the video frame rate - exactly like inside OBS.
#pragma once

#include "framing-controller.hpp"
#include "person-tracker.hpp"
#include "settings.hpp"
#include "test-framework.hpp"

#include <functional>
#include <vector>

namespace vmaf {

inline Detection person(float cx, float top, float h, float score = 0.9f)
{
	const float w = h * 0.36f;
	return {RectF{cx - 0.5f * w, top, w, h}, score};
}

struct Sim {
	AutoFrameConfig cfg;
	PersonTracker tracker;
	FramingController fr;
	float W = 1920.0f, H = 1080.0f;
	double t = 0.0;
	double nextDet = 0.0;
	float fps = 30.0f;
	bool detectorReady = true;
	int violations = 0; // crop outside the source or wrong aspect
	std::function<std::vector<Detection>(double)> world = [](double) { return std::vector<Detection>{}; };

	explicit Sim(const AutoFrameConfig &c, float w = 1920.0f, float h = 1080.0f) : cfg(c), W(w), H(h)
	{
		TrackerConfig tc;
		tc.highThresh = cfg.confidence;
		tc.lowThresh = std::max(0.08f, cfg.confidence * 0.4f);
		tc.maxLostTime = std::max(4.0f, cfg.lostTimeout + 2.0f);
		tracker.configure(tc);
		fr.configure(cfg);
		fr.setSourceSize(W, H);
	}

	void step()
	{
		const float dt = 1.0f / fps;
		t += dt;
		if (t >= nextDet) {
			tracker.update(world(t), t);
			nextDet += 1.0 / cfg.detectionFps;
		}
		fr.update(tracker, t, dt, detectorReady);
		const RectF &c = fr.output().crop;
		const float aspect = aspectValue(cfg.aspect, W, H);
		const bool inside = c.x >= -0.01f && c.y >= -0.01f && c.right() <= W + 0.01f && c.bottom() <= H + 0.01f;
		const bool aspectOk = std::fabs(c.w / c.h - aspect) < 1e-3f;
		if (!inside || !aspectOk)
			++violations;
	}

	void run(double seconds)
	{
		const int n = (int)(seconds * fps + 0.5);
		for (int i = 0; i < n; ++i)
			step();
	}

	const FramingOutput &out() const { return fr.output(); }
};

inline AutoFrameConfig testConfig(FramingPreset p = FramingPreset::Medium)
{
	AutoFrameConfig c;
	applyPreset(c, p);
	c.detectionFps = 5.0f;
	return c;
}

} // namespace vmaf
