// V Media AI AutoFrame - all GPU work of the filter (OBS graphics thread only).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "obs-renderer.hpp"

#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vmaf {

// ------------------------------------------------------------------ lifecycle

bool ObsRenderer::init(std::string &err)
{
	char *path = obs_module_file("effects/autoframe.effect");
	if (!path) {
		err = "effects/autoframe.effect not found in the plugin data folder";
		return false;
	}
	char *errors = nullptr;
	effect_ = gs_effect_create_from_file(path, &errors);
	bfree(path);
	if (!effect_) {
		err = std::string("could not compile autoframe.effect: ") + (errors ? errors : "unknown error");
		bfree(errors);
		return false;
	}
	bfree(errors);
	pImage_ = gs_effect_get_param_by_name(effect_, "image");
	pOffset_ = gs_effect_get_param_by_name(effect_, "uv_offset");
	pScale_ = gs_effect_get_param_by_name(effect_, "uv_scale");
	pDim_ = gs_effect_get_param_by_name(effect_, "base_dimension");
	pSharp_ = gs_effect_get_param_by_name(effect_, "sharpness");
	return true;
}

void ObsRenderer::destroy()
{
	obs_enter_graphics();
	if (srcRender_)
		gs_texrender_destroy(srcRender_);
	if (lbRender_)
		gs_texrender_destroy(lbRender_);
	if (stage_)
		gs_stagesurface_destroy(stage_);
	if (effect_)
		gs_effect_destroy(effect_);
	obs_leave_graphics();
	srcRender_ = lbRender_ = nullptr;
	stage_ = nullptr;
	effect_ = nullptr;
	staged_ = false;
}

// ------------------------------------------------------------------ source capture

gs_texture_t *ObsRenderer::renderTarget(obs_source_t *filter, uint32_t w, uint32_t h, enum gs_color_space space)
{
	obs_source_t *target = obs_filter_get_target(filter);
	obs_source_t *parent = obs_filter_get_parent(filter);
	if (!target || !parent || !w || !h)
		return nullptr;

	const enum gs_color_format format = gs_get_format_from_space(space);
	if (srcRender_ && srcFormat_ != format) {
		gs_texrender_destroy(srcRender_);
		srcRender_ = nullptr;
	}
	if (!srcRender_) {
		srcRender_ = gs_texrender_create(format, GS_ZS_NONE);
		srcFormat_ = format;
	}

	gs_texrender_reset(srcRender_);
	if (!gs_texrender_begin_with_color_space(srcRender_, w, h, space))
		return nullptr;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	// Same rule libobs uses for its own filter chain (obs_source_process_filter_begin).
	const uint32_t parentFlags = obs_source_get_output_flags(parent);
	const bool customDraw = (parentFlags & OBS_SOURCE_CUSTOM_DRAW) != 0;
	const bool async = (parentFlags & OBS_SOURCE_ASYNC) != 0;
	if (target == parent && !customDraw && !async)
		obs_source_default_render(target);
	else
		obs_source_video_render(target);

	gs_blend_state_pop();
	gs_texrender_end(srcRender_);
	return gs_texrender_get_texture(srcRender_);
}

// ------------------------------------------------------------------ detector frames

void ObsRenderer::captureForDetection(gs_texture_t *src, uint32_t srcW, uint32_t srcH, int inW, int inH,
				      double timestamp, uint64_t frameId)
{
	if (staged_ || !src || inW <= 0 || inH <= 0 || !srcW || !srcH)
		return;
	if (!lbRender_)
		lbRender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (!stage_ || stageW_ != inW || stageH_ != inH) {
		if (stage_)
			gs_stagesurface_destroy(stage_);
		stage_ = gs_stagesurface_create((uint32_t)inW, (uint32_t)inH, GS_RGBA);
		stageW_ = inW;
		stageH_ = inH;
	}
	if (!lbRender_ || !stage_)
		return;

	const LetterboxMap lb = LetterboxMap::compute((int)srcW, (int)srcH, inW, inH);
	gs_texrender_reset(lbRender_);
	if (!gs_texrender_begin(lbRender_, (uint32_t)inW, (uint32_t)inH))
		return;

	struct vec4 grey; // YOLOX padding value 114
	vec4_set(&grey, 114.0f / 255.0f, 114.0f / 255.0f, 114.0f / 255.0f, 1.0f);
	gs_clear(GS_CLEAR_COLOR, &grey, 0.0f, 0);
	gs_ortho(0.0f, (float)inW, 0.0f, (float)inH, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_reset_blend_state();
	gs_enable_blending(false);
	const bool prevSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false); // raw 8-bit sRGB values, like a camera JPEG

	// Area resampling: a proper average of every source pixel (no aliasing on 4K input).
	gs_effect_t *area = obs_get_base_effect(OBS_EFFECT_AREA);
	struct vec2 dim, dimi;
	vec2_set(&dim, (float)srcW, (float)srcH);
	vec2_set(&dimi, 1.0f / (float)srcW, 1.0f / (float)srcH);
	gs_effect_set_texture(gs_effect_get_param_by_name(area, "image"), src);
	gs_effect_set_vec2(gs_effect_get_param_by_name(area, "base_dimension"), &dim);
	gs_effect_set_vec2(gs_effect_get_param_by_name(area, "base_dimension_i"), &dimi);
	gs_eparam_t *mul = gs_effect_get_param_by_name(area, "multiplier");
	if (mul)
		gs_effect_set_float(mul, 1.0f);
	while (gs_effect_loop(area, "Draw"))
		gs_draw_sprite(src, 0, (uint32_t)lb.contentW(), (uint32_t)lb.contentH());

	gs_enable_framebuffer_srgb(prevSrgb);
	gs_blend_state_pop();
	gs_texrender_end(lbRender_);

	gs_stage_texture(stage_, gs_texrender_get_texture(lbRender_));
	staged_ = true;
	stagedAge_ = 0;
	stagedLb_ = lb;
	stagedTs_ = timestamp;
	stagedId_ = frameId;
}

bool ObsRenderer::stagedFrameReady()
{
	if (!staged_ || !stage_)
		return false;
	// Wait two frames before mapping so the GPU copy has certainly completed:
	// mapping earlier would make the render thread wait for the GPU.
	return ++stagedAge_ >= 2;
}

bool ObsRenderer::collectDetectionFrame(FramePacket &pkt, float &readbackMs)
{
	if (!staged_ || !stage_ || stagedAge_ < 2)
		return false;

	const uint64_t t0 = os_gettime_ns();
	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stage_, &data, &linesize)) {
		staged_ = false;
		return false;
	}
	const size_t row = (size_t)stageW_ * 4;
	pkt.rgba.resize(row * (size_t)stageH_);
	for (int y = 0; y < stageH_; ++y)
		memcpy(pkt.rgba.data() + row * (size_t)y, data + (size_t)linesize * (size_t)y, row);
	gs_stagesurface_unmap(stage_);
	pkt.width = stageW_;
	pkt.height = stageH_;
	pkt.stride = (int)row;
	pkt.lb = stagedLb_;
	pkt.timestamp = stagedTs_;
	pkt.id = stagedId_;
	staged_ = false;
	readbackMs = (float)((os_gettime_ns() - t0) / 1.0e6);
	return true;
}

// ------------------------------------------------------------------ output

void ObsRenderer::drawCrop(gs_texture_t *tex, const RectF &crop, uint32_t srcW, uint32_t srcH, uint32_t outW,
			   uint32_t outH, bool bicubic, float sharpen)
{
	if (!tex || !srcW || !srcH)
		return;
	const bool linear = gs_get_linear_srgb();
	const bool prev = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear);

	if (effect_) {
		struct vec2 off, scale, dim;
		vec2_set(&off, crop.x / (float)srcW, crop.y / (float)srcH);
		vec2_set(&scale, crop.w / (float)srcW, crop.h / (float)srcH);
		vec2_set(&dim, (float)srcW, (float)srcH);
		if (linear)
			gs_effect_set_texture_srgb(pImage_, tex);
		else
			gs_effect_set_texture(pImage_, tex);
		gs_effect_set_vec2(pOffset_, &off);
		gs_effect_set_vec2(pScale_, &scale);
		gs_effect_set_vec2(pDim_, &dim);
		if (pSharp_)
			gs_effect_set_float(pSharp_, bicubic ? std::clamp(sharpen, 0.0f, 1.5f) : 0.0f);
		while (gs_effect_loop(effect_, bicubic ? "DrawBicubic" : "Draw"))
			gs_draw_sprite(tex, 0, outW, outH);
	} else {
		// Fallback (effect failed to compile): whole-pixel crop with the stock shader.
		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
		if (linear)
			gs_effect_set_texture_srgb(img, tex);
		else
			gs_effect_set_texture(img, tex);
		gs_matrix_push();
		gs_matrix_scale3f((float)outW / std::max(1.0f, crop.w), (float)outH / std::max(1.0f, crop.h), 1.0f);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite_subregion(tex, 0, (uint32_t)crop.x, (uint32_t)crop.y, (uint32_t)crop.w,
						 (uint32_t)crop.h);
		gs_matrix_pop();
	}
	gs_enable_framebuffer_srgb(prev);
}

// ------------------------------------------------------------------ overlay

namespace {
// 5x7 glyphs, one byte per row, bit 4 = leftmost pixel.
struct Glyph {
	char c;
	uint8_t rows[7];
};
const Glyph kFont[] = {
	{'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
	{'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
	{'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
	{'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
	{'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
	{'A', {0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
	{'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
	{'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
	{'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
	{'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
	{'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
	{'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
	{'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
	{'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
	{'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
	{'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
	{'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
	{'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
	{'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
	{'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
	{'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}}, {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
	{'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
	{'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
	{'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
};

const Glyph *findGlyph(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (const Glyph &g : kFont)
		if (g.c == c)
			return &g;
	return nullptr; // space and unknown characters advance without drawing
}
} // namespace

void ObsRenderer::beginOverlay()
{
	solid_ = obs_get_base_effect(OBS_EFFECT_SOLID);
	solidTech_ = gs_effect_get_technique(solid_, "Solid");
	solidColor_ = gs_effect_get_param_by_name(solid_, "color");
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_technique_begin(solidTech_);
	gs_technique_begin_pass(solidTech_, 0);
	overlayActive_ = true;
	curColor_ = 0;
	setColor(0xFFFFFFFF);
}

void ObsRenderer::setColor(uint32_t argb)
{
	if (argb == curColor_)
		return;
	flushQuads();
	curColor_ = argb;
	struct vec4 c;
	vec4_set(&c, ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
		 ((argb >> 24) & 0xFF) / 255.0f);
	gs_effect_set_vec4(solidColor_, &c);
}

void ObsRenderer::pushQuad(float x, float y, float w, float h)
{
	if (!batchOpen_) {
		// false = draw with libobs' shared immediate vertex buffer. (true would build a NEW
		// buffer meant for gs_render_save(); pairing it with gs_render_stop() corrupts the
		// immediate buffer and crashes the next draw anywhere in OBS.)
		gs_render_start(false);
		batchOpen_ = true;
		quadCount_ = 0;
	}
	gs_vertex2f(x, y);
	gs_vertex2f(x + w, y);
	gs_vertex2f(x, y + h);
	gs_vertex2f(x + w, y);
	gs_vertex2f(x + w, y + h);
	gs_vertex2f(x, y + h);
	if (++quadCount_ >= 80) // libobs immediate mode holds at most 512 vertices
		flushQuads();
}

void ObsRenderer::flushQuads()
{
	if (!batchOpen_)
		return;
	gs_render_stop(GS_TRIS);
	batchOpen_ = false;
	quadCount_ = 0;
}

void ObsRenderer::fillRect(float x, float y, float w, float h, uint32_t argb)
{
	if (!overlayActive_ || w <= 0.0f || h <= 0.0f)
		return;
	setColor(argb);
	pushQuad(x, y, w, h);
}

void ObsRenderer::strokeRect(float x, float y, float w, float h, float t, uint32_t argb)
{
	fillRect(x, y, w, t, argb);
	fillRect(x, y + h - t, w, t, argb);
	fillRect(x, y + t, t, h - 2 * t, argb);
	fillRect(x + w - t, y + t, t, h - 2 * t, argb);
}

float ObsRenderer::textWidth(const char *text, float pixel)
{
	const size_t n = strlen(text);
	return n ? (float)(n * 6 - 1) * pixel : 0.0f;
}

float ObsRenderer::drawText(const char *text, float x, float y, float pixel, uint32_t argb)
{
	if (!overlayActive_)
		return 0.0f;
	setColor(argb);
	float cx = x;
	for (const char *p = text; *p; ++p, cx += 6.0f * pixel) {
		const Glyph *g = findGlyph(*p);
		if (!g)
			continue;
		for (int r = 0; r < 7; ++r) {
			// merge horizontal runs of lit pixels into one quad
			int c = 0;
			while (c < 5) {
				if (!(g->rows[r] & (0x10 >> c))) {
					++c;
					continue;
				}
				int e = c;
				while (e < 5 && (g->rows[r] & (0x10 >> e)))
					++e;
				pushQuad(cx + c * pixel, y + r * pixel, (e - c) * pixel, pixel);
				c = e;
			}
		}
	}
	return cx - x;
}

void ObsRenderer::endOverlay()
{
	if (!overlayActive_)
		return;
	flushQuads();
	gs_technique_end_pass(solidTech_);
	gs_technique_end(solidTech_);
	gs_blend_state_pop();
	overlayActive_ = false;
}

} // namespace vmaf
