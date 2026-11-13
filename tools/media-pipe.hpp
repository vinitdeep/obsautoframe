// Raw RGBA video in/out through ffmpeg pipes (test tooling only, not part of the plugin).
#pragma once

#include "geometry.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vmaf {

inline bool probeVideo(const std::string &path, int &w, int &h, double &fps, double &duration)
{
	const std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate "
				"-show_entries format=duration -of default=nw=1 \"" +
				path + "\"";
	FILE *p = _popen(cmd.c_str(), "r");
	if (!p)
		return false;
	char line[256];
	w = h = 0;
	fps = 0;
	duration = 0;
	while (fgets(line, sizeof(line), p)) {
		int a = 0, b = 1;
		double d = 0;
		if (sscanf(line, "width=%d", &a) == 1)
			w = a;
		else if (sscanf(line, "height=%d", &a) == 1)
			h = a;
		else if (sscanf(line, "r_frame_rate=%d/%d", &a, &b) == 2 && b)
			fps = (double)a / b;
		else if (sscanf(line, "duration=%lf", &d) == 1)
			duration = d;
	}
	_pclose(p);
	return w > 0 && h > 0;
}

class VideoReader {
public:
	~VideoReader() { close(); }
	bool open(const std::string &path, double startSec = 0.0, double fps = 0.0, int maxFrames = 0)
	{
		double srcFps = 0, dur = 0;
		if (!probeVideo(path, w_, h_, srcFps, dur))
			return false;
		fps_ = fps > 0 ? fps : srcFps;
		char ss[64];
		snprintf(ss, sizeof(ss), "%.3f", startSec);
		std::string cmd = "ffmpeg -v error -ss " + std::string(ss) + " -i \"" + path + "\"";
		if (fps > 0) {
			char f[64];
			snprintf(f, sizeof(f), " -vf fps=%.3f", fps);
			cmd += f;
		}
		if (maxFrames > 0)
			cmd += " -frames:v " + std::to_string(maxFrames);
		cmd += " -f rawvideo -pix_fmt rgba -";
		p_ = _popen(cmd.c_str(), "rb");
		return p_ != nullptr;
	}
	bool read(std::vector<uint8_t> &frame)
	{
		if (!p_)
			return false;
		frame.resize((size_t)w_ * h_ * 4);
		return fread(frame.data(), 1, frame.size(), p_) == frame.size();
	}
	void close()
	{
		if (p_)
			_pclose(p_);
		p_ = nullptr;
	}
	int width() const { return w_; }
	int height() const { return h_; }
	double fps() const { return fps_; }

private:
	FILE *p_ = nullptr;
	int w_ = 0, h_ = 0;
	double fps_ = 0;
};

class VideoWriter {
public:
	~VideoWriter() { close(); }
	bool open(const std::string &path, int w, int h, double fps)
	{
		w_ = w;
		h_ = h;
		char cmd[1024];
		snprintf(cmd, sizeof(cmd),
			 "ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %.3f -i - -c:v libx264 -preset veryfast "
			 "-crf 20 -pix_fmt yuv420p \"%s\"",
			 w, h, fps, path.c_str());
		p_ = _popen(cmd, "wb");
		return p_ != nullptr;
	}
	bool write(const std::vector<uint8_t> &frame)
	{
		return p_ && fwrite(frame.data(), 1, frame.size(), p_) == frame.size();
	}
	void close()
	{
		if (p_)
			_pclose(p_);
		p_ = nullptr;
	}

private:
	FILE *p_ = nullptr;
	int w_ = 0, h_ = 0;
};

inline bool writePng(const std::string &path, const std::vector<uint8_t> &rgba, int w, int h)
{
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s %dx%d -i - -frames:v 1 \"%s\"", w, h,
		 path.c_str());
	FILE *p = _popen(cmd, "wb");
	if (!p)
		return false;
	fwrite(rgba.data(), 1, rgba.size(), p);
	return _pclose(p) == 0;
}

/* --- tiny drawing helpers for annotated output frames */
inline void fillRectRgba(std::vector<uint8_t> &img, int w, int h, int x0, int y0, int x1, int y1, uint32_t argb)
{
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(w, x1);
	y1 = std::min(h, y1);
	const uint8_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x) {
			uint8_t *p = &img[4 * ((size_t)y * w + x)];
			p[0] = r;
			p[1] = g;
			p[2] = b;
		}
}

inline void strokeRectRgba(std::vector<uint8_t> &img, int w, int h, const RectF &r, int t, uint32_t argb)
{
	const int x0 = (int)r.x, y0 = (int)r.y, x1 = (int)r.right(), y1 = (int)r.bottom();
	fillRectRgba(img, w, h, x0, y0, x1, y0 + t, argb);
	fillRectRgba(img, w, h, x0, y1 - t, x1, y1, argb);
	fillRectRgba(img, w, h, x0, y0, x0 + t, y1, argb);
	fillRectRgba(img, w, h, x1 - t, y0, x1, y1, argb);
}

} // namespace vmaf
