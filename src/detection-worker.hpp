// V Media AI AutoFrame - background inference thread.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
//
// The OBS graphics thread never waits for the AI. It drops the newest downscaled frame
// into a single-slot mailbox (an older unprocessed frame is simply replaced = frame
// dropping under load) and picks up finished results on later ticks.
#pragma once

#include "geometry.hpp"
#include "person-detector.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vmaf {

struct FramePacket {
	std::vector<uint8_t> rgba; // letterboxed network input (inputW x inputH, RGBA)
	int width = 0, height = 0, stride = 0;
	LetterboxMap lb;
	double timestamp = 0.0; // capture time, monotonic seconds
	uint64_t id = 0;
};

struct DetectionResult {
	std::vector<Detection> dets; // source pixels
	double timestamp = 0.0;
	uint64_t frameId = 0;
	float inferenceMs = 0.0f;
	int srcW = 0, srcH = 0;
};

enum class WorkerStatus : int { Stopped = 0, Loading, Ready, Failed };

class DetectionWorker {
public:
	DetectionWorker() = default;
	~DetectionWorker();
	DetectionWorker(const DetectionWorker &) = delete;
	DetectionWorker &operator=(const DetectionWorker &) = delete;

	/* Starts the thread; the model is loaded on the worker thread (never blocks OBS). */
	void start(const DetectorOptions &opts, float minScore);
	/* Signals the thread and joins it. Safe to call repeatedly. */
	void stop();

	void setMinScore(float s) { minScore_.store(s); }

	// ---- producer side (OBS graphics thread)
	bool ready() const { return status_.load() == WorkerStatus::Ready; }
	bool idle() const; // no frame waiting -> worth capturing a new one
	/* Gives the caller a recycled buffer to fill (avoids per-frame allocations). */
	FramePacket takeSpare();
	void submit(FramePacket &&pkt);
	bool takeResult(DetectionResult &out);

	// ---- status (any thread)
	WorkerStatus status() const { return status_.load(); }
	std::string statusMessage() const;
	int inputWidth() const { return inW_.load(); }
	int inputHeight() const { return inH_.load(); }
	uint64_t framesProcessed() const { return processed_.load(); }
	uint64_t framesDropped() const { return dropped_.load(); }

private:
	void run(DetectorOptions opts);
	void runImpl(const DetectorOptions &opts);

	std::thread thread_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	bool stopFlag_ = false;
	bool hasPending_ = false;
	FramePacket pending_;
	FramePacket spare_;
	bool hasResult_ = false;
	DetectionResult result_;
	std::string message_;

	std::atomic<WorkerStatus> status_{WorkerStatus::Stopped};
	std::atomic<int> inW_{0}, inH_{0};
	std::atomic<float> minScore_{0.15f};
	std::atomic<uint64_t> processed_{0}, dropped_{0};
};

} // namespace vmaf
