// Lost-subject recovery: hold, return to wide, re-acquire, detector failure.
#include "sim.hpp"

using namespace vmaf;

TEST(recovery_holds_then_returns_to_wide_smoothly)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.lostTimeout = 2.0f;
	c.returnToWide = true;
	Sim s(c);
	s.world = [](double t) {
		if (t < 6)
			return std::vector<Detection>{person(1300, 280, 560)};
		return std::vector<Detection>{}; // leaves the stage
	};
	s.run(6);
	CHECK(s.out().status == FramingStatus::Tracking);
	const RectF before = s.out().crop;
	const float zoomBefore = s.out().zoom;
	CHECK(zoomBefore > 1.3f);

	// within the timeout: hold the framing exactly (no jump, no zoom)
	bool sawHolding = false;
	while (s.t < 7.7) {
		s.step();
		sawHolding |= s.out().status == FramingStatus::Holding;
		CHECK(std::fabs(s.out().crop.cx() - before.cx()) < 3.0f);
		CHECK(std::fabs(s.out().zoom - zoomBefore) < 0.01f);
	}
	CHECK(sawHolding);

	// after the timeout: glide to the wide shot, zoom decreasing monotonically
	float prevZoom = s.out().zoom;
	bool monotonic = true, sawReturning = false;
	for (int i = 0; i < 30 * 10; ++i) {
		s.step();
		sawReturning |= s.out().status == FramingStatus::Returning;
		if (s.out().zoom > prevZoom + 1e-4f)
			monotonic = false;
		prevZoom = s.out().zoom;
	}
	CHECK(sawReturning);
	CHECK(monotonic);
	CHECK_NEAR(s.out().zoom, 1.0, 0.01);
	CHECK(s.violations == 0);
}

TEST(recovery_reacquires_same_person_after_short_occlusion)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.lostTimeout = 3.0f;
	Sim s(c);
	s.world = [](double t) {
		std::vector<Detection> d;
		if (t < 5 || t > 6.5) // hidden behind a pillar for 1.5 s
			d.push_back(person(800, 280, 560));
		d.push_back(person(1600, 300, 500)); // someone else on stage the whole time
		return d;
	};
	s.run(5);
	const int id = s.out().targetId;
	s.run(1.5);
	CHECK(s.out().status == FramingStatus::Holding);
	CHECK(s.out().targetId == id); // did not jump to the other person
	s.run(2);
	CHECK(s.out().status == FramingStatus::Tracking);
	CHECK(s.out().targetId == id);
}

TEST(recovery_reacquires_when_tracker_issued_a_new_id)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.lostTimeout = 3.0f; // tracker keeps lost identities for max(4, 3 + 2) = 5 s
	Sim s(c);
	// hidden for 6.5 s (longer than the tracker keeps lost tracks) then reappears nearby
	s.world = [](double t) {
		if (t < 4 || t > 10.5)
			return std::vector<Detection>{person(t < 4 ? 700.0f : 760.0f, 280, 560)};
		return std::vector<Detection>{};
	};
	s.run(4);
	const int id = s.out().targetId;
	s.run(10);
	CHECK(s.out().status == FramingStatus::Tracking);
	CHECK(s.out().targetId != id); // new tracker ID ...
	CHECK(std::fabs(s.out().subject.cx() - 760) < 40); // ... but it is the same place / person
}

TEST(recovery_long_timeout_keeps_the_same_identity)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.lostTimeout = 10.0f; // tracker keeps lost identities for 12 s
	Sim s(c);
	s.world = [](double t) {
		if (t < 4 || t > 9)
			return std::vector<Detection>{person(t < 4 ? 700.0f : 760.0f, 280, 560)};
		return std::vector<Detection>{};
	};
	s.run(4);
	const int id = s.out().targetId;
	s.run(8);
	CHECK(s.out().status == FramingStatus::Tracking);
	CHECK(s.out().targetId == id);
}

TEST(recovery_detector_unavailable_goes_wide_and_keeps_video_inside_bounds)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	Sim s(c);
	s.world = [](double) { return std::vector<Detection>{person(1300, 280, 560)}; };
	s.run(5);
	s.detectorReady = false; // model failed / reloading
	s.run(8);
	CHECK(s.out().status == FramingStatus::NoDetector);
	CHECK_NEAR(s.out().zoom, 1.0, 0.01);
	CHECK(s.violations == 0);
}

TEST(recovery_without_return_to_wide_holds_indefinitely)
{
	AutoFrameConfig c = testConfig(FramingPreset::Medium);
	c.lostTimeout = 1.0f;
	c.returnToWide = false;
	Sim s(c);
	s.world = [](double t) { return t < 4 ? std::vector<Detection>{person(1300, 280, 560)} : std::vector<Detection>{}; };
	s.run(4);
	const RectF a = s.out().crop;
	s.run(10);
	CHECK(s.out().status == FramingStatus::Holding);
	CHECK(std::fabs(s.out().crop.cx() - a.cx()) < 5.0f);
}
