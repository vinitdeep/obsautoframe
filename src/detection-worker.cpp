// V Media AI AutoFrame - background inference thread.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "detection-worker.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#endif

#include <chrono>

namespace vmaf {

DetectionWorker::~DetectionWorker()
{
	stop();
}

void DetectionWorker::start(const DetectorOptions &opts, float minScore)
{
	stop();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopFlag_ = false;
		hasPending_ = false;
		hasResult_ = false;
		message_ = "Loading AI model...";
	}
	minScore_.store(minScore);
	inW_.store(0);
	inH_.store(0);
	processed_.store(0);
	dropped_.store(0);
	status_.store(WorkerStatus::Loading);
	thread_ = std::thread(&DetectionWorker::run, this, opts);
}

void DetectionWorker::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopFlag_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
	status_.store(WorkerStatus::Stopped);
}

bool DetectionWorker::idle() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return !hasPending_;
}

FramePacket DetectionWorker::takeSpare()
{
	std::lock_guard<std::mutex> lock(mutex_);
	FramePacket p;
	std::swap(p, spare_);
	return p;
}

void DetectionWorker::submit(FramePacket &&pkt)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (hasPending_) {
			// detector is behind: keep only the newest frame
			dropped_.fetch_add(1);
			std::swap(spare_, pending_);
		}
		pending_ = std::move(pkt);
		hasPending_ = true;
	}
	cv_.notify_one();
}

bool DetectionWorker::takeResult(DetectionResult &out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!hasResult_)
		return false;
	std::swap(out, result_);
	hasResult_ = false;
	return true;
}

std::string DetectionWorker::statusMessage() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return message_;
}

void DetectionWorker::run(DetectorOptions opts)
{
	// Inference is background work: OBS rendering and encoding always come first.
#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	SetThreadDescription(GetCurrentThread(), L"vmedia-autoframe-ai");
#elif defined(__APPLE__)
	pthread_setname_np("vmedia-autoframe-ai");
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
	// An exception escaping a std::thread calls std::terminate() and would take OBS down.
	try {
		runImpl(opts);
	} catch (const std::exception &e) {
		std::lock_guard<std::mutex> lock(mutex_);
		message_ = std::string("AI worker stopped: ") + e.what();
		status_.store(WorkerStatus::Failed);
	} catch (...) {
		std::lock_guard<std::mutex> lock(mutex_);
		message_ = "AI worker stopped: unknown error";
		status_.store(WorkerStatus::Failed);
	}
}

void DetectionWorker::runImpl(const DetectorOptions &opts)
{
	PersonDetector detector;
	std::string err;
	const bool ok = detector.load(opts, err);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!ok) {
			message_ = err;
		} else {
			message_ = "Ready: " + detector.modelInfo();
			if (!err.empty())
				message_ += " (" + err + ")"; // e.g. GPU fallback note
		}
	}
	if (!ok) {
		status_.store(WorkerStatus::Failed);
		return;
	}
	inW_.store(detector.inputWidth());
	inH_.store(detector.inputHeight());
	status_.store(WorkerStatus::Ready);

	FramePacket work;
	DetectionResult res;
	for (;;) {
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [&] { return stopFlag_ || hasPending_; });
			if (stopFlag_)
				break;
			std::swap(work, pending_);
			hasPending_ = false;
		}

		res.dets.clear();
		res.timestamp = work.timestamp;
		res.frameId = work.id;
		res.srcW = work.lb.srcW;
		res.srcH = work.lb.srcH;
		const auto t0 = std::chrono::steady_clock::now();
		std::string derr;
		bool good = false;
		if (work.width == detector.inputWidth() && work.height == detector.inputHeight() && !work.rgba.empty())
			good = detector.detect(work.rgba.data(), work.stride, work.lb, minScore_.load(), res.dets, derr);
		else
			derr = "frame size does not match model input";
		const auto t1 = std::chrono::steady_clock::now();
		res.inferenceMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (good) {
				std::swap(result_, res);
				hasResult_ = true;
				processed_.fetch_add(1);
			} else {
				message_ = "Detection error: " + derr;
			}
			// recycle the buffer for the producer
			if (spare_.rgba.empty())
				std::swap(spare_, work);
		}
	}
}

} // namespace vmaf
