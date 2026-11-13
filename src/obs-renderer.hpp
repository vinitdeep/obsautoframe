// V Media AI AutoFrame - all GPU work of the filter (OBS graphics thread only).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include "detection-worker.hpp"
#include "geometry.hpp"

#include <obs-module.h>

#include <cstdint>
#include <string>

namespace vmaf {

class ObsRenderer {
public:
	ObsRenderer() = default;
	~ObsRenderer() = default; // call destroy() (needs the graphics context) before deleting
	ObsRenderer(const ObsRenderer &) = delete;
	ObsRenderer &operator=(const ObsRenderer &) = delete;

	/* Loads data/effects/autoframe.effect. Must be called inside the graphics context. */
	bool init(std::string &err);
	/* Releases GPU resources (enters the graphics context itself). */
	void destroy();

	/* Renders the filter's target (previous filters + source) into an internal texture
	 * of the source's own resolution and color space. */
	gs_texture_t *renderTarget(obs_source_t *filter, uint32_t w, uint32_t h, enum gs_color_space space);

	/* ---- detector frame pipeline (no GPU stall)
	 * frame N : downscale + letterbox to the network size on the GPU, copy to a staging surface
	 * frame N+2: map the staging surface (the copy is long finished) and hand it to the worker */
	bool captureInFlight() const { return staged_; }
	void captureForDetection(gs_texture_t *src, uint32_t srcW, uint32_t srcH, int inW, int inH, double timestamp,
				 uint64_t frameId);
	/* Call once per rendered frame while a capture is in flight; true when it can be mapped. */
	bool stagedFrameReady();
	bool collectDetectionFrame(FramePacket &pkt, float &readbackMs);

	/* Draws the crop region of `tex` stretched to the filter output size. */
	void drawCrop(gs_texture_t *tex, const RectF &crop, uint32_t srcW, uint32_t srcH, uint32_t outW, uint32_t outH,
		      bool bicubic, float sharpen = 0.0f);

	/* ---- debug overlay primitives, in output pixels (call between begin/end) */
	void beginOverlay();
	void fillRect(float x, float y, float w, float h, uint32_t argb);
	void strokeRect(float x, float y, float w, float h, float thickness, uint32_t argb);
	/* Built-in 5x7 bitmap font (A-Z 0-9 and . : - % / # ( ) + = |). Returns text width. */
	float drawText(const char *text, float x, float y, float pixel, uint32_t argb);
	static float textWidth(const char *text, float pixel);
	void endOverlay();

private:
	void flushQuads();
	void pushQuad(float x, float y, float w, float h);
	void setColor(uint32_t argb);

	gs_texrender_t *srcRender_ = nullptr;
	enum gs_color_format srcFormat_ = GS_UNKNOWN;
	gs_texrender_t *lbRender_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
	int stageW_ = 0, stageH_ = 0;
	bool staged_ = false;
	int stagedAge_ = 0;
	LetterboxMap stagedLb_;
	double stagedTs_ = 0.0;
	uint64_t stagedId_ = 0;

	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *pImage_ = nullptr, *pOffset_ = nullptr, *pScale_ = nullptr, *pDim_ = nullptr, *pSharp_ = nullptr;

	// overlay state
	bool overlayActive_ = false;
	gs_effect_t *solid_ = nullptr;
	gs_technique_t *solidTech_ = nullptr;
	gs_eparam_t *solidColor_ = nullptr;
	uint32_t curColor_ = 0;
	int quadCount_ = 0;
	bool batchOpen_ = false;
};

} // namespace vmaf
