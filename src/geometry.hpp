// V Media AI AutoFrame - geometry primitives shared by detector, tracker and framing.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>

namespace vmaf {

struct RectF {
	float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

	constexpr RectF() = default;
	constexpr RectF(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

	static RectF fromCenter(float cx, float cy, float w, float h) { return {cx - w * 0.5f, cy - h * 0.5f, w, h}; }

	float cx() const { return x + w * 0.5f; }
	float cy() const { return y + h * 0.5f; }
	float right() const { return x + w; }
	float bottom() const { return y + h; }
	float area() const { return std::max(0.0f, w) * std::max(0.0f, h); }
	bool empty() const { return !(w > 0.0f && h > 0.0f); }
};

inline RectF intersect(const RectF &a, const RectF &b)
{
	const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
	const float x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
	if (x1 <= x0 || y1 <= y0)
		return {};
	return {x0, y0, x1 - x0, y1 - y0};
}

inline RectF unite(const RectF &a, const RectF &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;
	const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
	const float x1 = std::max(a.right(), b.right()), y1 = std::max(a.bottom(), b.bottom());
	return {x0, y0, x1 - x0, y1 - y0};
}

inline float iou(const RectF &a, const RectF &b)
{
	const float inter = intersect(a, b).area();
	const float uni = a.area() + b.area() - inter;
	return uni > 0.0f ? inter / uni : 0.0f;
}

/* Maps between source pixels and the letterboxed detector input.
 * YOLOX convention: the image is scaled uniformly and placed at the TOP-LEFT
 * corner of the network input; the remainder is padded with grey (114). */
struct LetterboxMap {
	float scale = 1.0f;
	int srcW = 0, srcH = 0;
	int dstW = 0, dstH = 0;

	static LetterboxMap compute(int srcW, int srcH, int dstW, int dstH)
	{
		LetterboxMap m;
		m.srcW = srcW;
		m.srcH = srcH;
		m.dstW = dstW;
		m.dstH = dstH;
		if (srcW > 0 && srcH > 0)
			m.scale = std::min((float)dstW / (float)srcW, (float)dstH / (float)srcH);
		return m;
	}

	int contentW() const { return std::clamp((int)std::lround(srcW * scale), 1, std::max(1, dstW)); }
	int contentH() const { return std::clamp((int)std::lround(srcH * scale), 1, std::max(1, dstH)); }

	RectF toSource(const RectF &r) const
	{
		const float inv = scale > 0.0f ? 1.0f / scale : 0.0f;
		RectF s{r.x * inv, r.y * inv, r.w * inv, r.h * inv};
		// clip to the source image
		return intersect(s, RectF{0.0f, 0.0f, (float)srcW, (float)srcH});
	}

	RectF toInput(const RectF &r) const { return {r.x * scale, r.y * scale, r.w * scale, r.h * scale}; }
};

/* Largest crop height with the given aspect (w/h) that fits inside the source. */
inline float maxCropHeight(float aspect, float srcW, float srcH)
{
	if (aspect <= 0.0f)
		return srcH;
	return std::min(srcH, srcW / aspect);
}

/* Builds a crop rectangle from center + height and the output aspect. */
inline RectF cropRect(float cx, float cy, float h, float aspect)
{
	return RectF::fromCenter(cx, cy, h * aspect, h);
}

/* Clamps a crop (center/height) so that it lies completely inside the source:
 * never shows anything outside the camera image, so no black edges. */
inline void clampCrop(float &cx, float &cy, float &h, float aspect, float srcW, float srcH)
{
	const float hmax = maxCropHeight(aspect, srcW, srcH);
	h = std::clamp(h, 1.0f, hmax);
	const float w = h * aspect;
	cx = std::clamp(cx, w * 0.5f, srcW - w * 0.5f);
	cy = std::clamp(cy, h * 0.5f, srcH - h * 0.5f);
}

inline float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

} // namespace vmaf
