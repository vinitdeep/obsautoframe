// V Media AI AutoFrame - offline harness.
//
// Runs the plugin's real detector + tracker + framing controller on a video file (no OBS),
// and writes a review video: left = camera with detections (green = followed person,
// yellow = others) and the virtual camera crop (red); right = the framed output.
//
//   autoframe-sim input.mp4 output.mp4 [options]
//     --preset N        0 Wide 1 FullBody 2 Medium 3 Upper 4 CloseUp 5 Face 6 Preacher
//                       7 Worship 8 Interview 9 Group             (default 2)
//     --model nano|tiny (default nano)    --gpu    --threads N (default 2)
//     --det-fps F       detections per second (default 5)
//     --max-zoom Z      --policy N (0 largest 1 center 2 confident)
//     --blank A-B       hide the camera from the AI between A and B seconds (occlusion test)
//     --stage L,T,R,B   stage area in percent           --ignore-bottom
//     --pulpit X,W      pulpit anchor center/width in percent
//     --aspect 9x16     vertical output              --start S  --duration S
//     --csv path        per-frame log
#include "../tools/media-pipe.hpp"
#include "framing-controller.hpp"
#include "person-detector.hpp"
#include "person-tracker.hpp"
#include "settings.hpp"
#include "yolox.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vmaf;

namespace {

void sampleBilinear(const std::vector<uint8_t> &src, int sw, int sh, const RectF &crop, std::vector<uint8_t> &dst,
		    int dw, int dh, int dstX, int dstStride)
{
	for (int y = 0; y < dh; ++y) {
		const float sy = std::clamp(crop.y + (y + 0.5f) * crop.h / dh - 0.5f, 0.0f, (float)sh - 1.001f);
		const int y0 = (int)sy;
		const float fy = sy - y0;
		for (int x = 0; x < dw; ++x) {
			const float sx = std::clamp(crop.x + (x + 0.5f) * crop.w / dw - 0.5f, 0.0f, (float)sw - 1.001f);
			const int x0 = (int)sx;
			const float fx = sx - x0;
			const uint8_t *p00 = &src[4 * ((size_t)y0 * sw + x0)];
			const uint8_t *p01 = p00 + 4;
			const uint8_t *p10 = p00 + 4 * (size_t)sw;
			const uint8_t *p11 = p10 + 4;
			uint8_t *o = &dst[4 * ((size_t)y * dstStride + dstX + x)];
			for (int c = 0; c < 4; ++c) {
				const float a = p00[c] + (p01[c] - p00[c]) * fx;
				const float b = p10[c] + (p11[c] - p10[c]) * fx;
				o[c] = (uint8_t)(a + (b - a) * fy + 0.5f);
			}
		}
	}
}

bool arg(int &i, int argc, char **argv, const char *name, std::string &out)
{
	if (strcmp(argv[i], name) == 0 && i + 1 < argc) {
		out = argv[++i];
		return true;
	}
	return false;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("usage: autoframe-sim input.mp4 output.mp4 [options]  (see source header)\n");
		return 2;
	}
	const std::string input = argv[1], output = argv[2];
	AutoFrameConfig cfg;
	applyPreset(cfg, FramingPreset::Medium);
	std::string model = "nano", csvPath, v;
	double blankA = -1, blankB = -1, start = 0, duration = 0;
	for (int i = 3; i < argc; ++i) {
		if (arg(i, argc, argv, "--preset", v))
			applyPreset(cfg, (FramingPreset)std::clamp(atoi(v.c_str()), 0, 9));
		else if (arg(i, argc, argv, "--model", v))
			model = v;
		else if (strcmp(argv[i], "--gpu") == 0)
			cfg.useGpu = true;
		else if (arg(i, argc, argv, "--threads", v))
			cfg.threads = atoi(v.c_str());
		else if (arg(i, argc, argv, "--det-fps", v))
			cfg.detectionFps = (float)atof(v.c_str());
		else if (arg(i, argc, argv, "--max-zoom", v))
			cfg.maxZoom = (float)atof(v.c_str());
		else if (arg(i, argc, argv, "--max-enlarge", v))
			cfg.maxEnlarge = (float)atof(v.c_str());
		else if (arg(i, argc, argv, "--policy", v))
			cfg.policy = (TargetPolicy)atoi(v.c_str());
		else if (arg(i, argc, argv, "--blank", v))
			sscanf(v.c_str(), "%lf-%lf", &blankA, &blankB);
		else if (arg(i, argc, argv, "--stage", v)) {
			float l, t, r, b;
			if (sscanf(v.c_str(), "%f,%f,%f,%f", &l, &t, &r, &b) == 4) {
				cfg.stageLeft = l / 100;
				cfg.stageTop = t / 100;
				cfg.stageRight = r / 100;
				cfg.stageBottom = b / 100;
			}
		} else if (strcmp(argv[i], "--ignore-bottom") == 0)
			cfg.ignoreBottomEdge = true;
		else if (arg(i, argc, argv, "--pulpit", v)) {
			float x, w;
			if (sscanf(v.c_str(), "%f,%f", &x, &w) == 2) {
				cfg.pulpitEnabled = true;
				cfg.pulpitX = x / 100;
				cfg.pulpitWidth = w / 100;
			}
		} else if (arg(i, argc, argv, "--aspect", v))
			cfg.aspect = v == "9x16" ? OutputAspect::A9x16 : v == "1x1" ? OutputAspect::A1x1 : OutputAspect::Source;
		else if (arg(i, argc, argv, "--start", v))
			start = atof(v.c_str());
		else if (arg(i, argc, argv, "--duration", v))
			duration = atof(v.c_str());
		else if (arg(i, argc, argv, "--csv", v))
			csvPath = v;
		else
			printf("ignoring unknown option %s\n", argv[i]);
	}

	VideoReader in;
	if (!in.open(input, start)) {
		printf("cannot open %s\n", input.c_str());
		return 1;
	}
	const int W = in.width(), H = in.height();
	const double fps = in.fps() > 0 ? in.fps() : 30.0;

	PersonDetector det;
	DetectorOptions o;
	const std::string modelFile = std::string(VMAF_MODELS_DIR) + (model == "tiny" ? "/yolox_tiny.onnx" : "/yolox_nano.onnx");
	o.runtimeDir = VMAF_RUNTIME_DIR;
	o.modelPath = modelFile;
	o.threads = cfg.threads;
	o.useGpu = cfg.useGpu;
	std::string err;
	if (!det.load(o, err)) {
		printf("detector: %s\n", err.c_str());
		return 1;
	}
	printf("%s: %dx%d @ %.2f fps | %s | preset %s | %.1f det/s\n", input.c_str(), W, H, fps, det.modelInfo().c_str(),
	       presetName(cfg.preset), cfg.detectionFps);

	PersonTracker tracker;
	TrackerConfig tc;
	tc.highThresh = cfg.confidence;
	tc.lowThresh = std::max(0.08f, cfg.confidence * 0.4f);
	tc.maxLostTime = std::max(4.0f, cfg.lostTimeout + 2.0f);
	tracker.configure(tc);
	FramingController fr;
	fr.configure(cfg);
	fr.setSourceSize((float)W, (float)H);

	// review video: two 640-px-tall panels
	const int PH = 360;
	const int PW0 = (int)std::lround(PH * (double)W / H) & ~1;
	const float aspect = aspectValue(cfg.aspect, (float)W, (float)H);
	const int PW1 = (int)std::lround(PH * aspect) & ~1;
	const int OW = PW0 + 8 + PW1;
	VideoWriter outv;
	if (!outv.open(output, OW, PH, fps)) {
		printf("cannot write %s\n", output.c_str());
		return 1;
	}
	FILE *csv = csvPath.empty() ? nullptr : fopen(csvPath.c_str(), "w");
	if (csv)
		fprintf(csv, "t,status,target,zoom,crop_x,crop_y,crop_w,crop_h,subject_cx,subject_cy,people,infer_ms\n");

	const LetterboxMap lb = LetterboxMap::compute(W, H, det.inputWidth(), det.inputHeight());
	std::vector<uint8_t> frame, lbuf((size_t)det.inputWidth() * det.inputHeight() * 4), canvas((size_t)OW * PH * 4);
	std::vector<Detection> dets;
	double t = 0, nextDet = 0, inferTotal = 0, inferMax = 0;
	int nInfer = 0, frames = 0, subjectFrames = 0, insideFrames = 0;
	float lastInfer = 0, maxPan = 0, prevCx = -1;
	const double dt = 1.0 / fps;

	while (in.read(frame)) {
		t += dt;
		if (duration > 0 && t > duration)
			break;
		++frames;
		const bool blank = t >= blankA && t <= blankB;
		if (t >= nextDet) {
			nextDet += 1.0 / cfg.detectionFps;
			if (blank) {
				dets.clear();
			} else {
				letterboxRgba(frame.data(), W, H, W * 4, lbuf.data(), lb);
				const auto t0 = std::chrono::steady_clock::now();
				det.detect(lbuf.data(), det.inputWidth() * 4, lb, tc.lowThresh, dets, err);
				lastInfer = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
				inferTotal += lastInfer;
				inferMax = std::max(inferMax, (double)lastInfer);
				++nInfer;
			}
			tracker.update(dets, t);
		}
		fr.update(tracker, t, (float)dt, true);
		const FramingOutput &out = fr.output();
		if (prevCx >= 0)
			maxPan = std::max(maxPan, std::fabs(out.crop.cx() - prevCx));
		prevCx = out.crop.cx();
		if (out.hasSubject) {
			++subjectFrames;
			if (out.subject.cx() > out.crop.x && out.subject.cx() < out.crop.right())
				++insideFrames;
		}

		// annotate the camera view
		std::vector<uint8_t> annotated = frame;
		const int th = std::max(2, H / 270);
		if (cfg.pulpitEnabled)
			strokeRectRgba(annotated, W, H, fr.pulpitRect(), th, 0xFF1E90FF);
		for (const Track &k : tracker.tracks()) {
			if (k.state == TrackState::Tentative)
				continue;
			const bool target = k.id == out.targetId ||
					    std::find(out.groupIds.begin(), out.groupIds.end(), k.id) != out.groupIds.end();
			const uint32_t col = !fr.isPresent(k, t) ? 0xFF8A8A8A : target ? 0xFF00E676 : 0xFFFFC400;
			strokeRectRgba(annotated, W, H, k.box, target ? th * 2 : th, col);
		}
		strokeRectRgba(annotated, W, H, out.crop, th * 2, 0xFFFF2020);
		if (blank)
			fillRectRgba(annotated, W, H, 0, 0, W, H / 20, 0xFF000000);

		std::fill(canvas.begin(), canvas.end(), (uint8_t)0);
		sampleBilinear(annotated, W, H, RectF{0, 0, (float)W, (float)H}, canvas, PW0, PH, 0, OW);
		sampleBilinear(frame, W, H, out.crop, canvas, PW1, PH, PW0 + 8, OW);
		for (int y = 0; y < PH; ++y)
			for (int x = 0; x < OW; ++x)
				canvas[4 * ((size_t)y * OW + x) + 3] = 255;
		outv.write(canvas);

		if (csv)
			fprintf(csv, "%.3f,%s,%d,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%zu,%.1f\n", t, statusName(out.status),
				out.targetId, out.zoom, out.crop.x, out.crop.y, out.crop.w, out.crop.h, out.subject.cx(),
				out.subject.cy(), tracker.tracks().size(), lastInfer);
	}
	outv.close();
	if (csv)
		fclose(csv);

	printf("frames %d | inference %d runs, avg %.1f ms, max %.1f ms | subject framed in %d/%d frames "
	       "(%.1f%% of frames with a subject had it inside the crop) | max pan %.1f px/frame\n",
	       frames, nInfer, nInfer ? inferTotal / nInfer : 0.0, inferMax, subjectFrames, frames,
	       subjectFrames ? 100.0 * insideFrames / subjectFrames : 0.0, maxPan);
	printf("wrote %s\n", output.c_str());
	shutdownOrtRuntime();
	return 0;
}
