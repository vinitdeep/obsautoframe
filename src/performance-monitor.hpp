// V Media AI AutoFrame - measured (not assumed) performance numbers.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include <cstdint>
#include <string>

namespace vmaf {

class PerformanceMonitor {
public:
	void reset();

	void addInference(float ms);  // worker inference time per frame
	void addDetection(double now); // a detection result arrived (rate measurement)
	void addRender(float ms);      // CPU time spent in the filter's video_render
	void addReadback(float ms);    // CPU time to map + copy the staged detector frame
	void addLatency(float ms);     // capture -> result available on the graphics thread

	float inferenceMs() const { return inferMs_; }
	float inferenceMaxMs() const { return inferMax_; }
	float detectionFps() const { return detFps_; }
	float renderMs() const { return renderMs_; }
	float readbackMs() const { return readbackMs_; }
	float latencyMs() const { return latencyMs_; }

	std::string summary(uint64_t processed, uint64_t dropped) const;

private:
	static void ema(float &acc, float v, bool &init, float a = 0.1f);

	float inferMs_ = 0, inferMax_ = 0, renderMs_ = 0, readbackMs_ = 0, latencyMs_ = 0, detFps_ = 0;
	bool inferInit_ = false, renderInit_ = false, readInit_ = false, latInit_ = false;
	double windowStart_ = -1.0;
	int windowCount_ = 0;
	int maxCount_ = 0;
};

} // namespace vmaf
