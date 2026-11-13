// Composition and zoom limits.
#include "sim.hpp"
#include "zoom-controller.hpp"

using namespace vmaf;

TEST(zoom_compose_medium_shot)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.maxZoom = 4.0f;
	const ZoomLimits lim = ZoomController::limits(16.0f / 9.0f, 1920, 1080, 1.0f, 4.0f);
	const RectF p{900, 200, 200, 600}; // full body, 600 px tall
	const Shot s = ZoomController::compose(p, false, c, 16.0f / 9.0f, lim, 1.0f);
	// waist-up portion (55% of 600 = 330 px) should fill 88% of the crop height
	CHECK_NEAR(s.h, 330.0f / 0.88f, 0.5);
	// head sits `headroom` (10%) below the top edge
	const float top = s.cy - 0.5f * s.h;
	CHECK_NEAR(p.y - top, 0.10f * s.h, 0.5);
	CHECK_NEAR(s.cx, p.cx(), 1e-3);
}

TEST(zoom_limits_are_respected)
{
	AutoFrameConfig c = testConfig(FramingPreset::Face);
	const ZoomLimits lim = ZoomController::limits(16.0f / 9.0f, 1920, 1080, 1.0f, 1.4f);
	CHECK_NEAR(lim.maxH, 1080, 1e-3);
	CHECK_NEAR(lim.minH, 1080 / 1.4, 1e-2);
	const Shot tight = ZoomController::compose({900, 300, 60, 160}, false, c, 16.0f / 9.0f, lim, 1.0f);
	CHECK(tight.h >= lim.minH - 1e-3f); // never more than 1.4x
	AutoFrameConfig w = testConfig(FramingPreset::Wide);
	const Shot loose = ZoomController::compose({100, 0, 1500, 1080}, false, w, 16.0f / 9.0f, lim, 1.0f);
	CHECK(loose.h <= lim.wideH + 1e-3f); // never wider than the camera image
}

TEST(zoom_min_zoom_defines_the_safe_wide_shot)
{
	const ZoomLimits lim = ZoomController::limits(16.0f / 9.0f, 3840, 2160, 1.2f, 2.0f);
	CHECK_NEAR(lim.wideH, 2160 / 1.2, 1e-2);
	CHECK_NEAR(lim.zoomFor(lim.wideH), 1.2, 1e-4);
	CHECK_NEAR(lim.zoomFor(lim.minH), 2.0, 1e-4);
}

TEST(zoom_keeps_wide_subjects_whole)
{
	// arms stretched out: the box is wide; the crop must be at least box width + margins
	AutoFrameConfig c = testConfig(FramingPreset::CloseUp);
	c.maxZoom = 4.0f;
	const ZoomLimits lim = ZoomController::limits(16.0f / 9.0f, 1920, 1080, 1.0f, 4.0f);
	const RectF p{600, 300, 700, 500};
	const Shot s = ZoomController::compose(p, false, c, 16.0f / 9.0f, lim, 1.0f);
	CHECK(s.h * 16.0f / 9.0f >= p.w * (1.0f + 2.0f * c.safeMargin) - 1e-2f);
}

TEST(zoom_truncated_body_estimate)
{
	// person cut off by the bottom of the image: visible 400 px, box 200 px wide
	CHECK_NEAR(ZoomController::estimatedBodyHeight({0, 680, 200, 400}, true), 600.0, 1e-3);
	CHECK_NEAR(ZoomController::estimatedBodyHeight({0, 680, 200, 400}, false), 400.0, 1e-3);
}

TEST(zoom_detail_guard_for_vertical_output)
{
	AutoFrameConfig c = testConfig(FramingPreset::Preacher); // max zoom 1.4
	c.aspect = OutputAspect::A9x16;                          // output 1080x1920 (Auto)
	// 1080p camera: the 608x1080 crop is already enlarged 1.78x at zoom 1.0
	CHECK_NEAR(ZoomController::enlargement(c, 1920, 1080, 1.0f), 1920.0 / 1080.0, 1e-3);
	CHECK_NEAR(ZoomController::effectiveMaxZoom(c, 1920, 1080), 2.0 / (1920.0 / 1080.0), 1e-3); // 1.125
	// 4K camera: plenty of detail, the configured 1.4x stays available
	CHECK_NEAR(ZoomController::effectiveMaxZoom(c, 3840, 2160), 1.4, 1e-4);
	// 16:9 from a 1080p camera is unaffected
	c.aspect = OutputAspect::Source;
	CHECK_NEAR(ZoomController::effectiveMaxZoom(c, 1920, 1080), 1.4, 1e-4);
	// guard can be relaxed
	c.aspect = OutputAspect::A9x16;
	c.maxEnlarge = 4.0f;
	CHECK_NEAR(ZoomController::effectiveMaxZoom(c, 1920, 1080), 1.4, 1e-4);
	// never below the minimum zoom
	c.maxEnlarge = 1.0f;
	CHECK_NEAR(ZoomController::effectiveMaxZoom(c, 1920, 1080), 1.0, 1e-4);
}

TEST(zoom_detail_guard_applies_while_tracking)
{
	AutoFrameConfig c = testConfig(FramingPreset::CloseUp); // wants a lot of zoom
	c.aspect = OutputAspect::A9x16;
	Sim s(c);
	s.world = [](double) { return std::vector<Detection>{person(960, 300, 400)}; };
	s.run(10);
	CHECK(s.out().zoom <= 1.126f);
	CHECK(s.violations == 0);
}

TEST(zoom_fixed_when_auto_zoom_off)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.autoZoom = false;
	c.fixedZoom = 1.25f;
	Sim s(c);
	s.world = [](double) { return std::vector<Detection>{person(960, 200, 700)}; };
	s.run(10);
	CHECK_NEAR(s.out().zoom, 1.25, 0.01);
}

TEST(zoom_changes_gradually)
{
	AutoFrameConfig c = testConfig(FramingPreset::FullBody);
	c.maxZoom = 2.0f;
	Sim s(c);
	// full-body shot, person walks toward the camera: grows from 300 to 900 px tall over 2 s
	// -> the shot must zoom out from 2.0x to ~1.0x, but no faster than the zoom speed limit
	s.world = [](double t) {
		const float h = std::min(900.0f, 300.0f + 300.0f * (float)std::max(0.0, t - 4.0));
		return std::vector<Detection>{person(960, 120, h)};
	};
	float prev = 0, maxRate = 0;
	for (int i = 0; i < 30 * 14; ++i) {
		s.step();
		const float z = std::log(s.out().zoom);
		if (i > 0)
			maxRate = std::max(maxRate, std::fabs(z - prev) * 30.0f);
		prev = z;
	}
	CHECK_MSG(maxRate <= c.zoomSpeed + 0.02f, "zoom rate %.3f ln/s exceeds the limit %.3f", maxRate, c.zoomSpeed);
	CHECK(s.out().zoom < 1.15f); // zoomed out to keep the closer person framed
	CHECK(s.violations == 0);
}
