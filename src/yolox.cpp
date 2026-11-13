// V Media AI AutoFrame - YOLOX pre/post-processing.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "yolox.hpp"

#include <algorithm>
#include <cmath>

namespace vmaf {

void letterboxRgba(const uint8_t *src, int srcW, int srcH, int srcStride, uint8_t *dst, const LetterboxMap &lb)
{
	const int dw = lb.dstW, dh = lb.dstH;
	for (int i = 0; i < dw * dh; ++i) {
		dst[4 * i + 0] = dst[4 * i + 1] = dst[4 * i + 2] = 114;
		dst[4 * i + 3] = 255;
	}
	const int cw = lb.contentW(), ch = lb.contentH();
	for (int y = 0; y < ch; ++y) {
		const int y0 = (int)((int64_t)y * srcH / ch);
		const int y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * srcH / ch));
		for (int x = 0; x < cw; ++x) {
			const int x0 = (int)((int64_t)x * srcW / cw);
			const int x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * srcW / cw));
			uint32_t r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; ++yy) {
				const uint8_t *row = src + (size_t)yy * (size_t)srcStride;
				for (int xx = x0; xx < x1; ++xx) {
					r += row[4 * xx + 0];
					g += row[4 * xx + 1];
					b += row[4 * xx + 2];
					++n;
				}
			}
			uint8_t *o = dst + 4 * ((size_t)y * dw + x);
			o[0] = (uint8_t)(r / n);
			o[1] = (uint8_t)(g / n);
			o[2] = (uint8_t)(b / n);
		}
	}
}

void rgbaToBgrPlanar(const uint8_t *rgba, int width, int height, int stride, float *dst)
{
	const size_t plane = (size_t)width * (size_t)height;
	float *b = dst;
	float *g = dst + plane;
	float *r = dst + 2 * plane;
	for (int y = 0; y < height; ++y) {
		const uint8_t *row = rgba + (size_t)y * (size_t)stride;
		const size_t o = (size_t)y * (size_t)width;
		for (int x = 0; x < width; ++x) {
			r[o + x] = (float)row[4 * x + 0];
			g[o + x] = (float)row[4 * x + 1];
			b[o + x] = (float)row[4 * x + 2];
		}
	}
}

bool decodeYolox(const float *out, int numAnchors, int stride, const YoloxParams &p, std::vector<Detection> &dets)
{
	dets.clear();
	if (stride < 5 + p.personClass + 1)
		return false;
	static const int kStrides[3] = {8, 16, 32};
	int expected = 0;
	for (int s : kStrides)
		expected += (p.inputH / s) * (p.inputW / s);
	if (expected != numAnchors)
		return false;

	int idx = 0;
	for (int s : kStrides) {
		const int gh = p.inputH / s, gw = p.inputW / s;
		for (int gy = 0; gy < gh; ++gy) {
			for (int gx = 0; gx < gw; ++gx, ++idx) {
				const float *a = out + (size_t)idx * (size_t)stride;
				// objectness and class scores are already sigmoid-activated in the export
				const float score = a[4] * a[5 + p.personClass];
				if (score < p.scoreThresh)
					continue;
				const float cx = (a[0] + (float)gx) * (float)s;
				const float cy = (a[1] + (float)gy) * (float)s;
				const float w = std::exp(std::min(a[2], 10.0f)) * (float)s;
				const float h = std::exp(std::min(a[3], 10.0f)) * (float)s;
				Detection d;
				d.box = RectF::fromCenter(cx, cy, w, h);
				d.score = score;
				dets.push_back(d);
			}
		}
	}
	nms(dets, p.nmsThresh, p.maxDetections);
	return true;
}

void nms(std::vector<Detection> &dets, float iouThresh, int maxKeep)
{
	std::sort(dets.begin(), dets.end(), [](const Detection &a, const Detection &b) { return a.score > b.score; });
	std::vector<Detection> keep;
	keep.reserve(std::min<size_t>(dets.size(), (size_t)std::max(1, maxKeep)));
	for (const Detection &d : dets) {
		bool suppressed = false;
		for (const Detection &k : keep) {
			if (iou(d.box, k.box) > iouThresh) {
				suppressed = true;
				break;
			}
		}
		if (!suppressed) {
			keep.push_back(d);
			if ((int)keep.size() >= maxKeep)
				break;
		}
	}
	dets.swap(keep);
}

} // namespace vmaf
