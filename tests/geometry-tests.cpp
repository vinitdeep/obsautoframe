// Bounding-box coordinate conversion, crop boundaries and aspect ratio.
#include "geometry.hpp"
#include "settings.hpp"
#include "test-framework.hpp"

using namespace vmaf;

TEST(iou_basic)
{
	CHECK_NEAR(iou({0, 0, 10, 10}, {0, 0, 10, 10}), 1.0, 1e-6);
	CHECK_NEAR(iou({0, 0, 10, 10}, {20, 20, 10, 10}), 0.0, 1e-6);
	CHECK_NEAR(iou({0, 0, 10, 10}, {5, 0, 10, 10}), 50.0 / 150.0, 1e-6);
	const RectF u = unite({0, 0, 10, 10}, {20, 5, 10, 10});
	CHECK_NEAR(u.x, 0, 1e-6);
	CHECK_NEAR(u.right(), 30, 1e-6);
	CHECK_NEAR(u.bottom(), 15, 1e-6);
}

TEST(letterbox_maps_back_to_source_1080p)
{
	// 1920x1080 into a 416x416 network input: scale 416/1920, content 416x234 at top-left
	const LetterboxMap lb = LetterboxMap::compute(1920, 1080, 416, 416);
	CHECK_NEAR(lb.scale, 416.0 / 1920.0, 1e-6);
	CHECK(lb.contentW() == 416);
	CHECK(lb.contentH() == 234);
	const RectF src{960, 300, 200, 600};
	const RectF in = lb.toInput(src);
	const RectF back = lb.toSource(in);
	CHECK_NEAR(back.x, src.x, 1e-3);
	CHECK_NEAR(back.y, src.y, 1e-3);
	CHECK_NEAR(back.w, src.w, 1e-3);
	CHECK_NEAR(back.h, src.h, 1e-3);
}

TEST(letterbox_maps_back_to_source_4k_and_portrait)
{
	const LetterboxMap lb = LetterboxMap::compute(3840, 2160, 416, 416);
	const RectF back = lb.toSource({208, 100, 41.6f, 83.2f});
	CHECK_NEAR(back.x, 1920, 0.5);
	CHECK_NEAR(back.w, 384, 0.5);
	const LetterboxMap p = LetterboxMap::compute(1080, 1920, 416, 416); // vertical camera
	CHECK(p.contentH() == 416);
	CHECK(p.contentW() == 234);
}

TEST(letterbox_clips_boxes_to_source)
{
	const LetterboxMap lb = LetterboxMap::compute(1920, 1080, 416, 416);
	// a box spilling into the grey padding below the image must be clipped
	const RectF back = lb.toSource({100, 200, 50, 100});
	CHECK(back.bottom() <= 1080.0f + 1e-3f);
	CHECK(back.x >= 0.0f);
}

TEST(crop_clamp_never_leaves_source)
{
	const float W = 1920, H = 1080, A = 16.0f / 9.0f;
	const float cases[][3] = {{0, 0, 600}, {1920, 1080, 600}, {-500, 2000, 900}, {960, 540, 5000}, {10, 540, 1080}};
	for (const auto &c : cases) {
		float cx = c[0], cy = c[1], h = c[2];
		clampCrop(cx, cy, h, A, W, H);
		const RectF r = cropRect(cx, cy, h, A);
		CHECK(r.x >= -1e-3f);
		CHECK(r.y >= -1e-3f);
		CHECK(r.right() <= W + 1e-3f);
		CHECK(r.bottom() <= H + 1e-3f);
		CHECK_NEAR(r.w / r.h, A, 1e-4);
	}
}

TEST(output_dimensions_for_streaming_formats)
{
	uint32_t w = 0, h = 0;
	auto check = [&](OutputAspect a, OutputSize s, uint32_t sw, uint32_t sh, uint32_t ew, uint32_t eh) {
		outputDimensions(a, s, sw, sh, w, h);
		CHECK_MSG(w == ew && h == eh, "aspect %d size %d from %ux%u -> %ux%u, expected %ux%u", (int)a, (int)s, sw, sh,
			  w, h, ew, eh);
	};
	// vertical live (YouTube Shorts / Instagram / Facebook): exactly 1080x1920 by default
	check(OutputAspect::A9x16, OutputSize::Auto, 1920, 1080, 1080, 1920);
	check(OutputAspect::A9x16, OutputSize::Auto, 3840, 2160, 1080, 1920);
	check(OutputAspect::A9x16, OutputSize::Native, 1920, 1080, 608, 1080);
	check(OutputAspect::A9x16, OutputSize::Native, 3840, 2160, 1216, 2160);
	check(OutputAspect::A9x16, OutputSize::P720, 1920, 1080, 720, 1280);
	check(OutputAspect::A9x16, OutputSize::P2160, 3840, 2160, 2160, 3840);
	// other social formats
	check(OutputAspect::A1x1, OutputSize::Auto, 1920, 1080, 1080, 1080);
	check(OutputAspect::A4x5, OutputSize::Auto, 1920, 1080, 1080, 1350);
	check(OutputAspect::A16x9, OutputSize::Auto, 3840, 2160, 1920, 1080);
	// "same as camera" keeps the camera size unless a size is forced
	check(OutputAspect::Source, OutputSize::Auto, 1280, 720, 1280, 720);
	check(OutputAspect::Source, OutputSize::Native, 3840, 2160, 3840, 2160);
	check(OutputAspect::Source, OutputSize::P1080, 3840, 2160, 1920, 1080);
}

TEST(overlay_never_in_program_output_by_default)
{
	// only windows (Filters dialog, Studio Mode preview, projectors) ever get graphics;
	// the program texture (stream / recording / virtual camera) never does
	CHECK(overlayAllowed(true));
	CHECK(!overlayAllowed(false));
}

TEST(max_crop_height_respects_output_aspect)
{
	CHECK_NEAR(maxCropHeight(16.0f / 9.0f, 1920, 1080), 1080, 1e-3);
	CHECK_NEAR(maxCropHeight(9.0f / 16.0f, 1920, 1080), 1080, 1e-3); // 607.5 x 1080 vertical crop
	CHECK_NEAR(maxCropHeight(1.0f, 1920, 1080), 1080, 1e-3);
	CHECK_NEAR(maxCropHeight(16.0f / 9.0f, 1440, 1080), 810, 1e-3); // 4:3 camera, 16:9 output
	CHECK_NEAR(aspectValue(OutputAspect::Source, 1280, 720), 16.0 / 9.0, 1e-6);
	CHECK_NEAR(aspectValue(OutputAspect::A9x16, 1280, 720), 9.0 / 16.0, 1e-6);
}
