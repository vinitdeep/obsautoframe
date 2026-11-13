// V Media AI AutoFrame - composition and zoom math.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "zoom-controller.hpp"

#include <algorithm>

namespace vmaf {

ZoomLimits ZoomController::limits(float aspect, float srcW, float srcH, float minZoom, float maxZoom)
{
	ZoomLimits l;
	l.maxH = maxCropHeight(aspect, srcW, srcH);
	minZoom = std::max(1.0f, minZoom);
	maxZoom = std::max(minZoom, maxZoom);
	l.minH = l.maxH / maxZoom;
	l.wideH = l.maxH / minZoom;
	return l;
}

float ZoomController::enlargement(const AutoFrameConfig &cfg, float srcW, float srcH, float zoom)
{
	if (srcW <= 0.0f || srcH <= 0.0f)
		return 1.0f;
	uint32_t ow = 0, oh = 0;
	outputDimensions(cfg.aspect, cfg.outputSize, (uint32_t)srcW, (uint32_t)srcH, ow, oh);
	const float cropH = maxCropHeight(aspectValue(cfg.aspect, srcW, srcH), srcW, srcH) / std::max(1.0f, zoom);
	return cropH > 0.0f ? (float)oh / cropH : 1.0f;
}

float ZoomController::effectiveMaxZoom(const AutoFrameConfig &cfg, float srcW, float srcH)
{
	const float minZ = std::max(1.0f, cfg.minZoom);
	float maxZ = std::max(minZ, cfg.maxZoom);
	if (cfg.maxEnlarge > 0.0f && srcW > 0.0f && srcH > 0.0f) {
		const float atOne = enlargement(cfg, srcW, srcH, 1.0f);
		if (atOne > 0.0f)
			maxZ = std::min(maxZ, cfg.maxEnlarge / atOne);
	}
	return std::max(minZ, maxZ);
}

float ZoomController::estimatedBodyHeight(const RectF &s, bool truncatedBottom)
{
	if (!truncatedBottom)
		return s.h;
	return std::max(s.h, std::min(s.w * 3.0f, s.h * 2.5f));
}

Shot ZoomController::compose(const RectF &subject, bool truncatedBottom, const AutoFrameConfig &cfg, float aspect,
			     const ZoomLimits &lim, float zoomBias)
{
	const Composition &c = cfg.composition;
	float h;
	if (cfg.autoZoom) {
		const float body = estimatedBodyHeight(subject, truncatedBottom);
		const float kept = body * std::clamp(c.bodyFraction, 0.05f, 1.0f);
		h = kept / std::clamp(c.subjectFill, 0.2f, 1.0f);
		// keep the whole width of the subject (arms, group edges) plus a safe margin
		const float needW = subject.w * (1.0f + 2.0f * std::max(0.0f, cfg.safeMargin));
		if (aspect > 0.0f)
			h = std::max(h, needW / aspect);
		h *= std::clamp(zoomBias, 0.25f, 4.0f);
		h = std::clamp(h, lim.minH, lim.wideH);
	} else {
		const float z = std::clamp(cfg.fixedZoom * (1.0f / std::clamp(zoomBias, 0.25f, 4.0f)), 1.0f,
					   std::max(1.0f, cfg.maxZoom));
		h = std::clamp(lim.heightFor(z), lim.minH, lim.maxH);
	}

	Shot s;
	s.h = h;
	const float w = h * aspect;
	s.cx = subject.cx() - cfg.offsetX * w;
	const float top = subject.y - std::clamp(c.headroom, 0.0f, 0.5f) * h;
	s.cy = top + 0.5f * h + cfg.offsetY * h;
	return s;
}

} // namespace vmaf
