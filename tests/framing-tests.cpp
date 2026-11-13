// Framing: centering, dead zone, smoothness, frame-rate independence, person lock,
// group framing, pulpit anchor, resolution change, operator overrides.
#include "sim.hpp"

#include <algorithm>

using namespace vmaf;

namespace {
// preacher walking left -> right across the stage and back, 1080p
std::vector<Detection> walker(double t)
{
	const double period = 16.0;
	double p = std::fmod(t, period) / period; // 0..1
	double x = p < 0.5 ? (300 + 1320 * (p * 2)) : (1620 - 1320 * ((p - 0.5) * 2));
	return {person((float)x, 280, 560)};
}
} // namespace

TEST(framing_keeps_walking_subject_in_frame_and_crop_inside_source)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	Sim s(c);
	s.world = walker;
	float worstOffset = 0;
	for (int i = 0; i < 30 * 32; ++i) {
		s.step();
		if (s.t < 3.0)
			continue; // initial framing
		const RectF &crop = s.out().crop;
		const Detection p = walker(s.t).front();
		// subject center stays inside the visible crop
		CHECK_MSG(p.box.cx() > crop.x && p.box.cx() < crop.right(), "t=%.2f subject %.0f outside crop [%.0f,%.0f]",
			  s.t, p.box.cx(), crop.x, crop.right());
		worstOffset = std::max(worstOffset, std::fabs(p.box.cx() - crop.cx()) / crop.w);
	}
	CHECK(s.violations == 0);
	CHECK_MSG(worstOffset < 0.42f, "subject drifted %.2f of the crop width from center", worstOffset);
}

TEST(framing_zoom_never_exceeds_limits)
{
	AutoFrameConfig c = testConfig(FramingPreset::CloseUp);
	c.maxZoom = 1.4f;
	Sim s(c);
	s.world = [](double) { return std::vector<Detection>{person(960, 400, 300)}; }; // small, far away
	s.run(10);
	CHECK(s.out().zoom <= 1.4f + 1e-3f);
	CHECK(s.out().zoom >= 1.0f - 1e-3f);
	CHECK_NEAR(s.out().zoom, 1.4, 0.02); // wants more, clamped at max
}

TEST(framing_dead_zone_ignores_small_movements)
{
	AutoFrameConfig c = testConfig(FramingPreset::Preacher);
	c.pulpitEnabled = false;
	Sim s(c);
	// preacher sways +-40 px (2% of the frame) around x=900 - typical while speaking
	s.world = [](double t) {
		return std::vector<Detection>{person(900.0f + 40.0f * (float)std::sin(t * 2.0), 280, 560)};
	};
	s.run(8); // settle
	const RectF a = s.out().crop;
	float maxMove = 0;
	for (int i = 0; i < 30 * 10; ++i) {
		s.step();
		maxMove = std::max(maxMove, std::fabs(s.out().crop.cx() - a.cx()));
	}
	CHECK_MSG(maxMove < 2.0f, "camera moved %.1f px for small sways inside the dead zone", maxMove);
}

TEST(framing_motion_is_smooth_no_jumps)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	Sim s(c);
	// subject teleports (detector picks up someone at the other side): camera must glide
	s.world = [](double t) { return std::vector<Detection>{person(t < 4 ? 400.0f : 1500.0f, 280, 560)}; };
	float maxStep = 0, prev = -1;
	for (int i = 0; i < 30 * 12; ++i) {
		s.step();
		const float x = s.out().crop.cx();
		if (prev >= 0)
			maxStep = std::max(maxStep, std::fabs(x - prev));
		prev = x;
	}
	// limit: trackingSpeed (0.45 W/s) / 30 fps = 28.8 px per frame
	CHECK_MSG(maxStep <= 0.45f * 1920.0f / 30.0f + 0.5f, "max per-frame pan %.1f px", maxStep);
	CHECK(s.violations == 0);
}

TEST(framing_is_frame_rate_independent)
{
	auto runAt = [](float fps, std::vector<float> &samples) {
		AutoFrameConfig c = testConfig(FramingPreset::Medium);
		Sim s(c);
		s.fps = fps;
		s.world = walker;
		const int frames = (int)(12 * fps);
		for (int i = 1; i <= frames; ++i) {
			s.step();
			if (i % (int)(fps / 2) == 0) // sample twice per second
				samples.push_back(s.out().crop.cx());
		}
	};
	std::vector<float> a30, a60, a144;
	runAt(30, a30);
	runAt(60, a60);
	runAt(144, a144);
	CHECK(a30.size() == a60.size());
	CHECK(a30.size() == a144.size());
	float worst = 0;
	for (size_t i = 0; i < std::min(a30.size(), std::min(a60.size(), a144.size())); ++i) {
		worst = std::max(worst, std::fabs(a30[i] - a60[i]));
		worst = std::max(worst, std::fabs(a30[i] - a144[i]));
	}
	// sampling instants differ by < 1 frame and the detector runs on its own clock,
	// so a few pixels of difference are expected - not tens or hundreds
	CHECK_MSG(worst < 12.0f, "30/60/144 fps trajectories differ by %.1f px", worst);
}

TEST(framing_crop_matches_output_aspect_vertical)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.aspect = OutputAspect::A9x16;
	Sim s(c);
	s.world = walker;
	s.run(20);
	CHECK(s.violations == 0);
	CHECK_NEAR(s.out().crop.w / s.out().crop.h, 9.0 / 16.0, 1e-3);
}

TEST(framing_person_lock_survives_someone_walking_past)
{
	AutoFrameConfig c = testConfig(FramingPreset::Preacher);
	c.pulpitEnabled = false;
	c.policy = TargetPolicy::Largest;
	Sim s(c);
	// preacher at x=700; from t=4..9 a deacon (taller in frame = closer to camera) walks
	// across in front from right to left
	s.world = [](double t) {
		std::vector<Detection> d{person(700, 280, 560)};
		if (t > 4 && t < 9) {
			const float x = 1800.0f - 280.0f * (float)(t - 4);
			if (std::fabs(x - 700) > 150) // fully hides the preacher while overlapping
				d.push_back(person(x, 200, 700));
			else
				d[0] = person(x, 200, 700);
		}
		return d;
	};
	s.run(3.5);
	const int preacher = s.out().targetId;
	CHECK(preacher > 0);
	int switched = 0;
	for (int i = 0; i < 30 * 9; ++i) {
		s.step();
		if (s.out().status == FramingStatus::Tracking && s.out().targetId != preacher)
			++switched;
	}
	CHECK_MSG(s.out().targetId == preacher, "target is #%d, expected preacher #%d", s.out().targetId, preacher);
	CHECK_MSG(switched == 0, "switched away from the preacher for %d frames", switched);
	CHECK(std::fabs(s.out().crop.cx() - 700) < 200);
}

TEST(framing_group_mode_frames_everyone_and_ignores_background)
{
	AutoFrameConfig c = testConfig(FramingPreset::Group);
	c.maxZoom = 2.0f;
	Sim s(c);
	// worship team of 3 + two small congregation members far in the background
	s.world = [](double) {
		return std::vector<Detection>{person(700, 300, 520), person(950, 290, 540), person(1200, 310, 500),
					      person(150, 600, 150), person(1800, 620, 140)};
	};
	s.run(10);
	const RectF crop = s.out().crop;
	CHECK(s.out().groupIds.size() == 3);
	for (float x : {700.0f, 950.0f, 1200.0f})
		CHECK(x - 100 > crop.x && x + 100 < crop.right());
	CHECK(crop.x > 150.0f || crop.right() < 1800.0f); // did not widen for the background people
	CHECK(s.violations == 0);
}

TEST(framing_vertical_group_never_cuts_people_in_half)
{
	// 4-piece worship team spread across a 1080p camera; vertical 9:16 output can only show
	// ~608 px of width. The old behaviour centred on the group = everyone cut in half.
	AutoFrameConfig c = testConfig(FramingPreset::Group);
	c.aspect = OutputAspect::A9x16;
	c.policy = TargetPolicy::Centered; // lead singer in the middle
	Sim s(c);
	s.world = [](double t) {
		const float j = 6.0f * (float)std::sin(t * 3.1); // detector jitter / swaying
		return std::vector<Detection>{person(300 + j, 300, 560), person(760 - j, 280, 600),
					      person(1120 + j, 290, 580), person(1620 - j, 300, 560)};
	};
	s.run(4);
	int cutFrames = 0, changes = 0, frames = 0;
	std::vector<int> prev;
	for (int i = 0; i < 30 * 10; ++i) {
		s.step();
		const FramingOutput &o = s.out();
		CHECK(!o.groupIds.empty());
		// every person the shot claims to show must be fully inside it horizontally
		for (int id : o.groupIds) {
			const Track *t = s.tracker.find(id);
			if (t && (t->box.x < o.crop.x - 2 || t->box.right() > o.crop.right() + 2))
				++cutFrames;
		}
		std::vector<int> ids = o.groupIds;
		std::sort(ids.begin(), ids.end());
		if (frames++ && ids != prev)
			++changes;
		prev = ids;
	}
	CHECK_MSG(cutFrames == 0, "framed people were cut by the vertical crop in %d frames", cutFrames);
	CHECK_MSG(changes == 0, "group membership flickered %d times", changes);
	CHECK(s.violations == 0);
	CHECK_NEAR(s.out().crop.w / s.out().crop.h, 9.0 / 16.0, 1e-3);
	// the crop sits on real people, not on the empty middle between them
	const float cx = s.out().crop.cx();
	CHECK_MSG(std::fabs(cx - 760) < 200 || std::fabs(cx - 1120) < 200, "crop center %.0f is not on the lead", cx);
}

TEST(framing_group_that_fits_still_frames_everyone)
{
	AutoFrameConfig c = testConfig(FramingPreset::Group);
	c.aspect = OutputAspect::A9x16;
	Sim s(c);
	// a duet standing close together fits even a vertical frame -> both framed
	s.world = [](double) { return std::vector<Detection>{person(900, 300, 560), person(1060, 290, 580)}; };
	s.run(6);
	CHECK(s.out().groupIds.size() == 2);
}

TEST(framing_stage_area_ignores_foreground_congregation)
{
	// Camera at the back of the church: two big heads/shoulders in the foreground (cut off by
	// the bottom edge, larger than the preacher), preacher on stage.
	auto world = [](double t) {
		return std::vector<Detection>{
			{RectF{40, 520, 420, 560}, 0.85f},   // front-row head, left
			{RectF{1420, 500, 460, 580}, 0.85f}, // front-row head, right
			person(1000.0f + 150.0f * (float)std::sin(t * 0.5), 220, 420),
		};
	};
	{
		AutoFrameConfig c = testConfig(FramingPreset::Preacher);
		c.pulpitEnabled = false;
		c.policy = TargetPolicy::Largest;
		Sim s(c);
		s.world = world;
		s.run(4);
		// without a stage area "largest" picks a foreground head: this is the problem...
		const Track *t = s.tracker.find(s.out().targetId);
		CHECK(t && t->box.bottom() >= 1079.0f);
	}
	{
		AutoFrameConfig c = testConfig(FramingPreset::Preacher);
		c.pulpitEnabled = false;
		c.policy = TargetPolicy::Largest;
		c.stageTop = 0.0f;
		c.stageBottom = 0.90f; // ...which the stage area solves: preacher's feet at 59%, heads cut off at 100%
		Sim s(c);
		s.world = world;
		s.run(6);
		const Track *t = s.tracker.find(s.out().targetId);
		CHECK(t && std::fabs(t->box.h - 420.0f) < 1.0f);
	}
	{
		AutoFrameConfig c = testConfig(FramingPreset::Preacher);
		c.pulpitEnabled = false;
		c.policy = TargetPolicy::Largest;
		c.ignoreBottomEdge = true; // ...and so does the bottom-edge rule
		Sim s(c);
		s.world = world;
		s.run(6);
		const Track *t = s.tracker.find(s.out().targetId);
		CHECK(t && std::fabs(t->box.h - 420.0f) < 1.0f);
	}
}

TEST(framing_pulpit_anchor_holds_still_and_follows_when_leaving)
{
	AutoFrameConfig c = testConfig(FramingPreset::Preacher);
	c.pulpitEnabled = true;
	c.pulpitX = 0.5f;
	c.pulpitWidth = 0.25f; // 720..1200 px
	Sim s(c);
	// t<12: moves around inside the pulpit zone; t>=12: walks to the far left
	s.world = [](double t) {
		if (t < 12)
			return std::vector<Detection>{person(960.0f + 180.0f * (float)std::sin(t * 0.9), 280, 560)};
		const float x = std::max(300.0f, 960.0f - 250.0f * (float)(t - 12));
		return std::vector<Detection>{person(x, 280, 560)};
	};
	s.run(5);
	float maxMove = 0;
	const float x0 = s.out().crop.cx();
	while (s.t < 11.5) {
		s.step();
		maxMove = std::max(maxMove, std::fabs(s.out().crop.cx() - x0));
		CHECK(s.out().inPulpitZone);
	}
	CHECK_MSG(maxMove < 2.0f, "shot moved %.1f px while the preacher was at the pulpit", maxMove);
	CHECK_NEAR(x0, 960, 2);
	s.run(10);
	CHECK(!s.out().inPulpitZone);
	CHECK_MSG(s.out().crop.cx() < 760, "did not follow the preacher off the pulpit (crop x %.0f)", s.out().crop.cx());
}

TEST(framing_handles_resolution_change)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	Sim s(c);
	s.world = walker;
	s.run(5);
	s.fr.setSourceSize(1280, 720);
	s.tracker.reset();
	s.W = 1280;
	s.H = 720;
	s.world = [](double) { return std::vector<Detection>{person(400, 180, 380)}; };
	s.violations = 0;
	s.run(6);
	CHECK(s.violations == 0);
	CHECK(s.out().crop.right() <= 1280.01f);
	CHECK(s.out().status == FramingStatus::Tracking);
}

TEST(framing_toggling_debug_settings_live_does_not_move_the_shot)
{
	AutoFrameConfig c = testConfig(FramingPreset::Preacher);
	c.pulpitEnabled = false;
	Sim s(c);
	// preacher stands still slightly off-centre, inside the dead zone
	s.world = [](double) { return std::vector<Detection>{person(1000, 280, 560)}; };
	s.run(8);
	const RectF before = s.out().crop;
	// operator ticks "show boxes", "show stats", changes detection rate and threads on air
	AutoFrameConfig c2 = c;
	c2.showBoxes = c2.showStats = c2.showGuides = true;
	c2.detectionFps = 8.0f;
	c2.threads = 4;
	c2.sharpen = 0.3f;
	s.fr.configure(c2);
	float maxMove = 0;
	for (int i = 0; i < 30 * 4; ++i) {
		s.step();
		maxMove = std::max(maxMove, std::fabs(s.out().crop.cx() - before.cx()));
	}
	CHECK_MSG(maxMove < 1.0f, "shot moved %.1f px after toggling non-framing settings", maxMove);
}

TEST(framing_operator_overrides)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	Sim s(c);
	s.world = walker;
	s.run(6);

	// pause: crop freezes even though the subject keeps walking
	s.fr.togglePause();
	s.run(0.5); // let velocity bleed off
	const RectF frozen = s.out().crop;
	s.run(3);
	CHECK(s.out().status == FramingStatus::Paused);
	CHECK(std::fabs(s.out().crop.cx() - frozen.cx()) < 25.0f);
	s.fr.togglePause();

	// force wide: goes to zoom 1.0 (whole camera image)
	s.fr.toggleForceWide();
	s.run(8);
	CHECK(s.out().status == FramingStatus::ForcedWide);
	CHECK_NEAR(s.out().zoom, 1.0, 0.01);
	s.fr.toggleForceWide();

	// lock position + manual zoom
	s.run(6);
	s.fr.toggleLockPosition();
	const float z0 = s.out().zoom;
	s.fr.zoomStep(1.1f);
	s.run(6);
	CHECK(s.out().status == FramingStatus::PositionLocked);
	CHECK_NEAR(s.out().zoom, std::min(z0 * 1.1f, 1.4f), 0.02);
	s.fr.resetZoom();
	s.run(8);
	CHECK_NEAR(s.out().zoom, 1.0, 0.01);
	s.fr.toggleLockPosition();

	// reset framing resumes automatic tracking
	s.fr.resetFraming();
	s.run(6);
	CHECK(s.out().status == FramingStatus::Tracking);
	CHECK(s.violations == 0);
}

TEST(framing_manual_person_selection_and_cycling)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.policy = TargetPolicy::Largest;
	Sim s(c);
	s.world = [](double) { return std::vector<Detection>{person(400, 280, 600), person(1500, 300, 520)}; };
	s.run(3);
	const int largest = s.out().targetId;
	const Track *t = s.tracker.find(largest);
	CHECK(t && t->box.cx() < 960); // the bigger one on the left
	s.fr.cycleTarget(s.tracker, s.t, +1);
	s.run(6);
	const Track *t2 = s.tracker.find(s.out().targetId);
	CHECK(t2 && t2->box.cx() > 960);
	CHECK(s.out().crop.cx() > 1100);
}
