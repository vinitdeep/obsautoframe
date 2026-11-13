// V Media AI AutoFrame - composition and zoom math.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include "geometry.hpp"
#include "settings.hpp"

namespace vmaf {

/* A desired virtual-camera shot: crop center and crop height in source pixels.
 * The crop width always follows from the output aspect ratio. */
struct Shot {
	float cx = 0.0f, cy = 0.0f, h = 0.0f;
};

struct ZoomLimits {
	float maxH = 0.0f; // crop height at zoom 1.0 (largest crop that fits the source)
	float minH = 0.0f; // crop height at the maximum zoom
	float wideH = 0.0f; // crop height at the minimum zoom (the "safe wide shot")

	float zoomFor(float h) const { return h > 0.0f ? maxH / h : 1.0f; }
	float heightFor(float zoom) const { return zoom > 0.0f ? maxH / zoom : maxH; }
};

class ZoomController {
public:
	static ZoomLimits limits(float aspect, float srcW, float srcH, float minZoom, float maxZoom);

	/* Maximum zoom after the detail guard: the crop is never enlarged by more than
	 * cfg.maxEnlarge on the way to the filter's output resolution. Never below min zoom. */
	static float effectiveMaxZoom(const AutoFrameConfig &cfg, float srcW, float srcH);
	/* Enlargement factor (output px per camera px) at a given zoom. */
	static float enlargement(const AutoFrameConfig &cfg, float srcW, float srcH, float zoom);

	/* Full-body height estimate. When the person is cut off by the bottom of the camera
	 * image the visible box underestimates their height, so it is extended using the box
	 * width (a standing adult is roughly 2.5-3.5x as tall as wide). */
	static float estimatedBodyHeight(const RectF &subject, bool truncatedBottom);

	/* Turns a subject box (one person or a whole group) into a shot:
	 *  - crop height so that `bodyFraction` of the person fills `subjectFill` of the frame
	 *  - never narrower than the subject plus the safe margin on both sides
	 *  - head placed `headroom` below the top edge, horizontal offset applied
	 *  - zoom limited to [minZoom, maxZoom]  (manual zoom bias multiplies the crop height) */
	static Shot compose(const RectF &subject, bool truncatedBottom, const AutoFrameConfig &cfg, float aspect,
			    const ZoomLimits &lim, float zoomBias, float srcW = 0.0f);
};

} // namespace vmaf
