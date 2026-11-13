// Real detector test + benchmark: real ONNX Runtime, real YOLOX models, real video frames.
// No OBS needed. Media comes from tests/fetch-test-media.ps1 (skipped if absent).
//
//   vmaf-detector-test            correctness checks (CPU)
//   vmaf-detector-test --bench    + latency benchmark: models x CPU threads x GPU (DirectML)
#include "../tools/media-pipe.hpp"
#include "person-detector.hpp"
#include "yolox.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <filesystem>

using namespace vmaf;

namespace {

std::string widen(const std::string &s) // paths are UTF-8 on every platform
{
	return s;
}

struct Case {
	const char *file;
	double at;
	int minPeople;
	const char *what;
};

const Case kCases[] = {
	{"23117.mp4", 2.0, 1, "presenter at whiteboard"},
	{"23117.mp4", 12.0, 1, "presenter walking"},
	{"23521.mp4", 11.0, 2, "two presenters"},
	{"36706.mp4", 3.0, 3, "band / worship team"},
	{"24068.mp4", 4.0, 3, "stage + foreground audience"},
};

bool grab(const std::string &path, double at, std::vector<uint8_t> &rgba, int &w, int &h)
{
	VideoReader r;
	if (!r.open(path, at, 0.0, 1))
		return false;
	w = r.width();
	h = r.height();
	return r.read(rgba);
}

int g_fail = 0;
#define EXPECT(cond, ...)                         \
	do {                                      \
		if (!(cond)) {                    \
			++g_fail;                 \
			printf("  FAIL: ");       \
			printf(__VA_ARGS__);      \
			printf("\n");             \
		}                                 \
	} while (0)

} // namespace

int main(int argc, char **argv)
{
	const bool bench = argc > 1 && strcmp(argv[1], "--bench") == 0;
	const std::string media = std::string(VMAF_TESTDATA_DIR) + "/../media/";
	const std::string out = std::string(VMAF_TESTDATA_DIR) + "/../out/";
	std::filesystem::create_directories(out);

	struct ModelDef {
		const char *name;
		const char *file;
		const char *sha;
	};
	const ModelDef models[] = {
		{"YOLOX-Nano", "yolox_nano.onnx", "c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d"},
		{"YOLOX-Tiny", "yolox_tiny.onnx", "427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7"},
	};

	// ---- integrity check must reject a tampered/wrong model
	{
		PersonDetector d;
		DetectorOptions o;
		o.runtimeDir = widen(VMAF_RUNTIME_DIR);
		o.modelPath = widen(std::string(VMAF_MODELS_DIR) + "/yolox_nano.onnx");
		o.expectedSha256 = std::string(64, '0');
		std::string err;
		EXPECT(!d.load(o, err), "a model with the wrong SHA-256 was accepted");
		printf("integrity check rejects wrong hash: %s\n", err.c_str());
	}

	for (const ModelDef &m : models) {
		PersonDetector det;
		DetectorOptions o;
		o.runtimeDir = widen(VMAF_RUNTIME_DIR);
		o.modelPath = widen(std::string(VMAF_MODELS_DIR) + "/" + m.file);
		o.expectedSha256 = m.sha;
		o.threads = 2;
		std::string err;
		if (!det.load(o, err)) {
			EXPECT(false, "%s failed to load: %s", m.name, err.c_str());
			continue;
		}
		printf("\n== %s  (%s)\n", m.name, det.modelInfo().c_str());
		const int inW = det.inputWidth(), inH = det.inputHeight();
		std::vector<uint8_t> lbuf((size_t)inW * inH * 4);

		for (const Case &c : kCases) {
			std::vector<uint8_t> frame;
			int w = 0, h = 0;
			if (!grab(media + c.file, c.at, frame, w, h)) {
				printf("  SKIP %s (media not found - run tests/fetch-test-media.ps1)\n", c.file);
				continue;
			}
			const LetterboxMap lb = LetterboxMap::compute(w, h, inW, inH);
			letterboxRgba(frame.data(), w, h, w * 4, lbuf.data(), lb);
			std::vector<Detection> dets;
			const auto t0 = std::chrono::steady_clock::now();
			const bool ok = det.detect(lbuf.data(), inW * 4, lb, 0.15f, dets, err);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			EXPECT(ok, "%s detect failed: %s", c.file, err.c_str());
			int confident = 0;
			for (const Detection &d : dets)
				if (d.score >= 0.4f)
					++confident;
			printf("  %-10s @%5.1fs %-30s %d people (>=40%%), %zu raw, %.1f ms\n", c.file, c.at, c.what, confident,
			       dets.size(), ms);
			for (const Detection &d : dets)
				if (d.score >= 0.4f)
					printf("      score %.2f  box x=%4.0f y=%4.0f w=%4.0f h=%4.0f\n", d.score, d.box.x, d.box.y,
					       d.box.w, d.box.h);
			EXPECT(confident >= c.minPeople, "%s @%.1f: expected >= %d people, found %d", c.file, c.at, c.minPeople,
			       confident);
			for (const Detection &d : dets) {
				EXPECT(d.box.x >= 0 && d.box.y >= 0 && d.box.right() <= w + 0.5f && d.box.bottom() <= h + 0.5f,
				       "box outside the image");
				if (d.score >= 0.4f)
					strokeRectRgba(frame, w, h, d.box, 3, 0xFF00E676);
			}
			char name[256];
			snprintf(name, sizeof(name), "%sdet_%s_%.0f_%s.png", out.c_str(), c.file, c.at, m.file);
			writePng(name, frame, w, h);
		}
	}

	if (bench) {
		std::vector<uint8_t> frame;
		int w = 0, h = 0;
		if (!grab(media + "23117.mp4", 2.0, frame, w, h)) {
			printf("bench: media missing\n");
		} else {
			printf("\n== Benchmark (%dx%d frame, 40 runs after 5 warm-up)\n", w, h);
			printf("%-11s %-16s %9s %9s %9s\n", "model", "provider", "avg ms", "p95 ms", "max ms");
			for (const ModelDef &m : models) {
				for (int mode = 0; mode < 4; ++mode) {
					PersonDetector det;
					DetectorOptions o;
					o.runtimeDir = widen(VMAF_RUNTIME_DIR);
					o.modelPath = widen(std::string(VMAF_MODELS_DIR) + "/" + m.file);
					o.threads = mode == 0 ? 1 : mode == 1 ? 2 : 4;
					o.useGpu = mode == 3;
					std::string err;
					if (!det.load(o, err)) {
						printf("%-11s mode %d failed: %s\n", m.name, mode, err.c_str());
						continue;
					}
					if (mode == 3 && det.providerName().find("GPU") == std::string::npos) {
						printf("%-11s %-16s (GPU unavailable: %s)\n", m.name, "DirectML", err.c_str());
						continue;
					}
					const int inW = det.inputWidth(), inH = det.inputHeight();
					std::vector<uint8_t> lbuf((size_t)inW * inH * 4);
					const LetterboxMap lb = LetterboxMap::compute(w, h, inW, inH);
					letterboxRgba(frame.data(), w, h, w * 4, lbuf.data(), lb);
					std::vector<Detection> dets;
					std::vector<double> times;
					for (int i = 0; i < 45; ++i) {
						const auto t0 = std::chrono::steady_clock::now();
						det.detect(lbuf.data(), inW * 4, lb, 0.15f, dets, err);
						const double ms = std::chrono::duration<double, std::milli>(
									  std::chrono::steady_clock::now() - t0)
									  .count();
						if (i >= 5)
							times.push_back(ms);
					}
					std::sort(times.begin(), times.end());
					double avg = 0;
					for (double t : times)
						avg += t;
					avg /= times.size();
					char prov[32];
					if (mode == 3)
						snprintf(prov, sizeof(prov), "DirectML GPU");
					else
						snprintf(prov, sizeof(prov), "CPU %d thread%s", o.threads, o.threads > 1 ? "s" : "");
					printf("%-11s %-16s %9.1f %9.1f %9.1f\n", m.name, prov, avg,
					       times[(size_t)(times.size() * 0.95)], times.back());
				}
			}
		}
	}

	printf("\n%s (%d failure(s))\n", g_fail ? "DETECTOR TEST FAILED" : "DETECTOR TEST PASSED", g_fail);
	shutdownOrtRuntime();
	return g_fail ? 1 : 0;
}
