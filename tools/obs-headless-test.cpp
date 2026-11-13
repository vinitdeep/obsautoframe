// V Media AI AutoFrame - libobs integration test.
//
// Boots the REAL libobs of the installed OBS Studio (Direct3D 11 renderer, no UI), loads the
// built plugin from the build's stage folder, plays real footage through a Media Source with
// the filter attached and captures the composited output frames. Phases:
//   baseline   same footage without the filter (render-cost reference)
//   preacher   Church Preacher preset, overlays on -> review video + live status
//   stress     ~60 rapid settings changes incl. model and GPU switches during playback
//   failure    model that cannot load -> video must keep flowing unchanged
//   multi      two sources with a filter each, one filter removed while rendering
//   resize     the camera changes resolution (720p -> 1080p) while running
//   gpu        DirectML inference
// Exit code 0 = every check passed.
//
//   obs-headless-test <obs bin dir> <plugin stage dir> <media dir> <out dir>
#include <obs.h>
#include <obs-module.h>
#include <util/base.h>
#include <util/platform.h>

#include "../tools/media-pipe.hpp"
#include "settings.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdarg>
#include <filesystem>
#include <thread>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

using namespace vmaf;

namespace {

std::mutex g_logMutex;
std::vector<std::string> g_log;
int g_fail = 0;

#define EXPECT(cond, ...)                                  \
	do {                                               \
		if (!(cond)) {                             \
			++g_fail;                          \
			printf("  FAIL: " __VA_ARGS__);    \
			printf("\n");                      \
		} else {                                   \
			printf("  ok:   " __VA_ARGS__);    \
			printf("\n");                      \
		}                                          \
	} while (0)

void logHandler(int level, const char *fmt, va_list args, void *)
{
	char buf[4096];
	vsnprintf(buf, sizeof(buf), fmt, args);
	std::lock_guard<std::mutex> lock(g_logMutex);
	g_log.emplace_back(buf);
	if (strstr(buf, "vmedia") || level <= LOG_ERROR || strstr(buf, "memory leaks"))
		printf("    [obs] %s\n", buf);
}

size_t logMark()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	return g_log.size();
}

bool logContains(size_t from, const char *needle)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	for (size_t i = from; i < g_log.size(); ++i)
		if (g_log[i].find(needle) != std::string::npos)
			return true;
	return false;
}

struct Capture {
	std::atomic<int> frames{0};
	std::atomic<uint64_t> maxGapNs{0};
	uint64_t lastTs = 0;
	VideoWriter *writer = nullptr;
	std::vector<uint8_t> buf;
	uint32_t w = 0, h = 0;
	std::vector<uint8_t> lastFrame;
	std::mutex mutex;
};

void rawVideo(void *param, struct video_data *frame)
{
	auto *c = static_cast<Capture *>(param);
	if (c->lastTs) {
		const uint64_t gap = frame->timestamp - c->lastTs;
		if (gap > c->maxGapNs)
			c->maxGapNs = gap;
	}
	c->lastTs = frame->timestamp;
	std::lock_guard<std::mutex> lock(c->mutex);
	c->buf.resize((size_t)c->w * c->h * 4);
	// libobs renders into a BGRA texture; convert to RGBA for the ffmpeg writers
	for (uint32_t y = 0; y < c->h; ++y) {
		const uint8_t *s = frame->data[0] + (size_t)y * frame->linesize[0];
		uint8_t *d = &c->buf[(size_t)y * c->w * 4];
		for (uint32_t x = 0; x < c->w; ++x) {
			d[4 * x + 0] = s[4 * x + 2];
			d[4 * x + 1] = s[4 * x + 1];
			d[4 * x + 2] = s[4 * x + 0];
			d[4 * x + 3] = 255;
		}
	}
	c->lastFrame = c->buf;
	if (c->writer)
		c->writer->write(c->buf);
	++c->frames;
}

void setPreset(obs_data_t *s, FramingPreset p)
{
	const PresetDefaults d = presetDefaults(p);
	obs_data_set_int(s, "framing_preset", (int)p);
	obs_data_set_int(s, "applied_framing_preset", (int)p);
	obs_data_set_double(s, "body_fraction", d.composition.bodyFraction * 100);
	obs_data_set_double(s, "subject_fill", d.composition.subjectFill * 100);
	obs_data_set_double(s, "headroom", d.composition.headroom * 100);
	obs_data_set_double(s, "smoothing", d.smoothing);
	obs_data_set_double(s, "tracking_speed", d.trackingSpeed * 100);
	obs_data_set_double(s, "dead_zone_x", d.deadZoneX * 100);
	obs_data_set_double(s, "dead_zone_y", d.deadZoneY * 100);
	obs_data_set_double(s, "max_zoom", d.maxZoom);
	obs_data_set_int(s, "tracking_mode", (int)d.mode);
	obs_data_set_bool(s, "pulpit_enabled", d.pulpit);
}

obs_source_t *mediaSource(const std::string &name, const std::string &file)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_bool(s, "is_local_file", true);
	obs_data_set_string(s, "local_file", file.c_str());
	obs_data_set_bool(s, "looping", true);
	obs_data_set_bool(s, "hw_decode", false);
	obs_data_set_bool(s, "restart_on_activate", true);
	obs_source_t *src = obs_source_create("ffmpeg_source", name.c_str(), s, nullptr);
	obs_data_release(s);
	return src;
}

std::string statusOf(obs_source_t *filter)
{
	obs_properties_t *p = obs_source_properties(filter);
	obs_property_t *st = obs_properties_get(p, "status_info");
	std::string s = st ? obs_property_description(st) : "";
	obs_properties_destroy(p);
	return s;
}

void waitMs(int ms)
{
	Sleep(ms);
}

/* Process CPU usage between two calls, in "% of the whole machine" (Task Manager style). */
struct CpuMeter {
	ULONGLONG cpu0 = 0;
	uint64_t wall0 = 0;
	static ULONGLONG cpuNow()
	{
		FILETIME c, e, k, u;
		GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
		return (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) +
		       (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
	}
	void start()
	{
		cpu0 = cpuNow();
		wall0 = os_gettime_ns();
	}
	double percent() const
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		const double cpuSec = (cpuNow() - cpu0) / 1e7;
		const double wallSec = (os_gettime_ns() - wall0) / 1e9;
		return wallSec > 0 ? 100.0 * cpuSec / wallSec / si.dwNumberOfProcessors : 0.0;
	}
};

// Prints a symbolized stack for any crash (any thread) so failures are diagnosable.
LONG WINAPI crashFilter(EXCEPTION_POINTERS *ep)
{
	HANDLE proc = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	SymInitialize(proc, nullptr, TRUE);
	printf("\n!!! CRASH 0x%08lX at %p (thread %lu)\n", ep->ExceptionRecord->ExceptionCode,
	       ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
	CONTEXT ctx = *ep->ContextRecord;
	STACKFRAME64 sf = {};
	sf.AddrPC.Offset = ctx.Rip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = ctx.Rbp;
	sf.AddrFrame.Mode = AddrModeFlat;
	sf.AddrStack.Offset = ctx.Rsp;
	sf.AddrStack.Mode = AddrModeFlat;
	for (int i = 0; i < 24; ++i) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr,
				 SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
		    !sf.AddrPC.Offset)
			break;
		char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
		auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 255;
		DWORD64 disp = 0;
		IMAGEHLP_LINE64 line = {sizeof(line)};
		DWORD ld = 0;
		char mod[MAX_PATH] = "?";
		HMODULE hm = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				       (LPCSTR)sf.AddrPC.Offset, &hm))
			GetModuleFileNameA(hm, mod, MAX_PATH);
		const char *base = strrchr(mod, '\\');
		const bool hasSym = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym);
		const bool hasLine = SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ld, &line);
		printf("  #%02d %-28s %s%s", i, base ? base + 1 : mod, hasSym ? sym->Name : "?",
		       hasLine ? "" : "\n");
		if (hasLine)
			printf("  (%s:%lu)\n", line.FileName, line.LineNumber);
	}
	fflush(stdout);
	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("usage: obs-headless-test <obs bin dir> <plugin stage dir> <media dir> <out dir>\n");
		return 2;
	}
	setvbuf(stdout, nullptr, _IONBF, 0);
	SetUnhandledExceptionFilter(crashFilter);
	const std::string obsBin = argv[1], stage = argv[2], media = argv[3], out = argv[4];
	const std::string phases = argc > 5 ? argv[5] : "baseline,fidelity,overlay,preacher,stress,failure,multi,resize,gpu";
	auto want = [&](const char *p) { return phases.find(p) != std::string::npos; };
	const std::string pluginDll = stage + "/bin/64bit/vmedia-ai-autoframe.dll";
	std::string pluginData = stage + "/data";
	if (want("noruntime")) {
		// a plugin install whose ONNX Runtime is missing, loaded in a FRESH process (the runtime
		// is process-global): the filter must fail gracefully, never crash OBS
		const std::filesystem::path broken = std::filesystem::path(out) / "stage-noruntime" / "data";
		std::error_code ec;
		std::filesystem::remove_all(broken, ec);
		std::filesystem::create_directories(broken, ec);
		std::filesystem::copy(pluginData, broken, std::filesystem::copy_options::recursive, ec);
		std::filesystem::remove_all(broken / "runtime", ec);
		pluginData = broken.string();
	}
	// Real OBS passes RELATIVE data paths for portable / in-program-folder installs
	// ("../../data/obs-plugins/<name>"); do the same so path handling is tested like in OBS.
	{
		std::error_code ec;
		const auto rel = std::filesystem::relative(pluginData, obsBin, ec);
		if (!ec && !rel.empty())
			pluginData = rel.generic_string();
		printf("plugin data path (relative, as OBS passes it): %s\n", pluginData.c_str());
	}

	// obs.dll is delay-loaded: point the loader (and libobs' relative data paths) at OBS.
	SetDllDirectoryA(obsBin.c_str());
	SetCurrentDirectoryA(obsBin.c_str());
	CreateDirectoryA(out.c_str(), nullptr);

	base_set_log_handler(logHandler, nullptr);
	if (!obs_startup("en-US", nullptr, nullptr)) {
		printf("obs_startup failed\n");
		return 1;
	}
	printf("libobs %s\n", obs_get_version_string());

	struct obs_audio_info ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	obs_reset_audio(&ai);

	struct obs_video_info vi = {};
	vi.graphics_module = "libobs-d3d11";
	vi.fps_num = 30;
	vi.fps_den = 1;
	// the recording phase runs alone on a 1080p canvas, the vertical phase on a 1080x1920
	// canvas (YouTube Shorts / Instagram / Facebook vertical live); everything else uses 720p
	const bool recordRun = want("record");
	const bool verticalRun = want("vertical");
	const uint32_t CW = verticalRun ? 1080 : recordRun ? 1920 : 1280;
	const uint32_t CH = verticalRun ? 1920 : recordRun ? 1080 : 720;
	vi.base_width = vi.output_width = CW;
	vi.base_height = vi.output_height = CH;
	vi.output_format = VIDEO_FORMAT_BGRA; // native format of the libobs output texture
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.scale_type = OBS_SCALE_BICUBIC;
	vi.gpu_conversion = true;
	const int vr = obs_reset_video(&vi);
	if (vr != OBS_VIDEO_SUCCESS) {
		printf("obs_reset_video failed: %d\n", vr);
		return 1;
	}

	obs_module_t *ffmpeg = nullptr;
	int r = obs_open_module(&ffmpeg, "../../core/obs-ffmpeg/obs-ffmpeg.dll", "../../core/obs-ffmpeg/data");
	EXPECT(r == MODULE_SUCCESS && obs_init_module(ffmpeg), "OBS media source module (obs-ffmpeg) loaded");
	obs_module_t *imgmod = nullptr; // still images for the fidelity phase
	if (obs_open_module(&imgmod, "../../core/image-source/image-source.dll", "../../core/image-source/data") ==
	    MODULE_SUCCESS)
		obs_init_module(imgmod);
	obs_module_t *filters = nullptr; // OBS's own filters, used as a control in the multi phase
	if (obs_open_module(&filters, "../../core/obs-filters/obs-filters.dll", "../../core/obs-filters/data") ==
	    MODULE_SUCCESS)
		obs_init_module(filters);
	obs_module_t *plugin = nullptr;
	r = obs_open_module(&plugin, pluginDll.c_str(), pluginData.c_str());
	EXPECT(r == MODULE_SUCCESS && obs_init_module(plugin), "V Media AI AutoFrame module loaded into libobs 33.0.1");

	Capture cap;
	cap.w = CW;
	cap.h = CH;
	obs_add_raw_video_callback(nullptr, rawVideo, &cap);

	auto run = [&](const char *name, obs_source_t *root, int seconds, bool record) -> int {
		VideoWriter vw;
		if (record) {
			vw.open(out + "/obs_" + name + ".mp4", 1280, 720, 30);
			std::lock_guard<std::mutex> lock(cap.mutex);
			cap.writer = &vw;
		}
		cap.maxGapNs = 0;
		cap.lastTs = 0;
		const int f0 = cap.frames.load();
		obs_set_output_source(0, root);
		waitMs(1500); // warm-up (decoder start, model load) excluded from the CPU figure
		CpuMeter cpu;
		cpu.start();
		waitMs(seconds * 1000 - 1500);
		printf("  CPU (whole process incl. video decoding): %.1f%% of all cores\n", cpu.percent());
		obs_set_output_source(0, nullptr);
		waitMs(150);
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			cap.writer = nullptr;
		}
		vw.close();
		return cap.frames.load() - f0;
	};

	// ------------------------------------------------------------------ baseline
	if (want("baseline")) {
		printf("\n== baseline (no filter)\n");
		obs_source_t *src = mediaSource("baseline", media + "/23117.mp4");
		const int n = run("baseline", src, 6, false);
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			writePng(out + "/obs_baseline_last.png", cap.lastFrame, 1280, 720);
		}
		printf("  frames %d, avg render %.2f ms, lagged %u/%u\n", n, obs_get_average_frame_time_ns() / 1e6,
		       obs_get_lagged_frames(), obs_get_total_frames());
		obs_source_release(src);
	}
	const double baseRenderMs = obs_get_average_frame_time_ns() / 1e6;
	const uint32_t lagged0 = obs_get_lagged_frames(), total0 = obs_get_total_frames();

	// ------------------------------------------------------------------ preacher
	if (want("preacher")) {
		printf("\n== preacher (Church Preacher preset, 21 s, overlays on)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("camera", media + "/23117.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Preacher);
		obs_data_set_bool(fs, "pulpit_enabled", false);
		obs_data_set_bool(fs, "show_boxes", true);
		obs_data_set_bool(fs, "show_stats", true);
		obs_data_set_bool(fs, "show_guides", true);
		obs_data_set_bool(fs, "diagnostic_logs", true);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AutoFrame", fs, nullptr);
		obs_data_release(fs);
		EXPECT(filter && strcmp(obs_source_get_id(filter), "vmedia_ai_autoframe_filter") == 0,
		       "filter 'V Media AI AutoFrame' created (%s)", filter ? obs_source_get_display_name("vmedia_ai_autoframe_filter") : "null");
		obs_source_filter_add(src, filter);

		obs_set_output_source(0, src);
		waitMs(8000);
		const std::string st = statusOf(filter);
		printf("  status after 8 s: %s\n", st.c_str());
		obs_set_output_source(0, nullptr);
		const int n = run("preacher", src, 21, true);
		const double renderMs = obs_get_average_frame_time_ns() / 1e6;
		printf("  frames %d, max frame gap %.1f ms, avg render %.2f ms (baseline %.2f ms)\n", n, cap.maxGapNs / 1e6,
		       renderMs, baseRenderMs);
		EXPECT(logContains(mark, "detector ready"), "AI detector started inside OBS");
		EXPECT(st.find("TRACKING") != std::string::npos || st.find("HOLDING") != std::string::npos,
		       "filter reports it is tracking a person");
		EXPECT(n >= 21 * 30 * 0.95, "video kept flowing at 30 fps while AI ran (%d frames)", n);
		EXPECT(cap.maxGapNs < 80000000ull, "no stalls in the video output (max gap %.1f ms)", cap.maxGapNs / 1e6);
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			writePng(out + "/obs_preacher_last.png", cap.lastFrame, 1280, 720);
		}
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	// ------------------------------------------------------------------ stress
	if (want("stress")) {
		printf("\n== stress (rapid settings changes while rendering)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("band", media + "/36706.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Group);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AutoFrame stress", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(src, filter);
		obs_set_output_source(0, src);
		const int f0 = cap.frames.load();
		const uint64_t t0 = os_gettime_ns();
		srand(12345);
		for (int i = 0; i < 60; ++i) {
			obs_data_t *s = obs_source_get_settings(filter);
			setPreset(s, (FramingPreset)(rand() % 10));
			obs_data_set_int(s, "output_aspect", rand() % 6);
			obs_data_set_double(s, "max_zoom", 1.0 + (rand() % 30) / 10.0);
			obs_data_set_double(s, "detection_fps", 1 + rand() % 15);
			obs_data_set_bool(s, "auto_zoom", rand() % 4 != 0);
			obs_data_set_bool(s, "show_boxes", rand() % 2);
			if (i % 20 == 10)
				obs_data_set_int(s, "model", (i / 20) % 2); // nano <-> tiny restart
			if (i == 45)
				obs_data_set_int(s, "threads", 3);
			obs_source_update(filter, s);
			obs_data_release(s);
			waitMs(80 + rand() % 120);
		}
		waitMs(1500);
		obs_set_output_source(0, nullptr);
		const int n = cap.frames.load() - f0;
		const double secs = (os_gettime_ns() - t0) / 1e9;
		EXPECT(n >= (int)(secs * 30 * 0.9), "survived 60 live settings changes (%d frames in %.1f s)", n, secs);
		EXPECT(!logContains(mark, "FAILED"), "no detector failure during model/thread switches");
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	// ------------------------------------------------------------------ failure
	if (want("failure")) {
		printf("\n== failure (model cannot be loaded)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("camera2", media + "/23117.mp4");
		obs_data_t *fs = obs_data_create();
		obs_data_set_int(fs, "model", 2);
		obs_data_set_string(fs, "custom_model_path", "C:/does/not/exist.onnx");
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AutoFrame broken", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(src, filter);
		const int n = run("failure", src, 4, false);
		EXPECT(logContains(mark, "detector FAILED"), "missing model reported in the OBS log");
		EXPECT(n >= 4 * 30 * 0.9, "video still flows with a broken model (%d frames)", n);
		const std::string st = statusOf(filter);
		EXPECT(st.find("unavailable") != std::string::npos, "properties show the failure: %s", st.c_str());
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	// ------------------------------------------------------------------ multi
	if (want("noruntime")) {
		printf("\n== noruntime (ONNX Runtime missing from the install)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("camera-nort", media + "/23117.mp4");
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF noruntime", nullptr, nullptr);
		obs_source_filter_add(src, filter);
		const int n = run("noruntime", src, 5, false);
		const std::string st = statusOf(filter);
		EXPECT(logContains(mark, "runtime folder (ONNX Runtime) is missing"), "missing runtime reported in the log");
		EXPECT(st.find("unavailable") != std::string::npos, "properties explain it: %s", st.c_str());
		EXPECT(n >= 5 * 30 * 0.9, "no crash, video passes through (%d frames)", n);
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	if (want("fidelity")) {
		printf("\n== fidelity (filter at zoom 1.0 must reproduce the input exactly)\n");
		obs_data_t *is = obs_data_create();
		obs_data_set_string(is, "file", (media + "/still_23117.png").c_str());
		obs_source_t *img = obs_source_create("image_source", "still", is, nullptr);
		obs_data_release(is);
		auto grabFrame = [&](std::vector<uint8_t> &dst) {
			obs_set_output_source(0, img);
			waitMs(1500);
			{
				std::lock_guard<std::mutex> lock(cap.mutex);
				dst = cap.lastFrame;
			}
			obs_set_output_source(0, nullptr);
			waitMs(100);
		};
		std::vector<uint8_t> ref, filtered;
		grabFrame(ref);
		obs_data_t *fs = obs_data_create();
		obs_data_set_bool(fs, "auto_tracking", false);
		obs_data_set_bool(fs, "auto_zoom", false);
		obs_data_set_double(fs, "fixed_zoom", 1.0);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF fidelity", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(img, filter);
		grabFrame(filtered);
		double se = 0;
		int maxd = 0;
		const size_t n = std::min(ref.size(), filtered.size());
		for (size_t i = 0; i < n; ++i) {
			if (i % 4 == 3)
				continue;
			const int d = (int)ref[i] - (int)filtered[i];
			se += (double)d * d;
			maxd = std::max(maxd, std::abs(d));
		}
		const double mse = n ? se / (n * 0.75) : 1e9;
		const double psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
		writePng(out + "/obs_fidelity_ref.png", ref, 1280, 720);
		writePng(out + "/obs_fidelity_filtered.png", filtered, 1280, 720);
		EXPECT(n > 0 && psnr >= 45.0, "filtered output matches the input: PSNR %.1f dB, max channel diff %d", psnr, maxd);
		obs_source_filter_remove(img, filter);
		obs_source_release(filter);
		obs_source_release(img);
	}

	if (verticalRun) {
		printf("\n== vertical (1080x1920 canvas, 1080p camera, 9:16 AutoFrame, 30 s x264 encode)\n");
		char self[MAX_PATH];
		GetModuleFileNameA(nullptr, self, MAX_PATH);
		std::string selfDir(self);
		selfDir = selfDir.substr(0, selfDir.find_last_of("\\/"));
		CopyFileA((obsBin + "/obs-ffmpeg-mux.exe").c_str(), (selfDir + "/obs-ffmpeg-mux.exe").c_str(), FALSE);
		const char *oldPath = getenv("PATH");
		SetEnvironmentVariableA("PATH", (obsBin + ";" + (oldPath ? oldPath : "")).c_str());
		obs_module_t *x264 = nullptr;
		if (obs_open_module(&x264, "../../core/obs-x264/obs-x264.dll", "../../core/obs-x264/data") == MODULE_SUCCESS)
			obs_init_module(x264);

		size_t mark = logMark();
		obs_scene_t *scene = obs_scene_create("Vertical");
		obs_source_t *cam = mediaSource("camera-v", media + "/23117_1080p.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Preacher);
		obs_data_set_bool(fs, "pulpit_enabled", false);
		obs_data_set_int(fs, "output_aspect", (int)OutputAspect::A9x16); // output size: Auto
		obs_data_set_double(fs, "sharpen", 30.0);
		obs_data_set_bool(fs, "diagnostic_logs", true);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF vertical", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(cam, filter);
		obs_scene_add(scene, cam); // position 0,0 scale 1: no manual transform needed
		obs_set_output_source(0, obs_scene_get_source(scene));
		waitMs(5000);
		const uint32_t sw = obs_source_get_width(cam), sh = obs_source_get_height(cam);
		EXPECT(sw == 1080 && sh == 1920, "camera + filter is exactly %ux%u (fills the vertical canvas 1:1)", sw, sh);

		std::vector<uint8_t> frame;
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			frame = cap.lastFrame;
		}
		// no black bars: the outermost rows/columns must carry picture content
		auto edgeLuma = [&](bool vertical, uint32_t idx) {
			double s = 0;
			const uint32_t n = vertical ? CH : CW;
			for (uint32_t i = 0; i < n; ++i) {
				const uint32_t x = vertical ? idx : i, y = vertical ? i : idx;
				const uint8_t *p = &frame[4 * ((size_t)y * CW + x)];
				s += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
			}
			return s / n;
		};
		if (!frame.empty()) {
			writePng(out + "/obs_vertical_frame.png", frame, CW, CH);
			const double l = edgeLuma(true, 1), r = edgeLuma(true, CW - 2), t = edgeLuma(false, 1),
				     b = edgeLuma(false, CH - 2);
			EXPECT(l > 12 && r > 12 && t > 12 && b > 12, "no black bars (edge luma L%.0f R%.0f T%.0f B%.0f)", l, r, t, b);
		}

		obs_data_t *es = obs_data_create();
		obs_data_set_string(es, "rate_control", "CBR");
		obs_data_set_int(es, "bitrate", 6000);
		obs_data_set_string(es, "preset", "veryfast");
		obs_data_set_int(es, "keyint_sec", 2);
		obs_encoder_t *venc = obs_video_encoder_create("obs_x264", "v-x264", es, nullptr);
		obs_data_release(es);
		obs_encoder_t *aenc = obs_audio_encoder_create("ffmpeg_aac", "v-aac", nullptr, 0, nullptr);
		obs_encoder_set_video(venc, obs_get_video());
		obs_encoder_set_audio(aenc, obs_get_audio());
		const std::string recPath = out + "/vertical_1080x1920.mp4";
		DeleteFileA(recPath.c_str());
		obs_data_t *os = obs_data_create();
		obs_data_set_string(os, "path", recPath.c_str());
		obs_output_t *rec = obs_output_create("ffmpeg_muxer", "vertical-rec", os, nullptr);
		obs_data_release(os);
		obs_output_set_video_encoder(rec, venc);
		obs_output_set_audio_encoder(rec, aenc, 0);
		const uint32_t lag0 = obs_get_lagged_frames(), tot0 = obs_get_total_frames();
		EXPECT(obs_output_start(rec), "vertical encode started (x264 CBR 6 Mbps, like a live stream)");
		CpuMeter cpu;
		cpu.start();
		waitMs(30000);
		const double cpuPct = cpu.percent();
		const std::string st = statusOf(filter);
		obs_output_stop(rec);
		for (int i = 0; i < 100 && obs_output_active(rec); ++i)
			waitMs(100);
		const int dropped = obs_output_get_frames_dropped(rec);
		const uint32_t lagged = obs_get_lagged_frames() - lag0, rendered = obs_get_total_frames() - tot0;
		printf("  status: %s\n  CPU %.1f%%, encoded %d frames, dropped %d, lagged %u/%u\n", st.c_str(), cpuPct,
		       obs_output_get_total_frames(rec), dropped, lagged, rendered);
		EXPECT(st.find("TRACKING") != std::string::npos || st.find("HOLDING") != std::string::npos,
		       "tracking in the vertical canvas");
		EXPECT(st.find("1080x1920") != std::string::npos, "status shows the 1080x1920 output and upscale factor");
		EXPECT(dropped == 0 && lagged * 100 <= rendered, "no dropped / lagged frames");
		char cmd[1024];
		snprintf(cmd, sizeof(cmd),
			 "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=width,height,nb_read_frames -of csv=p=0 \"%s\"",
			 recPath.c_str());
		std::string probe;
		if (FILE *p = _popen(cmd, "r")) {
			char line[256];
			while (fgets(line, sizeof(line), p))
				probe += line;
			_pclose(p);
		}
		int fw = 0, fh = 0, fn = 0;
		sscanf(probe.c_str(), "%d,%d,%d", &fw, &fh, &fn);
		EXPECT(fw == 1080 && fh == 1920 && fn >= 30 * 30 * 0.97, "encoded file is %dx%d, %d frames", fw, fh, fn);
		obs_set_output_source(0, nullptr);
		obs_output_release(rec);
		obs_encoder_release(venc);
		obs_encoder_release(aenc);
		obs_source_remove(obs_scene_get_source(scene));
		obs_scene_release(scene);
		obs_source_filter_remove(cam, filter);
		obs_source_release(filter);
		obs_source_release(cam);
		(void)mark;
	}

	if (want("scenefilter")) {
		printf("\n== scenefilter (one clean camera -> AutoFrame 16:9 on one scene + AutoFrame 9:16 on another)\n");
		// "Horizontal + vertical at the same time" from ONE camera: the camera source stays
		// clean (a filter on a source applies everywhere the source is used); each format is a
		// scene containing the camera, with its own AutoFrame on the scene.
		obs_source_t *cam = mediaSource("camera-dual", media + "/23117.mp4");
		obs_scene_t *camH = obs_scene_create("Camera (horizontal)");
		obs_scene_add(camH, cam);
		obs_data_t *fh = obs_data_create();
		setPreset(fh, FramingPreset::Preacher);
		obs_data_set_bool(fh, "pulpit_enabled", false);
		obs_source_t *fH = obs_source_create("vmedia_ai_autoframe_filter", "AF 16x9", fh, nullptr);
		obs_data_release(fh);
		obs_source_filter_add(obs_scene_get_source(camH), fH);
		obs_scene_t *camV = obs_scene_create("Camera (vertical)");
		obs_scene_add(camV, cam);
		obs_data_t *fv = obs_data_create();
		setPreset(fv, FramingPreset::Medium);
		obs_data_set_int(fv, "output_aspect", (int)OutputAspect::A9x16);
		obs_source_t *fV = obs_source_create("vmedia_ai_autoframe_filter", "AF 9x16", fv, nullptr);
		obs_data_release(fv);
		obs_source_filter_add(obs_scene_get_source(camV), fV);
		obs_scene_t *main = obs_scene_create("Program");
		obs_scene_add(main, obs_scene_get_source(camH));
		obs_sceneitem_t *iv = obs_scene_add(main, obs_scene_get_source(camV));
		struct vec2 sc;
		vec2_set(&sc, 0.25f, 0.25f);
		obs_sceneitem_set_scale(iv, &sc);
		const int n = run("scenefilter", obs_scene_get_source(main), 10, false);
		const std::string sh = statusOf(fH), sv = statusOf(fV);
		const uint32_t vw = obs_source_get_width(obs_scene_get_source(camV)),
			       vh = obs_source_get_height(obs_scene_get_source(camV));
		printf("  16:9 filter: %s\n  9:16 filter: %s\n", sh.c_str(), sv.c_str());
		EXPECT(vw == 1080 && vh == 1920, "vertical scene outputs %ux%u", vw, vh);
		EXPECT(sh.find("TRACKING") != std::string::npos && sv.find("TRACKING") != std::string::npos,
		       "both the 16:9 and the 9:16 framing track the presenter");
		EXPECT(n >= 10 * 30 * 0.9, "%d frames rendered", n);
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			writePng(out + "/obs_scenefilter.png", cap.lastFrame, CW, CH);
		}
		obs_source_remove(obs_scene_get_source(main));
		obs_scene_release(main);
		obs_source_filter_remove(obs_scene_get_source(camV), fV);
		obs_source_release(fV);
		obs_source_remove(obs_scene_get_source(camV));
		obs_scene_release(camV);
		obs_source_filter_remove(obs_scene_get_source(camH), fH);
		obs_source_release(fH);
		obs_source_remove(obs_scene_get_source(camH));
		obs_scene_release(camH);
		obs_source_release(cam);
	}

	if (recordRun) {
		printf("\n== record (1920x1080 @ 30 fps recording with x264 while the AI runs, 30 s)\n");
		// OBS's MP4 muxer is a helper process that must sit next to the host executable
		char self[MAX_PATH];
		GetModuleFileNameA(nullptr, self, MAX_PATH);
		std::string selfDir(self);
		selfDir = selfDir.substr(0, selfDir.find_last_of("\\/"));
		CopyFileA((obsBin + "/obs-ffmpeg-mux.exe").c_str(), (selfDir + "/obs-ffmpeg-mux.exe").c_str(), FALSE);
		const char *oldPath = getenv("PATH");
		SetEnvironmentVariableA("PATH", (obsBin + ";" + (oldPath ? oldPath : "")).c_str());

		obs_module_t *x264 = nullptr;
		const bool x264ok = obs_open_module(&x264, "../../core/obs-x264/obs-x264.dll", "../../core/obs-x264/data") ==
					    MODULE_SUCCESS &&
				    obs_init_module(x264);
		EXPECT(x264ok, "OBS x264 encoder module loaded");

		obs_source_t *src = mediaSource("camera1080", media + "/23117_1080p.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Preacher);
		obs_data_set_bool(fs, "pulpit_enabled", false);
		obs_data_set_bool(fs, "show_stats", true);
		obs_data_set_bool(fs, "show_boxes", true);
		obs_data_set_bool(fs, "diagnostic_logs", true);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF record", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(src, filter);

		// (overlay safety is proven in the dedicated "overlay" phase)
		obs_set_output_source(0, src);
		waitMs(4000); // AI ready

		obs_data_t *es = obs_data_create();
		obs_data_set_string(es, "rate_control", "CBR");
		obs_data_set_int(es, "bitrate", 6000);
		obs_data_set_string(es, "preset", "veryfast");
		obs_data_set_int(es, "keyint_sec", 2);
		obs_encoder_t *venc = obs_video_encoder_create("obs_x264", "rec-x264", es, nullptr);
		obs_data_release(es);
		obs_encoder_t *aenc = obs_audio_encoder_create("ffmpeg_aac", "rec-aac", nullptr, 0, nullptr);
		obs_encoder_set_video(venc, obs_get_video());
		obs_encoder_set_audio(aenc, obs_get_audio());
		const std::string recPath = out + "/record_1080p30.mp4";
		DeleteFileA(recPath.c_str());
		obs_data_t *os = obs_data_create();
		obs_data_set_string(os, "path", recPath.c_str());
		obs_output_t *rec = obs_output_create("ffmpeg_muxer", "recording", os, nullptr);
		obs_data_release(os);
		obs_output_set_video_encoder(rec, venc);
		obs_output_set_audio_encoder(rec, aenc, 0);

		const bool started = obs_output_start(rec);
		EXPECT(started, "recording started (%s)", started ? "x264 veryfast CBR 6 Mbps" : obs_output_get_last_error(rec));
		waitMs(1500);
		const uint32_t lag0 = obs_get_lagged_frames(), tot0 = obs_get_total_frames();
		CpuMeter cpu;
		cpu.start();
		waitMs(30000);
		const double cpuPct = cpu.percent();
		obs_output_stop(rec);
		for (int i = 0; i < 100 && obs_output_active(rec); ++i)
			waitMs(100);
		const int dropped = obs_output_get_frames_dropped(rec);
		const int encoded = obs_output_get_total_frames(rec);
		const uint32_t lagged = obs_get_lagged_frames() - lag0, rendered = obs_get_total_frames() - tot0;
		obs_set_output_source(0, nullptr);
		printf("  CPU while recording (x264 + AI + decoding): %.1f%% of all cores\n", cpuPct);
		printf("  encoded %d frames, dropped %d, render lagged %u of %u\n", encoded, dropped, lagged, rendered);
		EXPECT(dropped == 0, "no frames dropped by the recording output (%d)", dropped);
		EXPECT(rendered > 0 && lagged * 100 <= rendered, "render lag under 1%% while recording (%u/%u)", lagged, rendered);

		// check the file itself
		char cmd[1024];
		snprintf(cmd, sizeof(cmd),
			 "ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=width,height,avg_frame_rate,nb_read_frames -of csv=p=0 \"%s\"",
			 recPath.c_str());
		std::string probe;
		if (FILE *p = _popen(cmd, "r")) {
			char line[256];
			while (fgets(line, sizeof(line), p))
				probe += line;
			_pclose(p);
		}
		int fw = 0, fh = 0, fn = 0, fr0 = 0, fr1 = 1;
		sscanf(probe.c_str(), "%d,%d,%d/%d,%d", &fw, &fh, &fr0, &fr1, &fn);
		printf("  recording file: %s", probe.c_str());
		EXPECT(fw == 1920 && fh == 1080 && fn >= 30 * 30 * 0.97, "recorded file is 1920x1080 with %d frames (~%.1f s at 30 fps)", fn,
		       fn / 30.0);

		obs_output_release(rec);
		obs_encoder_release(venc);
		obs_encoder_release(aenc);
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	if (want("overlay")) {
		printf("\n== overlay (tracking graphics must never reach the stream / recording)\n");
		// recording plumbing (MP4 muxer helper next to the host exe, x264 module)
		char self[MAX_PATH];
		GetModuleFileNameA(nullptr, self, MAX_PATH);
		std::string selfDir(self);
		selfDir = selfDir.substr(0, selfDir.find_last_of("\\/"));
		CopyFileA((obsBin + "/obs-ffmpeg-mux.exe").c_str(), (selfDir + "/obs-ffmpeg-mux.exe").c_str(), FALSE);
		const char *oldPath = getenv("PATH");
		SetEnvironmentVariableA("PATH", (obsBin + ";" + (oldPath ? oldPath : "")).c_str());
		obs_module_t *x264 = nullptr;
		if (obs_open_module(&x264, "../../core/obs-x264/obs-x264.dll", "../../core/obs-x264/data") == MODULE_SUCCESS)
			obs_init_module(x264);

		auto snapNow = [&](std::vector<uint8_t> &dst) {
			std::lock_guard<std::mutex> lock(cap.mutex);
			dst = cap.lastFrame;
		};
		// mean abs difference in the top-left area where the stats panel and labels are drawn
		auto panelDiff = [&](const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
			if (a.size() != b.size() || a.empty())
				return 999.0;
			double s = 0;
			int n = 0;
			for (uint32_t y = 0; y < CH / 4; ++y)
				for (uint32_t x = 0; x < CW / 3; ++x, ++n)
					for (int c = 0; c < 3; ++c)
						s += std::abs((int)a[4 * ((size_t)y * CW + x) + c] -
							      (int)b[4 * ((size_t)y * CW + x) + c]);
			return s / (3.0 * n);
		};
		// records `ms` milliseconds and returns the first and last frame of the file
		auto recordClip = [&](const std::string &path, int ms, std::vector<uint8_t> &first,
				      std::vector<uint8_t> &last) {
			obs_data_t *es = obs_data_create();
			obs_data_set_string(es, "rate_control", "CRF");
			obs_data_set_int(es, "crf", 12);
			obs_data_set_string(es, "preset", "veryfast");
			obs_encoder_t *venc = obs_video_encoder_create("obs_x264", "ov-x264", es, nullptr);
			obs_data_release(es);
			obs_encoder_t *aenc = obs_audio_encoder_create("ffmpeg_aac", "ov-aac", nullptr, 0, nullptr);
			obs_encoder_set_video(venc, obs_get_video());
			obs_encoder_set_audio(aenc, obs_get_audio());
			DeleteFileA(path.c_str());
			obs_data_t *os = obs_data_create();
			obs_data_set_string(os, "path", path.c_str());
			obs_output_t *rec = obs_output_create("ffmpeg_muxer", "overlay-rec", os, nullptr);
			obs_data_release(os);
			obs_output_set_video_encoder(rec, venc);
			obs_output_set_audio_encoder(rec, aenc, 0);
			obs_output_start(rec);
			waitMs(ms);
			obs_output_stop(rec);
			for (int i = 0; i < 100 && obs_output_active(rec); ++i)
				waitMs(100);
			obs_output_release(rec);
			obs_encoder_release(venc);
			obs_encoder_release(aenc);
			VideoReader r;
			int frames = 0;
			std::vector<uint8_t> f;
			if (r.open(path)) {
				while (r.read(f)) {
					if (!frames++)
						first = f;
					last = f;
				}
			}
			return frames;
		};

		obs_data_t *is = obs_data_create();
		obs_data_set_string(is, "file", (media + "/still_23117.png").c_str());
		obs_source_t *still = obs_source_create("image_source", "still-overlay", is, nullptr);
		obs_data_release(is);
		obs_data_t *fs = obs_data_create(); // overlays OFF first -> clean reference
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF overlay", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(still, filter);
		obs_set_output_source(0, still);
		waitMs(7000); // AI ready + framing settled on the still
		std::vector<uint8_t> clean, idle0, idle1, rf, rl;
		snapNow(clean);
		auto zoomNow = [&]() {
			const std::string st = statusOf(filter);
			const size_t z = st.find("zoom ");
			return z == std::string::npos ? std::string("?") : st.substr(z, 10);
		};
		printf("  clean reference: %s\n", zoomNow().c_str());

		auto setOverlay = [&](bool on) {
			obs_data_t *s = obs_source_get_settings(filter);
			obs_data_set_bool(s, "show_boxes", on);
			obs_data_set_bool(s, "show_stats", on);
			obs_data_set_bool(s, "show_guides", on);
			obs_data_set_int(s, "overlay_where", 1); // value saved by 1.1.0 builds - must be ignored
			obs_source_update(filter, s);
			obs_data_release(s);
		};
		// worst frame of a whole recording, compared with the clean reference
		auto worstFrame = [&](const std::string &path, int &frames) {
			VideoReader r;
			std::vector<uint8_t> f;
			double worst = 0;
			frames = 0;
			if (r.open(path))
				while (r.read(f)) {
					worst = std::max(worst, panelDiff(f, clean));
					++frames;
				}
			return worst;
		};

		// 1) every debug graphic switched ON: none may reach the program output, idle or recording
		setOverlay(true);
		waitMs(1200);
		snapNow(idle0);
		printf("  after enabling overlays: %s\n", zoomNow().c_str());
		const double d0 = panelDiff(idle0, clean);
		EXPECT(d0 < 0.5, "boxes/stats/guides ON: program output identical to clean (diff %.2f)", d0);
		recordClip(out + "/overlay_on.mp4", 3000, rf, rl);
		int n = 0;
		const double w0 = worstFrame(out + "/overlay_on.mp4", n);
		EXPECT(n > 60 && w0 < 6.0, "recording with overlays ON: every one of %d frames clean, incl. the first (worst %.2f)",
		       n, w0);
		if (!rf.empty())
			writePng(out + "/overlay_recorded_first_frame.png", rf, CW, CH);
		writePng(out + "/overlay_clean_ref.png", clean, CW, CH);

		// 2) operator switches overlays OFF and back ON in the middle of a recording
		setOverlay(false);
		waitMs(1200);
		{
			std::thread toggler([&] {
				waitMs(1000);
				setOverlay(true);
				waitMs(800);
				setOverlay(false);
				waitMs(500);
				setOverlay(true);
			});
			recordClip(out + "/overlay_toggled.mp4", 3500, rf, rl);
			toggler.join();
		}
		const double w1 = worstFrame(out + "/overlay_toggled.mp4", n);
		EXPECT(n > 70 && w1 < 6.0, "overlays toggled during a recording: all %d frames clean (worst %.2f)", n, w1);
		(void)idle1;

		obs_set_output_source(0, nullptr);
		obs_source_filter_remove(still, filter);
		obs_source_release(filter);
		obs_source_release(still);
	}

	if (want("scenectl")) { // control: same scene handling, no filter (isolates harness vs plugin)
		printf("\n== scenectl (scene with two media sources, no filter)\n");
		obs_scene_t *scene = obs_scene_create("scene-ctl");
		obs_source_t *a = mediaSource("ctlA", media + "/23521.mp4");
		obs_source_t *b = mediaSource("ctlB", media + "/24068.mp4");
		obs_scene_add(scene, a);
		obs_scene_add(scene, b);
		obs_set_output_source(0, obs_scene_get_source(scene));
		waitMs(3000);
		obs_set_output_source(0, nullptr);
		obs_source_remove(obs_scene_get_source(scene));
		obs_scene_release(scene);
		obs_source_release(a);
		obs_source_release(b);
	}

	if (want("multi")) {
		printf("\n== multi (two instances, remove one while rendering)\n");
		obs_scene_t *scene = obs_scene_create("scene");
		obs_source_t *a = mediaSource("camA", media + "/23521.mp4");
		obs_source_t *b = mediaSource("camB", media + "/24068.mp4");
		obs_sceneitem_t *ia = obs_scene_add(scene, a);
		obs_sceneitem_t *ib = obs_scene_add(scene, b);
		if (!want("noxform")) {
			struct vec2 sc;
			vec2_set(&sc, 0.5f, 0.5f);
			obs_sceneitem_set_scale(ia, &sc);
			obs_sceneitem_set_scale(ib, &sc);
			struct vec2 pos;
			vec2_set(&pos, 640, 0);
			obs_sceneitem_set_pos(ib, &pos);
		}
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Medium);
		obs_data_set_bool(fs, "show_boxes", true);
		// "crop" variant: the same scene with OBS's own built-in Crop/Pad filter instead of ours
		const char *fid = want("obscrop") ? "crop_filter" : "vmedia_ai_autoframe_filter";
		obs_source_t *fa = obs_source_create(fid, "AF A", fs, nullptr);
		obs_source_t *fb = obs_source_create(fid, "AF B", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(a, fa);
		obs_source_filter_add(b, fb);
		obs_set_output_source(0, obs_scene_get_source(scene));
		const int f0 = cap.frames.load();
		waitMs(5000);
		const bool live = !want("nolive");
		if (live) {
			obs_source_filter_remove(b, fb); // removed while rendering
			obs_source_release(fb);
		}
		waitMs(3000);
		obs_set_output_source(0, nullptr);
		if (!live) {
			obs_source_filter_remove(b, fb);
			obs_source_release(fb);
		}
		const int n = cap.frames.load() - f0;
		EXPECT(n >= 8 * 30 * 0.9, "two instances + live removal: %d frames", n);
		{
			std::lock_guard<std::mutex> lock(cap.mutex);
			writePng(out + "/obs_multi_last.png", cap.lastFrame, 1280, 720);
		}
		obs_source_filter_remove(a, fa);
		obs_source_release(fa);
		// OBS 33 teardown order: remove the scene (and its items) before the sources it holds
		obs_source_remove(obs_scene_get_source(scene));
		obs_scene_release(scene);
		obs_source_release(a);
		obs_source_release(b);
	}

	// ------------------------------------------------------------------ resize
	if (want("resize")) {
		printf("\n== resize (camera switches 720p -> 1080p)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("camera3", media + "/23521.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Medium);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF resize", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(src, filter);
		obs_set_output_source(0, src);
		const int f0 = cap.frames.load();
		waitMs(4000);
		obs_data_t *s = obs_data_create();
		obs_data_set_string(s, "local_file", (media + "/23117_1080p.mp4").c_str());
		obs_source_update(src, s);
		obs_data_release(s);
		waitMs(5000);
		const std::string st = statusOf(filter);
		obs_set_output_source(0, nullptr);
		EXPECT(logContains(mark, "source resolution 1920x1080"), "filter noticed the new 1920x1080 resolution");
		EXPECT(cap.frames.load() - f0 >= 9 * 30 * 0.9, "video kept flowing across the resolution change");
		printf("  status: %s\n", st.c_str());
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	// ------------------------------------------------------------------ gpu
	if (want("gpu")) {
		printf("\n== gpu (DirectML)\n");
		size_t mark = logMark();
		obs_source_t *src = mediaSource("camera4", media + "/23521.mp4");
		obs_data_t *fs = obs_data_create();
		setPreset(fs, FramingPreset::Medium);
		obs_data_set_bool(fs, "use_gpu", true);
		obs_data_set_bool(fs, "diagnostic_logs", true);
		obs_source_t *filter = obs_source_create("vmedia_ai_autoframe_filter", "AF gpu", fs, nullptr);
		obs_data_release(fs);
		obs_source_filter_add(src, filter);
		const int n = run("gpu", src, 12, false);
		const std::string st = statusOf(filter);
		printf("  status: %s\n", st.c_str());
		EXPECT(logContains(mark, "DirectML (GPU)"), "inference running on the GPU through DirectML");
		EXPECT(n >= 12 * 30 * 0.9, "video flowing with GPU inference (%d frames)", n);
		obs_source_filter_remove(src, filter);
		obs_source_release(filter);
		obs_source_release(src);
	}

	const uint32_t lagged = obs_get_lagged_frames() - lagged0, total = obs_get_total_frames() - total0;
	printf("\nrender lag over all filtered phases: %u of %u frames\n", lagged, total);
	EXPECT(total == 0 || lagged <= total / 100, "fewer than 1%% of frames lagged");

	obs_remove_raw_video_callback(rawVideo, &cap);
	const size_t mark = logMark();
	obs_shutdown();
	// what the OBS frontend prints as "Number of memory leaks" at exit
	const long leaks = bnum_allocs();
	EXPECT(leaks == 0, "libobs memory leaks after shutdown: %ld", leaks);
	EXPECT(!logContains(mark, "Double destroy"), "clean source teardown at shutdown");

	printf("\n%s (%d failure(s))\n", g_fail ? "OBS INTEGRATION TEST FAILED" : "OBS INTEGRATION TEST PASSED", g_fail);
	return g_fail ? 1 : 0;
}
