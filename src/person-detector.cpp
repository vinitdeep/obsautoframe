// V Media AI AutoFrame - ONNX Runtime person detector (YOLOX).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "person-detector.hpp"
#include "yolox.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

// ORT_API_MANUAL_INIT is defined project-wide: we never link onnxruntime.lib, the
// runtime is loaded on demand so a missing/broken runtime cannot stop OBS from starting.
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <onnxruntime_session_options_config_keys.h>

#include <cstdio>
#include <mutex>

namespace vmaf {

namespace {
std::mutex g_ortMutex;
HMODULE g_ortModule = nullptr;
const OrtApi *g_api = nullptr;
Ort::Env *g_env = nullptr;
std::string g_loadError;

/* OBS hands out RELATIVE data paths ("../../data/obs-plugins/...") for portable and
 * in-program-folder installs. LoadLibraryExW(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR) rejects
 * relative paths, so everything is made absolute first. */
std::wstring absolutePath(const std::wstring &p)
{
	if (p.empty())
		return p;
	const DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
	if (!n)
		return p;
	std::wstring out(n, L'\0');
	const DWORD m = GetFullPathNameW(p.c_str(), n, out.data(), nullptr);
	out.resize(m);
	return out;
}

std::string narrow(const std::wstring &w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
	return s;
}
} // namespace

bool loadOrtRuntime(const std::wstring &runtimeDir, std::string &err)
{
	std::lock_guard<std::mutex> lock(g_ortMutex);
	if (g_env)
		return true;
	if (!g_loadError.empty()) {
		err = g_loadError;
		return false;
	}
	if (runtimeDir.empty()) {
		// obs_module_file() returns nothing when data\runtime does not exist
		err = "the plugin's data\\runtime folder (ONNX Runtime) is missing - please reinstall the plugin";
		return false; // not cached: a reinstall + filter re-add can recover without restarting OBS
	}
	const std::wstring dll = absolutePath(runtimeDir + L"\\onnxruntime.dll");
	// LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR: DirectML.dll is resolved from the same private folder
	g_ortModule = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!g_ortModule) {
		char buf[64];
		snprintf(buf, sizeof(buf), " (Windows error %lu)", GetLastError());
		g_loadError = "Could not load ONNX Runtime from " + narrow(dll) + buf;
		err = g_loadError;
		return false;
	}
	using GetApiBaseFn = const OrtApiBase *(ORT_API_CALL *)(void);
	auto getApiBase = reinterpret_cast<GetApiBaseFn>(GetProcAddress(g_ortModule, "OrtGetApiBase"));
	const OrtApiBase *base = getApiBase ? getApiBase() : nullptr;
	g_api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
	if (!g_api) {
		g_loadError = std::string("ONNX Runtime ") + (base ? base->GetVersionString() : "?") +
			      " does not provide API version " + std::to_string(ORT_API_VERSION);
		err = g_loadError;
		return false;
	}
	Ort::InitApi(g_api);
	try {
		g_env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "vmedia-autoframe");
		g_env->DisableTelemetryEvents(); // no telemetry, ever
	} catch (const Ort::Exception &e) {
		g_loadError = std::string("ONNX Runtime environment failed: ") + e.what();
		err = g_loadError;
		g_env = nullptr;
		return false;
	}
	return true;
}

void shutdownOrtRuntime()
{
	std::lock_guard<std::mutex> lock(g_ortMutex);
	delete g_env;
	g_env = nullptr;
	// onnxruntime.dll stays loaded until process exit: unloading it while DirectML or
	// thread-pool threads may still be winding down is a classic shutdown crash.
}

std::string sha256File(const std::wstring &path)
{
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			       FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return {};
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	std::string hex;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
	    BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
		std::vector<unsigned char> buf(1 << 20);
		DWORD got = 0;
		bool ok = true;
		while (ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got > 0) {
			if (BCryptHashData(hash, buf.data(), got, 0) != 0) {
				ok = false;
				break;
			}
		}
		unsigned char digest[32];
		if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
			static const char *k = "0123456789abcdef";
			for (unsigned char c : digest) {
				hex.push_back(k[c >> 4]);
				hex.push_back(k[c & 15]);
			}
		}
	}
	if (hash)
		BCryptDestroyHash(hash);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	CloseHandle(f);
	return hex;
}

/* Every ORT object lives in a unique_ptr that is only filled after the runtime loaded:
 * the ORT C++ wrappers call into the ORT API even when destroying an EMPTY handle, which
 * would crash if onnxruntime.dll never loaded. */
struct PersonDetector::Impl {
	std::unique_ptr<Ort::Session> session;
	std::unique_ptr<Ort::MemoryInfo> mem;
	std::string inputName, outputName;
	int inW = 0, inH = 0;
	std::vector<float> input;
	std::vector<Detection> raw;
	std::string provider = "CPU";
	std::string info;
};

PersonDetector::PersonDetector() : impl_(std::make_unique<Impl>()) {}
PersonDetector::~PersonDetector() = default;

bool PersonDetector::loaded() const
{
	return impl_->session != nullptr;
}
int PersonDetector::inputWidth() const
{
	return impl_->inW;
}
int PersonDetector::inputHeight() const
{
	return impl_->inH;
}
const std::string &PersonDetector::providerName() const
{
	return impl_->provider;
}
const std::string &PersonDetector::modelInfo() const
{
	return impl_->info;
}

bool PersonDetector::load(const DetectorOptions &opts, std::string &err)
{
	if (!loadOrtRuntime(opts.runtimeDir, err))
		return false;

	if (GetFileAttributesW(opts.modelPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		err = "Model file not found: " + narrow(opts.modelPath);
		return false;
	}
	if (!opts.expectedSha256.empty()) {
		const std::string sha = sha256File(opts.modelPath);
		if (sha != opts.expectedSha256) {
			err = "Model file failed integrity check (SHA-256 mismatch): " + narrow(opts.modelPath) +
			      ". Reinstall the plugin.";
			return false;
		}
	}

	try {
		auto makeOptions = [&](bool gpu, std::string &provider) {
			Ort::SessionOptions so;
			so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
			so.SetIntraOpNumThreads(std::max(1, opts.threads));
			so.SetInterOpNumThreads(1);
			so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
			// Busy-waiting worker threads would steal CPU from the OBS encoder.
			so.AddConfigEntry(kOrtSessionOptionsConfigAllowIntraOpSpinning, "0");
			so.AddConfigEntry(kOrtSessionOptionsConfigAllowInterOpSpinning, "0");
			provider = "CPU";
			if (gpu) {
				const OrtDmlApi *dml = nullptr;
				Ort::ThrowOnError(g_api->GetExecutionProviderApi("DML", ORT_API_VERSION,
										 reinterpret_cast<const void **>(&dml)));
				so.DisableMemPattern(); // required by DirectML
				OrtDmlDeviceOptions dev{};
				dev.Preference = OrtDmlPerformancePreference::HighPerformance; // dGPU on Optimus laptops
				dev.Filter = OrtDmlDeviceFilter::Gpu;
				Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML2(so, &dev));
				provider = "DirectML (GPU)";
			}
			return so;
		};

		std::string provider;
		if (opts.useGpu) {
			try {
				Ort::SessionOptions so = makeOptions(true, provider);
				impl_->session = std::make_unique<Ort::Session>(*g_env, opts.modelPath.c_str(), so);
			} catch (const Ort::Exception &e) {
				impl_->session.reset();
				err = std::string("GPU (DirectML) unavailable, using CPU: ") + e.what();
			}
		}
		if (!impl_->session) {
			Ort::SessionOptions so = makeOptions(false, provider);
			impl_->session = std::make_unique<Ort::Session>(*g_env, opts.modelPath.c_str(), so);
		}
		impl_->provider = provider;

		Ort::AllocatorWithDefaultOptions alloc;
		if (impl_->session->GetInputCount() != 1 || impl_->session->GetOutputCount() < 1)
			throw std::runtime_error("unexpected model signature (need 1 input, >=1 output)");
		impl_->inputName = impl_->session->GetInputNameAllocated(0, alloc).get();
		impl_->outputName = impl_->session->GetOutputNameAllocated(0, alloc).get();
		const auto shape = impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
		if (shape.size() != 4 || shape[1] != 3 || shape[2] <= 0 || shape[3] <= 0)
			throw std::runtime_error("model input must be [1,3,H,W] with a fixed size");
		impl_->inH = (int)shape[2];
		impl_->inW = (int)shape[3];
		impl_->input.assign((size_t)3 * impl_->inW * impl_->inH, 0.0f);
		impl_->mem = std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
		char buf[128];
		snprintf(buf, sizeof(buf), "%dx%d input, %s", impl_->inW, impl_->inH, impl_->provider.c_str());
		impl_->info = buf;
	} catch (const std::exception &e) {
		impl_->session.reset();
		err = std::string("Model load failed: ") + e.what();
		return false;
	}
	return true;
}

bool PersonDetector::detect(const uint8_t *rgba, int stride, const LetterboxMap &lb, float scoreThresh,
			    std::vector<Detection> &out, std::string &err)
{
	out.clear();
	if (!impl_->session || !impl_->mem) {
		err = "detector not loaded";
		return false;
	}
	Impl &d = *impl_;
	rgbaToBgrPlanar(rgba, d.inW, d.inH, stride, d.input.data());
	try {
		const int64_t shape[4] = {1, 3, d.inH, d.inW};
		Ort::Value in = Ort::Value::CreateTensor<float>(*d.mem, d.input.data(), d.input.size(), shape, 4);
		const char *inNames[] = {d.inputName.c_str()};
		const char *outNames[] = {d.outputName.c_str()};
		auto outputs = d.session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
		const auto info = outputs[0].GetTensorTypeAndShapeInfo();
		const auto oshape = info.GetShape();
		if (oshape.size() != 3) {
			err = "unexpected output rank";
			return false;
		}
		YoloxParams p;
		p.inputW = d.inW;
		p.inputH = d.inH;
		p.scoreThresh = scoreThresh; // caller passes the tracker's LOW threshold (ByteTrack stage 2)
		p.numClasses = (int)oshape[2] - 5;
		if (!decodeYolox(outputs[0].GetTensorData<float>(), (int)oshape[1], (int)oshape[2], p, d.raw)) {
			err = "model output does not match YOLOX layout";
			return false;
		}
	} catch (const std::exception &e) {
		err = std::string("inference failed: ") + e.what();
		return false;
	}

	const float minH = 0.03f * (float)lb.srcH; // ignore specks: nobody is framed that small
	for (const Detection &r : d.raw) {
		Detection s;
		s.box = lb.toSource(r.box);
		s.score = r.score;
		if (s.box.empty() || s.box.h < minH)
			continue;
		out.push_back(s);
	}
	return true;
}

} // namespace vmaf
