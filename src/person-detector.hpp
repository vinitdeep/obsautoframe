// V Media AI AutoFrame - ONNX Runtime person detector (YOLOX).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include "geometry.hpp"
#include "person-tracker.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vmaf {

struct DetectorOptions {
	std::wstring runtimeDir;     // folder containing onnxruntime.dll (+ DirectML.dll)
	std::wstring modelPath;      // .onnx file
	std::string expectedSha256;  // bundled models are verified; empty = skip (custom model)
	int threads = 2;             // CPU intra-op threads
	bool useGpu = false;         // DirectML on the high-performance GPU, falls back to CPU
};

/* Loads onnxruntime.dll from `runtimeDir` (by full path, never from PATH or the OBS folder,
 * so it cannot clash with another plugin's copy). Process-wide, idempotent, thread-safe. */
bool loadOrtRuntime(const std::wstring &runtimeDir, std::string &err);
/* Releases the shared ONNX Runtime environment. Call once at module unload. */
void shutdownOrtRuntime();

/* SHA-256 of a file as lowercase hex (Windows CNG). Empty string on failure. */
std::string sha256File(const std::wstring &path);

class PersonDetector {
public:
	PersonDetector();
	~PersonDetector();
	PersonDetector(const PersonDetector &) = delete;
	PersonDetector &operator=(const PersonDetector &) = delete;

	bool load(const DetectorOptions &opts, std::string &err);
	bool loaded() const;
	int inputWidth() const;
	int inputHeight() const;
	const std::string &providerName() const; // "CPU" or "DirectML (GPU)"
	const std::string &modelInfo() const;

	/* rgba: letterboxed network input, exactly inputWidth() x inputHeight().
	 * Detections are returned in SOURCE pixels (mapped back through `lb`). */
	bool detect(const uint8_t *rgba, int stride, const LetterboxMap &lb, float scoreThresh,
		    std::vector<Detection> &out, std::string &err);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace vmaf
