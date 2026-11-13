// V Media AI AutoFrame - performance numbers.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "performance-monitor.hpp"

#include <algorithm>
#include <cstdio>

namespace vmaf {

void PerformanceMonitor::reset()
{
	*this = PerformanceMonitor{};
}

void PerformanceMonitor::ema(float &acc, float v, bool &init, float a)
{
	if (!init) {
		acc = v;
		init = true;
	} else {
		acc += (v - acc) * a;
	}
}

void PerformanceMonitor::addInference(float ms)
{
	ema(inferMs_, ms, inferInit_);
	// slowly decaying peak so one spike is visible but does not stick forever
	if (++maxCount_ > 300) {
		inferMax_ = inferMs_;
		maxCount_ = 0;
	}
	inferMax_ = std::max(inferMax_, ms);
}

void PerformanceMonitor::addDetection(double now)
{
	if (windowStart_ < 0.0)
		windowStart_ = now;
	++windowCount_;
	const double span = now - windowStart_;
	if (span >= 2.0) {
		detFps_ = (float)(windowCount_ / span);
		windowStart_ = now;
		windowCount_ = 0;
	}
}

void PerformanceMonitor::addRender(float ms)
{
	ema(renderMs_, ms, renderInit_, 0.02f);
}

void PerformanceMonitor::addReadback(float ms)
{
	ema(readbackMs_, ms, readInit_);
}

void PerformanceMonitor::addLatency(float ms)
{
	ema(latencyMs_, ms, latInit_);
}

std::string PerformanceMonitor::summary(uint64_t processed, uint64_t dropped) const
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		 "detect %.1f/s | inference %.1f ms (peak %.1f) | readback %.2f ms | filter CPU %.2f ms/frame | "
		 "capture->result %.0f ms | frames %llu processed, %llu dropped",
		 detFps_, inferMs_, inferMax_, readbackMs_, renderMs_, latencyMs_, (unsigned long long)processed,
		 (unsigned long long)dropped);
	return buf;
}

} // namespace vmaf
